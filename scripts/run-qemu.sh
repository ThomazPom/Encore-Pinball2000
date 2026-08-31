#!/usr/bin/env bash
# scripts/run-qemu.sh — product wrapper for QEMU Encore (Williams Pinball 2000).
#
# This wrapper provides the user-facing CLI for the custom
# `pinball2000` QEMU machine. It does NOT modify guest behavior, DCS,
# timing, display, or audio protocol. It only wires CLI args to the
# existing QEMU machine options, env vars, and -display/-audio/-serial
# flags. See `--help` for the full table.
#
# Quick reference:
#   ./scripts/run-qemu.sh --game swe1
#   ./scripts/run-qemu.sh --game swe1 --update none --no-savedata
#   ./scripts/run-qemu.sh --game swe1 --update 0210
#   ./scripts/run-qemu.sh --game swe1 --update latest
#   ./scripts/run-qemu.sh --game rfm
#   ./scripts/run-qemu.sh --headless --game swe1
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT/scripts/internal/runtime-packages.sh"
ORIGINAL_ARGS=("$@")

# Preserve explicit environment selection; the CLI can replace it below.
export P2K_DCS_ENGINE="${P2K_DCS_ENGINE:-adsp-hybrid-thread}"

# --bench is an orchestration mode, not a QEMU machine option.  Dispatch it
# before normal argument parsing and forward every other option (game, update,
# strict, etc.) to the isolated self-diagnostic runner.
for __arg in "$@"; do
  if [[ "$__arg" == "--bench" ]]; then
    __bench_args=()
    for __forward in "$@"; do
      [[ "$__forward" == "--bench" ]] || __bench_args+=("$__forward")
    done
    exec python3 "$ROOT/scripts/internal/bench-qemu.py" "${__bench_args[@]}"
  fi
done

# --- defaults ---------------------------------------------------------------
GAME=auto
ROMS_DIR="$ROOT/roms"
SAVEDATA_DIR="$ROOT/savedata"
UPDATE_TOKEN="auto"
DISPLAY_MODE=""
HEADLESS=0
FULLSCREEN=0
FRAMEBUFFER_REQUEST="${P2K_FRAMEBUFFER_THREAD:-auto}"
case "$FRAMEBUFFER_REQUEST" in
  auto|0|1) ;;
  *)
    echo "[run-qemu] P2K_FRAMEBUFFER_THREAD: expected auto, 0, or 1" >&2
    exit 2
    ;;
esac
unset P2K_FRAMEBUFFER_THREAD
NO_SAVEDATA=0
FRESH_SAVEDATA=0
CLEAR_PB2K_ADSP_CACHE=0
PB2K_ADSP_CACHE_DIR="${P2K_PB2K_ADSP_CACHE_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/encore-pb2k/pb2kslib-adsp}"
PB2K_ADSP_CACHE_WORKERS="${P2K_PB2K_ADSP_CACHE_WORKERS:-6}"
export P2K_PB2K_ADSP_CACHE_DIR="$PB2K_ADSP_CACHE_DIR"
MONITOR=""
DEBUG=""
TCG_ONLY=0
VERBOSITY=0
UART_QUIET=0
AUDIO=""
SOUND_LOADING="lazy"
UART_TCP=""
SERIAL_STDIO=0
CONSOLE_SCRIPT=""
AUTOMATION_DIR=""
SPEED_TARGET="${P2K_SPEED_TARGET_PERCENT:-100}"
EXTRA=()
QEMU_FB_ASYNC_DRIVER="${P2K_QEMU_FB_ASYNC_DRIVER:-auto}"
SDL_VIDEO_DRIVER_REQUEST=""
PREFLIGHT=0
RUNTIME_BACKEND=""
BACKEND_WRAPPER=""
NETWORK=0
NETWORK_NAT=0
NETWORK_PASST=0
NETWORK_MIRROR=0
NETWORK_AUTO=0
HTTP_PORT=""
NETWORK_BRIDGE=""
NETWORK_FORWARDS=()
AUTO_HOSTFWD_SPEC=""
PASST_DIR=""
PASST_PID=""
GUEST_EXTENSIONS=0
GUEST_IP=""
GUEST_MASK=""
GUEST_GATEWAY=""

# --- QEMU binary lookup -----------------------------------------------------
resolve_qemu_bin() {
  [[ -n "${QEMU_BIN:-}" && -x "$QEMU_BIN" ]] && return
  QEMU_BIN=""
  QEMU_BIN="$ROOT/qemu-system-i386"
  [[ -x "$QEMU_BIN" ]] || QEMU_BIN="$HOME/.cache/p2k-qemu-build/qemu-10.0.8/build/qemu-system-i386"
  [[ -x "$QEMU_BIN" ]] || QEMU_BIN="$HOME/.cache/encore-qemu-release/qemu-system-i386"
  [[ -x "$QEMU_BIN" ]] || QEMU_BIN=""
}
resolve_qemu_bin

# --- audio backend support --------------------------------------------------
AUDIO_AUTO_CANDIDATES=(sdl pa alsa oss sndio dbus)
QEMU_AUDIO_HELP=""
QEMU_AUDIO_DRIVERS=""
QEMU_AUDIO_HELP_LOADED=0

qemu_audio_load_supported_drivers() {
  if [[ $QEMU_AUDIO_HELP_LOADED -eq 0 ]]; then
    QEMU_AUDIO_HELP="$("$QEMU_BIN" -audio help 2>/dev/null || true)"
    QEMU_AUDIO_DRIVERS="$(printf '%s\n' "$QEMU_AUDIO_HELP" \
      | sed -n '/Available audio drivers:/,$p' \
      | sed '1d;/^$/d' \
      | awk '{print $1}')"
    QEMU_AUDIO_HELP_LOADED=1
  fi
}

qemu_audio_supported_drivers() {
  qemu_audio_load_supported_drivers
  printf '%s\n' "$QEMU_AUDIO_DRIVERS"
}

audio_backend_supported() {
  local backend="$1"
  qemu_audio_load_supported_drivers
  [[ $'\n'"$QEMU_AUDIO_DRIVERS"$'\n' == *$'\n'"$backend"$'\n'* ]]
}

audio_supported_list() {
  local drivers
  qemu_audio_load_supported_drivers
  drivers="${QEMU_AUDIO_DRIVERS//$'\n'/, }"
  printf '%s' "${drivers:-none}"
}

audio_validate_explicit() {
  local backend="$1"
  if ! audio_backend_supported "$backend"; then
    echo "[run-qemu] --audio: '$backend' not supported by this qemu (not compiled into this qemu binary)" >&2
    echo "  Available audio drivers:" >&2
    qemu_audio_supported_drivers | sed 's/^/    /' >&2
    exit 2
  fi
}

audio_host_reason() {
  local backend="$1"
  case "$backend" in
    pa)
      if command -v pactl >/dev/null 2>&1 && pactl info >/dev/null 2>&1; then
        echo "pulse running"; return 0
      fi
      return 1 ;;
    alsa)
      if [[ -s /proc/asound/cards ]]; then
        echo "ALSA cards present"; return 0
      fi
      return 1 ;;
    sdl) echo "SDL trusted"; return 0 ;;
    oss|sndio|dbus) echo "host check trusted"; return 0 ;;
    *) return 1 ;;
  esac
}

audio_host_services_list() {
  local backend reason out=""
  for backend in "${AUDIO_AUTO_CANDIDATES[@]}"; do
    if reason="$(audio_host_reason "$backend")"; then
      out+="${out:+, }$backend: $reason"
    else
      out+="${out:+, }$backend: unavailable"
    fi
  done
  printf '%s' "$out"
}

audio_pick_auto() {
  local backend reason
  for backend in "${AUDIO_AUTO_CANDIDATES[@]}"; do
    if audio_backend_supported "$backend" && reason="$(audio_host_reason "$backend")"; then
      AUDIO="$backend"
      export P2K_DCS_AUDIO=1
      echo "[run-qemu] audio: auto-selected $backend (host: $reason, qemu: $backend supported)" >&2
      return
    fi
  done

  AUDIO=none
  echo "[run-qemu] audio: no supported backend found in QEMU build {$(audio_supported_list)} matching host services {$(audio_host_services_list)}; running silent. Use \`--audio none\` to suppress this warning." >&2
}

