/*
 * Configurable desktop letter bindings for Pinball 2000 matrix switches.
 *
 * A tiny YAML subset is intentionally parsed here to avoid adding a YAML
 * dependency to QEMU.  The file is created automatically on first launch at:
 *
 *   $P2K_SWITCH_KEYMAP, when explicitly set; otherwise
 *   $XDG_CONFIG_HOME/encore/switch-keymap.yaml
 *   (normally ~/.config/encore/switch-keymap.yaml)
 *
 * Holding a configured letter holds the selected matrix switch for the same
 * real duration.  The existing NN + Ctrl implementation remains the single
 * source of switch timing semantics.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "ui/input.h"
#include "ui/console.h"
#include "qemu/thread.h"

#include <SDL2/SDL.h>

#include "p2k-internal.h"

static unsigned s_switch_bindings[Q_KEY_CODE__MAX];
static bool s_bound_key_down[Q_KEY_CODE__MAX];
static int s_active_bound_key = Q_KEY_CODE_UNMAPPED;
static char *s_keymap_path;
static bool s_sdl_watch_installed;

static const char s_default_keymap[] =
    "# Encore desktop letter-to-switch bindings.\n"
    "# Hold a letter for exactly as long as the switch should stay closed.\n"
    "switches:\n"
    "  x: 28\n"
    "  f: 58\n"
    "  g: 53\n"
    "  l: 52\n"
    "  e: 55\n"
    "  t: 56\n";

static bool p2k_env_enabled(const char *name)
{
    const char *value = getenv(name);
    return value && *value && strcmp(value, "0") != 0;
}

static int p2k_letter_qcode(char key)
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

static int p2k_sdl_letter_qcode(SDL_Keycode key)
{
    if (key < SDLK_a || key > SDLK_z) {
        return Q_KEY_CODE_UNMAPPED;
    }
    return p2k_letter_qcode((char)('a' + key - SDLK_a));
}

static int p2k_digit_qcode(unsigned digit)
{
    switch (digit) {
    case 1: return Q_KEY_CODE_1;
    case 2: return Q_KEY_CODE_2;
    case 3: return Q_KEY_CODE_3;
    case 4: return Q_KEY_CODE_4;
    case 5: return Q_KEY_CODE_5;
    case 6: return Q_KEY_CODE_6;
    case 7: return Q_KEY_CODE_7;
    case 8: return Q_KEY_CODE_8;
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
        error_report("pinball2000: cannot create switch-keymap directory '%s' (%s)",
                     directory, strerror(errno));
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

static unsigned p2k_load_keymap(const char *path)
{
    char *contents = NULL;
    char **lines = NULL;
    GError *error = NULL;
    unsigned loaded = 0;

    if (!g_file_get_contents(path, &contents, NULL, &error)) {
        error_report("pinball2000: cannot read switch keymap '%s' (%s)",
                     path, error ? error->message : "unknown error");
        g_clear_error(&error);
        return 0;
    }

    lines = g_strsplit(contents, "\n", -1);
    for (size_t index = 0; lines[index]; index++) {
        char *line = g_strstrip(lines[index]);
        char key = 0;
        unsigned number = 0;
        int qcode;

        if (!*line || *line == '#' || g_str_has_prefix(line, "switches:")) {
            continue;
        }
        if (sscanf(line, " %c : %u", &key, &number) != 2) {
            warn_report("pinball2000: ignoring malformed switch-keymap line: %s",
                        line);
            continue;
        }

        qcode = p2k_letter_qcode(key);
        if (qcode == Q_KEY_CODE_UNMAPPED || !p2k_valid_matrix_switch(number)) {
            warn_report("pinball2000: invalid switch-keymap entry '%c: %u'",
                        key, number);
            continue;
        }

        s_switch_bindings[qcode] = number;
        loaded++;
    }

    g_strfreev(lines);
    g_free(contents);
    return loaded;
}

static void p2k_inject_numeric_digit(unsigned digit)
{
    int qcode = p2k_digit_qcode(digit);
    p2k_lpt_host_key(qcode, true);
    p2k_lpt_host_key(qcode, false);
}

static bool p2k_handle_bound_key(int qcode, bool down)
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
        /* The legacy selector supports one held Ctrl-driven switch at a time.
         * Ignore overlapping custom keys rather than releasing the first one. */
        if (s_active_bound_key != Q_KEY_CODE_UNMAPPED) {
            return true;
        }
        p2k_inject_numeric_digit(number / 10);
        p2k_inject_numeric_digit(number % 10);
        p2k_lpt_host_key(Q_KEY_CODE_CTRL, true);
        s_active_bound_key = qcode;
    } else if (s_active_bound_key == qcode) {
        p2k_lpt_host_key(Q_KEY_CODE_CTRL, false);
        s_active_bound_key = Q_KEY_CODE_UNMAPPED;
    }

    return true;
}

