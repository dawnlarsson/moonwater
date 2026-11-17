"""framecode.py [FRAMES] [FPS_GAP_MS]: a pixel ground-truth client for the screen (cedar, Round 2, 2026-11-17).

A plain Wayland client (raw socket, wl_shm, xdg-shell; no libraries) that draws frame i of FRAMES: its background is dark for an
even i and green for an odd one, and its top and bottom bands each carry the 16 low bits of i as 16 black or white blocks. A
frame is committed at each frame callback, and its commit time (CLOCK_MONOTONIC ns) is kept. Then `moonwater canvas film` films
the window's top band while it runs, and the film's frames are read against what was committed:

  shown        film frames whose code is a committed frame
  torn         a film frame whose top and bottom bands show different frames (a tear inside the screen's refresh)
  colour       a film frame whose background is not the parity of its code (a frame's colour with another frame's number)
  backwards    a film frame showing an older code than one shown before it (a flash back to an older frame)
  superseded   committed frames never seen in the film (the client moved on first: legal, counted for the rate)
  latency      per frame: the first film frame at or after its commit that shows it (ms; p50, p90, max)

Run under a bowl with the helper loop (fc-run.sh). Prints one JSON line. Env: MACHINE (name), GPU (on|off), WAIT (film seconds).
"""
import json, mmap, os, socket, struct, sys, time, zlib
sys.path.insert(0, "/tmp/cperf")
import battery as B

FRAMES = int(sys.argv[1]) if len(sys.argv) > 1 else 240
GAP = float(sys.argv[2]) / 1000 if len(sys.argv) > 2 else 0.0
W, H = 320, 120          # the window; the film takes its top FILM_H rows
FILM_H = 56
TITLE = "framecode"
OUT = "/tmp/cperf/runs/framecode"
os.makedirs(OUT, exist_ok=True)
SOCK = os.environ.get("WAYLAND_SOCKET", "/run/moonwater/wayland-0")
MACHINE = os.environ.get("MACHINE", "?")
# MODE full: every frame is a whole new picture, damage whole (the parity colour says which frame). MODE bands: a correct client whose
# background never changes (damage only where the content changes): the two bands are damaged every frame, nothing else after frame 1.
MODE = os.environ.get("MODE", "full")
GPU = os.environ.get("GPU", "?")
t_ns = time.clock_gettime_ns


# ---- wire ----
class Wire:
    def __init__(self, path):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.connect(path)
        self.buf = bytearray()
        self.next = 2
        self.objs = {}

    def new(self):
        self.next += 1
        return self.next - 1

    def send(self, obj, op, body=b"", fd=None):
        msg = struct.pack("<II", obj, ((8 + len(body)) << 16) | op) + body
        if fd is None:
            self.s.sendall(msg)
        else:
            socket.send_fds(self.s, [msg], [fd])

    def read(self, timeout=5.0):
        self.s.settimeout(timeout)
        data = self.s.recv(65536)
        if not data:
            raise SystemExit("compositor closed the connection")
        self.buf += data
        out = []
        while len(self.buf) >= 8:
            obj, so = struct.unpack_from("<II", self.buf)
            size, op = so >> 16, so & 0xffff
            if len(self.buf) < size:
                break
            out.append((obj, op, bytes(self.buf[8:size])))
            del self.buf[:size]
        return out


def string(text):
    raw = text.encode() + b"\0"
    return struct.pack("<I", len(raw)) + raw + b"\0" * (-len(raw) % 4)


def u32(v):
    return struct.pack("<I", v & 0xffffffff)


def i32(v):
    return struct.pack("<i", v)


