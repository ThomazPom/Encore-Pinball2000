# 01 — Cabinet installation

Encore has a native cabinet installer for systemd-based Debian, Ubuntu and
Kali systems:

```sh
./install.sh
```

The installer builds Encore when necessary, configures the selected game and
creates the boot session. It never installs or configures Xorg, an X11 window
manager, or XWayland.

## Profiles

```text
                          cabinet profile
                                │
          ┌─────────────────────┼─────────────────────┐
          │                     │                     │
          ▼                     ▼                     ▼
 existing GDM/SDDM          Cage / Weston       direct console
          │                     │                     │
          ▼                     ▼                     ▼
 existing Wayland       standalone Wayland       SDL2 KMSDRM
 user session           PAM/logind session       PAM/logind session
          │                     │                     │
          └─────────────────────┴─────────────────────┘
                                │
                                ▼
                  Encore direct SDL2 renderer
```

- **Existing display manager** is recommended when GDM or SDDM already owns a
  working Wayland desktop. The installer enables autologin. A service managed
  by the selected user's own `systemd --user` instance is attached to
  `graphical-session.target`. GNOME/KWin and GDM/SDDM remain responsible for
  establishing and publishing Wayland, D-Bus and audio. An `ExecStartPre`
  readiness gate waits for the user manager to contain a live Wayland socket;
  systemd then starts Encore with the freshly imported environment.
  This uses no XDG-autostart race or root environment broker. Encore requests
  both fullscreen state and window raising through SDL's portable video API.
  A capability dispatcher additionally
  dismisses a login overview when the running desktop publicly exposes one:
  Mutter through `org.gnome.Shell.OverviewActive`, and KWin through its active
  `overview` effect. It does not infer a desktop from names or configuration.
  Sway, Hyprland, niri, Wayfire, labwc, Weston, Cage and Gamescope have no
  equivalent startup overview to dismiss and stay on the plain SDL path.
  Exiting Encore returns to the desktop.
- **Cage** creates the smallest standalone single-application Wayland kiosk.
- **Weston** provides a standalone reference Wayland kiosk.
- **Direct console** gives SDL2 direct DRM/KMS ownership through its `KMSDRM`
  video backend. It is not SDL 1.2 `fbcon`, despite Linux potentially exposing
  a compatibility `/dev/fb0` device. The installer verifies KMSDRM directly.

The graphical profiles all run Encore as a native Wayland client. There is no
hidden X11 fallback. If a display-manager login is not a Wayland session,
Encore refuses to start and records the reason in the journal.

## Why the launcher stays unprivileged

Unlike the historical Nucore binary, Encore does not need root to run. A
standalone profile therefore uses the ordinary distribution login chain:

```text
systemd → agetty → login → PAM/logind → systemd --user → compositor → Encore
```

That real login session supplies `XDG_RUNTIME_DIR`, Wayland, D-Bus and the
distribution's normal audio services automatically. No service copies the
environment, guesses a socket, or imports a user-controlled environment into a
root process.

Real cabinet I/O uses a Linux `ppdev` character device such as
`/dev/parport0` or `/dev/parport1`. If selected, the installer adds the cabinet
account to the standard `lp` group instead of making Encore root.

## Installation flow

Run from the permanent checkout or extracted release directory, not `/tmp`:

```sh
./install.sh
```

The guided setup asks for:

1. the session profile;
2. the unprivileged cabinet account;
3. Star Wars Episode I or Revenge from Mars;
4. whether to start with flipscreen active (enabled by default);
5. whether to use the normal unprivileged runtime (default) or an experimental
   root diagnostic service;
6. whether cabinet startup may unmute the default host output and set it to
   100%, leaving the game's controls to manage the playing volume;
7. whether to use the distribution's quiet boot presentation;
8. whether to hide GRUB with a zero-second timeout when `update-grub` exists;
9. real `/dev/parportN` access when detected (enabled by default, with an
   opt-out for demonstration mode).

Root execution is only an A/B diagnostic escape hatch. The real PAM/logind
user session still owns Wayland, D-Bus and audio; it publishes a private
runtime rendezvous consumed by the optional root system service. The normal
installation creates no privileged Encore service.

