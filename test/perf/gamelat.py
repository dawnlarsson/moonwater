"""gamelat.py MODE [RATES] [SECONDS] (gameinput, gpu-round): game input measured at the client, on the real compositor.

A uinput relative mouse (REL_X/REL_Y + buttons, so the compositor takes it as a mouse) and a uinput keyboard, handed in as fds 7 and 8
(`7<>/dev/uinput 8<>/dev/uinput`, the bowl's own /dev/uinput is /dev/null). A forked sender process busy-waits a schedule of reports at
each RATE (Hz) for SECONDS, REL_X of 1, 2, 3, 1, 2, 3... (never 0) and a SYN after each, and stamps each write (CLOCK_MONOTONIC).

MODE wl:     a raw-wire Wayland client (no libraries): an xdg toplevel (fullscreen unless FS=0) drawn once with wl_shm, a relative pointer,
             and a persistent pointer lock (LOCK=lock|confine|none). Each zwp_relative_pointer_v1.relative_motion is stamped on arrival.
MODE x:      an X client through ctypes (libX11 + libXi) under the bowl's xwayland wrapper: a window, XI2 RawMotion on the root, a pointer
             grab confined to the window with an empty cursor, and a warp to its middle (Xwayland's lock emulation: it locks the
             Wayland pointer while an X client warps a hidden, confined pointer). Each RawMotion and each MotionNotify stamped on arrival.
             XGRAB=0 / XWARP=0 leave those out.
MODE sdl:    a real SDL2 game's mouse-look (libSDL2 by ctypes, SDL_SetRelativeMouseMode in a fullscreen window), SDL_VIDEODRIVER=wayland
             or x11 (then through Xwayland, which this starts with the bowl's xwayland-moonwater).
MODE photon: mouse-look to the screen: the wl client draws the count come so far as a 16-bit code in its top band at each frame
             callback; the caller films 0,0 320x24 first (`moonwater canvas film 14 0 0 320 24`); per report the first filmed
             present showing it, less its write's stamp, and each commit to its present.
Scripts: gamelat-ab.sh LABEL BINARY|image RATES SECONDS runs the cases on one compositor; gamelat-table.py prints the rows.

Then keys: KEYS presses (cycling asdfghjkl) 50 ms apart, each stamped at the write, and the client's key event stamped on arrival.

Per rate it prints one JSON row: sent reports, events that came, the sum of counts sent and come (drops), merged events (a value that is
not one report's), wl_pointer frames, latency inject->client (us, p50/p90/p99/max) by the cumulative-count match (event k is seen
when the counts come reach the counts sent through k), and for wl the device stamp's latency (utime). Env: MACHINE, GPU.
"""
import ctypes, fcntl, json, mmap, os, socket, struct, sys, time

MODE = sys.argv[1] if len(sys.argv) > 1 else "wl"
RATES = [int(r) for r in (sys.argv[2] if len(sys.argv) > 2 else "125,1000,4000,8000").split(",")]
SECONDS = float(sys.argv[3]) if len(sys.argv) > 3 else 2.0
KEYS = int(os.environ.get("KEYS", "30"))
LOCK = os.environ.get("LOCK", "lock")
FS = os.environ.get("FS", "1") == "1"
MACHINE, GPU = os.environ.get("MACHINE", "?"), os.environ.get("GPU", "?")
OUT = os.environ.get("OUT", "/tmp/cperf/runs/gamelat")
os.makedirs(OUT, exist_ok=True)
mono = lambda: time.clock_gettime_ns(time.CLOCK_MONOTONIC)

# ---- uinput ----
EV_SYN, EV_KEY, EV_REL = 0, 1, 2
REL_X, REL_Y = 0, 1
BTN_LEFT, BTN_RIGHT, BTN_MIDDLE = 0x110, 0x111, 0x112
UI_SET_EVBIT, UI_SET_KEYBIT, UI_SET_RELBIT = 0x40045564, 0x40045565, 0x40045566
UI_DEV_SETUP, UI_DEV_CREATE, UI_DEV_DESTROY = 0x405C5503, 0x5501, 0x5502
LETTERS = [30, 31, 32, 33, 34, 35, 36, 37, 38]  # a s d f g h j k l


def uinput(fd, name, keys, rel=()):
    fcntl.ioctl(fd, UI_SET_EVBIT, EV_KEY)
    for k in keys:
        fcntl.ioctl(fd, UI_SET_KEYBIT, k)
    if rel:
        fcntl.ioctl(fd, UI_SET_EVBIT, EV_REL)
        for r in rel:
            fcntl.ioctl(fd, UI_SET_RELBIT, r)
    fcntl.ioctl(fd, UI_DEV_SETUP, struct.pack("HHHH80sI", 3, 0x1209, 0x6d90 + len(name), 1, name.encode(), 0))
    fcntl.ioctl(fd, UI_DEV_CREATE)


def ev(t, c, v):
    return struct.pack("qqHHi", 0, 0, t, c, v)


