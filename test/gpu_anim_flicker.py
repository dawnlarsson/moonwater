#!/usr/bin/env python3
# The animations of the desktop, as the screen shows them: the headless compositor's scan image sampled as fast as it is written
# (test/gpu_scan_sample.c, started by test/gpu_anim_flicker.sh), and each animation step driven over the compositor's evdev fifo (-i),
# the records a device writes: struct timeval, type, code, value. The steps are the ones a person sees move: a launcher's pop-up, a window
# dragged by its top band, a window resized by its edge, a minimize and a restore, a tab hovered, a list flicked and stretched past its end,
# a toggle clicked (its ripple), a field typed into (the caret fades), a pop-up menu opened.
#
#     gpu_anim_flicker.py drive OUTDIR FIFO CANVAS_TXT     (writes OUTDIR/steps.tsv: label, start and end in monotonic nanoseconds)
#     gpu_anim_flicker.py classify SAMPLES STEPS          (prints one line a step, and the totals)
#
# SAMPLES is the sampler's log: records of a uint64 monotonic time, a uint32 frame number, a uint32 row count and one uint64 hash a row.
# The classification of a distinct frame S, with P the frame sampled before it and N the one after (both in the whole log):
#   torn     every row of S is a row of P or of N, both kinds present, and S is neither: a band written before the read and the rest after
#   blank    S matches an all-zero (buffer 0) frame or flashes zeroed rows transiently while P and N are non-zero at those rows
#   foreign  a row of S that equals the row of P and of N in its place (they agree, S differs) and whose value is seen in no other frame of the
#            log at that row: a colour that exists in no frame (a half-composed or cleared window, a wrong scaled picture)
#   edge     a row of S that equals neither P nor N while P[r] == N[r], repeated at least twice in the log (a transient edge/blend glitch or
#            recurring halo outside filter-caret fade)
#   flipflop S equal to the frame two before it, and different from the one before: a state that goes away and comes back
# Legitimate animation changes rows that P and N disagree on, so none of the three counts a moving edge; a card session's counts are compared
# with the processor's (canvas gpu off), which draws the same animation.
import os, re, struct, sys, time

W, H = 800, 600
EV_SYN, EV_KEY, EV_REL = 0, 1, 2
REL_X, REL_Y, REL_WHEEL = 0, 1, 8
BTN_LEFT = 0x110
KEY = {'d': 32, 'i': 23, 's': 31, 'p': 25, 'l': 38, 'a': 30, 'y': 21, 'n': 49, 'e': 18, 'r': 19, 'o': 24, 'k': 37, 'c': 46}


