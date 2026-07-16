#!/usr/bin/env python3
"""Build one pb2kslib-adsp PCM cache with isolated parallel QEMU workers."""

import argparse
import os
import re
import shutil
import struct
import subprocess
import tempfile
import threading
from pathlib import Path


def read_part(path: Path):
    data = path.read_bytes()
    if len(data) < 16:
        raise ValueError(f"short cache part: {path}")
    version, table, count, entry_size = struct.unpack_from("<4I", data)
    if version != 1 or table != 16 or entry_size != 72:
        raise ValueError(f"unsupported cache part: {path}")
    result = []
    for index in range(count):
        start = table + index * entry_size
        decoded = bytes(value ^ 0x3A for value in data[start:start + entry_size])
        if len(decoded) != entry_size:
            raise ValueError(f"truncated cache table: {path}")
        offset, size = struct.unpack_from("<II", decoded, 64)
        if offset + size > len(data):
            raise ValueError(f"bad PCM bounds in {path}")
        name = decoded[:32].split(b"\0", 1)[0].decode("ascii")
        if name == "dcs-bong":
            command = 0x003A
        else:
            command = int(name[1:5], 16)
        result.append((command, decoded, data[offset:offset + size]))
    return result


def merge(parts, output: Path):
    entries = []
    for part in parts:
        if part.is_file():
            entries.extend(read_part(part))
    entries.sort(key=lambda item: item[0])
    if not entries:
        raise RuntimeError("workers produced no PCM tracks")
    if len({item[0] for item in entries}) != len(entries):
        raise RuntimeError("worker ranges produced duplicate track IDs")

    table_size = len(entries) * 72
    payload_offset = 16 + table_size
    tmp = output.with_suffix(output.suffix + ".tmp")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tmp.open("wb") as stream:
        stream.write(struct.pack("<4I", 1, 16, len(entries), 72))
        cursor = payload_offset
        for _, decoded, payload in entries:
            record = bytearray(decoded)
            struct.pack_into("<II", record, 64, cursor, len(payload))
            stream.write(bytes(value ^ 0x3A for value in record))
            cursor += len(payload)
        for _, _, payload in entries:
            stream.write(payload)
    tmp.replace(output)
    return len(entries)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", required=True)
    parser.add_argument("--game", required=True)
    parser.add_argument("--roms", required=True)
    parser.add_argument("--update")
    parser.add_argument("--cache-root", required=True)
    parser.add_argument("--workers", required=True, type=int)
    args = parser.parse_args()
    args.qemu = str(Path(args.qemu).resolve())
    args.roms = str(Path(args.roms).resolve())
    if args.update:
        args.update = str(Path(args.update).resolve())

    workers = max(1, min(args.workers, 32, 4095))
    range_first = max(1, int(os.environ.get("P2K_PB2K_ADSP_FIRST_ID", "1"), 0))
    range_last = min(4095, int(os.environ.get("P2K_PB2K_ADSP_LAST_ID", "4095"), 0))
    if range_first > range_last:
        raise ValueError("empty DSP track range")
    total_ids = range_last - range_first + 1
    workers = min(workers, total_ids)
    key_command = [str(Path(__file__).with_name("pb2k-sound-key.py")),
                   "--game", args.game, "--roms", args.roms]
    if args.update:
        key_command += ["--update", args.update]
    source_key = subprocess.check_output(key_command, text=True).strip()
    output = Path(args.cache_root) / args.game / f"{source_key}.pcm.pb2k"
    if output.is_file():
        return

    Path(args.cache_root).mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="pb2kslib-adsp-",
                                 dir=args.cache_root))
    processes = []
    progress = [0] * workers
    progress_lock = threading.Lock()
    last_percent = [-1]

    def drain_output(pipe, log, worker):
        pattern = re.compile(rb"dcs-cache-progress: ([0-9]+)/([0-9]+)")
        for line in iter(pipe.readline, b""):
            log.write(line)
            match = pattern.search(line)
            if not match:
                continue
            with progress_lock:
                progress[worker] = int(match.group(1))
                done = sum(progress)
                total = total_ids
                if total:
                    percent = min(100, done * 100 // total)
                    if percent != last_percent[0]:
                        last_percent[0] = percent
                        print(f"[dcs-cache] generating PCM: {percent:3d}% "
                              f"({done}/{total} IDs)", flush=True)
        pipe.close()

    try:
        for worker in range(workers):
            first = range_first + (total_ids * worker) // workers
            last = range_first + (total_ids * (worker + 1)) // workers - 1
            root = work / f"worker-{worker}"
            cwd = work / f"cwd-{worker}"
            cwd.mkdir(parents=True)
            log = (work / f"worker-{worker}.log").open("wb")
            env = os.environ.copy()
            env.update({
                "P2K_DCS_AUDIO": "1",
                "P2K_DCS_ENGINE": "pb2kslib-adsp",
                "P2K_PB2K_ADSP_CACHE_DIR": str(root),
                "P2K_PB2K_ADSP_FIRST_ID": str(first),
                "P2K_PB2K_ADSP_LAST_ID": str(last),
                "P2K_PB2K_ADSP_WORKER": "1",
                "P2K_NO_SAVEDATA": "1",
                "P2K_NO_UART_STDERR": "1",
                "P2K_UART_INPUT": "\r\n" * 48,
            })
            machine = f"pinball2000,game={args.game},roms-dir={args.roms}"
            if args.update:
                machine += f",update={args.update}"
            command = [args.qemu, "-M", machine, "-no-reboot", "-m", "16",
                       "-display", "none", "-serial", "null",
                       "-audio", "driver=wav"]
            print(f"[dcs-cache] worker {worker + 1}/{workers}: "
                  f"IDs {first:#05x}..{last:#05x}", flush=True)
            process = subprocess.Popen(command, cwd=cwd, env=env,
                                       stdout=subprocess.PIPE,
                                       stderr=subprocess.STDOUT)
            reader = threading.Thread(target=drain_output,
                                      args=(process.stdout, log, worker),
                                      daemon=True)
            reader.start()
            processes.append((process, reader, log, root, worker))

        failures = []
        for process, reader, log, _, worker in processes:
            status = process.wait()
            reader.join()
            log.close()
            if status:
                failures.append((worker, status))
            print(f"[dcs-cache] worker {worker + 1}/{workers} finished",
                  flush=True)
        if failures:
            logs = ", ".join(str(work / f"worker-{i}.log")
                             for i, _ in failures)
            raise RuntimeError(f"cache workers failed; logs: {logs}")

        produced = []
        for _, _, _, root, _ in processes:
            produced.extend((root / args.game).glob("*.pcm.pb2k"))
        names = {part.name for part in produced}
        if len(names) > 1:
            raise RuntimeError("workers selected different update bundles")
        if names:
            output = Path(args.cache_root) / args.game / names.pop()
        parts = produced
        count = merge(parts, output)
        print(f"[dcs-cache] merged {count} tracks: {output}", flush=True)
    except Exception:
        for process, reader, log, _, _ in processes:
            if process.poll() is None:
                process.terminate()
            reader.join(timeout=1)
            if not log.closed:
                log.close()
        print(f"[dcs-cache] retained worker artifacts: {work}", flush=True)
        raise
    else:
        shutil.rmtree(work)


if __name__ == "__main__":
    main()