SYN = ev(EV_SYN, 0, 0)
MOUSE, KBD = 7, 8


def sender(plan_w):
    """The child: on a byte from the parent, runs the next item of the plan and writes its stamps back as JSON lines."""
    r = os.fdopen(plan_w[0], "r")
    w = os.fdopen(plan_w[1], "w")
    for line in r:
        item = json.loads(line)
        stamps = []
        if item["kind"] == "rel":
            gap = 1_000_000_000 // item["rate"]
            n = int(item["rate"] * item["seconds"])
            packs = [ev(EV_REL, REL_X, 1 + k % 3) + SYN for k in range(3)]
            t = mono() + 2_000_000
            for k in range(n):
                while mono() < t:
                    pass
                os.write(MOUSE, packs[k % 3])
                stamps.append(mono())
                t += gap
        elif item["kind"] == "key":
            for k in range(item["n"]):
                code = LETTERS[k % len(LETTERS)]
                os.write(KBD, ev(EV_KEY, code, 1) + SYN)
                stamps.append(mono())
                time.sleep(0.012)
                os.write(KBD, ev(EV_KEY, code, 0) + SYN)
                time.sleep(0.038)
        elif item["kind"] == "click":
            os.write(MOUSE, ev(EV_KEY, BTN_LEFT, 1) + SYN)
            time.sleep(0.03)
            os.write(MOUSE, ev(EV_KEY, BTN_LEFT, 0) + SYN)
        elif item["kind"] == "move":
            for k in range(item["n"]):
                os.write(MOUSE, ev(EV_REL, REL_X, item["dx"]) + ev(EV_REL, REL_Y, item["dy"]) + SYN)
                time.sleep(0.004)
        w.write(json.dumps(stamps) + "\n")
        w.flush()
    os._exit(0)


class Sender:
    def __init__(self):
        uinput(MOUSE, "gamelat mouse", [BTN_LEFT, BTN_RIGHT, BTN_MIDDLE], rel=[REL_X, REL_Y])
        uinput(KBD, "gamelat keyboard", LETTERS)
        a, b = os.pipe(), os.pipe()
        if os.fork() == 0:
            os.close(a[1]), os.close(b[0])
            sender((a[0], b[1]))
        os.close(a[0]), os.close(b[1])
        self.to, self.back = os.fdopen(a[1], "w"), b[0]
        os.set_blocking(self.back, False)
        self.pending = b""
        time.sleep(1.5)  # the compositor's hotplug watch opens the devices

    def start(self, **item):
        self.to.write(json.dumps(item) + "\n")
        self.to.flush()

    def done(self):
        """The stamps of the item, when the child has written them; None before."""
        try:
            self.pending += os.read(self.back, 1 << 20)
        except BlockingIOError:
            pass
        if b"\n" not in self.pending:
            return None
        line, self.pending = self.pending.split(b"\n", 1)
        return json.loads(line)


def quantiles(v):
    v = sorted(v)
    q = lambda p: round(v[min(len(v) - 1, int(p * len(v)))], 1) if v else None
    return {"p50": q(0.5), "p90": q(0.9), "p99": q(0.99), "max": round(v[-1], 1) if v else None, "n": len(v)}


def match(stamps, arrivals):
    """Latency (us) of each sent report: the first arrival whose cumulative count reaches the report's. arrivals: (t_ns, count)."""
    out, sent, came, i = [], 0, 0, 0
    for k, t in enumerate(stamps):
        sent += 1 + k % 3
        while i < len(arrivals) and came < sent:
            came += arrivals[i][1]
            i += 1
        if came < sent:
            break
        out.append((arrivals[i - 1][0] - t) / 1000)
    return out


# ---- the Wayland client ----
class Wire:
    def __init__(self, path):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.connect(path)
        self.buf = bytearray()
        self.next = 2

    def new(self):
        self.next += 1
        return self.next - 1

    def send(self, obj, op, body=b"", fd=None):
        msg = struct.pack("<II", obj, ((8 + len(body)) << 16) | op) + body
        if fd is None:
            self.s.sendall(msg)
        else:
            socket.send_fds(self.s, [msg], [fd])

    def read(self, timeout):
        self.s.settimeout(timeout)
        try:
            data, fds, _, _ = socket.recv_fds(self.s, 1 << 16, 8)
        except socket.timeout:
            return 0, []
        at = mono()
        for fd in fds:
            os.close(fd)
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
        return at, out


def string(text):
    raw = text.encode() + b"\0"
    return struct.pack("<I", len(raw)) + raw + b"\0" * (-len(raw) % 4)


u32 = lambda v: struct.pack("<I", v & 0xffffffff)
i32 = lambda v: struct.pack("<i", v)
fixed = lambda b, o: struct.unpack_from("<i", b, o)[0] / 256.0