# --- help -------------------------------------------------------------------
print_help() {
    local -a help_filter=(cat)
    if [[ -t 1 && -z "${NO_COLOR:-}" && "${TERM:-}" != dumb ]]; then
        help_filter=(awk '
          BEGIN {
            esc = sprintf("%c", 27)
            reset = esc "[0m"; bold = esc "[1m"; cyan = esc "[36m"
            bright_cyan = esc "[1;36m"; section = esc "[1;34m"
            green = esc "[32m"; bright_green = esc "[1;32m"
            yellow = esc "[33m"; red = esc "[31m"; dim = esc "[2m"
          }
          /^Usage:/ { print bold $0 reset; next }
          /^[A-Z][A-Z /-]+$/ { print section $0 reset; next }
          /^  --/ {
            option_line = substr($0, 3)
            match(option_line, /  +/)
            signature = "  " substr(option_line, 1, RSTART - 1)
            description = substr(option_line, RSTART)
            print cyan signature reset description
            next
          }
          /^    scripts\/run-qemu/ { print bright_cyan $0 reset; next }
          /^    HOTLOOP adsp-thread with/ { print bright_cyan $0 reset; next }
          /^  Faithful ADSP timing candidates/ { print bold $0 reset; next }
          /^    Example steady-state results/ { print bold $0 reset; next }
          /^      Configuration/ { print bold $0 reset; next }
          /^      HOTLOOP.*FAIL$/ { print red $0 reset; next }
          /^      HOTLOOP adsp-thread PCM CPU2/ { print bright_green $0 reset; next }
          /^      HOTLOOP/ { print green $0 reset; next }
          /^    Plain pb2kslib/ || /^    Qualification gates/ ||
          /^    These figures/ { print yellow $0 reset; next }
          /^Run Williams Pinball 2000/ || /^machine\. Stock/ {
            print dim $0 reset; next
          }
          { print }
        ')
    fi
    cat <<'EOF' | "${help_filter[@]}"
Usage: scripts/run-qemu.sh [OPTIONS] [-- <qemu passthrough>]

Run Williams Pinball 2000 firmware under the custom QEMU `pinball2000`
machine. Stock qemu-system-i386 cannot boot — see qemu/README.md.

CORE LAUNCH
  --game auto|swe1|rfm      Game selection. auto identifies a connected
                            playfield, or uses SWE1 with the emulated board.
                            Default: auto.
  --roms <dir>              ROM directory. Default: <repo>/roms.
  --savedata <dir>          Persistent savedata dir (reads
                            <dir>/<game>.{flash,nvram2,see}).
                            Default: <repo>/savedata.
  --no-savedata             Run without persistent savedata (also exports
                            P2K_NO_SAVEDATA=1) and switches cwd to a fresh
                            throwaway dir for the run.
  --fresh                   Ignore existing savedata for this boot, then save
                            the newly initialized state normally on exit.
  --guest-extensions        Add Encore's volatile serial-shell extensions to
                            supported update ROMs. ROM files stay untouched.
  --setip <ip> <mask> <gw>  Enable guest extensions and persist these XINA
                            network resources immediately before netstart.
                            They can later be changed from serial with:
                            setip <ip> <mask> <gateway>.
  --update <spec>           Update bundle selection. Spec is one of:
                              auto      (default) machine auto-discovers
                                        the newest matching bundle in
                                        ./updates/. Falls back to base ROMs
                                        if no bundle is found.
                              latest    explicitly resolve to the highest
                                        version bundle for this game.
                              none      base-ROM mode — no update
                                        bundle is staged. Sets
                                        P2K_NO_AUTO_UPDATE=1, isolating
                                        base-ROM compatibility support
                                        (probe-cell shim, etc.) to this
                                        mode only.
                              0210      short version code (also accepts
                                        "210", "2.10"); resolved against
                                        ./updates/pin2000_<gid>_<vvvv>_*
                              <dir>     explicit path to an inner-game
                                        bundle dir (containing
                                        *_bootdata.rom + *_im_flsh0.rom +
                                        *_game.rom + *_symbols.rom).

DISPLAY / UX
  --display <backend>       QEMU -display backend. The wrapper queries
                            `qemu-system-i386 -display help` and rejects
                            any backend the binary wasn't compiled with.
                            Out of the box this build supports: sdl,
                            none, dbus. Rebuild with `--enable-gtk` if
                            you want gtk.
  --headless                Shortcut for --display none -serial stdio
                            (so you actually see UART output).
                            Combine with --uart-quiet to suppress the
                            UART entirely (uses -serial null).
  --fullscreen              Open the window fullscreen (-full-screen).
  --bpp 16|32               Display surface depth. 32 (default) keeps the
                            ARGB8888 path with RGB555→ARGB conversion. 16
                            switches the QEMU surface to native PIXMAN
                            x1r5g5b5 — the source format the GX framebuffer
                            already uses, so pixels are copied without
                            conversion (P2K_DISPLAY_BPP=16).
  --framebuffer             Direct host renderer (desktop default). QEMU runs with
                            display none while a dedicated SDL thread reads
                            native guest RGB555 RAM and lets SDL scale and
                            present it without host pixel conversion.
                            Use --display <backend> to select QEMU's display
                            path explicitly.
  --wayland                 Use the direct SDL2 renderer as a native Wayland
                            client. Requires WAYLAND_DISPLAY and implies
                            --framebuffer.
  --display-manager         Use the current Wayland display-manager session.
  --cage                    Install/use Cage as a standalone Wayland kiosk.
  --weston                  Install/use Weston as a standalone Wayland kiosk.
  --preflight               Follow the requested launch path and prepare every
                            runtime dependency and asset it encounters, then
                            stop immediately before a compositor or QEMU.
  --flipscreen              Vertically reverse the displayed image, exactly as
                            if F2 had been pressed once. F2 toggles the same
                            state at run time.
  --switch-keymap <yaml>    A-Z to matrix-switch bindings. Missing files are
                            initialized with an editable starter map. Default:
                            $XDG_CONFIG_HOME/encore/switch-keymap.yaml, or
                            ~/.config/encore/switch-keymap.yaml when unset.
                            The strict YAML subset is documented in the
                            desktop-controls guide.
  --qemu-framebuffer        Experimental fast QEMU-console renderer. Keeps the
                            selected QEMU display backend and its input/window
                            handling, but reads RGB555 directly from guest RAM
                            and expands it through a lookup table into QEMU's
                            preferred ARGB surface, without address-space reads.
  --qemu-framebuffer-async  Additionally move QEMU surface submission to an
                            experimental worker. Uses QEMU's SDL display via
                            accelerated X11 by default. Native Wayland uses an
                            explicit OpenGL-context handoff; software SDL is the
                            compatibility fallback.
  --qemu-framebuffer-async-driver auto|wayland|x11|software
                            Select the SDL presentation path for async display
                            A/B measurements. Default: auto (accelerated X11
                            when available, otherwise software).

AUDIO
  --audio auto|none|pa|alsa|sdl|oss|sndio|dbus|wav|<qemu-driver>
                            DCS audio backend. Default and `auto`:
                            choose the first QEMU-supported backend
                            whose host check passes (sdl, pa, alsa,
                            oss, sndio, dbus). `pa` requires pactl;
                            `alsa` requires /proc/asound/cards;
                            later backends trust QEMU. Falls back to
                            `none` with a warning if nothing matches.
                            Explicit backend names are validated
                            against the qemu binary's compiled-in
                            driver list (see `qemu-system-i386 -audio
                            help`); unsupported names fail fast. Sets
                            P2K_DCS_AUDIO=1 and `-audio driver=<x>`
                            so AUD_register_card binds. NOTE: `auto`
                            is the *wrapper* autodetect, NOT QEMU's
                            `driver=auto`. The DCS code path is
                            unchanged.
  --no-audio                Force DCS audio off (overrides --audio).
  --strict                  Disable HOTLOOP IRQ0 delivery and use the
                            natural i8254+i8259 path.
  --with-pit                Let HOTLOOP and the natural i8254 both
                            supply IRQ0. Default is HOTLOOP-only.
  --legacy-hotloop          Select the retained TB-boundary HOTLOOP instead
                            of host-wall-clock pacing for temporary A/B tests.
  --speed-target <percent>  Deliberate game-clock speed, 25..300 (default
                            100). Scales the i8254 PIT divisor in strict and
                            combo modes and the adaptive HOTLOOP target in
                            HOTLOOP modes. Example: 75 = three-quarter speed,
                            120 = 1.2x speed.
  --pb2kslib <path>         Override pb2kslib container path
                            (P2K_PB2KSLIB=<path>). Default lookup is
                            <roms_dir>/<game>_sound.bin. No directory walks.
  --dcs-sound-flash <path>  Explicit 1 MiB ADSP 28F800/sf.rom image.
                            Useful with update bundles such as SWE1 2.00
                            that contain game code but omit *_sf.rom.
  --dcs-engine pb2kslib|pb2kslib-adsp|adsp|adsp-thread|adsp-clock-thread|adsp-hybrid-thread
                            Audio content engine. adsp-hybrid-thread is the
                            default: original firmware/assets with an event-
                            gated low/high-water DSP producer. adsp-clock-thread
                            retains the fixed-slice producer; adsp-thread the
                            condition-driven mailbox worker; adsp runs the same
                            firmware synchronously. pb2kslib uses extracted
                            samples. pb2kslib-adsp generates/uses a persistent
                            PCM cache rendered by the update's native DSP.
  --dcs-pcm-cpu <cpu>       Pin the ADSP worker used by adsp-thread,
                            adsp-clock-thread or adsp-hybrid-thread to this
                            Linux logical CPU. Experimental; host topology and
                            workload determine whether affinity helps.
  --clear-pb2kslib-cache   Delete the generated ADSP PCM cache before launch.
  --pb2kslib-cache-workers <n>
                            Parallel DSP processes used for first-time PCM
                            generation (1..32, default: 6). Use 1 for the
                            original in-window single-worker generator.
  --sound-loading lazy|preload
                            lazy   (default) decode samples on-demand.
                            preload  walk every pb2k entry at install
                            time and decode now (P2K_DCS_PRELOAD=1).
                            Adds ~1 s startup cost; eliminates first-
                            trigger decode hitch.

  Faithful ADSP timing candidates currently under validation:
    scripts/run-qemu.sh --dcs-engine pb2kslib-adsp
                            Host-clock HOTLOOP with update-derived PCM.
    scripts/run-qemu.sh --dcs-engine adsp-thread
                            Host-clock HOTLOOP with live DSP emulation.
    scripts/run-qemu.sh --with-pit --dcs-engine pb2kslib-adsp
                            HOTLOOP plus natural PIT with update-derived PCM.
    scripts/run-qemu.sh --with-pit --dcs-engine adsp-thread
                            HOTLOOP plus natural PIT with live DSP emulation.
    scripts/run-qemu.sh --dcs-engine adsp-thread --dcs-pcm-cpu 2
                            Host-clock HOTLOOP with the dcs-pcm worker pinned.
    scripts/run-qemu.sh --dcs-engine adsp-clock-thread --dcs-pcm-cpu 3
                            Fixed-slice DSP clock with optional pinning.
    scripts/run-qemu.sh --dcs-engine adsp-hybrid-thread
                            Default event-gated eight-frame producer.

    Plain pb2kslib is excluded because a fixed library can omit sounds added
    by newer updates. Strict real-ADSP modes currently fail the IRQ-jitter gate.

    Refreshed steady-state results (SWE1 2.00, this host):
      Configuration                 Delivery IRQ/s Mean Sigma p50 p95 p99 Worst DATA/s PDB/s P50 P95 P99 Worst    Status
      HOTLOOP pb2kslib-adsp          100.07%  4007  250     7 249 254 277   474  42558  4004 249 254 274  410 us   PASS
      HOTLOOP adsp-thread            100.10%  4008  250     7 249 255 275   362  42562  4004 248 255 275  407 us   PASS
      HOTLOOP+PIT pb2kslib-adsp      100.16%  4010  249    16 248 265 296   681  42559  4004 248 260 285  2.63 ms  FAIL
      HOTLOOP+PIT adsp-thread        100.60%  4028  248    53 242 300 480  1470  42563  4004 248 255 274  537 us   FAIL
      HOTLOOP adsp-thread PCM CPU2   100.11%  4008  249     7 249 255 276   392  42560  4004 248 256 276  830 us   PASS
      Timing columns after IRQ/s and PDB/s are microseconds unless marked ms.

    Qualification gates: IRQ sigma < 10 us and PDB worst <= 2 ms.
    These figures are a comparison example, not portable performance promises;
    rerun the forensic full benchmark when choosing for another host.

NETWORK
  --network                 Add the emulated SMC8416T Ethernet card on an
                            isolated QEMU user network. XINA keeps ownership
                            of IP configuration and network startup.
  --network-nat             Add the card on QEMU's user-mode NAT. XINA can
                            reach the host network and Internet without root,
                            a TAP, Docker, or host firewall changes.
  --network-passt           Add the card through the unprivileged passt daemon.
                            XINA can share the host's address, routes and DNS
                            while passt translates traffic through host sockets.
  --network-mirror          Experimental: mirror the host IPv4 subnet inside
                            QEMU/libslirp while retaining QEMU's rootless NAT.
  --network-auto            Rootless NAT independent of XINA's configured IP,
                            mask and gateway. No guest reconfiguration needed.
  --expose-services         Enable NAT and publish the built-in HTTP service as
                            host TCP 8080 -> guest TCP 80. Uses passt when
                            selected, otherwise NAT. Telnet is excluded; expose
                            it explicitly with --forward 2323:23.
  --forward <host:guest>    With --network-nat or --network-passt, publish a
                            guest TCP port on
                            every host interface. Repeatable. This deliberately
                            exposes the old guest stack to the host network.
  --http-port <port>        Forward 127.0.0.1:<port> to the guest's HTTP
                            server at 10.0.2.15:80. Implies --network. The
                            listener is never exposed beyond localhost.
  --network-bridge <name>   Attach the emulated card to an existing Linux
                            bridge through an Encore-managed TAP. The runner's
                            root phase creates/attaches the TAP; QEMU remains
                            unprivileged. Incompatible with NAT/forwarding.

CONSOLE / DIAGNOSTICS
  --bench                   Run an isolated self-diagnostic using the normal
                            windowed display and audio defaults. Pass 1 installs
                            a temporary RAM-only probe in XINU's live clkint
                            entry, settles after GDB, measures IRQ intervals and
                            restores the six original bytes. Pass 2 starts a new,
                            unpatched guest for LPT/PDB05 measurement. Both run
                            the cabinet-input workload before 10 s guest warmup;
                            a wall-timed XINU `sleep 10` checks absolute speed.
                            Other options such as --game, --update and --strict
                            are forwarded. Pass `--display none --no-audio` for
                            headless testing. Requires gdb, as, ld and objcopy.
                            Returns 2 for unhealthy speed, IRQ delivery or a
                            steady PDB05 gap above 2.5 ms.
  --bench-long              With --bench, restore the 30 s guest warmup used
                            for final validation. The measured window remains
                            the same; only post-workload settling is longer.
  --bench-guest-load        With --bench, create a temporary low-priority XINU
                            worker in guest RAM. It keeps the guest scheduler
                            busy while yielding cooperatively, without touching
                            game state or devices. The worker and patches vanish
                            when each isolated benchmark pass exits.
  --serial                  Bind COM1 to THIS terminal interactively.
                            Spawns QEMU in the background with a
                            127.0.0.1 TCP UART, then runs `nc` in the
                            foreground of your current shell so you
                            get cooked-mode echo, line editing, and
                            full XINA monitor access — same UX as
                            `nc localhost <port>` would, but in one
                            window. Ctrl-C exits (kills the QEMU
                            child). Mutually exclusive with
                            --serial-tcp / --uart-tcp / --headless.
                            Implies --uart-quiet.
  --script <file>           After XINU is ready, execute each non-comment line
                            as a console command. Directives provide waits,
                            keys, matrix switches, repeat blocks, assertions,
                            state polling, screenshots and timed audio capture.
                            Syntax is checked before QEMU starts. The emulator
                            stays open when the file completes. Example:
                            `--script scripts/demos/start-game.p2k`.
  --console-script <file>   Compatibility alias for --script.
  --uart-quiet              Silence ALL COM1/UART output (stderr mirror
                            AND chardev sink, the latter via
                            -serial null). Wins over -v. Also pre-stuffs
                            P2K_UART_INPUT='\r\n\r\n\r\n\r\n' so XINU's
                            synchronous polled-getc loop (op=22) does not
                            wedge swe1 boot when interrupts are disabled.
                            Override by exporting P2K_UART_INPUT before
                            invocation (set to "" to keep RX truly silent).
  --uart-drop <substr>      Drop any UART line containing <substr>
                            before it reaches stdout/tcp/stderr. By
                            default the wrapper drops "swd Debug:"
                            (phantom switch debug spam from XINA).
                            Repeatable. Set the empty string via
                            --uart-no-filter to forward EVERY byte.
  --uart-no-filter          Disable the default "swd Debug:" filter
                            and any --uart-drop entries (sets
                            P2K_UART_DROP="").
  --uart-tcp <host:port>    Bind COM1 to a bidirectional TCP server
                            socket (-serial tcp:<host:port>,server=on,
                            wait=off). `nc <host> <port>` becomes a
                            two-way XINA monitor: the guest's stream
                            comes out, your keystrokes go IN to the
                            serial-tcp mode). Compatible with --headless.
  --serial-tcp <port>       Alias for
                            `--uart-tcp 127.0.0.1:<port>`. Same
                            bidirectional behavior — type XINA
                            commands like `?` or `continue` directly
                            into nc and watch the guest respond.
  --monitor <spec>          QEMU -monitor target (e.g. stdio,
                            unix:/tmp/qmon,server,nowait).
  --debug <opts>            QEMU -d options (e.g. int,cpu_reset,in_asm).
                            Output goes to /tmp/p2k_qemu.log.
  --screenshot-dir <dir>    Where F3 writes screenshots (defaults to
                            /tmp). Exported as P2K_SCREENSHOT_DIR.
  --record-video <file>     Record the complete run. FFmpeg selects the
                            container and its default codec from the filename
                            extension. Native RGB555 frames are piped directly
                            to the encoder; no raw video file is written.
                            Existing output is never overwritten.
  --diag                    Enable the read-only PIT/PIC/IDT/XINU
                            change-only sampler (P2K_DIAG=1).
  --trace-dcs               Per-byte DCS UART trace (P2K_DCS_BYTE_TRACE=1).
  --trace-audio             Per-event DCS audio trace + per-second
                            renderer status (P2K_DCS_AUDIO_TRACE=1).
  --trace-timing            Alias for --diag (no separate timing trace
                            module exists today).
  --timing-snapshots        Emit the lightweight three-second timing subset
                            used by automated benchmarks. Unlike -v/--diag,
                            this does not run the 100 ms diagnostic sampler
                            or sort every detailed timing ring.
  -v                        Verbose level 1: re-enable UART stderr
                            mirror + --diag. Default (no -v) is QUIET
                            — UART output suppressed so the terminal
                            stays clean. Use --uart-tcp to capture the
                            stream interactively without spamming the
                            console.
  -vv                       Level 2: -v + --trace-audio.
  -vvv                      Level 3: -v + --trace-audio + --trace-dcs.
                            (--headless implies at least -v so the
                            session isn't completely silent.)
  --dcs-mode io-handled|bar4-patch
                            Select a DCS frontend label. Both labels use the
                            same shared BAR4 + UART core today.

CABINET
  --lpt-device auto|emulated|required|disconnected|none|/dev/parportN
                            Driver-board source. auto scans /dev/parportN and
                            falls back to keyboard-backed emulation; emulated
                            ignores physical ports; required scans but refuses
                            fallback. disconnected exposes an open bus; none
                            installs no guest LPT device. A path is an
                            authoritative ppdev override.
                            The explicitly selected port
                            remains connected to the guest when its cable is
                            silent, so the ROM performs the diagnosis. A real
                            port disables emulated cabinet keys; host controls
                            F1 quit, F2 flip and F3 screenshot remain available.
                            Default: auto.
  --lpt-ioport 0xNNN       Set the guest LPT address (default: 0x378),
                           independently of emulated or physical backend.
  --lpt-input physical|hybrid
                           With a real board, accept physical switches only
                           (default) or add keyboard switch closures while all
                           outputs and keepalive remain physical.
  --lpt-trace <file>        Append every LPT read/write to <file>
                            (P2K_LPT_TRACE_FILE). Format:
                            "<ts> R|W <off>=<val>" with µs timestamps.
ESCAPE HATCHES
  --tcg-only                Smoke-test the host QEMU binary alone (no
                            Pinball 2000 hardware, no game boot).
  --                        Pass remaining args straight to qemu-system-i386.
  -h, --help                Show this help.

KEY BINDINGS (delivered by the QEMU machine, not by this wrapper)
  F1                        Quit / shutdown request
  F4                        Toggle coin door
  F5 / Enter / KP-Enter     ~60-frame Enter pulse
  F6 / F9                   Left / right action buttons
  F7 / F8                   Left / right flippers
  F10 / C                   Coin slot 1
  Space / S                 Start
  Esc / Left arrow          Service
  Down / KP-                Volume down
  Up / = / KP+              Volume up
  Right arrow               Begin test
  F12                       State dump
  NN, hold Ctrl             Type a matrix switch number 11..88, then hold Ctrl
                            for the desired switch duration. Releasing Ctrl
                            releases it; holding Ctrl again repeats the same
                            switch. Example: 13, hold Ctrl for 3 s holds Start.
  Configured A-Z            Hold mapped letters to hold their matrix switches.
                            See --switch-keymap and docs/41-cli-keyboard-guide.md.
  F2                        Toggle vertical flipscreen
                            (default ON: bottom-up source → top-down
                             display).
  F3                        Screenshot to <screenshot-dir>/p2k_screen_<ts>.jpg
                            (default dir /tmp; override with
                             --screenshot-dir or P2K_SCREENSHOT_DIR.
                             Falls back to .ppm if no jpeg helper —
                             cjpeg / magick / convert — is on PATH;
                             --framebuffer writes .bmp directly through SDL)
  (Fullscreen toggle: use SDL's default Ctrl+Alt+F.)

ENV PASSTHROUGH (advanced; see qemu/README.md for the full table)
  P2K_NO_UART_STDERR
  P2K_FRESH_SAVEDATA P2K_MEM_DETECT_PATCH P2K_DCS_AUDIO P2K_NO_DCS_AUDIO
  P2K_DCS_AUDIO_TRACE P2K_DCS_BYTE_TRACE P2K_DCS_NO_BYTE_PAIR
  P2K_DCS_RAW_55_PAIR P2K_DIAG P2K_TIMING_SNAPSHOTS P2K_NO_AUTO_UPDATE
  P2K_PB2KSLIB P2K_DCS_ENGINE P2K_DCS_PCM_CPU P2K_DCS_MODE P2K_SCREENSHOT_DIR
  P2K_DISPLAY_BPP P2K_FRAMEBUFFER_THREAD P2K_QEMU_FRAMEBUFFER
  P2K_GUEST_EXTENSIONS P2K_GUEST_IP P2K_GUEST_MASK P2K_GUEST_GATEWAY
  P2K_LPT_DEVICE P2K_LPT_INPUT
  P2K_LPT_IOPORT P2K_LPT_TRACE_FILE P2K_DCS_PRELOAD
  P2K_SWITCH_KEYMAP P2K_VIDEO_CAPTURE P2K_FFMPEG_BIN
EOF
}

# --- arg parse --------------------------------------------------------------
LPT_DEVICE="${P2K_LPT_DEVICE:-auto}"
LPT_INPUT="${P2K_LPT_INPUT:-physical}"
LPT_IOPORT_SET=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --game)
      case "${2:-}" in auto|swe1|rfm) GAME="$2" ;; *) echo "[run-qemu] --game: expected auto, swe1, or rfm" >&2; exit 2 ;; esac
      shift 2 ;;
    --roms)            ROMS_DIR="$2"; shift 2 ;;
    --savedata)        SAVEDATA_DIR="$2"; shift 2 ;;
    --no-savedata)     NO_SAVEDATA=1; shift ;;
    --fresh)           FRESH_SAVEDATA=1; shift ;;
    --guest-extensions)
      GUEST_EXTENSIONS=1; shift ;;
    --setip)
      [[ $# -ge 4 ]] || {
        echo "[run-qemu] --setip: expected IP MASK GATEWAY" >&2
        exit 2
      }
      GUEST_EXTENSIONS=1
      GUEST_IP="$2"; GUEST_MASK="$3"; GUEST_GATEWAY="$4"
      shift 4 ;;
    --clear-pb2kslib-cache) CLEAR_PB2K_ADSP_CACHE=1; shift ;;
    --pb2kslib-cache-workers)
      PB2K_ADSP_CACHE_WORKERS="$2"; shift 2 ;;
    --update)          UPDATE_TOKEN="$2"; shift 2 ;;
    --display)
      __qbin="${QEMU_BIN:-$ROOT/qemu-system-i386}"
      [[ -x "$__qbin" ]] || __qbin="$HOME/.cache/p2k-qemu-build/qemu-10.0.8/build/qemu-system-i386"
      [[ -x "$__qbin" ]] || __qbin="qemu-system-i386"
      __dbase="${2%%,*}"
      if ! "$__qbin" -display help 2>/dev/null | grep -qx "$__dbase"; then
        echo "[run-qemu] --display: '$__dbase' not compiled into this qemu binary." >&2
        echo "  Available backends:" >&2
        "$__qbin" -display help 2>/dev/null | tail -n +2 | sed '/^Some/,$d;/^$/d;s/^/    /' >&2
        echo "  (Rebuild with scripts/build-qemu.sh after installing the relevant" >&2
        echo "   dev package, e.g. apt install libgtk-3-dev for gtk.)" >&2
        exit 2
      fi
      DISPLAY_MODE="$2"; shift 2 ;;
    --headless)        HEADLESS=1; shift ;;
    --fullscreen)      FULLSCREEN=1; shift ;;
    --framebuffer)     FRAMEBUFFER_REQUEST=1; shift ;;
    --wayland)
      RUNTIME_BACKEND=wayland
      SDL_VIDEO_DRIVER_REQUEST=wayland
      FRAMEBUFFER_REQUEST=1
      shift ;;
    --display-manager)
      RUNTIME_BACKEND=display-manager
      SDL_VIDEO_DRIVER_REQUEST=wayland
      FRAMEBUFFER_REQUEST=1
      shift ;;
    --cage|--weston)
      RUNTIME_BACKEND="${1#--}"
      BACKEND_WRAPPER="$RUNTIME_BACKEND"
      SDL_VIDEO_DRIVER_REQUEST=wayland
      FRAMEBUFFER_REQUEST=1
      shift ;;
    --preflight)
      PREFLIGHT=1
      shift ;;
    --flipscreen)
      export P2K_FLIPSCREEN=1
      shift ;;
    --switch-keymap)
      [[ -n "${2:-}" ]] || {
        echo "[run-qemu] --switch-keymap: expected a YAML file path" >&2
        exit 2
      }
      export P2K_SWITCH_KEYMAP="$(realpath -m "$2")"
      shift 2 ;;
    --qemu-framebuffer) export P2K_QEMU_FRAMEBUFFER=1; shift ;;
    --qemu-framebuffer-async)
      export P2K_QEMU_FRAMEBUFFER=1 P2K_QEMU_FB_ASYNC=1; shift ;;
    --qemu-framebuffer-async-driver)
      case "${2:-}" in
        auto|wayland|x11|software) QEMU_FB_ASYNC_DRIVER="$2" ;;
        *) echo "[run-qemu] $1: expected auto, wayland, x11, or software" >&2; exit 2 ;;
      esac
      shift 2 ;;
    --bpp)
      case "$2" in
        32) ;;  # default ARGB8888 path
        16) export P2K_DISPLAY_BPP=16 ;;  # native x1r5g5b5 surface
        *)  echo "[run-qemu] --bpp: only 16 and 32 are supported (got '$2')" >&2; exit 2 ;;
      esac
      shift 2 ;;
    --splash-screen|--splash-time|--no-splash)
      # Removed: in-window splash didn't work (QEMU is too fast — guest
      # draws before splash even paints once). Silently accept-and-drop
      # so old command lines don't break.
      case "$1" in
        --no-splash) shift ;;
        *) shift 2 ;;
      esac ;;
    --serial)          SERIAL_STDIO=1; shift ;;
    --network)         NETWORK=1; shift ;;
    --network-nat)     NETWORK=1; NETWORK_NAT=1; shift ;;
    --network-passt)   NETWORK=1; NETWORK_PASST=1; shift ;;
    --network-mirror)  NETWORK=1; NETWORK_NAT=1; NETWORK_MIRROR=1; shift ;;
    --network-auto)    NETWORK=1; NETWORK_NAT=1; NETWORK_AUTO=1; shift ;;
    --expose-services)
      for forward in "${NETWORK_FORWARDS[@]}"; do
        [[ "${forward%%:*}" != 8080 ]] || {
          echo "[run-qemu] --expose-services: host TCP port 8080 is already specified" >&2
          exit 2
        }
      done
      NETWORK_FORWARDS+=("8080:80")
      NETWORK=1
      shift ;;
    --forward)
      [[ "${2:-}" =~ ^([0-9]+):([0-9]+)$ ]] || {
        echo "[run-qemu] --forward: expected HOST_PORT:GUEST_PORT" >&2
        exit 2
      }
      host_port="$((10#${BASH_REMATCH[1]}))"
      guest_port="$((10#${BASH_REMATCH[2]}))"
      (( host_port >= 1 && host_port <= 65535 &&
         guest_port >= 1 && guest_port <= 65535 )) || {
        echo "[run-qemu] --forward: ports must be from 1 to 65535" >&2
        exit 2
      }
      for forward in "${NETWORK_FORWARDS[@]}"; do
        [[ "${forward%%:*}" != "$host_port" ]] || {
          echo "[run-qemu] --forward: host TCP port $host_port is specified twice" >&2
          exit 2
        }
      done
      NETWORK_FORWARDS+=("$host_port:$guest_port")
      NETWORK=1
      shift 2 ;;
    --network-bridge)
      [[ -n "${2:-}" && "$2" =~ ^[A-Za-z0-9_.-]{1,15}$ ]] || {
        echo "[run-qemu] --network-bridge: expected a Linux interface name" >&2
        exit 2
      }
      NETWORK_BRIDGE="$2"
      NETWORK=1
      shift 2 ;;
    --http-port)
      if [[ -z "${2:-}" || ! "$2" =~ ^[0-9]+$ ]] ||
         (( 10#$2 < 1 || 10#$2 > 65535 )); then
        echo "[run-qemu] --http-port: expected a TCP port from 1 to 65535" >&2
        exit 2
      fi
      HTTP_PORT="$((10#$2))"
      NETWORK=1
      shift 2 ;;
    --script|--console-script)
      [[ -f "${2:-}" ]] || { echo "[run-qemu] $1: '${2:-}' is not a file" >&2; exit 2; }
      CONSOLE_SCRIPT="$(realpath "$2")"; shift 2 ;;
    --monitor)         MONITOR="$2"; shift 2 ;;
    --debug)           DEBUG="$2"; shift 2 ;;
    --uart-quiet)      UART_QUIET=1; shift ;;
    --uart-drop)
      # Drop any UART line containing this substring. Repeatable.
      # Internally joined with TAB and exported as P2K_UART_DROP.
      if [[ -z "${UART_DROP:-}" ]]; then UART_DROP="$2"
      else UART_DROP="$UART_DROP"$'\t'"$2"; fi
      shift 2 ;;
    --uart-no-filter)  export P2K_UART_DROP=""; shift ;;
    --uart-tcp)        UART_TCP="$2"; shift 2 ;;
    --serial-tcp)
      # Alias: --serial-tcp <port> ⇒ --uart-tcp 127.0.0.1:<port>
      if [[ -z "${2:-}" || "$2" =~ [^0-9] ]]; then
        echo "[run-qemu] --serial-tcp: expected numeric port, got '${2:-}'" >&2
        exit 2
      fi
      UART_TCP="127.0.0.1:$2"; shift 2 ;;
    --screenshot-dir)
      [[ -d "$2" ]] || { echo "[run-qemu] --screenshot-dir: '$2' is not a directory" >&2; exit 2; }
      export P2K_SCREENSHOT_DIR="$(cd "$2" && pwd)"; shift 2 ;;
    --record-video)
      [[ -n "${2:-}" ]] || {
        echo "[run-qemu] --record-video: expected an output path" >&2
        exit 2
      }
      [[ ! -e "$2" ]] || {
        echo "[run-qemu] --record-video: refusing to overwrite '$2'" >&2
        exit 2
      }
      __video_dir="$(cd "$(dirname "$2")" 2>/dev/null && pwd)" || {
        echo "[run-qemu] --record-video: parent directory of '$2' is missing" >&2
        exit 2
      }
      __ffmpeg="$(command -v ffmpeg || true)"
      [[ -n "$__ffmpeg" ]] || {
        echo "[run-qemu] --record-video: ffmpeg is required (install package 'ffmpeg')" >&2
        exit 2
      }
      export P2K_VIDEO_CAPTURE="$__video_dir/$(basename "$2")"
      export P2K_FFMPEG_BIN="$__ffmpeg"
      shift 2 ;;
    --audio)
      case "$2" in
        auto) AUDIO=""; shift 2 ;;  # fall through to host/QEMU autodetect below
        none) AUDIO=none; unset P2K_DCS_AUDIO || true; shift 2 ;;
        *)
          audio_validate_explicit "$2"
          AUDIO="$2"; export P2K_DCS_AUDIO=1; shift 2 ;;
      esac ;;
    --no-audio)        AUDIO="none"; unset P2K_DCS_AUDIO || true; shift ;;
    --strict)
      # Disable HOTLOOP and use natural i8254 + i8259 delivery.
      export P2K_TCG_CLKINT_HOTLOOP=0; shift ;;
    --with-pit)
      # Combined mode: HOTLOOP and the natural i8254 both raise IRQ0.
      export P2K_TCG_CLKINT_HOTLOOP_WITH_PIT=1
      unset P2K_TCG_CLKINT_HOTLOOP_NO_PIT || true
      shift ;;
    --legacy-hotloop)
      export P2K_HOTLOOP_HOST_TIMER=0
      shift ;;
    --speed-target)
      [[ -n "${2:-}" ]] || { echo "[run-qemu] --speed-target: expected percent" >&2; exit 2; }
      SPEED_TARGET="$2"; shift 2 ;;
    --pb2kslib)        export P2K_PB2KSLIB="$2"; shift 2 ;;
    --dcs-sound-flash)
      [[ -f "$2" ]] || { echo "[run-qemu] --dcs-sound-flash: '$2' is not a file" >&2; exit 2; }
      [[ "$(stat -c %s "$2")" = 1048576 ]] || {
        echo "[run-qemu] --dcs-sound-flash: '$2' must be exactly 1048576 bytes" >&2
        exit 2
      }
      export P2K_DCS_SOUND_FLASH="$(realpath "$2")"; shift 2 ;;
    --dcs-engine)
      case "$2" in
        pb2kslib|pb2kslib-adsp|adsp|adsp-thread|adsp-clock-thread|adsp-hybrid-thread) export P2K_DCS_ENGINE="$2" ;;
        *) echo "[run-qemu] --dcs-engine: expected pb2kslib|pb2kslib-adsp|adsp|adsp-thread|adsp-clock-thread|adsp-hybrid-thread, got '$2'" >&2; exit 2 ;;
      esac
      shift 2 ;;
    --dcs-pcm-cpu)
      [[ -n "${2:-}" && "$2" =~ ^[0-9]+$ ]] || {
        echo "[run-qemu] --dcs-pcm-cpu: expected a non-negative logical CPU" >&2
        exit 2
      }
      export P2K_DCS_PCM_CPU="$2"; shift 2 ;;
    --sound-loading)
      case "$2" in
        lazy)    SOUND_LOADING="lazy" ;;
        preload) SOUND_LOADING="preload"; export P2K_DCS_PRELOAD=1 ;;
        *) echo "[run-qemu] --sound-loading: expected lazy|preload, got '$2'" >&2; exit 2 ;;
      esac
      shift 2 ;;
    --dcs-mode)        export P2K_DCS_MODE="$2"; shift 2 ;;
    --diag)            export P2K_DIAG=1; shift ;;
    --trace-dcs)       export P2K_DCS_BYTE_TRACE=1; shift ;;
    --trace-audio)     export P2K_DCS_AUDIO_TRACE=1; shift ;;
    --trace-timing)    export P2K_DIAG=1; shift ;;
    --timing-snapshots) export P2K_TIMING_SNAPSHOTS=1; shift ;;
    -v)                VERBOSITY=1; shift ;;
    -vv)               VERBOSITY=2; shift ;;
    -vvv)              VERBOSITY=3; shift ;;
    --lpt-device)
      case "${2:-}" in
        auto|emulated|required|disconnected|none) LPT_DEVICE="$2" ;;
        /dev/parport[0-9]*)
          [[ -c "$2" ]] || { echo "[run-qemu] --lpt-device: '$2' is not an existing character device" >&2; exit 2; }
          LPT_DEVICE="$2" ;;
        *) echo "[run-qemu] --lpt-device: expected auto, emulated, required, disconnected, none, or an existing /dev/parportN" >&2; exit 2 ;;
      esac
      shift 2 ;;
    --lpt-ioport)
      [[ "${2:-}" =~ ^(0x[0-9a-fA-F]+|[0-9]+)$ ]] || {
        echo "[run-qemu] --lpt-ioport: expected 0xNNN or a decimal address" >&2; exit 2; }
      export P2K_LPT_IOPORT="$2"; LPT_IOPORT_SET=1; shift 2 ;;
    --lpt-input)
      case "${2:-}" in
        physical|hybrid) LPT_INPUT="$2" ;;
        *) echo "[run-qemu] --lpt-input: expected physical or hybrid" >&2; exit 2 ;;
      esac
      shift 2 ;;
    --lpt-trace)
      [[ -n "${2:-}" ]] || { echo "[run-qemu] --lpt-trace: expected <file>" >&2; exit 2; }
      LPT_TRACE_DIR="$(cd "$(dirname "$2")" 2>/dev/null && pwd)" || { echo "[run-qemu] --lpt-trace: parent dir of '$2' missing" >&2; exit 2; }
      export P2K_LPT_TRACE_FILE="$LPT_TRACE_DIR/$(basename "$2")"
      shift 2 ;;
    --tcg-only)        TCG_ONLY=1; shift ;;
    --)                shift; EXTRA+=("$@"); break ;;
    -h|--help)         print_help; exit 0 ;;
    *) echo "Unknown arg: $1 (try --help)" >&2; exit 2 ;;
  esac