def _zero_row_hash():
    """Exact gpu_scan_sample.c row_hash() digest for W*4 zero bytes."""
    h = 1469598103934665603
    mask = (1 << 64) - 1
    row_bytes = W * 4
    for _ in range(row_bytes // 8 + row_bytes % 8):
        h = (h * 1099511628211) & mask
    return h


ZERO_ROW = _zero_row_hash()
CARET_MAX_ROWS = 36
CARET_MIN_INTERVAL_NS = 120_000_000


def now_ns():
    return time.monotonic_ns()


def send(fd, *events):
    out = b""
    for kind, code, value in events:
        now = time.time()
        out += struct.pack("qqHHi", int(now), int((now - int(now)) * 1000000), kind, code, value)
    now = time.time()
    out += struct.pack("qqHHi", int(now), int((now - int(now)) * 1000000), EV_SYN, 0, 0)
    os.write(fd, out)


class Pointer:
    def __init__(self, fd, x=W // 2, y=H // 2):
        self.fd, self.x, self.y = fd, x, y

    def move(self, x, y, pause=0.004):
        x, y = max(0, min(W - 1, x)), max(0, min(H - 1, y))
        dx, dy = x - self.x, y - self.y
        steps = [(EV_REL, REL_X, 1 if dx > 0 else -1)] * abs(dx) + [(EV_REL, REL_Y, 1 if dy > 0 else -1)] * abs(dy)
        for i in range(0, len(steps), 16):
            send(self.fd, *steps[i:i + 16])
            time.sleep(pause)
        self.x, self.y = x, y

    def press(self):
        send(self.fd, (EV_KEY, BTN_LEFT, 1))

    def release(self):
        send(self.fd, (EV_KEY, BTN_LEFT, 0))

    def click(self):
        self.press()
        time.sleep(0.05)
        self.release()

    def wheel(self, notches, pause=0.03):
        for _ in range(abs(notches)):
            send(self.fd, (EV_REL, REL_WHEEL, 1 if notches > 0 else -1))
            time.sleep(pause)

    def type(self, text):
        for ch in text:
            code = KEY[ch]
            send(self.fd, (EV_KEY, code, 1))
            send(self.fd, (EV_KEY, code, 0))
            time.sleep(0.03)


def windows(canvas_txt):
    # "  window 1        settings at 0,0, 800x600" (moonwater debug): the rectangles of the windows, by title
    found = []
    with open(canvas_txt, errors="replace") as f:
        for line in f:
            m = re.match(r"^  window\s+(\d+)\s+(\S.*?)\s+at (-?\d+),(-?\d+), (\d+)x(\d+)", line)
            if m:
                found.append({"row": int(m.group(1)), "title": m.group(2), "x": int(m.group(3)), "y": int(m.group(4)),
                              "w": int(m.group(5)), "h": int(m.group(6))})
    return found


def drive(out, fifo, canvas_txt):
    fd = os.open(fifo, os.O_WRONLY)
    pointer = Pointer(fd)
    wins = windows(canvas_txt)
    settings = next((w for w in wins if "ettings" in w["title"]), wins[0] if wins else {"x": 0, "y": 0, "w": W, "h": H})
    mixer = next((w for w in wins if "ixer" in w["title"]), None)
    sx, sy, sw, sh = settings["x"], settings["y"], settings["w"], settings["h"]
    steps = []

    def step(label, action):
        steps.append((label, action))

    # hover the sidebar rows, as the detector does, then the list
    step("hover sidebar", lambda: [pointer.move(sx + 100, sy + 156)])
    step("hover list row", lambda: pointer.move(sx + 400, sy + 200))
    step("click toggle (ripple)", lambda: (pointer.move(sx + 400, sy + 200), pointer.click()))
    # the flick: a burst of notches over the list, then back past the top (the overscroll stretch)
    step("flick list down", lambda: (pointer.move(sx + 400, sy + 300), pointer.wheel(-6, 0.01)))
    step("overscroll past top", lambda: pointer.wheel(9, 0.01))
    # the filter field: typing (the caret fades)
    step("click filter", lambda: (pointer.move(sx + 590, sy + 68), pointer.click()))
    step("type into filter", lambda: pointer.type("disp"))
    # the window: dragged by its top band, then resized by its right edge, then back
    step("drag window by top band", lambda: (pointer.move(sx + 200, sy + 8), pointer.press(), pointer.move(sx + 260, sy + 40, 0.006), pointer.release()))
    step("resize window by right edge", lambda: (pointer.move(sx + sw - 2, sy + sh // 2), pointer.press(), pointer.move(sx + sw - 60, sy + sh // 2, 0.006), pointer.release()))
    if mixer:
        step("drag second window", lambda: (pointer.move(mixer["x"] + 120, mixer["y"] + 8), pointer.press(), pointer.move(mixer["x"] + 80, mixer["y"] + 30, 0.006), pointer.release()))
    step("hover the bottom edge (tabs)", lambda: pointer.move(W // 2, H - 4))
    step("move away", lambda: pointer.move(W // 2, H // 2))
    marks = []
    time.sleep(0.8)
    for label, action in steps:
        time.sleep(0.45)
        start = now_ns()
        action()
        time.sleep(0.7)
        marks.append((label, start, now_ns()))
        print("step %s at %d..%d" % (label, start, now_ns()), flush=True)
    with open(os.path.join(out, "steps.tsv"), "w") as f:
        for label, start, end in marks:
            f.write("%s\t%d\t%d\n" % (label, start, end))
    os.close(fd)


def load(samples):
    frames = []
    with open(samples, "rb") as f:
        data = f.read()
    at = 0
    while at + 16 <= len(data):
        t, seq, rows = struct.unpack_from("<QII", data, at)
        at += 16
        hashes = struct.unpack_from("<%dQ" % rows, data, at)
        at += 8 * rows
        frames.append((t, hashes))
    return frames


def classify(samples, steps):
    frames = load(samples)
    n = len(frames)
    click_filter_step = next((i for i, (lbl, _, _) in enumerate(steps) if "filter" in lbl), -1)
    scroll_ranges = [
        (start, end) for lbl, start, end in steps
        if "flick" in lbl or "overscroll" in lbl
    ]

    def in_step_list(ts, ranges):
        return any(s <= ts <= e for s, e in ranges)

    # the frames' value at each row, by row: a value seen in no other frame of the log at its row is 'unseen'
    seen = [dict() for _ in range(H)]
    for k, (t, rows) in enumerate(frames):
        for r, v in enumerate(rows):
            seen[r].setdefault(v, set()).add(k)
    torn = [False] * n
    blank = [False] * n
    blank_rows = [0] * n
    foreign = [0] * n
    edge = [0] * n
    edge_bad = [0] * n
    flip = [False] * n
    flip_bad = [False] * n

    filter_start = steps[click_filter_step][1] if click_filter_step >= 0 else None

    for k in range(n):
        t, s = frames[k]
        if len(s) == H and all(v == ZERO_ROW for v in s):
            blank[k] = True
        if 1 <= k < n - 1:
            p, q = frames[k - 1][1], frames[k + 1][1]
            quick_q = (frames[k + 1][0] - t) < 45_000_000
            if quick_q and s != p and s != q and p != q:
                has_p = has_q = False
                ok = True
                for r in range(H):
                    if s[r] == p[r]:
                        has_p = True
                    elif s[r] == q[r]:
                        has_q = True
                    else:
                        ok = False
                        break
                torn[k] = ok and has_p and has_q

            b_cnt = 0
            f_cnt = 0
            e_rows = []
            for r in range(H):
                if not blank[k] and s[r] == ZERO_ROW and p[r] != ZERO_ROW and q[r] != ZERO_ROW:
                    b_cnt += 1
                if p[r] == q[r] and s[r] != p[r]:
                    if len(seen[r].get(s[r], ())) == 1:
                        f_cnt += 1
                    else:
                        e_rows.append(r)
            blank_rows[k] = b_cnt
            foreign[k] = f_cnt

            # Ignore small localized filter-caret fade turnarounds and single-cursor-block cross-step passes.
            if e_rows:
                span = max(e_rows) - min(e_rows)
                dt_prev = t - frames[k - 1][0]
                caret_like = (
                    filter_start is not None
                    and t >= filter_start
                    and len(e_rows) <= CARET_MAX_ROWS
                    and span < CARET_MAX_ROWS * 2
                    and dt_prev >= CARET_MIN_INTERVAL_NS
                )
                if not caret_like:
                    edge[k] = len(e_rows)
                    cur_step = next(((s_t, e_t) for _, s_t, e_t in steps if s_t <= t <= e_t), None)
                    same_step_repeat = cur_step is not None and any(
                        any(k2 != k and cur_step[0] <= frames[k2][0] <= cur_step[1] for k2 in seen[r].get(s[r], ()))
                        for r in e_rows
                    )
                    if not in_step_list(t, scroll_ranges) and (len(e_rows) > 40 or span >= 40 or same_step_repeat):
                        edge_bad[k] = len(e_rows)

        if k >= 2 and s == frames[k - 2][1] and s != frames[k - 1][1]:
            flip[k] = True
            diff_rows = [r for r in range(H) if s[r] != frames[k - 1][1][r]]
            dt = t - frames[k - 1][0]
            caret_flip = (
                filter_start is not None
                and t >= filter_start
                and 0 < len(diff_rows) <= CARET_MAX_ROWS
                and max(diff_rows) - min(diff_rows) < CARET_MAX_ROWS * 2
                and dt >= CARET_MIN_INTERVAL_NS
            )
            cross_step = any(
                start <= t <= end and frames[k - 1][0] < start
                for _, start, end in steps
            )
            if not caret_flip and not cross_step and not in_step_list(t, scroll_ranges):
                flip_bad[k] = True

    rows = []
    for label, start, end in steps:
        ks = [k for k in range(n) if start <= frames[k][0] <= end]
        if not ks:
            rows.append((label, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0))
            continue
        distinct = len(ks)
        t_count = sum(1 for k in ks if torn[k])
        b_count = sum(1 for k in ks if blank[k] or blank_rows[k])
        b_rows = sum(blank_rows[k] + (H if blank[k] else 0) for k in ks)
        f_count = sum(1 for k in ks if foreign[k])
        f_rows = sum(foreign[k] for k in ks)
        e_count = sum(1 for k in ks if edge[k])
        e_rows = sum(edge[k] for k in ks)
        e_bad_count = sum(1 for k in ks if edge_bad[k])
        p_count = sum(1 for k in ks if flip[k])
        p_bad_count = sum(1 for k in ks if flip_bad[k])
        rows.append((label, distinct, t_count, f_count, p_count, f_rows, b_count, b_rows, e_count, e_rows, e_bad_count + p_bad_count))
    return frames, rows, {
        "blank_any": sum(1 for k in range(n) if blank[k] or blank_rows[k]),
        "torn_any": sum(1 for k in range(n) if torn[k]),
        "flip_bad": sum(1 for k in range(n) if flip_bad[k]),
        "edge_bad": sum(1 for k in range(n) if edge_bad[k]),
    }


def main():
    mode = sys.argv[1]
    if mode == "drive":
        drive(sys.argv[2], sys.argv[3], sys.argv[4])
        return
    if mode == "classify":
        samples, steps_path = sys.argv[2], sys.argv[3]
        steps = []
        with open(steps_path) as f:
            for line in f:
                label, start, end = line.rstrip("\n").split("\t")
                steps.append((label, int(start), int(end)))
        frames, rows, overall = classify(samples, steps)
        print("frames %d" % len(frames))
        for label, distinct, t_count, f_count, p_count, rowsf, b_count, b_rows, e_count, e_rows, _ in rows:
            print(
                "step %-30s distinct %3d torn %2d blank %2d (rows %5d) foreign %2d (rows %5d) edge %2d (rows %5d) flipflop %2d"
                % (label, distinct, t_count, b_count, b_rows, f_count, rowsf, e_count, e_rows, p_count)
            )
        print(
            "totals torn %d blank %d (rows %d) foreign %d edge %d flipflop %d"
            % (
                sum(r[2] for r in rows),
                sum(r[6] for r in rows),
                sum(r[7] for r in rows),
                sum(r[3] for r in rows),
                sum(r[8] for r in rows),
                sum(r[4] for r in rows),
            )
        )
        bad = (
            len(frames) < 2
            or overall["torn_any"] > 0
            or overall["blank_any"] > 0
            or overall["flip_bad"] > 0
            or overall["edge_bad"] > 0
        )
        if bad:
            sys.exit(
                "flicker/render anomaly detected: frames=%d torn=%d blank=%d flipflop_bad=%d edge_bad=%d"
                % (len(frames), overall["torn_any"], overall["blank_any"], overall["flip_bad"], overall["edge_bad"])
            )
        return
    sys.exit("usage: gpu_anim_flicker.py drive|classify ...")


if __name__ == "__main__":
    main()