def run_wl(snd):
    w = Wire(os.environ.get("WAYLAND_SOCKET", "/run/moonwater/wayland-0"))
    registry = w.new()
    w.send(1, 1, u32(registry))
    names = {}
    end = time.monotonic() + 5
    while time.monotonic() < end and not {"wl_compositor", "wl_shm", "xdg_wm_base", "wl_seat"} <= names.keys():
        for obj, op, body in w.read(1)[1]:
            if obj == registry and op == 0:
                n = struct.unpack_from("<I", body, 4)[0]
                names[body[8:8 + n - 1].decode()] = (struct.unpack_from("<I", body)[0], struct.unpack_from("<I", body, 8 + ((n + 3) & ~3))[0])
    for _ in range(3):
        for obj, op, body in w.read(0.2)[1]:
            if obj == registry and op == 0:
                n = struct.unpack_from("<I", body, 4)[0]
                names[body[8:8 + n - 1].decode()] = (struct.unpack_from("<I", body)[0], struct.unpack_from("<I", body, 8 + ((n + 3) & ~3))[0])
    o = {}
    for iface, ver in (("wl_compositor", 4), ("wl_shm", 1), ("xdg_wm_base", 1), ("wl_seat", 5),
                       ("zwp_relative_pointer_manager_v1", 1), ("zwp_pointer_constraints_v1", 1)):
        if iface not in names:
            raise SystemExit("no %s" % iface)
        o[iface] = w.new()
        w.send(registry, 0, u32(names[iface][0]) + string(iface) + u32(min(ver, names[iface][1])) + u32(o[iface]))
    surface = w.new(); w.send(o["wl_compositor"], 0, u32(surface))
    xsurf = w.new(); w.send(o["xdg_wm_base"], 2, u32(xsurf) + u32(surface))
    top = w.new(); w.send(xsurf, 1, u32(top))
    w.send(top, 2, string("gamelat"))
    w.send(top, 3, string("gamelat"))
    if FS:
        w.send(top, 11, u32(0))
    pointer = w.new(); w.send(o["wl_seat"], 0, u32(pointer))
    keyboard = w.new(); w.send(o["wl_seat"], 1, u32(keyboard))
    rel = w.new(); w.send(o["zwp_relative_pointer_manager_v1"], 1, u32(rel) + u32(pointer))
    w.send(surface, 6)
    size, configured = [0, 0], False
    st = {"locked": 0, "kbd": 0, "enter": 0}
    log = []  # (kind, at, values)

    def pump(timeout):
        nonlocal configured
        at, events = w.read(timeout)
        for obj, op, body in events:
            if obj == o["xdg_wm_base"] and op == 0:
                w.send(obj, 3, body)
            elif obj == top and op == 0:
                size[0], size[1] = struct.unpack_from("<ii", body)
            elif obj == xsurf and op == 0:
                w.send(xsurf, 4, body)
                configured = True
            elif obj == 1 and op == 0:
                raise SystemExit("display error %r" % body)
            elif obj == rel and op == 0:
                hi, lo = struct.unpack_from("<II", body)
                log.append(("rel", at, ((hi << 32) | lo, fixed(body, 8), fixed(body, 12), fixed(body, 16), fixed(body, 20))))
            elif obj == pointer and op == 5:
                log.append(("frame", at, None))
            elif obj == pointer and op == 2:
                log.append(("motion", at, (fixed(body, 4), fixed(body, 8))))
            elif obj == pointer and op == 0:
                st["enter"] = 1
                if os.environ.get("HIDE") == "1":  # no pointer over the window, as a game asks (set_cursor with no surface)
                    w.send(pointer, 0, body[:4] + u32(0) + i32(0) + i32(0))
            elif obj == keyboard and op == 1:
                st["kbd"] = 1
            elif obj == keyboard and op == 2:
                st["kbd"] = 0
            elif obj == keyboard and op == 3:
                if struct.unpack_from("<I", body, 12)[0] == 1:
                    log.append(("key", at, struct.unpack_from("<I", body, 8)[0]))
            elif obj == lock_id[0] and op == 0:
                st["locked"] = 1
            elif obj == lock_id[0] and op == 1:
                st["locked"] = 0
        return events

    lock_id = [-1]
    end = time.monotonic() + 10
    while not configured and time.monotonic() < end:
        pump(0.5)
    if not configured:
        raise SystemExit("no configure")
    W, H = (size[0] or 800), (size[1] or 600)
    nbytes = W * H * 4
    fd = os.memfd_create("gamelat", 0)
    os.ftruncate(fd, 2 * nbytes)
    mm = mmap.mmap(fd, 2 * nbytes)
    mm[:nbytes] = bytes((0x30, 0x20, 0x10, 0)) * (W * H)
    mm[nbytes:] = mm[:nbytes]
    pool = w.new(); w.send(o["wl_shm"], 0, u32(pool) + i32(2 * nbytes), fd=fd)
    buf = w.new(); w.send(pool, 0, u32(buf) + i32(0) + i32(W) + i32(H) + i32(W * 4) + u32(1))
    buf2 = w.new(); w.send(pool, 0, u32(buf2) + i32(nbytes) + i32(W) + i32(H) + i32(W * 4) + u32(1))
    w.send(surface, 1, u32(buf) + i32(0) + i32(0))
    w.send(surface, 2, i32(0) + i32(0) + i32(W) + i32(H))
    w.send(surface, 6)
    if LOCK != "none":
        lock_id[0] = w.new()
        w.send(o["zwp_pointer_constraints_v1"], 1 if LOCK == "lock" else 2, u32(lock_id[0]) + u32(surface) + u32(pointer) + u32(0) + u32(2))
    # the pointer onto the window and the keys to it: a small move, then a click when the keys did not come
    end = time.monotonic() + 1.0
    while time.monotonic() < end:
        pump(0.1)
    for attempt in range(3):
        if st["kbd"] and (st["locked"] or LOCK == "none"):
            break
        snd.start(kind="move", n=20, dx=-3, dy=-2)
        while snd.done() is None:
            pump(0.02)
        if not st["kbd"]:
            snd.start(kind="click")
            while snd.done() is None:
                pump(0.02)
        end = time.monotonic() + 0.8
        while time.monotonic() < end:
            pump(0.1)
    head = {"machine": MACHINE, "gpu": GPU, "mode": "wl", "lock": LOCK, "fs": FS, "size": [W, H], "keys_focus": st["kbd"],
            "locked": st["locked"], "entered": st["enter"]}
    print(json.dumps(head), flush=True)
    if MODE == "photon":
        photon(snd, w, surface, pump, log, head, W, mm, [buf, buf2], nbytes)
        return
    rows = []
    for rate in RATES:
        del log[:]
        snd.start(kind="rel", rate=rate, seconds=SECONDS)
        stamps = None
        while stamps is None:
            pump(0.01)
            stamps = snd.done()
        end = time.monotonic() + 0.3
        while time.monotonic() < end:
            pump(0.05)
        rels = [(at, v) for kind, at, v in log if kind == "rel"]
        arrivals = [(at, int(round(v[3]))) for at, v in rels]  # dx_unaccel
        lat = match(stamps, arrivals)
        dev = [(at - v[0] * 1000) / 1000 for at, v in rels]
        sent_sum = sum(1 + k % 3 for k in range(len(stamps)))
        came_unacc = sum(a[1] for a in arrivals)
        came_acc = sum(v[1] for _, v in rels)
        merged = sum(1 for _, c in arrivals if c not in (1, 2, 3))
        frames = sum(1 for kind, _, _ in log if kind == "frame")
        motions = sum(1 for kind, _, _ in log if kind == "motion")
        # the place's motion, when every report came as one (a slow rate: each report its own read): its arrival less the report's
        mats = [at for kind, at, _ in log if kind == "motion"]
        mlat = [(mats[k] - stamps[k]) / 1000 for k in range(len(stamps))] if len(mats) == len(stamps) else []
        # wakes: distinct arrival stamps (one recv each), how many relative events each carried
        wakes = len({at for at, _ in rels})
        span = (stamps[-1] - stamps[0]) / 1e9 if len(stamps) > 1 else 0
        row = dict(head, rate=rate, sent=len(stamps), sent_hz=round(len(stamps) / span) if span else None, events=len(rels),
                   sent_counts=sent_sum, came_unaccel=came_unacc, came_accel=round(came_acc, 2), merged=merged, frames=frames,
                   motions=motions, recv_wakes=wakes, inject_to_client_us=quantiles(lat), device_stamp_to_client_us=quantiles(dev), motion_inject_to_client_us=quantiles(mlat))
        rows.append(row)
        print(json.dumps(row), flush=True)
        time.sleep(0.3)
    keys_row(snd, lambda t: pump(t), log, head)