done
if [[ $GUEST_EXTENSIONS -eq 1 ]]; then
  export P2K_GUEST_EXTENSIONS=1
  if [[ -n "$GUEST_IP" ]]; then
    export P2K_GUEST_IP="$GUEST_IP"
    export P2K_GUEST_MASK="$GUEST_MASK"
    export P2K_GUEST_GATEWAY="$GUEST_GATEWAY"
  fi
fi
if [[ -n "$HTTP_PORT" ]]; then
  for forward in "${NETWORK_FORWARDS[@]}"; do
    [[ "${forward%%:*}" != "$HTTP_PORT" ]] || {
      echo "[run-qemu] host TCP port $HTTP_PORT is used by both --http-port and --forward" >&2
      exit 2
    }
  done
fi
if [[ -n "$NETWORK_BRIDGE" ]]; then
  [[ -z "$HTTP_PORT" && ${#NETWORK_FORWARDS[@]} -eq 0 &&
     $NETWORK_NAT -eq 0 && $NETWORK_PASST -eq 0 ]] || {
    echo "[run-qemu] NAT/port-forwarding options cannot be combined with --network-bridge" >&2
    exit 2
  }
  [[ -d "/sys/class/net/$NETWORK_BRIDGE/bridge" ]] || {
    echo "[run-qemu] --network-bridge: '$NETWORK_BRIDGE' is not an existing Linux bridge" >&2
    exit 2
  }
fi
if [[ $NETWORK_PASST -eq 1 && $NETWORK_NAT -eq 1 ]]; then
  echo "[run-qemu] --network-passt and --network-nat are mutually exclusive" >&2
  exit 2
fi
if [[ $NETWORK_AUTO -eq 1 &&
      ( $NETWORK_MIRROR -eq 1 || $NETWORK_PASST -eq 1 || -n "$NETWORK_BRIDGE" ) ]]; then
  echo "[run-qemu] --network-auto cannot be combined with another network transport" >&2
  exit 2
fi
if [[ ${#NETWORK_FORWARDS[@]} -gt 0 && $NETWORK_PASST -eq 0 &&
      -z "$NETWORK_BRIDGE" ]]; then
  NETWORK_NAT=1
fi
export P2K_NETWORK_BRIDGE="$NETWORK_BRIDGE"
export P2K_NETWORK_PASST="$NETWORK_PASST"
export P2K_NETWORK_MIRROR="$NETWORK_MIRROR"
export P2K_NETWORK_AUTO="$NETWORK_AUTO"
if [[ "$LPT_DEVICE" == none && $LPT_IOPORT_SET -eq 1 ]]; then
  echo "[run-qemu] --lpt-ioport has no meaning with --lpt-device none" >&2
  exit 2
fi
if [[ "$LPT_INPUT" == hybrid &&
      ( "$LPT_DEVICE" == emulated || "$LPT_DEVICE" == disconnected ||
        "$LPT_DEVICE" == none ) ]]; then
  echo "[run-qemu] --lpt-input hybrid requires --lpt-device auto, required, or /dev/parportN" >&2
  exit 2
fi
export P2K_LPT_DEVICE="$LPT_DEVICE"
export P2K_LPT_INPUT="$LPT_INPUT"

if [[ ! "$SPEED_TARGET" =~ ^[0-9]+([.][0-9]+)?$ ]] ||
   ! awk -v value="$SPEED_TARGET" 'BEGIN { exit !(value >= 25 && value <= 300) }'; then
  echo "[run-qemu] --speed-target: expected a number from 25 through 300, got '$SPEED_TARGET'" >&2
  exit 2
fi

LOCAL_VT=""
if [[ -t 0 ]]; then
  LOCAL_VT="$(tty 2>/dev/null || true)"
fi
inherited_sdl_driver="${SDL_VIDEODRIVER:-}"
if [[ -z "$SDL_VIDEO_DRIVER_REQUEST" &&
      "${inherited_sdl_driver^^}" == KMSDRM ]]; then
  SDL_VIDEO_DRIVER_REQUEST=KMSDRM
  FRAMEBUFFER_REQUEST=1
fi
if [[ $TCG_ONLY -eq 0 && $HEADLESS -eq 0 && -z "$DISPLAY_MODE" &&
      -z "$SDL_VIDEO_DRIVER_REQUEST" &&
      -z "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]]; then
  if [[ "$LOCAL_VT" == /dev/tty[0-9]* ]]; then
    SDL_VIDEO_DRIVER_REQUEST=KMSDRM
    FRAMEBUFFER_REQUEST=1
    echo "[run-qemu] local VT detected ($LOCAL_VT); using SDL2/KMSDRM"
  else
    echo "[run-qemu] no graphical session or display backend is available." >&2
    echo "[run-qemu] Run it from a local login VT, or use --headless." >&2
    exit 2
  fi
fi

if [[ -z "$RUNTIME_BACKEND" ]]; then
  if [[ "$SDL_VIDEO_DRIVER_REQUEST" == KMSDRM ]]; then
    RUNTIME_BACKEND=direct-console
  elif [[ "$SDL_VIDEO_DRIVER_REQUEST" == wayland || -n "${WAYLAND_DISPLAY:-}" ]]; then
    RUNTIME_BACKEND=wayland
  fi
fi
ENCORE_SDL_DRIVER=""
runtime_root_phase=0

# usermod updates the account database, but Linux cannot mutate the
# supplementary groups of this already-running shell. Re-enter this wrapper
# once through sg so Encore immediately receives the refreshed lp membership;
# the caller does not need to log out. ORIGINAL_ARGS preserves the exact CLI.
refresh_lp_membership() {
  local current_user reexec_cmd quoted arg
  [[ $EUID -ne 0 && "$LPT_DEVICE" != emulated &&
     "$LPT_DEVICE" != disconnected && "$LPT_DEVICE" != none ]] || return 0
  id -nG | tr ' ' '\n' | grep -qx lp && return 0
  current_user="$(id -un)"
  id -nG "$current_user" | tr ' ' '\n' | grep -qx lp || return 0
  command -v sg >/dev/null 2>&1 || return 0

  printf -v reexec_cmd 'exec %q' "$0"
  for arg in "${ORIGINAL_ARGS[@]}"; do
    printf -v quoted '%q' "$arg"
    reexec_cmd+=" $quoted"
  done
  echo "[run-qemu] activating refreshed lp membership for this run"
  exec sg lp -c "$reexec_cmd"
}

refresh_lp_membership
if [[ $EUID -ne 0 ]] && encore_runtime_needs_root_phase "$RUNTIME_BACKEND"; then
  runtime_owner="$(id -un)"
  if command -v run0 >/dev/null 2>&1 && command -v pkttyagent >/dev/null 2>&1; then
    run0 --description="Encore runtime preparation" -- \
      bash "$ROOT/scripts/internal/runtime-packages.sh" \
      --runtime-root-phase "$ROOT" "$RUNTIME_BACKEND" "$runtime_owner" \
      "$HOME" "$QEMU_BIN" "$LPT_DEVICE" "$NETWORK_BRIDGE" "$NETWORK_PASST" "$NETWORK_MIRROR"
  elif command -v sudo >/dev/null 2>&1; then
    sudo bash "$ROOT/scripts/internal/runtime-packages.sh" \
      --runtime-root-phase "$ROOT" "$RUNTIME_BACKEND" "$runtime_owner" \
      "$HOME" "$QEMU_BIN" "$LPT_DEVICE" "$NETWORK_BRIDGE" "$NETWORK_PASST" "$NETWORK_MIRROR"
  elif command -v pkexec >/dev/null 2>&1; then
    pkexec bash "$ROOT/scripts/internal/runtime-packages.sh" \
      --runtime-root-phase "$ROOT" "$RUNTIME_BACKEND" "$runtime_owner" \
      "$HOME" "$QEMU_BIN" "$LPT_DEVICE" "$NETWORK_BRIDGE" "$NETWORK_PASST" "$NETWORK_MIRROR"
  else
    echo "[run-qemu] runtime preparation needs root; no supported privilege helper found" >&2
    exit 2
  fi
  runtime_root_phase=1
  QEMU_BIN=""
  resolve_qemu_bin
fi
refresh_lp_membership
if [[ $runtime_root_phase -eq 0 ]]; then
  encore_prepare_runtime "$RUNTIME_BACKEND"
else
  case "$RUNTIME_BACKEND" in
    direct-console) ENCORE_SDL_DRIVER=KMSDRM ;;
    display-manager|wayland|cage|weston) ENCORE_SDL_DRIVER=wayland ;;
  esac
fi

if [[ $EUID -ne 0 && "$LPT_DEVICE" == /dev/parport[0-9]* &&
      ( ! -r "$LPT_DEVICE" || ! -w "$LPT_DEVICE" ) ]]; then
  echo "[run-qemu] $LPT_DEVICE is not accessible in this login session." >&2
  echo "[run-qemu] The refreshed lp membership did not grant access; check the device permissions." >&2
  exit 2
fi

# Download complete ROM and update trees only when their directories are absent.
# Existing directories are never inspected, modified, or refreshed.
if [[ $EUID -eq 0 && -n "${ENCORE_RUNTIME_USER:-}" &&
      "$ENCORE_RUNTIME_USER" != root ]]; then
  runtime_home="$(getent passwd "$ENCORE_RUNTIME_USER" | cut -d: -f6)"
  [[ -n "$runtime_home" ]] || {
    echo "[run-qemu] unknown runtime asset user: $ENCORE_RUNTIME_USER" >&2
    exit 2
  }
  runuser -u "$ENCORE_RUNTIME_USER" -- env HOME="$runtime_home" \
    "$ROOT/scripts/internal/fetch-assets-if-missing.sh" \
    "$ROOT" "$ROMS_DIR" "$ROOT/updates"
else
  "$ROOT/scripts/internal/fetch-assets-if-missing.sh" "$ROOT" "$ROMS_DIR" "$ROOT/updates"
fi

# A preflight deliberately stops at the last common preparation point
# before cache generation or any other operation can start QEMU.
if [[ $PREFLIGHT -eq 1 ]]; then
  echo "[run-qemu] preflight complete; stopping before runtime launch"
  exit 0
fi

# Standalone compositors wrap the exact same launcher. The child receives the
# compositor's Wayland environment and continues through the ordinary
# --wayland path; only the outer backend selector is removed.
if [[ -n "$BACKEND_WRAPPER" ]]; then
  backend_child_args=()
  for backend_arg in "${ORIGINAL_ARGS[@]}"; do
    [[ "$backend_arg" == "--$BACKEND_WRAPPER" ]] || backend_child_args+=("$backend_arg")
  done
  case "$BACKEND_WRAPPER" in
    cage)
      exec cage -d -s -- "$ROOT/scripts/run-qemu.sh" \
        --wayland "${backend_child_args[@]}"
      ;;
    weston)
      exec weston --backend=drm --shell=kiosk --idle-time=0 --no-config -- \
        "$ROOT/scripts/run-qemu.sh" --wayland "${backend_child_args[@]}"
      ;;
  esac
