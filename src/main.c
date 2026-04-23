/*
 * main.c — Encore Pinball 2000 emulator entry point.
 *
 * Encore (French for "again") — a clean x64 Pinball 2000 emulator using:
 *   - Unicorn Engine for CPU emulation (i386 guest on x64 host)
 *   - SDL2 for display
 *   - SDL2_mixer for audio
 *   - Pre-deinterleaved ROM banks
 *   - ROM-agnostic: auto-detects game from ROM content
 *   - Savedata persistence between sessions
 */
#include "encore.h"
#include <dirent.h>
#include <ctype.h>

EncoreState g_emu;

static void on_term_signal(int sig)
{
    (void)sig;
    g_emu.running = false;
}

static void print_banner(void)
{
    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║  Encore — Pinball 2000 Emulator                 ║\n");
    printf("║  CPU: Unicorn Engine (i386)                     ║\n");
    printf("║  Video: SDL2 | Audio: SDL2_mixer                ║\n");
    printf("╚══════════════════════════════════════════════════╝\n\n");
}

/* Resolve a --update value to an actual path.
 *
 *   - If `token` is an existing file or directory, return it verbatim.
 *   - Otherwise, treat `token` as a version string and search the bundled
 *     `./updates/` folder (also parent `../updates/`) for a bundle whose
 *     version field matches.  Accepted forms:
 *         "150"   decimal version × 10 (the on-disk 4-digit field, leading
 *                  zero allowed: "0150" or "150")
 *         "1.5"   human-readable dotted version
 *         "1.80"  → "0180"
 *     The bundle naming convention is pin2000_<game_id>_<vvvv>_<date>_...
 *     If g_emu.game_prefix is set to "swe1" or "rfm", the matching bundle
 *     is also filtered by game_id (50069 or 50070); otherwise the first
 *     version match wins.
 *
 * Writes the resolved path into `out` (size cap) and returns true on
 * success; returns false if no match found. */
static bool resolve_update_token(const char *token, char *out, size_t out_sz)
{
    if (!token || !*token) return false;

    /* Pass-through: existing file or directory */
    struct stat st;
    if (stat(token, &st) == 0) {
        strncpy(out, token, out_sz - 1);
        out[out_sz - 1] = '\0';
        return true;
    }

    bool latest = (strcasecmp(token, "latest") == 0);

    /* Normalize to 4-digit version × 10 (e.g. "1.5" → "0150", "150"→"0150"). */
    char want[8] = {0};
    const char *dot = (latest ? NULL : strchr(token, '.'));
    if (latest) {
        /* sentinel — matched by version-max sweep below */
    } else if (dot) {
        int major = atoi(token);
        int minor = atoi(dot + 1);
        /* For "1.5" we want "0150"; for "1.80" we want "0180". Detect if the
         * caller already wrote a two-digit minor (e.g. "1.80" → minor=80)
         * vs a single-digit minor (e.g. "2.1" → minor=1, treat as 10). */
        int minor_len = 0;
        for (const char *p = dot + 1; *p && isdigit((unsigned char)*p); p++) minor_len++;
        if (minor_len == 1) minor *= 10;
        int vvvv = major * 100 + minor;
        snprintf(want, sizeof(want), "%04d", vvvv);
    } else {
        int v = atoi(token);
        snprintf(want, sizeof(want), "%04d", v);
    }

    const char *want_gid = NULL;
    if (strcmp(g_emu.game_prefix, "swe1") == 0) want_gid = "50069";
    else if (strcmp(g_emu.game_prefix, "rfm") == 0) want_gid = "50070";

    const char *search_dirs[] = { "./updates", "../updates", "updates", NULL };
    char best_path[1024] = {0};
    int  best_v = -1;
    for (int i = 0; search_dirs[i]; i++) {
        DIR *d = opendir(search_dirs[i]);
        if (!d) continue;
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (de->d_name[0] == '.') continue;
            /* Expected form: pin2000_<gid>_<vvvv>_...  */
            if (strncmp(de->d_name, "pin2000_", 8) != 0) continue;
            const char *p = de->d_name + 8;
            if (want_gid) {
                if (strncmp(p, want_gid, 5) != 0) continue;
                p += 5;
                if (*p != '_') continue;
                p++;
            } else {
                /* skip the 5-digit gid */
                for (int k = 0; k < 5 && isdigit((unsigned char)*p); k++) p++;
                if (*p != '_') continue;
                p++;
            }
            if (latest) {
                /* Take the highest 4-digit version field across all matches. */
                int v = 0;
                for (int k = 0; k < 4 && isdigit((unsigned char)p[k]); k++)
                    v = v * 10 + (p[k] - '0');
                if (v > best_v) {
                    best_v = v;
                    snprintf(best_path, sizeof(best_path), "%s/%s",
                             search_dirs[i], de->d_name);
                }
                continue;
            }
            if (strncmp(p, want, 4) != 0) continue;
            /* Match.  Return the directory path (rom.c's load_update_anyform
             * handles both dirs and files). */
            snprintf(out, out_sz, "%s/%s", search_dirs[i], de->d_name);
            closedir(d);
            return true;
        }
        closedir(d);
    }
    if (latest && best_v >= 0) {
        strncpy(out, best_path, out_sz - 1);
        out[out_sz - 1] = '\0';
        return true;
    }
    return false;
}