FILM = "/run/moonwater/record/film.raw"


def photon(snd, w, surface, pump, log, head, W, mm, bufs, nbytes):
    """Mouse-look to the screen: a game's loop on the locked pointer. The window's top band (y 4..16, x 4..292) carries the 16 low bits
    of the counts come so far as 16 black or white blocks (framecode's code); the client draws and commits whenever counts came and the
    last frame callback is answered (a game's vsync-paced loop, drawing as soon as it may). `moonwater canvas film` of 0,0 320x24, started
    by the caller before this (FILMED=1), is read after: for each report, the first filmed present showing counts at or past its own,
    less its write's stamp (inject to present). One JSON row per rate."""
    bg, on, off = bytes((0x30, 0x20, 0x10, 0)), bytes((255, 255, 255, 0)), bytes((0, 0, 0, 0))

    def draw(which, code):
        band = bg * 4 + b"".join((on if (code >> k) & 1 else off) * 14 + bg * 4 for k in range(16))
        base = which * nbytes
        for y in range(4, 16):
            mm[base + y * W * 4:base + y * W * 4 + len(band)] = band
    free = {bufs[0]: True, bufs[1]: True}
    state = {"cb": None, "next": 0, "drawn": -1, "count": 0, "seen": len(log)}  # the setup's moves are no report of a run
    commits = []

    def tick(timeout):
        for obj, op, body in pump(timeout):
            if obj in free and op == 0:
                free[obj] = True
            elif obj == state["cb"] and op == 0:
                state["cb"] = None
        rels = [v for kind, _, v in log[state["seen"]:] if kind == "rel"]
        state["seen"] = len(log)
        state["count"] += sum(int(round(v[3])) for v in rels)
        b = bufs[state["next"]]
        if state["count"] != state["drawn"] and state["cb"] is None and free[b]:
            draw(state["next"], state["count"] & 0xffff)
            cb = w.new()
            w.send(surface, 3, u32(cb))
            w.send(surface, 1, u32(b) + i32(0) + i32(0))
            w.send(surface, 2, i32(0) + i32(4) + i32(300) + i32(12))
            w.send(surface, 6)
            commits.append((state["count"], mono()))
            state["cb"], state["drawn"] = cb, state["count"]
            free[b] = False
            state["next"] ^= 1
    runs = []
    for rate in RATES:
        base, first = state["count"], len(commits)
        snd.start(kind="rel", rate=rate, seconds=SECONDS)
        stamps = None
        while stamps is None:
            tick(0.001)
            stamps = snd.done()
        end = time.monotonic() + 0.5
        while time.monotonic() < end:
            tick(0.005)
        runs.append((rate, base, stamps, len(commits), first))
        time.sleep(0.2)
    # the film: whole when it has its text
    for _ in range(120):
        if os.path.exists("/run/moonwater/record/film.txt"):
            break
        tick(0.25)
    time.sleep(1)
    data = open(FILM, "rb").read() if os.path.exists(FILM) else b""
    shown, pos = [], 0
    while pos + 32 <= len(data):
        magic, seq, flags, when, fw, fh = struct.unpack_from("<8sIIQII", data, pos)
        pos += 32
        if magic != b"MWFILM01":
            break
        px = data[pos:pos + fw * fh * 4]
        pos += fw * fh * 4
        code = 0
        for k in range(16):
            o = (10 * fw + 4 + k * 18 + 7) * 4
            if o + 3 <= len(px) and px[o] + px[o + 1] + px[o + 2] > 384:
                code |= 1 << k
        shown.append((when, code))
    for rate, base, stamps, upto, first in runs:
        # each commit's own picture, to the first filmed present of it: the compositor's share (commit to present)
        cp = []
        for count, t in commits[first:upto]:
            hit = next((when for when, code in shown if when >= t and code == count & 0xffff), None)
            if hit is not None:
                cp.append((hit - t) / 1000)
        lat, cum = [], base
        for k, t in enumerate(stamps):
            cum += 1 + k % 3
            hit = next((when for when, code in shown if when >= t and ((code - cum) & 0xffff) < 0x8000), None)
            if hit is not None:
                lat.append((hit - t) / 1000)
        row = dict(head, mode="photon", rate=rate, sent=len(stamps), film_frames=len(shown), commits=upto - first,
                   inject_to_present_us=quantiles(lat), commit_to_present_us=quantiles(cp))
        print(json.dumps(row), flush=True)


