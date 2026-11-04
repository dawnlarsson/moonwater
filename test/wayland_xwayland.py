#!/usr/bin/env python3
"""
The X clients on the Wayland shim, end to end: an Xlib client (test/wayland_xclient.c) inside Xwayland, which is a client of
the compositor, read through the compositor's evdev FIFO, which this script writes. The rows are what the client's own output
says it was sent (its window drawn, a key by keysym, pointer motion in root coordinates, a click) and what the screen holds
(the client's colour, counted in scan.img, the compositor's headless screen image).

    python3 test/wayland_xwayland.py FIFO CLIENT_LOG SCAN WIDTH HEIGHT

One line a row: "ok NAME" or "FAIL NAME ...", and the exit status is the number of failures.
"""
import os
import struct
import sys
import time

fifo_path, client_log, scan, width, height = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
EV_SYN, EV_KEY, EV_REL = 0, 1, 2
REL_X, REL_Y = 0, 1
BTN_LEFT = 272
KEY_A, KEY_ENTER = 30, 28
COLOUR = bytes((0xcc, 0x66, 0x33))  # the client's colour, 0x3366cc, in the screen's byte order either way round
failures = 0
clock = [1_000_000_000]
fifo = os.open(fifo_path, os.O_WRONLY)


def record(kind, code, value):
    clock[0] += 20_000
    return struct.pack("qqHHi", clock[0] // 1_000_000, clock[0] % 1_000_000, kind, code, value)


def send(*records):
    os.write(fifo, b"".join(records) + record(EV_SYN, 0, 0))


def key(code):
    send(record(EV_KEY, code, 1))
    time.sleep(0.05)
    send(record(EV_KEY, code, 0))
    time.sleep(0.05)


def move(dx, dy):
    send(record(EV_REL, REL_X, dx), record(EV_REL, REL_Y, dy))
    time.sleep(0.05)


def click():
    send(record(EV_KEY, BTN_LEFT, 1))
    time.sleep(0.05)
    send(record(EV_KEY, BTN_LEFT, 0))


def read(path):
    try:
        with open(path) as f:
            return f.read()
    except OSError:
        return ""


def lines(prefix):
    return [line.split() for line in read(client_log).splitlines() if line.startswith(prefix)]


def wait_for(text, timeout=4.0):
    end = time.time() + timeout
    while time.time() < end:
        if text in read(client_log):
            return True
        time.sleep(0.05)
    return False


def row(ok, name, detail=""):
    global failures
    print(("ok " if ok else "FAIL ") + name + ("" if ok else " " + detail), flush=True)
    failures += 0 if ok else 1


def colour_pixels():
    """The pixels of the screen image that are the client's colour, in either byte order of the pixel's three colour bytes."""
    try:
        raw = open(scan, "rb").read()
    except OSError:
        return -1
    if len(raw) < width * height * 4:
        return -1
    count = 0
    for at in range(0, width * height * 4, 4):
        px = raw[at:at + 3]
        if sorted(px) == sorted(COLOUR):
            count += 1
    return count


# The client maps and draws: its first expose is its window on the screen.
row(wait_for("expose"), "the X client's window is mapped and drawn through Xwayland", read(client_log)[-300:])
time.sleep(1.0)
pixels = colour_pixels()
row(pixels >= 0.8 * 800 * 600, "the client's colour is on the compositor's screen", f"{pixels} pixels")

# Keys: the X server sends the client the keys the compositor's seat sends its focus.
key(KEY_A)
row(wait_for("key 61"), "a typed key reaches the X client by its keysym", read(client_log)[-200:])
key(KEY_ENTER)
row(wait_for("key ff0d"), "Return reaches the X client by its keysym", read(client_log)[-200:])

# Pointer: relative motion from the compositor's pointer reaches the client as root coordinates, and a click at the place. The
# pointer's entry into the window is said first (its place is the old one): the motion after it is what is counted.
time.sleep(0.5)
before = len(lines("motion"))
for _ in range(6):
    move(8, 4)
time.sleep(0.5)
after = lines("motion")[before:][1:]  # the first is the entry, at the place the pointer had before the window had it
xs = [int(m[1]) for m in after]
ys = [int(m[2]) for m in after]
moved = bool(xs) and max(xs) - min(xs) >= 16 and all(b >= a for a, b in zip(xs, xs[1:]))
row(moved, "relative motion reaches the X client in root coordinates", f"motion lines {after[-4:]}")
if moved:
    click()
    row(wait_for("button 1 "), "a click reaches the X client", read(client_log)[-200:])
    button = [b for b in lines("button") if b[1] == "1"]
    row(bool(button) and int(button[-1][4]) == xs[-1] and int(button[-1][5]) == ys[-1],
        "the click is where the pointer was", f"button {button[-1:]} motion {xs[-1:]} {ys[-1:]}")

os.close(fifo)
print(f"{failures} failed", flush=True)
sys.exit(failures)
