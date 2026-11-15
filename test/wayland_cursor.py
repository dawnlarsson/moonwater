#!/usr/bin/env python3
"""
A client's own pointer picture, end to end: a headless compositor built with the Wayland shim draws the pointer into its screen
(-P, as a card with no plane does), reads one more input device (a FIFO of evdev records this script writes), and a client that
speaks the wire from here (no libwayland) shows a window, takes the pointer into it, and names a picture of its own for it with
wl_pointer.set_cursor. The picture is looked for in the headless screen's pixels: at the pointer with its hotspot on it, its
translucent part mixed over the window, a new commit of it drawn, the pointer hidden for no surface, and the arrow for a buffer
larger than the compositor reads.

    unshare -rm python3 test/wayland_cursor.py SHELL WORKDIR

SHELL is the shell built with CONFIG_MOONWATER_WAYLAND, reached by the name compositor. It runs as root of a user namespace with a
private /run and /root, so nothing of the machine's is touched. One line a row: "ok NAME" or "FAIL NAME ...", and the exit status
is the number of failures.
"""
import array
import mmap
import os
import select
import socket
import struct
import subprocess
import sys
import time

shell, work = sys.argv[1], sys.argv[2]
W, H = 400, 300
WINDOW = 0x203040
failures = 0


def row(ok, name, detail=""):
    global failures
    print(("ok " if ok else "FAIL ") + name + ("" if ok else " " + detail), flush=True)
    failures += 0 if ok else 1


os.makedirs(work + "/bin", exist_ok=True)
for where in ("/run", "/root"):
    if subprocess.run(["mount", "-t", "tmpfs", "tmpfs", where]).returncode:
        row(False, "a private " + where + " is mounted")
        sys.exit(failures)
os.makedirs("/run/moonwater", exist_ok=True)
compositor = work + "/bin/compositor"
if os.path.lexists(compositor):
    os.unlink(compositor)
os.symlink(os.path.abspath(shell), compositor)
fifo_path = work + "/in.fifo"
if os.path.exists(fifo_path):
    os.unlink(fifo_path)
os.mkfifo(fifo_path)
scan = work + "/scan.img"
log = open(work + "/compositor.log", "w")
proc = subprocess.Popen([compositor, "-H", "%dx%d" % (W, H), scan, "-P", "-i", fifo_path, "-l", "us"], stdout=log, stderr=log)
for _ in range(100):
    if os.path.exists("/run/moonwater/wayland-0"):
        break
    time.sleep(0.1)
fifo = os.open(fifo_path, os.O_WRONLY)
clock = [1_000_000_000]


