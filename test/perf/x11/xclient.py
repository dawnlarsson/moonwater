"""xclient.py [SECONDS]: an X11 window (libX11 through ctypes) for Xwayland's damage and key path, run by the bowl's X wrapper:

    xwayland-moonwater python3 /tmp/cperf/xclient.py 3      (bowl python; DISPLAY set by the wrapper)

It draws a blue square that moves for SECONDS (every frame damages two small rectangles), then the FINAL picture (final_rects(), the same
function xcheck.py uses) and stays. Each key press paints a bar in the bottom strip (never a letter: keys are not read as text). It prints
"ready WINDOW" once mapped and "key N AT_MS" per key on the monotonic clock. The final picture is what xcheck.py compares with the screen.
"""
import ctypes, sys, time

W, H = 640, 400
TITLE = b"xtest"
BG = 0x202020
KEY_PRESS, EXPOSE = 2, 12


def final_rects():
    """The final picture: the background and forty rectangles at fixed places and colours (xcheck.py recomputes them)."""
    out = [(0, 0, W, H, BG)]
    for i in range(40):
        out.append(((i * 37 * 7) % (W - 60), (i * 53 * 11) % (H - 60), 20 + (i * 13) % 60, 10 + (i * 7) % 40, (i * 0x0A3B7D) & 0xFFFFFF))
    return out


def main():
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 3.0
    x = ctypes.CDLL("libX11.so.6")
    x.XOpenDisplay.restype = ctypes.c_void_p
    x.XCreateGC.restype = ctypes.c_void_p
    d = x.XOpenDisplay(None)
    if not d:
        sys.exit("xclient: no display")
    scr = x.XDefaultScreen(ctypes.c_void_p(d))
    root = x.XRootWindow(ctypes.c_void_p(d), scr)
    win = x.XCreateSimpleWindow(ctypes.c_void_p(d), root, 100, 100, W, H, 0, 0, BG)
    x.XStoreName(ctypes.c_void_p(d), win, TITLE)
    x.XSelectInput(ctypes.c_void_p(d), win, ctypes.c_long((1 << 15) | (1 << 0) | (1 << 17)))  # Exposure, KeyPress, StructureNotify
    x.XMapWindow(ctypes.c_void_p(d), win)
    gc = ctypes.c_void_p(x.XCreateGC(ctypes.c_void_p(d), win, 0, None))  # a 64-bit pointer: never a plain int
    dpy = ctypes.c_void_p(d)
    fill = lambda rx, ry, rw, rh, c: (x.XSetForeground(dpy, gc, ctypes.c_ulong(c)), x.XFillRectangle(dpy, win, gc, rx, ry, rw, rh))
    x.XFlush(dpy)
    print("ready %d" % win, flush=True)
    end = time.monotonic() + seconds
    px, py, last = 0, 0, None
    while time.monotonic() < end:
        if last is not None:
            fill(last[0], last[1], 60, 60, BG)
        px = (px + 7) % (W - 60)
        py = (py + 5) % (H - 180)
        fill(px, py, 60, 60, 0x3366CC)
        last = (px, py)
        x.XFlush(dpy)
        time.sleep(0.016)
    for rx, ry, rw, rh, c in final_rects():
        fill(rx, ry, rw, rh, c)
    x.XFlush(dpy)
    print("final", flush=True)
    keys = 0
    ev = ctypes.create_string_buffer(256)
    while True:
        if not x.XPending(dpy):
            time.sleep(0.002)
            continue
        x.XNextEvent(dpy, ev)
        kind = ctypes.c_int.from_buffer(ev).value
        if kind == EXPOSE:
            for rx, ry, rw, rh, c in final_rects():
                fill(rx, ry, rw, rh, c)
            x.XFlush(dpy)
        elif kind == KEY_PRESS:
            keys += 1
            fill((keys * 13) % (W - 12), H - 30, 12, 30, (0x00FF00 if keys % 2 else 0xFF8800))
            x.XFlush(dpy)
            print("key %d %.3f" % (keys, time.monotonic() * 1000), flush=True)


if __name__ == "__main__":
    main()
