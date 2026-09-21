# 46 — Real LPT passthrough

Encore can connect the guest's three LPT registers directly to a Linux
`ppdev` character device and let the original Pinball 2000 software operate a
real driver board. This is raw cabinet I/O: reads, output writes, keepalive and
lamp/solenoid commands reach the selected hardware.

The emulated command protocol and input rules are documented in
[LPT driver board](26-lpt-board.md). This page owns Linux preparation, the
first physical bring-up and the boundary between kernel-path and real-cabinet
evidence.

> [!CAUTION]
> A real launch is an active hardware operation, not device discovery in the
> usual read-only sense. Encore claims the port and emits driver-board command
> cycles before loading a game ROM. Use the cabinet service documentation to
> verify wiring, grounding, supply isolation and safe power state. This
> repository does not define the electrical connector pinout.

### Implementation owners

| Concern | Primary source |
|---|---|
| ppdev claim, probe and runtime I/O | `qemu/p2k-lpt-board.c` |
| option policy and immediate group re-entry | `scripts/run-qemu.sh` |
| `lp` group preparation | `scripts/internal/runtime-packages.sh` |
| installed cabinet selection/preflight | `install.sh` |
| temporary open-bus ppdev fixture | `tools/test-disconnected-vport.sh` |
| Debian VM open-bus laboratory | `tools/debian-qemu/lab.sh` |

## Required host interface

Physical passthrough is Linux-only and requires a character device named
`/dev/parportN`. The kernel `ppdev` API behind that node must support:

- exclusive `PPCLAIM`/`PPRELEASE`;
- DATA, STATUS and CONTROL reads/writes;
- IEEE-1284 compatibility mode;
- `PPDATADIR` direction changes for switch reads.

The launcher deliberately accepts only `/dev/parportN` as an explicit path.
`/dev/usb/lpN` is a printer-class interface, not the ppdev raw parallel-port
API, and cannot be substituted. Many inexpensive USB printer cables expose
only that printer interface; a host controller supported by `parport_pc` or an
equivalent real ppdev provider is required.

On a conventional PC-style controller the relevant kernel modules are usually:

```bash
sudo modprobe parport_pc
sudo modprobe ppdev
ls -l /dev/parport*
```

Module discovery is a host/kernel responsibility. Encore does not load these
modules during a normal launch because changing the machine's parallel-port
configuration can disrupt another device or driver.

On Debian-family systems the standard udev policy normally assigns ppdev nodes
to group `lp`. Inspect the actual host rather than assuming:

```bash
stat -c 'node=%n type=%F owner=%U group=%G mode=%A' /dev/parport0
getent group lp
id -nG
```

The node must be readable and writable by the user that will run Encore. It
must not already be claimed by another program.

Linux parport is designed to share a controller among registered drivers; do
not blindly unload the printer driver. If `PPCLAIM` fails, inspect the kernel's
active owner first:

```bash
cat /proc/sys/dev/parport/parport0/devices/active
```