fi

export P2K_SPEED_TARGET_PERCENT="$SPEED_TARGET"
export P2K_TCG_CLKINT_HOTLOOP_TARGET_HZ="$(
  awk -v percent="$SPEED_TARGET" 'BEGIN { printf "%.6f", 4003.966443 * percent / 100.0 }'
)"

# When neither --strict nor --with-pit is given, use HOTLOOP-only.
# the TCG hook. --with-pit remains available for A/B comparisons.
if [[ -z "${P2K_TCG_CLKINT_HOTLOOP_WITH_PIT:-}" && "${P2K_TCG_CLKINT_HOTLOOP:-1}" != "0" ]]; then
  export P2K_TCG_CLKINT_HOTLOOP_NO_PIT=1
fi

# Scale HOTLOOP's useful search range around the requested period. At 100%
# retain the established defaults exactly. Combined PIT+HOTLOOP keeps the
# broad 100 ms ceiling so a fast natural PIT can make HOTLOOP nearly dormant.
if [[ "$SPEED_TARGET" != "100" && "$SPEED_TARGET" != "100.0" &&
      "${P2K_TCG_CLKINT_HOTLOOP:-1}" != "0" ]]; then
  __target_period_ns="$(awk -v hz="$P2K_TCG_CLKINT_HOTLOOP_TARGET_HZ" \
    'BEGIN { printf "%.0f", 1000000000.0 / hz }')"
  if [[ -z "${P2K_TCG_CLKINT_HOTLOOP_GAP_LOW_NS:-}" ]]; then
    export P2K_TCG_CLKINT_HOTLOOP_GAP_LOW_NS="$(( __target_period_ns / 5 ))"
  fi
  if [[ -z "${P2K_TCG_CLKINT_HOTLOOP_GAP_HIGH_NS:-}" ]]; then
    if [[ -n "${P2K_TCG_CLKINT_HOTLOOP_WITH_PIT:-}" ]]; then
      export P2K_TCG_CLKINT_HOTLOOP_GAP_HIGH_NS=100000000
    else
      export P2K_TCG_CLKINT_HOTLOOP_GAP_HIGH_NS="$(( __target_period_ns * 4 ))"
    fi
  fi
