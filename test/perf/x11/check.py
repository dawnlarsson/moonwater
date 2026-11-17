"""check.py (ember, round 2): ground truth for the X11 and GL cases, from the SCANOUTS (scan-dump.sh), never from `canvas shot`
(on the processor path the shot disagrees with the screen: FINDINGS, coordinator 20:10).

    python3 -I check.py x11 WINDOWS SCREEN_W SCREEN_H DUMP...   the final picture of xclient.py's window "xtest" (640x400)
    python3 -I check.py gl  WINDOWS SCREEN_W LOG DUMP...        the frame counter xgl.py draws, against the last frame it printed
    python3 -I check.py keys KEYS_TXT                          `canvas keys` stages for the keys typed into the xgl window (p50/p90 us)

WINDOWS is `moonwater window`'s output saved to a file. Prints "METRIC VALUE" lines: the worst dump counts (both scanout buffers must be
right). x11: wrong_pixels (the picture's pixels other than the window's rounded corners, which the compositor masks) and wrong_rows.
gl: counter_on_screen, stale_frames (the last frame printed minus the one on screen; 0 when the screen shows the final frame).
"""
import os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from xclient import final_rects, W, H  # noqa: E402
from xgl import CELLS, CELL_W, CELL_H  # noqa: E402

CORNER = 12  # the compositor's rounded window corners: not the picture


def place(text, title, w, h):
    m = re.search(r"at (\d+),(\d+), %d by %d: %s" % (w, h, re.escape(title)), text)
    if not m:
        sys.exit("no window %s %dx%d" % (title, w, h))
    return int(m.group(1)), int(m.group(2))


def x11(windows, sw, sh, dumps):
    wx, wy = place(open(windows).read(), "xtest", W, H)
    want = {}
    for rx, ry, rw, rh, c in final_rects():
        for yy in range(ry, min(H, ry + rh)):
            for xx in range(rx, min(W, rx + rw)):
                want[(xx, yy)] = ((c >> 16) & 255, (c >> 8) & 255, c & 255)
    worst_px, worst_rows = 0, 0
    for d in dumps:
        data = open(d, "rb").read()
        px, rows = 0, set()
        for yy in range(H):
            for xx in range(W):
                if min(xx, W - 1 - xx) < CORNER and min(yy, H - 1 - yy) < CORNER:
                    continue
                o = ((wy + yy) * sw + wx + xx) * 4
                if (data[o + 2], data[o + 1], data[o]) != want.get((xx, yy), (0x20, 0x20, 0x20)):
                    px += 1
                    rows.add(yy)
        worst_px, worst_rows = max(worst_px, px), max(worst_rows, len(rows))
    print("wrong_pixels %d" % worst_px)
    print("wrong_rows %d" % worst_rows)


def gl(windows, sw, log, dumps):
    wx, wy = place(open(windows).read(), "xgl", 480, 240)
    frames = [int(v) for v in re.findall(r"^frame (\d+) ", open(log).read(), re.M)]
    last = frames[-1] if frames else 0
    stale = 0
    shown = None
    for d in dumps:
        data = open(d, "rb").read()
        value = 0
        for c in range(CELLS):
            o = ((wy + CELL_H // 2) * sw + wx + c * CELL_W + CELL_W // 2) * 4
            if data[o + 1] > 128:
                value |= 1 << c
        shown = value if shown is None else shown
        stale = max(stale, last - value)
    print("counter_on_screen %d" % (shown or 0))
    print("stale_frames %d" % stale)


def keys(path):
    """keys.txt of `canvas keys` (us per stage: code, read, sent, flushed, commit, present, shown, whole): p50/p90 per stage in ms."""
    rows = [[float(v) for v in line.split()] for line in open(path) if line.strip() and not line.startswith("#")]
    rows = [r for r in rows if len(r) == 8]
    print("keys %d" % len(rows))
    if not rows:
        return
    names = ["read", "sent", "flushed", "commit", "present", "shown", "whole"]
    for i, name in enumerate(names):
        v = sorted(r[i + 1] for r in rows)
        p50 = v[len(v) // 2]
        p90 = v[min(len(v) - 1, int(0.9 * len(v)))]
        print("key_%s_p50_us %.1f" % (name, p50))
        print("key_%s_p90_us %.1f" % (name, p90))


if __name__ == "__main__":
    kind = sys.argv[1]
    if kind == "x11":
        x11(sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), sys.argv[5:])
    elif kind == "keys":
        keys(sys.argv[2])
    elif kind == "gl":
        gl(sys.argv[2], int(sys.argv[3]), sys.argv[4], sys.argv[5:])
    else:
        sys.exit(__doc__)
