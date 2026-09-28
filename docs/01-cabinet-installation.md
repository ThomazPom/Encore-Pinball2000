# 01 — Cabinet installation

This workflow turns a Linux host into a Pinball 2000 cabinet: boot reaches a
fullscreen Encore session automatically, the emulator normally runs as an
unprivileged user, suspend and shutdown are inhibited while the game runs,
and leaving the game opens the selected maintenance path.

For an ordinary desktop launch, use [Quickstart](02-quickstart.md) instead.
Read [Real LPT passthrough](46-real-lpt-passthrough.md) before connecting a
physical driver board.

> [!IMPORTANT]
> `install.sh` changes login, boot and display-manager integration. Run it
> from the final, persistent Encore directory—not from `/tmp`—and keep that
> directory in place while the cabinet profile is installed.

## Recommended starting point

Use a dedicated cabinet account on a systemd-based Linux installation with a
working Wayland login. If GDM or SDDM is already installed, start with:

```bash
./install.sh --display-manager
```

Accept the safe defaults for a first installation:

| Choice | First-install recommendation | Reason |
|---|---|---|
| session user | dedicated normal user, UID at least 1000 | owns the graphical and emulator session |
| game | `auto` | follows a recognized real board, otherwise selects SWE1 |
| LPT | `emulated` during commissioning | prevents an accidental physical-board probe |
| network card | disabled | removes an unrelated configuration variable |
| flipscreen | enabled for a mounted cabinet display | matches the normal cabinet orientation; `F2` changes it live |
| execution | session user | root mode is diagnostic only |
| host audio | authorize only on a dedicated cabinet | unmutes and sets the current default output to 100% at every start |
| quiet boot | optional | changes presentation, not emulator behavior |
| zero-second GRUB menu | leave disabled until recovery is proven | otherwise local boot recovery is harder |

Confirm that this conservative profile boots, accepts controls, produces
sound and exits cleanly before selecting `auto` or `required` LPT, networking,
quiet boot or a standalone display stack.

> [!CAUTION]
> `auto` LPT is convenient only after the cabinet wiring is ready: it actively
> probes a usable `/dev/parportN` and falls back to emulation if no recognized
> board is found. `required` stops instead of falling back. Physical bring-up
> is a separate staged procedure.

## What the installer does

The installer is interactive even when a display profile is named on the
command line. Before changing cabinet integration it:

1. finds or asks for a non-system session user;
2. collects game, LPT, optional network, display, execution and boot choices;
3. shows the complete configuration and asks for confirmation;
4. runs the normal launcher in `--preflight --fullscreen` mode for the future
   session user's runtime identity;
5. verifies that the selected custom QEMU implements the `pinball2000`
   machine;
6. refuses collisions with unrelated service, getty, shell, GRUB or cabinet
   ownership files;
7. writes the selected integration and synchronizes it before reporting
   success.

Preflight owns runtime preparation. It may offer to install packages, acquire
the custom QEMU executable, fetch absent ROM/update trees, add the session user
to `lp`, or prepare a managed bridge TAP. QEMU itself still runs as the session
user unless root diagnostic mode was explicitly selected.

The final message asks for a reboot. Rebooting is significant: it starts the
new default target, obtains a fresh login/PAM session and makes any new
supplementary group membership effective.

## Choose a display profile

List the supported entry points without changing the host:

```bash
./install.sh --help
```

The first choice determines who owns the graphical session and where `F1`
leaves the operator:

```text
                              ./install.sh
                                    │
              ┌─────────────────────┴──────────────────────┐
              │                                            │
              ▼                                            ▼
   --display-manager (default)                       standalone tty1
              │                                            │
              │                     ┌──────────────────────┼───────────────────┐
              │                     │                      │                   │
              ▼                     ▼                      ▼                   ▼
        GDM or SDDM          --direct-console           --cage              --weston
              │                     │                      │                   │
              ▼                     ▼                      ▼                   ▼
   existing Wayland desktop     SDL2 KMSDRM          Cage Wayland        Weston Wayland
              │                     │                      │                   │
              ▼                     │                      └─────────┬─────────┘
   user systemd service             │                                │
              │                     └──────────────┬─────────────────┘
              │                                    │
              ▼                                    ▼
   Encore direct SDL2 renderer        Encore direct SDL2 renderer
              │                                    │
              ▼                                    ▼
      F1 returns to desktop          F1 ends cabinet session, then opens
                                     the selected maintenance tty or DM
                                     for this boot; reboot returns to Encore
```

