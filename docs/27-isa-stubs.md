# 27 — ISA Stubs

This doc covers the minimal ISA and SuperIO stubs implemented by `qemu/p2k-isa-stubs.c` and `qemu/p2k-superio.c`.  These devices replace just enough of a PC southbridge for PRISM/XINU boot paths without switching the custom machine to QEMU's full `pc` model.

## Installed ISA regions

`p2k_install_isa_stubs()` maps small I/O regions with `memory_region_init_io()` over `get_system_io()` (`qemu/p2k-isa-stubs.c:465-475`, `qemu/p2k-isa-stubs.c:477-528`).

> [!NOTE]
> These stubs replace just enough of a PC southbridge for PRISM/XINU boot. They are minimal: keyboard-controller probes, CMOS time, POST codes, and COM1/COM2 diagnostics. No full ISA bus model exists.

| Port(s) | Device | R/W | Meaning |
|---:|---|---|---|
| `0x60` | i8042 data | R/W | Read returns output buffer and clears OBF; writes ignored because there is no real PS/2 device (`qemu/p2k-isa-stubs.c:35-63`, `qemu/p2k-isa-stubs.c:65-79`). |
| `0x64` | i8042 status/cmd | R/W | Status starts `0x14`; command writes set canned responses and OBF (`qemu/p2k-isa-stubs.c:43-50`, `qemu/p2k-isa-stubs.c:65-77`). |
| `0x61` | System Control Port B | R/W | Bit 4 toggles on every read; low control bits are stored (`qemu/p2k-isa-stubs.c:88-112`). |
| `0x70..0x71` | CMOS/RTC | R/W | Index/data pair; time registers are synthesized in BCD from host local time, other bytes are stored zeros/defaults (`qemu/p2k-isa-stubs.c:114-162`). |
| `0x80` | POST port | R/W | Stores last POST code; read returns it (`qemu/p2k-isa-stubs.c:164-184`). |
| `0x2F8..0x2FF` | COM2 16550 | R/W | Stateful probe-compatible UART registers and internal loopback; no host chardev bridge. |
| `0x3F8..0x3FF` | COM1 16550 | R/W | Line-buffered TX to stderr/chardev, optional RX from chardev/env, IRQ4 pulses if wired (`qemu/p2k-isa-stubs.c:186-232`, `qemu/p2k-isa-stubs.c:320-463`). |

> [!IMPORTANT]
> Host keystrokes are not consumed through this PS/2/i8042 stub.  Gameplay keys go through the LPT board's QEMU input handler; the i8042 ports only answer controller probes (`qemu/p2k-isa-stubs.c:35-50`, `qemu/p2k-lpt-board.c:449-555`).

## i8042 keyboard controller stub

| Port | Access | Value / behavior |
|---:|---|---|
| `0x60` | read | Return `s_kbc_outbuf`; clear status bit 0 OBF (`qemu/p2k-isa-stubs.c:55-61`). |
| `0x60` | write | Ignored; no PS/2 command/data consumer exists (`qemu/p2k-isa-stubs.c:78-79`). |
| `0x64` | read | Return `s_kbc_status`, initially `0x14` (`qemu/p2k-isa-stubs.c:43-53`, `qemu/p2k-isa-stubs.c:62`). |
| `0x64` | write `0xAA` | Set output `0x55` self-test OK (`qemu/p2k-isa-stubs.c:69-72`). |
| `0x64` | write `0xAB` | Set output `0x00` interface-test OK (`qemu/p2k-isa-stubs.c:71-73`). |
| `0x64` | write `0x20` | Set output `0x45` CCB (`qemu/p2k-isa-stubs.c:73`). |
| `0x64` | any command write | Assert OBF plus self-test-passed status `0x15` (`qemu/p2k-isa-stubs.c:74-77`). |

The phrase “always idle” is true from the guest-input perspective: no host keyboard stream is queued here.  The controller does return canned probe responses so BIOS/XINU polling loops can complete.

## CMOS / RTC

