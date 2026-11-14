#!/usr/bin/env python3
# The flicker detector of the lists lane (test/gpu_lists_e2e.sh): input through the compositor's evdev fifo (-i, the records a device
# writes: struct timeval, type, code, value), the headless scan image sampled as fast as it is written, and every distinct frame kept in
# order with its step. A step is one input (hover a row, click, wheel, type); its frames are the distinct screens from the 'before' screen
# to the settled 'after'. Usage:
#     gpu_lists_flicker.py SCAN FIFO OUTDIR
# writes OUTDIR/frames/NNNN.ppm (each distinct frame, in order), OUTDIR/steps.txt (per step: its label and the frame numbers it showed,
# in order), and prints one line per step: "step NAME frames a,b,c" and the verdict.
import hashlib, os, struct, sys, threading, time, zlib

W, H = 800, 600
ROW_BYTES = W * 4
ZERO_ROW_CRC = zlib.crc32(b"\x00" * ROW_BYTES)
MIN_DRAWN_NONZERO = 1024
CARET_BAND = (36, 108)
CARET_MAX_ROWS = 36
CARET_MIN_INTERVAL_NS = 120_000_000
EV_SYN, EV_KEY, EV_REL = 0, 1, 2
REL_X, REL_Y, REL_WHEEL = 0, 1, 8
BTN_LEFT = 0x110
KEY = {'d': 32, 'i': 23, 's': 31, 'p': 25, 'l': 38, 'a': 30, 'y': 21, 'n': 49, 'e': 18, 'r': 19, 'o': 24, 'k': 37, 'c': 46}


def analyze_frame(data):
    """Return per-row CRC32 tuple and count of non-zero bytes for an 800x600 XRGB frame."""
    mv = memoryview(data)
    row_hashes = tuple(
        zlib.crc32(mv[r * ROW_BYTES:(r + 1) * ROW_BYTES]) for r in range(H)
    )
    non_zero = len(data) - data.count(0)
    return row_hashes, non_zero


def classify(frames, click_filter_step=-1):
    """Classify distinct frames for torn rows, flip-flops, blank flashes, and foreign rows.

    Each frame tuple is `(number, digest, t_ns, row_hashes, non_zero, step_idx)`.
    Cross-step intentional reversals (such as wheel down followed by wheel up) and
    slow localized filter-caret blink turnarounds are excluded from flip-flop failures.
    """
    n = len(frames)
    seen = [dict() for _ in range(H)]
    for k, (_, _, _, rows, _, _) in enumerate(frames):
        for r, v in enumerate(rows):
            seen[r].setdefault(v, set()).add(k)

    torn = []
    flipflop = []
    caret_blinks = []
    blank_frames = []
    blank_row_flashes = []
    foreign = []

    for k, (number, _, t_ns, s, non_zero, step_idx) in enumerate(frames):
        if non_zero < MIN_DRAWN_NONZERO:
            blank_frames.append(number)
        if 1 <= k < n - 1:
            p = frames[k - 1][3]
            q = frames[k + 1][3]
            same_step_q = step_idx >= 0 and frames[k + 1][5] == step_idx
            quick_q = (frames[k + 1][2] - t_ns) < 45_000_000
            if same_step_q and quick_q and s != p and s != q and p != q:
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
                if ok and has_p and has_q:
                    torn.append(number)

            if non_zero >= MIN_DRAWN_NONZERO:
                b_rows = sum(
                    1 for r in range(H)
                    if s[r] == ZERO_ROW_CRC and p[r] != ZERO_ROW_CRC and q[r] != ZERO_ROW_CRC
                )
                if b_rows:
                    blank_row_flashes.append((number, b_rows))

            f_rows = sum(
                1 for r in range(H)
                if p[r] == q[r] and s[r] != p[r] and len(seen[r].get(s[r], ())) == 1
            )
            if f_rows:
                foreign.append((number, f_rows))

        if k >= 2 and s == frames[k - 2][3] and s != frames[k - 1][3]:
            prev_step = frames[k - 1][5]
            prev2_step = frames[k - 2][5]
            if step_idx != prev_step and prev_step != prev2_step and step_idx >= 0 and prev_step >= 0:
                continue
            diff_rows = [r for r in range(H) if s[r] != frames[k - 1][3][r]]
            dt = t_ns - frames[k - 1][2]
            in_caret = (
                click_filter_step >= 0
                and step_idx >= click_filter_step
                and 0 < len(diff_rows) <= CARET_MAX_ROWS
                and all(CARET_BAND[0] <= r <= CARET_BAND[1] for r in diff_rows)
                and dt >= CARET_MIN_INTERVAL_NS
            )
            if in_caret:
                caret_blinks.append(number)
            else:
                flipflop.append(number)

    return {
        "torn": torn,
        "flipflop": flipflop,
        "caret_blinks": caret_blinks,
        "blank_frames": blank_frames,
        "blank_row_flashes": blank_row_flashes,
        "foreign": foreign,
    }


