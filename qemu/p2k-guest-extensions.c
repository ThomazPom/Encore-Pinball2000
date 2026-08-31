/*
 * Volatile, cross-ROM guest extensions.
 *
 * Nothing here changes an update image or saved flash.  Once the update
 * loader has copied a supported game to RAM, structural signatures resolve
 * the small import set and a sub-1-KiB payload is installed at 0x00ff0000.
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
};

typedef struct MaskedPattern {
    const uint8_t *bytes;
    const uint8_t *mask;
    size_t length;
} MaskedPattern;

static bool ge_enabled;
static bool ge_installed;
static bool ge_retired;

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

static bool parse_ipv4_env(const char *name, uint32_t *result)
{
    const char *s = getenv(name);
    unsigned a, b, c, d;
    char tail;

    if (!s || !*s) {
        return false;
    }
    if (sscanf(s, "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4 ||
        a > 255 || b > 255 || c > 255 || d > 255) {
        error_report("pinball2000: %s is not an IPv4 address: %s", name, s);
        return false;
    }
    *result = a << 24 | b << 16 | c << 8 | d;
    return true;
}

static bool resolve_resources(const uint8_t *netstart, uint32_t ns_addr,
                              uint32_t resources[3])
{
    uint32_t get_value;
    unsigned found = 0;

    if (netstart[9] != 0x68 || netstart[14] != 0xe8) {
        return false;
    }
    get_value = ns_addr + 19 + (int32_t)ld32(netstart + 15);
    for (unsigned i = 0; i + 10 <= 0x300 && found < 3; i++) {
        uint32_t target;
        if (netstart[i] != 0x68 || netstart[i + 5] != 0xe8) {
            continue;
        }
        target = ns_addr + i + 10 + (int32_t)ld32(netstart + i + 6);
        if (target == get_value) {
            resources[found++] = ld32(netstart + i + 1);
        }
    }
    return found == 3;
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
    uint8_t *shell_at, *put_at, *anchor_at, *netstart, *factory_reset;
    uint32_t resources[3], startup[3];
    uint8_t payload[P2K_GE_PAYLOAD_SIZE];
    uint8_t hook[GE_HOOK_LENGTH] = { 0xe9, 0, 0, 0, 0, 0x90 };
    uint8_t factory_hook[GE_FACTORY_HOOK_LENGTH] = {
        0xe9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90
    };
    bool have_startup;

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
    factory_reset = resolve_factory_reset(ram, GE_SCAN_LENGTH);
    if (memcmp(netstart, "\x55\x89\xe5\x83\xec\x08", GE_HOOK_LENGTH) ||
        !resolve_resources(netstart,
                           GE_SCAN_BASE + (netstart - ram), resources)) {
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
    st32(payload + GE_O_ORIGINAL_LEN, GE_HOOK_LENGTH);
    memcpy(payload + GE_O_ORIGINAL, netstart, GE_HOOK_LENGTH);

    have_startup = parse_ipv4_env("P2K_GUEST_IP", &startup[0]);
    if (have_startup != parse_ipv4_env("P2K_GUEST_MASK", &startup[1]) ||
        have_startup != parse_ipv4_env("P2K_GUEST_GATEWAY", &startup[2])) {
        error_report("pinball2000: guest IP, mask and gateway must be supplied together");
        g_free(ram);
        ge_retired = true;
        return false;
    }
    if (have_startup) {
        if (!factory_reset) {
            error_report("pinball2000: automatic factory-reset path was not resolved");
            g_free(ram);
            ge_retired = true;
            return false;
        }
        st32(payload + GE_O_STARTUP_ENABLE, 1);
        st32(payload + GE_O_STARTUP_IP, startup[0]);
        st32(payload + GE_O_STARTUP_MASK, startup[1]);
        st32(payload + GE_O_STARTUP_GW, startup[2]);
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
    if (have_startup) {
        st32(factory_hook + 1,
             (GE_PAYLOAD_BASE + GE_FACTORY_ENTRY_OFFSET) -
             (GE_SCAN_BASE + (factory_reset - ram) + 5));
        cpu_physical_memory_write(GE_SCAN_BASE + (factory_reset - ram),
                                  factory_hook, sizeof(factory_hook));
    }
    info_report("pinball2000: guest extension installed: netstart=0x%08x "
                "ShellCmdAdd=0x%08x resources=%08x/%08x/%08x%s",
                GE_SCAN_BASE + (unsigned)(netstart - ram),
                GE_SCAN_BASE + (unsigned)(shell_at - ram),
                resources[0], resources[1], resources[2],
                have_startup ? " factory-reset persistence armed" : "");
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
    if (ge_enabled) {
        info_report("pinball2000: volatile guest extensions armed for XINA startup");
    }
}

void p2k_guest_extensions_reset(void)
{
    ge_installed = false;
    ge_retired = !ge_enabled;
}

void p2k_guest_extensions_observe_uart_line(const char *line, size_t len)
{
    while (len && (*line == '\r' || *line == '\n')) {
        line++;
        len--;
    }
    if (ge_retired || ge_installed || len < 5 || memcmp(line, "XINA:", 5)) {
        return;
    }
    /* The update loader has completely materialised the selected image before
     * its XINA banner reaches the emulated UART.  This one hardware event is
     * before netstart, so no translated-block polling is necessary. */
    if (!ge_try_install()) {
        info_report("pinball2000: no compatible guest-extension image; feature retired");
        ge_retired = true;
    }
}
