#!/usr/bin/env python3
"""
The window manager rows (test/wayland_xwm.sh). First the connection the compositor accepts for the window manager is spoken to by
a server that lies (a setup cut short, a length over the cap, a setup that is no setup, a reply of a length no one may send, an
event whose format is no format, a refusal of the root's events): each connection is dropped and the compositor goes on. Then
Xwayland, started with its window manager's connection given as -wm, is a client of the compositor: the compositor redirects the
root's subwindows (Composite, Manual), and an Xlib client's window is a window of the compositor with its title, the keys, the
pointer, a size it is asked to, a popup where it asks, a screen it fills, and a close that reaches it as WM_DELETE_WINDOW. Last,
Xwayland is killed: the compositor goes on, the windows go, and a new Xwayland has the same.

    python3 test/wayland_xwm.py WORK XROOT CLIENT

WORK is the work directory (the compositor's log is WORK/compositor.log, its screen image WORK/scan.img, its input FIFO WORK/in.fifo,
its pid WORK/cpid), XROOT the root Xwayland runs in ("" for the host's), CLIENT the Xlib client's path as the root sees it. One line
a row: "ok NAME" or "FAIL NAME ...", and the exit status is the number of failures.
"""
import os
import socket
import struct
import subprocess
import sys
import time

work, xroot, client = sys.argv[1], sys.argv[2], sys.argv[3]
failures = 0
WIDTH, HEIGHT = 800, 600
ENV = ["HOME=/tmp", "XDG_RUNTIME_DIR=/run", "PATH=/usr/local/bin:/usr/bin:/bin", "LANG=C.UTF-8"]
WM = "/run/moonwater/xwm/wm-0"
LOG = os.path.join(work, "compositor.log")
BLUE = bytes((0xcc, 0x66, 0x33))   # the client's colour, 0x3366cc, in the screen's byte order either way round
GREEN = bytes((0x22, 0xaa, 0x22))  # the popup's, 0x22aa22
EV_SYN, EV_KEY, EV_REL = 0, 1, 2
KEY_A, KEY_LEFTALT, KEY_F4, BTN_LEFT = 30, 56, 62, 272
clock = [1_000_000_000]


def row(ok, name, detail=""):
    global failures
    print(("ok " if ok else "FAIL ") + name + ("" if ok else " " + detail), flush=True)
    failures += 0 if ok else 1


def inside(args):
    return (["chroot", xroot] + args) if xroot else args


def read(path):
    try:
        with open(path, errors="replace") as f:
            return f.read()
    except OSError:
        return ""


def wait_for(path, text, timeout=8.0, start=0):
    end = time.time() + timeout
    while time.time() < end:
        if text in read(path)[start:]:
            return True
        time.sleep(0.05)
    return False


def alive():
    try:
        os.kill(int(read(os.path.join(work, "cpid")).strip()), 0)
        return True
    except (OSError, ValueError):
        return False


# ---- the screen: the pixels of a colour, and where they are ----
def screen():
    try:
        with open(os.path.join(work, "scan.img"), "rb") as f:
            return f.read()
    except OSError:
        return b""


def colour(raw, want):
    """Count and bounding box (x1, y1, x2, y2) of the pixels whose three colour bytes are the colour's, in either order."""
    if len(raw) < WIDTH * HEIGHT * 4:
        return 0, None
    key = sorted(want)
    count, x1, y1, x2, y2 = 0, WIDTH, HEIGHT, -1, -1
    for y in range(HEIGHT):
        line = raw[y * WIDTH * 4:(y + 1) * WIDTH * 4]
        for x in range(WIDTH):
            if sorted(line[x * 4:x * 4 + 3]) == key:
                count += 1
                x1, y1, x2, y2 = min(x1, x), min(y1, y), max(x2, x), max(y2, y)
    return count, ((x1, y1, x2, y2) if count else None)


def wait_colour(want, test, timeout=6.0):
    end = time.time() + timeout
    last = (0, None)
    while time.time() < end:
        last = colour(screen(), want)
        if test(*last):
            return True, last
        time.sleep(0.25)
    return False, last


def size_is(box, w, h, slack=6):
    return box is not None and abs(box[2] - box[0] + 1 - w) <= slack and abs(box[3] - box[1] + 1 - h) <= slack


# ---- the compositor's input: evdev records written to its FIFO ----
fifo = None


