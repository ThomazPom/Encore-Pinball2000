#!/usr/bin/env python3
"""Temporary, systemd-backed CPU shielding for Encore's QEMU process.

The repository copy is the unprivileged client.  Runtime preparation installs
the same file root-owned under /usr/local/libexec, where socket-activated
systemd instances use --serve/--restore.  No command supplied by the client is
ever executed with privilege: the broker accepts only a stopped sibling PID,
moves it into its own cgroup, and adjusts scheduler placement.
"""

from __future__ import annotations

import argparse
import fcntl
import json
import os
from pathlib import Path
import select
import signal
import socket
import struct
import subprocess
import sys


RUNTIME = Path("/run/encore-pinball2000")
SOCKET = RUNTIME / "cpu-shield.sock"
LOCK = RUNTIME / "cpu-shield.lock"
STATE = RUNTIME / "cpu-shield-state.json"
CONFIG = Path("/etc/encore-pinball2000/cpu-shield-user")
CGROUP = Path("/sys/fs/cgroup")
CPUFREQ = Path("/sys/devices/system/cpu/cpufreq")
MANAGED_UNITS = ("user.slice", "system.slice", "init.scope", "machine.slice")
QEMU_NICE = -15


class ShieldError(RuntimeError):
    pass


def log(message: str) -> None:
    print(f"encore-cpu-shield: {message}", file=sys.stderr, flush=True)


def parse_cpu_list(value: str) -> set[int]:
    cpus: set[int] = set()
    for item in value.strip().split(","):
        if not item:
            continue
        if "-" in item:
            first, last = (int(part) for part in item.split("-", 1))
            cpus.update(range(first, last + 1))
        else:
            cpus.add(int(item))
    return cpus


def format_cpu_list(cpus: set[int]) -> str:
    ordered = sorted(cpus)
    if not ordered:
        return ""
    ranges: list[str] = []
    start = previous = ordered[0]
    for cpu in ordered[1:]:
        if cpu == previous + 1:
            previous = cpu
            continue
        ranges.append(str(start) if start == previous else f"{start}-{previous}")
        start = previous = cpu
    ranges.append(str(start) if start == previous else f"{start}-{previous}")
    return ",".join(ranges)


def hardware_irq_totals() -> dict[int, int]:
    """Return cumulative device IRQ counts, excluding Linux internal IPIs."""
    try:
        lines = Path("/proc/interrupts").read_text().splitlines()
        cpu_ids = [int(item.removeprefix("CPU")) for item in lines[0].split()]
    except (FileNotFoundError, IndexError, ValueError):
        return {}
    totals = {cpu: 0 for cpu in cpu_ids}
    for line in lines[1:]:
        label, separator, remainder = line.partition(":")
        if not separator or not label.strip().isdigit():
            continue
        fields = remainder.split()
        try:
            counts = [int(value) for value in fields[:len(cpu_ids)]]
        except ValueError:
            continue
        if len(counts) != len(cpu_ids):
            continue
        for cpu, count in zip(cpu_ids, counts):
            totals[cpu] += count
    return totals


