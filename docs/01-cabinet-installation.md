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
  working Wayland desktop. The installer enables autologin and uses an XDG
  autostart entry. Encore inherits the compositor socket, audio, D-Bus and user
  services directly from that session. Exiting Encore returns to the desktop.
- **Cage** creates the smallest standalone single-application Wayland kiosk.
- **Weston** provides a standalone reference Wayland kiosk.
- **Direct console** gives SDL2 direct DRM/KMS ownership through its `KMSDRM`
  video backend. The installer offers this profile only when the installed
  SDL2 reports that backend.

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

Real cabinet I/O normally uses `/dev/parport0`. If selected, the installer adds
the cabinet account to the standard `lp` group instead of making Encore root.

## Installation flow

Run from the permanent checkout or extracted release directory, not `/tmp`:

```sh
./install.sh
```

The guided setup asks for:

1. the session profile;
2. the unprivileged cabinet account;
3. Star Wars Episode I or Revenge from Mars;
4. optional real `/dev/parport0` access when the device exists.

Missing Cage or Weston packages can be installed through APT. If the custom
QEMU binary is absent, the installer offers to install the documented build
dependencies and build it in the selected user's cache.

Non-interactive profile selection is also available:

```sh
./install.sh --display-manager
./install.sh --cage
./install.sh --weston
./install.sh --direct-console
```

Reboot after installation. In a standalone profile, exiting Encore with F1
ends the cabinet session and exposes a normal login prompt on tty1 for
maintenance. Logging out starts a new cabinet session through the normal getty
lifecycle.

## Removal

```sh
./uninstall.sh
```

The uninstaller restores the previous default boot target and tty1 enablement,
removes the autologin and session files it created, and preserves the project,
ROMs, updates and savedata. A `.hushlogin` created to hide standalone-login
chatter is removed only if its device/inode identity and empty contents are
unchanged.

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
