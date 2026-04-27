/*
 * pinball2000 BAR3 — update flash window @ 0x12000000 (4 MiB).
 *
 * The PRISM option ROM and the game read game resources (bootdata,
 * im_flsh0, game.rom, symbols.rom) out of this window.  In the real
 * board it's an Intel 28F320 NOR flash; here we expose it as plain
 * RAM seeded from `savedata/<game>.flash` at boot, mirroring what
 * unicorn.old/src/rom.c:399-422 + src/main.c:914-916 does.
 *
 * Without seeding BAR3 the game's resource lookup (e.g. PC 0x2586a6
 * `mov ebx, [esi+0x10]; test ebx,ebx; jne ...; push 0x14ab9e ; call`
 * — string "Retrieve Resource (get &) Failed, ID=%8.8s") returns
 * NULL and the game panics.
 *
 * Write semantics (Intel 28F320 command interface) are NOT modelled.
 * The flash savedata is loaded read-mostly; if the game writes to it
 * during runtime those writes go into the RAM mirror and are not
 * persisted (deferred concern; matches what we want for a clean
 * QEMU-first architecture — savedata persistence is a separate
 * lifecycle issue).
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "exec/address-spaces.h"

#include "pinball2000.h"
#include "p2k-internal.h"

#define P2K_BAR3_BASE        0x12000000u
#define P2K_BAR3_SIZE        0x00400000u   /* 4 MiB */

static int slurp_file(const char *path, uint8_t *dst, size_t cap, size_t *out_n)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return -1;
    }
    size_t n = fread(dst, 1, cap, f);
    fclose(f);
    *out_n = n;
    return 0;
}

void p2k_install_bar3_flash(Pinball2000MachineState *s)
{
    MemoryRegion *sm = get_system_memory();
    MemoryRegion *mr = g_new(MemoryRegion, 1);

    memory_region_init_ram(mr, NULL, "p2k.bar3-flash",
                           P2K_BAR3_SIZE, &error_abort);
    /* QEMU initialises RAM to zero; Intel 28F320 erased state is 0xFF.
     * Pre-fill so unwritten regions match real flash. */
    void *host = memory_region_get_ram_ptr(mr);
    memset(host, 0xFF, P2K_BAR3_SIZE);

    /* Seed from savedata/<game>.flash if present. */
    const char *game = s->game ? s->game : "swe1";
    char path[1024];
    snprintf(path, sizeof(path), "savedata/%s.flash", game);

    size_t n = 0;
    if (slurp_file(path, host, P2K_BAR3_SIZE, &n) == 0 && n > 0) {
        info_report("pinball2000: BAR3 seeded from %s (%zu bytes)", path, n);
    } else {
        warn_report("pinball2000: BAR3 flash %s not loaded "
                    "(resource lookups will fail)", path);
    }

    memory_region_add_subregion(sm, P2K_BAR3_BASE, mr);
}
