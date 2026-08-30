/*
 * Pinball 2000 EtherEZ-compatible ISA network adapter.
 *
 * XINA drives the SMC8216/8416 as a WD80x3 front-end around a DP8390:
 * ASIC registers at 0x300, DP8390 registers at 0x310, IRQ 7, and an
 * 8 KiB shared-memory window at 0xD0000.  QEMU already provides the tested
 * DP8390 packet engine; this file supplies only the small WD/SMC front-end
 * and maps its packet RAM into the guest address space.
 */

#include "qemu/osdep.h"
#include "exec/memory.h"
#include "hw/isa/isa.h"
#include "hw/net/ne2000.h"
#include "net/net.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "exec/address-spaces.h"
#include "p2k-internal.h"
#include <slirp/libslirp.h>

#define TYPE_P2K_SMC8416 "p2k-smc8416"
#define P2K_SMC_IOSIZE       0x10u
#define P2K_SMC_SHMEM_BASE   0x000d0000u
#define P2K_SMC_SHMEM_SIZE   0x00002000u
#define P2K_SMC_MAX_AUTO_FWDS 16

OBJECT_DECLARE_SIMPLE_TYPE(P2KSMCState, P2K_SMC8416)

/* The user netdev embeds NetClientState first, followed by its queue link and
 * public libslirp handle.  Keep this deliberately tiny compatibility view: the
 * card needs no private Slirp implementation detail. */
typedef struct P2KSlirpPeer P2KSlirpPeer;
struct P2KSlirpPeer {
    NetClientState nc;
    QTAILQ_ENTRY(P2KSlirpPeer) entry;
    Slirp *slirp;
};

typedef struct P2KAutoHostFwd {
    struct sockaddr_in host;
    struct sockaddr_in guest;
    bool active;
} P2KAutoHostFwd;

struct P2KSMCState {
    ISADevice parent_obj;
    uint32_t iobase;
    uint32_t isairq;
    uint8_t asic_regs[P2K_SMC_IOSIZE];
    uint8_t prom[8];
    MemoryRegion asic_io;
    MemoryRegion dp8390_io;
    MemoryRegion shared_mem;
    NE2000State dp8390;
    bool proxy_arp;
    bool proxy_arp_all;
    bool gateway_mac_valid;
    uint8_t gateway_mac[6];
    char *auto_hostfwd;
    struct in_addr current_guest_ip;
    P2KAutoHostFwd auto_fwds[P2K_SMC_MAX_AUTO_FWDS];
    unsigned auto_fwd_count;
};

static P2KSMCState *p2k_auto_smc;

static ssize_t p2k_smc_receive(NetClientState *nc, const uint8_t *buf,
                               size_t size);

static uint16_t p2k_net_be16(const uint8_t *p)
{
    return ((uint16_t)p[0] << 8) | p[1];
}

static void p2k_net_put_be16(uint8_t *p, uint16_t value)
{
    p[0] = value >> 8;
    p[1] = value;
}

static Slirp *p2k_smc_slirp(P2KSMCState *s)
{
    NetClientState *nic = qemu_get_queue(s->dp8390.nic);
    NetClientState *peer = nic ? nic->peer : NULL;

    if (!peer || !peer->info || peer->info->type != NET_CLIENT_DRIVER_USER ||
        !peer->model || strcmp(peer->model, "user")) {
        return NULL;
    }
    return ((P2KSlirpPeer *)peer)->slirp;
}

static void p2k_smc_retarget_hostfwds(P2KSMCState *s,
                                       struct in_addr guest_ip)
{
    Slirp *slirp;

    if (!s->auto_fwd_count ||
        guest_ip.s_addr == s->current_guest_ip.s_addr) {
        return;
    }
    slirp = p2k_smc_slirp(s);
    if (!slirp) {
        return;
    }

