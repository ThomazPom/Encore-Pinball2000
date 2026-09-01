/*
 * Williams Prism Update Board (PUB) read path.
 *
 * XINA selects one 16 KiB page through four nibble registers at 0xD4000,
 * then reads that page through the 0xD0000 window.  The decoded selector is:
 *
 *   game   : bank 0, pages 0x000..0x0ff (4 MiB)
 *   sound8 : bank 1, pages 0x100..0x13f (1 MiB)
 *   sound1 : bank 8, pages 0x000..0x03f (1 MiB)
 *
 * This experimental model deliberately implements only the read path used by
 * XINA's `pub ... dump` command.  No normal machine behavior changes unless
 * the pinball2000 `pub-card` property names an update bundle directory.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "p2k-qemu-compat.h"

#include "p2k-internal.h"

#define PUB_WINDOW_BASE 0x000d0000u
#define PUB_WINDOW_SIZE 0x00004000u
#define PUB_REG_BASE    0x000d4000u
#define PUB_REG_SIZE    4u
#define PUB_GAME_SIZE   0x00400000u
#define PUB_SOUND_SIZE  0x00100000u

typedef struct P2KPubCard {
    uint8_t *game;
    uint8_t *sound1;
    uint8_t *sound8;
    uint8_t reg[4];
} P2KPubCard;

static char *find_suffix(const char *dir, const char *suffix)
{
    GDir *d = g_dir_open(dir, 0, NULL);
    const char *name;
    char *result = NULL;

    if (!d) {
        return NULL;
    }
    while ((name = g_dir_read_name(d)) != NULL) {
        if (g_str_has_suffix(name, suffix)) {
            result = g_build_filename(dir, name, NULL);
            break;
        }
    }
    g_dir_close(d);
    return result;
}

static bool append_file(uint8_t *dst, size_t capacity, size_t *offset,
                        const char *path, size_t limit)
{
    gchar *data = NULL;
    gsize size = 0;

    if (!path || !g_file_get_contents(path, &data, &size, NULL)) {
        return false;
    }
    if (limit && size > limit) {
        size = limit;
    }
    if (*offset + size > capacity) {
        g_free(data);
        return false;
    }
    memcpy(dst + *offset, data, size);
    *offset += size;
    g_free(data);
    return true;
}

static bool load_game_bank(P2KPubCard *card, const char *dir, size_t *used)
{
    char *boot = find_suffix(dir, "_bootdata.rom");
    char *image = find_suffix(dir, "_im_flsh0.rom");
    char *game = find_suffix(dir, "_game.rom");
    char *symbols = find_suffix(dir, "_symbols.rom");
    size_t off = 0;
    bool ok;

    card->game = g_malloc(PUB_GAME_SIZE);
    memset(card->game, 0xff, PUB_GAME_SIZE);
    ok = boot && image && game && symbols &&
         append_file(card->game, PUB_GAME_SIZE, &off, boot, 0x8000);
    if (ok) {
        off = 0x8000;
        ok = append_file(card->game, PUB_GAME_SIZE, &off, image, 0) &&
             append_file(card->game, PUB_GAME_SIZE, &off, game, 0) &&
             append_file(card->game, PUB_GAME_SIZE, &off, symbols, 0);
    }
    g_free(boot);
    g_free(image);
    g_free(game);
    g_free(symbols);
    *used = off;
    return ok;
}

static uint8_t *selected_bank(P2KPubCard *card, uint32_t *offset,
                              size_t *capacity)
{
    uint32_t page = (card->reg[0] & 0xf) |
                    ((card->reg[1] & 0xf) << 4) |
                    ((card->reg[2] & 0xf) << 8);
    uint8_t bank = card->reg[3] & 0xf;

    if (bank == 0) {
        *offset = page * PUB_WINDOW_SIZE;
        *capacity = PUB_GAME_SIZE;
        return card->game;
    }
    if (bank == 8) {
        *offset = page * PUB_WINDOW_SIZE;
        *capacity = PUB_SOUND_SIZE;
        return card->sound1;
    }
    if (bank == 1 && page >= 0x100) {
        *offset = (page - 0x100) * PUB_WINDOW_SIZE;
        *capacity = PUB_SOUND_SIZE;
        return card->sound8;
    }
    return NULL;
}

static uint64_t pub_window_read(void *opaque, hwaddr addr, unsigned size)
{
    P2KPubCard *card = opaque;
    uint32_t base = 0;
    size_t capacity = 0;
    uint8_t *bank = selected_bank(card, &base, &capacity);
    uint64_t value = 0;

    if (!bank || base + addr + size > capacity) {
        return UINT64_MAX;
    }
    for (unsigned i = 0; i < size; i++) {
        value |= (uint64_t)bank[base + addr + i] << (8 * i);
    }
    return value;
}

static void pub_window_write(void *opaque, hwaddr addr, uint64_t value,
                             unsigned size)
{
    /* The preservation model exposes a read-only programmed card. */
}

static uint64_t pub_reg_read(void *opaque, hwaddr addr, unsigned size)
{
    P2KPubCard *card = opaque;
    return addr < PUB_REG_SIZE ? card->reg[addr] : 0xff;
}

static void pub_reg_write(void *opaque, hwaddr addr, uint64_t value,
                          unsigned size)
{
    P2KPubCard *card = opaque;
    if (addr < PUB_REG_SIZE) {
        card->reg[addr] = value & 0xf;
    }
}

static const MemoryRegionOps pub_window_ops = {
    .read = pub_window_read,
    .write = pub_window_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pub_reg_ops = {
    .read = pub_reg_read,
    .write = pub_reg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 1 },
    .impl = { .min_access_size = 1, .max_access_size = 1 },
};

void p2k_install_pub_card(Pinball2000MachineState *s)
{
    P2KPubCard *card;
    MemoryRegion *window;
    MemoryRegion *regs;
    size_t used = 0;

    if (!s->pub_card_path || !*s->pub_card_path) {
        return;
    }
    card = g_new0(P2KPubCard, 1);
    if (!load_game_bank(card, s->pub_card_path, &used)) {
        error_report("pinball2000: cannot construct PUB game bank from %s",
                     s->pub_card_path);
        exit(1);
    }
    card->sound1 = g_malloc0(PUB_SOUND_SIZE);
    card->sound8 = g_malloc0(PUB_SOUND_SIZE);
    memset(card->sound1, 0xff, PUB_SOUND_SIZE);
    memset(card->sound8, 0xff, PUB_SOUND_SIZE);

    window = g_new(MemoryRegion, 1);
    regs = g_new(MemoryRegion, 1);
    memory_region_init_io(window, NULL, &pub_window_ops, card,
                          "p2k.pub.window", PUB_WINDOW_SIZE);
    memory_region_init_io(regs, NULL, &pub_reg_ops, card,
                          "p2k.pub.page-registers", PUB_REG_SIZE);
    memory_region_add_subregion_overlap(get_system_memory(), PUB_WINDOW_BASE,
                                        window, 10);
    memory_region_add_subregion_overlap(get_system_memory(), PUB_REG_BASE,
                                        regs, 10);
    info_report("pinball2000: PUB card installed from %s "
                "(game bank used=0x%zx/0x%x)",
                s->pub_card_path, used, PUB_GAME_SIZE);
}
