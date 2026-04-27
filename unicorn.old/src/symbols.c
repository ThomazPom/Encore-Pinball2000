/*
 * symbols.c — XINU symbol-table reader for Pinball 2000 update flash.
 *
 * The dearchived `*_symbols.rom` blobs (and the equivalent block produced
 * by the build pipeline that ships in the original update.bin) embed a
 * "SYMBOL TABLE" section that lists every C/C++ symbol the linker emitted
 * for that build, with its guest virtual address.
 *
 * After rom_load_update_flash() concatenates bootdata + im_flsh0 + game +
 * symbols into g_emu.flash, sym_init() scans the buffer for the magic and
 * builds an in-memory index.  sym_lookup("clkruns") then returns
 * 0x002fe664 on a v2.1 SWE1 build, 0 on older stripped tables.
 *
 * Patch sites that hardcode an address can use it as a soft override:
 *
 *     uint32_t fatal = sym_lookup("Fatal(char const *,...)");
 *     if (!fatal) fatal = 0x0022722Cu;   // SWE1 v1.19 fallback
 *
 * which is how we move toward update-agnostic, version-agnostic patching
 * without breaking existing fallbacks.
 *
 * Format (verified for SWE1 v1.5/v2.1 and RFM v1.6/v2.6):
 *   +0x00  "SYMBOL TABLE"      12 bytes
 *   +0x0C  u32 checksum
 *   +0x10  u32 num_entries
 *   +0x14  u32 string_table_size
 *   +0x18  u32 base (0x10000000 in every sample seen)
 *   +0x1C  entries[num_entries] of (u32 name_off, u32 addr) — 8 bytes each
 *   +end   (small zero pad), then NUL-terminated strings
 *
 * num_entries from the header is occasionally 1–2 entries longer than what
 * actually fits, so we walk the entries until addr leaves the plausible
 * 0x100000..0x500000 RAM range — same heuristic as tools/sym_dump.py.
 */
#include "encore.h"
#include <ctype.h>

typedef struct {
    uint32_t name_off;
    uint32_t addr;
} sym_entry_t;

static struct {
    bool         loaded;
    uint32_t     n;
    uint32_t     str_base;       /* offset within g_sym.blob of string table */
    uint32_t     str_size;       /* bytes available after str_base */
    const uint8_t *blob;         /* pointer into g_emu.flash, do not free */
    uint32_t     blob_size;      /* bytes available from blob onward */
    const sym_entry_t *entries;  /* points inside blob (LE u32 pairs) */
} g_sym;

static const uint8_t MAGIC[12] = {
    'S','Y','M','B','O','L',' ','T','A','B','L','E'
};

/* Find "SYMBOL TABLE" magic in flash. Aligned to 4 bytes (always observed). */
static const uint8_t *find_magic(const uint8_t *buf, size_t len)
{
    if (len < 32) return NULL;
    /* Scan in 4-byte steps — magic is always aligned in practice. */
    size_t end = len - 12;
    for (size_t i = 0; i <= end; i += 4) {
        if (buf[i] == 'S' && memcmp(buf + i, MAGIC, 12) == 0)
            return buf + i;
    }
    return NULL;
}

/* Anchor str_base by trying offsets just past the entries table and keeping
 * the first that resolves several entries to printable identifier strings. */
static uint32_t anchor_str_base(const uint8_t *blob, uint32_t blob_size,
                                const sym_entry_t *entries, uint32_t n)
{
    uint32_t end = 28u + n * 8u;
    for (uint32_t cand = end; cand < end + 256u && cand + 1u < blob_size; cand++) {
        int ok = 0;
        int probes = (n < 8u) ? (int)n : 8;
        for (int j = 0; j < probes; j++) {
            uint32_t no = entries[j].name_off;
            uint64_t pj = (uint64_t)cand + no;
            if (pj == 0 || pj >= blob_size) { ok = -1; break; }
            uint8_t c = blob[pj];
            if (!((c >= 'A' && c <= 'z') || c == '_' || c == '~')) { ok = -1; break; }
            ok++;
        }
        if (ok >= 6) return cand;
    }
    return 0;
}

