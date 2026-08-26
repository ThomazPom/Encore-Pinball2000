/*
 * Configurable A-Z bindings for Pinball 2000 matrix switches.
 *
 * The configuration is a deliberately strict YAML subset:
 *
 *   switches:
 *     a: 11
 *     z: 88
 *
 * No YAML dependency is added to QEMU.  Rejecting the complete file on a
 * malformed line is safer than silently accepting a configuration the user
 * did not mean.  Input still follows the normal display -> LPT key path;
 * this module only translates configured letter qcodes into switch state.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "ui/input.h"

#include "p2k-internal.h"

static unsigned s_switch_bindings[Q_KEY_CODE__MAX];
static bool s_bound_key_down[Q_KEY_CODE__MAX];
static uint8_t s_switch_hold_count[89];
static char *s_keymap_path;
static bool s_keymap_installed;

static const char s_default_keymap[] =
    "# Encore A-Z matrix-switch bindings.\n"
    "# Strict format: one indented letter: NN entry per line; NN is 11..88.\n"
    "# Holding a letter keeps that switch closed for the same duration.\n"
    "switches:\n"
    "  x: 28\n"
    "  f: 58\n"
    "  d: 53\n"
    "  g: 54\n"
    "  e: 55\n"
    "  t: 56\n"
    "  l: 52\n";

int p2k_switch_keymap_letter_qcode(char key)
{
    switch (g_ascii_tolower(key)) {
    case 'a': return Q_KEY_CODE_A;
    case 'b': return Q_KEY_CODE_B;
    case 'c': return Q_KEY_CODE_C;
    case 'd': return Q_KEY_CODE_D;
    case 'e': return Q_KEY_CODE_E;
    case 'f': return Q_KEY_CODE_F;
    case 'g': return Q_KEY_CODE_G;
    case 'h': return Q_KEY_CODE_H;
    case 'i': return Q_KEY_CODE_I;
    case 'j': return Q_KEY_CODE_J;
    case 'k': return Q_KEY_CODE_K;
    case 'l': return Q_KEY_CODE_L;
    case 'm': return Q_KEY_CODE_M;
    case 'n': return Q_KEY_CODE_N;
    case 'o': return Q_KEY_CODE_O;
    case 'p': return Q_KEY_CODE_P;
    case 'q': return Q_KEY_CODE_Q;
    case 'r': return Q_KEY_CODE_R;
    case 's': return Q_KEY_CODE_S;
    case 't': return Q_KEY_CODE_T;
    case 'u': return Q_KEY_CODE_U;
    case 'v': return Q_KEY_CODE_V;
    case 'w': return Q_KEY_CODE_W;
    case 'x': return Q_KEY_CODE_X;
    case 'y': return Q_KEY_CODE_Y;
    case 'z': return Q_KEY_CODE_Z;
    default: return Q_KEY_CODE_UNMAPPED;
    }
}

static bool p2k_valid_matrix_switch(unsigned number)
{
    unsigned column = number / 10;
    unsigned row = number % 10;
    return column >= 1 && column <= 8 && row >= 1 && row <= 8;
}

static char *p2k_default_keymap_path(void)
{
    const char *override = getenv("P2K_SWITCH_KEYMAP");
    if (override && *override) {
        return g_strdup(override);
    }
    return g_build_filename(g_get_user_config_dir(), "encore",
                            "switch-keymap.yaml", NULL);
}

static bool p2k_initialize_keymap_file(const char *path)
{
    char *directory;
    GError *error = NULL;

    if (g_file_test(path, G_FILE_TEST_EXISTS)) {
        return true;
    }

    directory = g_path_get_dirname(path);
    if (g_mkdir_with_parents(directory, 0700) < 0) {
        error_report("pinball2000: cannot create switch-keymap directory "
                     "'%s' (%s)", directory, strerror(errno));
        g_free(directory);
        return false;
    }
    g_free(directory);

    if (!g_file_set_contents(path, s_default_keymap, -1, &error)) {
        error_report("pinball2000: cannot initialize switch keymap '%s' (%s)",
                     path, error ? error->message : "unknown error");
        g_clear_error(&error);
        return false;
    }

    info_report("pinball2000: initialized switch keymap at %s", path);
    return true;
}

static bool p2k_parse_binding(char *text, int *qcode, unsigned *number)
{
    char *cursor = text;
    char *end;
    char key;
    unsigned long parsed;

    if (!g_ascii_isalpha(*cursor)) {
        return false;
    }
    key = *cursor++;
    while (g_ascii_isspace(*cursor)) {
        cursor++;
    }
    if (*cursor++ != ':') {
        return false;
    }
    while (g_ascii_isspace(*cursor)) {
        cursor++;
    }
    if (!g_ascii_isdigit(*cursor)) {
        return false;
    }

    errno = 0;
    parsed = strtoul(cursor, &end, 10);
    if (errno || end == cursor || parsed > UINT_MAX) {
        return false;
    }
    while (g_ascii_isspace(*end)) {
        end++;
    }
    if (*end) {
        return false;
    }

    *qcode = p2k_switch_keymap_letter_qcode(key);
    *number = parsed;
    return *qcode != Q_KEY_CODE_UNMAPPED;
}

static unsigned p2k_load_keymap(const char *path)
{
    unsigned parsed_bindings[Q_KEY_CODE__MAX] = { 0 };
    char *contents = NULL;
    char **lines = NULL;
    GError *error = NULL;
    bool saw_switches = false;
    bool invalid = false;
    unsigned loaded = 0;

    if (!g_file_get_contents(path, &contents, NULL, &error)) {
        error_report("pinball2000: cannot read switch keymap '%s' (%s)",
                     path, error ? error->message : "unknown error");
        g_clear_error(&error);
        return 0;
    }

    lines = g_strsplit(contents, "\n", -1);
    for (size_t index = 0; lines[index]; index++) {
        char *line = lines[index];
        char *comment;
        size_t indent = 0;
        int qcode;
        unsigned number;

        g_strchomp(line);
        comment = strchr(line, '#');
        if (comment) {
            *comment = '\0';
            g_strchomp(line);
        }
        if (!*line) {
            continue;
        }
        if (strchr(line, '\t')) {
            warn_report("%s:%zu: tabs are not accepted in switch keymap",
                        path, index + 1);
            invalid = true;
            continue;
        }

        while (line[indent] == ' ') {
            indent++;
        }
        if (!saw_switches) {
            if (indent == 0 && !strcmp(line, "switches:")) {
                saw_switches = true;
            } else {
                warn_report("%s:%zu: expected top-level 'switches:'",
                            path, index + 1);
                invalid = true;
            }
            continue;
        }
        if (indent == 0) {
            warn_report("%s:%zu: switch entries must be indented under "
                        "'switches:'", path, index + 1);
            invalid = true;
            continue;
        }
        if (!p2k_parse_binding(line + indent, &qcode, &number)) {
            warn_report("%s:%zu: expected one A-Z key and switch number, "
                        "for example '  a: 13'", path, index + 1);
            invalid = true;
            continue;
        }
        if (!p2k_valid_matrix_switch(number)) {
            warn_report("%s:%zu: switch %u is outside 11..88 "
                        "(both digits must be 1..8)", path, index + 1,
                        number);
            invalid = true;
            continue;
        }
        if (parsed_bindings[qcode]) {
            warn_report("%s:%zu: duplicate binding for '%c'", path,
                        index + 1, g_ascii_tolower(line[indent]));
            invalid = true;
            continue;
        }
        parsed_bindings[qcode] = number;
        loaded++;
    }

    if (!saw_switches) {
        warn_report("%s: missing top-level 'switches:'", path);
        invalid = true;
    } else if (!loaded) {
        warn_report("%s: 'switches:' contains no usable bindings", path);
        invalid = true;
    }

    if (invalid) {
        error_report("pinball2000: invalid switch keymap %s; "
                     "custom A-Z bindings disabled (use -v for details)",
                     path);
        loaded = 0;
    } else {
        memcpy(s_switch_bindings, parsed_bindings,
               sizeof(s_switch_bindings));
    }
    g_strfreev(lines);
    g_free(contents);
    return loaded;
}

bool p2k_switch_keymap_handle_key(int qcode, bool down)
{
    unsigned number;

    if (qcode < 0 || qcode >= Q_KEY_CODE__MAX) {
        return false;
    }
    number = s_switch_bindings[qcode];
    if (!number) {
        return false;
    }
    if (s_bound_key_down[qcode] == down) {
        return true;
    }
    s_bound_key_down[qcode] = down;

    if (down) {
        if (s_switch_hold_count[number]++ == 0) {
            p2k_lpt_set_keymap_switch(number, true);
        }
    } else if (s_switch_hold_count[number] > 0 &&
               --s_switch_hold_count[number] == 0) {
        p2k_lpt_set_keymap_switch(number, false);
    }
    return true;
}

void p2k_install_switch_keymap(void)
{
    unsigned loaded;

    if (s_keymap_installed || p2k_lpt_blocks_emulated_input()) {
        return;
    }
    s_keymap_installed = true;

    s_keymap_path = p2k_default_keymap_path();
    if (!p2k_initialize_keymap_file(s_keymap_path)) {
        return;
    }
    loaded = p2k_load_keymap(s_keymap_path);
    if (!loaded) {
        return;
    }

    info_report("pinball2000: loaded %u switch key binding%s from %s",
                loaded, loaded == 1 ? "" : "s", s_keymap_path);
}
