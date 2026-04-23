#!/usr/bin/env python3
"""
snap_attract.py — Drive Encore's F3 screenshot key from outside the SDL window.

Encore has no `--screenshot-at-frame` flag: F3 is the only way to emit a
PNG (see `save_screenshot()` in src/display.c). This helper finds the
running Encore window on the current X display and synthesises a series
of F3 keypresses, spaced by a fixed interval. Combine with `Xvfb` for
fully offscreen / CI capture.

Usage:
    DISPLAY=:99 python3 tools/snap_attract.py [TITLE_SUBSTR] [N] [INTERVAL_S]

Defaults: TITLE_SUBSTR="Encore", N=4, INTERVAL_S=2.0

Requires python-xlib (apt: `python3-xlib`, or
`pip install --break-system-packages python-xlib`).

Output PNGs land in $ENCORE_SCREENSHOT_DIR (or ./screenshots/) — Encore
chooses the filename, not this script.
"""
import sys
import time

from Xlib import display, X, XK
from Xlib.ext.xtest import fake_input


def find_window(d, root, title_substr):
    def walk(w):
        try:
            name = w.get_wm_name() or ""
        except Exception:
            name = ""
        if title_substr in (name or ""):
            return w
        try:
            for child in w.query_tree().children:
                hit = walk(child)
                if hit:
                    return hit
        except Exception:
            pass
        return None
    return walk(root)


def main():
    title    = sys.argv[1] if len(sys.argv) > 1 else "Encore"
    n        = int(sys.argv[2])   if len(sys.argv) > 2 else 4
    interval = float(sys.argv[3]) if len(sys.argv) > 3 else 2.0

    d = display.Display()
    root = d.screen().root

    deadline = time.time() + 30
    win = None
    while time.time() < deadline and not win:
        win = find_window(d, root, title)
        if not win:
            time.sleep(0.5)

    if not win:
        sys.stderr.write(f"snap_attract: no window matching {title!r} found\n")
        sys.exit(1)

    sys.stderr.write(f"snap_attract: found 0x{win.id:x} ({win.get_wm_name()!r})\n")
    win.set_input_focus(X.RevertToParent, X.CurrentTime)
    d.sync()

    keycode = d.keysym_to_keycode(XK.string_to_keysym("F3"))

    for i in range(n):
        time.sleep(interval)
        win.set_input_focus(X.RevertToParent, X.CurrentTime)
        d.sync()
        fake_input(d, X.KeyPress,   keycode); d.sync()
        time.sleep(0.05)
        fake_input(d, X.KeyRelease, keycode); d.sync()
        sys.stderr.write(f"snap_attract: sent F3 #{i + 1}/{n}\n")


if __name__ == "__main__":
    main()