The CMOS index byte is written at offset 0, and data is read/written at offset 1 (`qemu/p2k-isa-stubs.c:116-123`, `qemu/p2k-isa-stubs.c:147-154`).  Registers `0x00..0x09` and `0x32` are refreshed from host local time in BCD; status A is `0x26`, status B is `0x02`, and status D is `0x80` battery-valid (`qemu/p2k-isa-stubs.c:123-142`).

## COM1/COM2 16550 stubs

Both UARTs retain the register state exercised by the XINA hardware probes. COM1 additionally owns the host console bridge; COM2 remains a polled guest-visible device.

| Offset | Register | R/W | Meaning |
|---:|---|---|---|
| 0 | RBR/THR/DLL | R/W | RBR pops chardev/env RX or returns 0; THR writes enter line filter; DLL when DLAB set (`qemu/p2k-isa-stubs.c:320-337`, `qemu/p2k-isa-stubs.c:424-436`). |
| 1 | IER/DLM | R/W | IER stored when DLAB clear; controls RX/THRE IRQ pulses (`qemu/p2k-isa-stubs.c:338`, `qemu/p2k-isa-stubs.c:438-445`). |
| 2 | IIR/FCR | R/W | Reports RX data, THR empty, or no pending, including FIFO-present bits; FCR stores FIFO-enable/reset state. |
| 3 | LCR | R/W | Stored; bit 7 is DLAB (`qemu/p2k-isa-stubs.c:350`, `qemu/p2k-isa-stubs.c:447-449`). |
| 4 | MCR | R/W | Stores modem outputs and enables standard 16550 internal loopback with bit 4. |
| 5 | LSR | R | `0x60` THRE+TEMT, plus bit 0 if RX data exists (`qemu/p2k-isa-stubs.c:188-202`, `qemu/p2k-isa-stubs.c:352`). |
| 6 | MSR | R | In internal loopback, reflects RTS/DTR/OUT1/OUT2 as CTS/DSR/RI/DCD; otherwise returns 0. |
| 7 | SCR | R/W | Scratch byte (`qemu/p2k-isa-stubs.c:354`, `qemu/p2k-isa-stubs.c:450-452`). |

TX goes to stderr when enabled and to the first QEMU serial chardev if one is bound (`qemu/p2k-isa-stubs.c:397-406`, `qemu/p2k-isa-stubs.c:481-516`).  RX comes first from the chardev ring, then from `P2K_UART_INPUT` as a one-shot fallback (`qemu/p2k-isa-stubs.c:225-232`, `qemu/p2k-isa-stubs.c:288-318`, `qemu/p2k-isa-stubs.c:320-337`).

THR writes made while MCR loopback is active return through that UART's RBR and set LSR.DR. This is sufficient for both XINA `tty0` and `tty1` startup probes without putting COM2 on a host hot path.

## UART TX filter

UART output is line-buffered.  When a newline or 1024-byte buffer limit arrives, the full line is dropped if it contains any configured substring (`qemu/p2k-isa-stubs.c:212-223`, `qemu/p2k-isa-stubs.c:384-421`).

| Variable | Default | Effect |
|---|---|---|
| `P2K_UART_DROP` | `swd Debug:` | Tab- or newline-separated substrings; matching lines are suppressed (`qemu/p2k-isa-stubs.c:359-380`). |
| `P2K_UART_DROP=""` | explicit empty | Disable all TX line filtering (`qemu/p2k-isa-stubs.c:363-365`). |
| `P2K_NO_UART_STDERR` | unset | If set nonzero, disables stderr mirror (`qemu/p2k-isa-stubs.c:481-494`). |
| `P2K_UART_TO_STDERR` | fallback | Explicitly enables stderr mirror if the `NO` variable did not disable it (`qemu/p2k-isa-stubs.c:486-493`). |
| `P2K_UART_INPUT` | unset | Supplies canned RX bytes such as `continue\r\n` when no chardev byte is waiting (`qemu/p2k-isa-stubs.c:288-292`, `qemu/p2k-isa-stubs.c:330-335`). |

This is the low-level mechanism behind the wrapper's `--uart-drop` and `--uart-no-filter` behavior referenced from [04 — Troubleshooting](04-troubleshooting.md).