/* Apply one option (key, optional value) to g_emu. Used by both CLI parsing
 * and yaml config loading so behaviour stays consistent across both paths.
 * Returns 1 if value was consumed, 0 if option was a bare flag, -1 on
 * unknown option. The caller decides whether unknowns are fatal. */
static int apply_option(const char *key, const char *value)
{
    if (strcmp(key, "game") == 0 && value) {
        strncpy(g_emu.game_prefix, value, sizeof(g_emu.game_prefix) - 1);
        return 1;
    }
    if (strcmp(key, "roms") == 0 && value) {
        strncpy(g_emu.roms_dir, value, sizeof(g_emu.roms_dir) - 1);
        return 1;
    }
    if (strcmp(key, "savedata") == 0 && value) {
        strncpy(g_emu.savedata_dir, value, sizeof(g_emu.savedata_dir) - 1);
        return 1;
    }
    if (strcmp(key, "serial-tcp") == 0 && value) {
        g_emu.serial_tcp_port = atoi(value); return 1;
    }
    if (strcmp(key, "keyboard-tcp") == 0 && value) {
        g_emu.keyboard_tcp_port = atoi(value); return 1;
    }
    if (strcmp(key, "headless") == 0) { g_emu.headless = true; return 0; }
    if (strcmp(key, "no-savedata") == 0) { g_emu.no_savedata = true; return 0; }
    if (strcmp(key, "dcs-mode") == 0 && value) {
        if (strcmp(value, "bar4-patch") == 0) {
            g_emu.dcs_mode_choice = ENCORE_DCS_BAR4_PATCH;
        } else if (strcmp(value, "io-handled") == 0) {
            g_emu.dcs_mode_choice = ENCORE_DCS_IO_HANDLED;
        } else {
            fprintf(stderr,
                "[main] --dcs-mode '%s' invalid; expected bar4-patch|io-handled "
                "(falling back to bar4-patch)\n", value);
            g_emu.dcs_mode_choice = ENCORE_DCS_BAR4_PATCH;
        }
        return 1;
    }
    if (strcmp(key, "fullscreen") == 0) { g_emu.start_fullscreen = true; return 0; }
    if (strcmp(key, "flipscreen") == 0) { g_emu.start_flipscreen = true; return 0; }
    if (strcmp(key, "bpp") == 0 && value) {
        int b = atoi(value);
        if (b == 16 || b == 32) g_emu.bpp = b;
        else if (b == 24) {
            fprintf(stderr, "[main] --bpp 24 not supported, falling back to 32\n");
            g_emu.bpp = 32;
        } else {
            fprintf(stderr, "[main] --bpp %d invalid; using 32\n", b);
            g_emu.bpp = 32;
        }
        return 1;
    }
    if (strcmp(key, "lpt-device") == 0 && value) {
        strncpy(g_emu.lpt_device, value, sizeof(g_emu.lpt_device) - 1);
        g_emu.lpt_device_explicit = true;
        return 1;
    }
    if (strcmp(key, "update") == 0 && value) {
        if (strcasecmp(value, "none") == 0) {
            g_emu.update_file[0] = '\0';
            g_emu.update_explicit_none = true;
            printf("[main] --update none → boot from base ROMs only "
                   "(may not reach a usable state)\n");
            return 1;
        }
        char resolved[1024];
        if (!resolve_update_token(value, resolved, sizeof(resolved))) {
            fprintf(stderr,
                "[main] --update: could not resolve '%s' — not a file/dir "
                "and no bundle under ./updates matched that version\n", value);
            return 1;
        }
        strncpy(g_emu.update_file, resolved, sizeof(g_emu.update_file) - 1);
        if (strcmp(resolved, value) != 0)
            printf("[main] --update: resolved '%s' → %s\n", value, resolved);

        /* Infer game_prefix from the bundle name (pin2000_<gid>_...)
         * whether resolved is a directory or a file. */
        const char *bn = strrchr(resolved, '/');
        bn = bn ? bn + 1 : resolved;
        if (strncmp(bn, "pin2000_50069", 13) == 0) {
            strncpy(g_emu.game_prefix, "swe1", sizeof(g_emu.game_prefix) - 1);
            printf("[main] --update: bundle for SWE1 → forcing prefix=swe1\n");
        } else if (strncmp(bn, "pin2000_50070", 13) == 0) {
            strncpy(g_emu.game_prefix, "rfm", sizeof(g_emu.game_prefix) - 1);
            printf("[main] --update: bundle for RFM → forcing prefix=rfm\n");
        }

        FILE *f = fopen(g_emu.update_file, "rb");
        if (f) {
            uint32_t gid = 0;
            if (fseek(f, 0x3C, SEEK_SET) == 0 && fread(&gid, 4, 1, f) == 1) {
                if (gid == 50069u) {
                    strncpy(g_emu.game_prefix, "swe1", sizeof(g_emu.game_prefix) - 1);
                    printf("[main] --update: detected SWE1 (game_id=%u) → forcing prefix=swe1\n", gid);
                } else if (gid == 50070u) {
                    strncpy(g_emu.game_prefix, "rfm", sizeof(g_emu.game_prefix) - 1);
                    printf("[main] --update: detected RFM (game_id=%u) → forcing prefix=rfm\n", gid);
                } else {
                    printf("[main] --update: WARN unknown game_id=%u at file offset 0x3C\n", gid);
                }
            }
            fclose(f);
        }
        return 1;
    }
    return -1;
}