fi

# --- HOTLOOP initial gap ---------------------------------------------------
# One 145 µs starting gap passed the sequential timing matrix, including
# repeated RFM headless + --with-pit boots. The adaptive controller then owns
# steady-state pacing. Explicit P2K_TCG_CLKINT_HOTLOOP_MIN_GAP_NS wins.
if [[ -z "${P2K_TCG_CLKINT_HOTLOOP_MIN_GAP_NS:-}" && "${P2K_TCG_CLKINT_HOTLOOP:-1}" != "0" ]]; then
  export P2K_TCG_CLKINT_HOTLOOP_MIN_GAP_NS="$(
    awk -v percent="$SPEED_TARGET" 'BEGIN { printf "%.0f", 14500000.0 / percent }'
  )"
fi

# --- verbosity → diag/trace tier mapping -----------------------------------
# Default (level 0) is QUIET: the hand-rolled UART in p2k-isa-stubs.c
# would otherwise spam every XINU NonFatal byte to stderr. Use --uart-tcp
# to capture the stream cleanly; or use -v to put it back on stderr.
#   level 0  (default)  silent (P2K_NO_UART_STDERR=1)
#   level 1  -v         UART stderr mirror + P2K_DIAG (lifecycle messages)
#   level 2  -vv        + P2K_DCS_AUDIO_TRACE
#   level 3  -vvv       + P2K_DCS_BYTE_TRACE
# --headless promotes verbosity to at least 1 (otherwise the headless
# session would be completely silent — defeats the purpose).
# --serial implies --uart-quiet (otherwise every byte appears twice:
# once on stdout via the chardev, once on stderr via the mirror).
# --uart-quiet always wins (forces UART silent regardless of level).
if [[ $SERIAL_STDIO -eq 1 ]]; then
  UART_QUIET=1
