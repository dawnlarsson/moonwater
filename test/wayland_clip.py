#!/usr/bin/env python3
"""
The Wayland shim's clipboard, end to end: two real libwayland clients on a headless compositor that reads one more input
device, a FIFO of evdev records this script writes. The first copies a text when it gets a key, the second pastes when it
gets one; what the second says it received is what the compositor's clipboard held between them.

    python3 test/wayland_clip.py FIFO CLIENT WORKDIR

CLIENT is test/wayland_shm.c built (the compositor's socket is in WAYLAND_DISPLAY, as for any client). One line a row:
"ok NAME" or "FAIL NAME ...". The exit status is the number of failures.
"""
import os
import struct
import subprocess
import sys
import time

fifo_path, client, work = sys.argv[1], sys.argv[2], sys.argv[3]
EV_SYN, EV_KEY = 0, 1
failures = 0
clock = [1_000_000_000]
fifo = os.open(fifo_path, os.O_WRONLY)
children = []


def record(kind, code, value):
    clock[0] += 20_000
    return struct.pack("qqHHi", clock[0] // 1_000_000, clock[0] % 1_000_000, kind, code, value)


def key(code):
    os.write(fifo, record(EV_KEY, code, 1) + record(EV_SYN, 0, 0))
    os.write(fifo, record(EV_KEY, code, 0) + record(EV_SYN, 0, 0))


def start(name, *args, asked=True):
    log = open(os.path.join(work, name + ".log"), "w")
    env = dict(os.environ)
    if asked:
        env["MOONWATER_ASKED"] = "1"
    children.append(subprocess.Popen([client, "-n", "1000000", "-s", "-w", "200", "-h", "150", "-q"] + list(args),
                                     stdout=log, stderr=subprocess.STDOUT, env=env))
    return os.path.join(work, name + ".log")


def read(path):
    try:
        return open(path).read()
    except OSError:
        return ""


def wait_for(path, text, timeout=4.0):
    end = time.time() + timeout
    while time.time() < end:
        if text in read(path):
            return True
        time.sleep(0.05)
    return False


def row(ok, name, detail=""):
    global failures
    print(("ok " if ok else "FAIL ") + name + ("" if ok else " " + detail), flush=True)
    failures += 0 if ok else 1


# A window nobody asked for has the keys from the first key of all, and is not left without it: the focus that key makes is
# told to the client before the key is.
lone = start("lone", "-v", asked=False)
time.sleep(1.5)
key(30)
row(wait_for(lone, "key 30 1"), "the first key of all reaches a window nobody asked for", read(lone)[-300:])
children[0].kill()
children[0].wait()
children.pop(0)
time.sleep(0.3)

first = start("copier", "-C", "copied by a client")
row(wait_for(first, "selection none"), "a client that has the keys is told there is no selection, to begin with", read(first)[-300:])
time.sleep(0.3)
key(30)
row(wait_for(first, "copy set") and wait_for(first, "sent text/plain;charset=utf-8"),
    "a copy at a key is asked of the source, which sends its text", read(first)[-300:])
time.sleep(0.5)
second = start("paster", "-P")
row(wait_for(second, "selection offered"), "a client that gets the keys is offered the text", read(second)[-300:])
time.sleep(0.3)
key(30)
row(wait_for(second, "pasted 18 copied by a client"), "and a paste at a key is the text the other client copied", read(second)[-300:])
row("cancelled" not in read(first), "the copy's source is still the selection after a paste", read(first)[-200:])

# A third client copies in its turn: the text is its, and the first client's source is told it is not wanted.
third = start("third", "-C", "third's text")
row(wait_for(third, "selection offered"), "a client started after a copy is offered it too", read(third)[-300:])
time.sleep(0.3)
key(30)
row(wait_for(third, "copy set") and wait_for(third, "sent"), "its own copy at a key is taken", read(third)[-300:])
row(wait_for(first, "cancelled"), "and the first client's source is cancelled, as the text is another's", read(first)[-300:])

# A paste whose reader keeps the lock of its pipe (test/wayland_hold.py, MW-2026-113): the compositor's write of the text waits
# for the lock, and the compositor goes on all the same; when the reader lets go it gets the text.
env = dict(os.environ, MOONWATER_ASKED="1")
hold = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), "wayland_hold.py"), "paste"],
                        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env)
said = hold.stdout.readline().strip()
if said == "ready":
    key(30)
    said = hold.stdout.readline().strip()
time.sleep(0.5)
try:
    drawn = subprocess.run([client, "-n", "5", "-w", "100", "-h", "80", "-q"], capture_output=True, timeout=5).returncode == 0
except subprocess.TimeoutExpired:
    drawn = False
row(said == "pasting" and drawn, "a paste whose reader holds its pipe's lock does not hold the compositor", "the reader said %r" % said)
try:
    hold.stdin.write("\n")
    hold.stdin.flush()
    got = hold.stdout.readline().strip()
    hold.wait(10)
except (OSError, subprocess.TimeoutExpired):
    got = ""
    hold.kill()
row(got == "got %d" % len("third's text"), "and the text reaches it when it lets go of the lock", "it said %r" % got)

# A copy ended in the same write that asked for it (MW-2026-114): the source's send event still goes out with the pipe's end,
# which the compositor closed at the abort and then sent as a number that was nothing (the client was dropped) or was whatever
# the compositor had opened since.
copier = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), "wayland_hold.py"), "copy"],
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env)
said = copier.stdout.readline().strip()
if said == "ready":
    key(30)
    said = copier.stdout.readline().strip()
try:
    copier.wait(10)
except subprocess.TimeoutExpired:
    copier.kill()
row(said == "sent fifo", "a copy ended in the same write as it was asked is still sent its pipe's end", "the client said %r" % said)

for child in children:
    child.kill()
    child.wait()
sys.exit(failures)
