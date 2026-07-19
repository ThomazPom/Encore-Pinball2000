#!/usr/bin/env python3
"""Build one pb2kslib-adsp PCM cache with isolated parallel QEMU workers."""

import argparse
import concurrent.futures
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
    unique = []
    by_command = {}
    for entry in entries:
        command, decoded, payload = entry
        previous = by_command.get(command)
        if previous is None:
            by_command[command] = entry
            unique.append(entry)
            continue
        previous_name = previous[1][:32].split(b"\0", 1)[0]
        name = decoded[:32].split(b"\0", 1)[0]
        if (command == 0x003A and name == previous_name == b"dcs-bong" and
                payload == previous[2]):
            continue
        raise RuntimeError(f"worker ranges produced duplicate track ID {command:#06x}")
    entries = unique

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


def legacy_track_ids(path: Path):
    """Return distinct numbered tracks without decoding the legacy audio."""
    if not path.is_file():
        return set()
    data = path.read_bytes()
    if len(data) < 16:
        return set()
    version, table, count, entry_size = struct.unpack_from("<4I", data)
    if version != 1 or table != 16 or entry_size < 72:
        return set()
    result = set()
    for index in range(count):
        start = table + index * entry_size
        if start + 72 > len(data):
            break
        name = bytes(value ^ 0x3A for value in data[start:start + 32])
        name = name.split(b"\0", 1)[0]
        if len(name) < 5 or name[:1] != b"S":
            continue
        try:
            result.add(int(name[1:5], 16))
        except ValueError:
            pass
    return result


def generation_jobs(first, last, hinted_ids, tracks_per_job=6,
                    max_ids_per_job=256):
    """Bound mutable DSP history while keeping empty ID spans inexpensive."""
    jobs = []
    job_first = first
    hinted = 0
    for command in range(first, last + 1):
        is_hinted = command in hinted_ids
        if command > job_first and (
                command - job_first >= max_ids_per_job or
                (is_hinted and hinted >= tracks_per_job)):
            jobs.append((job_first, command - 1))
            job_first = command
            hinted = 0
        if is_hinted:
            hinted += 1
    jobs.append((job_first, last))
    return jobs


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
    hinted_ids = legacy_track_ids(Path(args.roms) / f"{args.game}_sound.bin")
    jobs = generation_jobs(range_first, range_last, hinted_ids)
    progress = [0] * len(jobs)
    progress_lock = threading.Lock()
    processes_lock = threading.Lock()
    live_processes = set()
    last_percent = [-1]

    def note_progress(line, job):
        pattern = re.compile(rb"dcs-cache-progress: ([0-9]+)/([0-9]+)")
        match = pattern.search(line)
        if not match:
            return
        with progress_lock:
            progress[job] = int(match.group(1))
            done = sum(progress)
            if total_ids:
                percent = min(100, done * 100 // total_ids)
                if percent != last_percent[0]:
                    last_percent[0] = percent
                    print(f"[dcs-cache] generating PCM: {percent:3d}% "
                          f"({done}/{total_ids} IDs)", flush=True)

    def run_job(job, first, last):
        root = work / f"job-{job}"
        cwd = work / f"cwd-{job}"
        cwd.mkdir(parents=True)
        log_path = work / f"job-{job}.log"
        env = os.environ.copy()
        # Cache workers are appliances, not child copies of the user's
        # interactive launch.  In particular P2K_FRAMEBUFFER_THREAD would
        # create one SDL window/render thread per worker even though QEMU
        # itself is launched with -display none.  Traces and PCM dumps can
        # likewise turn a bounded conversion into an I/O-heavy job.
        for name in (
            "P2K_FRAMEBUFFER_THREAD", "P2K_FRAMEBUFFER_FULLSCREEN",
            "P2K_QEMU_FRAMEBUFFER",
            "P2K_DISPLAY_BPP", "P2K_SCREENSHOT_DIR", "P2K_DIAG",
            "P2K_DCS_AUDIO_TRACE", "P2K_DCS_BYTE_TRACE",
            "P2K_DCS_ADSP_TRACE", "P2K_DCS_AUDIO_DUMP",
            "P2K_DCS_PCM_CPU",
        ):
            env.pop(name, None)
        env.update({
            "P2K_DCS_AUDIO": "1",
            "P2K_DCS_ENGINE": "pb2kslib-adsp",
            "P2K_PB2K_ADSP_CACHE_DIR": str(root),
            "P2K_PB2K_ADSP_FIRST_ID": str(first),
            "P2K_PB2K_ADSP_LAST_ID": str(last),
            "P2K_PB2K_ADSP_WORKER": "1",
            "P2K_NO_SAVEDATA": "1",
            "P2K_NO_UART_STDERR": "1",
            "P2K_NO_TIMING_AUDIT": "1",
            "P2K_UART_INPUT": "\r\n" * 48,
        })
        machine = f"pinball2000,game={args.game},roms-dir={args.roms}"
        if args.update:
            machine += f",update={args.update}"
        command = [args.qemu, "-M", machine, "-no-reboot", "-m", "16",
                   "-display", "none", "-serial", "null",
                   "-audio", "driver=wav"]
        print(f"[dcs-cache] job {job + 1}/{len(jobs)}: "
              f"IDs {first:#05x}..{last:#05x} "
              f"(headless, -display none)", flush=True)
        with log_path.open("wb") as log:
            process = subprocess.Popen(command, cwd=cwd, env=env,
                                       stdout=subprocess.PIPE,
                                       stderr=subprocess.STDOUT)
            with processes_lock:
                live_processes.add(process)
            try:
                for line in iter(process.stdout.readline, b""):
                    log.write(line)
                    note_progress(line, job)
                process.stdout.close()
                status = process.wait()
            finally:
                with processes_lock:
                    live_processes.discard(process)
            if status:
                raise RuntimeError(f"cache job {job + 1} failed ({status}); "
                                   f"log: {log_path}")
        print(f"[dcs-cache] job {job + 1}/{len(jobs)} finished", flush=True)
        return root

    try:
        roots = []
        with concurrent.futures.ThreadPoolExecutor(
                max_workers=min(workers, len(jobs))) as executor:
            futures = [executor.submit(run_job, job, first, last)
                       for job, (first, last) in enumerate(jobs)]
            for future in concurrent.futures.as_completed(futures):
                roots.append(future.result())

        produced = []
        for root in roots:
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
        with processes_lock:
            for process in live_processes:
                if process.poll() is None:
                    process.terminate()
        print(f"[dcs-cache] retained worker artifacts: {work}", flush=True)
        raise
    else:
        shutil.rmtree(work)


if __name__ == "__main__":
    main()
