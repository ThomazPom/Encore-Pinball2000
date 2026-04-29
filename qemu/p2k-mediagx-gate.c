/* p2k-mediagx-gate.c — Pinball 2000 / Cyrix MediaGX TCG-extension gate
 *
 * Cyrix/National MediaGX defines a small set of "Display Driver
 * Instructions" in the second-byte-of-0F opcode space (Cyrix MediaGX
 * Processor Data Book v2.0, section 4.1.5, Table 4-6):
 *
 *     0F 3A   BB0_RESET    (BLT buffer 0 pointer reset)
 *     0F 3B   BB1_RESET    (BLT buffer 1 pointer reset)
 *     0F 3C   CPU_WRITE    (EBX=internal-register addr, EAX=data)
 *     0F 3D   CPU_READ     (EBX=internal-register addr, EAX=data)
 *
 * Per section 4.1.6 these are gated on GCR (index B8h) bits[3:2] —
 * SCRATCHPAD_SIZE. When that field is zero they should #UD; when it is
 * non-zero they execute as MediaGX display/internal-register accesses.
 *
 * Stock x86 reserves none of these, and 0F38 / 0F3A are repurposed by
 * SSSE3 / SSE4 as escape prefixes for a 3-byte opcode map. Because of
 * this collision we MUST gate any MediaGX behavior to the pinball2000
 * machine; outside that mode the upstream i386 decoder must keep its
 * normal SSE4 dispatch and #UD on the rest of the slots.
 *
 * This file owns the runtime gate. It is set TRUE at machine init by
 * pinball2000 (qemu/pinball2000.c) and read by the TCG helpers that
 * the upstream-patch installs into target/i386/tcg/misc_helper.c.
 *
 * Default = FALSE so a stock qemu-system-i386 run (e.g. -M isapc) is
 * not affected by our patch beyond the trivial decode-table additions
 * that immediately route to the helpers — and the helpers, with the
 * gate FALSE, raise #UD just like vanilla.
 *
 * Counters are exposed for the timing/diag panel and for tests. */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "p2k-internal.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

/* Gate — read by the TCG helpers in misc_helper.c via extern. */
bool p2k_mediagx_extensions_enabled = false;

/* Per-opcode counters. Indexed by the second opcode byte (0x36..0x3F). */
static atomic_uint s_p2k_mediagx_seen[16];

/* Called by pinball2000 machine init. */
void p2k_mediagx_enable_extensions(void)
{
    p2k_mediagx_extensions_enabled = true;
    info_report("pinball2000: Cyrix MediaGX TCG extensions ENABLED "
                "(0F 3C CPU_WRITE shim active; 0F 36/37/39/3B/3D/3F "
                "logged then #UD; 0F 3A reserved for SSE4 dispatch). "
                "Stock i386 binaries are unaffected.");
}

/* Called by the TCG helpers. Returns the post-increment count for
 * rate-limit decisions. Idempotent / lock-free. */
unsigned p2k_mediagx_note_opcode(uint8_t op2)
{
    if (op2 < 0x30 || op2 > 0x3F) {
        return 0;
    }
    return atomic_fetch_add(&s_p2k_mediagx_seen[op2 - 0x30], 1u) + 1u;
}

/* Optional read-back for diag output. */
unsigned p2k_mediagx_get_opcode_count(uint8_t op2)
{
    if (op2 < 0x30 || op2 > 0x3F) {
        return 0;
    }
    return atomic_load(&s_p2k_mediagx_seen[op2 - 0x30]);
}