fi
if [[ -n "$CONSOLE_SCRIPT" ]]; then
  if [[ $SERIAL_STDIO -eq 1 || -n "$UART_TCP" || -n "$MONITOR" || $HEADLESS -eq 1 ]]; then
    echo "[run-qemu] --script manages UART and monitor itself; do not combine it with --serial, --uart-tcp, --monitor or --headless" >&2
    exit 2
  fi
  AUTOMATION_DIR="$(mktemp -d /tmp/p2k-console-script.XXXXXX)"
  python3 "$ROOT/scripts/internal/run-console-script.py" "$CONSOLE_SCRIPT" --check
  SERIAL_PORT="$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1]); s.close()')"
  UART_TCP="127.0.0.1:$SERIAL_PORT"
  MONITOR="unix:$AUTOMATION_DIR/monitor.sock,server=on,wait=off"
  UART_QUIET=1
  if grep -Eq '^[[:space:]]*@record-audio([[:space:]]|$)' "$CONSOLE_SCRIPT"; then
    export P2K_DCS_AUDIO_CAPTURE="$AUTOMATION_DIR/audio.raw"
  fi
fi
if [[ $HEADLESS -eq 1 && $VERBOSITY -lt 1 ]]; then
  VERBOSITY=1
fi
if [[ $VERBOSITY -ge 1 ]]; then
  export P2K_DIAG=1
  unset P2K_NO_UART_STDERR
else
  export P2K_NO_UART_STDERR=1
fi
if [[ $UART_QUIET -eq 1 ]]; then
  export P2K_NO_UART_STDERR=1
  # SWE1 (and possibly other titles) reach an XINU control() op=22 callback
  # (killsafe_waits_msg → ttysputc) early in boot which performs a SYNCHRONOUS
  # polled read on COM1 LSR.DR with interrupts disabled (cli). With -serial null
  # no byte ever arrives and the guest wedges forever — IMR=0xff, IRQ0 cannot
  # deliver, replay BH cannot fire, clock is dead. Pre-stuff a few CRs into
  # the RX FIFO so the polling loop completes and the boot can proceed. Users
  # who want a strictly silent UART can explicitly set P2K_UART_INPUT= to "".
  if [[ -z "${P2K_UART_INPUT+x}" ]]; then
    # Bounded one-shot pattern: enough CR/LF pairs to satisfy ALL op=22
    # reads during boot, then deliberately empty so the diag handler
    # exits its loop. 16 pairs was empirically insufficient (~10% wedge
    # rate): depending on boot timing, XINU exhausts the buffer before
    # the critical killsafe_waits_msg poll, leaving it spinning on COM1
    # LSR.DR with interrupts disabled (IMR=0xff, clkint frozen).
    # 48 pairs eliminates the wedge across 10/10 test runs.
    # Do NOT cycle forever — every extra CR submits an empty command at
    # the diag prompt, eliciting more output and more cli-guarded reads.
    export P2K_UART_INPUT='\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n'
  fi
fi
if [[ -n "${UART_DROP:-}" ]]; then export P2K_UART_DROP="$UART_DROP"; fi
if [[ $VERBOSITY -ge 2 ]]; then export P2K_DCS_AUDIO_TRACE=1; fi
if [[ $VERBOSITY -ge 3 ]]; then export P2K_DCS_BYTE_TRACE=1; fi

# Quiet default: filter info/warning lines from QEMU's stderr (the
# `pinball2000: loaded ...`, `mapped ... @ 0x...`, etc. noise from
# info_report / warn_report inside the machine setup). Errors and
# our own [run-qemu] lines pass through. Lightweight timing snapshots must
# retain QEMU info lines for their machine-readable report without enabling
# the heavier -v diagnostic path.
if [[ $VERBOSITY -lt 1 && "${P2K_TIMING_SNAPSHOTS:-0}" != "1" ]]; then
  exec 2> >(grep -v --line-buffered -E \
    '^qemu-system-i386: (info|warning):' >&2)
fi

# --- display defaults -------------------------------------------------------
# Cabinet-facing SDL selection is explicit only when requested. Normal desktop
# launches retain SDL's discovery. These options target the direct SDL2
# framebuffer renderer; they never introduce an X11 or XWayland fallback.
case "$SDL_VIDEO_DRIVER_REQUEST" in
  wayland)
    [[ -n "${WAYLAND_DISPLAY:-}" && -n "${XDG_RUNTIME_DIR:-}" ]] || {
      echo "[run-qemu] --wayland requires WAYLAND_DISPLAY and XDG_RUNTIME_DIR" >&2
      exit 2
    }
    export SDL_VIDEODRIVER=wayland
    ;;
  KMSDRM)
    unset DISPLAY XAUTHORITY WAYLAND_DISPLAY
    export SDL_VIDEODRIVER=KMSDRM
    ;;
esac

# Direct framebuffer presentation is the desktop default. Explicit headless,
# QEMU display, or QEMU-framebuffer selections retain their requested path.
if [[ "$FRAMEBUFFER_REQUEST" == "auto" ]]; then
  if [[ $HEADLESS -eq 1 || -n "$DISPLAY_MODE" ||
        "${P2K_QEMU_FRAMEBUFFER:-}" == "1" ||
        -z "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]]; then
    FRAMEBUFFER_REQUEST=0
  else
    FRAMEBUFFER_REQUEST=1
  fi
fi
if [[ "$FRAMEBUFFER_REQUEST" == "1" ]]; then
  export P2K_FRAMEBUFFER_THREAD=1
else
  unset P2K_FRAMEBUFFER_THREAD
fi

if [[ "${P2K_FRAMEBUFFER_THREAD:-}" == "1" &&
      "${P2K_QEMU_FRAMEBUFFER:-}" == "1" ]]; then
  echo "[run-qemu] --framebuffer and --qemu-framebuffer are mutually exclusive" >&2
  exit 2
fi
if [[ "${P2K_FRAMEBUFFER_THREAD:-}" == "1" ]]; then
  if [[ $HEADLESS -eq 1 ]]; then
    echo "[run-qemu] --framebuffer and --headless are mutually exclusive" >&2
    exit 2
  fi
  [[ $FULLSCREEN -eq 1 ]] && export P2K_FRAMEBUFFER_FULLSCREEN=1
  DISPLAY_MODE=none
  FULLSCREEN=0
fi
if [[ "${P2K_QEMU_FRAMEBUFFER:-}" == "1" && $HEADLESS -eq 1 ]]; then
  echo "[run-qemu] --qemu-framebuffer and --headless are mutually exclusive" >&2
  exit 2
fi
if [[ -z "$DISPLAY_MODE" ]]; then
  if [[ $HEADLESS -eq 1 ]]; then
    DISPLAY_MODE=none
  elif [[ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]]; then
    DISPLAY_MODE=sdl
  else
    DISPLAY_MODE=none
  fi
fi
if [[ "${P2K_QEMU_FB_ASYNC:-}" == "1" ]]; then
  if [[ "${DISPLAY_MODE%%,*}" != "sdl" ]]; then
    echo "[run-qemu] --qemu-framebuffer-async requires QEMU's SDL display backend" >&2
    exit 2
  fi
  case "$QEMU_FB_ASYNC_DRIVER" in
    auto)
      if [[ -n "${DISPLAY:-}" ]]; then
        export SDL_VIDEODRIVER=x11
        unset SDL_RENDER_DRIVER
        echo "[run-qemu] async framebuffer: accelerated SDL/X11 presentation"
      else
        unset SDL_VIDEODRIVER
        export SDL_RENDER_DRIVER=software
        echo "[run-qemu] async framebuffer: software SDL presentation (no X11 DISPLAY)"
      fi ;;
    wayland)
      [[ -n "${WAYLAND_DISPLAY:-}" ]] || {
        echo "[run-qemu] async framebuffer: no WAYLAND_DISPLAY" >&2; exit 2; }
      export SDL_VIDEODRIVER=wayland
      unset SDL_RENDER_DRIVER
      echo "[run-qemu] async framebuffer: accelerated SDL/Wayland presentation" ;;
    x11)
      [[ -n "${DISPLAY:-}" ]] || {
        echo "[run-qemu] async framebuffer: no X11 DISPLAY" >&2; exit 2; }
      export SDL_VIDEODRIVER=x11
      unset SDL_RENDER_DRIVER
      echo "[run-qemu] async framebuffer: accelerated SDL/X11 presentation" ;;
    software)
      unset SDL_VIDEODRIVER
      export SDL_RENDER_DRIVER=software
      echo "[run-qemu] async framebuffer: software SDL presentation" ;;
  esac
fi
if [[ "$DISPLAY_MODE" == "none" && $FULLSCREEN -eq 1 ]]; then
  echo "[run-qemu] --fullscreen ignored with --display none / --headless" >&2
  FULLSCREEN=0
fi

# --- audio auto-detect ------------------------------------------------------
# Default/auto chooses a backend only when both the QEMU build supports it and
# the host-side service check is acceptable for that backend.
if [[ -z "$AUDIO" ]]; then
  audio_pick_auto
fi
if [[ -n "${P2K_DCS_AUDIO_CAPTURE:-}" && "$AUDIO" == "none" ]]; then
  echo "[run-qemu] --script uses @record-audio but no audio backend is available" >&2
  exit 2
fi

# --- update token resolution ------------------------------------------------
# UPDATE_TOKEN ∈ {auto, latest, none, <short-code>, <dir>}
# Output: UPDATE_DIR_ABS (empty → no -M update=...; or P2K_NO_AUTO_UPDATE=1
# in base-ROM mode).
UPDATE_DIR_ABS=""

