/*
 * Volatile, cross-ROM guest extensions.
 *
 * Nothing here changes an update image or saved flash.  Once the update
 * loader has copied a supported game to RAM, structural signatures resolve
 * the small import set and a sub-2-KiB payload is installed at 0x00ff0000.
 * A six-byte netstart prologue is intercepted once; the payload registers its
 * shell command, restores those original bytes, and enters real netstart.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "exec/cpu-common.h"

#include "p2k-internal.h"
#include "p2k-guest-extension-payload.inc"

#define GE_SCAN_BASE       0x00100000u
#define GE_SCAN_LENGTH     0x00400000u
#define GE_PAYLOAD_BASE    0x00ff0000u
#define GE_ENTRY_OFFSET    0x00000100u
#define GE_FACTORY_ENTRY_OFFSET 0x00000400u
#define GE_MAGIC           0x58454750u
#define GE_HOOK_LENGTH     6u
#define GE_FACTORY_HOOK_LENGTH 9u
#define GE_UDP_TTL_IMMEDIATE_OFFSET 15u
#define GE_UDP_TTL_DEFAULT     1u
#define GE_UDP_TTL_SLIRP       64u

enum {
    GE_O_MAGIC          = 0x00,
    GE_O_SHELL_CMD_ADD  = 0x08,
    GE_O_PUT_VALUE      = 0x0c,
    GE_O_NETSTART       = 0x10,
    GE_O_IP_RESOURCE    = 0x14,
    GE_O_MASK_RESOURCE  = 0x18,
    GE_O_GW_RESOURCE    = 0x1c,
    GE_O_STARTUP_ENABLE = 0x20,
    GE_O_STARTUP_IP     = 0x24,
    GE_O_STARTUP_MASK   = 0x28,
    GE_O_STARTUP_GW     = 0x2c,
    GE_O_ORIGINAL_LEN   = 0x30,
    GE_O_ORIGINAL       = 0x34,
    GE_O_FACTORY_RESET  = 0x3c,
    GE_O_FACTORY_ORIG_LEN = 0x40,
    GE_O_FACTORY_ORIGINAL = 0x44,
    GE_O_DNS_RESOURCE   = 0x58,
    GE_O_STARTUP_DNS_ENABLE = 0x5c,
    GE_O_STARTUP_DNS    = 0x60,
    GE_O_TOURNEY_IP_RESOURCE = 0x64,
    GE_O_TOURNAMENT_RESOURCE = 0x68,
    GE_O_FREE_PLAY_RESOURCE = 0x6c,
    GE_O_STARTUP_TOURNAMENT_ENABLE = 0x70,
    GE_O_STARTUP_TOURNEY_IP = 0x74,
    GE_O_STARTUP_TOURNAMENT_ON = 0x78,
    GE_O_STARTUP_FREE_PLAY = 0x7c,
};

typedef enum GuestEnvStatus {
    GE_ENV_ABSENT,
    GE_ENV_VALID,
    GE_ENV_INVALID,
} GuestEnvStatus;

typedef struct MaskedPattern {
    const uint8_t *bytes;
    const uint8_t *mask;
    size_t length;
} MaskedPattern;

static bool ge_enabled;
static bool ge_installed;
static bool ge_retired;
static bool ge_udp_ttl_done;

static const uint8_t shell_bytes[] = {
    0x55,0x89,0xe5,0x83,0xec,0x04,0x57,0x56,0x53,0xc7,0x45,0xfc,
    0xff,0xff,0xff,0xff,0x8b,0x35,0,0,0,0,0xbf,0x30,0,0,0,0x8b,0x1d,0,0,0,0,
};
static const uint8_t shell_mask[] = {
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,1,1,1,1,1,1,1,0,0,0,0,
};

static const uint8_t put_bytes[] = {
    0x55,0x89,0xe5,0x56,0x53,0x8b,0x75,0x08,0x8d,0x45,0x0c,0x50,
    0xff,0x76,0x04,0xff,0x36,0x68,0,0,0,0,0xe8,0,0,0,0,0x89,0xc3,
    0x83,0xc4,0x10,0x85,0xdb,0x75,0x0b,0x56,0x68,0,0,0,0,0xe8,0,0,0,0,
    0x89,0xd8,0x8d,0x65,0xf8,0x5b,0x5e,0xc9,0xc3,
};
static const uint8_t put_mask[] = {
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,1,0,0,0,0,1,1,
    1,1,1,1,1,1,1,1,1,0,0,0,0,1,0,0,0,0,1,1,1,1,1,1,1,1,1,
};

/* This is the first byte-order conversion in modern netstart.  It is unique
 * in each preserved network-capable update and sits exactly 0x2d bytes after
 * the function entry. */