/* Trim leading and trailing ASCII whitespace from s in place. */
static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
        e--;
    *e = '\0';
    return s;
}

/* Minimal yaml subset loader: one "key: value" per line, # comments, blank
 * lines ignored. Bare flags can be written as "headless: true" or just
 * "headless:". Quoted values have their surrounding quotes stripped. */
static int load_config_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[1024];
    int applied = 0;
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        char *hash = strchr(p, '#');
        if (hash) *hash = '\0';
        p = trim(p);
        if (!*p) continue;
        char *colon = strchr(p, ':');
        if (!colon) continue;
        *colon = '\0';
        char *key = trim(p);
        char *val = trim(colon + 1);
        size_t vl = strlen(val);
        if (vl >= 2 && ((val[0] == '"' && val[vl-1] == '"') ||
                        (val[0] == '\'' && val[vl-1] == '\''))) {
            val[vl-1] = '\0'; val++;
        }
        const char *vptr = (*val) ? val : NULL;
        /* Bare boolean shortcuts. */
        if (vptr && (strcasecmp(vptr, "true") == 0 || strcasecmp(vptr, "yes") == 0
                  || strcasecmp(vptr, "on") == 0 || strcmp(vptr, "1") == 0))
            vptr = NULL; /* treat as bare flag */
        if (vptr && (strcasecmp(vptr, "false") == 0 || strcasecmp(vptr, "no") == 0
                  || strcasecmp(vptr, "off") == 0 || strcmp(vptr, "0") == 0))
            continue; /* skip; we don't have explicit-off */
        int rc = apply_option(key, vptr);
        if (rc < 0) {
            fprintf(stderr, "[config] %s: unknown key '%s'\n", path, key);
        } else {
            applied++;
        }
    }
    fclose(f);
    LOG("config", "loaded %d setting(s) from %s\n", applied, path);
    return 0;
}

static void parse_args(int argc, char **argv)
{
    /* Defaults: auto-detect game, standard paths */
    strncpy(g_emu.game_prefix, "auto", sizeof(g_emu.game_prefix));
    strncpy(g_emu.roms_dir, "./roms", sizeof(g_emu.roms_dir));
    strncpy(g_emu.savedata_dir, "./savedata", sizeof(g_emu.savedata_dir));
    g_emu.bpp = 32;

    /* First pass: scan for --config so explicit config loads BEFORE other
     * CLI args, allowing CLI to override config (CLI > config > defaults). */
    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "--config") == 0) {
            strncpy(g_emu.config_file, argv[i + 1], sizeof(g_emu.config_file) - 1);
            load_config_file(g_emu.config_file);
            break;
        }
    }

    /* Auto-load ./encore.yaml only when no CLI args were given AND the
     * file exists. Loud log so this isn't a silent surprise. */
    if (argc == 1 && !g_emu.config_file[0]) {
        if (access("encore.yaml", R_OK) == 0) {
            LOG("config", "no CLI args — auto-loading ./encore.yaml\n");
            strncpy(g_emu.config_file, "encore.yaml", sizeof(g_emu.config_file) - 1);
            load_config_file("encore.yaml");
        }
    }

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strncmp(a, "--", 2) != 0) continue;
        const char *key = a + 2;
        if (strcmp(key, "config") == 0) { i++; continue; } /* already handled */
        if (strcmp(key, "help") == 0 || strcmp(a, "-h") == 0) {
            goto print_help;
        }
        const char *val = (i + 1 < argc && argv[i + 1][0] != '-') ? argv[i + 1] : NULL;
        int rc = apply_option(key, val);
        if (rc < 0) {
            fprintf(stderr, "[main] unknown option: %s\n", a);
            continue;
        }
        if (rc == 1) i++;
    }
    return;
