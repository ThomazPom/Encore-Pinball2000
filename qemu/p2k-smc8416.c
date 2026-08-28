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
#include "exec/address-spaces.h"

#define TYPE_P2K_SMC8416 "p2k-smc8416"
#define P2K_SMC_IOSIZE       0x10u
#define P2K_SMC_SHMEM_BASE   0x000d0000u
#define P2K_SMC_SHMEM_SIZE   0x00002000u

OBJECT_DECLARE_SIMPLE_TYPE(P2KSMCState, P2K_SMC8416)

struct P2KSMCState {
    ISADevice parent_obj;
    uint32_t iobase;
    uint32_t isairq;
    uint8_t asic_regs[P2K_SMC_IOSIZE];
    uint8_t prom[8];
    MemoryRegion asic_io;
    MemoryRegion shared_mem;
    NE2000State dp8390;
};

static ssize_t p2k_smc_receive(NetClientState *nc, const uint8_t *buf,
                               size_t size)
{
    NE2000State *dp = qemu_get_nic_opaque(nc);
    uint8_t saved_prom[12];
    ssize_t ret;

    /* Upstream NE2000 uses its duplicated PROM bytes for destination-MAC
     * filtering.  EtherEZ exposes packet RAM from offset zero, so XINA's TX
     * buffer legitimately overwrites those bytes.  Real DP8390 hardware
     * filters against the page-1 physical-address registers instead.  Supply
     * that view only while the common receive engine performs its filter. */
    memcpy(saved_prom, dp->mem, sizeof(saved_prom));
    for (unsigned i = 0; i < 6; i++) {
        dp->mem[i * 2] = dp->phys[i];
    }
    ret = ne2000_receive(nc, buf, size);
    memcpy(dp->mem, saved_prom, sizeof(saved_prom));
    return ret;
}

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
    isa_register_ioport(isadev, &dp->io, s->iobase + 0x10);
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
}

static const Property p2k_smc_properties[] = {
    DEFINE_PROP_UINT32("iobase", P2KSMCState, iobase, 0x300),
    DEFINE_PROP_UINT32("irq", P2KSMCState, isairq, 7),
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