static const uint8_t netstart_anchor[] = {
    0x89,0xf0,0xc1,0xe0,0x18,0x89,0xf2,0xc1,0xea,0x18,0x09,0xc2,
    0x89,0xf0,0x25,0,0,0xff,0,0xc1,0xe8,0x08,0x09,0xd0,
    0x81,0xe6,0,0xff,0,0,0xc1,0xe6,0x08,0x09,0xc6,
};

static const uint8_t factory_reset_message[] =
    "*** Automatic Factory Reset underway";

/* All preserved network-capable XINA images initialise udpsend()'s unicast
 * TTL through this unique instruction sequence.  Multicast may subsequently
 * replace EBX from its route, so changing this immediate repairs only the
 * common unicast default. */
static const uint8_t udp_ttl_anchor[] = {
    0x83,0xc4,0x0c,0x66,0x85,0xc0,0x75,0x06,0x66,0xc7,0x46,0x06,0xff,0xff,
    0xbb,0x01,0x00,0x00,0x00,0x8b,0x45,0x08,0x25,0xf0,0x00,0x00,0x00,
    0x3d,0xe0,0x00,0x00,0x00,0x75,0x2e,
};

static uint32_t ld32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void st32(uint8_t *p, uint32_t value)
{
    p[0] = value; p[1] = value >> 8; p[2] = value >> 16; p[3] = value >> 24;
}

static uint8_t *find_masked(uint8_t *buf, size_t size, MaskedPattern pat)
{
    for (size_t off = 0; off + pat.length <= size; off++) {
        size_t i;
        for (i = 0; i < pat.length; i++) {
            if (pat.mask[i] && buf[off + i] != pat.bytes[i]) {
                break;
            }
        }
        if (i == pat.length) {
            return buf + off;
        }
    }
    return NULL;
}

static uint8_t *find_exact(uint8_t *buf, size_t size,
                           const uint8_t *needle, size_t length)
{
    for (size_t off = 0; off + length <= size; off++) {
        if (!memcmp(buf + off, needle, length)) {
            return buf + off;
        }
    }
    return NULL;
}

static uint8_t *find_exact_unique(uint8_t *buf, size_t size,
                                  const uint8_t *needle, size_t length)
{
    uint8_t *match = NULL;

    for (size_t off = 0; off + length <= size; off++) {
        if (memcmp(buf + off, needle, length)) {
            continue;
        }
        if (match) {
            return NULL;
        }
        match = buf + off;
    }
    return match;
}

static uint8_t *find_udp_ttl_unique(uint8_t *buf, size_t size,
                                    uint32_t *current_ttl)
{
    uint8_t *match = NULL;

    for (size_t off = 0; off + sizeof(udp_ttl_anchor) <= size; off++) {
        uint32_t value;

        if (memcmp(buf + off, udp_ttl_anchor, GE_UDP_TTL_IMMEDIATE_OFFSET) ||
            memcmp(buf + off + GE_UDP_TTL_IMMEDIATE_OFFSET + 4,
                   udp_ttl_anchor + GE_UDP_TTL_IMMEDIATE_OFFSET + 4,
                   sizeof(udp_ttl_anchor) -
                   GE_UDP_TTL_IMMEDIATE_OFFSET - 4)) {
            continue;
        }
        value = ld32(buf + off + GE_UDP_TTL_IMMEDIATE_OFFSET);
        if (match) {
            return NULL;
        }
        match = buf + off;
        *current_ttl = value;
    }
    if (*current_ttl != GE_UDP_TTL_DEFAULT &&
        *current_ttl != GE_UDP_TTL_SLIRP) {
        return NULL;
    }
    return match;
}

