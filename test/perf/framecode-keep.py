"""framecode-keep.py TITLE OUTDIR: a framecode window that follows its configures (cedar, Round 2 resize storm). Draws frame i (its number in
the top and bottom bands, a parity colour) at every frame callback, forever, at the size the compositor configures it to (a new pair of
buffers for each new size). Writes OUTDIR/fc-timeline.txt: 'i monotonic-seconds w h' per committed frame. Killed by its driver."""
import mmap, os, socket, struct, sys, time
TITLE = sys.argv[1] if len(sys.argv) > 1 else "framecode-storm"
OUTDIR = sys.argv[2] if len(sys.argv) > 2 else "/tmp/cperf/runs/storm"
SOCK = os.environ.get("WAYLAND_SOCKET", "/run/moonwater/wayland-0")
TL = open(os.path.join(OUTDIR, "fc-timeline.txt"), "a")


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

    def read(self):
        self.s.settimeout(0.02)
        try:
            data = self.s.recv(65536)
        except socket.timeout:
            return []
        if not data:
            raise SystemExit("compositor closed")
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


def draw(frame, i, W, H):
    bgpx = bytes((0x20, 0x20, 0x20, 0)) if i % 2 == 0 else bytes((0x20, 0x40, 0x20, 0))
    on, off = bytes((255, 255, 255, 0)), bytes((0, 0, 0, 0))
    band = bgpx * 4
    for k in range(16):
        band += (on if (i >> k) & 1 else off) * 14 + bgpx * 4
    band += bgpx * (W - len(band) // 4)
    row_bg = bgpx * W
    frame[:] = b"".join(band if (4 <= y < 16 or 36 <= y < 48) else row_bg for y in range(H))


class Pool:
    def __init__(self, w, shm, wire, W, H):
        self.W, self.H = W, H
        size = 2 * W * H * 4
        self.fd = os.memfd_create("framecode-storm", 0)
        os.ftruncate(self.fd, size)
        self.mm = mmap.mmap(self.fd, size)
        self.pool = wire.new()
        wire.send(shm, 0, u32(self.pool) + i32(size), fd=self.fd)
        self.buffers = []
        for b in range(2):
            buf = wire.new()
            wire.send(self.pool, 0, u32(buf) + i32(b * W * H * 4) + i32(W) + i32(H) + i32(W * 4) + u32(1))
            self.buffers.append(buf)
        self.released = {b: True for b in self.buffers}


def main():
    w = Wire(SOCK)
    registry = w.new()
    w.send(1, 1, u32(registry))
    names = {}
    for _ in range(6):
        for obj, op, body in w.read():
            if obj == registry and op == 0:
                name = struct.unpack_from("<I", body)[0]
                n = struct.unpack_from("<I", body, 4)[0]
                names[body[8:8 + n - 1].decode()] = name
        if {"wl_compositor", "wl_shm", "xdg_wm_base"} <= names.keys():
            break
    objs = {}
    for iface, ver in (("wl_compositor", 4), ("wl_shm", 1), ("xdg_wm_base", 1)):
        obj = w.new()
        w.send(registry, 0, u32(names[iface]) + string(iface) + u32(ver) + u32(obj))
        objs[iface] = obj
    comp, shm, wm = objs["wl_compositor"], objs["wl_shm"], objs["xdg_wm_base"]
    surface = w.new(); w.send(comp, 0, u32(surface))
    xsurf = w.new(); w.send(wm, 2, u32(xsurf) + u32(surface))
    top = w.new(); w.send(xsurf, 1, u32(top))
    w.send(top, 2, string(TITLE))
    w.send(top, 3, string("framecode"))
    w.send(surface, 6)
    W, H = 320, 120
    pool = None
    configured = False
    size_ack = None
    pending_size = None
    i = 0
    while True:
        for obj, op, body in w.read():
            if obj == wm and op == 0:
                w.send(wm, 3, body)                              # ping: pong
            if obj == top and op == 0:                           # configure w h states: follow the size asked
                cw, ch = struct.unpack_from("<ii", body)
                if cw > 0 and ch > 0 and (cw, ch) != (W, H):
                    pending_size = (cw, ch)
            if obj == xsurf and op == 0:                         # configure serial: acknowledge, and the new size takes effect
                w.send(xsurf, 4, body)
                configured = True
                if pending_size:
                    W, H = pending_size
                    pending_size = None
                    pool = None
            if pool is not None and obj in pool.released and op == 0:
                pool.released[obj] = True
        if not configured:
            continue
        if pool is None:
            pool = Pool(w, shm, w, W, H)
            w.send(surface, 6)
            continue
        idx = i % 2
        buf = pool.buffers[idx]
        if not pool.released.get(buf, False):
            continue
        frame = memoryview(pool.mm)[idx * W * H * 4:(idx + 1) * W * H * 4]
        draw(frame, i, W, H)
        pool.released[buf] = False
        cb = w.new()
        w.send(surface, 3, u32(cb))
        w.send(surface, 1, u32(buf) + i32(0) + i32(0))
        w.send(surface, 2, i32(0) + i32(0) + i32(W) + i32(H))
        w.send(surface, 6)
        TL.write("%d %.6f %d %d\n" % (i, time.monotonic(), W, H))
        TL.flush()
        got = False
        while not got:
            for obj, op, body in w.read():
                if obj == cb and op == 0:
                    got = True
                if obj in pool.released and op == 0:
                    pool.released[obj] = True
                if obj == wm and op == 0:
                    w.send(wm, 3, body)
                if obj == top and op == 0:
                    cw, ch = struct.unpack_from("<ii", body)
                    if cw > 0 and ch > 0 and (cw, ch) != (W, H):
                        pending_size = (cw, ch)
                if obj == xsurf and op == 0:
                    w.send(xsurf, 4, body)
                    if pending_size:
                        W, H = pending_size
                        pending_size = None
                        pool = None
                        got = True
        i += 1


main()
