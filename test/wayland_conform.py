# What the protocol lets a client do that the shim once got wrong, run by test/wayland_e2e.sh against the headless compositor on
# /run/moonwater/wayland-0 (800 by 600, its screen the file named as the one argument). One line a row, "ok NAME" or "FAIL NAME detail";
# the exit status is the number of failures.
#
#   a buffer destroyed between its attach and the commit is no protocol error (the client is kept);
#   a window with a subsurface that commits a new buffer with damage wholly outside it still shows the subsurface;
#   a client with two buffers that damages all of each (so the compositor reads them where they are) and draws on its frame
#   callbacks always has a buffer free when the callback comes;
#   and the same client put away (minimized), drawing without callbacks, gets back the buffer each commit replaced.
import array, os, select, socket, struct, sys, time

PATH = '/run/moonwater/wayland-0'
SCAN = sys.argv[1]
failures = 0


def row(ok, name, detail=''):
    global failures
    print(('ok ' + name) if ok else ('FAIL %s %s' % (name, detail)))
    sys.stdout.flush()
    if not ok:
        failures += 1


def string(text):
    raw = text.encode() + b'\0'
    return struct.pack('<I', len(raw)) + raw + b'\0' * (-len(raw) % 4)


class Client:
    """A connection with wl_compositor, wl_shm, wl_subcompositor and xdg_wm_base bound, the wire packed by hand."""

    def __init__(self):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.connect(PATH)
        self.next = 1
        self.data = b''
        self.seen = []
        registry = self.new()
        self.send(1, 1, struct.pack('<I', registry))
        self.sync()
        names = {}
        for obj, op, body in self.seen:
            if obj == registry and op == 0:
                name, length = struct.unpack_from('<II', body)
                names[body[8:8 + length - 1].decode()] = name
        self.bound = {}
        for interface, version in (('wl_compositor', 4), ('wl_shm', 1), ('wl_subcompositor', 1), ('xdg_wm_base', 1)):
            self.bound[interface] = self.new()
            self.send(registry, 0, struct.pack('<I', names[interface]) + string(interface) + struct.pack('<II', version, self.bound[interface]))

    def new(self):
        self.next += 1
        return self.next

    def send(self, obj, op, body=b'', fds=()):
        text = struct.pack('<II', obj, (8 + len(body)) << 16 | op) + body
        if fds:
            self.s.sendmsg([text], [(socket.SOL_SOCKET, socket.SCM_RIGHTS, array.array('i', fds))])
        else:
            self.s.sendall(text)

    def until(self, want, wait):
        """Events for up to wait seconds, or until want(obj, op, body) is true of one, which is returned."""
        end = time.time() + wait
        while time.time() < end:
            if not select.select([self.s], [], [], max(0, end - time.time()))[0]:
                continue
            try:
                got = self.s.recv(65536)
            except OSError:
                return None
            if not got:
                return None
            self.data += got
            found = None
            while len(self.data) >= 8:
                obj, head = struct.unpack_from('<II', self.data)
                if len(self.data) < head >> 16:
                    break
                event = (obj, head & 0xffff, self.data[8:head >> 16])
                self.data = self.data[head >> 16:]
                self.seen.append(event)
                if found is None and want(*event):
                    found = event
            if found is not None:
                return found
        return None

    def sync(self, wait=3):
        done = self.new()
        self.send(1, 0, struct.pack('<I', done))
        return self.until(lambda obj, op, body: obj == done, wait) is not None

    def error(self):
        for obj, op, body in self.seen:
            if obj == 1 and op == 0:
                length = struct.unpack_from('<I', body, 8)[0]
                return body[12:12 + length - 1].decode(errors='replace')
        return None

    def pool(self, size, fill):
        fd = os.memfd_create('pool')
        os.ftruncate(fd, size)
        os.pwrite(fd, fill * (size // len(fill)), 0)
        at = self.new()
        self.send(self.bound['wl_shm'], 0, struct.pack('<Ii', at, size), [fd])
        return fd, at

    def buffer(self, pool, offset, width, height):
        at = self.new()
        self.send(pool, 0, struct.pack('<Iiiiii', at, offset, width, height, width * 4, 1))
        return at

    def window(self):
        surface, shell, top = self.new(), self.new(), self.new()
        self.send(self.bound['wl_compositor'], 0, struct.pack('<I', surface))
        self.send(self.bound['xdg_wm_base'], 2, struct.pack('<II', shell, surface))
        self.send(shell, 1, struct.pack('<I', top))
        self.send(surface, 6)
        configure = self.until(lambda obj, op, body: obj == shell and op == 0, 3)
        self.send(shell, 4, configure[2][:4] if configure else struct.pack('<I', 0))
        return surface, shell, top


def ink(colour):
    """How many pixels of the screen are the colour (blue, green, red)."""
    raw = open(SCAN, 'rb').read()
    want = bytes(colour)
    return sum(1 for at in range(0, len(raw), 4) if raw[at:at + 3] == want)


W, H = 200, 100

# A buffer destroyed between its attach and the commit.
c = Client()
surface, shell, top = c.window()
fd, pool = c.pool(W * H * 4 * 2, b'\x50')
shown, other = c.buffer(pool, 0, W, H), c.buffer(pool, W * H * 4, W, H)
c.send(surface, 1, struct.pack('<Iii', shown, 0, 0))
c.send(surface, 6)
c.send(surface, 1, struct.pack('<Iii', other, 0, 0))
c.send(other, 0)
try:
    c.send(surface, 6)
    kept = c.sync(2)
except OSError:
    kept = False
row(kept and not c.error(), 'a buffer destroyed between its attach and the commit is an attach unmade, and the client is kept', c.error())
c.s.close()

# Damage wholly outside the buffer, on a window with a subsurface.
c = Client()
surface, shell, top = c.window()
fd, pool = c.pool(W * H * 4 * 2, b'\x50')
os.pwrite(fd, b'\x70' * (W * H * 4), W * H * 4)
one, two = c.buffer(pool, 0, W, H), c.buffer(pool, W * H * 4, W, H)
c.send(surface, 1, struct.pack('<Iii', one, 0, 0))
c.send(surface, 6)
child, sub = c.new(), c.new()
c.send(c.bound['wl_compositor'], 0, struct.pack('<I', child))
c.send(c.bound['wl_subcompositor'], 1, struct.pack('<III', sub, child, surface))
c.send(sub, 1, struct.pack('<ii', 20, 20))
c.send(sub, 5)
fd2, pool2 = c.pool(40 * 40 * 4, bytes([0x10, 0xff, 0x20, 0xff]))
c.send(child, 1, struct.pack('<Iii', c.buffer(pool2, 0, 40, 40), 0, 0))
c.send(child, 6)
c.send(surface, 6)
c.until(lambda obj, op, body: False, 0.5)
before = ink((0x10, 0xff, 0x20))
c.send(surface, 1, struct.pack('<Iii', two, 0, 0))
c.send(surface, 9, struct.pack('<iiii', 5000, 5000, 10, 10))
c.send(surface, 6)
c.until(lambda obj, op, body: False, 0.5)
after = ink((0x10, 0xff, 0x20))
row(before >= 1500 and after == before and not c.error(), 'damage wholly outside the buffer leaves the subsurface over the window',
    '%d pixels of it before, %d after, %s' % (before, after, c.error()))
c.s.close()

# Two buffers, all of each damaged, drawn on the frame callbacks; then put away and drawn with no callbacks.
c = Client()
surface, shell, top = c.window()
fd, pool = c.pool(W * H * 4 * 2, b'\x60')
buffers = [c.buffer(pool, 0, W, H), c.buffer(pool, W * H * 4, W, H)]
free = list(buffers)


def take(obj, op, body):
    if obj in buffers and op == 0 and obj not in free:
        free.append(obj)
    return False


def draw(callback):
    buffer = free.pop(0)
    c.send(surface, 1, struct.pack('<Iii', buffer, 0, 0))
    if callback:  # all of it damaged; put away, no damage is said at all, which is the whole picture too
        c.send(surface, 9, struct.pack('<iiii', 0, 0, W, H))
        c.send(surface, 3, struct.pack('<I', callback))
    c.send(surface, 6)


starved = late = 0
for frame in range(30):
    if not free:
        starved += 1
        c.until(lambda obj, op, body: take(obj, op, body) or bool(free), 1)
        if not free:
            break
    callback = c.new()
    draw(callback)
    if c.until(lambda obj, op, body: take(obj, op, body) or obj == callback, 1) is None:
        late += 1
row(starved == 0 and late == 0 and not c.error(), 'a client with two buffers, all of each damaged, has one free at every frame callback of thirty',
    'no buffer free at %d callbacks, no callback within a second %d times, %s' % (starved, late, c.error()))
c.send(top, 13)  # set_minimized
c.until(take, 0.3)
starved = 0
for frame in range(6):
    if not free:
        c.until(lambda obj, op, body: take(obj, op, body) or bool(free), 1)
    if not free:
        starved += 1
        break
    draw(0)
    c.until(take, 0.05)
row(starved == 0 and not c.error(), 'put away, it draws six times with no callback and is given back the buffer each commit replaced',
    'no buffer released within a second, %s' % c.error())
c.s.close()
sys.exit(failures)
