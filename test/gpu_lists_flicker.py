#!/usr/bin/env python3
# The flicker detector of the lists lane (test/gpu_lists_e2e.sh): input through the compositor's evdev fifo (-i, the records a device
# writes: struct timeval, type, code, value), the headless scan image sampled as fast as it is written, and every distinct frame kept in
# order with its step. A step is one input (hover a row, click, wheel, type); its frames are the distinct screens from the 'before' screen
# to the settled 'after'. Usage:
#     gpu_lists_flicker.py SCAN FIFO OUTDIR
# writes OUTDIR/frames/NNNN.ppm (each distinct frame, in order), OUTDIR/steps.txt (per step: its label and the frame numbers it showed,
# in order), and prints one line per step: "step NAME frames a,b,c" and the verdict.
import hashlib, os, struct, sys, threading, time

W, H = 800, 600
EV_SYN, EV_KEY, EV_REL = 0, 1, 2
REL_X, REL_Y, REL_WHEEL = 0, 1, 8
BTN_LEFT = 0x110
KEY = {'d': 32, 'i': 23, 's': 31, 'p': 25, 'l': 38, 'a': 30, 'y': 21, 'n': 49, 'e': 18, 'r': 19, 'o': 24, 'k': 37, 'c': 46}

scan, fifo, out = sys.argv[1], sys.argv[2], sys.argv[3]
frames_dir = os.path.join(out, "frames")
os.makedirs(frames_dir, exist_ok=True)


class Sampler:
    def __init__(self):
        self.lock = threading.Lock()
        self.last = None
        self.distinct = []  # (number, hash)
        self.stop = False
        self.thread = threading.Thread(target=self.run)
        self.thread.daemon = True

    def run(self):
        while not self.stop:
            try:
                with open(scan, "rb") as f:
                    data = f.read()
            except OSError:
                data = b""
            if len(data) == W * H * 4:
                digest = hashlib.sha1(data).hexdigest()
                with self.lock:
                    if digest != self.last:
                        number = len(self.distinct)
                        self.distinct.append((number, digest))
                        self.last = digest
                        with open(os.path.join(frames_dir, "%04d.ppm" % number), "wb") as p:
                            p.write(b"P6\n%d %d\n255\n" % (W, H) + bytes(b for i in range(0, len(data), 4) for b in (data[i + 2], data[i + 1], data[i])))
            time.sleep(0.005)

    def number(self):
        with self.lock:
            return len(self.distinct), (self.distinct[-1][1] if self.distinct else None)


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
    sampler = Sampler()
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
    steps.append(("click the filter", lambda: (pointer.move(590, 68), pointer.click())))
    steps.append(("type into the filter", lambda: pointer.type("disp")))
    settle(sampler, 0.6, 4.0)
    report = []
    for label, action in steps:
        settle(sampler, 0.4, 4.0)
        before_number, _ = sampler.number()
        action()
        settle(sampler, 0.5, 6.0)
        after_number, _ = sampler.number()
        shown = list(range(before_number - 1 if before_number else 0, after_number))
        report.append((label, shown))
        print("step %s frames %s" % (label, ",".join(str(n) for n in shown)))
    sampler.stop = True
    sampler.thread.join(timeout=2)
    with open(os.path.join(out, "steps.txt"), "w") as f:
        for label, shown in report:
            f.write("%s\t%s\n" % (label, ",".join(str(n) for n in shown)))
    print("distinct frames %d" % sampler.number()[0])


if __name__ == "__main__":
    main()