Stop or detach only the identified conflicting consumer. The
[Linux parport documentation](https://docs.kernel.org/admin-guide/parport.html)
describes this sharing model and the `/proc/sys/dev/parport` state.

## Safe preparation without probing the board

Run preflight as the normal desktop/cabinet user:

```bash
scripts/run-qemu.sh --preflight \
  --game auto --lpt-device /dev/parport0
```

Preflight validates the character-device path, prepares runtime dependencies
and, when needed, asks for elevation to add the runtime user to `lp`. The
launcher then re-enters itself once through `sg lp`, so a normal interactive
launch does not require logging out and back in.

Preflight fetches complete ROM/update trees when their configured directories
are absent, then stops before cache generation or QEMU launch. It therefore
does **not** open, claim or probe the board and cannot prove that the cable,
board or game signature works.

If the account database was updated but access still fails, check the node's
real group/mode and any site-specific udev rule. Do not solve it by running the
whole emulator as root: that changes desktop/audio/session behavior and gives
the guest-facing process unnecessary privilege.

The cabinet installer offers `auto`, `emulated` or `required`, performs this
runtime preparation for the selected unprivileged session user, and records
the choice in its launch arguments. The following cabinet reboot starts a
fresh session with the installed group membership. See
[Cabinet installation](01-cabinet-installation.md) for the wider kiosk setup.

Uninstalling the cabinet session does not remove a user from `lp`, because that
membership may be shared with other host software. If it was added solely for
Encore and is no longer wanted, an administrator must revoke it deliberately.

## Choose the physical policy deliberately

| Selection | Physical meaning | Failure behavior |
|---|---|---|
| `/dev/parportN` | use exactly this ppdev node | open/claim failure is fatal; a silent/unrecognized cable remains attached for guest diagnosis |
| `required` | actively scan accessible `/dev/parport0`…`31` for a recognized board | no recognized signature is fatal |
| `auto` | same active scan | falls back to the software board when no recognized signature is found |
| `emulated` | never touch a physical port | always use the software board |

Use the explicit path for first bring-up: it identifies the target and cannot
conceal a silent cable behind emulation. Use `required` for an installed
cabinet that must not boot without its recognized board. `auto` is convenient
for a laptop/cabinet-shared profile, but a successful game boot alone does not
prove that it selected hardware.

> [!WARNING]
> `auto` and `required` are active scans. They open/claim every accessible
> candidate long enough to send the Pinball 2000 identification sequence.
> Do not use them on a host where another `/dev/parportN` controls unrelated
> equipment.

## First active board run

Only continue after the cabinet is in the electrically safe state required by
its service procedure. Let the board identify the game on the first run:

```bash
scripts/run-qemu.sh \
  --game auto \
  --lpt-device /dev/parport0 \
  --no-savedata
```

The resolver opens and exclusively claims the node, requests ordinary SPP
compatibility mode, defaults DATA to output, sends the identification command
sequence and samples the physical rows. A recognized signature selects SWE1
or RFM. The runtime then reopens the same node and maps guest I/O
`0x378–0x37a` directly to it.

Expected evidence includes all of:

```text
pinball2000: explicit /dev/parport0 identifies a SWE1 playfield
pinball2000: LPT board PASSTHROUGH from guest I/O 0x378 to host /dev/parport0
pinball2000: physical board active — emulated board controls disabled
```

RFM prints the corresponding RFM identity. The exact first line is absent when
the signature is not recognized.

An explicit port that reads open bus or an unknown signature is intentionally
not replaced with emulation. Encore reports that raw passthrough is preserved,
and the original game can display its own board/cable diagnosis. That is useful
failure evidence, not a successful physical validation.

If `--game swe1` or `--game rfm` is forced, a mismatched recognized playfield
warns but the forced game remains selected. Prefer `--game auto` until the
hardware identity is established.

Quit normally with host F1 while cabinet routing is selected. After Tab has
connected the XINA keyboard, use Alt+F1 because plain F1 belongs to the guest.
Encore releases the ppdev claim during shutdown; the kernel also closes the
descriptor if the process is terminated.

## Input behavior on a real board

The default `--lpt-input physical` makes the board authoritative. Emulated
cabinet keys are blocked, XINA's AT keyboard begins unplugged, and physical
switch rows are read through DATA-direction changes. Host F1/F2/F3 remain
available while cabinet routing is selected. Tab connects the emulated AT
keyboard for guest maintenance input, not for cabinet-switch replacement;
then Alt+F1/F2/F3 invoke the host actions while the plain keys go to XINA.

After the real board path is proven, this experimental form can add keyboard
closures for diagnosis:

```bash
scripts/run-qemu.sh \
  --game auto \
  --lpt-device /dev/parport0 \
  --lpt-input hybrid \
  --no-savedata
```

Hybrid mode combines keyboard closures with physical active-low switch input
by clearing additional bits. A keyboard closure cannot reopen a switch that
the board already reports closed, and F4 cannot override the physical
coin-door interlock. It does not virtualize or suppress outputs: lamp/coil
writes and keepalive remain physical. Never treat `hybrid` as a hardware-safety
mode. There is no hot switch back to a software board; restart Encore with an
emulated policy instead of interrupting physical traffic mid-session.

F12 prints Encore's local door/control/lamp/switch state. Pure physical traffic
bypasses the software model's protocol counters, and F12 does not poll the
board, so this dump is not proof of physical row state. `--lpt-trace FILE` adds
every port access with a wall timestamp, but the resulting synchronous I/O and
log volume perturb timing; use it only for a short investigation.

## Acceptance sequence

Separate transport proof from cabinet proof:

1. Confirm `/dev/parportN` is a ppdev character node and accessible to the
   unprivileged runtime user.
2. Run `--preflight` and confirm it stops without probing hardware.
3. On the same host, run the intended game/update through the emulated board
   and its normal benchmark to establish a software/timing baseline.
4. Place the cabinet in the documented safe service state.
5. Launch one explicit port with `--game auto --no-savedata`.
6. Save the complete log and confirm explicit identity, passthrough and physical
   mode messages; reject any emulated fallback.
7. Verify the original game's board diagnostics before enabling normal play.
8. Exercise one input and one output at a time under the cabinet service
   procedure; record board revision, cable, host controller and kernel.
9. Quit normally, then confirm another process can claim the port.
10. Only after this passes, install `required` as the cabinet policy and perform
   the normal full user run from [Testing and validation](26-testing-validation-matrix.md).

This procedure cannot define electrically safe output choices for an unknown
cabinet. That must come from the specific board/manual and a person able to
observe the mechanism.

## Kernel-path test without a board

`tools/test-disconnected-vport.sh` is a laboratory fixture, not a physical
cabinet test. On a machine with no existing parallel-port configuration it:

1. refuses to run as root;
2. refuses to alter a host that already has `parport_pc` or `/dev/parportN`;
3. loads a temporary `parport_pc` at unused ISA address `0x278`, then `ppdev`;
4. gives the invoking user temporary access to `/dev/parport0`;
5. launches the normal explicit ppdev path against the open bus;
6. unloads only the modules it loaded on exit.

The ROM should diagnose the disconnected driver board. This proves open,
`PPCLAIM`, SPP ioctls, raw open-bus propagation and guest diagnosis through the
real kernel API. It does not prove wiring, voltages, a signature or any
physical output.

The Debian regression laboratory creates the same class of real ppdev/open-bus
path inside a disposable VM at ISA `0x378`; it additionally checks installer
group policy and lifecycle. Neither fixture substitutes for powered hardware.

> [!CAUTION]
> The standalone helper loads and unloads host kernel modules. Read it first
> and use it only on a dedicated test host with no existing parallel-port
> configuration. Encore's software `--lpt-device disconnected` is the
> zero-kernel-change choice for ordinary failure-path testing.

## Troubleshooting by boundary

| Symptom | Boundary to inspect |
|---|---|
| no `/dev/parportN` | host controller, kernel driver and `ppdev`; Encore has not reached hardware |
| node exists but preflight access fails | udev group/mode, `lp` membership and session re-entry |
| `cannot open/claim` | permissions or another claimant; explicit mode never falls back |
| auto boots but logs software board | no recognized physical signature; not a passthrough success |
| explicit path says unrecognized signature | cable, power, direction support or wrong/non-Pinball device; raw path remains active |
| recognized board but wrong forced game | rerun with `--game auto` and verify physical playfield identity |
| keyboard cabinet keys do nothing | expected under physical-only input; use the board or deliberate hybrid mode |
| timing changes under trace | remove `--lpt-trace` and verbose diagnostics before measuring |

For general launch failures see [Troubleshooting](04-troubleshooting.md). For
guest port edges, opcodes, switch algebra and counters, return to
[LPT driver board](26-lpt-board.md).

---

← [LPT driver board](26-lpt-board.md) ·
[Documentation index](README.md) · [Testing and validation](26-testing-validation-matrix.md)