    for (unsigned i = 0; i < s->auto_fwd_count; i++) {
        P2KAutoHostFwd *fwd = &s->auto_fwds[i];

        if (fwd->active) {
#if SLIRP_CHECK_VERSION(4, 5, 0)
            slirp_remove_hostxfwd(slirp, (struct sockaddr *)&fwd->host,
                                  sizeof(fwd->host), 0);
#else
            slirp_remove_hostfwd(slirp, false, fwd->host.sin_addr,
                                 ntohs(fwd->host.sin_port));
#endif
            fwd->active = false;
        }
        fwd->guest.sin_addr = guest_ip;
#if SLIRP_CHECK_VERSION(4, 5, 0)
        if (slirp_add_hostxfwd(slirp,
                (struct sockaddr *)&fwd->host, sizeof(fwd->host),
                (struct sockaddr *)&fwd->guest, sizeof(fwd->guest), 0) < 0) {
#else
        if (slirp_add_hostfwd(slirp, false,
                fwd->host.sin_addr, ntohs(fwd->host.sin_port),
                fwd->guest.sin_addr, ntohs(fwd->guest.sin_port)) < 0) {
#endif
            warn_report("p2k-smc8416: could not retarget host TCP %u",
                        ntohs(fwd->host.sin_port));
        } else {
            fwd->active = true;
        }
    }
    s->current_guest_ip = guest_ip;
    info_report("p2k-smc8416: automatic forwards now target XINA %s",
                inet_ntoa(guest_ip));
}

bool p2k_smc_auto_ip_requested(void)
{
    return p2k_auto_smc && p2k_auto_smc->auto_fwd_count &&
           p2k_auto_smc->current_guest_ip.s_addr == INADDR_ANY;
}

void p2k_smc_auto_ip_discovered(const char *address)
{
    struct in_addr guest_ip;

    if (p2k_auto_smc && inet_aton(address, &guest_ip)) {
        p2k_smc_retarget_hostfwds(p2k_auto_smc, guest_ip);
    }
}

static void p2k_smc_inspect_tx(P2KSMCState *s, const uint8_t *packet,
                               size_t size)
{
    struct in_addr source;
    uint16_t ethertype;

    if (!s->auto_fwd_count || size < 14) {
        return;
    }
    ethertype = p2k_net_be16(packet + 12);
    if (ethertype == 0x0800 && size >= 14 + 20 &&
        (packet[14] >> 4) == 4) {
        memcpy(&source, packet + 26, sizeof(source));
        p2k_smc_retarget_hostfwds(s, source);
    } else if (ethertype == 0x0806 && size >= 42 &&
               p2k_net_be16(packet + 16) == 0x0800 &&
               packet[18] == 6 && packet[19] == 4) {
        memcpy(&source, packet + 28, sizeof(source));
        p2k_smc_retarget_hostfwds(s, source);
    }
}

static void p2k_smc_parse_auto_hostfwds(P2KSMCState *s, Error **errp)
{
    g_auto(GStrv) entries = NULL;

    if (!s->auto_hostfwd || !*s->auto_hostfwd) {
        return;
    }
    entries = g_strsplit(s->auto_hostfwd, "|", -1);
    for (char **entry = entries; *entry; entry++) {
        g_auto(GStrv) fields = g_strsplit(*entry, ":", 3);
        char *end = NULL;
        long host_port, guest_port;
        P2KAutoHostFwd *fwd;

        if (s->auto_fwd_count == P2K_SMC_MAX_AUTO_FWDS ||
            !fields[0] || !fields[1] || !fields[2] || fields[3] ||
            !*fields[0]) {
            error_setg(errp, "invalid p2k-smc8416 auto-hostfwd '%s'", *entry);
            return;
        }
        host_port = strtol(fields[1], &end, 10);
        if (*fields[1] == '\0' || *end != '\0' || host_port < 1 ||
            host_port > 65535) {
            error_setg(errp, "invalid auto-hostfwd host port '%s'", fields[1]);
            return;
        }
        guest_port = strtol(fields[2], &end, 10);
        if (*fields[2] == '\0' || *end != '\0' || guest_port < 1 ||
            guest_port > 65535) {
            error_setg(errp, "invalid auto-hostfwd guest port '%s'", fields[2]);
            return;
        }
        fwd = &s->auto_fwds[s->auto_fwd_count];
        fwd->host.sin_family = AF_INET;
        fwd->guest.sin_family = AF_INET;
        if (!inet_aton(fields[0], &fwd->host.sin_addr)) {
            error_setg(errp, "invalid auto-hostfwd bind address '%s'", fields[0]);
            return;
        }
        fwd->host.sin_port = htons(host_port);
        fwd->guest.sin_port = htons(guest_port);
        s->auto_fwd_count++;
    }
}

static void p2k_smc_proxy_arp_tx(P2KSMCState *s, const uint8_t *packet,
                                 size_t size)
{
    static const uint8_t slirp_default_mac[6] = {
        0x52, 0x55, 0x0a, 0x00, 0x02, 0x02
    };
    uint8_t reply[42] = { 0 };
    const uint8_t *proxy_mac;
    const uint8_t *guest_mac;
    const uint8_t *guest_ip;
    const uint8_t *target_ip;

    if (!s->proxy_arp || size < sizeof(reply) ||
        p2k_net_be16(packet + 12) != 0x0806 ||
        p2k_net_be16(packet + 14) != 1 ||
        p2k_net_be16(packet + 16) != 0x0800 ||
        packet[18] != 6 || packet[19] != 4 ||
        p2k_net_be16(packet + 20) != 1) {
        return;
    }

    if (s->proxy_arp_all) {
        /* QEMU's default libslirp host is 10.0.2.2.  libslirp derives its
         * Ethernet address as 52:55:<IPv4>, hence 52:55:0a:00:02:02.
         * The Ethernet destination is only the entrance to Slirp: the IP
         * packet itself keeps XINA's arbitrary source and real destination. */
        proxy_mac = slirp_default_mac;
    } else if (s->gateway_mac_valid) {
        proxy_mac = s->gateway_mac;
    } else {
        return;
    }

    guest_mac = packet + 22;
    guest_ip = packet + 28;
    target_ip = packet + 38;
    memcpy(reply, guest_mac, 6);
    memcpy(reply + 6, proxy_mac, 6);
    p2k_net_put_be16(reply + 12, 0x0806);
    p2k_net_put_be16(reply + 14, 1);
    p2k_net_put_be16(reply + 16, 0x0800);
    reply[18] = 6;
    reply[19] = 4;
    p2k_net_put_be16(reply + 20, 2);
    memcpy(reply + 22, proxy_mac, 6);
    memcpy(reply + 28, target_ip, 4);
    memcpy(reply + 32, guest_mac, 6);
    memcpy(reply + 38, guest_ip, 4);

    /* Use the card's receive frontend, not ne2000_receive() directly: the
     * frontend temporarily restores the physical-address filter bytes that
     * EtherEZ shared-memory traffic may overwrite. */
    p2k_smc_receive(qemu_get_queue(s->dp8390.nic), reply, sizeof(reply));
}

static ssize_t p2k_smc_receive(NetClientState *nc, const uint8_t *buf,
                               size_t size)
{
    NE2000State *dp = qemu_get_nic_opaque(nc);
    P2KSMCState *s = container_of(dp, P2KSMCState, dp8390);
    uint8_t saved_prom[12];
    ssize_t ret;

    /* Upstream NE2000 uses its duplicated PROM bytes for destination-MAC
     * filtering.  EtherEZ exposes packet RAM from offset zero, so XINA's TX
     * buffer legitimately overwrites those bytes.  Real DP8390 hardware
     * filters against the page-1 physical-address registers instead.  Supply
     * that view only while the common receive engine performs its filter. */
    if (s->proxy_arp && !s->proxy_arp_all && size >= 14 &&
        p2k_net_be16(buf + 12) == 0x0800) {
        memcpy(s->gateway_mac, buf + 6, sizeof(s->gateway_mac));
        s->gateway_mac_valid = true;
    }

    memcpy(saved_prom, dp->mem, sizeof(saved_prom));
    for (unsigned i = 0; i < 6; i++) {
        dp->mem[i * 2] = dp->phys[i];
    }
    ret = ne2000_receive(nc, buf, size);
    memcpy(dp->mem, saved_prom, sizeof(saved_prom));
    return ret;
}

static uint64_t p2k_smc_dp8390_read(void *opaque, hwaddr off, unsigned size)
{
    P2KSMCState *s = opaque;
    return s->dp8390.io.ops->read(s->dp8390.io.opaque, off, size);
}

static void p2k_smc_dp8390_write(void *opaque, hwaddr off, uint64_t value,
                                 unsigned size)
{
    P2KSMCState *s = opaque;
    NE2000State *dp = &s->dp8390;

    /* The DP8390 transmits synchronously when TRANS is written to its command
     * register. Inspect the still-resident TX buffer immediately beforehand;
     * the upstream engine remains completely unchanged. */
    if (size == 1 && off == 0 && (value & 0x04)) {
        uint32_t index = dp->tpsr << 8;
        if (index >= NE2000_PMEM_END) {
            index -= NE2000_PMEM_SIZE;
        }
        if (index + dp->tcnt <= NE2000_PMEM_END) {
            p2k_smc_inspect_tx(s, dp->mem + index, dp->tcnt);
            p2k_smc_proxy_arp_tx(s, dp->mem + index, dp->tcnt);
        }
    }
    dp->io.ops->write(dp->io.opaque, off, value, size);
}

static const MemoryRegionOps p2k_smc_dp8390_ops = {
    .read = p2k_smc_dp8390_read,
    .write = p2k_smc_dp8390_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 1 },
};