The two families still share the distribution's ordinary unprivileged login
machinery:

```text
display-manager:
systemd → GDM/SDDM → PAM/logind → Wayland desktop → systemd --user → Encore

standalone:
systemd → agetty → login → PAM/logind → systemd --user
        → Cage / Weston / SDL2 KMSDRM → Encore
```

No branch installs Xorg, an X11 window manager or XWayland, and none requires
Encore itself to run as root.

| Profile | Session path | Host requirement | When to use it |
|---|---|---|---|
| `--display-manager` | GDM/SDDM autologin → Wayland session → user systemd service | an existing supported GDM or SDDM Wayland setup | default and easiest first choice; keeps the distribution's familiar desktop/session lifecycle |
| `--direct-console` | tty1 autologin → PAM/logind → SDL2 KMSDRM → Encore | SDL2 KMSDRM and direct input/DRM access | equally valid production choice: fewer graphical layers and a cabinet-focused direct path |
| `--cage` | tty1 autologin → PAM/logind → Cage kiosk → Encore | DRM/KMS and Cage | compact standalone Wayland compositor when its isolation model is wanted |
| `--weston` | tty1 autologin → PAM/logind → Weston DRM kiosk → Encore | DRM/KMS and Weston | reference standalone Wayland path and useful Cage comparison |

All four profiles are native Wayland or SDL2/KMSDRM paths. The installer does
not create an Xorg, X11 window-manager or XWayland cabinet session.

Choose by operating model:

- keep `--display-manager` for the safest first installation, a casual or
  mixed-use host, familiar desktop recovery, and the least disruption to an
  existing GDM/SDDM system;
- choose `--direct-console` for a dedicated production cabinet when SDL2
  KMSDRM has passed the local-seat test and minimizing compositor/session
  layers matters more than retaining a desktop;
- choose `--cage` when a small standalone Wayland kiosk is specifically
  preferred;
- choose `--weston` when its reference compositor or comparison value is the
  reason for the deployment.

`--direct-console` is not a degraded fallback. It trades the display manager's
desktop convenience for a shorter SDL2/KMSDRM path and stricter local-seat
requirements. Keeping display-manager mode as the installer default reflects
lower first-install risk, not higher emulator fidelity.

`--display-manager` accepts only a detected GDM or SDDM service. It creates an
autologin configuration and a user service attached to
`graphical-session.target`. The service waits up to 180 seconds for a real
Wayland socket imported into the user manager. A desktop session that starts
as X11 is rejected rather than silently using a different rendering path.

The three standalone profiles make tty1 the cabinet login. They temporarily
set the selected account's login shell to Encore's session helper and enable
an autologin getty. The helper launches the cabinet only on tty1; SSH, other
virtual terminals, display-manager command invocations and the maintenance
login delegate to the user's original shell.

Direct-console preflight installs a project-marked udev rule only when needed
so logind can grant the active local session access to evdev. It does not add
the account to the broad `input` group, and uninstall removes the managed
rule. Run this profile only from the active local seat with DRM available.

> [!NOTE]
> Cage and Weston are not wrappers around an anonymous root process. The
> autologin still passes through `login`, PAM and logind, so the user gets the
> runtime directory, D-Bus session, device ACLs and session teardown expected
> by a normal graphical login.

## Game, LPT and network choices

The persistent game choice is `auto`, `swe1` or `rfm`. `auto` is useful in a
finished two-game cabinet; an explicit game is easier to diagnose while
commissioning.

The installed LPT policy is one of:

- `emulated`: never opens a physical parallel port;
- `auto`: uses a recognized real board and otherwise installs the emulated
  board;
- `required`: requires a recognized real board and aborts the launch if none
  is available.

The installer deliberately does not offer an arbitrary `/dev/parportN` path.
Use the ordinary launcher to commission an explicit port first, then choose a
stable installed policy. See [LPT driver board](26-lpt-board.md) for protocol
behavior and [Real LPT passthrough](46-real-lpt-passthrough.md) for the host
device procedure.