def keys_row(snd, pump, log, head):
    if not KEYS:
        return
    del log[:]
    snd.start(kind="key", n=KEYS)
    stamps = None
    while stamps is None:
        pump(0.01)
        stamps = snd.done()
    end = time.monotonic() + 0.3
    while time.monotonic() < end:
        pump(0.05)
    got = [at for kind, at, _ in log if kind == "key"]
    lat = [(got[k] - stamps[k]) / 1000 for k in range(min(len(got), len(stamps)))]
    json.dump(lat, open("%s/lat-keys-%s.json" % (OUT, head.get("driver", head["mode"])), "w"))
    print(json.dumps(dict(head, what="keys", sent=len(stamps), came=len(got), inject_to_client_us=quantiles(lat))), flush=True)


# ---- the X client ----
class XIEventMask(ctypes.Structure):
    _fields_ = [("deviceid", ctypes.c_int), ("mask_len", ctypes.c_int), ("mask", ctypes.POINTER(ctypes.c_ubyte))]


class XGenericEventCookie(ctypes.Structure):
    _fields_ = [("type", ctypes.c_int), ("serial", ctypes.c_ulong), ("send_event", ctypes.c_int), ("display", ctypes.c_void_p),
                ("extension", ctypes.c_int), ("evtype", ctypes.c_int), ("cookie", ctypes.c_uint), ("data", ctypes.c_void_p)]


class XIValuatorState(ctypes.Structure):
    _fields_ = [("mask_len", ctypes.c_int), ("mask", ctypes.POINTER(ctypes.c_ubyte)), ("values", ctypes.POINTER(ctypes.c_double))]


class XIRawEvent(ctypes.Structure):
    _fields_ = [("type", ctypes.c_int), ("serial", ctypes.c_ulong), ("send_event", ctypes.c_int), ("display", ctypes.c_void_p),
                ("extension", ctypes.c_int), ("evtype", ctypes.c_int), ("time", ctypes.c_ulong), ("deviceid", ctypes.c_int),
                ("sourceid", ctypes.c_int), ("detail", ctypes.c_int), ("flags", ctypes.c_int), ("valuators", XIValuatorState),
                ("raw_values", ctypes.POINTER(ctypes.c_double))]


