#!/usr/bin/env python3
"""
Two clients of the wire for test/wayland_clip.py, each with a window and the keys, that do at the next key what a toolkit
does not:

    MOONWATER_ASKED=1 python3 test/wayland_hold.py paste
        asks for the selection into a pipe while a thread of its own splices into the same pipe from a socket that has
        nothing to say (MW-2026-113). A splice holds the pipe's lock while it waits, and a read or a write of the pipe takes
        that lock before it looks at O_NONBLOCK, so the compositor's write of the text waits for as long as the splice
        does; the compositor's loop must not be the one that waits. It says "ready" when it has the keys and an offer,
        "pasting" once the paste is asked, and at a line on its standard input lets go, reads the pipe and says "got N".

    MOONWATER_ASKED=1 python3 test/wayland_hold.py copy
        sets a source as the selection and destroys it in the same write (MW-2026-114): the compositor has made a pipe for
        the source and queued its write end with the source's send event, and the source's end lets the copy go before the
        event is sent. It says "ready" when it has the keys, and after the key what came: "sent fifo" for the send event
        with the pipe's end, "sent other" for one with a descriptor that is not a pipe, "dropped" when the compositor
        ended the connection, "nothing" when no send event came.
"""
import os
import socket
import stat
import struct
import sys
import threading
import time

mode = sys.argv[1] if len(sys.argv) > 1 else "paste"


def message(obj, op, *words, extra=b""):
    body = b"".join(struct.pack("<I", w & 0xFFFFFFFF) for w in words) + extra
    return struct.pack("<II", obj, ((8 + len(body)) << 16) | op) + body


def string(text):
    raw = text.encode() + b"\0"
    return struct.pack("<I", len(raw)) + raw + b"\0" * (-len(raw) % 4)


wire = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
wire.connect(os.environ.get("WAYLAND_DISPLAY", "/run/moonwater/wayland-0"))
pending = b""
received = []   # the descriptors that came, in order
closed = [False]


def events(timeout):
    """The events that came within timeout seconds, as (object, opcode, body)."""
    global pending
    wire.settimeout(timeout)
    try:
        data, fds, _, _ = socket.recv_fds(wire, 65536, 16)
        if not data:
            closed[0] = True
        pending += data
        received.extend(fds)
    except socket.timeout:
        pass
    except OSError:
        closed[0] = True
    found, at = [], 0
    while at + 8 <= len(pending):
        obj, head = struct.unpack_from("<II", pending, at)
        if at + (head >> 16) > len(pending):
            break
        found.append((obj, head & 0xFFFF, pending[at + 8:at + (head >> 16)]))
        at += head >> 16
    pending = pending[at:]
    return found


def until(test, timeout=5.0):
    end = time.time() + timeout
    while time.time() < end and not closed[0]:
        for event in events(0.1):
            got = test(event)
            if got is not None:
                return got
    return None


# The globals by name, then a window of 64 by 64, the keyboard and the data device.
wire.sendall(message(1, 1, 2))
names = {}


def global_(event):
    obj, op, body = event
    if obj == 2 and op == 0:
        length = struct.unpack_from("<I", body, 4)[0]
        names[body[8:8 + length - 1].decode()] = struct.unpack_from("<I", body)[0]
    return True if all(n in names for n in ("wl_compositor", "wl_shm", "xdg_wm_base", "wl_seat", "wl_data_device_manager")) else None


until(global_)
for interface, new, version in (("wl_compositor", 3, 4), ("wl_shm", 4, 1), ("xdg_wm_base", 5, 1), ("wl_seat", 11, 5),
                                ("wl_data_device_manager", 12, 3)):
    wire.sendall(message(2, 0, names[interface], extra=string(interface) + struct.pack("<II", version, new)))
wire.sendall(message(11, 1, 13) + message(12, 1, 14, 11))   # get_keyboard, get_data_device
wire.sendall(message(3, 0, 6) + message(5, 2, 7, 6) + message(7, 1, 8) + message(6, 6))
serial = until(lambda e: struct.unpack_from("<I", e[2])[0] if e[0] == 7 and e[1] == 0 else None)
wire.sendall(message(7, 4, serial))
pool = os.memfd_create("hold")
os.ftruncate(pool, 64 * 64 * 4)
os.pwrite(pool, b"\x40" * (64 * 64 * 4), 0)
socket.send_fds(wire, [message(4, 0, 9, 64 * 64 * 4)], [pool])
wire.sendall(message(9, 0, 10, 0, 64, 64, 256, 1) + message(6, 1, 10, 0, 0) + message(6, 9, 0, 0, 64, 64) + message(6, 6))

# The keys, and (to paste) an offer of the selection that is current: the last selection event names it.
state = {"keys": False, "offer": 0}


def ready(event):
    obj, op, body = event
    if obj == 13 and op == 1:
        state["keys"] = True
    if obj == 14 and op == 5:
        state["offer"] = struct.unpack_from("<I", body)[0]
    return True if state["keys"] and (state["offer"] or mode == "copy") else None


if not until(ready, 8.0):
    print("no keys or no offer", state, flush=True)
    sys.exit(1)
print("ready", flush=True)
key = until(lambda e: struct.unpack_from("<I", e[2])[0] if e[0] == 13 and e[1] == 3 and struct.unpack_from("<I", e[2], 12)[0] == 1 else None, 8.0)
if not key:
    print("no key came", flush=True)
    sys.exit(1)

if mode == "copy":
    # A source of text set as the selection and destroyed at once, and a sync after them to know they were taken.
    wire.sendall(message(12, 0, 15) + message(15, 0, extra=string("text/plain;charset=utf-8")) + message(14, 1, 15, key) +
                 message(15, 1) + message(1, 0, 16))
    sent = until(lambda e: len(received) if e[0] == 15 and e[1] == 1 else None, 5.0)
    if closed[0] and sent is None:
        print("dropped", flush=True)
    elif sent is None:
        print("nothing", flush=True)
    else:
        print("sent fifo" if stat.S_ISFIFO(os.fstat(received[-1]).st_mode) else "sent other", flush=True)
    sys.exit(0)

# The pipe's lock held by a splice from a socket that says nothing, and the paste asked into the same pipe.
read_end, write_end = os.pipe()
quiet, speaker = socket.socketpair()
holder = threading.Thread(target=lambda: os.splice(quiet.fileno(), write_end, 4096), daemon=True)
holder.start()
time.sleep(0.2)
socket.send_fds(wire, [message(state["offer"], 1, extra=string("text/plain;charset=utf-8"))], [write_end])
print("pasting", flush=True)
sys.stdin.readline()
speaker.send(b"\n")
holder.join(5)
os.close(write_end)
got = b""
os.set_blocking(read_end, False)
end = time.time() + 5
while time.time() < end:
    try:
        chunk = os.read(read_end, 65536)
    except BlockingIOError:
        time.sleep(0.05)
        continue
    if not chunk:
        break
    got += chunk
print("got %d" % (len(got) - 1), flush=True)  # the splice's own byte is not the text
