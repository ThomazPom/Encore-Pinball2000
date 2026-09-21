# Debian 13 cabinet laboratory

This laboratory installs a small Debian 13 guest once, seals that qcow2 image,
and runs each automated Encore cabinet experiment in a disposable overlay. It
tests Linux boot/session integration without turning the developer's host into
a cabinet.

It is not a gameplay, timing or physical-hardware certification environment.
The top-level [Cabinet installation](../../01-cabinet-installation.md) guide
owns deployment choices; [Testing and validation](../../26-testing-validation-matrix.md)
defines the wider evidence ladder.

> [!CAUTION]
> `prepare` performs a Debian netinstall and downloads packages. `reset` deletes
> the current disposable overlay. `all` additionally downloads a published
> release and performs a real QEMU build inside guests; it is intentionally
> much slower than one focused lifecycle case.

## Host prerequisites

The harness requires:

- `qemu-system-x86_64` and `qemu-img`;
- `curl`, `cpio`, `gzip`, `tar` and OpenSSH client tools;
- `sshpass` for guest control;
- `expect` for interactive installer cases;
- Git for the host-side asset case;
- QEMU GTK display support for graphical mode, or
  `ENCORE_QEMU_HEADLESS=1` for unattended execution.

KVM is used when `/dev/kvm` is readable and writable. Otherwise the outer VM
uses TCG with `-cpu max`. The default guest allocation is 2 GiB RAM and two
vCPUs.

## Image lifecycle

The sealed base filename includes:

- host locale;
- XKB layout and variant;
- the first 12 hexadecimal characters of the preseed SHA-256.

A locale, keyboard or recipe change therefore selects a new base rather than
silently reusing an incompatible image. Override the locale or keymap with:

```bash
ENCORE_QEMU_LOCALE=en_US.UTF-8 \
ENCORE_QEMU_KEYBOARD=us \
ENCORE_QEMU_KEYBOARD_VARIANT= \
  ./tools/debian-qemu/lab.sh prepare
```

By default images live under
`${XDG_CACHE_HOME:-$HOME/.cache}/encore-qemu`. Set `ENCORE_QEMU_DIR` to move
them, and `ENCORE_QEMU_RAM` or `ENCORE_QEMU_CPUS` to change resources.

Prepare and seal a base:

```bash
./tools/debian-qemu/lab.sh prepare
```

The preseed creates root and `cabinet` test accounts with the laboratory-only
password `cabinet`. It installs OpenSSH, CA certificates, Git and QEMU guest
agent, disables installation of recommended packages, and explicitly removes
`polkitd`, `pkexec` and `sudo`. That absence is part of the privilege test, not
a production recommendation.

`prepare` never overwrites an existing base with the same recipe identity.
Each automated case begins with:

```bash
./tools/debian-qemu/lab.sh reset
```

This stops the current VM, deletes `current.qcow2`, creates a fresh overlay on
the sealed base and marks it for one checkout injection. The base itself is
made read-only and is never booted by a test.

> [!WARNING]
> A manual experiment lives only in `current.qcow2`. Running `reset` or any
> automated test intentionally discards it.

## Focused commands

| Command | What it proves | Not proved |
|---|---|---|
| `test cage user` | Cage install, reboot, unprivileged session, inhibitor, audio policy, maintenance and uninstall | real Cage/DRM rendering |
| `test weston user` | same lifecycle for Weston | real Weston/DRM rendering |
| `test direct-console user` | direct-console generated integration and lifecycle | real SDL KMSDRM/input behavior |
| `test <profile> root` | optional root diagnostic handoff for that standalone profile | that root is appropriate for deployment |
| `test-dm user` | generated SDDM autologin, user service, default target and restoration | an actual graphical login or real GNOME/KDE desktop |
| `test-dm root` | display-manager root-diagnostic handoff | real compositor rendering |
| `test-git` | missing Git is detected and installed | arbitrary distro package managers |
| `test-assets` | absent asset fetch and present-tree no-op | ROM correctness or redistribution rights |
| `test-release` | latest archive download, checksum, extraction and package shape | source checkout equivalence |
| `test-acquire release` | installer acquisition of the release QEMU | future release availability |
| `test-acquire build` | complete pinned QEMU build and machine check in the guest | gameplay timing |
| `test-alternates` | non-default game/LPT/audio/maintenance/GRUB choices and removal | every interactive combination |
| `test-interrupted` | lock-only interrupted installation can be removed safely | rollback after every possible host mutation |

Examples:

```bash
ENCORE_QEMU_HEADLESS=1 ./tools/debian-qemu/lab.sh test cage user
ENCORE_QEMU_HEADLESS=1 ./tools/debian-qemu/lab.sh test-dm user
ENCORE_QEMU_HEADLESS=1 ./tools/debian-qemu/lab.sh test-interrupted
```

Run the complete implemented matrix only when its downloads and build cost are
appropriate:

```bash
ENCORE_QEMU_HEADLESS=1 ./tools/debian-qemu/lab.sh all
```

The `all` command covers Cage, Weston and direct-console in both user and root
modes; display-manager user/root; missing Git; assets; release package; release
and build acquisition; alternate choices; and interrupted-install recovery.
It executes cases sequentially, recreating the overlay for each one.

## What lifecycle tests actually run

The host checkout is copied to `/opt/Encore-PB2K` and linked from the cabinet
user's home. Host and guest SHA-256 hashes of the launcher and runtime-package
helper must match before testing continues.

Automated lifecycle cases then install controlled stand-ins:

- a QEMU executable that advertises the `pinball2000` machine and sleeps;
- Cage and Weston wrappers that provide the expected Wayland environment;
- a `wpctl` fixture that starts muted at 50% and records the cabinet policy.

These stand-ins keep the test focused on generated services and transitions.
The cases still execute the real `install.sh`, session helper and
`uninstall.sh`. They assert configuration contents, reboot behavior,
PAM/logind open and close records, the live `systemd-inhibit` owner, audio
before/after evidence, maintenance getty and uninstall symmetry. Shared `lp`
membership must remain after uninstall.

Before an unprivileged installation, the test confirms the sealed guest has
`run0` but lacks `pkttyagent`, `pkexec` and `sudo`. It installs only `polkitd`
in the disposable overlay, then launches the installer as `cabinet`. This
exercises the `run0` plus `pkttyagent` path rather than bypassing elevation by
starting the installer as root.

> [!NOTE]
> If this initial stripped-guest assertion fails, the test must stop. Do not
> reinterpret a stale cached base as passing evidence; prepare the base whose
> filename matches the current preseed hash.

## Open-bus parallel-port fixture

The outer QEMU VM exposes no emulated parallel controller. After every boot,
the harness asks Linux `parport_pc` to register the unimplemented ISA address
`0x378` and loads `ppdev`. The guest receives a real `/dev/parport0` character
device whose reads reach an open virtual hardware bus.

This exercises Encore's real ppdev open, claim and ioctl path and allows the
original game software to diagnose a disconnected driver board. It does not
emulate Encore's software `disconnected` policy and does not prove a cable,
powered driver board, switches, lamps or solenoids. See
[Real LPT passthrough](../../46-real-lpt-passthrough.md) for that boundary.

## Interactive use

Create a fresh graphical experiment with the current checkout:

```bash
./tools/debian-qemu/lab.sh reset
./tools/debian-qemu/lab.sh manual
```

The default starts an 800×600 GTK QEMU window. On the first `manual` boot after
reset, the harness copies the checkout and installs `polkitd`; later manual
boots preserve the same overlay and do not replace its guest worktree.

Inside the guest:

```bash
cd ~/Encore-PB2K
./install.sh
```

To inspect the published package instead of copying the checkout:

```bash
./tools/debian-qemu/lab.sh release
```

`release` first creates a fresh overlay. It then downloads the latest archive
and checksum inside the guest, verifies and extracts it to
`~/Encore-Pinball2000`, checks the minimal release shape and leaves the VM
running for manual installation.

Operational commands are:

```bash
./tools/debian-qemu/lab.sh boot
./tools/debian-qemu/lab.sh shell 'systemctl status getty@tty1.service'
./tools/debian-qemu/lab.sh stop
```

Serial output is retained as `serial.log` in the lab directory. The current
overlay is preserved by `boot`, repeated `manual` boots, `shell` and `stop`.
`reset`, `release` and VM-backed automated cases intentionally replace it;
the host-only `test-assets` case does not.

## Evidence boundary

The automated lab can support claims about:

- generated files, ownership markers and restoration;
- systemd target, getty, user-service and inhibitor transitions;
- the unprivileged escalation route;
- installer acquisition control flow;
- ppdev transport against an open bus.

It cannot support claims about:

- pixels from a real compositor/DRM/KMS stack;
- physical input, audio level or sound quality;
- a powered Pinball 2000 driver board;
- gameplay stability, guest timing, IRQ depth or IStack margin;
- network behavior outside the controlled guest;
- compatibility of a newly changed preseed until `prepare` and the relevant
  cases have completed with that recipe identity.

Promote conclusions only through the normal-run and measurement procedure in
[Testing and validation](../../26-testing-validation-matrix.md).