static bool ge_try_patch_udp_ttl(void)
{
    uint8_t *ram = g_malloc(GE_SCAN_LENGTH);
    uint8_t *anchor;
    uint32_t current_ttl = 0;
    uint8_t replacement[4];
    uint32_t address;

    cpu_physical_memory_read(GE_SCAN_BASE, ram, GE_SCAN_LENGTH);
    anchor = find_udp_ttl_unique(ram, GE_SCAN_LENGTH, &current_ttl);
    if (!anchor) {
        g_free(ram);
        return false;
    }
    address = GE_SCAN_BASE + (anchor - ram) + GE_UDP_TTL_IMMEDIATE_OFFSET;
    if (current_ttl == GE_UDP_TTL_DEFAULT) {
        st32(replacement, GE_UDP_TTL_SLIRP);
        cpu_physical_memory_write(address, replacement, sizeof(replacement));
    }
    info_report("pinball2000: Slirp UDP guest extension installed: "
                "udpsend TTL=%u at 0x%08x%s",
                GE_UDP_TTL_SLIRP, address,
                current_ttl == GE_UDP_TTL_SLIRP ? " (already active)" : "");
    g_free(ram);
    return true;
}

static GuestEnvStatus parse_ipv4_env(const char *name, uint32_t *result)
{
    const char *s = getenv(name);
    const char *p;
    uint32_t value = 0;

    if (!s || !*s) {
        return GE_ENV_ABSENT;
    }
    p = s;
    for (unsigned part = 0; part < 4; part++) {
        unsigned octet = 0;
        unsigned digits = 0;

        while (*p >= '0' && *p <= '9') {
            octet = octet * 10 + (*p++ - '0');
            if (++digits > 3 || octet > 255) {
                goto invalid;
            }
        }
        if (!digits || (part < 3 ? *p++ != '.' : *p != '\0')) {
            goto invalid;
        }
        value = value << 8 | octet;
    }
    *result = value;
    return GE_ENV_VALID;

invalid:
    error_report("pinball2000: %s is not an IPv4 address: %s", name, s);
    return GE_ENV_INVALID;
}

static GuestEnvStatus parse_bool_env(const char *name, uint32_t *result)
{
    const char *s = getenv(name);

    if (!s || !*s) {
        return GE_ENV_ABSENT;
    }
    if (!strcmp(s, "0") || !strcmp(s, "1")) {
        *result = s[0] - '0';
        return GE_ENV_VALID;
    }
    error_report("pinball2000: %s is not 0 or 1: %s", name, s);
    return GE_ENV_INVALID;
}

static bool resolve_resources(const uint8_t *netstart, uint32_t ns_addr,
                              uint32_t resources[3], uint32_t *get_value_out)
{
    uint32_t get_value;
    unsigned found = 0;

    if (netstart[9] != 0x68 || netstart[14] != 0xe8) {
        return false;
    }
    get_value = ns_addr + 19 + (int32_t)ld32(netstart + 15);
    for (unsigned i = 0; i + 10 <= 0x300; i++) {
        uint32_t target;
        if (netstart[i] != 0x68 || netstart[i + 5] != 0xe8) {
            continue;
        }
        target = ns_addr + i + 10 + (int32_t)ld32(netstart + i + 6);
        if (target == get_value) {
            if (found < 3) {
                resources[found] = ld32(netstart + i + 1);
            }
            found++;
        }
    }
    if (found != 3) {
        return false;
    }
    *get_value_out = get_value;
    return true;
}

static bool resolve_named_resource(uint8_t *ram, size_t size,
                                   const char *resource_name,
                                   uint32_t *resource_out,
                                   uint32_t *constructor_out)
{
    size_t name_length = strlen(resource_name) + 1;
    uint8_t *name = find_exact_unique(ram, size,
                                      (const uint8_t *)resource_name,
                                      name_length);
    uint8_t *constructor_ref = NULL;
    uint32_t name_addr;
    uint32_t resource;
    uint32_t constructor;

    if (!name) {
        return false;
    }
    name_addr = GE_SCAN_BASE + (name - ram);
    for (size_t off = 0; off + 20 <= size; off++) {
        if (ram[off] != 0x68 || ld32(ram + off + 1) != name_addr ||
            ram[off + 5] != 0x68 || ram[off + 10] != 0x68 ||
            ram[off + 15] != 0xe8) {
            continue;
        }
        if (constructor_ref) {
            return false;
        }
        constructor_ref = ram + off;
    }
    if (!constructor_ref) {
        return false;
    }
    resource = ld32(constructor_ref + 11);
    if (resource < GE_SCAN_BASE || resource > GE_SCAN_BASE + size - 4) {
        return false;
    }
    constructor = GE_SCAN_BASE + (constructor_ref - ram) + 20 +
                  (int32_t)ld32(constructor_ref + 16);
    if (constructor < GE_SCAN_BASE || constructor >= GE_SCAN_BASE + size) {
        return false;
    }
    *resource_out = resource;
    *constructor_out = constructor;
    return true;
}