static NetClientInfo p2k_smc_net_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .receive = p2k_smc_receive,
};

static uint64_t p2k_smc_asic_read(void *opaque, hwaddr off, unsigned size)
{
    P2KSMCState *s = opaque;

    if (size != 1 || off >= P2K_SMC_IOSIZE) {
        return UINT64_MAX;
    }
    if (off >= 8) {
        if (s->asic_regs[4] & 0x80) {
            /* 83C790 interrupt-select encoding: index 4 is IRQ7. */
            if (off == 0x0d) {
                return 0x40;
            }
            if (off == 0x0b) {
                return s->asic_regs[0x0b];
            }
        }
        return s->prom[off - 8];
    }
    return s->asic_regs[off];
}

static void p2k_smc_asic_write(void *opaque, hwaddr off,
                               uint64_t value, unsigned size)
{
    P2KSMCState *s = opaque;

    if (size != 1 || off >= P2K_SMC_IOSIZE) {
        return;
    }
    if (off >= 8 && !(s->asic_regs[4] & 0x80)) {
        return;
    }
    s->asic_regs[off] = value;
    if (off == 0 && (value & 0x80)) {
        ne2000_reset(&s->dp8390);
    }
}

static const MemoryRegionOps p2k_smc_asic_ops = {
    .read = p2k_smc_asic_read,
    .write = p2k_smc_asic_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 1 },
};

