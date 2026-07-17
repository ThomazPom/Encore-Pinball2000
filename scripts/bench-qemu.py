#!/usr/bin/env python3
"""Run Encore's non-invasive guest IRQ and LPT/PDB self-diagnostic."""

from __future__ import annotations

import json
import os
import re
import shutil
import signal
import socket
import statistics
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
RUNNER = ROOT / "scripts" / "run-qemu.sh"
PROBE_SOURCE = ROOT / "scripts" / "guest-irq-probe.S"
LOAD_SOURCE = ROOT / "scripts" / "guest-load.S"
EXPECTED_IRQ_HZ = 1193182.0 / 298.0
DATA_ADDR = 0x00FE0000
CODE_ADDR = 0x00FF0000
RING_ENTRIES = 8192
CLKINT_SIGNATURE = bytes.fromhex("fa60b020e620")
WARMUP_MARKER = b"__P2K_BENCH_WARM__"
DONE_MARKER = b"__P2K_BENCH_DONE__"


def take_internal_options(arguments: list[str]) -> tuple[list[str], bool]:
    guest_load = "--bench-guest-load" in arguments
    return [arg for arg in arguments if arg != "--bench-guest-load"], guest_load


def field(line: str, name: str, default: str = "n/a") -> str:
    match = re.search(rf"(?:^|[ |]){re.escape(name)}=([^ |]+)", line)
    return match.group(1) if match else default


def numeric_field(line: str, name: str) -> float:
    value = field(line, name, "")
    match = re.match(r"[-+]?[0-9]+(?:\.[0-9]+)?", value)
    if not match:
        raise ValueError(f"missing {name}= in {line}")
    return float(match.group())


def pick_port() -> int:
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def requested_speed(arguments: list[str]) -> float:
    for index, argument in enumerate(arguments):
        if argument == "--speed-target" and index + 1 < len(arguments):
            return float(arguments[index + 1])
    return 100.0


def connect_console(port: int, process: subprocess.Popen, timeout: float) -> socket.socket:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"Encore exited before XINU became ready ({process.returncode})")
        try:
            sock = socket.create_connection(("127.0.0.1", port), timeout=1.0)
            sock.settimeout(1.0)
            return sock
        except OSError:
            time.sleep(0.2)
    raise RuntimeError("timed out connecting to Encore's XINU console")


def wait_for(sock: socket.socket, token: bytes, timeout: float,
             wake: bool = False) -> bytes:
    deadline = time.monotonic() + timeout
    received = bytearray()
    next_wake = 0.0
    while time.monotonic() < deadline:
        if wake and time.monotonic() >= next_wake:
            sock.sendall(b"\r")
            next_wake = time.monotonic() + 1.0
        try:
            chunk = sock.recv(65536)
        except socket.timeout:
            continue
        if not chunk:
            raise RuntimeError("XINU console disconnected")
        received.extend(chunk)
        if token in received:
            return bytes(received)
    raise RuntimeError(f"timed out waiting for XINU response {token!r}")


def wait_port(port: int, process: subprocess.Popen, timeout: float = 30.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"Encore exited before GDB became ready ({process.returncode})")
        with socket.socket() as sock:
            sock.settimeout(0.1)
            if sock.connect_ex(("127.0.0.1", port)) == 0:
                return
        time.sleep(0.05)
    raise RuntimeError(f"GDB port {port} did not become ready")


