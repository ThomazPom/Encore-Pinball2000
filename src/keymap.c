/*
 * keymap.c — configurable A-Z matrix-switch bindings for Encore (unicorn).
 *
 * Direct port of main/QEMU's qemu/p2k-switch-keymap.c, adapted from the QOM /
 * GLib environment to plain C + SDL2. Semantics are identical:
 *
 *   - Config is a strict YAML subset:
 *         switches:
 *           a: 13
 *           z: 88
 *   - Switch numbers are two digits, both 1..8 (matrix col.row = 11..88).
 *   - Any malformed line rejects the WHOLE file (fail-closed) — accepting a
 *     partial config the operator did not intend is more dangerous than
 *     disabling custom bindings entirely.
 *   - Holding a bound letter keeps that switch closed; overlapping holds of
 *     the same switch are reference-counted so a release of one key does not
 *     drop a switch another key is still holding.
 *
 * The injected switch state reaches the guest through
 * lpt_set_keymap_switch() (src/io.c), which ORs it into the LPT matrix scan.
 */

#include "keymap.h"
#include "encore.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>

/* One binding slot per letter a..z (index 0..25). 0 = unbound, else NN. */
static unsigned s_bindings[26];
static bool     s_key_down[26];
static uint8_t  s_hold_count[89];   /* refcount per switch number 11..88 */
static bool     s_installed;
static unsigned s_loaded;

static const char s_default_keymap[] =
    "# Encore A-Z matrix-switch bindings.\n"
    "# Strict format: one indented 'letter: NN' entry per line; NN is 11..88\n"
    "# (both digits 1..8). Holding a letter keeps that switch closed.\n"
    "switches:\n"
    "  x: 28\n"
    "  f: 58\n"
    "  d: 53\n"
    "  g: 54\n"
    "  e: 55\n"
    "  t: 56\n"
    "  l: 52\n";

static int letter_index(char key)
{
    key = (char)tolower((unsigned char)key);
    if (key >= 'a' && key <= 'z')
        return key - 'a';
    return -1;
}

static bool valid_matrix_switch(unsigned number)
{
    unsigned column = number / 10;
    unsigned row    = number % 10;
    return column >= 1 && column <= 8 && row >= 1 && row <= 8;
}

/* Map an SDL keycode to a letter index 0..25, or -1 if not a-z. */
static int sym_letter_index(SDL_Keycode sym)
{
    if (sym >= SDLK_a && sym <= SDLK_z)
        return (int)(sym - SDLK_a);
    return -1;
}

static char *default_keymap_path(void)
{
    const char *override = getenv("P2K_SWITCH_KEYMAP");
    if (override && *override)
        return strdup(override);

    const char *cfg = getenv("XDG_CONFIG_HOME");
    char buf[1024];
    if (cfg && *cfg) {
        snprintf(buf, sizeof(buf), "%s/encore/switch-keymap.yaml", cfg);
    } else {
        const char *home = getenv("HOME");
        if (!home || !*home) home = ".";
        snprintf(buf, sizeof(buf), "%s/.config/encore/switch-keymap.yaml", home);
    }
    return strdup(buf);
}

static bool mkdir_p(const char *path)
{
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0700) < 0 && errno != EEXIST)
                return false;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0700) < 0 && errno != EEXIST)
        return false;
    return true;
}

static bool file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static bool initialize_keymap_file(const char *path)
{
    if (file_exists(path))
        return true;

    /* Create parent directory. */
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        if (dir[0] && !mkdir_p(dir)) {
            LOG("keymap", "cannot create directory '%s' (%s)\n",
                dir, strerror(errno));
            return false;
        }
    }

    FILE *f = fopen(path, "w");
    if (!f) {
        LOG("keymap", "cannot initialize keymap '%s' (%s)\n",
            path, strerror(errno));
        return false;
    }
    fwrite(s_default_keymap, 1, sizeof(s_default_keymap) - 1, f);
    fclose(f);
    LOG("keymap", "initialized switch keymap at %s\n", path);
    return true;
}

/* Trim leading/trailing ASCII whitespace in-place; returns pointer into s. */
static char *trim(char *s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = '\0';
    return s;
}

/* Parse "letter : NN" (already indent-stripped). On success sets *idx and
 * *number and returns true. */