def run_x(snd):
    X = ctypes.CDLL("libX11.so.6")
    XI = ctypes.CDLL("libXi.so.6")
    X.XOpenDisplay.restype = ctypes.c_void_p
    X.XCreatePixmapCursor.restype = ctypes.c_ulong
    X.XCreateBitmapFromData.restype = ctypes.c_ulong
    X.XCreateSimpleWindow.restype = ctypes.c_ulong
    X.XRootWindow.restype = ctypes.c_ulong
    X.XGetEventData.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    X.XFreeEventData.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    d = X.XOpenDisplay(None)
    if not d:
        raise SystemExit("no X display")
    dp = ctypes.c_void_p(d)
    root = X.XRootWindow(dp, X.XDefaultScreen(dp))
    sw, sh = X.XDisplayWidth(dp, 0), X.XDisplayHeight(dp, 0)
    W, H = (sw, sh) if FS else (800, 600)
    win = X.XCreateSimpleWindow(dp, ctypes.c_ulong(root), 0, 0, W, H, 0, 0, 0x102030)
    X.XStoreName(dp, ctypes.c_ulong(win), b"gamelat-x")
    # KeyPress | ButtonPress | PointerMotion | StructureNotify | FocusChange
    X.XSelectInput(dp, ctypes.c_ulong(win), ctypes.c_long((1 << 0) | (1 << 2) | (1 << 6) | (1 << 17) | (1 << 21)))
    if FS:
        state, fs_atom = X.XInternAtom(dp, b"_NET_WM_STATE", 0), X.XInternAtom(dp, b"_NET_WM_STATE_FULLSCREEN", 0)
        atom = (ctypes.c_ulong * 1)(fs_atom)
        X.XChangeProperty(dp, ctypes.c_ulong(win), ctypes.c_ulong(state), ctypes.c_ulong(4), 32, 0, atom, 1)  # XA_ATOM, replace
    X.XMapWindow(dp, ctypes.c_ulong(win))
    X.XFlush(dp)
    opcode, evb, err = ctypes.c_int(), ctypes.c_int(), ctypes.c_int()
    if not X.XQueryExtension(dp, b"XInputExtension", ctypes.byref(opcode), ctypes.byref(evb), ctypes.byref(err)):
        raise SystemExit("no XInputExtension")
    major, minor = ctypes.c_int(2), ctypes.c_int(2)
    XI.XIQueryVersion(dp, ctypes.byref(major), ctypes.byref(minor))
    bits = (ctypes.c_ubyte * 4)()
    bits[17 >> 3] |= 1 << (17 & 7)  # XI_RawMotion
    mask = XIEventMask(1, 4, bits)  # XIAllMasterDevices
    XI.XISelectEvents(dp, ctypes.c_ulong(root), ctypes.byref(mask), 1)
    X.XFlush(dp)
    ev = ctypes.create_string_buffer(192)
    log, st = [], {"mapped": 0, "focus": 0, "grab": -1}
    fd = X.XConnectionNumber(dp)
    import select

    def pump(timeout):
        if not X.XPending(dp):
            select.select([fd], [], [], timeout)
        at = mono()
        while X.XPending(dp):
            X.XNextEvent(dp, ev)
            t = ctypes.c_int.from_buffer(ev).value
            if t == 35:  # GenericEvent
                cookie = XGenericEventCookie.from_buffer(ev)
                if cookie.extension == opcode.value and X.XGetEventData(dp, ctypes.addressof(cookie)):
                    if cookie.evtype == 17:
                        raw = XIRawEvent.from_address(cookie.data)
                        m = raw.valuators.mask
                        vals, k, dx, udx = raw.valuators.values, 0, 0.0, 0.0
                        for axis in range(raw.valuators.mask_len * 8):
                            if m[axis >> 3] >> (axis & 7) & 1:
                                if axis == 0:
                                    dx, udx = vals[k], raw.raw_values[k]
                                k += 1
                        log.append(("rel", at, (raw.time, dx, 0, udx, 0, raw.deviceid, raw.sourceid)))
                    X.XFreeEventData(dp, ctypes.addressof(cookie))
            elif t == 6:
                log.append(("motion", at, None))
            elif t == 2:
                log.append(("key", at, None))
            elif t == 19:
                st["mapped"] = 1
            elif t == 9:
                st["focus"] = 1
            elif t == 10:
                st["focus"] = 0

    end = time.monotonic() + 10
    while not st["mapped"] and time.monotonic() < end:
        pump(0.1)
    time.sleep(0.5)
    pump(0.1)
    # the pointer onto it and the keys: a small move and a click
    for _ in range(2):
        snd.start(kind="move", n=20, dx=-3, dy=-2)
        while snd.done() is None:
            pump(0.02)
        snd.start(kind="click")
        while snd.done() is None:
            pump(0.02)
        pump(0.3)
    X.XSetInputFocus(dp, ctypes.c_ulong(win), 1, 0)
    data = (ctypes.c_char * 8)()
    blank = X.XCreateBitmapFromData(dp, ctypes.c_ulong(win), data, 8, 8)
    col = (ctypes.c_char * 16)()
    cursor = X.XCreatePixmapCursor(dp, ctypes.c_ulong(blank), ctypes.c_ulong(blank), col, col, 0, 0)
    for _ in range(20 if os.environ.get("XGRAB", "1") == "1" else 0):
        st["grab"] = X.XGrabPointer(dp, ctypes.c_ulong(win), 1, ctypes.c_uint((1 << 2) | (1 << 3) | (1 << 6)), 1, 1,
                                    ctypes.c_ulong(win), ctypes.c_ulong(cursor), 0)
        if st["grab"] == 0:
            break
        time.sleep(0.1)
    XWARP = os.environ.get("XWARP", "1") == "1"
    if XWARP:
        X.XWarpPointer(dp, 0, ctypes.c_ulong(win), 0, 0, 0, 0, W // 2, H // 2)
    X.XFlush(dp)
    end = time.monotonic() + 0.8
    while time.monotonic() < end:
        pump(0.05)
    head = {"machine": MACHINE, "gpu": GPU, "mode": "x", "xgrab": os.environ.get("XGRAB", "1"), "xwarp": XWARP, "fs": FS, "size": [W, H], "grab": st["grab"], "focus": st["focus"]}
    print(json.dumps(head), flush=True)
    warp_at = [0]
    for rate in RATES:
        del log[:]
        snd.start(kind="rel", rate=rate, seconds=SECONDS)
        stamps = None
        while stamps is None:
            pump(0.005)
            stamps = snd.done()
            # a game warps back to the middle once a frame, as Source does
            if XWARP and mono() >= warp_at[0]:
                warp_at[0] = mono() + 16_000_000
                X.XWarpPointer(dp, 0, ctypes.c_ulong(win), 0, 0, 0, 0, W // 2, H // 2)
                X.XFlush(dp)
        end = time.monotonic() + 0.3
        while time.monotonic() < end:
            pump(0.05)
        allrels = [(at, v) for kind, at, v in log if kind == "rel"]
        sources = {}
        for at, v in allrels:
            sources["%d/%d" % (v[5], v[6])] = sources.get("%d/%d" % (v[5], v[6]), 0) + 1
        first = min(sources) if sources else ""
        rels = [(at, v) for at, v in allrels if "%d/%d" % (v[5], v[6]) == first]  # one source: the device/source pair named first
        # SDL2's rule for XI_RawMotion: an event with the X time and the raw values of the one before it is a duplicate and dropped
        # (Xwayland sends each relative motion twice while it emulates a lock for a grab); counted, and the rest is what a game sees
        dups, kept = 0, []
        for at, v in rels:
            if kept and kept[-1][1][0] == v[0] and kept[-1][1][3] == v[3]:
                dups += 1
                continue
            kept.append((at, v))
        rels = kept
        arrivals = [(at, int(round(v[3]))) for at, v in rels]
        lat = match(stamps, arrivals)
        span = (stamps[-1] - stamps[0]) / 1e9 if len(stamps) > 1 else 0
        row = dict(head, rate=rate, sent=len(stamps), sent_hz=round(len(stamps) / span) if span else None, events=len(rels),
                   sent_counts=sum(1 + k % 3 for k in range(len(stamps))), came_unaccel=sum(a[1] for a in arrivals),
                   came_accel=round(sum(v[1] for _, v in rels), 2), merged=sum(1 for _, c in arrivals if c not in (1, 2, 3)),
                   sources=sources, sdl_dups_dropped=dups, motions=sum(1 for kind, _, _ in log if kind == "motion"), recv_wakes=len({at for at, _ in rels}),
                   inject_to_client_us=quantiles(lat))
        print(json.dumps(row), flush=True)
        time.sleep(0.3)
    X.XUngrabPointer(dp, 0)
    X.XFlush(dp)
    keys_row(snd, pump, log, head)


def run_sdl(snd):
    """A real SDL2 game's mouse-look: SDL_SetRelativeMouseMode in a fullscreen SDL window (SDL_VIDEODRIVER=wayland: SDL's own
    zwp_relative_pointer + zwp_locked_pointer; x11: XI2 raw motion through Xwayland, with SDL's grab and its duplicate filter). Each
    SDL_MOUSEMOTION's xrel stamped as SDL_WaitEventTimeout hands it over, each SDL_KEYDOWN (not a repeat) the same."""
    S = ctypes.CDLL(os.environ.get("LIBSDL", "libSDL2-2.0.so.0"))
    S.SDL_CreateWindow.restype = ctypes.c_void_p
    S.SDL_GetWindowSurface.restype = ctypes.c_void_p
    S.SDL_GetCurrentVideoDriver.restype = ctypes.c_char_p
    S.SDL_GetError.restype = ctypes.c_char_p
    if S.SDL_Init(0x20) != 0:
        raise SystemExit("SDL_Init: %s" % S.SDL_GetError())
    centred = 0x2FFF0000
    win = ctypes.c_void_p(S.SDL_CreateWindow(b"gamelat-sdl", centred, centred, 800, 600, 0x1001 if FS else 0x4))
    if not win.value:
        raise SystemExit("SDL_CreateWindow: %s" % S.SDL_GetError())
    ev = ctypes.create_string_buffer(64)
    log, st = [], {"focus": 0}

    def draw():
        surf = ctypes.c_void_p(S.SDL_GetWindowSurface(win))
        if surf.value:
            S.SDL_FillRect(surf, None, 0x00203040)
            S.SDL_UpdateWindowSurface(win)

    def pump(timeout):
        if not S.SDL_WaitEventTimeout(ev, max(1, int(timeout * 1000))):
            return
        at = mono()
        while True:
            t = struct.unpack_from("<I", ev.raw)[0]
            if t == 0x400:  # SDL_MOUSEMOTION: type, timestamp, windowID, which, state, x, y, xrel, yrel
                log.append(("rel", at, struct.unpack_from("<i", ev.raw, 28)[0]))
            elif t == 0x300 and not ev.raw[13]:  # SDL_KEYDOWN, not a repeat
                log.append(("key", at, None))
            elif t == 0x200:  # SDL_WINDOWEVENT: 12 focus gained, 13 lost, 1 shown, 3 exposed
                e = ev.raw[12]
                if e == 12:
                    st["focus"] = 1
                elif e == 13:
                    st["focus"] = 0
                elif e in (1, 3, 6):
                    draw()
            if not S.SDL_PollEvent(ev):
                break

    draw()
    end = time.monotonic() + 1.5
    while time.monotonic() < end:
        pump(0.1)
    for _ in range(2):
        if st["focus"]:
            break
        snd.start(kind="move", n=20, dx=-3, dy=-2)
        while snd.done() is None:
            pump(0.02)
        snd.start(kind="click")
        while snd.done() is None:
            pump(0.02)
        end = time.monotonic() + 0.5
        while time.monotonic() < end:
            pump(0.05)
    rel_ok = S.SDL_SetRelativeMouseMode(1)
    end = time.monotonic() + 0.8
    while time.monotonic() < end:
        pump(0.05)
    head = {"machine": MACHINE, "gpu": GPU, "mode": "sdl", "driver": S.SDL_GetCurrentVideoDriver().decode(), "fs": FS,
            "focus": st["focus"], "relative": S.SDL_GetRelativeMouseMode(), "set_relative": rel_ok}
    print(json.dumps(head), flush=True)
    for rate in RATES:
        del log[:]
        snd.start(kind="rel", rate=rate, seconds=SECONDS)
        stamps = None
        while stamps is None:
            pump(0.005)
            stamps = snd.done()
        end = time.monotonic() + 0.3
        while time.monotonic() < end:
            pump(0.05)
        arrivals = [(at, v) for kind, at, v in log if kind == "rel"]
        lat = match(stamps, arrivals)
        json.dump(lat, open("%s/lat-sdl-%s-%d.json" % (OUT, head["driver"], rate), "w"))
        span = (stamps[-1] - stamps[0]) / 1e9 if len(stamps) > 1 else 0
        row = dict(head, rate=rate, sent=len(stamps), sent_hz=round(len(stamps) / span) if span else None, events=len(arrivals),
                   sent_counts=sum(1 + k % 3 for k in range(len(stamps))), came_unaccel=sum(v for _, v in arrivals), came_accel=0,
                   merged=sum(1 for _, v in arrivals if v not in (1, 2, 3)), recv_wakes=len({at for at, _ in arrivals}),
                   inject_to_client_us=quantiles(lat))
        print(json.dumps(row), flush=True)
        time.sleep(0.3)
    keys_row(snd, pump, log, head)
    S.SDL_SetRelativeMouseMode(0)
    S.SDL_Quit()


server = None
if (MODE == "x" or (MODE == "sdl" and os.environ.get("SDL_VIDEODRIVER") == "x11")) and not os.environ.get("DISPLAY"):
    # the bowl's X server, started here (the wrapper closes descriptors it does not know, and the uinput ones are ours)
    import subprocess
    server = subprocess.Popen([os.environ.get("XWRAP", "/usr/local/bin/xwayland-moonwater")], stdout=subprocess.PIPE, pass_fds=())
    os.environ["DISPLAY"] = server.stdout.readline().decode().strip()
    time.sleep(0.5)
snd = Sender()
try:
    {"wl": run_wl, "photon": run_wl, "x": run_x, "sdl": run_sdl}[MODE](snd)
finally:
    for fd in (MOUSE, KBD):
        try:
            fcntl.ioctl(fd, UI_DEV_DESTROY)
        except OSError:
            pass
    if server:
        server.terminate()