resolve_update_token() {
  local token="$1" gn=""
  case "$GAME" in
    swe1) gn=50069 ;;
    rfm)  gn=50070 ;;
    auto) gn=50069 ;; # emulated-board fallback; numeric lookup tries RFM second
    *)    echo "" ; return 1 ;;
  esac

  # Search roots: ./updates and <roms_dir>/../updates.
  local roots=()
  [[ -d "$ROOT/updates" ]] && roots+=( "$ROOT/updates" )
  local roms_parent
  roms_parent="$(cd "$ROMS_DIR" && cd .. && pwd)" 2>/dev/null || true
  [[ -n "$roms_parent" && -d "$roms_parent/updates" && "$roms_parent/updates" != "$ROOT/updates" ]] \
    && roots+=( "$roms_parent/updates" )

  if [[ "$token" == "latest" ]]; then
    local best="" best_v=""
    for r in "${roots[@]}"; do
      while IFS= read -r -d '' d; do
        local base v
        base="$(basename "$d")"
        v="${base#pin2000_${gn}_}"; v="${v%%_*}"
        [[ "$v" =~ ^[0-9]{4}$ ]] || continue
        if [[ -z "$best_v" || "$v" > "$best_v" ]]; then
          best_v="$v"; best="$d/$gn"
        fi
      done < <(find "$r" -mindepth 1 -maxdepth 1 -type d -name "pin2000_${gn}_*" -print0 2>/dev/null)
    done
    [[ -n "$best" && -d "$best" ]] && { echo "$best"; return 0; }
    return 1
  fi

  # Short version code: "0210", "210", "2.10", "2.1"
  local want=""
  if [[ "$token" =~ ^[0-9]+$ ]]; then
    printf -v want '%04d' "$((10#$token))"
  elif [[ "$token" =~ ^([0-9]+)\.([0-9]+)$ ]]; then
    local maj="${BASH_REMATCH[1]}" min="${BASH_REMATCH[2]}"
    [[ ${#min} -eq 1 ]] && min=$((10#$min * 10))
    printf -v want '%04d' "$((10#$maj * 100 + 10#$min))"
  fi

  if [[ -n "$want" ]]; then
    local candidate_gns=("$gn")
    [[ "$GAME" != auto ]] || candidate_gns+=(50070)
    local candidate_gn
    for candidate_gn in "${candidate_gns[@]}"; do
      for r in "${roots[@]}"; do
        while IFS= read -r -d '' d; do
          local inner="$d/$candidate_gn"
          [[ -d "$inner" ]] && { echo "$inner"; return 0; }
        done < <(find "$r" -mindepth 1 -maxdepth 1 -type d -name "pin2000_${candidate_gn}_${want}_*" -print0 2>/dev/null)
      done
    done
    return 1
  fi

  return 1
}

case "$UPDATE_TOKEN" in
  none)
    # Museum / base mode. The C code's auto-discover is suppressed; the
    # Base-ROM compatibility helpers arm only under
    # this env (see qemu/p2k-probe-cell-shim.c, qemu/p2k-dcs-core.c).
    export P2K_NO_AUTO_UPDATE=1
    echo "[run-qemu] --update none → base-ROM mode (P2K_NO_AUTO_UPDATE=1)" >&2
    ;;
  auto|"")
    # Default. Leave -M update= unset; the machine auto-discovers in
    # ./updates and falls back to base ROMs if nothing matches.
    ;;
  latest|[0-9]*|*.*)
    if [[ "$UPDATE_TOKEN" == "latest" || "$UPDATE_TOKEN" =~ ^[0-9.]+$ ]]; then
      if ! UPDATE_DIR_ABS="$(resolve_update_token "$UPDATE_TOKEN")"; then
        echo "[run-qemu] ERROR: --update '$UPDATE_TOKEN' did not resolve to a bundle dir" >&2
        echo "[run-qemu] hint: list ./updates/pin2000_*_<vvvv>_*/<gid>/" >&2
        exit 1
      fi
      echo "[run-qemu] --update $UPDATE_TOKEN → $UPDATE_DIR_ABS" >&2
    elif [[ -d "$UPDATE_TOKEN" ]]; then
      UPDATE_DIR_ABS="$(cd "$UPDATE_TOKEN" && pwd)"
      echo "[run-qemu] --update $UPDATE_TOKEN → $UPDATE_DIR_ABS" >&2
    else
      echo "[run-qemu] ERROR: --update '$UPDATE_TOKEN' is neither a known spec nor a directory" >&2
      exit 1
    fi
    ;;
  *)
    if [[ -d "$UPDATE_TOKEN" ]]; then
      UPDATE_DIR_ABS="$(cd "$UPDATE_TOKEN" && pwd)"
      echo "[run-qemu] --update $UPDATE_TOKEN → $UPDATE_DIR_ABS" >&2
    else
      echo "[run-qemu] ERROR: --update '$UPDATE_TOKEN' is neither a known spec nor a directory" >&2
      exit 1
    fi
    ;;
esac

if [[ -n "$UPDATE_DIR_ABS" && "$GAME" == auto ]]; then
  case "$UPDATE_DIR_ABS" in
    */50069) GAME=swe1 ;;
    */50070) GAME=rfm ;;
  esac
  [[ "$GAME" == auto ]] || echo "[run-qemu] explicit update selected game: $GAME" >&2
fi

if [[ $CLEAR_PB2K_ADSP_CACHE -eq 1 ]]; then
  rm -rf -- "$PB2K_ADSP_CACHE_DIR"
  echo "[run-qemu] cleared generated pb2kslib cache: $PB2K_ADSP_CACHE_DIR"
fi
mkdir -p "$PB2K_ADSP_CACHE_DIR"

if [[ ! "$PB2K_ADSP_CACHE_WORKERS" =~ ^[0-9]+$ ]] ||
   (( PB2K_ADSP_CACHE_WORKERS < 1 || PB2K_ADSP_CACHE_WORKERS > 32 )); then
  echo "[run-qemu] --pb2kslib-cache-workers: expected 1..32" >&2
  exit 2
fi

# Build a missing generated cache before the real game starts. Each worker is
# a separate QEMU process with an independent DSP, so mutable firmware/SRAM
# state is never shared across tracks. A single worker retains the interactive
# in-window generator for diagnostics.
if [[ "${P2K_DCS_ENGINE:-adsp-hybrid-thread}" == "pb2kslib-adsp" &&
      "$PB2K_ADSP_CACHE_WORKERS" -gt 1 &&
      -z "${P2K_PB2K_ADSP_WORKER:-}" ]]; then
  __cache_update="$UPDATE_DIR_ABS"
  if [[ -z "$__cache_update" && "$UPDATE_TOKEN" != "none" ]]; then
    __cache_update="$(resolve_update_token latest || true)"
  fi
  if [[ -n "$__cache_update" ]]; then
    __key_args=(--game "$GAME" --roms "$ROMS_DIR" --update "$__cache_update")
  else
    __key_args=(--game "$GAME" --roms "$ROMS_DIR")
  fi
  __cache_key="$("$ROOT/scripts/internal/pb2k-sound-key.py" "${__key_args[@]}")"
  __cache_file="$PB2K_ADSP_CACHE_DIR/$GAME/${__cache_key}.pcm.pb2k"
  if [[ ! -f "$__cache_file" ]]; then
    __cache_args=(
      "$ROOT/scripts/internal/build-pcm-cache.py"
      --qemu "$QEMU_BIN"
      --game "$GAME"
      --roms "$ROMS_DIR"
      --cache-root "$PB2K_ADSP_CACHE_DIR"
      --workers "$PB2K_ADSP_CACHE_WORKERS"
    )
    [[ -n "$__cache_update" ]] && __cache_args+=(--update "$__cache_update")
    python3 "${__cache_args[@]}"
  fi
fi

# --- savedata handling ------------------------------------------------------
# The selected path is passed directly to the Pinball 2000 machine. In
# --no-savedata mode an empty throwaway cwd remains a second guard against any
# accidental legacy cwd-relative access.
if [[ -n "${P2K_NO_SAVEDATA:-}" && "${P2K_NO_SAVEDATA:-}" != "0" ]]; then
  NO_SAVEDATA=1
fi
if [[ -n "${P2K_FRESH_SAVEDATA:-}" && "${P2K_FRESH_SAVEDATA:-}" != "0" ]]; then
  FRESH_SAVEDATA=1
fi
if [[ $NO_SAVEDATA -eq 1 && $FRESH_SAVEDATA -eq 1 ]]; then
  echo "[run-qemu] ERROR: --fresh and --no-savedata are mutually exclusive" >&2
  exit 2
fi
RUN_CWD="$ROOT"
CLEANUP=""
if [[ $NO_SAVEDATA -eq 1 ]]; then
  export P2K_NO_SAVEDATA=1
  RUN_CWD="$(mktemp -d "$ROOT/.run-qemu.XXXXXX")"
  CLEANUP="$RUN_CWD"
  trap '[[ -n "$CLEANUP" ]] && rm -rf "$CLEANUP"' EXIT
  echo "[run-qemu] read-only savedata: P2K_NO_SAVEDATA=1; running in $RUN_CWD"
else
  if [[ $FRESH_SAVEDATA -eq 1 ]]; then
    export P2K_FRESH_SAVEDATA=1
    echo "[run-qemu] fresh savedata: ignoring existing seeds; new state will be saved on exit"
  fi
fi
SAVEDATA_DIR="$(realpath -m "$SAVEDATA_DIR")"

# --- assemble QEMU command line --------------------------------------------
# Auto-append display options so QEMU never captures the host pointer
# (we have no guest mouse device anyway, but a stray click would
# otherwise hide the cursor and toggle grab until Ctrl-Alt-G).
# Keyboard events still flow normally to the guest.
#   sdl: supports show-cursor only (no grab-on-hover keyword in -display sdl)
#   gtk: supports both show-cursor and grab-on-hover
DISPLAY_ARG="$DISPLAY_MODE"
case "${DISPLAY_MODE%%,*}" in
  sdl)
    [[ "$DISPLAY_MODE" == *show-cursor=*    ]] || DISPLAY_ARG="$DISPLAY_ARG,show-cursor=on"
    ;;
  gtk)
    [[ "$DISPLAY_MODE" == *show-cursor=*    ]] || DISPLAY_ARG="$DISPLAY_ARG,show-cursor=on"
    [[ "$DISPLAY_MODE" == *grab-on-hover=*  ]] || DISPLAY_ARG="$DISPLAY_ARG,grab-on-hover=off"
    ;;
esac
ARGS=( -no-reboot -m 16 -display "$DISPLAY_ARG" -rtc base=localtime )
[[ $FULLSCREEN -eq 1 ]] && ARGS+=( -full-screen )

if [[ $SERIAL_STDIO -eq 1 ]]; then
  if [[ -n "$UART_TCP" || $HEADLESS -eq 1 ]]; then
    echo "[run-qemu] --serial is mutually exclusive with --serial-tcp/--uart-tcp/--headless" >&2
    exit 2
  fi
  # We want UNIX `nc`-quality UX (cooked-mode terminal echo, line
  # editing, no swallowed local-echo) without forcing the user to
  # open a second terminal. Solution: bind COM1 to a TCP server on
  # 127.0.0.1:<random free port>, launch QEMU in the background,
  # then exec `nc 127.0.0.1 <port>` in the foreground of THIS
  # terminal. This keeps the console in the invoking shell.
  if ! command -v nc >/dev/null 2>&1; then
    echo "[run-qemu] --serial needs 'nc' (apt install netcat-openbsd)" >&2
    exit 2
  fi
  # Find a free ephemeral port.
  SERIAL_PORT="$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1]); s.close()' 2>/dev/null || echo 5570)"
  ARGS+=( -serial "tcp:127.0.0.1:${SERIAL_PORT},server=on,wait=off" )
  UART_QUIET=1
elif [[ -n "$UART_TCP" ]]; then
  ARGS+=( -serial "tcp:${UART_TCP},server=on,wait=off" )
  if [[ $HEADLESS -eq 1 ]]; then
    echo "[run-qemu] --uart-tcp + --headless: serial0 -> tcp://${UART_TCP}; "\
"host-stderr UART mirror still active (use --uart-quiet to silence)." >&2
  fi
elif [[ $HEADLESS -eq 1 ]]; then
  if [[ $UART_QUIET -eq 1 ]]; then
    ARGS+=( -serial null )
  else
    ARGS+=( -serial stdio )
  fi
fi

[[ -n "$MONITOR" ]] && ARGS+=( -monitor "$MONITOR" )
[[ -n "$DEBUG" ]] && ARGS+=( -d "$DEBUG" -D /tmp/p2k_qemu.log )

if [[ -n "$AUDIO" && "$AUDIO" != "none" ]]; then
  ARGS+=( -audio "driver=$AUDIO" )
fi

