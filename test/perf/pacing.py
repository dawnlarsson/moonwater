#!/usr/bin/env python3
"""
Frame pacing as a game sees it: a client of the wire with a window that draws every frame and reads back what the compositor
says of each (wp_presentation's presented or discarded, the frame callback's done), the way Mesa's Vulkan WSI paces a swapchain.

    python3 test/perf/pacing.py MODE [SECONDS] [BUFFERS]

MODE is how the client paces itself:
    fifo     Mesa's FIFO with wp_fifo_v1: every commit waits for the barrier the last one set and sets its own; the client draws
             as soon as a buffer is free (BUFFERS of them, 3 by default), so the compositor's barrier alone paces it.
    frame    the older FIFO: draw on the frame callback.
    mailbox  draw as soon as a buffer is free, no barrier: the compositor shows the newest at each refresh and discards the rest.
    timed    wp_commit_timing_v1: each commit is for the refresh after the last one presented (presented + refresh), with FIFO.

Each commit's feedback is matched to it. The rows said at the end (one "name value" a line):
    refresh        the refresh the compositor said (ns), and the period its presented times measure over their sequence counts
    seq_vs_time    the worst distance (us) of a presented time from the line its sequence count puts it on (first + n * measured)
    same_seq       commits answered with the sequence of an earlier commit's (two commits presented at one refresh: one was not)
    early          commits whose presented time is before the commit was sent (impossible)
    late_frames    commits presented two or more refreshes after the one before (the barrier released late, a frame skipped)
    flags          the flags seen, and zero_copy among them
    interval       presented intervals in refreshes: a histogram, and the standard deviation of the interval in us
    callback_lead  frame callback to the next presented refresh: mean, least (us): the time the client was given to draw
"""
import os
import socket
import struct
import sys
import time

mode = sys.argv[1] if len(sys.argv) > 1 else "fifo"
seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 5.0
buffer_count = int(sys.argv[3]) if len(sys.argv) > 3 else 3
W, H = 256, 256


def message(obj, op, *words, extra=b""):
    body = b"".join(struct.pack("<I", w & 0xFFFFFFFF) for w in words) + extra
    return struct.pack("<II", obj, ((8 + len(body)) << 16) | op) + body


def string(text):
    raw = text.encode() + b"\0"
    return struct.pack("<I", len(raw)) + raw + b"\0" * (-len(raw) % 4)


def now_ns():
    return time.clock_gettime_ns(time.CLOCK_MONOTONIC)


wire = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
wire.connect(os.environ.get("WAYLAND_DISPLAY", "/run/moonwater/wayland-0"))
pending = b""
next_id = [100]


def new_id():
    next_id[0] += 1
    return next_id[0]


def events(timeout):
    global pending
    wire.settimeout(timeout)
    try:
        data = wire.recv(65536)
        if not data:
            sys.exit("the compositor ended the connection")
        pending += data
    except (socket.timeout, BlockingIOError):
        pass
    found, at = [], 0
    while at + 8 <= len(pending):
        obj, head = struct.unpack_from("<II", pending, at)
        if at + (head >> 16) > len(pending):
            break
        found.append((obj, head & 0xFFFF, pending[at + 8:at + (head >> 16)], now_ns()))
        at += head >> 16
    pending = pending[at:]
    return found


names = {}
wire.sendall(message(1, 1, 2) + message(1, 0, 3))
synced = False
while not synced:
    for obj, op, body, _ in events(2.0):
        if obj == 2 and op == 0:
            length = struct.unpack_from("<I", body, 4)[0]
            names[body[8:8 + length - 1].decode()] = struct.unpack_from("<I", body)[0]
        if obj == 3 and op == 0:
            synced = True
for need in ("wl_compositor", "wl_shm", "xdg_wm_base", "wp_presentation"):
    if need not in names:
        sys.exit("no " + need)
