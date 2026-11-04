#!/usr/bin/env python3
"""
The screen's scale changed while real libwayland clients run (test/wayland_shm.c, built with -DHAVE_SCALE): what the
clients are told, in what order, and what the screen then shows of them.

    python3 wayland_scale.py CLIENT SCAN WORKDIR

The compositor (headless, 800 by 600, in a private /run and /root) reads the scale of its one screen, named Canvas, from the
line `Canvas scale=PERCENT` of /root/canvas.displays, and takes a change at once, as it does on a machine. A client run
with -S draws as a toolkit does: its size is logical, its buffer the size times the scale it was told (a viewport whose
destination is the size, with the fractional scale; or the whole scale with -I, as a client that knows no other), and it
says on standard output every wl_output and xdg_output event, each preferred scale, each configure and each draw. The
window it draws carries three marks of a colour of their own, 16 pixels in from the picture's top left, top right and bottom
right corners, which the screen file is searched for: the window is on the screen at the size the buffer is, or it is not
(the panel at the bottom of the screen covers a window that fills it, so a window that does is judged by its width).

One line a row: "ok NAME" or "FAIL NAME detail"; the exit status is the number of failures.
"""
import os
import subprocess
import sys
import time

client, scan, work = sys.argv[1:4]
SCREEN = (800, 600)
DISPLAYS = "/root/canvas.displays"
MARK_A = b"\x0c\x0b\x0a\xff"  # 0xff0a0b0c in memory
MARK_B = b"\x0a\x0b\x0c\xff"  # 0xff0c0b0a
MARK_C = b"\x0a\x0c\x0b\xff"  # 0xff0b0c0a, 16 in from the top right corner
MARK_IN = 16
failures = 0


def row(ok, name, detail=""):
    global failures
    if ok:
        print("ok " + name)
    else:
        print("FAIL %s %s" % (name, detail))
        failures += 1
    sys.stdout.flush()


def set_scale(percent):
    with open(DISPLAYS, "w") as f:
        f.write("Canvas scale=%d\n" % percent)


class Client:
    """A client that follows the scale, and the lines it has said."""

    def __init__(self, name, *args):
        self.path = os.path.join(work, "scale-%s.log" % name)
        self.out = open(self.path, "w")
        env = dict(os.environ, MOONWATER_ASKED="1")
        self.proc = subprocess.Popen([client, "-S", "-n", "1000000", "-s"] + list(args), stdout=self.out, stderr=subprocess.STDOUT, env=env)

    def lines(self, start=0):
        try:
            return open(self.path).read().splitlines()[start:]
        except OSError:
            return []

    def wait(self, want, start=0, timeout=8.0):
        """The lines from start on, once one is exactly want (or the time is up)."""
        end = time.time() + timeout
        while time.time() < end:
            lines = self.lines(start)
            if want in lines:
                time.sleep(0.4)  # the rest of what the same change sends, and the compositor's next frame
                return self.lines(start)
            time.sleep(0.1)
        return self.lines(start)

    def stop(self):
        self.proc.terminate()
        try:
            self.proc.wait(3)
        except subprocess.TimeoutExpired:
            self.proc.kill()
        self.out.close()


def in_order(lines, wanted):
    """Whether the lines wanted are among lines in that order."""
    at = 0
    for line in lines:
        if at < len(wanted) and line == wanted[at]:
            at += 1
    return at == len(wanted)