static unsigned resource_call_count(uint8_t *ram, size_t size,
                                    uint32_t resource, uint32_t target)
{
    unsigned calls = 0;

    for (size_t off = 0; off + 10 <= size; off++) {
        uint32_t call_target;

        if (ram[off] != 0x68 || ld32(ram + off + 1) != resource ||
            ram[off + 5] != 0xe8) {
            continue;
        }
        call_target = GE_SCAN_BASE + off + 10 +
                      (int32_t)ld32(ram + off + 6);
        if (call_target == target) {
            calls++;
        }
    }
    return calls;
}

static uint8_t *resolve_factory_reset(uint8_t *ram, size_t size)
{
    uint8_t *message = find_exact(ram, size, factory_reset_message,
                                  sizeof(factory_reset_message) - 1);
    uint32_t message_addr;

    if (!message) {
        return NULL;
    }
    message_addr = GE_SCAN_BASE + (message - ram);
    for (size_t off = 0; off + 15 <= size; off++) {
        uint32_t target;
        if (ram[off] != 0x68 || ld32(ram + off + 1) != message_addr ||
            ram[off + 5] != 0xe8 || ram[off + 10] != 0xe8) {
            continue;
        }
        target = GE_SCAN_BASE + off + 15 + (int32_t)ld32(ram + off + 11);
        if (target < GE_SCAN_BASE || target + GE_FACTORY_HOOK_LENGTH >
            GE_SCAN_BASE + size) {
            continue;
        }
        uint8_t *fn = ram + (target - GE_SCAN_BASE);
        if (fn[0] == 0x55 && fn[1] == 0x89 && fn[2] == 0xe5) {
            return fn;
        }
    }
    return NULL;
}