COMP, SHM, WM, PRES, FIFOM, TIMERM = 4, 5, 6, 7, 8, 9
SURF, XSURF, TOP, FIFO, TIMER = 10, 11, 12, 13, 14
binds = [("wl_compositor", COMP, 4), ("wl_shm", SHM, 1), ("xdg_wm_base", WM, 1), ("wp_presentation", PRES, 1)]
has_fifo = "wp_fifo_manager_v1" in names
has_timer = "wp_commit_timing_manager_v1" in names
if has_fifo:
    binds.append(("wp_fifo_manager_v1", FIFOM, 1))
if has_timer:
    binds.append(("wp_commit_timing_manager_v1", TIMERM, 1))
for interface, new, version in binds:
    wire.sendall(message(2, 0, names[interface], extra=string(interface) + struct.pack("<II", version, new)))
out = message(COMP, 0, SURF) + message(WM, 2, XSURF, SURF) + message(XSURF, 1, TOP)
out += message(TOP, 2, extra=string("pacing " + mode))
if has_fifo:
    out += message(FIFOM, 1, FIFO, SURF)
if has_timer:
    out += message(TIMERM, 1, TIMER, SURF)
wire.sendall(out + message(SURF, 6))
serial = None
while serial is None:
    for obj, op, body, _ in events(2.0):
        if obj == XSURF and op == 0:
            serial = struct.unpack_from("<I", body)[0]
        if obj == WM and op == 0:
            wire.sendall(message(WM, 3, struct.unpack_from("<I", body)[0]))
wire.sendall(message(XSURF, 4, serial))

# The buffers, in one pool: each frame writes a band of its own colour across a buffer and damages the band.
pool = os.memfd_create("pacing")
size = W * H * 4
os.ftruncate(pool, size * buffer_count)
POOL = 20
socket.send_fds(wire, [message(SHM, 0, POOL, size * buffer_count)], [pool])
buffers = []
for at in range(buffer_count):
    bid = 30 + at
    wire.sendall(message(POOL, 0, bid, at * size, W, H, W * 4, 1))
    buffers.append(bid)
free = set(buffers)

commits = []          # dicts: sent, seq, when, refresh, flags, discarded, callback
feedback_of = {}      # feedback id -> commit index
callback_of = {}      # callback id -> commit index
last_presented = [None]
frame_n = [0]
end = now_ns() + int(seconds * 1e9)
callback_ready = [True]