def cpu_topology() -> tuple[set[int], set[int], set[int], set[int]]:
    online = parse_cpu_list(Path("/sys/devices/system/cpu/online").read_text())
    effective_file = CGROUP / "cpuset.cpus.effective"
    if effective_file.exists():
        effective = parse_cpu_list(effective_file.read_text())
        if effective:
            online.intersection_update(effective)
    if len(online) < 2:
        raise ShieldError("at least two usable logical CPUs are required")

    cores: dict[tuple[int, int], list[int]] = {}
    for cpu in sorted(online):
        base = Path(f"/sys/devices/system/cpu/cpu{cpu}/topology")
        try:
            package = int((base / "physical_package_id").read_text())
            core = int((base / "core_id").read_text())
        except (FileNotFoundError, ValueError):
            package, core = 0, cpu
        cores.setdefault((package, core), []).append(cpu)

    ordered_cores = sorted(cores.values(), key=lambda group: min(group))
    system_core = next(group for group in ordered_cores if min(online) in group)
    candidates = [group for group in ordered_cores if group is not system_core]
    if candidates:
        # Keep the first physical core wholly available to the host. Select
        # one logical CPU from each of up to three other physical cores for
        # QEMU's private scheduler pool, then keep every SMT
        # sibling of those CPUs out of ordinary host slices. The main loop is
        # a timing role too: leaving it in the saturated host pool can starve
        # device dispatch even while TCG itself remains isolated.
        irq_totals = hardware_irq_totals()
        ranked_cores = sorted(
            candidates,
            key=lambda group: (
                sum(irq_totals.get(cpu, 0) for cpu in group), min(group),
            ),
        )
        selected_cores = ranked_cores[:3]
        reserved = {min(group) for group in selected_cores}
        isolated = set().union(*(set(group) for group in selected_cores))
    else:
        reserved = {max(online)}
        isolated = set(reserved)
    general = online.difference(isolated)
    if not general:
        raise ShieldError("CPU selection would leave no CPU for the host")
    return online, reserved, general, isolated


def run_systemctl(*args: str, check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["systemctl", *args], text=True, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, check=check,
    )


def unit_exists(unit: str) -> bool:
    result = run_systemctl("show", unit, "-p", "LoadState", "--value", check=False)
    return result.returncode == 0 and result.stdout.strip() != "not-found"


def unit_allowed_cpus(unit: str) -> str:
    result = run_systemctl("show", unit, "-p", "AllowedCPUs", "--value")
    return result.stdout.strip()


def set_unit_cpus(unit: str, value: str) -> None:
    result = run_systemctl(
        "set-property", "--runtime", unit, f"AllowedCPUs={value}", check=False,
    )
    if result.returncode:
        detail = result.stderr.strip() or result.stdout.strip()
        raise ShieldError(f"cannot set {unit} AllowedCPUs={value!r}: {detail}")


def selected_governors(isolated: set[int]) -> dict[str, str]:
    """Return writable policies that can explicitly wake a reserved CPU."""
    result: dict[str, str] = {}
    for policy in sorted(CPUFREQ.glob("policy*")):
        governor = policy / "scaling_governor"
        try:
            affected = parse_cpu_list((policy / "affected_cpus").read_text())
            available = (policy / "scaling_available_governors").read_text().split()
            current = governor.read_text().strip()
        except (FileNotFoundError, PermissionError, ValueError):
            continue
        if affected.intersection(isolated) and "performance" in available:
            result[str(governor)] = current
    return result


def set_governor(path_value: str, value: str) -> None:
    path = Path(path_value)
    if (path.name != "scaling_governor" or
            path.parent.parent != CPUFREQ or
            not path.parent.name.startswith("policy")):
        raise ShieldError(f"refusing invalid governor path {path}")
    available = (path.parent / "scaling_available_governors").read_text().split()
    if value not in available:
        raise ShieldError(f"governor {value!r} is unavailable for {path.parent.name}")
    path.write_text(f"{value}\n")


def save_state(payload: dict[str, object]) -> None:
    temporary = STATE.with_suffix(".tmp")
    temporary.write_text(json.dumps(payload, sort_keys=True) + "\n")
    os.chmod(temporary, 0o600)
    temporary.replace(STATE)


def restore_state() -> None:
    if not STATE.exists():
        return
    try:
        payload = json.loads(STATE.read_text())
        units = payload.get("units", {})
        if not isinstance(units, dict):
            raise ShieldError("invalid saved unit state")
        governors = payload.get("governors", {})
        if not isinstance(governors, dict):
            raise ShieldError("invalid saved governor state")
        errors: list[str] = []
        for unit, value in units.items():
            try:
                set_unit_cpus(str(unit), str(value))
            except Exception as error:  # preserve every other original value
                errors.append(str(error))
        for path, value in governors.items():
            try:
                set_governor(str(path), str(value))
            except Exception as error:
                errors.append(str(error))
        if errors:
            raise ShieldError("; ".join(errors))
        STATE.unlink(missing_ok=True)
    except Exception as error:
        log(f"restore failed: {error}")
        raise