static bool ge_try_install(void)
{
    uint8_t *ram = g_malloc(GE_SCAN_LENGTH);
    MaskedPattern shell = { shell_bytes, shell_mask, sizeof(shell_bytes) };
    MaskedPattern put = { put_bytes, put_mask, sizeof(put_bytes) };
    uint8_t *shell_at, *put_at, *anchor_at, *netstart;
    uint8_t *factory_reset = NULL;
    uint32_t resources[3], dns_resource, tourney_ip_resource;
    uint32_t tournament_resource, free_play_resource, get_value;
    uint32_t dns_constructor, tourney_ip_constructor;
    uint32_t tournament_constructor, free_play_constructor;
    uint32_t startup[3], startup_dns, startup_tourney_ip;
    uint32_t startup_tournament_on, startup_free_play;
    uint8_t payload[P2K_GE_PAYLOAD_SIZE];
    uint8_t hook[GE_HOOK_LENGTH] = { 0xe9, 0, 0, 0, 0, 0x90 };
    uint8_t factory_hook[GE_FACTORY_HOOK_LENGTH] = {
        0xe9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90
    };
    GuestEnvStatus ip_status, mask_status, gateway_status, dns_status;
    GuestEnvStatus tourney_ip_status, tournament_on_status, free_play_status;
    bool have_startup, have_startup_dns, have_startup_tournament;
    const char *tournament_suffix = "";

    cpu_physical_memory_read(GE_SCAN_BASE, ram, GE_SCAN_LENGTH);
    shell_at = find_masked(ram, GE_SCAN_LENGTH, shell);
    put_at = find_masked(ram, GE_SCAN_LENGTH, put);
    anchor_at = find_exact(ram, GE_SCAN_LENGTH, netstart_anchor,
                           sizeof(netstart_anchor));
    if (!shell_at || !put_at || !anchor_at || anchor_at < ram + 0x2d) {
        g_free(ram);
        return false;
    }
    netstart = anchor_at - 0x2d;
    if (memcmp(netstart, "\x55\x89\xe5\x83\xec\x08", GE_HOOK_LENGTH) ||
        !resolve_resources(netstart,
                           GE_SCAN_BASE + (netstart - ram), resources,
                           &get_value) ||
        !resolve_named_resource(ram, GE_SCAN_LENGTH, "DNSIPA",
                                &dns_resource, &dns_constructor) ||
        resource_call_count(ram, GE_SCAN_LENGTH, dns_resource,
                            get_value) != 1 ||
        !resolve_named_resource(ram, GE_SCAN_LENGTH, "TS_IPA",
                                &tourney_ip_resource,
                                &tourney_ip_constructor) ||
        !resolve_named_resource(ram, GE_SCAN_LENGTH, "GmTour",
                                &tournament_resource,
                                &tournament_constructor) ||
        !resolve_named_resource(ram, GE_SCAN_LENGTH, "CrdFPl",
                                &free_play_resource,
                                &free_play_constructor) ||
        tourney_ip_constructor != dns_constructor ||
        tournament_constructor != free_play_constructor ||
        tournament_constructor == dns_constructor ||
        dns_resource == tourney_ip_resource ||
        dns_resource == tournament_resource ||
        dns_resource == free_play_resource ||
        tourney_ip_resource == tournament_resource ||
        tourney_ip_resource == free_play_resource ||
        tournament_resource == free_play_resource) {
        g_free(ram);
        return false;
    }

    memcpy(payload, p2k_ge_payload, sizeof(payload));
    st32(payload + GE_O_MAGIC, GE_MAGIC);
    st32(payload + GE_O_SHELL_CMD_ADD,
         GE_SCAN_BASE + (shell_at - ram));
    /* Every scalar Resource<T>::putValue specialization has this ABI and
     * identical persistence semantics; the first structural match suffices. */
    st32(payload + GE_O_PUT_VALUE, GE_SCAN_BASE + (put_at - ram));
    st32(payload + GE_O_NETSTART, GE_SCAN_BASE + (netstart - ram));
    st32(payload + GE_O_IP_RESOURCE, resources[0]);
    st32(payload + GE_O_MASK_RESOURCE, resources[1]);
    st32(payload + GE_O_GW_RESOURCE, resources[2]);
    st32(payload + GE_O_DNS_RESOURCE, dns_resource);
    st32(payload + GE_O_TOURNEY_IP_RESOURCE, tourney_ip_resource);
    st32(payload + GE_O_TOURNAMENT_RESOURCE, tournament_resource);
    st32(payload + GE_O_FREE_PLAY_RESOURCE, free_play_resource);
    st32(payload + GE_O_ORIGINAL_LEN, GE_HOOK_LENGTH);
    memcpy(payload + GE_O_ORIGINAL, netstart, GE_HOOK_LENGTH);

    ip_status = parse_ipv4_env("P2K_GUEST_IP", &startup[0]);
    mask_status = parse_ipv4_env("P2K_GUEST_MASK", &startup[1]);
    gateway_status = parse_ipv4_env("P2K_GUEST_GATEWAY", &startup[2]);
    dns_status = parse_ipv4_env("P2K_GUEST_DNS", &startup_dns);
    tourney_ip_status = parse_ipv4_env("P2K_TOURNAMENT_IP",
                                       &startup_tourney_ip);
    tournament_on_status = parse_bool_env("P2K_TOURNAMENT_ENABLED",
                                           &startup_tournament_on);
    free_play_status = parse_bool_env("P2K_TOURNAMENT_FREE_PLAY",
                                      &startup_free_play);
    if (ip_status == GE_ENV_INVALID || mask_status == GE_ENV_INVALID ||
        gateway_status == GE_ENV_INVALID || dns_status == GE_ENV_INVALID ||
        tourney_ip_status == GE_ENV_INVALID ||
        tournament_on_status == GE_ENV_INVALID ||
        free_play_status == GE_ENV_INVALID) {
        error_report("pinball2000: invalid guest startup value");
        g_free(ram);
        ge_retired = true;
        return false;
    }
    if (ip_status != mask_status || ip_status != gateway_status) {
        error_report("pinball2000: guest IP, mask and gateway must be supplied together");
        g_free(ram);
        ge_retired = true;
        return false;
    }
    if (tourney_ip_status != tournament_on_status ||
        tourney_ip_status != free_play_status) {
        error_report("pinball2000: tournament IP, enabled state and "
                     "free-play state must be supplied together");
        g_free(ram);
        ge_retired = true;
        return false;
    }
    have_startup = ip_status == GE_ENV_VALID;
    have_startup_dns = dns_status == GE_ENV_VALID;
    have_startup_tournament = tourney_ip_status == GE_ENV_VALID;
    if (have_startup) {
        st32(payload + GE_O_STARTUP_ENABLE, 1);
        st32(payload + GE_O_STARTUP_IP, startup[0]);
        st32(payload + GE_O_STARTUP_MASK, startup[1]);
        st32(payload + GE_O_STARTUP_GW, startup[2]);
    }
    if (have_startup_dns) {
        st32(payload + GE_O_STARTUP_DNS_ENABLE, 1);
        st32(payload + GE_O_STARTUP_DNS, startup_dns);
    }
    if (have_startup_tournament) {
        st32(payload + GE_O_STARTUP_TOURNAMENT_ENABLE, 1);
        st32(payload + GE_O_STARTUP_TOURNEY_IP, startup_tourney_ip);
        st32(payload + GE_O_STARTUP_TOURNAMENT_ON,
             startup_tournament_on);
        st32(payload + GE_O_STARTUP_FREE_PLAY, startup_free_play);
        tournament_suffix = startup_tournament_on ?
            (startup_free_play ? " startup tournament=on/free" :
                                 " startup tournament=on/no-free") :
            (startup_free_play ? " startup tournament=off/free" :
                                 " startup tournament=off/no-free");
    }
    if (have_startup || have_startup_dns || have_startup_tournament) {
        factory_reset = resolve_factory_reset(ram, GE_SCAN_LENGTH);
        if (!factory_reset) {
            error_report("pinball2000: automatic factory-reset path was not resolved");
            g_free(ram);
            ge_retired = true;
            return false;
        }
        st32(payload + GE_O_FACTORY_RESET,
             GE_SCAN_BASE + (factory_reset - ram));
        st32(payload + GE_O_FACTORY_ORIG_LEN, GE_FACTORY_HOOK_LENGTH);
        memcpy(payload + GE_O_FACTORY_ORIGINAL, factory_reset,
               GE_FACTORY_HOOK_LENGTH);
    }

    st32(hook + 1, (GE_PAYLOAD_BASE + GE_ENTRY_OFFSET) -
                    (GE_SCAN_BASE + (netstart - ram) + 5));
    cpu_physical_memory_write(GE_PAYLOAD_BASE, payload, sizeof(payload));
    cpu_physical_memory_write(GE_SCAN_BASE + (netstart - ram),
                              hook, sizeof(hook));
    if (have_startup || have_startup_dns || have_startup_tournament) {
        st32(factory_hook + 1,
             (GE_PAYLOAD_BASE + GE_FACTORY_ENTRY_OFFSET) -
             (GE_SCAN_BASE + (factory_reset - ram) + 5));
        cpu_physical_memory_write(GE_SCAN_BASE + (factory_reset - ram),
                                  factory_hook, sizeof(factory_hook));
    }
    info_report("pinball2000: guest extension installed: netstart=0x%08x "
                "ShellCmdAdd=0x%08x resources=%08x/%08x/%08x "
                "dns=%08x tournament=%08x/%08x/%08x%s%s%s",
                GE_SCAN_BASE + (unsigned)(netstart - ram),
                GE_SCAN_BASE + (unsigned)(shell_at - ram),
                resources[0], resources[1], resources[2], dns_resource,
                tourney_ip_resource, tournament_resource,
                free_play_resource,
                have_startup ? " startup IPv4" : "",
                have_startup_dns ? " startup DNS" : "",
                tournament_suffix);
    g_free(ram);
    ge_installed = true;
    return true;
}