def record(kind, code, value):
    clock[0] += 20_000
    return struct.pack("qqHHi", clock[0] // 1_000_000, clock[0] % 1_000_000, kind, code, value)


def send(*records):
    os.write(fifo, b"".join(records) + record(EV_SYN, 0, 0))


def key(code, hold=()):
    for h in hold:
        send(record(EV_KEY, h, 1))
    send(record(EV_KEY, code, 1))
    time.sleep(0.05)
    send(record(EV_KEY, code, 0))
    for h in hold:
        send(record(EV_KEY, h, 0))
    time.sleep(0.05)


def move(dx, dy):
    send(record(EV_REL, 0, dx), record(EV_REL, 1, dy))
    time.sleep(0.05)


# ---- a server that lies, on the window manager's socket ----
def setup_reply():
    """A setup reply as the codec's checks lay one out: Xwayland, one screen, depth 24 with one visual and depth 32 with none."""
    b = bytearray(144)

    def u(off, v, n):
        b[off:off + n] = v.to_bytes(n, "little")

    b[0] = 1
    u(2, 11, 2); u(4, 0, 2); u(6, 34, 2); u(8, 11, 4); u(12, 0x200000, 4); u(16, 0x1fffff, 4); u(20, 256, 4)
    u(24, 8, 2); u(26, 65535, 2); b[28] = 1; b[29] = 2; b[32] = 32; b[33] = 32; b[34] = 8; b[35] = 255
    b[40:48] = b"Xwayland"
    b[48] = 24; b[49] = 32; b[50] = 32; b[56] = 32; b[57] = 32; b[58] = 32
    u(64, 0x5a1, 4); u(68, 0x20, 4); u(72, 0xffffff, 4); u(80, 0x1a0000, 4); u(84, 800, 2); u(86, 600, 2); u(88, 508, 2)
    u(90, 285, 2); u(92, 1, 2); u(94, 1, 2); u(96, 0x21, 4); b[102] = 24; b[103] = 2
    b[104] = 24; u(106, 1, 2); u(112, 0x21, 4); b[116] = 4; b[117] = 8; u(118, 256, 2); u(120, 0xff0000, 4); u(124, 0xff00, 4)
    u(128, 0xff, 4)
    b[136] = 32
    return bytes(b)


def liar(name, steps, closed_by_us=False):
    """Connects as the server, takes the compositor's setup request, sends the steps (bytes, or a pause), and says whether the
    compositor dropped the connection: its end ended while we waited, or (closed_by_us) it was ended from here and the compositor
    answered in its log."""
    mark = len(read(LOG))
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(3.0)
    s.connect(WM)
    try:
        request = s.recv(12)
    except OSError:
        request = b""
    ok = len(request) == 12 and request[0] == 0x6c and request[2:4] == b"\x0b\x00"
    dropped = False
    try:
        for step in steps:
            if isinstance(step, bytes):
                s.sendall(step)
            else:
                time.sleep(step)
        if closed_by_us:
            s.close()
            dropped = wait_for(LOG, "the connection ended", 4.0, mark)
        else:
            s.settimeout(4.0)
            try:
                while True:
                    data = s.recv(4096)
                    if not data:
                        dropped = True
                        break
            except OSError:
                dropped = False
    except OSError:
        dropped = True
    finally:
        s.close()
    row(ok and dropped and alive(), name, f"request {request!r} dropped {dropped} alive {alive()}")


fifo_path = os.path.join(work, "in.fifo")
fifo = os.open(fifo_path, os.O_WRONLY)

row(os.stat(WM).st_mode & 0o777 == 0o600 and os.stat(os.path.dirname(WM)).st_mode & 0o777 == 0o700,
    "the window manager's socket is mode 0600 in a directory of mode 0700")
liar("a setup reply cut short, then the connection ended, is dropped", [setup_reply()[:5], 0.2], closed_by_us=True)
liar("a setup reply whose length is over the cap is dropped", [b"\x01\x00\x0b\x00\x00\x00\xff\xff" + bytes(64)])
liar("a setup refused by the server is dropped", [b"\x00\x05\x0b\x00\x00\x00\x02\x00" + b"refused!" + bytes(4)])
garbage = bytearray(setup_reply())
garbage[28] = 200  # more screens than the cap
liar("a setup reply that is no setup is dropped", [bytes(garbage)])
good = setup_reply()
liar("a reply whose length would be 4 gigabytes is dropped", [good, b"\x01\x00\x05\x00\xff\xff\xff\xff" + bytes(24)])
liar("a ClientMessage of a format that is none is dropped", [good, b"\x21\x07\x00\x00" + bytes(28)])
liar("an error answering the root's events (another window manager) is dropped", [good, b"\x00\x0a\x01\x00" + bytes(28)])
liar("an event cut short, then the connection ended, is dropped", [good, b"\x14\x00\x00\x00\x01\x00", 0.2], closed_by_us=True)
# A second connection while one is live is refused, and the first is not hurt by it.
one = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
one.settimeout(3.0)
one.connect(WM)
one.recv(12)
two = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
two.settimeout(3.0)
two.connect(WM)
try:
    refused = two.recv(12) == b""
except OSError:
    refused = False
row(refused and alive(), "a second connection while one is live is refused")
one.close()
two.close()
time.sleep(0.3)

# ---- Xwayland, with its window manager's connection ----
def start_xwayland(display, tag):
    wm = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    wm.connect(WM)
    fd = wm.detach()
    proc = subprocess.Popen(
        inside(["/usr/bin/env", "-i"] + ENV + ["MOONWATER_ASKED=1", "WAYLAND_DISPLAY=/run/moonwater/wayland-0", "WAYLAND_DEBUG=1",
                                               "/usr/bin/Xwayland", display, "-rootless", "-wm", str(fd), "-nolisten", "local",
                                               "-noreset", "-verbose", "3"]),
        pass_fds=[fd], stdout=open(os.path.join(work, f"xwayland{tag}.log"), "w"), stderr=subprocess.STDOUT)
    os.close(fd)
    return proc


def start_client(display, tag, size="300 200"):
    w, h = size.split()
    log = os.path.join(work, f"client{tag}.log")
    proc = subprocess.Popen(
        inside(["/usr/bin/env", "-i"] + ENV + [f"DISPLAY={display}", client, "-w", w, "-h", h, "-t", "60"]),
        stdin=subprocess.PIPE, stdout=open(log, "w"), stderr=subprocess.STDOUT)
    return proc, log


def command(proc, text):
    proc.stdin.write((text + "\n").encode())
    proc.stdin.flush()


mark = len(read(LOG))
xwayland = start_xwayland(":1", "")
row(wait_for(LOG, "is connected", 8.0, mark), "the window manager of Xwayland is connected", read(LOG)[-200:])
row(wait_for(LOG, "redirected", 8.0, mark), "the root's subwindows are redirected (Composite, Manual)", read(LOG)[-200:])

xclient, client_log = start_client(":1", "")
xwayland_log = os.path.join(work, "xwayland.log")
row(wait_for(client_log, "expose"), "a mapped X window is drawn: it gets its Expose", read(client_log)[-200:])
row(wait_for(xwayland_log, "get_xwayland_surface"), "the shim makes the window its surface (get_xwayland_surface)", read(xwayland_log)[-300:])
row(wait_for(xwayland_log, ".attach(wl_buffer"), "the window's surface takes a buffer", read(xwayland_log)[-300:])

ok, (count, box) = wait_colour(BLUE, lambda c, b: c >= 0.9 * 300 * 200 and size_is(b, 300, 200))
row(ok, "the X window is a window of the compositor: its colour is on the screen at its size", f"{count} pixels, box {box}")
title = b"moonwater xwm client\0"
table = b""
try:
    with open("/run/moonwater/windows", "rb") as f:
        table = f.read()
except OSError:
    pass
row(title in table, "its title is in the compositor's window table", f"table {len(table)} bytes")
home = box

# Keys: the first window of a client the person asked for has the keys, and X focus is the window manager's.
key(KEY_A)
row(wait_for(client_log, "key 61"), "a typed key reaches the X window by its keysym", read(client_log)[-200:])

# The pointer: to the middle of the window, then moved; the coordinates the client reads are the output's.
if home:
    cx, cy = (home[0] + home[2]) // 2, (home[1] + home[3]) // 2
    move(-3000, -3000)
    move(cx, cy)
    time.sleep(0.4)
    before = len([l for l in read(client_log).splitlines() if l.startswith("motion")])
    for _ in range(6):
        move(8, 4)
    time.sleep(0.5)
    motions = [l.split() for l in read(client_log).splitlines() if l.startswith("motion")][before:]
    xs = [int(m[1]) for m in motions]
    ys = [int(m[2]) for m in motions]
    row(len(xs) >= 5 and all(b >= a for a, b in zip(xs, xs[1:])) and xs[-1] - xs[0] >= 16,
        "relative motion reaches the X window", f"motions {motions[-4:]}")
    row(bool(xs) and home[0] <= xs[-1] <= home[2] and home[1] <= ys[-1] <= home[3],
        "the root coordinates the client reads are inside the window's place on the output", f"last {xs[-1:]} {ys[-1:]} window {home}")
    send(record(EV_KEY, BTN_LEFT, 1))
    time.sleep(0.05)
    send(record(EV_KEY, BTN_LEFT, 0))
    buttons = [l.split() for l in read(client_log).splitlines() if l.startswith("button")]
    row(wait_for(client_log, "button 1 ") and bool(xs), "a click reaches the X window", read(client_log)[-200:])
else:
    row(False, "relative motion reaches the X window", "no window to move to")

# The client asks for a size: the window manager honours it, and the window on the screen is that size.
command(xclient, "resize 400 250")
row(wait_for(client_log, "configure 400 250"), "a size the client asks for (ConfigureRequest) is given it", read(client_log)[-200:])
ok, (count, box) = wait_colour(BLUE, lambda c, b: c >= 0.9 * 400 * 250 and size_is(b, 400, 250))
row(ok, "the window on the screen is the size it was given", f"{count} pixels, box {box}")

# An override-redirect window is a popup where it asks, over the window.
command(xclient, "popup")
ok, (count, box) = wait_colour(GREEN, lambda c, b: c >= 0.9 * 120 * 80 and size_is(b, 120, 80, 4))
row(ok and box and abs(box[0] - 100) <= 3 and abs(box[1] - 100) <= 3, "an override-redirect window is a popup at the place it asks",
    f"{count} pixels, box {box}")

# Fullscreen: the request fills the output, and the reverse gives the size back.
command(xclient, "fullscreen")
row(wait_for(client_log, "configure 800 600"), "a fullscreen request makes the X window the output's size", read(client_log)[-200:])
ok, (count, box) = wait_colour(BLUE, lambda c, b: c >= 0.9 * WIDTH * HEIGHT)
row(ok, "the screen shows the X window's colour over the whole output", f"{count} pixels, box {box}")
command(xclient, "unfullscreen")
ok, (count, box) = wait_colour(BLUE, lambda c, b: size_is(b, 400, 250) and c >= 0.8 * 400 * 250, 8.0)
row(ok, "leaving fullscreen gives the window its size back", f"{count} pixels, box {box}")

# Close: Alt-F4 is the compositor's, and it reaches the client as WM_DELETE_WINDOW.
key(KEY_F4, hold=(KEY_LEFTALT,))
row(wait_for(client_log, "delete"), "closing the window sends the client WM_DELETE_WINDOW", read(client_log)[-200:])
time.sleep(0.5)
xclient.kill()

# ---- Xwayland killed ----
xclient, client_log = start_client(":1", "-b")
ok, (count, box) = wait_colour(BLUE, lambda c, b: c >= 0.9 * 300 * 200)
row(ok, "a window is on the screen before Xwayland is killed", f"{count} pixels, box {box}")
mark = len(read(LOG))
xwayland.kill()
xwayland.wait()
row(wait_for(LOG, "window manager connection is closed", 5.0, mark), "the window manager's connection ends with Xwayland", read(LOG)[mark:][-200:])
ok, (count, box) = wait_colour(BLUE, lambda c, b: c == 0, 6.0)
row(ok and alive(), "the compositor goes on and the X windows are gone from the screen", f"{count} pixels, alive {alive()}")
xclient.kill()

# A new Xwayland, with its own display, connects and has windows again.
mark = len(read(LOG))
xwayland = start_xwayland(":2", "2")
row(wait_for(LOG, "redirected", 8.0, mark), "a new Xwayland's window manager connects and redirects the root", read(LOG)[mark:][-200:])
xclient, client_log = start_client(":2", "2")
ok, (count, box) = wait_colour(BLUE, lambda c, b: c >= 0.9 * 300 * 200 and size_is(b, 300, 200))
row(ok, "a window of the new Xwayland is on the screen", f"{count} pixels, box {box}")

xclient.kill()
xwayland.terminate()
xwayland.wait()
os.close(fifo)
print(f"{failures} failed", flush=True)
sys.exit(failures)