The host-volume option is enabled by default and can be declined during
installation. When enabled, each cabinet launch changes only the current
default output: it sets 100% and removes mute using an available host control
tool. It does not select a sound card, hard-code PipeWire or PulseAudio, or
continuously synchronize host and guest volume. The guest's service buttons
remain the normal playing-volume controls.

Each launch records the selected policy, control tool, and observed before and
after state in the journal. It can be checked after boot with:

```sh
journalctl -b --no-pager | grep '\[encore-audio\]'
```

That choice adds `--flipscreen`. It vertically reverses the displayed image
relative to the normal display. F2 toggles exactly that same state while Encore
is running. The cabinet installer enables it by default; declining the prompt
leaves the normal display unchanged.

Missing Cage, Weston, SDL2 or host graphics components can be installed through
APT. The installer queries SDL itself for the required Wayland or KMSDRM
backend, installs the appropriate runtime when requested, then checks again.
If the custom QEMU binary is absent, it similarly offers the documented build
dependencies and builds QEMU in the selected user's cache.

The installer discovers every `/dev/parportN` device rather than assuming port
zero. When the kernel exposes a port through `/sys/class/parport` but no ppdev
device exists yet, it attempts to load the kernel's `ppdev` module and waits
for udev. If several ports exist, the operator chooses one. The selected path
must be a character device. Declining the default-enabled prompt keeps the
emulated board. Detailed electrical validation and diagnostic procedures remain in
[real LPT passthrough](46-real-lpt-passthrough.md).

Non-interactive profile selection is also available:

```sh
./install.sh --display-manager
./install.sh --cage
./install.sh --weston
./install.sh --direct-console
```

Standalone profiles own tty1 and cannot coexist with another cabinet launcher
that manages the same getty. The installer detects conflicting persistent or
runtime getty drop-ins and asks you to remove the other integration first.

For an unprivileged direct-console session, the installer lets logind grant
the active local login temporary access to `/dev/input/event*`. This is needed
because SDL/KMSDRM reads evdev directly. The account is not added to the
global `input` group, and the managed udev rule is removed on uninstall.

Reboot after installation. In a standalone profile, the installer asks whether
F1 should hand the machine to the existing display manager or to a
password-backed tty1 login. The display-manager handoff is only for the current
boot; the next boot still returns directly to Encore. Exiting Encore with F1
ends the cabinet session before the selected maintenance environment starts.
For the duration of a standalone installation, a small system
copy of the session helper is the selected account's login shell. It launches
Encore only on tty1; SSH, other VTs and maintenance logins immediately delegate
to the user's saved original shell. When tty maintenance is selected, the
root `ExecStopPost` installs a runtime-only ordinary-getty override after
PAM/logind closes the cabinet session. Further maintenance logouts restart that
ordinary getty, never the automatic Encore entry point, until reboot clears the
runtime override.

The quiet-boot option adds only a project-owned GRUB drop-in. It requests
`quiet`, low kernel/systemd/udev verbosity, a hidden cursor and Plymouth's
`splash` argument when Plymouth is installed. The separate GRUB choice hides
the menu and sets its timeout to zero. The installer then runs `update-grub`.
It does not edit `/etc/default/grub` in place.

Standalone profiles keep tty1 as the real controlling terminal but send the
automatic-login and compositor diagnostics to the journal. `agetty` invokes
the distribution's normal `/bin/login` and PAM/logind stack directly. Encore
does not install a custom PAM file or synthesize a user-service environment.

## Removal

```sh
./uninstall.sh
```

The uninstaller restores the previous default boot target and tty1 enablement,
removes the autologin, GRUB drop-in, silent-handoff generator and session files
it created, restores the account's exact original login shell, regenerates GRUB
when required, and preserves the project, ROMs,
updates and savedata. A `.hushlogin` created to hide standalone-login chatter
is removed only if its device/inode identity and empty contents are unchanged.

## Manual backend checks

From an existing Wayland session:

```sh
scripts/run-qemu.sh --wayland --fullscreen --game swe1
```

From a free local login VT with no compositor or display manager owning DRM:

```sh
scripts/run-qemu.sh --direct-console --fullscreen --game swe1
```

The second command intentionally fails when SDL2 lacks KMSDRM or the current
session does not own the active seat/display devices.
