# Debian netinst regression VM

This laboratory creates a genuinely minimal Debian 13 guest once, keeps that
post-install disk immutable, and runs every Encore Pinball 2000 experiment in a
fresh qcow2 overlay.

By default the guest opens a GTK window backed by an 800x600 virtual display,
so fullscreen placement and scaling can be inspected. Set
`ENCORE_QEMU_HEADLESS=1` only for unattended or CI runs.

The base guest intentionally has no desktop, `sudo`, `pkexec`, `polkitd`, Xorg
or Wayland compositor. It includes only enough infrastructure for automated
testing: OpenSSH, Git, CA certificates and `qemu-guest-agent`. The host harness
needs `qemu-system-x86`, `qemu-system-gui`, `qemu-utils`, `curl`, `cpio`,
`sshpass` and `expect`.

Useful commands:

```sh
./tools/debian-qemu/lab.sh prepare       # netinstall and seal the base image
./tools/debian-qemu/lab.sh test cage user # unprivileged lifecycle test
./tools/debian-qemu/lab.sh test cage root # root diagnostic A/B test
./tools/debian-qemu/lab.sh manual        # fresh graphical VM, ready after netinst
./tools/debian-qemu/lab.sh shell         # SSH into the current overlay
./tools/debian-qemu/lab.sh stop
./tools/debian-qemu/lab.sh reset         # discard only the current overlay
```

The automated path uses controlled fake Cage and QEMU executables. It tests
the installer and system lifecycle rather than guest GPU or emulation:
non-root elevation, PAM/logind login, inhibitor lifetime, explicit
transition to a persistent maintenance getty, optional root execution, and
reversible uninstall. Weston, real rendering, audio and cabinet timing remain
physical-host tests.

Artifacts live outside Git under `${XDG_CACHE_HOME:-~/.cache}/encore-qemu` by
default. Set `ENCORE_QEMU_DIR`, `ENCORE_QEMU_CPUS`, or `ENCORE_QEMU_RAM` to
override the location and VM size. The base image is never booted by a test;
`current.qcow2` is always a disposable backing-file overlay.

The netinstall inherits the host's `LANG`, keyboard layout and keyboard
variant. Different locale/keymap combinations receive different sealed base
images, so switching between AZERTY and QWERTY never mutates an existing base.
They can also be selected explicitly with `ENCORE_QEMU_LOCALE`,
`ENCORE_QEMU_KEYBOARD` and `ENCORE_QEMU_KEYBOARD_VARIANT`.

The automated test first proves that the stock guest has `run0`, but no
`pkttyagent`, `pkexec`, `polkitd` or `sudo`. It then installs only `polkitd` in
the disposable overlay and launches `./install.sh` as the unprivileged
`cabinet` user. This exercises the corrected `run0`/`pkttyagent` path instead
of bypassing it as root. It validates installation state and uninstall
symmetry before discarding the VM.

`manual` starts from the sealed end-of-netinstall snapshot, adds only `polkitd`
to make its non-root `run0` test possible, and stops there. The current checkout
is available at `~/Encore-PB2K`, and the graphical console is left open for
interactive install, reboot and scaling checks. Both the test user and root
use the laboratory-only password `cabinet`.

The outer VM deliberately exposes no QEMU parallel controller. After boot, the
harness forces Linux `parport_pc` to register the otherwise unimplemented ISA
address `0x378`, then loads `ppdev`. The guest therefore receives a genuine
`/dev/parport0`: Encore must open it, claim it and use the normal ppdev ioctls,
while reads reach an open virtual hardware bus with no board behind it. This
tests the complete real-cabinet transport and lets the original ROM diagnose
the disconnected driver board; it does not substitute Encore's software
`disconnected` target.