print_help:
            printf(
"Usage: encore [OPTIONS]\n"
"\n"
"════════════════════════════════════════════════════════════════════════\n"
" Game / paths\n"
"════════════════════════════════════════════════════════════════════════\n"
"  --game swe1|rfm|auto   Game selection (default: auto-detect from ROMs)\n"
"  --roms /path           ROM directory (default: ./roms)\n"
"  --savedata /path       Save data directory\n"
"  -h, --help             Show this help\n"
"\n"
"════════════════════════════════════════════════════════════════════════\n"
" Network console (no SDL window required)\n"
"════════════════════════════════════════════════════════════════════════\n"
"\n"
"  --serial-tcp PORT      Bidirectional bridge ↔ emulated COM1 UART.\n"
"                         localhost only. Single client (new conn replaces).\n"
"\n"
"      What you get: the same data stream a real Pinball 2000 tech sees on\n"
"      HyperTerminal at 9600 8N1 — every XINA shell prompt, pinevents log\n"
"      line, assertion, and crash dump. Bytes you SEND are pushed into the\n"
"      UART RX path (RBR/LSR.DR/IIR.RDA + IRQ4) so XINA's serial input\n"
"      handler picks them up.\n"
"\n"
"      Example — watch boot output:\n"
"          # term 1\n"
"          ./build/encore --game swe1 --serial-tcp 4444\n"
"          # term 2\n"
"          nc localhost 4444\n"
"\n"
"      Example — capture a boot log to disk:\n"
"          nc -q5 localhost 4444 > boot.log\n"
"\n"
"      Example — fully scripted XINA session (CI-friendly):\n"
"          ./build/encore --game swe1 --headless --serial-tcp 4444 &\n"
"          sleep 6\n"
"          { sleep 1; printf '?\\nfb\\n'; sleep 2; } | nc -q3 localhost 4444\n"
"\n"
"  --keyboard-tcp PORT    Forward TCP bytes as PS/2 Set 1 scancodes.\n"
"                         localhost only. Single client. EXPERIMENTAL.\n"
"\n"
"      What you get: each ASCII byte → make+break scan codes pushed to\n"
"      port 0x60. Shift auto-applied for caps/symbols. IRQ1 raised while\n"
"      bytes are queued. Same shape as plugging a real PC/AT keyboard\n"
"      into the head-cabinet 5-pin DIN, per the gerwiki XINA doc.\n"
"\n"
"      Example — type into the guest as if at the head keyboard:\n"
"          # term 1\n"
"          ./build/encore --game swe1 --serial-tcp 4444 --keyboard-tcp 4445\n"
"          # term 2 — read XINA output\n"
"          nc localhost 4444\n"
"          # term 3 — type commands\n"
"          nc localhost 4445\n"
"          # then in term 3 type:   ?<Enter>   help<Enter>   fb<Enter>\n"
"\n"
"      Mapped keys: a-z, A-Z (auto-Shift), 0-9, space, Enter, Tab,\n"
"          Backspace, Esc, and -=[];',./\\\\` plus shifted !@*()_+:\"?\n"
"          NOT mapped: F-keys, arrows, Ctrl, Alt, Insert/Home/End/PgUp/PgDn\n"
"          (those need E0-prefixed extended scan codes — not implemented).\n"
"          For F-keys and gameplay buttons during a run, use the SDL\n"
"          window keys listed below.\n"
"\n"
"      Why experimental: the KBC stub in this emulator was previously\n"
"          only answering BIOS self-test (cmd 0xAA → 0x55, 0xAB → 0x00).\n"
"          The full PS/2 negotiation (8042 cmd 0xAE 'enable kbd', scancode\n"
"          set selection 0xF0) is NOT emulated. XINA may or may not pick\n"
"          up keystrokes depending on what its keyboard driver actually\n"
"          probes for. If it doesn't, fall back to --serial-tcp which is\n"
"          the proven driveable shell path.\n"
"\n"
"  --headless             Skip SDL window AND audio init — pure CPU.\n"
"\n"
"      Use case: CI runners, regression burn-in, scripted XINA sessions,\n"
"      or running encore on a server. Combine with --serial-tcp so you\n"
"      still have a way to see what the guest is doing.\n"
"\n"
"      Example — minimal CI invocation:\n"
"          ./build/encore --game swe1 --headless --serial-tcp 4444 &\n"
"          ENC_PID=$!\n"
"          sleep 8                                  # let it boot\n"
"          nc -q2 localhost 4444 | grep 'STARTING GAME CODE' && echo OK\n"
"          kill $ENC_PID\n"
"\n"
"════════════════════════════════════════════════════════════════════════\n"
" Real cabinet — drive an actual Pinball 2000 driver board via host LPT\n"
"════════════════════════════════════════════════════════════════════════\n"
"\n"
"  --lpt-device PATH      Forward all guest LPT (0x378/0x379/0x37A) traffic\n"
"                         to a real parallel port via Linux ppdev.\n"
"                         Default: /dev/parport0 (auto-detect; silent\n"
"                         fallback to emulation if absent).\n"
"                         PATH = 'none' / 'emu' → force emulation.\n"
"                         PATH explicitly given → fail hard if open fails\n"
"                         (so you don't silently end up emulated when you\n"
"                         expected real hardware).\n"
"\n"
"      What you get: bytes the guest writes to 0x378/0x37A go to the real\n"
"          port; reads pull live data from the cabinet driver board. The\n"
"          emulated state machine (opcode latch, switch matrix, button\n"
"          injection) is bypassed — the cabinet owns the protocol. F-key /\n"
"          SDL switch injection becomes inert (real switches are read from\n"
"          the actual board).\n"
"\n"
"      Prerequisites (one-time setup):\n"
"          sudo modprobe ppdev parport parport_pc\n"
"          sudo usermod -a -G lp $USER          # then re-login\n"
"          sudo rmmod lp                        # if printer driver claims it\n"
"          # confirm bidirectional support:\n"
"          ls -l /dev/parport0                  # should exist, owned by lp\n"
"\n"
"      Example — drive a connected cabinet:\n"
"          ./build/encore --game swe1 --lpt-device /dev/parport0\n"
"\n"
"      Example — force emulation even if /dev/parport0 exists:\n"
"          ./build/encore --game swe1 --lpt-device none\n"
"\n"
"      Notes / gotchas:\n"
"          * Hardware port is opened at startup but stays guest-invisible\n"
"            (returns 0xFF) until the existing UART-driven activation gate\n"
"            fires (XINU/Allegro detected). This preserves boot-probe\n"
"            semantics so the guest's BIOS/PinIO sees the same bring-up\n"
"            sequence as in pure-emulated mode.\n"
"          * Direction: encore unconditionally flips PPDATADIR to input\n"
"            around data-port reads and back to output afterwards. This\n"
"            mirrors what the original P2K driver expects (verified by RE\n"
"            of P2K-driver: its data-read handler gates on renderingFlags\n"
"            bits 0+3 only — the protocol never touches control bit 5).\n"
"          * On exit (clean or SIGINT/SIGTERM), the port is released. Hard\n"
"            crashes (SIGKILL, segfault) may leave it claimed — re-running\n"
"            encore re-claims it; otherwise reboot or rmmod/modprobe ppdev.\n"
"          * Combine with --serial-tcp to drive XINA over TCP while the\n"
"            cabinet handles physical I/O.\n"
"\n"
"════════════════════════════════════════════════════════════════════════\n"
" Maybe-fun future ideas (not implemented — left as bread crumbs)\n"
"════════════════════════════════════════════════════════════════════════\n"
"  --record FILE / --replay FILE   deterministic input capture/replay\n"
"                                  (timestamps every LPT switch, kbd, serial)\n"
"  --http PORT                     read-only status endpoint (FPS, switch\n"
"                                  matrix, register snapshot, RAM peek)\n"
"  --lpt-trace FILE                opt-in LPT bus trace dump\n"
"  --xina-script FILE              type a list of XINA commands at boot\n"
"                                  (sugar over --keyboard-tcp / --serial-tcp)\n"
"  --net-bridge tap0               TUN/TAP for RFM internet leaderboard\n"
"                                  (would require emulating the on-board NIC)\n"
"\n"
"════════════════════════════════════════════════════════════════════════\n"
" SDL window key bindings (F-row keys are positionally identical on every\n"
" keyboard layout — works on QWERTY, AZERTY, DVORAK, etc.)\n"
"════════════════════════════════════════════════════════════════════════\n"
"\n"
"  F1               Quit\n"
"  F2               Flip display vertically\n"
"  F3               Screenshot (PNG to ./screenshots/, override with\n"
"                   $ENCORE_SCREENSHOT_DIR)\n"
"  F4               Toggle COIN DOOR (closed/open interlock)\n"
"  F6               LEFT  action button       (Phys[10].b7)\n"
"  F7               LEFT  flipper             (Phys[10].b5)\n"
"  F8               RIGHT flipper             (Phys[10].b4)\n"
"  F9               RIGHT action button       (Phys[10].b6)\n"
"  F10 / C          Insert credit (queueable; mash for multi)\n"
"  F11 / ALT+ENTER  Toggle FULLSCREEN\n"
"  F12              Dump guest switch state to stderr\n"
"  SPACE / S        START button              (sw=2 / Phys[0].b2)\n"
"  ALT+K            Toggle raw keyboard CAPTURE (route every key to guest\n"
"                   PS/2 KBC; F-key gameplay bindings disabled while ON;\n"
"                   F1 still quits, ALT+K toggles back).\n"
"\n"
"════════════════════════════════════════════════════════════════════════\n"
" Session / display\n"
"════════════════════════════════════════════════════════════════════════\n"
"  --no-savedata          Don't load NVRAM/SEEPROM at boot AND don't save\n"
"                         on exit. Useful for fresh-state regression runs.\n"
"  --fullscreen           Open the SDL window fullscreen at startup.\n"
"  --flipscreen           Start with the display Y-flipped (some cabs).\n"
"  --bpp 16|32            Output texture bit depth (default 32 / ARGB8888;\n"
"                         16 = RGB565). 24 falls back to 32 with a warning.\n"
"  --dcs-mode MODE        How the DCS-2 sound subsystem is reached.\n"
"                           bar4-patch  (default) — pattern-scan + 5-byte\n"
"                              CMP/JNE prologue patch at xinu_ready, forces\n"
"                              dcs_mode=1, audio via PCI BAR4 + sound.c mixer.\n"
"                              Proven on every bundle (SWE1 v1.5/v2.1 + RFM\n"
"                              v1.2/1.6/1.8/2.5/2.6).\n"
"                           io-handled — skip the patch; the game runs its\n"
"                              unmodified PCI-detect probe. Bundles whose\n"
"                              natural probe returns 1 use BAR4 (e.g. SWE1\n"
"                              v1.5); the rest fall through to the DCS2 UART\n"
"                              port handlers (0x138-0x13F) in io.c. Audio\n"
"                              coverage on the UART-only path is partial — a\n"
"                              full I/O handshake pump is WIP.\n"
"  --config FILE.yaml     Load options from a yaml-ish file (one key:value\n"
"                         per line; '#' starts a comment). CLI args override\n"
"                         config; auto-loads ./encore.yaml when no CLI args.\n"
"\n"
"  Coin-door panel (4 buttons, dual-function by mode):\n"
"                          attract        |   service / test\n"
"                          --------------- + -----------------\n"
"  ESC   / LEFT      btn1: Service Credits | Escape     (Phys[9].b0)\n"
"  DOWN  / KP_-      btn2: Volume −        | Menu Down  (Phys[9].b1)\n"
"  UP    / KP_+      btn3: Volume +        | Menu Up    (Phys[9].b2)\n"
"  RIGHT / ENTER /   btn4: Begin Test      | Enter      (Phys[9].b3)\n"
"      KP_ENTER\n"
"\n");
            exit(0);
}