def gdb(port: int, commands: list[str]) -> str:
    argv = ["gdb", "-q", "-batch", "-ex", f"target remote 127.0.0.1:{port}"]
    for command in commands:
        argv.extend(("-ex", command))
    result = subprocess.run(argv, check=True, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    return result.stdout


def build_probe(output: Path) -> bytes:
    obj = output / "guest-irq-probe.o"
    elf = output / "guest-irq-probe.elf"
    raw = output / "guest-irq-probe-template.bin"
    subprocess.run(["as", "--32", "-o", str(obj), str(PROBE_SOURCE)], check=True)
    subprocess.run(["ld", "-m", "elf_i386", f"-Ttext=0x{CODE_ADDR:x}",
                    "-o", str(elf), str(obj)], check=True)
    subprocess.run(["objcopy", "-O", "binary", "-j", ".text",
                    str(elf), str(raw)], check=True)
    return raw.read_bytes()


def update_token(arguments: list[str]) -> str:
    for index, argument in enumerate(arguments):
        if argument == "--update" and index + 1 < len(arguments):
            return arguments[index + 1].lstrip("0") or "0"
    return "210"


def load_symbols(arguments: list[str]) -> tuple[int, int, int, int]:
    token = update_token(arguments)
    candidates = sorted(ROOT.glob(f"updates/*_{int(token):04d}_*/*/*_symbols.rom"))
    if not candidates:
        raise RuntimeError(f"guest load needs a symbol ROM for update {token}")
    sys.path.insert(0, str(ROOT / "tools"))
    sys.dont_write_bytecode = True
    import sym_dump  # type: ignore
    data, count, base, by_no, _ = sym_dump.parse(str(candidates[-1]))
    values = []
    for name in ("create(void *, int, unsigned int, char *, int,...)",
                 "resume(int, Bool)", "resched(void)", "announce(void)"):
        address = sym_dump.lookup(data, count, base, by_no, name)
        if address is None:
            raise RuntimeError(f"update {token} has no symbol for {name}")
        values.append(address)
    return values[0], values[1], values[2], values[3] + 0x13A


def build_guest_load(output: Path, arguments: list[str], port: int) -> None:
    create, resume, resched, idle = load_symbols(arguments)
    original_path = output / "idle-original.bin"
    gdb(port, [f"dump binary memory {original_path} 0x{idle:08x} 0x{idle + 5:08x}",
               "detach"])
    original = original_path.read_bytes()
    if original[:2] != b"\xeb\xfe":
        raise RuntimeError(f"XINU idle loop at 0x{idle:08x} is {original.hex()}, expected ebfe")
    code_address = 0x00FD0000
    obj, elf, raw = (output / "guest-load.o", output / "guest-load.elf",
                     output / "guest-load.bin")
    defs = [f"CREATE_ADDR={create}", f"RESUME_ADDR={resume}",
            f"RESCHED_ADDR={resched}", f"IDLE_ADDR={idle}",
            f"IDLE_ORIGINAL={int.from_bytes(original[:4], 'little')}",
            f"IDLE_ORIGINAL_4={original[4]}"]
    command = ["as", "--32", "-o", str(obj)]
    for value in defs:
        command.extend(("--defsym", value))
    command.append(str(LOAD_SOURCE))
    subprocess.run(command, check=True)
    subprocess.run(["ld", "-m", "elf_i386", "-e", "load_trampoline",
                    f"-Ttext=0x{code_address:x}",
                    "-o", str(elf), str(obj)], check=True)
    subprocess.run(["objcopy", "-O", "binary", "-j", ".text", str(elf), str(raw)],
                   check=True)
    patch = b"\xe9" + struct.pack("<i", code_address - (idle + 5))
    commands = [f"restore {raw} binary 0x{code_address:08x}"]
    commands.extend(f"set {{unsigned char}}0x{idle + i:08x} = 0x{byte:02x}"
                    for i, byte in enumerate(patch))
    commands.extend(("detach",))
    gdb(port, commands)
    (output / "guest-load.json").write_text(json.dumps({
        "create": hex(create), "resume": hex(resume), "resched": hex(resched),
        "idle": hex(idle),
        "code": hex(code_address), "original": original.hex(), "patch": patch.hex(),
    }, indent=2) + "\n")


def locate_clkint(port: int, artifact: Path) -> int:
    # Ask the running CPU for IDTR rather than searching RAM for something
    # that merely resembles an IDT. XINU updates do not all align IDTR to an
    # eight-byte address, and several legitimate routines share clkint's six
    # prologue bytes. Vector 0x20 in the active table is unambiguous.
    registers = gdb(port, ["monitor info registers", "detach"])
    match = re.search(r"^IDT=\s*([0-9a-fA-F]+)\s+([0-9a-fA-F]+)",
                      registers, re.MULTILINE)
    if not match:
        raise RuntimeError("QEMU did not report the active IDT register")
    idt_base = int(match.group(1), 16)
    idt_limit = int(match.group(2), 16)
    vector_offset = 0x20 * 8
    if idt_limit < vector_offset + 7:
        raise RuntimeError(f"active IDT is too short for IRQ0: limit=0x{idt_limit:x}")

    gate_path = artifact / "irq0-gate.bin"
    gate_address = idt_base + vector_offset
    gdb(port, [f"dump binary memory {gate_path} 0x{gate_address:08x} "
               f"0x{gate_address + 8:08x}", "detach"])
    descriptor = gate_path.read_bytes()
    if len(descriptor) != 8:
        raise RuntimeError("could not read the active IRQ0 gate")
    selector = struct.unpack_from("<H", descriptor, 2)[0]
    attributes = descriptor[5]
    handler = (struct.unpack_from("<H", descriptor)[0] |
               struct.unpack_from("<H", descriptor, 6)[0] << 16)
    if not attributes & 0x80 or attributes & 0x0F not in (0x0E, 0x0F):
        raise RuntimeError(f"active IRQ0 descriptor is not a present x86 gate: "
                           f"{descriptor.hex()}")

    prologue = artifact / "irq0-prologue.bin"
    gdb(port, [f"dump binary memory {prologue} 0x{handler:08x} "
               f"0x{handler + len(CLKINT_SIGNATURE):08x}", "detach"])
    actual = prologue.read_bytes()
    if actual != CLKINT_SIGNATURE:
        raise RuntimeError(f"active IRQ0 handler 0x{handler:08x} has unknown "
                           f"prologue {actual.hex()}")
    (artifact / "idt.json").write_text(json.dumps({
        "irq0_handler": f"0x{handler:08x}",
        "idt_base": f"0x{idt_base:08x}", "idt_limit": f"0x{idt_limit:04x}",
        "selector": f"0x{selector:04x}", "descriptor": descriptor.hex(),
    }, indent=2) + "\n")
    return handler


def install_probe(port: int, artifact: Path, template: bytes) -> tuple[int, bytes]:
    handler = locate_clkint(port, artifact)
    original = CLKINT_SIGNATURE
    stub = bytearray(template)
    if stub[-5] != 0xE9:
        raise RuntimeError("probe template has no final JMP")
    struct.pack_into("<i", stub, len(stub) - 4,
                     handler + len(original) - (CODE_ADDR + len(stub)))
    stub_path = artifact / "guest-irq-probe.bin"
    stub_path.write_bytes(stub)
    patch = b"\xe9" + struct.pack("<i", CODE_ADDR - (handler + 5)) + b"\x90"
    commands = [
        f"restore {stub_path} binary 0x{CODE_ADDR:08x}",
        f"set {{unsigned int}}0x{DATA_ADDR:08x} = 0",
        f"set {{unsigned int}}0x{DATA_ADDR + 4:08x} = 0",
        f"set {{unsigned int}}0x{DATA_ADDR + 8:08x} = 0",
    ]
    commands.extend(f"set {{unsigned char}}0x{handler + i:08x} = 0x{byte:02x}"
                    for i, byte in enumerate(patch))
    commands.append("detach")
    gdb(port, commands)
    (artifact / "probe.json").write_text(json.dumps({
        "handler": f"0x{handler:08x}", "original": original.hex(),
        "patch": patch.hex(), "data": f"0x{DATA_ADDR:08x}",
        "code": f"0x{CODE_ADDR:08x}",
    }, indent=2) + "\n")
    return handler, original


def read_count(port: int, artifact: Path, name: str) -> int:
    path = artifact / f"{name}.bin"
    gdb(port, [f"dump binary memory {path} 0x{DATA_ADDR:08x} 0x{DATA_ADDR + 8:08x}",
               "detach"])
    return struct.unpack_from("<I", path.read_bytes())[0]


def arm_probe(port: int, artifact: Path) -> int:
    count = read_count(port, artifact, "irq-before-arm")
    armed = (count & ~0xF) | 0xF
    gdb(port, [
        f"set {{unsigned int}}0x{DATA_ADDR:08x} = {armed}",
        f"set {{unsigned int}}0x{DATA_ADDR + 4:08x} = 0",
        f"set {{unsigned int}}0x{DATA_ADDR + 8:08x} = 0",
        f"set {{unsigned int}}0x{DATA_ADDR + 12:08x} = 0",
        f"set {{unsigned char[{RING_ENTRIES * 4}]}}0x{DATA_ADDR + 16:08x} = {{0}}",
        "detach",
    ])
    return armed


def finish_probe(port: int, artifact: Path, handler: int,
                 original: bytes) -> tuple[int, list[int]]:
    ring = artifact / "guest-irq-ring.bin"
    commands = [f"set {{unsigned char}}0x{handler + i:08x} = 0x{byte:02x}"
                for i, byte in enumerate(original)]
    commands += [
        f"dump binary memory {ring} 0x{DATA_ADDR:08x} "
        f"0x{DATA_ADDR + 16 + RING_ENTRIES * 4:08x}",
        "detach",
    ]
    gdb(port, commands)
    data = ring.read_bytes()
    count = struct.unpack_from("<I", data)[0]
    values = struct.unpack_from(f"<{RING_ENTRIES}I", data, 16)
    return count, [value for value in values if value]


def probe_stats(start_count: int, end_count: int, elapsed: float,
                cycles: list[int], target_speed: float) -> dict[str, float | int]:
    delivered = (end_count - start_count) & 0xFFFFFFFF
    if delivered < 100 or len(cycles) < 100:
        raise RuntimeError(f"too few IRQ samples: delivered={delivered}, ring={len(cycles)}")
    rate = delivered / elapsed
    mean_us = 1.0e6 / rate
    raw_mean = statistics.fmean(cycles)
    scaled = sorted(value * mean_us / raw_mean for value in cycles)

    def percentile(q: float) -> float:
        return scaled[int((len(scaled) - 1) * q)]

    return {
        "samples": delivered, "ring_samples": len(scaled), "rate": rate,
        "delivery": 100.0 * rate / (EXPECTED_IRQ_HZ * target_speed / 100.0),
        "mean": mean_us, "stddev": statistics.pstdev(scaled),
        "p50": percentile(0.50), "p95": percentile(0.95),
        "p99": percentile(0.99), "worst": scaled[-1], "elapsed": elapsed,
    }


def monitor_workload(path: Path) -> None:
    deadline = time.monotonic() + 10.0
    while not path.exists() and time.monotonic() < deadline:
        time.sleep(0.05)
    if not path.exists():
        raise RuntimeError("QEMU monitor socket did not appear")
    commands = ["sendkey f4"] + ["sendkey c"] * 3
    commands += [f"sendkey {'up' if index % 2 == 0 else 'down'}" for index in range(20)]
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
        sock.settimeout(2.0)
        sock.connect(str(path))
        try:
            sock.recv(4096)
        except socket.timeout:
            pass
        for index, command in enumerate(commands):
            sock.sendall((command + "\n").encode())
            time.sleep(0.5 if index == 0 else 0.3 if index <= 3 else 0.1)


def stop_process(process: subprocess.Popen) -> None:
    if process.poll() is not None:
        return
    os.killpg(process.pid, signal.SIGTERM)
    try:
        process.wait(timeout=8)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait()


def launch_command(forwarded: list[str], port: int, monitor: Path,
                   extra: list[str]) -> list[str]:
    return [
        "bash", str(RUNNER), *forwarded, "--no-savedata", "--uart-quiet",
        "--uart-tcp", f"127.0.0.1:{port}",
        "--monitor", f"unix:{monitor},server=on,wait=off", *extra,
    ]


def warm_guest(console: socket.socket, target_speed: float, monitor: Path) -> None:
    wait_for(console, b"%", 45.0, wake=True)
    # Apply the cabinet workload before warmup. Its key sequence deliberately
    # creates short scheduling/audio disturbances; the measured phase must
    # begin only after those have drained, not in a snapshot crossing them.
    monitor_workload(monitor)
    console.sendall(b"sleep 30\recho __P2K_BENCH_WARM__\r")
    wait_for(console, WARMUP_MARKER, max(120.0, 45.0 * 100.0 / target_speed))


def run_irq_pass(forwarded: list[str], artifact: Path, template: bytes,
                 target_speed: float, guest_load: bool) -> tuple[dict, float]:
    port, gdb_port = pick_port(), pick_port()
    monitor = artifact / "monitor.sock"
    log_path = artifact / "encore.log"
    command = launch_command(forwarded, port, monitor,
                             ["--", "-gdb", f"tcp:127.0.0.1:{gdb_port}"])
    (artifact / "command.json").write_text(json.dumps(command, indent=2) + "\n")
    with log_path.open("wb") as log:
        process = subprocess.Popen(command, cwd=ROOT, stdin=subprocess.DEVNULL,
                                   stdout=log, stderr=subprocess.STDOUT,
                                   start_new_session=True)
        handler = None
        original = b""
        try:
            wait_port(gdb_port, process)
            # A readiness connection can leave some GDB-stub versions in a
            # stopped state after the peer closes without a detach packet.
            # Perform one real attach/detach before waiting for XINU so boot
            # is explicitly resumed and the probe's later pauses are known.
            gdb(gdb_port, ["detach"])
            with connect_console(port, process, 30.0) as console:
                warm_guest(console, target_speed, monitor)
                if guest_load:
                    build_guest_load(artifact, forwarded, gdb_port)
                    time.sleep(1.0)
                handler, original = install_probe(gdb_port, artifact, template)
                # First attachment perturbs pacing. Resume, settle, then clear
                # and re-arm so no GDB pause is included in the sample window.
                arm_probe(gdb_port, artifact)
                time.sleep(1.0)
                start_count = arm_probe(gdb_port, artifact)
                started = time.monotonic()
                console.sendall(b"sleep 10\recho __P2K_BENCH_DONE__\r")
                wait_for(console, DONE_MARKER,
                         max(45.0, 15.0 * 100.0 / target_speed))
                stopped = time.monotonic()
                end_count, cycles = finish_probe(gdb_port, artifact, handler, original)
                handler = None
                elapsed = stopped - started
                return probe_stats(start_count, end_count, elapsed, cycles,
                                   target_speed), elapsed
        finally:
            if handler is not None and process.poll() is None:
                try:
                    finish_probe(gdb_port, artifact, handler, original)
                except Exception:
                    pass
            stop_process(process)
            monitor.unlink(missing_ok=True)


def run_lpt_pass(forwarded: list[str], artifact: Path,
                 target_speed: float, guest_load: bool) -> tuple[list[str], list[str], float]:
    port = pick_port()
    monitor = artifact / "monitor.sock"
    log_path = artifact / "encore.log"
    gdb_port = pick_port()
    extra = ["-v"]
    if guest_load:
        extra += ["--", "-gdb", f"tcp:127.0.0.1:{gdb_port}"]
    command = launch_command(forwarded, port, monitor, extra)
    (artifact / "command.json").write_text(json.dumps(command, indent=2) + "\n")
    with log_path.open("wb") as log:
        launched = time.monotonic()
        process = subprocess.Popen(command, cwd=ROOT, stdin=subprocess.DEVNULL,
                                   stdout=log, stderr=subprocess.STDOUT,
                                   start_new_session=True)
        try:
            if guest_load:
                wait_port(gdb_port, process)
                gdb(gdb_port, ["detach"])
            with connect_console(port, process, 30.0) as console:
                warm_guest(console, target_speed, monitor)
                if guest_load:
                    build_guest_load(artifact, forwarded, gdb_port)
                    time.sleep(1.0)
                boot_wall = time.monotonic() - launched
                boot_lines = log_path.read_text(errors="replace").splitlines()
                console.sendall(b"sleep 10\recho __P2K_BENCH_DONE__\r")
                wait_for(console, DONE_MARKER,
                         max(45.0, 15.0 * 100.0 / target_speed))
                time.sleep(6.2)
                lines = log_path.read_text(errors="replace").splitlines()
                return boot_lines, lines[len(boot_lines):], boot_wall
        finally:
            stop_process(process)
            monitor.unlink(missing_ok=True)


def parse_lpt(lines: list[str]) -> dict[str, float]:
    data_rates: list[float] = []
    pdb: list[dict[str, float]] = []
    pdb_counts: list[tuple[float, int]] = []
    current_wall: float | None = None
    for line in lines:
        if "p2k-timing #" in line and " snap |" in line:
            current_wall = numeric_field(line, "wall")
        elif "p2k-lpt-hz snap" in line:
            match = re.search(r"data=\d+ \(\+([0-9.]+)/s\)", line)
            if match:
                data_rates.append(float(match.group(1)))
        elif "p2k-pdb05 snap | pdb05_wall_delta" in line:
            pdb.append({name: numeric_field(line, name)
                        for name in ("p50", "p95", "p99", "max_total")})
        elif "p2k-latency snap" in line and current_wall is not None:
            match = re.search(r"pdb05 count=(\d+)", line)
            if match:
                pdb_counts.append((current_wall, int(match.group(1))))
    rates = [(b_count - a_count) / (b_wall - a_wall)
             for (a_wall, a_count), (b_wall, b_count)
             in zip(pdb_counts, pdb_counts[1:]) if b_wall > a_wall]
    if not data_rates or not pdb or not rates:
        raise RuntimeError("steady LPT/PDB snapshots missing")
    return {
        "data_rate": statistics.fmean(data_rates),
        "rate": statistics.fmean(rates),
        "p50": statistics.fmean(item["p50"] for item in pdb),
        "p95": statistics.fmean(item["p95"] for item in pdb),
        "p99": statistics.fmean(item["p99"] for item in pdb),
        "worst": max(item["max_total"] for item in pdb),
    }


def parse_boot(lines: list[str]) -> dict[str, str]:
    timing = next((line for line in reversed(lines)
                   if "p2k-timing #" in line and " snap |" in line), "")
    lpt = next((line for line in reversed(lines) if "p2k-lpt-hz snap" in line), "")
    data = re.search(r"data=\d+ \(\+([0-9.]+)/s\)", lpt)
    hotloop_worst = []
    pdb_worst = []
    for line in lines:
        if "p2k-clkint-hotloop snap" in line:
            value = field(line, "max_us", "")
            if value.isdigit():
                hotloop_worst.append(int(value))
        if "p2k-pdb05 snap" in line and "pdb05_wall_total" in line:
            try:
                pdb_worst.append(int(numeric_field(line, "max_total")))
            except ValueError:
                pass
    return {
        "delivery": field(timing, "delivery"),
        "current_delivery": field(timing, "current_delivery"),
        "data_rate": data.group(1) if data else "n/a",
        "irq_worst": str(max(hotloop_worst)) if hotloop_worst else "n/a",
        "pdb_worst": str(max(pdb_worst)) if pdb_worst else "n/a",
    }


def fmt_us(value: float) -> str:
    return f"{value / 1000:.2f}ms" if value >= 1000 else f"{value:.0f}us"


def with_unit(value: str, unit: str) -> str:
    return value if value == "n/a" else value + unit


def print_irq_preview(irq: dict, sleep_wall: float) -> None:
    effective = 1000.0 / sleep_wall
    print("[bench] clkint preview:", flush=True)
    print(f"  XINU sleep 10 wall:    {sleep_wall:.3f}s "
          f"({effective:.2f}% real-time)", flush=True)
    print(f"  Guest IRQ delivery:    {irq['delivery']:.2f}%", flush=True)
    print(f"  Guest IRQ rate:        {irq['rate']:.1f}/s "
          f"({irq['samples']} entries)", flush=True)
    print(f"  Guest IRQ intervals:   mean={fmt_us(irq['mean'])} "
          f"sigma={fmt_us(irq['stddev'])} p50={fmt_us(irq['p50'])} "
          f"p95={fmt_us(irq['p95'])} p99={fmt_us(irq['p99'])} "
          f"worst={fmt_us(irq['worst'])}", flush=True)


def write_report(artifact: Path, result: dict) -> None:
    irq, lpt, boot = result["irq"], result["lpt"], result["boot"]
    report = [
        "# Encore self-diagnostic",
        "",
        "> [!NOTE]",
        "> IRQ timing comes from a temporary guest-RAM `clkint` probe. LPT and "
        "PDB05 timing comes from a separate unpatched run.",
        "> Guest CPU load: " +
        ("cooperative low-priority worker." if result["guest_load"]
         else "normal game workload."),
        "",
        "| Phase | Speed/delivery | IRQ rate | IRQ sigma | IRQ p99 | IRQ worst | DATA/s | PDB05/s | PDB p99 | PDB worst |",
        "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|",
        f"| Boot/warmup | {boot['current_delivery']} | — | — | — | "
        f"{with_unit(boot['irq_worst'], 'us')} | {boot['data_rate']} | — | — | "
        f"{with_unit(boot['pdb_worst'], 'us')} |",
        f"| Steady state | {irq['delivery']:.2f}% | {irq['rate']:.1f} | "
        f"{fmt_us(irq['stddev'])} | {fmt_us(irq['p99'])} | "
        f"{fmt_us(irq['worst'])} | {lpt['data_rate']:.0f} | {lpt['rate']:.0f} | "
        f"{fmt_us(lpt['p99'])} | {fmt_us(lpt['worst'])} |",
        "",
        f"XINU `sleep 10`: {result['sleep_wall']:.3f}s wall "
        f"({result['effective_speed']:.2f}% real-time).",
    ]
    (artifact / "report.md").write_text("\n".join(report) + "\n")


def main() -> int:
    forwarded, guest_load = take_internal_options(sys.argv[1:])
    target_speed = requested_speed(forwarded)
    artifact = Path(tempfile.mkdtemp(prefix="p2k-bench-"))
    irq_dir, lpt_dir = artifact / "irq", artifact / "lpt"
    irq_dir.mkdir()
    lpt_dir.mkdir()
    print(f"[bench] artifact={artifact}", flush=True)
    missing = [tool for tool in ("gdb", "as", "ld", "objcopy")
               if shutil.which(tool) is None]
    if missing:
        print(f"[bench] ERROR: required tools missing: {', '.join(missing)}",
              file=sys.stderr)
        return 1
    print("[bench] pass 1/2: guest-RAM clkint probe", flush=True)
    try:
        template = build_probe(irq_dir)
        irq, sleep_wall = run_irq_pass(forwarded, irq_dir, template, target_speed,
                                       guest_load)
        print_irq_preview(irq, sleep_wall)
        print("[bench] pass 2/2: unpatched LPT/PDB measurement", flush=True)
        boot_lines, steady_lines, boot_wall = run_lpt_pass(
            forwarded, lpt_dir, target_speed, guest_load)
        lpt = parse_lpt(steady_lines)
        boot = parse_boot(boot_lines)
    except Exception as error:
        print(f"[bench] ERROR: {error}", file=sys.stderr)
        print(f"[bench] artifacts={artifact}", file=sys.stderr)
        return 1

    effective = 1000.0 / sleep_wall
    result = {
        "requested_speed": target_speed, "guest_load": guest_load,
        "sleep_wall": sleep_wall,
        "effective_speed": effective, "boot_wall": boot_wall,
        "irq": irq, "lpt": lpt, "boot": boot,
    }
    (artifact / "results.json").write_text(json.dumps(result, indent=2) + "\n")
    (artifact / "metadata.json").write_text(json.dumps({
        "arguments": forwarded, "expected_irq_hz": EXPECTED_IRQ_HZ,
        "repo_commit": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "probe_source": str(PROBE_SOURCE.relative_to(ROOT)),
    }, indent=2) + "\n")
    write_report(artifact, result)

    print("\nEncore self-diagnostic")
    print(f"  Requested speed:        {target_speed:.2f}%")
    print(f"  Guest CPU load:         {'cooperative worker' if guest_load else 'normal'}")
    print("  Boot/warmup phase:")
    print(f"    Wall duration:         {boot_wall:.3f}s")
    print(f"    IRQ delivery total:    {boot['delivery']}")
    print(f"    IRQ delivery end:      {boot['current_delivery']}")
    print(f"    Emulator IRQ worst:    {with_unit(boot['irq_worst'], 'us')}")
    print(f"    LPT DATA rate end:     {boot['data_rate']}/s")
    print(f"    PDB05 worst:           {with_unit(boot['pdb_worst'], 'us')}")
    print("  Steady-state phase:")
    print("    Warmup completed:      30.000s guest time")
    print(f"    XINU sleep 10 wall:    {sleep_wall:.3f}s ({effective:.2f}% real-time)")
    print(f"    Guest IRQ delivery:    {irq['delivery']:.2f}%")
    print(f"    Guest IRQ rate:        {irq['rate']:.1f}/s ({irq['samples']} entries)")
    print(f"    Guest IRQ intervals:   mean={fmt_us(irq['mean'])} "
          f"sigma={fmt_us(irq['stddev'])} p50={fmt_us(irq['p50'])} "
          f"p95={fmt_us(irq['p95'])} p99={fmt_us(irq['p99'])} "
          f"worst={fmt_us(irq['worst'])}")
    print(f"    LPT DATA rate:         {lpt['data_rate']:.0f}/s")
    print(f"    PDB05 rate:            {lpt['rate']:.1f}/s")
    print(f"    PDB05 intervals:       p50={fmt_us(lpt['p50'])} "
          f"p95={fmt_us(lpt['p95'])} p99={fmt_us(lpt['p99'])} "
          f"worst={fmt_us(lpt['worst'])}")
    print(f"  Artifacts:               {artifact}")

    speed_low, speed_high = target_speed * 0.95, target_speed * 1.05
    healthy = (95.0 <= irq["delivery"] <= 105.0 and
               speed_low <= effective <= speed_high and lpt["worst"] <= 2500.0)
    print(f"  RESULT: {'PASS' if healthy else 'ABNORMAL'}")
    return 0 if healthy else 2


if __name__ == "__main__":
    raise SystemExit(main())
