#!/usr/bin/env python3
"""Boot QEMU, snapshot guest physical memory at t1 and t2 via -monitor TCP
pmemsave, terminate QEMU, diff dwords. Report addresses that incremented at
a rate plausible for a tick counter (clkint cadence)."""
import os, socket, subprocess, sys, time, signal, struct

ROOT = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(ROOT, "..", "..", ".."))
W1 = float(os.environ.get("W1", 12))
W2 = float(os.environ.get("W2", 32))
BASE = int(os.environ.get("BASE", "0x00200000"), 0)
LEN  = int(os.environ.get("LEN",  str(0x100000)), 0)
GAME = os.environ.get("GAME", "swe1")
PORT = 19000 + int.from_bytes(os.urandom(2), "big") % 1000

FAST = os.environ.get("FAST_DIR", "/home/encore/.cache/p2k-mem")
os.makedirs(FAST, exist_ok=True)
t1_path = os.path.join(FAST, f"snap_{GAME}_t1.bin")
t2_path = os.path.join(FAST, f"snap_{GAME}_t2.bin")
for p in (t1_path, t2_path):
    if os.path.exists(p):
        os.remove(p)

env = os.environ.copy()
env["P2K_DIAG"] = "1"
cmd = [
    os.path.join(REPO, "scripts/run-qemu.sh"), "-v", "--headless",
    "--game", GAME, "--update", "none", "--no-savedata",
    "--", "-qmp", f"tcp:127.0.0.1:{PORT},server=on,wait=off",
]
env.pop("QEMU_EXTRA", None)
log = open(os.path.join(ROOT, f"snap_{GAME}.qemu.log"), "wb")
print(f"[mem_diff] launching qemu, monitor port {PORT}, base=0x{BASE:08x} len=0x{LEN:x}")
proc = subprocess.Popen(cmd, cwd=REPO, env=env, stdin=subprocess.DEVNULL,
                        stdout=log, stderr=subprocess.STDOUT,
                        preexec_fn=os.setsid)


def connect_monitor(timeout=20):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            return socket.create_connection(("127.0.0.1", PORT), timeout=2)
        except OSError:
            time.sleep(0.5)
    raise RuntimeError("monitor never opened")


def stop_qemu():
    try:
        os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
    except Exception:
        pass


def qmp_send(s, obj, settle=0.1):
    s.sendall((__import__("json").dumps(obj) + "\n").encode())
    time.sleep(settle)
    out = b""
    end = time.time() + 5
    while time.time() < end:
        try:
            chunk = s.recv(8192)
            if not chunk:
                break
            out += chunk
            if b'"return"' in out or b'"error"' in out:
                break
        except (socket.timeout, BlockingIOError):
            break
    return out.decode(errors="replace")


try:
    s = connect_monitor(20)
    s.settimeout(2)
    print("[mem_diff] qmp connected")
    # consume greeting + send capabilities
    qmp_send(s, {"execute": "qmp_capabilities"}, 0.3)
    t0 = time.time()
    while time.time() - t0 < W1:
        time.sleep(0.1)
    print(f"[mem_diff] pmemsave -> {t1_path}")
    print(qmp_send(s, {"execute": "pmemsave", "arguments":
        {"val": BASE, "size": LEN, "filename": t1_path}}, 0.3))
    while time.time() - t0 < W2:
        time.sleep(0.1)
    print(f"[mem_diff] pmemsave -> {t2_path}")
    print(qmp_send(s, {"execute": "pmemsave", "arguments":
        {"val": BASE, "size": LEN, "filename": t2_path}}, 0.3))
    qmp_send(s, {"execute": "quit"}, 0.3)
    s.close()
finally:
    stop_qemu()
    try:
        proc.wait(timeout=10)
    except Exception:
        pass

if not (os.path.exists(t1_path) and os.path.exists(t2_path)):
    print("ERROR: snapshots missing", file=sys.stderr)
    sys.exit(1)

with open(t1_path, "rb") as f:
    a = f.read()
with open(t2_path, "rb") as f:
    b = f.read()
n = min(len(a), len(b)) // 4
print(f"[mem_diff] comparing {n} dwords ({n*4} bytes)")
hits = []
A = struct.unpack_from(f"<{n}I", a)
B = struct.unpack_from(f"<{n}I", b)
DT = (W2 - W1)
for i in range(n):
    da = A[i]
    db = B[i]
    if da == db:
        continue
    delta = (db - da) & 0xffffffff
    sdelta = delta if delta < (1 << 31) else delta - (1 << 32)
    if sdelta <= 0:
        continue
    rate = sdelta / DT
    if 5 <= rate <= 4500:
        hits.append((rate, BASE + i * 4, A[i], B[i], sdelta))

hits.sort(key=lambda h: -h[0])
print(f"[mem_diff] {len(hits)} dwords increased at 5..4500/s")
print("addr        rate/s        t1            t2          delta")
for rate, addr, va, vb, d in hits[:80]:
    print(f"0x{addr:08x}  {rate:8.2f}  0x{va:08x}  0x{vb:08x}  {d:>10}")
