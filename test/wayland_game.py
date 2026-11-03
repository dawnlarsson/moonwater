#!/usr/bin/env python3
"""
The Wayland shim's pointer lock, end to end: a real libwayland client that locks the pointer and says each relative motion, on
a headless compositor that reads one more input device, a FIFO of evdev records this script writes.

    python3 test/wayland_game.py FIFO CLIENT_LOG

The client (test/wayland_shm.c -L -v) writes "locked" when its lock holds, "relative DX DY" for each relative motion and the
usual line for each pointer event, and this reads that log. One line a row: "ok NAME" or "FAIL NAME ...". The exit status is the
number of failures.
"""
import os
import struct
import sys
import time

fifo_path, log_path = sys.argv[1], sys.argv[2]
EV_SYN, EV_REL = 0, 2
REL_X, REL_Y = 0, 1
failures = 0
clock = [1_000_000_000]
fifo = os.open(fifo_path, os.O_WRONLY)


def record(kind, code, value):
    clock[0] += 20_000
    return struct.pack("qqHHi", clock[0] // 1_000_000, clock[0] % 1_000_000, kind, code, value)


def send(*events):
    os.write(fifo, b"".join(record(*e) for e in events) + record(EV_SYN, 0, 0))


def log():
    try:
        return open(log_path).read()
    except OSError:
        return ""


def wait_for(text, since=0, timeout=3.0):
    end = time.time() + timeout
    while time.time() < end:
        at = log().find(text, since)
        if at >= 0:
            return at
        time.sleep(0.05)
    return -1


def row(ok, name, detail=""):
    global failures
    print(("ok " if ok else "FAIL ") + name + ("" if ok else " " + detail), flush=True)
    failures += 0 if ok else 1


# The pointer from the middle of the screen into the window at the top left: the window is under it and has the keys (the
# client was started by the person), so the lock holds.
for _ in range(6):
    send(*[(EV_REL, REL_X, -1)] * 50, *[(EV_REL, REL_Y, -1)] * 33)
    time.sleep(0.05)
time.sleep(0.3)
row(wait_for("locked") >= 0, "a lock on the window under the pointer that has the keys holds it", log()[-300:])
mark = len(log())
time.sleep(0.2)
send((EV_REL, REL_X, 7), (EV_REL, REL_Y, -5))
row(wait_for("relative 7 -5", mark) >= 0, "the hand's counts are the client's, as they came", log()[mark:][-200:])
mark = len(log())
send(*[(EV_REL, REL_X, 1)] * 400)
time.sleep(0.4)
counts = sum(int(line.split()[1]) for line in log()[mark:].split("\n") if line.startswith("relative "))
row(counts == 400, "and keep coming where the pointer would have reached the edge of the screen (%d of 400)" % counts, log()[mark:][-200:])
row("pointer motion" not in log()[mark:], "the pointer does not move: the client is told no place", log()[mark:][-200:])

sys.exit(failures)