static uint64_t p2k_smc_shared_read(void *opaque, hwaddr off, unsigned size)
{
    P2KSMCState *s = opaque;
    uint8_t *p = &s->dp8390.mem[off];

    if (off + size > P2K_SMC_SHMEM_SIZE) {
        return UINT64_MAX;
    }
    switch (size) {
    case 1: return *p;
    case 2: return lduw_le_p(p);
    case 4: return ldl_le_p(p);
    default: return UINT64_MAX;
    }
}

static void p2k_smc_shared_write(void *opaque, hwaddr off,
                                 uint64_t value, unsigned size)
{
    P2KSMCState *s = opaque;
    uint8_t *p = &s->dp8390.mem[off];

    if (off + size > P2K_SMC_SHMEM_SIZE) {
        return;
    }
    switch (size) {
    case 1: *p = value; break;
    case 2: stw_le_p(p, value); break;
    case 4: stl_le_p(p, value); break;
    }
}

static const MemoryRegionOps p2k_smc_shared_ops = {
    .read = p2k_smc_shared_read,
    .write = p2k_smc_shared_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl = { .min_access_size = 1, .max_access_size = 4 },
};

static void p2k_smc_realize(DeviceState *dev, Error **errp)
{
    P2KSMCState *s = P2K_SMC8416(dev);
    ISADevice *isadev = ISA_DEVICE(dev);
    NE2000State *dp = &s->dp8390;
    uint8_t sum = 0;

    p2k_smc_parse_auto_hostfwds(s, errp);
    if (*errp) {
        return;
    }

    memcpy(s->prom, dp->c.macaddr.a, 6);
    s->prom[6] = 0x2a; /* SMC8216/8416 family identifier. */
    for (unsigned i = 0; i < 7; i++) {
        sum += s->prom[i];
    }
    s->prom[7] = 0xff - sum;

    memory_region_init_io(&s->asic_io, OBJECT(dev), &p2k_smc_asic_ops, s,
                          "p2k-smc8416.asic", P2K_SMC_IOSIZE);
    isa_register_ioport(isadev, &s->asic_io, s->iobase);

    ne2000_setup_io(dp, dev, 0x10);
    memory_region_init_io(&s->dp8390_io, OBJECT(dev), &p2k_smc_dp8390_ops, s,
                          "p2k-smc8416.dp8390", 0x10);
    isa_register_ioport(isadev, &s->dp8390_io, s->iobase + 0x10);
    dp->irq = isa_get_irq(isadev, s->isairq);
    ne2000_reset(dp);

    memory_region_init_io(&s->shared_mem, OBJECT(dev), &p2k_smc_shared_ops, s,
                          "p2k-smc8416.shared", P2K_SMC_SHMEM_SIZE);
    memory_region_add_subregion_overlap(get_system_memory(),
                                        P2K_SMC_SHMEM_BASE,
                                        &s->shared_mem, 2);

    dp->nic = qemu_new_nic(&p2k_smc_net_info, &dp->c,
                           object_get_typename(OBJECT(dev)), dev->id,
                           &dev->mem_reentrancy_guard, dp);
    qemu_format_nic_info_str(qemu_get_queue(dp->nic), dp->c.macaddr.a);
    if (s->proxy_arp_all || s->auto_fwd_count) {
        p2k_auto_smc = s;
    }
}