class Sampler:
    def __init__(self, scan_path, frames_dir):
        self.scan = scan_path
        self.frames_dir = frames_dir
        self.lock = threading.Lock()
        self.last = None
        self.step_idx = -1
        self.distinct = []  # (number, digest, t_ns, row_hashes, non_zero, step_idx)
        self.stop = False
        self.thread = threading.Thread(target=self.run)
        self.thread.daemon = True

    def set_step(self, step_idx):
        with self.lock:
            self.step_idx = step_idx

    def run(self):
        while not self.stop:
            try:
                with open(self.scan, "rb") as f:
                    data = f.read()
            except OSError:
                data = b""
            if len(data) == W * H * 4:
                digest = hashlib.sha1(data).hexdigest()
                with self.lock:
                    changed = digest != self.last
                if changed:
                    t_ns = time.monotonic_ns()
                    row_hashes, non_zero = analyze_frame(data)
                    ppm = (
                        b"P6\n%d %d\n255\n" % (W, H)
                        + bytes(b for i in range(0, len(data), 4) for b in (data[i + 2], data[i + 1], data[i]))
                    )
                    with self.lock:
                        if digest != self.last:
                            number = len(self.distinct)
                            self.distinct.append((number, digest, t_ns, row_hashes, non_zero, self.step_idx))
                            self.last = digest
                            with open(os.path.join(self.frames_dir, "%04d.ppm" % number), "wb") as p:
                                p.write(ppm)
            time.sleep(0.005)

    def number(self):
        with self.lock:
            return len(self.distinct), (self.distinct[-1][1] if self.distinct else None)

    def snapshot(self):
        with self.lock:
            return list(self.distinct)


def send(fd, *events):
    out_bytes = b""
    for kind, code, value in events:
        now = time.time()
        out_bytes += struct.pack("qqHHi", int(now), int((now - int(now)) * 1000000), kind, code, value)
    now = time.time()
    out_bytes += struct.pack("qqHHi", int(now), int((now - int(now)) * 1000000), EV_SYN, 0, 0)
    os.write(fd, out_bytes)


class Pointer:
    def __init__(self, fd):
        self.fd = fd
        self.x, self.y = W // 2, H // 2

    def move(self, x, y):
        dx, dy = x - self.x, y - self.y
        steps = [(EV_REL, REL_X, 1 if dx > 0 else -1)] * abs(dx) + [(EV_REL, REL_Y, 1 if dy > 0 else -1)] * abs(dy)
        for i in range(0, len(steps), 32):
            send(self.fd, *steps[i:i + 32])
            time.sleep(0.004)
        self.x, self.y = x, y

    def click(self):
        send(self.fd, (EV_KEY, BTN_LEFT, 1))
        time.sleep(0.05)
        send(self.fd, (EV_KEY, BTN_LEFT, 0))

    def wheel(self, notches):
        for _ in range(abs(notches)):
            send(self.fd, (EV_REL, REL_WHEEL, 1 if notches > 0 else -1))
            time.sleep(0.03)

    def type(self, text):
        for ch in text:
            code = KEY[ch]
            send(self.fd, (EV_KEY, code, 1))
            send(self.fd, (EV_KEY, code, 0))
            time.sleep(0.03)