/*
 * ROM-agnostic pattern scanning: find a 4-byte value in guest RAM.
 * Returns offset or 0 if not found.
 */
__attribute__((unused))
static uint32_t scan_ram_u32(uint32_t start, uint32_t end, uint32_t target)
{
    if (!g_emu.ram || end > RAM_SIZE) return 0;
    for (uint32_t off = start; off + 4 <= end; off += 4) {
        if (*(uint32_t *)(g_emu.ram + off) == target)
            return off;
    }
    return 0;
}

/*
 * Minimal boot assistance — applied only when correct emulation alone
 * doesn't suffice. ROM-agnostic: uses pattern scanning, not hardcoded addresses.
 */
static void apply_boot_assist(void)
{
    uint8_t *ram = g_emu.ram;
    if (!ram) return;

    /* 1. IVT safety — fill IVT[0..255] with IRET+EOI stub at 0x20000.
     *    Required because Unicorn doesn't have default interrupt handlers. */
    {
        uint8_t *stub = ram + 0x20000u;
        stub[0] = 0x50;                     /* PUSH AX */
        stub[1] = 0xB0; stub[2] = 0x20;     /* MOV AL, 0x20 */
        stub[3] = 0xE6; stub[4] = 0x20;     /* OUT 0x20, AL (EOI) */
        stub[5] = 0x58;                     /* POP AX */
        stub[6] = 0xCF;                     /* IRET */
        uint32_t *ivt = (uint32_t *)ram;
        for (int i = 0; i < 256; i++) ivt[i] = 0x20000000u;
        LOG("boot", "IVT[0..255] → IRET+EOI stub at 0x20000\n");
    }

    /* 2. Safety stub at 0x400000: MOV EAX,1; RET — catches null vtable calls.
     *    This is emulator infrastructure, not a guest patch. */
    {
        uint8_t *stub = ram + 0x400000u;
        stub[0] = 0xB8; stub[1] = 0x01; stub[2] = 0x00;
        stub[3] = 0x00; stub[4] = 0x00; stub[5] = 0xC3;
    }

    fflush(stdout);
}