def configured_uid() -> int:
    try:
        first = CONFIG.read_text().splitlines()[0]
        return int(first.split(":", 1)[0])
    except (FileNotFoundError, IndexError, ValueError) as error:
        raise ShieldError(f"invalid or missing {CONFIG}") from error


def proc_status(pid: int) -> dict[str, str]:
    result: dict[str, str] = {}
    for line in Path(f"/proc/{pid}/status").read_text().splitlines():
        if ":" in line:
            key, value = line.split(":", 1)
            result[key] = value.strip()
    return result


def process_cgroup_path() -> Path:
    for line in Path("/proc/self/cgroup").read_text().splitlines():
        hierarchy, controllers, path = line.split(":", 2)
        if hierarchy == "0" and controllers == "":
            return CGROUP / path.lstrip("/")
    raise ShieldError("unified cgroup path is unavailable")


def validate_target(target: int, peer_pid: int, peer_uid: int) -> None:
    if peer_uid != configured_uid():
        raise ShieldError(f"caller uid {peer_uid} is not authorized")
    target_status = proc_status(target)
    peer_status = proc_status(peer_pid)
    target_uid = int(target_status["Uid"].split()[0])
    if target_uid != peer_uid:
        raise ShieldError("target belongs to another user")
    if target_status.get("PPid") != peer_status.get("PPid"):
        raise ShieldError("target and client are not siblings of the runner")
    if not target_status.get("State", "").startswith(("T", "t")):
        raise ShieldError("target must be stopped before attachment")


def apply_general_slice_mask(general: set[int], target: int,
                             reserved: set[int],
                             isolated: set[int]) -> dict[str, str]:
    originals = {
        unit: unit_allowed_cpus(unit)
        for unit in MANAGED_UNITS if unit_exists(unit)
    }
    governors = selected_governors(isolated)
    save_state({
        "target_pid": target,
        "reserved": sorted(reserved),
        "isolated": sorted(isolated),
        "general": sorted(general),
        "units": originals,
        "governors": governors,
    })
    value = format_cpu_list(general)
    try:
        for unit in originals:
            set_unit_cpus(unit, value)
        for path, current in governors.items():
            if current != "performance":
                set_governor(path, "performance")
    except Exception:
        restore_state()
        raise
    return originals


def pin_threads(pid: int, reserved: set[int]) -> None:
    task_root = Path(f"/proc/{pid}/task")
    try:
        tasks = list(task_root.iterdir())
    except FileNotFoundError:
        return
    for task in tasks:
        try:
            tid = int(task.name)
            if os.sched_getaffinity(tid) != reserved:
                os.sched_setaffinity(tid, reserved)
            if os.getpriority(os.PRIO_PROCESS, tid) > QEMU_NICE:
                os.setpriority(os.PRIO_PROCESS, tid, QEMU_NICE)
        except (FileNotFoundError, ProcessLookupError):
            continue
        except PermissionError as error:
            log(f"cannot pin thread {task.name}: {error}")


def send_reply(conn: socket.socket, ok: bool, **fields: object) -> None:
    conn.sendall((json.dumps({"ok": ok, **fields}, sort_keys=True) + "\n").encode())