static bool parse_binding(char *text, int *idx, unsigned *number)
{
    char *cursor = text;
    if (!isalpha((unsigned char)*cursor))
        return false;
    char key = *cursor++;
    while (isspace((unsigned char)*cursor)) cursor++;
    if (*cursor++ != ':')
        return false;
    while (isspace((unsigned char)*cursor)) cursor++;
    if (!isdigit((unsigned char)*cursor))
        return false;

    char *end;
    errno = 0;
    unsigned long parsed = strtoul(cursor, &end, 10);
    if (errno || end == cursor || parsed > UINT_MAX)
        return false;
    while (isspace((unsigned char)*end)) end++;
    if (*end)
        return false;

    *idx = letter_index(key);
    *number = (unsigned)parsed;
    return *idx >= 0;
}

static unsigned load_keymap(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        LOG("keymap", "cannot read keymap '%s' (%s)\n", path, strerror(errno));
        return 0;
    }

    unsigned parsed_bindings[26] = {0};
    bool saw_switches = false;
    bool invalid = false;
    unsigned loaded = 0;
    char line[512];
    size_t lineno = 0;

    while (fgets(line, sizeof(line), f)) {
        lineno++;
        /* Strip trailing newline. */
        char *nl = strpbrk(line, "\r\n");
        if (nl) *nl = '\0';
        /* Strip comments. */
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';

        /* Reject tabs (ambiguous indentation), matching main. */
        if (strchr(line, '\t')) {
            LOG("keymap", "%s:%zu: tabs are not accepted\n", path, lineno);
            invalid = true;
            continue;
        }

        size_t indent = 0;
        while (line[indent] == ' ') indent++;

        char *body = trim(line);
        if (!*body)
            continue;

        if (!saw_switches) {
            if (indent == 0 && strcmp(body, "switches:") == 0) {
                saw_switches = true;
            } else {
                LOG("keymap", "%s:%zu: expected top-level 'switches:'\n",
                    path, lineno);
                invalid = true;
            }
            continue;
        }
        if (indent == 0) {
            LOG("keymap", "%s:%zu: entries must be indented under 'switches:'\n",
                path, lineno);
            invalid = true;
            continue;
        }

        int idx;
        unsigned number;
        if (!parse_binding(line + indent, &idx, &number)) {
            LOG("keymap", "%s:%zu: expected 'letter: NN' (e.g. '  a: 13')\n",
                path, lineno);
            invalid = true;
            continue;
        }
        if (!valid_matrix_switch(number)) {
            LOG("keymap", "%s:%zu: switch %u outside 11..88 (both digits 1..8)\n",
                path, lineno, number);
            invalid = true;
            continue;
        }
        if (parsed_bindings[idx]) {
            LOG("keymap", "%s:%zu: duplicate binding for '%c'\n",
                path, lineno, 'a' + idx);
            invalid = true;
            continue;
        }
        parsed_bindings[idx] = number;
        loaded++;
    }
    fclose(f);

    if (!saw_switches) {
        LOG("keymap", "%s: missing top-level 'switches:'\n", path);
        invalid = true;
    } else if (!loaded) {
        LOG("keymap", "%s: 'switches:' contains no usable bindings\n", path);
        invalid = true;
    }

    if (invalid) {
        LOG("keymap", "invalid keymap %s; custom A-Z bindings disabled\n", path);
        return 0;
    }
    memcpy(s_bindings, parsed_bindings, sizeof(s_bindings));
    return loaded;
}

unsigned keymap_install(const char *path)
{
    if (s_installed)
        return s_loaded;
    s_installed = true;

    char *resolved = path && *path ? strdup(path) : default_keymap_path();
    if (!resolved)
        return 0;

    if (!initialize_keymap_file(resolved)) {
        free(resolved);
        return 0;
    }
    s_loaded = load_keymap(resolved);
    if (s_loaded)
        LOG("keymap", "loaded %u switch key binding%s from %s\n",
            s_loaded, s_loaded == 1 ? "" : "s", resolved);
    free(resolved);
    return s_loaded;
}

bool keymap_handle_key(SDL_Keycode sym, bool down)
{
    int idx = sym_letter_index(sym);
    if (idx < 0)
        return false;
    unsigned number = s_bindings[idx];
    if (!number)
        return false;
    if (s_key_down[idx] == down)
        return true;             /* auto-repeat / no state change */
    s_key_down[idx] = down;

    if (down) {
        if (s_hold_count[number]++ == 0)
            lpt_set_keymap_switch(number, 1);
    } else if (s_hold_count[number] > 0 &&
               --s_hold_count[number] == 0) {
        lpt_set_keymap_switch(number, 0);
    }
    return true;
}

bool keymap_active(void)
{
    return s_loaded > 0;
}