def settle(sampler, quiet=0.4, limit=6.0):
    start = time.time()
    last_number, last_hash = sampler.number()
    since = time.time()
    while time.time() - start < limit:
        time.sleep(0.02)
        number, digest = sampler.number()
        if digest != last_hash:
            last_hash, since = digest, time.time()
        if time.time() - since >= quiet:
            return
    return


def main():
    if len(sys.argv) < 4:
        sys.exit("usage: gpu_lists_flicker.py SCAN FIFO OUTDIR")
    scan, fifo, out = sys.argv[1], sys.argv[2], sys.argv[3]
    frames_dir = os.path.join(out, "frames")
    os.makedirs(frames_dir, exist_ok=True)

    sampler = Sampler(scan, frames_dir)
    sampler.thread.start()
    fd = os.open(fifo, os.O_WRONLY)
    pointer = Pointer(fd)
    # the steps: a hover over each settings row, a click, a wheel over the list, typing into the filter
    rows = [116, 156, 196, 236, 276, 316]
    steps = []
    for y in rows:
        steps.append(("hover sidebar row %d" % y, lambda y=y: pointer.move(100, y)))
    steps.append(("click sidebar row 156", lambda: (pointer.move(100, 156), pointer.click())))
    steps.append(("hover list row 200", lambda: pointer.move(400, 200)))
    steps.append(("click list row 200", lambda: (pointer.move(400, 200), pointer.click())))
    steps.append(("wheel down over the list", lambda: (pointer.move(400, 300), pointer.wheel(-3))))
    steps.append(("wheel up over the list", lambda: pointer.wheel(3)))
    click_filter_step = len(steps)
    steps.append(("click the filter", lambda: (pointer.move(590, 68), pointer.click())))
    steps.append(("type into the filter", lambda: pointer.type("disp")))
    settle(sampler, 0.6, 4.0)
    report = []
    for idx, (label, action) in enumerate(steps):
        settle(sampler, 0.4, 4.0)
        before_number, _ = sampler.number()
        sampler.set_step(idx)
        action()
        settle(sampler, 0.5, 6.0)
        after_number, _ = sampler.number()
        shown = list(range(before_number - 1 if before_number else 0, after_number))
        report.append((label, shown))
        print("step %s frames %s" % (label, ",".join(str(n) for n in shown)))
    os.close(fd)
    sampler.stop = True
    sampler.thread.join(timeout=2)
    frames = sampler.snapshot()
    with open(os.path.join(out, "steps.txt"), "w") as f:
        for label, shown in report:
            f.write("%s\t%s\n" % (label, ",".join(str(n) for n in shown)))

    verdict = classify(frames, click_filter_step=click_filter_step)
    blank_total = len(verdict["blank_frames"]) + len(verdict["blank_row_flashes"])
    print("distinct frames %d" % len(frames))
    print(
        "totals torn %d flipflop %d caret %d blank %d (blank_rows %d) foreign %d"
        % (
            len(verdict["torn"]),
            len(verdict["flipflop"]),
            len(verdict["caret_blinks"]),
            len(verdict["blank_frames"]),
            sum(cnt for _, cnt in verdict["blank_row_flashes"]),
            len(verdict["foreign"]),
        )
    )
    problems = []
    if len(frames) < 2:
        problems.append("too few distinct frames (%d)" % len(frames))
    if verdict["torn"]:
        problems.append("torn=%s" % verdict["torn"])
    if verdict["flipflop"]:
        problems.append("flipflop=%s" % verdict["flipflop"])
    if blank_total:
        problems.append(
            "blank_frames=%s blank_row_flashes=%s"
            % (verdict["blank_frames"], verdict["blank_row_flashes"])
        )
    if problems:
        sys.exit("flicker check failed: " + " ".join(problems))


if __name__ == "__main__":
    main()