def serve() -> int:
    if os.geteuid() != 0:
        raise ShieldError("broker must run as root")
    conn = socket.socket(fileno=0)
    RUNTIME.mkdir(mode=0o755, parents=True, exist_ok=True)
    lock_fd = os.open(LOCK, os.O_RDWR | os.O_CREAT, 0o600)
    target: int | None = None
    try:
        peer_pid, peer_uid, _peer_gid = struct.unpack(
            "3i", conn.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12)
        )
        request = conn.recv(256).decode(errors="replace").strip().split()
        if len(request) != 2 or request[0] != "ATTACH" or not request[1].isdigit():
            raise ShieldError("expected ATTACH PID")
        target = int(request[1])
        validate_target(target, peer_pid, peer_uid)
        try:
            fcntl.flock(lock_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise ShieldError("another Encore CPU shield is active") from error
        if STATE.exists():
            restore_state()

        _online, reserved, general, isolated = cpu_topology()

        # Move the stopped wrapper into this root-managed service cgroup before
        # excluding the reserved CPUs from user.slice.  It remains owned by the
        # desktop user and execs QEMU without acquiring any privilege.
        cgroup_procs = process_cgroup_path() / "cgroup.procs"
        cgroup_procs.write_text(f"{target}\n")
        os.sched_setaffinity(os.getpid(), general)
        os.sched_setaffinity(target, reserved)
        os.setpriority(os.PRIO_PROCESS, target, QEMU_NICE)
        apply_general_slice_mask(
            general, target, reserved, isolated,
        )

        os.kill(target, signal.SIGCONT)
        send_reply(
            conn, True,
            reserved=format_cpu_list(reserved),
            isolated=format_cpu_list(isolated),
            general=format_cpu_list(general),
        )
        log(
            f"active for pid {target}: QEMU={format_cpu_list(reserved)} "
            f"host={format_cpu_list(general)}"
        )

        pidfd = os.pidfd_open(target)
        try:
            poller = select.poll()
            poller.register(pidfd, select.POLLIN)
            while not poller.poll(50):
                pin_threads(target, reserved)
        finally:
            os.close(pidfd)
        return 0
    except Exception as error:
        try:
            send_reply(conn, False, error=str(error))
        except OSError:
            pass
        if target is not None:
            try:
                os.kill(target, signal.SIGCONT)
            except ProcessLookupError:
                pass
        log(str(error))
        return 1
    finally:
        try:
            restore_state()
        except Exception:
            pass
        os.close(lock_fd)


def client(pid: int) -> int:
    try:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as conn:
            conn.settimeout(10)
            conn.connect(str(SOCKET))
            conn.sendall(f"ATTACH {pid}\n".encode())
            response = bytearray()
            while b"\n" not in response:
                chunk = conn.recv(4096)
                if not chunk:
                    raise ShieldError("broker closed without a response")
                response.extend(chunk)
        result = json.loads(response.split(b"\n", 1)[0])
        if not result.get("ok"):
            raise ShieldError(str(result.get("error", "broker rejected the request")))
        print(
            "[run-qemu] CPU shield: "
            f"QEMU={result['reserved']}@nice{QEMU_NICE} "
            f"isolated={result['isolated']} host={result['general']}",
            file=sys.stderr,
        )
        return 0
    except (OSError, ValueError, KeyError, ShieldError) as error:
        print(f"[run-qemu] CPU shield failed: {error}", file=sys.stderr)
        return 1


def restore() -> int:
    if os.geteuid() != 0:
        raise ShieldError("restore must run as root")
    RUNTIME.mkdir(mode=0o755, parents=True, exist_ok=True)
    lock_fd = os.open(LOCK, os.O_RDWR | os.O_CREAT, 0o600)
    try:
        fcntl.flock(lock_fd, fcntl.LOCK_EX)
        restore_state()
        return 0
    finally:
        os.close(lock_fd)


def check() -> int:
    online, reserved, general, isolated = cpu_topology()
    print(
        f"online={format_cpu_list(online)} "
        f"qemu={format_cpu_list(reserved)}@nice{QEMU_NICE} "
        f"isolated={format_cpu_list(isolated)} "
        f"host={format_cpu_list(general)}"
    )
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--serve", action="store_true")
    group.add_argument("--restore", action="store_true")
    group.add_argument("--client", type=int, metavar="PID")
    group.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if args.serve:
        return serve()
    if args.restore:
        return restore()
    if args.check:
        return check()
    return client(args.client)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ShieldError as error:
        log(str(error))
        raise SystemExit(1)
