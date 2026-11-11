#!/usr/bin/env python3
from __future__ import annotations

import os
import select
import shutil
import subprocess
import tempfile
import time
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools" / "serial-updater-bridge" / "serial-updater-bridge.sh"


class SerialUpdaterBridgeTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("cc") and shutil.which("socat"),
                         "cc and socat are required")
    def test_reconnect_and_command_handoff_preserve_binary(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory)
            cache = temporary / "cache"
            environment = os.environ.copy()
            environment["XDG_CACHE_HOME"] = str(cache)
            shim = Path(subprocess.check_output(
                [str(TOOL), "build-shim"], env=environment, text=True
            ).strip())

            updater = temporary / "updater"
            guest = temporary / "guest"
            bridge = subprocess.Popen(
                [
                    shutil.which("socat"),
                    f"PTY,link={updater},rawer,echo=0",
                    f"PTY,link={guest},rawer,echo=0",
                ],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            self.addCleanup(self._stop_process, bridge)
            for _attempt in range(50):
                if updater.is_char_device() and guest.is_char_device():
                    break
                time.sleep(0.05)
            else:
                self.fail("socat did not create both PTYs")

            drivers = temporary / "tty-drivers"
            drivers.write_text("serial_8250 /dev/full 1 7 serial\n")
            fixture_source = temporary / "fixture.c"
            fixture_source.write_text(
                """
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(void) {
    char line[128] = {0};
    FILE *drivers = fopen("/proc/tty/drivers", "r");
    if (!drivers || !fgets(line, sizeof(line), drivers) ||
        !strstr(line, "/dev/full")) return 10;
    fclose(drivers);
    for (int attempt = 0; attempt < 2; attempt++) {
        int fd = open("/dev/full", O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd < 0) return 11;
        if (ioctl(fd, TIOCEXCL) != 0) return 12;
        if (close(fd) != 0) return 13;
    }
    int fd = open("/dev/full", O_RDWR | O_NOCTTY | O_NONBLOCK);
    const char command[] = "fupdate load com1 115200 sf\\n";
    const unsigned char binary[] = {0x00, 0x0a, 0xff};
    if (fd < 0) return 14;
    if (write(fd, command, sizeof(command) - 1) != sizeof(command) - 1)
        return 15;
    if (close(fd) != 0) return 17;
    usleep(200000);
    fd = open("/dev/full", O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return 18;
    if (write(fd, binary, sizeof(binary)) != sizeof(binary)) return 20;
    if (close(fd) != 0) return 21;
    return 0;
}
"""
            )
            fixture = temporary / "fixture"
            subprocess.run(
                [shutil.which("cc"), "-Wall", "-Wextra", "-Werror",
                 "-o", str(fixture), str(fixture_source)],
                check=True,
            )
            shim_environment = environment | {
                "LD_PRELOAD": str(shim),
                "ENCORE_SERIAL_DRIVERS_FILE": str(drivers),
                "ENCORE_SERIAL_UPDATER_ALIAS": "/dev/full",
                "ENCORE_SERIAL_UPDATER_DEVICE": str(updater),
                "ENCORE_SERIAL_UPDATER_CRLF": "1",
            }

            guest_fd = os.open(guest, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
            self.addCleanup(os.close, guest_fd)
            fixture_process = subprocess.Popen([str(fixture)], env=shim_environment)
            command = b"fupdate load com1 115200 sf\r\n"
            readers, _, _ = select.select([guest_fd], [], [], 0.1)
            self.assertEqual(readers, [], "command was not held across reopen")
            observed = bytearray()
            deadline = time.monotonic() + 2
            while len(observed) < len(command) and time.monotonic() < deadline:
                try:
                    observed.extend(os.read(guest_fd, 4096))
                except BlockingIOError:
                    time.sleep(0.01)
            self.assertEqual(bytes(observed), command)
            os.write(guest_fd, command[:-1] + b"\r\n")
            fixture_process.wait(timeout=2)
            self.assertEqual(fixture_process.returncode, 0)
            expected = b"fupdate load com1 115200 sf\r\n\x00\x0a\xff"
            received = bytearray(command)
            deadline = time.monotonic() + 1
            while len(received) < len(expected) and time.monotonic() < deadline:
                try:
                    received.extend(os.read(guest_fd, 4096))
                except BlockingIOError:
                    time.sleep(0.01)
            self.assertEqual(bytes(received), expected)
            direct_fd = os.open(updater, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
            os.close(direct_fd)
            subprocess.run([str(fixture)], env=shim_environment, check=True)

    @staticmethod
    def _stop_process(process: subprocess.Popen[str]) -> None:
        process.terminate()
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=2)


if __name__ == "__main__":
    unittest.main()