/*
 * Apply post-ROM-copy patches. Called AFTER option ROM is copied to 0x80000
 * (by cpu_setup_protected_mode). These patches are in-RAM, not in ROM files.
 */
static void apply_ram_patches(void)
{
    /* Safety halt at option ROM offset 0x1F7 (0x801F7):
     * This is garbled 16-bit PM switch code that shouldn't execute.
     * If Init2 returns instead of jumping to game code, we catch it cleanly. */
    uint8_t halt_loop[] = { 0xF4, 0xEB, 0xFD };  /* HLT; JMP $-3 */
    uc_mem_write(g_emu.uc, 0x801F7, halt_loop, sizeof(halt_loop));
    LOG("boot", "Safety halt at 0x801F7 (post-Init2 fallthrough catcher)\n");
}

/* DCS presence table — no longer zeroed. Game needs it to detect DCS2
 * hardware and create the DCS2 driver task. NonFatal from missing DCS
 * board is caught by the NonFatal→XOR EAX,EAX;RET patch in cpu.c. */

/* PRISM option ROM framebuffer write NOP + checksum fix.
 * ROM-agnostic: scan for the MOV [EDI+...], EAX pattern. */
static void patch_prism_rom(void)
{
    if (!g_emu.rom_banks[0] || g_emu.rom_sizes[0] < PRISM_ROM_SIZE) return;
    uint8_t *rom = g_emu.rom_banks[0];

    if (rom[0] != 0x55 || rom[1] != 0xAA) return;

    uint32_t rom_len = rom[2] * 512u;
    if (rom_len > PRISM_ROM_SIZE) rom_len = PRISM_ROM_SIZE;

    /* Scan for framebuffer write pattern: 89 87 xx xx xx xx (MOV [EDI+disp32], EAX)
     * where disp32 is a high address (framebuffer). Look for writes to 0x40xxxxxx. */
    for (uint32_t off = 0x100; off < rom_len - 6; off++) {
        if (rom[off] == 0x89 && rom[off+1] == 0x87) {
            uint32_t disp = *(uint32_t *)&rom[off+2];
            if ((disp & 0xFF000000u) == 0x40000000u) {
                for (int i = 0; i < 6; i++) rom[off + i] = 0x90; /* NOP */
                LOG("boot", "PRISM ROM: NOP'd framebuf write at 0x%x (disp=0x%08x)\n", off, disp);
                break;
            }
        }
    }

    /* Fix checksum */
    uint8_t sum = 0;
    for (uint32_t i = 0; i < rom_len; i++) sum += rom[i];
    if (sum != 0) {
        rom[rom_len - 1] -= sum;
        LOG("boot", "PRISM ROM: checksum fixed\n");
    }
}