void p2k_guest_extensions_init(void)
{
    const char *v = getenv("P2K_GUEST_EXTENSIONS");
    ge_enabled = v && *v && strcmp(v, "0");
    ge_installed = false;
    ge_retired = !ge_enabled;
    ge_udp_ttl_done = false;
    if (ge_enabled) {
        info_report("pinball2000: volatile guest extensions armed for XINA startup");
    }
}

void p2k_guest_extensions_reset(void)
{
    ge_installed = false;
    ge_retired = !ge_enabled;
    ge_udp_ttl_done = false;
}

void p2k_guest_extensions_observe_uart_line(const char *line, size_t len)
{
    while (len && (*line == '\r' || *line == '\n')) {
        line++;
        len--;
    }
    if (len < 5 || memcmp(line, "XINA:", 5)) {
        return;
    }
    /* The update loader has completely materialised the selected image before
     * its XINA banner reaches the emulated UART.  This one hardware event is
     * before netstart, so no translated-block polling is necessary. */
    if (!ge_udp_ttl_done && p2k_smc_slirp_active()) {
        ge_udp_ttl_done = true;
        if (!ge_try_patch_udp_ttl()) {
            info_report("pinball2000: no compatible UDP TTL guest-extension "
                        "image; feature retired");
        }
    }
    if (!ge_retired && !ge_installed && !ge_try_install()) {
        info_report("pinball2000: no compatible guest-extension image; feature retired");
        ge_retired = true;
    }
}