Networking is disabled by default. When enabled, the installer exposes the
same `auto`, `mirror`, `nat`, `passt`, `isolated` and existing-Linux-bridge
modes as the launcher. Automatic mode is the normal choice. Bridge mode
creates and owns only a TAP attached to an already existing bridge; it does
not create or reconfigure that bridge.

When the installer configures XINA, it records address, contiguous mask,
gateway and DNS through the guest's native persistent resources. Automatic
and conventional NAT default to QEMU's `10.0.2.3` DNS forwarder. Mirror and
passt modes prefer a non-loopback resolver detected from the host and fall
back to the selected gateway; the confirmation screen shows the final value.

Local forwards bind cabinet-only services; network forwards make them
reachable through the host. XINA's web and Telnet services are historical and
must not be treated as modern authenticated Internet services. Full topology
and guest-address behavior belong in [Optional network card](48-network.md).

## Audio, fullscreen and power behavior

Every profile starts the normal launcher with fullscreen enabled. The selected
flipscreen state is stored as a launcher argument; `F2` toggles the display
orientation at runtime.

If host-audio control was authorized, the session tries these existing host
tools in order:

1. `wpctl` for the default PipeWire sink;
2. `pactl` for the default PulseAudio-compatible sink;
3. `amixer` for the `Master` control.

It records before/after state under the `encore-audio` journal tag. Failure is
non-fatal and does not select an audio server or hard-code an output device.
Without authorization, the host mixer is left unchanged.

While Encore runs, `systemd-inhibit` blocks idle handling, suspend and
shutdown. This inhibitor belongs to the emulator lifetime; it is released
when the session ends.

In an existing GNOME or KDE session, a bounded background adapter dismisses a
login overview only when Mutter or KWin exposes the corresponding public D-Bus
capability. Other compositors remain on the ordinary SDL fullscreen path.

## Check a backend before changing boot

From an existing Wayland session, exercise the renderer with physical I/O
disabled:

```bash
scripts/run-qemu.sh --wayland --fullscreen \
  --game swe1 --lpt-device emulated
```

From a free local login VT, with no compositor or display manager owning DRM,
exercise SDL2 KMSDRM:

```bash
scripts/run-qemu.sh --fullscreen \
  --game swe1 --lpt-device emulated
```

The second command is expected to fail if SDL2 lacks KMSDRM or the login does
not own the active seat/display devices. Resolve that before installing the
direct-console profile.

## Normal exit and maintenance

`F1` requests a clean emulator exit. In a display-manager profile, the user
service ends inside the graphical session. In a standalone profile, the
getty's `ExecStopPost` waits until the cabinet login and PAM session have
closed, then hands tty1 to exactly one selected maintenance path:

- a normal password-backed tty1 login; or
- an installed display manager, when that was selected during installation.

The next reboot still enters the cabinet profile. Maintenance mode is a
post-exit handoff, not a permanent change to the default boot profile.

For logs after an exit:

```bash
journalctl -b -u getty@tty1.service --no-pager
journalctl -b -t encore-cage --no-pager
journalctl -b -t encore-weston --no-pager
journalctl -b -t encore-audio --no-pager
```

Display-manager installations also expose a user service:

```bash
systemctl --user status encore-pinball2000.service
journalctl --user -u encore-pinball2000.service --no-pager
```

## Root diagnostic mode

Root execution is an explicit A/B diagnostic, not a fix for missing device
permissions. The normal user session still owns the Wayland socket and D-Bus
environment; a small root service waits for a protected handoff file and then
starts the launcher with the session user's runtime context.

Use it only to answer a narrow question such as “does this failure disappear
when device permissions are bypassed?”. Return to unprivileged execution after
the comparison. A persistent root-only success means the host access policy
still needs repair.

## Managed host state

The durable configuration is deliberately inspectable:

```text
/etc/encore-pinball2000/session.conf
/etc/encore-pinball2000/launch.args
/var/lib/encore-pinball2000/
/var/lib/pinball2000-cabinet.lock
```

Depending on the selected choices, Encore also owns marked files under:

- `/etc/systemd/system/` for getty, root diagnostic and managed bridge units;
- the session user's `~/.config/systemd/user/` for display-manager mode;
- `/etc/gdm*/` or `/etc/sddm.conf.d/` for autologin;
- `/usr/local/libexec/` and `/etc/shells` for standalone login;
- `/etc/default/grub.d/` and `/etc/grub.d/` for optional boot presentation;
- `/etc/udev/rules.d/` for managed direct-console input access.