def draw():
    bid = min(free)
    free.discard(bid)
    n = frame_n[0]
    frame_n[0] += 1
    band = (n * 8) % H
    os.pwrite(pool, bytes([n & 255, (n * 7) & 255, 0x80, 0xFF]) * (W * 8), (bid - 30) * size + band * W * 4)
    fb, cb = new_id(), new_id()
    out = message(SURF, 1, bid, 0, 0) + message(SURF, 9, 0, band, W, 8)
    out += message(SURF, 3, cb) + message(PRES, 1, SURF, fb)
    if has_fifo and mode in ("fifo", "timed"):
        out += message(FIFO, 1) + message(FIFO, 0)      # wait for the last barrier, set this one's
    if has_timer and mode == "timed" and last_presented[0]:
        when, refresh = last_presented[0]
        target = when + refresh * (1 + sum(1 for c in commits if c.get("seq") is None and not c.get("discarded")))
        out += message(TIMER, 0, (target // 1_000_000_000) >> 32, target // 1_000_000_000, target % 1_000_000_000)
    out += message(SURF, 6)
    commits.append({"sent": now_ns(), "buffer": bid})
    feedback_of[fb] = len(commits) - 1
    callback_of[cb] = len(commits) - 1
    wire.sendall(out)
    callback_ready[0] = False


while now_ns() < end:
    if free and (mode != "frame" or callback_ready[0]):
        draw()
        if mode == "mailbox":
            time.sleep(0.001)   # a game at 1000 frames a second: see pacing.c
        incoming = events(0)
    else:
        incoming = events(0.05)
    for obj, op, body, at in incoming:
        if obj in buffers and op == 0:
            free.add(obj)
        elif obj in callback_of and op == 0:
            commits[callback_of.pop(obj)]["callback"] = at
            callback_ready[0] = True
        elif obj in feedback_of:
            c = commits[feedback_of.pop(obj)]
            if op == 1:
                hi, lo, nsec, refresh, shi, slo, flags = struct.unpack_from("<7I", body)
                c.update(when=((hi << 32) | lo) * 1_000_000_000 + nsec, refresh=refresh, seq=(shi << 32) | slo, flags=flags, got=at)
                last_presented[0] = (c["when"], refresh)
            elif op == 2:
                c["discarded"] = True
        elif obj == WM and op == 0:
            wire.sendall(message(WM, 3, struct.unpack_from("<I", body)[0]))
# What is still owed comes within a few refreshes.
stop = now_ns() + 300_000_000
while feedback_of and now_ns() < stop:
    for obj, op, body, at in events(0.05):
        if obj in feedback_of:
            c = commits[feedback_of.pop(obj)]
            if op == 1:
                hi, lo, nsec, refresh, shi, slo, flags = struct.unpack_from("<7I", body)
                c.update(when=((hi << 32) | lo) * 1_000_000_000 + nsec, refresh=refresh, seq=(shi << 32) | slo, flags=flags, got=at)
            elif op == 2:
                c["discarded"] = True

shown = [c for c in commits if "seq" in c]
print("mode", mode, "buffers", buffer_count, "fifo", int(has_fifo), "timer", int(has_timer))
print("commits", len(commits), "presented", len(shown), "discarded", sum(1 for c in commits if c.get("discarded")),
      "unanswered", len(feedback_of))
if len(shown) < 3:
    sys.exit(0)
skip = shown[len(shown) // 10:]                # the start, while the compositor's lead settles, is left out
first, last = skip[0], skip[-1]
measured = (last["when"] - first["when"]) / max(1, last["seq"] - first["seq"])
print("refresh said %d measured %.0f (ns)" % (first["refresh"], measured))
worst = max(abs(c["when"] - first["when"] - (c["seq"] - first["seq"]) * measured) for c in skip)
print("seq_vs_time %.1f us" % (worst / 1000))
seqs, same = set(), 0
for c in skip:
    same += c["seq"] in seqs
    seqs.add(c["seq"])
print("same_seq", same)
print("early", sum(1 for c in skip if c["when"] < c["sent"]))
gaps = [(b["seq"] - a["seq"]) for a, b in zip(skip, skip[1:])]
hist = {}
for g in gaps:
    hist[g] = hist.get(g, 0) + 1
print("late_frames", sum(1 for g in gaps if g >= 2))
intervals = [(b["when"] - a["when"]) for a, b in zip(skip, skip[1:])]
mean = sum(intervals) / len(intervals)
dev = (sum((i - mean) ** 2 for i in intervals) / len(intervals)) ** 0.5
print("interval refreshes", " ".join("%d:%d" % kv for kv in sorted(hist.items())), "mean %.1f us sd %.1f us" % (mean / 1000, dev / 1000))
flags = {}
for c in skip:
    flags[c["flags"]] = flags.get(c["flags"], 0) + 1
print("flags", " ".join("0x%x:%d" % kv for kv in sorted(flags.items())))
# The time from a callback to the refresh the next commit was presented at: what the client had to draw in.
leads = []
for a, b in zip(skip, skip[1:]):
    if "callback" in a:
        leads.append(b["when"] - a["callback"])
if leads:
    print("callback_lead mean %.0f us least %.0f us" % (sum(leads) / len(leads) / 1000, min(leads) / 1000))
# Sent to presented, the latency of a frame.
lat = sorted(c["when"] - c["sent"] for c in skip)
print("latency median %.0f us p95 %.0f us" % (lat[len(lat) // 2] / 1000, lat[len(lat) * 95 // 100] / 1000))
