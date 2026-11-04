#!/usr/bin/env python3
"""
A toolkit program on the shim whose colours follow the machine's theme while it runs (libadwaita's GTK 4, Qt 6 with the
portal's platform theme), read off the screen it draws on.

    python3 wayland_toolkit.py luminance SCAN WIDTH HEIGHT SETTINGS_DIRECTORY [SECONDS [dark|light]]

SCAN is the headless compositor's screen file, WIDTH by HEIGHT. The theme (SETTINGS_DIRECTORY/canvas.theme) is set to dark,
light, dark and light again, and each time the mean brightness of the window on the screen is read once the program has had
SECONDS (4) to follow (it starts with the word last, dark or light): a dark window is dark, a light one is light, and it is the same window. The window is the box of
what is not the desktop's own colour above the panel along the bottom (the lowest 64 rows).

One line a row: "ok NAME" or "FAIL NAME detail"; the exit status is the number of failures.
"""
import os
import sys
import time

failures = 0


def row(ok, name, detail=""):
    global failures
    if ok:
        print("ok " + name)
    else:
        print("FAIL %s %s" % (name, detail))
        failures += 1
    sys.stdout.flush()


def brightness(scan, width, height):
    """The mean brightness (0 to 255) of the middle of the window drawn on the screen, and the window's box."""
    raw = open(scan, "rb").read()
    desktop = b"\x1e\x1e\x1e"
    x1, y1, x2, y2 = width, height, -1, -1
    for y in range(height - 64):
        line = raw[y * width * 4:(y + 1) * width * 4]
        for x in range(width):
            if line[x * 4:x * 4 + 3] != desktop:
                x1, y1, x2, y2 = min(x1, x), min(y1, y), max(x2, x), max(y2, y)
    if x2 < 0:
        return None, None
    # the middle 60 percent of the box, away from the corners, the line and a title bar's own colours
    left, right = x1 + (x2 - x1) * 2 // 10, x1 + (x2 - x1) * 8 // 10
    top, bottom = y1 + (y2 - y1) * 2 // 10, y1 + (y2 - y1) * 8 // 10
    total = count = 0
    for y in range(top, bottom + 1):
        line = raw[y * width * 4:(y + 1) * width * 4]
        for x in range(left, right + 1, 2):
            b, g, r = line[x * 4], line[x * 4 + 1], line[x * 4 + 2]
            total += 0.299 * r + 0.587 * g + 0.114 * b
            count += 1
    return (total / count if count else None), (x1, y1, x2, y2)


def luminance(scan, width, height, root, seconds, first="dark"):
    seen = {}
    for word in (("dark", "light", "dark", "light") if first == "dark" else ("light", "dark", "light", "dark")):
        with open(os.path.join(root, "canvas.theme"), "w") as f:
            f.write(word + "\n")
        time.sleep(seconds)
        value, box = brightness(scan, width, height)
        seen.setdefault(word, []).append(value)
        if word == "dark":
            row(value is not None and value < 90, "with the theme dark the window is dark: mean brightness %s" % (None if value is None else round(value)), str(box))
        else:
            row(value is not None and value > 160, "with the theme light the window is light: mean brightness %s" % (None if value is None else round(value)), str(box))


if __name__ == "__main__":
    if sys.argv[1] == "luminance":
        luminance(sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), sys.argv[5], float(sys.argv[6]) if len(sys.argv) > 6 else 4.0,
                  sys.argv[7] if len(sys.argv) > 7 else "dark")
    else:
        raise SystemExit("unknown scenario " + sys.argv[1])
    sys.exit(failures)