if [[ $NETWORK -eq 1 ]]; then
  if [[ -n "$NETWORK_BRIDGE" ]]; then
    NETWORK_SPEC="tap,id=p2knet,ifname=$ENCORE_NETWORK_TAP,script=no,downscript=no"
    echo "[run-qemu] network: SMC8416T via $ENCORE_NETWORK_TAP -> $NETWORK_BRIDGE"
    echo "[run-qemu] network: WARNING: XINA is directly reachable from that network"
  elif [[ $NETWORK_PASST -eq 1 ]]; then
    # passt enters its own mount/user sandbox before binding. A private /tmp
    # directory remains reachable there; some /run/user mounts do not.
    PASST_DIR="$(mktemp -d "${TMPDIR:-/tmp}/encore-passt.XXXXXX")"
    PASST_SOCKET="$PASST_DIR/network.sock"
    PASST_PID_FILE="$PASST_DIR/passt.pid"
    trap '[[ -n "$CLEANUP" ]] && rm -rf "$CLEANUP"; [[ -n "$PASST_DIR" ]] && rm -rf "$PASST_DIR"' EXIT INT TERM
    PASST_TCP_SPEC=""
    PASST_ARGS=(--socket "$PASST_SOCKET" --pid "$PASST_PID_FILE"
      --runas "$(id -u):$(id -g)" --one-off --ipv4-only)
    for forward in "${NETWORK_FORWARDS[@]}"; do
      host_port="${forward%%:*}"
      guest_port="${forward##*:}"
      [[ -z "$PASST_TCP_SPEC" ]] || PASST_TCP_SPEC+=","
      PASST_TCP_SPEC+="0.0.0.0/${host_port}:${guest_port}"
      echo "[run-qemu] network: WARNING: host TCP $host_port exposes XINA TCP $guest_port"
    done
    if [[ -n "$HTTP_PORT" ]]; then
      [[ -z "$PASST_TCP_SPEC" ]] || PASST_TCP_SPEC+=","
      PASST_TCP_SPEC+="127.0.0.1/${HTTP_PORT}:80"
      echo "[run-qemu] network: http://127.0.0.1:${HTTP_PORT}/ -> XINA TCP 80"
    fi
    PASST_ARGS+=(--tcp-ports "${PASST_TCP_SPEC:-none}")
    passt "${PASST_ARGS[@]}"
    for _ in {1..50}; do
      [[ -S "$PASST_SOCKET" ]] && break
      sleep 0.02
    done
    [[ -S "$PASST_SOCKET" ]] || {
      echo "[run-qemu] passt failed to create its QEMU socket" >&2
      rm -rf "$PASST_DIR"
      PASST_DIR=""
      exit 2
    }
    PASST_PID="$(cat "$PASST_PID_FILE")"
    NETWORK_SPEC="stream,id=p2knet,server=off,addr.type=unix,addr.path=$PASST_SOCKET"
    echo "[run-qemu] network: unprivileged passt using the host's IPv4 topology"
  else
    if [[ $NETWORK_NAT -eq 1 ]]; then
      if [[ $NETWORK_MIRROR -eq 1 ]]; then
        MIRROR_ROUTE="$(ip -4 route show default | head -n1)"
        MIRROR_GATEWAY="$(awk '{for (i=1;i<=NF;i++) if ($i=="via") print $(i+1)}' <<<"$MIRROR_ROUTE")"
        MIRROR_IFACE="$(awk '{for (i=1;i<=NF;i++) if ($i=="dev") print $(i+1)}' <<<"$MIRROR_ROUTE")"
        MIRROR_CIDR="$(ip -o -4 addr show dev "$MIRROR_IFACE" scope global | awk 'NR==1 {print $4}')"
        [[ -n "$MIRROR_GATEWAY" && -n "$MIRROR_CIDR" ]] || {
          echo "[run-qemu] --network-mirror: no usable IPv4 default route" >&2
          exit 2
        }
        read -r MIRROR_NETWORK MIRROR_ADDRESS < <(python3 - "$MIRROR_CIDR" <<'PY'
import ipaddress, sys
i = ipaddress.ip_interface(sys.argv[1])
print(i.network, i.ip)
PY
)
        NETWORK_SPEC="user,id=p2knet,net=$MIRROR_NETWORK,host=$MIRROR_GATEWAY"
        GUEST_NETWORK_ADDR="$MIRROR_ADDRESS"
        echo "[run-qemu] network: mirrored Slirp $MIRROR_NETWORK via $MIRROR_GATEWAY on $MIRROR_IFACE"
      else
        if [[ $NETWORK_AUTO -eq 1 ]]; then
          NETWORK_SPEC="user,id=p2knet"
        else
          NETWORK_SPEC="user,id=p2knet"
        fi
        GUEST_NETWORK_ADDR="10.0.2.15"
        if [[ $NETWORK_AUTO -eq 1 ]]; then
          echo "[run-qemu] network: configuration-independent user-mode NAT"
        else
          echo "[run-qemu] network: user-mode NAT at 10.0.2.0/24"
        fi
      fi
    else
      NETWORK_SPEC="user,id=p2knet,restrict=on"
      GUEST_NETWORK_ADDR="10.0.2.15"
      echo "[run-qemu] network: isolated SMC8416T at 10.0.2.0/24"
    fi
    if [[ -n "$HTTP_PORT" ]]; then
      if [[ $NETWORK_AUTO -eq 1 ]]; then
        AUTO_HOSTFWD_SPEC="127.0.0.1:${HTTP_PORT}:80"
      else
        NETWORK_SPEC+=",hostfwd=tcp:127.0.0.1:${HTTP_PORT}-${GUEST_NETWORK_ADDR}:80"
      fi
    fi
    for forward in "${NETWORK_FORWARDS[@]}"; do
      host_port="${forward%%:*}"
      guest_port="${forward##*:}"
      if [[ $NETWORK_AUTO -eq 1 ]]; then
        [[ -z "$AUTO_HOSTFWD_SPEC" ]] || AUTO_HOSTFWD_SPEC+="|"
        AUTO_HOSTFWD_SPEC+="0.0.0.0:${host_port}:${guest_port}"
      else
        NETWORK_SPEC+=",hostfwd=tcp:0.0.0.0:${host_port}-${GUEST_NETWORK_ADDR}:${guest_port}"
      fi
      echo "[run-qemu] network: WARNING: host TCP $host_port exposes XINA TCP $guest_port"
    done
    if [[ -n "$HTTP_PORT" ]]; then
      echo "[run-qemu] network: http://127.0.0.1:${HTTP_PORT}/ → ${GUEST_NETWORK_ADDR}:80"
    fi
  fi
  if [[ $NETWORK_AUTO -eq 1 ]]; then
    AUTO_DEVICE="p2k-smc8416,netdev=p2knet,proxy-arp=on,proxy-arp-all=on"
    [[ -z "$AUTO_HOSTFWD_SPEC" ]] || AUTO_DEVICE+=",auto-hostfwd=$AUTO_HOSTFWD_SPEC"
    ARGS+=( -netdev "$NETWORK_SPEC" -device "$AUTO_DEVICE" )
  elif [[ $NETWORK_MIRROR -eq 1 ]]; then
    ARGS+=( -netdev "$NETWORK_SPEC" -device p2k-smc8416,netdev=p2knet,proxy-arp=on )
  else
    ARGS+=( -netdev "$NETWORK_SPEC" -device p2k-smc8416,netdev=p2knet )
  fi
fi

# --- TCG smoke-test escape hatch -------------------------------------------
if [[ $TCG_ONLY -eq 1 ]]; then
  ARGS=( -M isapc "${ARGS[@]}" -bios "$ROMS_DIR/bios.bin" )
  echo "[run-qemu] TCG smoke-test (NOT a Pinball 2000 boot)"
  cd "$RUN_CWD"
  exec "$QEMU_BIN" "${ARGS[@]}" "${EXTRA[@]}"
fi

# --- pinball2000 machine ----------------------------------------------------
MACHINE_OPTS="pinball2000,game=$GAME,roms-dir=$ROMS_DIR,savedata-dir=$SAVEDATA_DIR"
if [[ -n "$UPDATE_DIR_ABS" ]]; then
  MACHINE_OPTS+=",update=$UPDATE_DIR_ABS"
fi
ARGS=( -M "$MACHINE_OPTS" "${ARGS[@]}" )

echo "[run-qemu] cwd=$RUN_CWD"
echo "[run-qemu] $QEMU_BIN ${ARGS[*]} ${EXTRA[*]:-}"
cd "$RUN_CWD"

if [[ $SERIAL_STDIO -eq 1 ]]; then
  # --serial: launch QEMU in background so it owns its own stdio (no
  # raw-mode fight with the terminal), then run `nc` in the foreground
  # of THIS terminal. Cooked-mode echo, line editing, signals all work
  # as the user's shell expects. nc exits → QEMU is killed.
  "$QEMU_BIN" "${ARGS[@]}" "${EXTRA[@]}" </dev/null >/dev/null 2>&1 &
  QEMU_PID=$!
  trap '[[ -n "${QEMU_PID:-}" ]] && kill "$QEMU_PID" 2>/dev/null; [[ -n "$CLEANUP" ]] && rm -rf "$CLEANUP"; [[ -n "$PASST_DIR" ]] && rm -rf "$PASST_DIR"' EXIT INT TERM
  # Wait for the TCP server to come up (QEMU takes ~1s to bind).
  for _ in 1 2 3 4 5 6 7 8 9 10; do
    if (exec 3<>/dev/tcp/127.0.0.1/"$SERIAL_PORT") 2>/dev/null; then
      break
    fi
    sleep 0.3
  done
  # Use rlwrap if available for readline (history/up-arrow/Ctrl-R).
  # History is per-host-user, persisted in ~/.cache/p2k-qemu-build/.
  HIST_DIR="${XDG_CACHE_HOME:-$HOME/.cache}/p2k-qemu-build"
  mkdir -p "$HIST_DIR" 2>/dev/null || true
  if command -v rlwrap >/dev/null 2>&1; then
    echo "[run-qemu] --serial: COM1 → 127.0.0.1:${SERIAL_PORT}; bridging via rlwrap+nc (history: ${HIST_DIR}/serial.history, Ctrl-C to quit)" >&2
    exec rlwrap -A -H "${HIST_DIR}/serial.history" nc 127.0.0.1 "$SERIAL_PORT"
  else
    echo "[run-qemu] --serial: COM1 → 127.0.0.1:${SERIAL_PORT}; bridging via nc (install 'rlwrap' for history/up-arrow; Ctrl-C to quit)" >&2
    exec nc 127.0.0.1 "$SERIAL_PORT"
  fi
fi

"$QEMU_BIN" "${ARGS[@]}" "${EXTRA[@]}" &
QEMU_PID=$!
trap 'status=$?; if [[ -n "${QEMU_PID:-}" ]]; then kill "$QEMU_PID" 2>/dev/null; wait "$QEMU_PID" 2>/dev/null || true; fi; [[ -n "$CLEANUP" ]] && rm -rf "$CLEANUP"; [[ -n "$AUTOMATION_DIR" ]] && rm -rf "$AUTOMATION_DIR"; [[ -n "$PASST_DIR" ]] && rm -rf "$PASST_DIR"; exit "$status"' EXIT INT TERM
if [[ -n "$CONSOLE_SCRIPT" ]]; then
  __script_args=(
    "$ROOT/scripts/internal/run-console-script.py" "$CONSOLE_SCRIPT"
    --port "$SERIAL_PORT"
    --monitor "$AUTOMATION_DIR/monitor.sock"
    --screenshot-dir "${P2K_SCREENSHOT_DIR:-/tmp}"
  )
  if [[ -n "${P2K_DCS_AUDIO_CAPTURE:-}" ]]; then
    __script_args+=(--audio-capture "$P2K_DCS_AUDIO_CAPTURE")
  fi
  python3 "${__script_args[@]}"
fi
wait "$QEMU_PID"
QEMU_STATUS=$?
trap - EXIT INT TERM
[[ -n "$CLEANUP" ]] && rm -rf "$CLEANUP"
[[ -n "$AUTOMATION_DIR" ]] && rm -rf "$AUTOMATION_DIR"
[[ -n "$PASST_DIR" ]] && rm -rf "$PASST_DIR"
exit "$QEMU_STATUS"