# ---- the client ----
def draw(frame, i):
    """Frame i: the background's parity colour, and the 16 low bits of i in the top and the bottom band (XRGB, B G R 0 bytes)."""
    bgpx = bytes((0x20, 0x20, 0x20, 0)) if i % 2 == 0 or MODE == "bands" else bytes((0x20, 0x40, 0x20, 0))
    on, off = bytes((255, 255, 255, 0)), bytes((0, 0, 0, 0))
    band = bgpx * 4
    for k in range(16):
        band += (on if (i >> k) & 1 else off) * 14 + bgpx * 4
    band += bgpx * (W - len(band) // 4)
    row_bg = bgpx * W
    frame[:] = b"".join(band if (4 <= y < 16 or 36 <= y < 48) else row_bg for y in range(H))


def main():
    w = Wire(SOCK)
    display = 1
    registry = w.new()
    w.send(display, 1, u32(registry))
    names = {}
    for _ in range(4):
        for obj, op, body in w.read():
            if obj == registry and op == 0:
                name = struct.unpack_from("<I", body)[0]
                n = struct.unpack_from("<I", body, 4)[0]
                iface = body[8:8 + n - 1].decode()
                names[iface] = name
        if {"wl_compositor", "wl_shm", "xdg_wm_base"} <= names.keys():
            break
    for iface, ver in (("wl_compositor", 4), ("wl_shm", 1), ("xdg_wm_base", 1)):
        obj = w.new()
        w.send(registry, 0, u32(names[iface]) + string(iface) + u32(ver) + u32(obj))
        w.objs[iface] = obj
    comp, shm, wm = w.objs["wl_compositor"], w.objs["wl_shm"], w.objs["xdg_wm_base"]
    surface = w.new(); w.send(comp, 0, u32(surface))
    xsurf = w.new(); w.send(wm, 2, u32(xsurf) + u32(surface))
    top = w.new(); w.send(xsurf, 1, u32(top))
    w.send(top, 2, string(TITLE))
    w.send(top, 3, string("framecode"))
    w.send(surface, 6)                                    # commit with no buffer: the configure comes
    configured = False
    released = {}
    frame_done = set()
    size = 2 * W * H * 4
    fd = os.memfd_create("framecode", 0)
    os.ftruncate(fd, size)
    mm = mmap.mmap(fd, size)
    pool = w.new(); w.send(shm, 0, u32(pool) + i32(size), fd=fd)
    buffers = []
    for b in range(2):
        buf = w.new(); w.send(pool, 0, u32(buf) + i32(b * W * H * 4) + i32(W) + i32(H) + i32(W * 4) + u32(1))
        buffers.append(buf)
        released[buf] = True
    commits = []
    deadline = time.monotonic() + 20
    while not configured and time.monotonic() < deadline:
        for obj, op, body in w.read():
            if obj == wm and op == 0:
                w.send(wm, 3, body)                       # pong
            if obj == xsurf and op == 0:
                w.send(xsurf, 4, body)                    # ack_configure
                configured = True
            if obj == display and op == 0:
                raise SystemExit("display error: %r" % body)
    if not configured:
        raise SystemExit("no configure from the compositor")
    film_started = False
    for i in range(FRAMES):
        if i == 3 and not film_started:
            # the window is on the screen once its first frames are committed: its place, then the film of its top band
            film_started = True
            win = None
            for _ in range(20):
                for line in B.machine("moonwater window").splitlines():
                    if TITLE in line and " at " in line:
                        at = line.split(" at ")[1].split(",")
                        win = (int(at[0]), int(at[1]))
                if win:
                    break
                time.sleep(0.2)
            if win is None:
                raise SystemExit("the window is not on the screen")
            film_s = FRAMES * (GAP + 0.02) + 6
            B.machine("rm -f /run/moonwater/record/film.txt /run/moonwater/record/film.raw; moonwater canvas film %d %d %d %d %d" % (
                min(60, int(film_s) + 1), win[0], win[1], W, FILM_H))
            time.sleep(0.5)
            film_t0 = t_ns(time.CLOCK_MONOTONIC)
        # wait for a free buffer and a callback slot
        buf_idx = i % 2
        buf = buffers[buf_idx]
        deadline = time.monotonic() + 5
        while not released.get(buf, False):
            if time.monotonic() > deadline:
                raise SystemExit("buffer %d never released" % buf_idx)
            for obj, op, body in w.read():
                if obj in released and op == 0:
                    released[obj] = True
                if obj == wm and op == 0:
                    w.send(wm, 3, body)
                if obj == xsurf and op == 0:
                    w.send(xsurf, 4, body)
        frame = memoryview(mm)[buf_idx * W * H * 4:(buf_idx + 1) * W * H * 4]
        draw(frame, i)
        released[buf] = False
        cb = w.new()
        w.send(surface, 3, u32(cb))
        w.send(surface, 1, u32(buf) + i32(0) + i32(0))
        if MODE == "bands" and i >= 2:
            w.send(surface, 2, i32(0) + i32(4) + i32(W) + i32(12))
            w.send(surface, 2, i32(0) + i32(36) + i32(W) + i32(12))
        else:
            w.send(surface, 2, i32(0) + i32(0) + i32(W) + i32(H))
        commits.append([i, t_ns(time.CLOCK_MONOTONIC)])
        w.send(surface, 6)
        # wait for the frame callback (the next frame's time)
        got = False
        deadline = time.monotonic() + 5
        while not got:
            if time.monotonic() > deadline:
                raise SystemExit("no frame callback for frame %d" % i)
            for obj, op, body in w.read():
                if obj == cb and op == 0:
                    got = True
                if obj in released and op == 0:
                    released[obj] = True
                if obj == wm and op == 0:
                    w.send(wm, 3, body)
                if obj == xsurf and op == 0:
                    w.send(xsurf, 4, body)
        if GAP:
            time.sleep(GAP)
    with open("%s/commits.json" % OUT, "w") as f:
        json.dump(commits, f)
    # wait for the film, then judge it
    for _ in range(60):
        if os.path.exists("/run/moonwater/record/film.txt"):
            break
        time.sleep(1)
    time.sleep(1)
    analyse(commits, film_t0)


def analyse(commits, film_t0):
    frames, pos = [], 0
    data = open("/run/moonwater/record/film.raw", "rb").read()
    while pos + 32 <= len(data):
        magic, seq, flags, when, fw, fh = struct.unpack_from("<8sIIQII", data, pos)
        pos += 32
        if magic != b"MWFILM01":
            break
        px = data[pos:pos + fw * fh * 4]
        pos += fw * fh * 4
        frames.append((when, seq, px, fw, fh))
    t0 = commits[0][1]
    shown_codes = []
    torn = colour = 0
    for when, seq, px, fw, fh in frames:
        def pix(x, y):
            o = (y * fw + x) * 4
            return px[o + 2], px[o + 1], px[o]          # R G B from B G R 0
        top = bot = 0
        for k in range(16):
            x = 4 + k * 18 + 7
            if sum(pix(x, 10)) > 384:
                top |= 1 << k
            if sum(pix(x, 42)) > 384:
                bot |= 1 << k
        odd = pix(2, 28)[1] > 0x30
        if top != bot:
            torn += 1
            continue
        if MODE != "bands" and odd != bool(top & 1):
            colour += 1
        shown_codes.append((when, top))
    committed = {i: t for i, t in commits}
    backwards = 0
    high = -1
    for when, code in shown_codes:
        if code < high:
            backwards += 1
        high = max(high, code)
    lat = []
    missing = 0
    worst = (0.0, -1)
    for i, t in commits:
        if t < film_t0:  # committed before the film started (the window's first frames): not judged
            continue
        hit = next((when for when, code in shown_codes if when >= t and code == i & 0xffff), None)
        if hit is None:
            missing += 1
        else:
            ms = (hit - t) / 1e6
            lat.append(ms)
            worst = max(worst, (ms, i))
    commits = [c for c in commits if c[1] >= film_t0]
    over50 = sum(1 for v in lat if v > 50)
    lat.sort()

    def q(p):
        return round(lat[min(len(lat) - 1, int(p * len(lat)))], 2) if lat else None
    seen = len({c for _, c in shown_codes})
    final = shown_codes[-1][1] if shown_codes else None
    final_ok = final == (commits[-1][0] & 0xffff) if commits else None
    row = {"machine": MACHINE, "gpu": GPU, "path": "framecode", "frames": len(commits), "film_frames": len(frames),
           "shown": seen, "superseded": missing, "torn": torn, "colour": colour, "backwards": backwards,
           "mode": MODE, "final_ok": final_ok, "over_50ms": over50, "worst_frame": worst[1], "worst_ms": round(worst[0], 1), "p50_ms": q(0.5), "p90_ms": q(0.9), "p99_ms": q(0.99), "max_ms": q(0.999)}
    print(json.dumps(row), flush=True)
    open("%s/last.json" % OUT, "w").write(json.dumps(row))
    open("%s/rows.jsonl" % OUT, "a").write(json.dumps(row) + "\n")


main()