static void p2k_switch_key_event(DeviceState *dev, QemuConsole *src,
                                 InputEvent *event)
{
    InputKeyEvent *key = event->u.key.data;
    int qcode = qemu_input_key_value_to_qcode(key->key);

    p2k_handle_bound_key(qcode, key->down);
}

static const QemuInputHandler p2k_switch_key_input_handler = {
    .name = "pinball2000 YAML switch keymap",
    .mask = INPUT_EVENT_MASK_KEY,
    .event = p2k_switch_key_event,
};

typedef struct P2KBoundKeyEvent {
    int qcode;
    bool down;
} P2KBoundKeyEvent;

static void p2k_bound_key_bh(void *opaque)
{
    P2KBoundKeyEvent *event = opaque;
    p2k_handle_bound_key(event->qcode, event->down);
    g_free(event);
}

static int p2k_sdl_event_watch(void *opaque, SDL_Event *event)
{
    P2KBoundKeyEvent *queued;
    int qcode;

    if (event->type != SDL_KEYDOWN && event->type != SDL_KEYUP) {
        return 1;
    }
    if (event->type == SDL_KEYDOWN && event->key.repeat) {
        return 1;
    }

    qcode = p2k_sdl_letter_qcode(event->key.keysym.sym);
    if (qcode == Q_KEY_CODE_UNMAPPED || !s_switch_bindings[qcode]) {
        return 1;
    }

    queued = g_new(P2KBoundKeyEvent, 1);
    queued->qcode = qcode;
    queued->down = event->type == SDL_KEYDOWN;
    aio_bh_schedule_oneshot(qemu_get_aio_context(), p2k_bound_key_bh, queued);
    return 1;
}

static gboolean p2k_wait_for_direct_sdl(gpointer opaque)
{
    if (!SDL_WasInit(SDL_INIT_EVENTS)) {
        return G_SOURCE_CONTINUE;
    }
    if (!s_sdl_watch_installed) {
        SDL_AddEventWatch(p2k_sdl_event_watch, NULL);
        s_sdl_watch_installed = true;
        info_report("pinball2000: switch keymap attached to direct SDL input");
    }
    return G_SOURCE_REMOVE;
}

static void p2k_switch_keymap_init(void)
{
    unsigned loaded;

    if (p2k_env_enabled("P2K_CABINET_PURIST") ||
        p2k_env_enabled("P2K_LPT_DISABLE")) {
        return;
    }

    s_keymap_path = p2k_default_keymap_path();
    if (!p2k_initialize_keymap_file(s_keymap_path)) {
        return;
    }

    loaded = p2k_load_keymap(s_keymap_path);
    if (!loaded) {
        warn_report("pinball2000: no usable switch bindings in %s",
                    s_keymap_path);
        return;
    }

    qemu_input_handler_register(NULL, &p2k_switch_key_input_handler);
    info_report("pinball2000: loaded %u switch key binding%s from %s",
                loaded, loaded == 1 ? "" : "s", s_keymap_path);

    if (p2k_env_enabled("P2K_FRAMEBUFFER_THREAD")) {
        g_timeout_add(25, p2k_wait_for_direct_sdl, NULL);
    }
}

type_init(p2k_switch_keymap_init)