static void setup_timer(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = cpu_timer_handler;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGALRM, &sa, NULL);

    /* SIGALRM disabled — tick injection is iteration-count based.
     * SIGALRM signal delivery was interfering with Unicorn JIT execution
     * (signal masking, cpu_exit from signal handler context).
     * HLT wakeup handled by busy-wait with nanosleep. */
    LOG("timer", "SIGALRM disabled — iteration-count ticks only\n");
}

static void cleanup_and_save(void)
{
    /* Save state before exit (unless --no-savedata gates it) */
    if (!g_emu.no_savedata)
        savedata_save();
    else
        LOG("save", "--no-savedata: skipping NVRAM/SEEPROM save\n");

    display_cleanup();
    sound_cleanup();
}

int main(int argc, char **argv)
{
    memset(&g_emu, 0, sizeof(g_emu));
    print_banner();
    parse_args(argc, argv);

    /* Resolve the game prefix early so the auto-update gate below works
     * even when --game is omitted (default "auto"). rom_detect_game()
     * short-circuits when a concrete prefix is already set, so the
     * second call from rom_load_all() below is a no-op. */
    if (rom_detect_game() != 0) {
        fprintf(stderr, "Failed to detect game from ROMs\n");
        return 1;
    }

    /* Auto-pick latest update bundle when none specified.
     * Base ROMs alone don't reach a usable state on this platform: XINA
     * panics with "sysinit: game code overlaps 640k - 1024k hole" on RFM,
     * and DCS sound never wires up on SWE1. Real cabinets always shipped
     * with at least one update flashed; mirror that. Override with
     * --update none|<version|path>. Only applies when the game prefix
     * resolves to swe1 or rfm (whether via --game or auto-detect). */
    if (g_emu.update_file[0] == '\0' && !g_emu.update_explicit_none
        && (strcmp(g_emu.game_prefix, "swe1") == 0
            || strcmp(g_emu.game_prefix, "rfm") == 0)) {
        char resolved[1024];
        if (resolve_update_token("latest", resolved, sizeof(resolved))) {
            strncpy(g_emu.update_file, resolved,
                    sizeof(g_emu.update_file) - 1);
            printf("[main] no --update given → auto-selected latest bundle: %s\n",
                   resolved);
        } else {
            fprintf(stderr,
                "[main] WARN no --update given and no bundle found under "
                "updates/ — base ROMs alone are not expected to boot.\n");
        }
    }

    LOG("init", "Game: %s | ROMs: %s | Savedata: %s\n",
        g_emu.game_prefix, g_emu.roms_dir, g_emu.savedata_dir);

    /* Initialize subsystems in order:
     * 1. ROMs (load files from disk)
     * 2. CPU (create Unicorn engine)
     * 3. Memory (map guest physical address space)
     */
    if (rom_load_all() != 0) {
        fprintf(stderr, "Failed to load ROMs\n");
        return 1;
    }

    /* Build the XINU symbol-table index from the freshly-loaded update
     * flash.  Safe to call even when no symbols are present — sym_lookup()
     * just returns 0 and patch sites fall back to hardcoded constants. */
    sym_init();

    if (cpu_init() != 0) {
        fprintf(stderr, "Failed to init CPU\n");
        return 1;
    }

    if (memory_init() != 0) {
        fprintf(stderr, "Failed to init memory\n");
        return 1;
    }

    io_init();
    bar_init();  /* sets EEPROM defaults, but .see file already loaded by rom_init */

    /* Re-apply EEPROM defaults — .see file may contain stale data from prior POC */
    bar_seeprom_reinit();

    /* Populate NIC LAN ROM data in D-segment guest RAM (BT-131) */
    nic_dseg_init();

    /* Boot assistance (IVT stubs, PRISM ROM fix).
     * DCS presence table left intact so game detects DCS2 hardware. */
    apply_boot_assist();
    patch_prism_rom();

    /* Load NVRAM/SEEPROM into guest memory after mapping */
    if (g_emu.bar2_sram[0] || g_emu.bar2_sram[1]) {
        uc_mem_write(g_emu.uc, WMS_BAR2, g_emu.bar2_sram, BAR2_SIZE);
        LOG("init", "NVRAM loaded into guest BAR2\n");
    }

    /* Write flash into guest BAR3 */
    if (g_emu.flash)
        uc_mem_write(g_emu.uc, WMS_BAR3, g_emu.flash, FLASH_SIZE);

    /* Pre-fill BAR4 with 0xFF (i386 POC copycat: absent DCS2 hardware reads 0xFF).
     * Guest DCS2 driver checks BAR4 initial state during init. */
    {
        uint8_t *bar4_fill = calloc(1, BAR4_SIZE);
        if (bar4_fill) {
            memset(bar4_fill, 0xFF, BAR4_SIZE);
            uc_mem_write(g_emu.uc, WMS_BAR4, bar4_fill, BAR4_SIZE);
            free(bar4_fill);
            LOG("init", "BAR4 pre-filled with 0xFF (%u MB)\n", BAR4_SIZE >> 20);
        }
    }

    /* Set up protected mode (skip BIOS, start at option ROM PM code) */
    if (cpu_setup_protected_mode() != 0) {
        fprintf(stderr, "Failed to setup protected mode\n");
        return 1;
    }

    /* Apply RAM patches after ROM is in guest memory */
    apply_ram_patches();

    /* Display and sound (non-fatal if they fail) — both skipped under --headless */
    if (g_emu.headless) {
        LOG("init", "headless mode — SDL display and audio disabled\n");
    } else {
        if (display_init() != 0)
            LOG("warn", "Display init failed — running headless\n");

        if (sound_init() != 0)
            LOG("warn", "Sound init failed — running silent\n");
    }

    /* Network console bridges (no-ops if their --*-tcp ports were not given) */
    netcon_init();

    /* Real LPT passthrough auto-detection.
     * - Default mode probes /dev/parport0 silently; success switches to
     *   real-cabinet mode and disables button emulation, absence is silent.
     * - --lpt-device PATH: explicit request, hard-fails if open() fails.
     * - --lpt-device none: skip probe entirely (force emulation). */
    {
        const char *dev = g_emu.lpt_device[0] ? g_emu.lpt_device : "/dev/parport0";
        bool quiet = !g_emu.lpt_device_explicit;
        if (lpt_passthrough_open(dev, quiet) < 0) {
            if (g_emu.lpt_device_explicit) {
                fprintf(stderr,
                    "encore: --lpt-device %s requested but unavailable. "
                    "Aborting (use --lpt-device none to force emulation).\n", dev);
                return 1;
            }
            LOG("lpt", "no real cabinet detected (probed %s) — "
                       "using emulated buttons (F-keys / keyboard)\n", dev);
        }
    }

    /* --game auto resolution.
     *
     * In EMULATED mode (no real LPT board): the chip ROMs already loaded
     * by rom_load_all() determine which game we're running, and
     * --update FILE has already overridden game_prefix when applicable.
     * If we're still on "auto" here, default to swe1 per user spec.
     *
     * In REAL CABINET mode: use lpt_passthrough_detect_game() — ported
     * 1:1 from P2K-driver @ 0x804fe56. It bit-bangs a 3-register probe
     * over the parallel port and classifies the playfield from the
     * weighted popcounts. If the board doesn't answer clearly, bail out
     * rather than guessing (wrong game = wrong coil driver map → risk
     * of damage on real hardware). */
    if (strcmp(g_emu.game_prefix, "auto") == 0) {
        if (lpt_passthrough_active()) {
            char detected[16] = {0};
            if (lpt_passthrough_detect_game(detected, sizeof(detected)) == 0) {
                LOG("init", "LPT board auto-detect → %s\n", detected);
                strncpy(g_emu.game_prefix, detected,
                        sizeof(g_emu.game_prefix) - 1);
                g_emu.game_prefix[sizeof(g_emu.game_prefix) - 1] = '\0';
            } else {
                fprintf(stderr,
                    "encore: --game auto: LPT board did not return a\n"
                    "recognizable playfield signature. Pass --game swe1\n"
                    "or --game rfm explicitly.\n");
                return 1;
            }
        } else {
            LOG("init", "--game auto in emulated mode → defaulting to swe1\n");
            strncpy(g_emu.game_prefix, "swe1",
                    sizeof(g_emu.game_prefix) - 1);
        }
    }

    /* Signal handlers — set running=false so main loop exits and runs
     * normal cleanup (releases LPT, closes TCP, saves NVRAM). */
    {
        struct sigaction sa = { 0 };
        sa.sa_handler = on_term_signal;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGINT,  &sa, NULL);
        sigaction(SIGTERM, &sa, NULL);
        sigaction(SIGHUP,  &sa, NULL);
    }
    atexit(lpt_passthrough_close);

    /* Start timer and run */
    setup_timer();
    g_emu.running = true;

    LOG("cpu", "Starting Encore in protected mode...\n\n");
    fflush(stdout);

    cpu_run();

    cleanup_and_save();
    netcon_cleanup();
    lpt_passthrough_close();

    LOG("exit", "Encore finished (exec_count=%lu frames=%d)\n",
        (unsigned long)g_emu.exec_count, g_emu.frame_count);
    return 0;
}