> [!TIP]
> Use `P2K_UART_DROP=""` to disable all TX line filtering and see raw guest console output. Default filter suppresses "swd Debug:" spam. Set `P2K_UART_INPUT` to inject canned RX bytes like `continue\r\n` for automated debugging.

## SuperIO and CC5530 config stubs

`p2k-superio.c` maps two config pairs, but deliberately does not overlap port `0x61` because another PC-HW/i8254 provider already owns it in this QEMU branch (`qemu/p2k-superio.c:1-14`, `qemu/p2k-superio.c:100-120`).

| Port(s) | Device | R/W | Meaning |
|---:|---|---|---|
| `0x2E/0x2F` | Winbond W83977EF SuperIO config | R/W | Offset 0 stores index; data read at index `0x20` returns chip ID `0x97`; other data reads return 0 (`qemu/p2k-superio.c:23-45`, `qemu/p2k-superio.c:104-108`). |
| `0xEA/0xEB` | Cyrix CC5530 config | R/W | Offset 0 stores index; data index `0x20` returns chip ID `0x02`, `0x21` returns revision `0x01` (`qemu/p2k-superio.c:23-24`, `qemu/p2k-superio.c:54-72`, `qemu/p2k-superio.c:109-112`). |
| `0x61` | SuperIO-local toggler | not installed | Code exists but is intentionally not registered to avoid corrupting timer-calibration reads (`qemu/p2k-superio.c:81-98`, `qemu/p2k-superio.c:114-117`). |


## IRQ behavior

The UART IRQ line is supplied later by `p2k_isa_set_uart_irq()` (`qemu/p2k-isa-stubs.c:272-275`).

TX holding-empty interrupts are edge pulses when IER bit 1 is enabled (`qemu/p2k-isa-stubs.c:277-286`).

RX data interrupts pulse when chardev bytes arrive and IER bit 0 is enabled (`qemu/p2k-isa-stubs.c:255-269`).

If no IRQ line has been wired, the UART still works as a polled device.

## Chardev binding

The COM1 stub binds to QEMU's first serial backend with `serial_hd(0)` (`qemu/p2k-isa-stubs.c:498-516`).

That makes `-serial stdio`, TCP serial, file serial, or null serial a launch-time choice outside the device model.

When no chardev exists, stderr mirroring can still expose guest TX output (`qemu/p2k-isa-stubs.c:397-406`, `qemu/p2k-isa-stubs.c:481-494`).

> [!NOTE]
> COM1 can operate without a QEMU chardev. TX output mirrors to stderr by default. Use `-serial stdio`, `-serial tcp:...`, or `-serial file:...` to bind a real backend if interactive RX is needed.

## Duplicate port 0x61 note

Both `p2k-isa-stubs.c` and `p2k-superio.c` contain port-0x61 helper code (`qemu/p2k-isa-stubs.c:88-112`, `qemu/p2k-superio.c:81-98`).

Only the ISA stubs installer maps `0x61` in this file set (`qemu/p2k-isa-stubs.c:518-523`).

The SuperIO installer explicitly avoids registering its port-0x61 region because another timer/control implementation already exists in the QEMU machine (`qemu/p2k-superio.c:114-117`).

## Minimalism rule

These stubs are not full device models.

They answer the exact probes and hot loops PRISM/XINU uses: keyboard controller self-test, CMOS time, POST byte storage, SuperIO chip IDs, CC5530 IDs, and COM1/COM2 diagnostics.

If future firmware paths need more behavior, add it as another narrow I/O block rather than replacing the custom machine with generic PC hardware.

> [!IMPORTANT]
> These are intentionally minimal stubs, not full device models. They answer only the probes PRISM/XINU firmware uses. Do not expect full PC/AT compatibility; expect boot-path compatibility.


## See also

- [04 — Troubleshooting](04-troubleshooting.md)
- [11 — Machine init](11-machine-init.md)
- [12 — CPU and timers](12-cpu-and-timers.md)
- [20 — PLX / PCI configuration](20-plx-pci.md)
- [26 — LPT board](26-lpt-board.md)
