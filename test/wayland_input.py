#!/usr/bin/env python3
"""
The Wayland shim's seat, end to end: a real libwayland client on a headless compositor that reads one more input device,
a FIFO of evdev records this script writes, so a pointer, a wheel and keys arrive as a person's would.

    python3 test/wayland_input.py FIFO CLIENT_LOG [LAYOUT]

The client (test/wayland_shm.c -v, built with libxkbcommon) writes a line on standard output for each event it is sent,
and this reads that log. One line a row: "ok NAME" or "FAIL NAME ...". The exit status is the number of failures.
"""
import os
import struct
import sys
import time

fifo_path, log_path = sys.argv[1], sys.argv[2]
layout = sys.argv[3] if len(sys.argv) > 3 else "us"
EV_SYN, EV_KEY, EV_REL = 0, 1, 2
REL_X, REL_Y, REL_WHEEL, REL_HWHEEL = 0, 1, 8, 6
BTN_LEFT = 0x110
failures = 0
clock = [1_000_000_000]
fifo = os.open(fifo_path, os.O_WRONLY)


def record(kind, code, value):
    clock[0] += 20_000
    return struct.pack("qqHHi", clock[0] // 1_000_000, clock[0] % 1_000_000, kind, code, value)


def send(*events):
    data = b"".join(record(*e) for e in events) + record(EV_SYN, 0, 0)
    os.write(fifo, data)


def log():
    try:
        return open(log_path).read()
    except OSError:
        return ""


def wait_for(text, since=0, timeout=3.0):
    end = time.time() + timeout
    while time.time() < end:
        found = log()
        at = found.find(text, since)
        if at >= 0:
            return at
        time.sleep(0.05)
    return -1


def row(ok, name, detail=""):
    global failures
    print(("ok " if ok else "FAIL ") + name + ("" if ok else " " + detail), flush=True)
    failures += 0 if ok else 1


def move(dx, dy):
    # One count at a time and slowly: the compositor's pointer is one to one below a count a millisecond.
    steps = []
    for _ in range(abs(dx)):
        steps.append((EV_REL, REL_X, 1 if dx > 0 else -1))
    for _ in range(abs(dy)):
        steps.append((EV_REL, REL_Y, 1 if dy > 0 else -1))
    for at in range(0, len(steps), 64):
        send(*steps[at:at + 64])
    time.sleep(0.2)


def key(code, down):
    send((EV_KEY, code, 1 if down else 0))


# What the client was told at once: the keymap, its compilation, the repeat, and the keys.
row(wait_for("xkb ok") >= 0, "the keymap is a descriptor the client's xkb compiles", log()[-300:])
row(wait_for("repeat ") >= 0, "the client is told how fast a held key repeats")
row(wait_for("keyboard enter") >= 0, "a window the person asked for has the keys when it is shown", log()[-300:])

# The pointer, from the middle of the screen to the window at the top left.
move(-300, -200)
at = wait_for("pointer enter")
row(at >= 0, "the pointer entering the window is an enter", log()[-300:])
if at >= 0:
    # The compositor reads a burst of the hand's motion at once, so the enter is at the place the pointer had reached when it
    # looked, and the last place the client is told, enter or motion, is where the hand ended.
    time.sleep(0.3)
    places = [line.split() for line in log()[at:].split("\n") if line.startswith(("pointer enter", "pointer motion"))]
    x, y = int(places[-1][2]), int(places[-1][3])
    row(90 <= x <= 110 and 90 <= y <= 110, "and the last place it is told is where the hand ended (%d, %d)" % (x, y))
move(5, 5)
row(wait_for("pointer motion") >= 0, "motion over the window is motion")
mark = len(log())
send((EV_KEY, BTN_LEFT, 1))
send((EV_KEY, BTN_LEFT, 0))
row(wait_for("pointer button 272 1", mark) >= 0 and wait_for("pointer button 272 0", mark) >= 0, "a click is a press and a release")
mark = len(log())
send((EV_REL, REL_WHEEL, 1))
row(wait_for("pointer value120 0 -120", mark) >= 0 and wait_for("pointer axis 0 -15", mark) >= 0,
    "a notch of the wheel up is value120 -120 and fifteen units on the vertical axis", log()[mark:][-200:])
mark = len(log())
send((EV_REL, REL_HWHEEL, 1))
row(wait_for("pointer value120 1 120", mark) >= 0, "a notch to the right is positive on the horizontal axis", log()[mark:][-200:])

# Keys, read by the client's own keymap.
mark = len(log())
key(30, True)
key(30, False)
want = {"us": ("a", "a"), "de": ("a", "a"), "fr": ("q", "q")}[layout]
row(wait_for("key 30 1 %s %s" % want, mark) >= 0 and wait_for("key 30 0 %s" % want[0], mark) >= 0,
    "the key at A's place is %s on the %s layout" % (want[0], layout), log()[mark:][-200:])
mark = len(log())
key(42, True)
key(30, True)
key(30, False)
key(42, False)
upper = want[0].upper()
row(wait_for("modifiers 1 0 0", mark) >= 0 and wait_for("key 30 1 %s %s" % (upper, upper), mark) >= 0,
    "Shift is a modifier of the client's keymap and capitalises the letter", log()[mark:][-300:])
mark = len(log())
key(58, True)
key(58, False)
key(30, True)
key(30, False)
row(wait_for("modifiers 0 0 2", mark) >= 0 and wait_for("key 30 1 %s %s" % (upper, upper), mark) >= 0,
    "Caps Lock is a locked modifier", log()[mark:][-300:])
key(58, True)
key(58, False)
mark = len(log())
key(100, True)
key(16, True)
key(16, False)
key(100, False)
row(wait_for("modifiers 128 0 0", mark) >= 0, "AltGr is a modifier of its own (Mod5)", log()[mark:][-300:])
if layout == "de":
    mark = len(log())
    key(21, True)
    key(21, False)
    row(wait_for("key 21 1 z z", mark) >= 0, "the German layout has Z where the US has Y", log()[mark:][-200:])
    mark = len(log())
    key(13, True)
    key(13, False)
    row(wait_for("key 13 1 dead_acute", mark) >= 0, "a dead key reaches the client as a dead key", log()[mark:][-200:])

# Out of the window.
mark = len(log())
move(400, 0)
row(wait_for("pointer leave", mark) >= 0, "the pointer leaving the window is a leave", log()[mark:][-200:])
mark = len(log())
send((EV_REL, REL_WHEEL, 1))
time.sleep(0.3)
row("value120" not in log()[mark:], "a wheel over no window is nobody's")

os.close(fifo)
sys.exit(failures)