Quiet boot uses a project-owned GRUB drop-in for `quiet`, reduced
kernel/systemd/udev verbosity, a hidden cursor and `splash` when Plymouth is
available. The separate zero-timeout choice installs a marked early GRUB
script. Neither path edits `/etc/default/grub` in place; both regenerate the
GRUB configuration when `update-grub` is available.

The private state directory records the previous default target, getty state,
original login shell and identities of files created by the installer. The
ownership lock is written before any installer-owned boot/session mutation.
If power is lost in the tiny interval before `install-mode` is written,
`uninstall.sh` recognizes the Encore-owned partial marker and safely removes
it. Runtime prerequisites that preflight prepared earlier—such as packages or
shared group membership—are intentionally preserved.

> [!WARNING]
> Do not hand-edit generated unit or ownership files as a normal way to change
> profiles. Uninstall first, rerun the installer, and preserve the warning if
> uninstall reports that an expected managed file was changed.

## Change or remove a profile

There is no in-place profile conversion. Remove the current integration, then
install the new choices:

```bash
./uninstall.sh
./install.sh --display-manager
```

The uninstaller requires the cabinet lock to contain the Encore owner marker;
it refuses an absent, unreadable or foreign lock. For Encore-owned state it:

- stops and removes managed services;
- removes only user files whose expected ownership marker still matches;
- restores the original login shell, previous default target and prior getty
  enable/mask state;
- removes managed autologin, GRUB, direct-input and TAP integration;
- leaves modified user files in place with a warning instead of deleting
  them blindly.

A `.hushlogin` created for a standalone cabinet is removed only when it is
still empty and has the same device/inode identity recorded at installation.

It intentionally preserves the project directory, ROMs, updates, savedata,
download/build caches and shared `lp` group membership. Inspect its passive
contract with:

```bash
./uninstall.sh --help
```

Reboot after uninstalling when validating that the host's ordinary login and
display path have been restored.

## Recovery

If installation stops after confirmation:

1. keep the checkout at the same path;
2. run `./uninstall.sh` from it;
3. read any “leaving changed … in place” warning before editing files;
4. reboot and verify the host's ordinary login;
5. rerun preflight or the installer only after cleanup succeeds.

If uninstall says the lock belongs to another cabinet integration, stop. That
is a collision guard, not an instruction to delete the lock. Identify and
remove the owning integration through its own uninstaller.

If a standalone cabinet does not reach the game, use another VT or SSH and
collect:

```bash
systemctl status getty@tty1.service --no-pager
journalctl -b -u getty@tty1.service --no-pager
cat /etc/encore-pinball2000/session.conf
cat /etc/encore-pinball2000/launch.args
```

Continue with [Troubleshooting](04-troubleshooting.md) without changing timing,
audio engine or headless/display mode before reproducing the original failure.

## Validation boundary

The disposable Debian laboratory exercises generated integration rather than
claiming physical cabinet proof:

```bash
ENCORE_QEMU_HEADLESS=1 ./tools/debian-qemu/lab.sh test cage user
ENCORE_QEMU_HEADLESS=1 ./tools/debian-qemu/lab.sh test-dm user
ENCORE_QEMU_HEADLESS=1 ./tools/debian-qemu/lab.sh test-interrupted
```

Its lifecycle cases use controlled compositor and QEMU stand-ins to validate
install, reboot, PAM/logind, inhibitor, audio-policy, maintenance and uninstall
transitions. Separate acquisition cases download a release or build QEMU. The
outer VM's `/dev/parport0` reaches an open virtual ISA bus, not a powered
driver board.

> [!NOTE]
> The base image is keyed by locale, keyboard and the preseed recipe hash. A
> recipe change therefore creates a new sealed image instead of silently
> reusing an incompatible one. `prepare` may download and install Debian; the
> per-test overlays are disposable.

The lab does **not** certify a real compositor/DRM stack, physical audio,
cabinet wiring, powered LPT outputs or gameplay timing. Finish a deployment
with the normal full user run and evidence ladder in
[Testing and validation](26-testing-validation-matrix.md).

---

[Documentation index](README.md) · [Quickstart](02-quickstart.md) ·
[Troubleshooting](04-troubleshooting.md) ·
[Testing and validation](26-testing-validation-matrix.md)