def feed(*events):
    out = b""
    for kind, code, value in events + ((0, 0, 0),):
        clock[0] += 20_000
        out += struct.pack("qqHHi", clock[0] // 1_000_000, clock[0] % 1_000_000, kind, code, value)
    os.write(fifo, out)


def screen():
    raw = open(scan, "rb").read()
    return lambda x, y: struct.unpack_from("<I", raw, 4 * (y * W + x))[0] & 0xffffff


def mix(ground, ink, cover):
    out = 0
    for shift in (0, 8, 16):
        out |= (((ground >> shift & 255) * (255 - cover) + (ink >> shift & 255) * cover + 127) // 255) << shift
    return out


s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect("/run/moonwater/wayland-0")
ids = [1]
data = b""
pending = []


def new():
    ids[0] += 1
    return ids[0]


def send(obj, op, body=b"", fds=()):
    head = struct.pack("<II", obj, (8 + len(body)) << 16 | op) + body
    if fds:
        s.sendmsg([head], [(socket.SOL_SOCKET, socket.SCM_RIGHTS, array.array("i", fds))])
    else:
        s.sendall(head)


def string(text):
    b = text.encode() + b"\0"
    return struct.pack("<I", len(b)) + b + b"\0" * (-len(b) % 4)


def events(wait, until=None):
    global data
    end = time.time() + wait
    while time.time() < end:
        if not select.select([s], [], [], max(0, end - time.time()))[0]:
            continue
        got = s.recv(65536)
        if not got:
            raise SystemExit("the compositor closed the connection")
        data += got
        while len(data) >= 8:
            obj, head = struct.unpack_from("<II", data)
            if len(data) < head >> 16:
                break
            pending.append((obj, head & 0xffff, data[8:head >> 16]))
            data = data[head >> 16:]
        if until and any(until(e) for e in pending):
            break


def take(match):
    for at, e in enumerate(pending):
        if match(e):
            return pending.pop(at)
    return None


registry = new()
send(1, 1, struct.pack("<I", registry))
done = new()
send(1, 0, struct.pack("<I", done))
events(3, lambda e: e[0] == done)
globals_ = {}
for obj, op, body in list(pending):
    if obj == registry and op == 0:
        name, length = struct.unpack_from("<II", body)
        iface = body[8:8 + length - 1].decode()
        globals_[iface] = (name, struct.unpack_from("<I", body, 8 + ((length + 3) & ~3))[0])
pending.clear()


def bind(iface, version):
    i = new()
    send(registry, 0, struct.pack("<I", globals_[iface][0]) + string(iface) + struct.pack("<II", min(version, globals_[iface][1]), i))
    return i


wl_compositor, shm, wm, seat = bind("wl_compositor", 4), bind("wl_shm", 1), bind("xdg_wm_base", 1), bind("wl_seat", 5)
pointer = new()
send(seat, 0, struct.pack("<I", pointer))


def buffer(width, height, rows, fmt=0):
    size = width * height * 4
    fd = os.memfd_create("cursor")
    os.ftruncate(fd, size)
    m = mmap.mmap(fd, size)
    for y in range(height):
        m[y * width * 4:(y + 1) * width * 4] = b"".join(struct.pack("<I", p) for p in rows(y))
    pool = new()
    send(shm, 0, struct.pack("<Ii", pool, size), [fd])
    os.close(fd)
    b = new()
    send(pool, 0, struct.pack("<IiiiiI", b, 0, width, height, width * 4, fmt))
    return b, m


surface = new()
send(wl_compositor, 0, struct.pack("<I", surface))
xdg = new()
send(wm, 2, struct.pack("<II", xdg, surface))
top = new()
send(xdg, 1, struct.pack("<I", top))
send(top, 2, string("cursor"))
send(surface, 6)
events(3, lambda e: e[0] == xdg and e[1] == 0)
configure = take(lambda e: e[0] == xdg and e[1] == 0)
if configure:
    send(xdg, 4, configure[2][:4])
window, _ = buffer(160, 120, lambda y: [0xff000000 | WINDOW] * 160, 1)
send(surface, 1, struct.pack("<Iii", window, 0, 0))
send(surface, 9, struct.pack("<iiii", 0, 0, 160, 120))
send(surface, 6)
events(1.0)


def window_box():
    raw = open(scan, "rb").read()
    words = struct.unpack("<%dI" % (W * H), raw[:4 * W * H])
    xs, ys = [], []
    for y in range(H):
        line = words[y * W:(y + 1) * W]
        hits = [x for x in range(W) if line[x] & 0xffffff == WINDOW]
        if hits:
            xs += (hits[0], hits[-1])
            ys.append(y)
    return (min(xs), min(ys), max(xs), max(ys)) if xs else None


# The window's picture is 160 by 120, and it lands (it grows to its size about its middle): its place is read once it is whole.
box = None
for _ in range(40):
    box = window_box()
    if box and box[2] - box[0] == 159 and box[3] - box[1] == 119:
        break
    events(0.1)
origin = box[:2] if box else None
row(box is not None and box[2] - box[0] == 159 and box[3] - box[1] == 119, "the client's window is on the headless screen, whole", repr(box))
if not box:
    proc.kill()
    sys.exit(failures)

# The pointer from wherever it is to the window's middle: relative steps until the window says it is entered, then the place the
# pointer has in the window is the one its last enter or motion says.
serial, local = None, None
for _ in range(60):
    for e in pending:
        if e[0] == wm and e[1] == 0:
            send(wm, 3, e[2][:4])
    entered = take(lambda e: e[0] == pointer and e[1] == 0)
    if entered:
        serial = struct.unpack_from("<I", entered[2])[0]
        local = (struct.unpack_from("<i", entered[2], 8)[0] >> 8, struct.unpack_from("<i", entered[2], 12)[0] >> 8)
    moved = [e for e in pending if e[0] == pointer and e[1] == 2]
    if moved:
        local = (struct.unpack_from("<i", moved[-1][2], 4)[0] >> 8, struct.unpack_from("<i", moved[-1][2], 8)[0] >> 8)
    pending[:] = [e for e in pending if not (e[0] == pointer and e[1] in (2, 5))]
    if serial is not None and local and abs(local[0] - 80) < 20 and abs(local[1] - 60) < 20:
        break
    target = (origin[0] + 80, origin[1] + 60)
    here = (origin[0] + local[0], origin[1] + local[1]) if local else (W // 2, H // 2)
    dx, dy = max(-4, min(4, target[0] - here[0])), max(-4, min(4, target[1] - here[1]))
    feed((2, 0, dx or 1), (2, 1, dy or 1))
    events(0.1)
row(serial is not None and local is not None, "the pointer is taken into the window and the window is told", repr(local))
if serial is None or local is None:
    proc.kill()
    sys.exit(failures)
px, py = origin[0] + local[0], origin[1] + local[1]

# A 16 by 16 picture, premultiplied: the top half opaque magenta, the bottom left translucent white, the bottom right clear; the
# hotspot 3 across and 4 down.
cursor = new()
send(wl_compositor, 0, struct.pack("<I", cursor))
picture, _ = buffer(16, 16, lambda y: [0xffff00ff] * 16 if y < 8 else [0x80808080] * 8 + [0] * 8)
send(pointer, 0, struct.pack("<IIii", serial, cursor, 3, 4))
send(cursor, 1, struct.pack("<Iii", picture, 0, 0))
send(cursor, 9, struct.pack("<iiii", 0, 0, 16, 16))
send(cursor, 6)
events(0.5)
at = screen()
left, top = px - 3, py - 4
wrong = [(x, y, at(left + x, top + y)) for y in range(8) for x in range(16) if at(left + x, top + y) != 0xff00ff]
row(not wrong, "a client's own picture is drawn at the pointer, its hotspot on the pointer",
    "at %d,%d: %d pixels not its colour, the first %r" % (left, top, len(wrong), wrong[:3]))
row(at(left + 2, top + 10) == mix(WINDOW, 0xffffff, 0x80) and at(left + 12, top + 12) == WINDOW and at(left - 1, top) == WINDOW and
    at(left + 16, top) == WINDOW, "its translucent part is mixed over the window, its clear part and what is past it are the window's",
    "%06x %06x" % (at(left + 2, top + 10), at(left + 12, top + 12)))

# The same surface named again with another hotspot: the picture moves by the difference, the pointer where it was.
send(pointer, 0, struct.pack("<IIii", serial, cursor, 10, 2))
events(0.5)
at = screen()
left, top = px - 10, py - 2
row(all(at(left + x, top + y) == 0xff00ff for x in range(16) for y in range(8)) and at(left - 1, top) == WINDOW and at(left, top - 1) == WINDOW,
    "the hotspot named again moves the picture by as much", "%06x %06x" % (at(left, top), at(left - 1, top)))

# Another commit of the cursor's surface is a new picture, drawn at once.
again, _ = buffer(16, 16, lambda y: [0xff00ffff] * 16)
send(cursor, 1, struct.pack("<Iii", again, 0, 0))
send(cursor, 9, struct.pack("<iiii", 0, 0, 16, 16))
send(cursor, 6)
events(0.5)
at = screen()
row(at(left, top) == 0x00ffff and at(left + 15, top + 15) == 0x00ffff, "a new commit of the picture is drawn in its place",
    "%06x" % at(left, top))

# No surface: the pointer is hidden over the window.
send(pointer, 0, struct.pack("<IIii", serial, 0, 0, 0))
events(0.5)
at = screen()
row(all(at(px + dx, py + dy) == WINDOW for dx in range(-4, 16) for dy in range(-4, 16)),
    "set_cursor with no surface hides the pointer over the window", "%06x" % at(px, py))

# A buffer larger than the compositor reads is not worn: the arrow is, its tip on the pointer.
send(pointer, 0, struct.pack("<IIii", serial, cursor, 0, 0))
huge, _ = buffer(512, 512, lambda y: [0xffff00ff] * 512)
send(cursor, 1, struct.pack("<Iii", huge, 0, 0))
send(cursor, 9, struct.pack("<iiii", 0, 0, 512, 512))
send(cursor, 6)
events(0.5)
at = screen()
row(at(px, py) != WINDOW and not any(at(px + dx, py + dy) in (0xff00ff, 0x00ffff) for dx in range(0, 24) for dy in range(0, 24)),
    "a picture larger than the compositor reads is not worn: the pointer is the arrow", "%06x" % at(px, py))

proc.terminate()
proc.wait(5)
row(proc.returncode in (0, -15), "the compositor ends when asked", str(proc.returncode))
sys.exit(failures)