void sym_init(void)
{
    memset(&g_sym, 0, sizeof(g_sym));
    if (!g_emu.flash) return;

    const uint8_t *m = find_magic(g_emu.flash, FLASH_SIZE);
    if (!m) {
        LOG("sym", "no SYMBOL TABLE magic in flash — symbol lookup disabled\n");
        return;
    }

    uint32_t off_in_flash = (uint32_t)(m - g_emu.flash);
    uint32_t avail = (uint32_t)FLASH_SIZE - off_in_flash;
    if (avail < 32u) {
        LOG("sym", "SYMBOL TABLE found at flash+0x%x but truncated\n", off_in_flash);
        return;
    }

    /* Header fields after magic */
    uint32_t n_hdr   = *(const uint32_t *)(m + 16);
    uint32_t str_sz  = *(const uint32_t *)(m + 20);
    /* base at +24 unused — always 0x10000000 in every sample. */

    /* Walk entries until addr leaves the 0x100000..0x500000 plausible range
     * (n_hdr is sometimes a hair larger than what actually fits). */
    const sym_entry_t *entries = (const sym_entry_t *)(m + 28);
    uint32_t n = 0;
    uint32_t cap = (n_hdr + 32u);
    if (cap > (avail - 28u) / 8u) cap = (avail - 28u) / 8u;
    for (uint32_t i = 0; i < cap; i++) {
        uint32_t addr = entries[i].addr;
        if (addr < 0x100000u || addr > 0x500000u) break;
        n++;
    }
    if (n < 16u) {
        LOG("sym", "SYMBOL TABLE has only %u entries — refusing to use\n", n);
        return;
    }

    uint32_t sb = anchor_str_base(m, avail, entries, n);
    if (!sb) {
        LOG("sym", "could not anchor string table base — disabled\n");
        return;
    }

    g_sym.loaded    = true;
    g_sym.n         = n;
    g_sym.str_base  = sb;
    g_sym.str_size  = str_sz;
    g_sym.blob      = m;
    g_sym.blob_size = avail;
    g_sym.entries   = entries;

    LOG("sym", "loaded: %u entries, str_base=0x%x (header n=%u, str_sz=0x%x) "
               "@ flash+0x%x\n",
        n, sb, n_hdr, str_sz, off_in_flash);
}

/* Reverse lookup: scan blob for `name\0` and keep the occurrence whose
 * (pos - str_base) matches a real entry's name_off.  O(n) per call;
 * patch sites resolve at most a few dozen names, so no caching needed. */
uint32_t sym_lookup(const char *name)
{
    if (!g_sym.loaded || !name || !*name) return 0;
    size_t nlen = strlen(name);
    if (nlen >= 256u) return 0;

    /* Build needle = name + '\0' on the stack. */
    char needle[260];
    memcpy(needle, name, nlen);
    needle[nlen] = '\0';
    size_t needle_len = nlen + 1u;

    /* memmem-like scan from str_base onward. */
    const uint8_t *p   = g_sym.blob + g_sym.str_base;
    const uint8_t *end = g_sym.blob + g_sym.blob_size;
    if (p + needle_len > end) return 0;

    while (p + needle_len <= end) {
        const uint8_t *hit = (const uint8_t *)memmem(p, end - p, needle, needle_len);
        if (!hit) return 0;

        /* Must start at str_base or be preceded by a NUL (i.e. be a full
         * symbol, not a tail of a longer symbol). */
        if (hit == g_sym.blob + g_sym.str_base || hit[-1] == 0) {
            uint32_t no = (uint32_t)(hit - (g_sym.blob + g_sym.str_base));
            for (uint32_t i = 0; i < g_sym.n; i++) {
                if (g_sym.entries[i].name_off == no)
                    return g_sym.entries[i].addr;
            }
        }
        p = hit + 1;
    }
    return 0;
}

/* sym_lookup_first: try each name in turn, return first non-zero hit.
 * Useful when a symbol's exact mangled spelling differs across builds:
 *   sym_lookup_first((const char*[]){
 *       "Fatal(char const *,...)",
 *       "Fatal(const char *,...)",
 *       NULL
 *   });
 */
uint32_t sym_lookup_first(const char *const *names)
{
    if (!names) return 0;
    for (; *names; names++) {
        uint32_t a = sym_lookup(*names);
        if (a) return a;
    }
    return 0;
}

bool sym_loaded(void)         { return g_sym.loaded; }
uint32_t sym_count(void)      { return g_sym.loaded ? g_sym.n : 0u; }