def window_on_screen():
    """The size the marked window has on the screen, from its two marks, or None."""
    raw = open(scan, "rb").read()
    a, b = raw.find(MARK_A), raw.find(MARK_B)
    if a < 0 or b < 0:
        return None
    ax, ay = (a // 4) % SCREEN[0], (a // 4) // SCREEN[0]
    bx, by = (b // 4) % SCREEN[0], (b // 4) // SCREEN[0]
    return (bx - ax + 1 + 2 * MARK_IN, by - ay + 1 + 2 * MARK_IN)


def width_on_screen():
    """The width the marked window has on the screen, from its top marks, or None."""
    raw = open(scan, "rb").read()
    a, c = raw.find(MARK_A), raw.find(MARK_C)
    if a < 0 or c < 0:
        return None
    return (c // 4) % SCREEN[0] - (a // 4) % SCREEN[0] + 1 + 2 * MARK_IN


def shown(expected, timeout=4.0):
    end = time.time() + timeout
    size = None
    while time.time() < end:
        size = window_on_screen() if isinstance(expected, tuple) else width_on_screen()
        if size == expected:
            return True, size
        time.sleep(0.2)
    return False, size


def settle(percent):
    set_scale(percent)
    time.sleep(0.6)


# A floating window of 320 by 240 logical units, at the scales the settings offer. The fractional client follows the screen.
settle(100)
c = Client("frac", "-w", "320", "-h", "240")
lines = c.wait("draw 320 240 320 240 120")
row("draw 320 240 320 240 120" in lines, "at 100 percent a following client draws 320 by 240 in 320 by 240 pixels", " / ".join(lines[-4:]))
row("output scale 1" in lines and "preferred_scale 120" in lines and "preferred_buffer_scale 1" in lines and
    "xdg_output size 800 600" in lines, "and was told the screen whole: output scale 1, preferred scale 120, xdg_output 800 by 600",
    " / ".join(lines))
ok, size = shown((320, 240))
row(ok, "and the screen shows it 320 by 240", str(size))

for percent, scale120, whole, logical, buffer in ((150, 180, 2, (533, 400), (480, 360)), (200, 240, 2, (400, 300), (640, 480)),
                                                  (125, 150, 2, (640, 480), (400, 300)), (75, 90, 1, (1066, 800), (240, 180))):
    mark = len(c.lines())
    set_scale(percent)
    draw = "draw 320 240 %d %d %d" % (buffer[0], buffer[1], scale120)
    lines = c.wait(draw, mark)
    row(draw in lines, "at %d percent the following client draws 320 by 240 in %d by %d pixels" % (percent, buffer[0], buffer[1]),
        " / ".join(lines))
    row(in_order(lines, ["xdg_output position 0 0", "xdg_output size %d %d" % logical, "output geometry 0 0", "output mode 800 600",
                         "output scale %d" % whole, "output done", "preferred_scale %d" % scale120, draw]),
        "at %d percent it was told the screen whole and in order: xdg_output, wl_output geometry, mode, scale, done, then the preferred scale" % percent,
        " / ".join(lines))
    ok, size = shown(buffer)
    row(ok, "at %d percent the screen shows it %d by %d, the buffer's size and not resampled" % (percent, buffer[0], buffer[1]), str(size))
c.stop()
row(not any(line == "xdg_output done" for line in c.lines()), "an xdg_output of version 3 is not sent the deprecated done (the wl_output's done is its)")

# A client that knows whole scales only: told 2 at 150 percent, it draws twice the size in twice the pixels.
settle(150)
c = Client("whole", "-I", "-w", "320", "-h", "240")
lines = c.wait("draw 320 240 640 480 240")
row("draw 320 240 640 480 240" in lines and "preferred_buffer_scale 2" in lines and "output scale 2" in lines,
    "a client that knows whole scales is told 2 at 150 percent and draws 320 by 240 in 640 by 480", " / ".join(lines))
ok, size = shown((640, 480))
row(ok, "and the screen shows it 640 by 480", str(size))
mark = len(c.lines())
set_scale(100)
lines = c.wait("draw 320 240 320 240 120", mark)
row("preferred_buffer_scale 1" in lines and "draw 320 240 320 240 120" in lines and "output scale 1" in lines,
    "back at 100 percent it is told 1 and draws 320 by 240 in 320 by 240", " / ".join(lines))
ok, size = shown((320, 240))
row(ok, "and the screen shows it 320 by 240", str(size))
c.stop()

# A client that connects after the scale changed is told it at once.
settle(150)
c = Client("late", "-w", "320", "-h", "240")
lines = c.wait("draw 320 240 480 360 180")
row("output scale 2" in lines and "preferred_scale 180" in lines and "draw 320 240 480 360 180" in lines and
    "draw 320 240 320 240 120" not in lines, "a client that connects at 150 percent is told 180 and draws 480 by 360 at once", " / ".join(lines))
c.stop()

# A maximized window: the compositor asks for physical pixels, which the client is told in its units, and what it draws fills them.
settle(100)
c = Client("max", "-m", "-w", "320", "-h", "240")
time.sleep(2.0)
lines = c.lines()
big = [l for l in lines if l.startswith("configure ") and l != "configure 0 0"]
row(bool(big), "a maximized following client is configured to a size", " / ".join(lines))
if big:
    w100, h100 = [int(v) for v in big[-1].split()[1:]]
    mark = len(c.lines())
    set_scale(150)
    time.sleep(2.0)
    lines = c.lines(mark)
    configures = [l for l in lines if l.startswith("configure ") and l != "configure 0 0"]
    row(bool(configures), "and at 150 percent it is configured again, in the new unit", " / ".join(lines))
    if configures:
        w150, h150 = [int(v) for v in configures[-1].split()[1:]]
        # The same pixels, a third fewer units: floor(w100 * 120 / 180), not the pixels again.
        row(abs(w150 * 3 - w100 * 2) <= 2 and abs(h150 * 3 - h100 * 2) <= 2,
            "and the size is two thirds of what it was (%dx%d units at 100, %dx%d at 150)" % (w100, h100, w150, h150))
        draws = [l for l in lines if l.startswith("draw ")]
        last = draws[-1].split() if draws else []
        row(bool(last) and int(last[1]) == w150 and int(last[2]) == h150 and abs(int(last[3]) - w150 * 3 // 2) <= 1,
            "and it draws that in 1.5 pixels a unit", " / ".join(draws[-2:]))
        ok, size = shown(int(last[3])) if last else (False, None)
        row(ok, "and the screen shows it as wide as the pixels it drew, filling what the window filled at 100 percent", str(size))
c.stop()

# A floating window that stays its size in its units is larger in pixels at a larger scale: past the screen's room, the
# compositor asks it to fit, in the units the client was told.
settle(100)
c = Client("big", "-w", "700", "-h", "500")
lines = c.wait("draw 700 500 700 500 120")
row("draw 700 500 700 500 120" in lines, "a window of 700 by 500 units on a screen of 800 by 600 pixels fits at 100 percent", " / ".join(lines))
mark = len(c.lines())
set_scale(150)
time.sleep(2.0)
lines = c.lines(mark)
draws = [l.split() for l in lines if l.startswith("draw ")]
last = [int(v) for v in draws[-1][1:5]] if draws else []
row(bool(last) and last[2] <= SCREEN[0] and last[3] <= SCREEN[1] and last[0] * 3 // 2 - last[2] in (-1, 0, 1),
    "at 150 percent it is asked to fit the screen, and draws what fits in 1.5 pixels a unit (last draw %s)" % (last,), " / ".join(lines))
ok, size = shown((last[2], last[3])) if last else (False, None)
row(ok, "and the screen shows it, whole, at that size", str(size))
c.stop()

settle(100)
sys.exit(failures)