static void p2k_smc_unrealize(DeviceState *dev)
{
    P2KSMCState *s = P2K_SMC8416(dev);

    if (p2k_auto_smc == s) {
        p2k_auto_smc = NULL;
    }
}

static const Property p2k_smc_properties[] = {
    DEFINE_PROP_UINT32("iobase", P2KSMCState, iobase, 0x300),
    DEFINE_PROP_UINT32("irq", P2KSMCState, isairq, 7),
    DEFINE_PROP_BOOL("proxy-arp", P2KSMCState, proxy_arp, false),
    DEFINE_PROP_BOOL("proxy-arp-all", P2KSMCState, proxy_arp_all, false),
    DEFINE_PROP_STRING("auto-hostfwd", P2KSMCState, auto_hostfwd),
    DEFINE_NIC_PROPERTIES(P2KSMCState, dp8390.c),
};

static void p2k_smc_instance_init(Object *obj)
{
    P2KSMCState *s = P2K_SMC8416(obj);
    static const uint8_t mac[6] = { 0x00, 0x00, 0xc0, 0x01, 0x02, 0x03 };

    memcpy(s->dp8390.c.macaddr.a, mac, sizeof(mac));
}

static void p2k_smc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = p2k_smc_realize;
    dc->unrealize = p2k_smc_unrealize;
    device_class_set_props(dc, p2k_smc_properties);
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

static const TypeInfo p2k_smc_type_info = {
    .name = TYPE_P2K_SMC8416,
    .parent = TYPE_ISA_DEVICE,
    .instance_size = sizeof(P2KSMCState),
    .instance_init = p2k_smc_instance_init,
    .class_init = p2k_smc_class_init,
};

static void p2k_smc_register_types(void)
{
    type_register_static(&p2k_smc_type_info);
}

type_init(p2k_smc_register_types)
