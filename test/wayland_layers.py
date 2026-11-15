# A window with subsurfaces, run by test/wayland_e2e.sh against the headless compositor on /run/moonwater/wayland-0 (800 by 600, its
# screen the file named as the first argument; the second is the shell reached as `moonwater`, whose debug page counts the pictures).
# The compositor hands a window's subsurfaces to the screen as layers of their own, each read where the client keeps it (wayland.c's
# row_layers): what the screen shows is what one picture with the subsurfaces laid into it showed, and nothing is copied for it.
#
#   an opaque subsurface over the window, committed with it: it is on the screen at its place, the window round it;
#   its commit alone (desynchronized), another buffer: the screen has the new one, nothing of the window copied (the debug page's counts);
#   a translucent one: laid over what is under it, as premultiplied alpha is;
#   one under the window (place_below the parent), which the opaque window hides: none of it shows;
#   one moved, and two put in the other order where they overlap, and one taken off (a null buffer): each is what the screen shows;
#   nine at once (more than a window has layers): laid into one picture, as before, and still what the screen shows;
#   the pool of one cut under the compositor while it is read in place: the compositor goes on, and the next client is drawn.
#
# One line a row, "ok NAME" or "FAIL NAME detail"; the exit status is the number of failures.
import array, os, select, socket, struct, subprocess, sys, time

SCAN = sys.argv[1]
MOONWATER = sys.argv[2] if len(sys.argv) > 2 else ''
SW, SH = 800, 600
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect('/run/moonwater/wayland-0')
ids = [1]
data = b''
failures = 0
W, H = 240, 160
GROUND, INK, OTHER, THIRD = (0x31, 0x52, 0x73), (0x10, 0xf0, 0x20), (0xe0, 0x30, 0xd0), (0x20, 0x40, 0xf0)   # blue, green, red


def row(ok, name, detail=''):
    global failures
    print(('ok ' + name) if ok else ('FAIL %s %s' % (name, detail)))
    sys.stdout.flush()
    failures += 0 if ok else 1


def new():
    ids[0] += 1
    return ids[0]


def send(obj, op, body=b'', fds=(), to=None):
    text = struct.pack('<II', obj, (8 + len(body)) << 16 | op) + body
    if fds:
        (to or s).sendmsg([text], [(socket.SOL_SOCKET, socket.SCM_RIGHTS, array.array('i', fds))])
    else:
        (to or s).sendall(text)


def events(wait, of=None):
    global data
    out = []
    end = time.time() + wait
    while time.time() < end:
        if not select.select([of or s], [], [], max(0, end - time.time()))[0]:
            continue
        got = (of or s).recv(65536)
        if not got:
            break
        data += got
        while len(data) >= 8:
            obj, head = struct.unpack_from('<II', data)
            if len(data) < head >> 16:
                break
            out.append((obj, head & 0xffff, data[8:head >> 16]))
            data = data[head >> 16:]
    return out


def string(text):
    raw = text.encode() + b'\0'
    return struct.pack('<I', len(raw)) + raw + b'\0' * (-len(raw) % 4)


def counts():
    """The debug page's client pictures: read in place, copied into the table (None when there is no shell to ask)."""
    if not MOONWATER:
        return None
    out = subprocess.run([MOONWATER, 'debug'], capture_output=True, text=True).stdout
    for line in out.splitlines():
        if 'client frames' in line:
            words = line.split()
            return int(words[words.index('frames') + 1]), int(words[words.index('place,') + 1])
    return 0, 0


def screen():
    return open(SCAN, 'rb').read()


def count(raw, colour, near=0):
    want = bytes(colour)
    if not near:
        return sum(1 for at in range(0, len(raw), 4) if raw[at:at + 3] == want)
    return sum(1 for at in range(0, len(raw), 4) if all(abs(raw[at + k] - want[k]) <= near for k in range(3)))


def place(raw):
    """The window's top left on the screen: the leftmost and the topmost pixel of its ground (its corners are round)."""
    x1, y1, want = SW, SH, bytes(GROUND)
    for y in range(SH):
        line = raw[y * SW * 4:(y + 1) * SW * 4]
        for x in range(SW):
            if line[x * 4:x * 4 + 3] == want:
                x1, y1 = min(x1, x), min(y1, y)
    return (x1, y1) if x1 < SW else None


def at(raw, origin, x, y):
    ox, oy = origin
    i = ((oy + y) * SW + ox + x) * 4
    return tuple(raw[i:i + 3])


registry = new()
send(1, 1, struct.pack('<I', registry))
names = {}
for obj, op, body in events(0.5):
    if obj == registry and op == 0:
        name, length = struct.unpack_from('<II', body)
        names[body[8:8 + length - 1].decode()] = name
bound = []
for interface, version in (('wl_compositor', 4), ('wl_shm', 1), ('xdg_wm_base', 1), ('wl_subcompositor', 1)):
    bound.append(new())
    send(registry, 0, struct.pack('<I', names[interface]) + string(interface) + struct.pack('<II', version, bound[-1]))
compositor, shm, wm, subcompositor = bound
surface, shell, top = new(), new(), new()
send(compositor, 0, struct.pack('<I', surface))
send(wm, 2, struct.pack('<II', shell, surface))
send(shell, 1, struct.pack('<I', top))
send(surface, 6)
for obj, op, body in events(0.5):
    if obj == shell and op == 0:
        send(shell, 4, body[:4])


def buffer(width, height, colour, alpha=0xff, frames=1):
    """A pool of frames buffers of one colour (premultiplied by alpha when it is not 0xff, ARGB then), its file and the buffers."""
    fd = os.memfd_create('layer')
    size = width * height * 4 * frames
    os.ftruncate(fd, size)
    if alpha == 0xff:
        pixel = bytes(colour) + b'\xff'
    else:
        pixel = bytes(c * alpha // 255 for c in colour) + bytes([alpha])
    os.pwrite(fd, pixel * (width * height * frames), 0)
    pool = new()
    send(shm, 0, struct.pack('<Ii', pool, size), [fd])
    made = []
    for k in range(frames):
        made.append(new())
        send(pool, 0, struct.pack('<Iiiiii', made[-1], k * width * height * 4, width, height, width * 4, 1 if alpha == 0xff else 0))
    return fd, made


def child(width, height, colour, x, y, alpha=0xff, frames=1):
    """A subsurface of the window at (x, y), its buffer attached and committed (shown with the window's next commit, as it is synchronized)."""
    made = new()
    send(compositor, 0, struct.pack('<I', made))
    sub = new()
    send(subcompositor, 1, struct.pack('<III', sub, made, surface))
    send(sub, 1, struct.pack('<ii', x, y))
    fd, buffers = buffer(width, height, colour, alpha, frames)
    send(made, 1, struct.pack('<Iii', buffers[0], 0, 0))
    send(made, 9, struct.pack('<iiii', 0, 0, width, height))
    send(made, 6)
    return made, sub, fd, buffers


root_fd, root_buffers = buffer(W, H, GROUND)
send(surface, 1, struct.pack('<Iii', root_buffers[0], 0, 0))
send(surface, 9, struct.pack('<iiii', 0, 0, W, H))
send(surface, 6)
events(0.5)
raw = screen()
whole = count(raw, GROUND)
origin = place(raw)
row(origin is not None and whole >= W * H - 2 * (W + H), 'the window is on the screen in its ground colour', '%d pixels of it at %s' % (whole, origin))
origin = origin or (0, 0)

before = counts()
a, a_sub, a_fd, a_buffers = child(60, 40, INK, 20, 30, frames=2)
send(surface, 6)
events(0.5)
raw = screen()
row(count(raw, INK) == 60 * 40 and count(raw, GROUND) == whole - 60 * 40 and at(raw, origin, 20, 30) == INK and at(raw, origin, 79, 69) == INK and
    at(raw, origin, 80, 70) == GROUND,
    'an opaque subsurface committed with its window is on the screen at its place, the window round it',
    '%d of it, %d of the ground of %d' % (count(raw, INK), count(raw, GROUND), whole))

# Its commits alone, a buffer at a time, as a video's: what the screen shows is the new buffer, and nothing of the window is copied.
send(a_sub, 5)
os.pwrite(a_fd, (bytes(OTHER) + b'\xff') * (60 * 40), 60 * 40 * 4)
send(a, 1, struct.pack('<Iii', a_buffers[1], 0, 0))
send(a, 9, struct.pack('<iiii', 0, 0, 60, 40))
send(a, 6)
events(0.5)
raw = screen()
row(count(raw, OTHER) == 60 * 40 and count(raw, INK) == 0 and at(raw, origin, 20, 30) == OTHER,
    'its commit alone (desynchronized), another buffer, is what the screen shows', '%d of the new, %d of the old' % (count(raw, OTHER), count(raw, INK)))
for k in range(6):
    send(a, 1, struct.pack('<Iii', a_buffers[k & 1], 0, 0))
    send(a, 9, struct.pack('<iiii', 0, 0, 60, 40))
    send(a, 6)
    events(0.1)
after = counts()
if before is None:
    print('NOT RUN none of a subsurface\'s commits is copied into the table: no shell named to read the debug page from')
else:
    row(after[1] == before[1] and after[0] - before[0] >= 6,
        'none of a subsurface\'s commits is copied into the table: each is read in place (moonwater debug counts them)',
        'read in place %d then %d, copied %d then %d' % (before[0], after[0], before[1], after[1]))
send(a_sub, 4)

# Translucent, premultiplied: laid over the window under it.
b, b_sub, b_fd, b_buffers = child(40, 40, THIRD, 100, 60, alpha=0x80)
send(surface, 6)
events(0.5)
raw = screen()
mixed = tuple(THIRD[k] * 0x80 // 255 + (GROUND[k] * (255 - 0x80) + 127) // 255 for k in range(3))
got = at(raw, origin, 110, 70)
row(count(raw, mixed, near=2) == 40 * 40 and all(abs(got[k] - mixed[k]) <= 2 for k in range(3)),
    'a translucent subsurface is laid over the window under it, as premultiplied alpha is', '%d of %s, at its middle %s' % (count(raw, mixed, near=2), mixed, got))

# Under the window, which is opaque and hides it whole: none of it shows.
c, c_sub, c_fd, c_buffers = child(30, 30, THIRD, 150, 100)
send(c_sub, 3, struct.pack('<I', surface))
send(surface, 6)
events(0.5)
raw = screen()
row(count(raw, THIRD) == 0 and at(raw, origin, 160, 110) == GROUND, 'a subsurface under its opaque window, inside it, does not show',
    '%d of it' % count(raw, THIRD))

# Moved: at its new place only.
send(a_sub, 1, struct.pack('<ii', 170, 20))
send(surface, 6)
events(0.5)
raw = screen()
row(at(raw, origin, 170, 20) == OTHER and at(raw, origin, 20, 30) == GROUND and count(raw, OTHER) == 60 * 40,
    'a subsurface moved is at its new place, and the window is where it was', 'at the new %s, at the old %s' % (at(raw, origin, 170, 20), at(raw, origin, 20, 30)))

# Two that overlap, put in the other order (the second past the root's right edge: the window takes it in, and is laid as one picture while it does).
d, d_sub, d_fd, d_buffers = child(60, 40, INK, 190, 30)
send(surface, 6)
events(0.5)
raw = screen()
first = at(raw, origin, 200, 40)
send(d_sub, 3, struct.pack('<I', a))
send(surface, 6)
events(0.5)
raw = screen()
row(first == INK and at(raw, origin, 200, 40) == OTHER and at(raw, origin, 175, 25) == OTHER and at(raw, origin, 235, 65) == INK and
    count(raw, INK) == 60 * 40 - 40 * 30,
    'two subsurfaces that overlap, put in the other order, show the other one where they meet', 'before %s, after %s, %d of the lower' % (
        first, at(raw, origin, 200, 40), count(raw, INK)))

# Taken off: a null buffer.
send(d, 1, struct.pack('<Iii', 0, 0, 0))
send(d, 6)
send(surface, 6)
events(0.5)
raw = screen()
row(count(raw, INK) == 0 and at(raw, origin, 235, 65) == GROUND, 'a subsurface given no buffer is off the window, and the window shows where it was',
    '%d of it' % count(raw, INK))

# Nine: more than a window has layers, laid into one picture, and still the picture.
nine = []
for k in range(9):
    nine.append(child(8, 8, INK, 10 + k * 12, 140))
send(surface, 6)
events(0.6)
raw = screen()
row(count(raw, INK) == 9 * 64 and count(raw, OTHER) == 60 * 40 and at(raw, origin, 10 + 8 * 12, 140) == INK,
    'nine subsurfaces, more than a window has layers, are laid into one picture and are what the screen shows', '%d of them' % count(raw, INK))
for made, sub, fd, buffers in nine:
    send(sub, 0)
    send(made, 0)
send(surface, 6)
events(0.6)
raw = screen()
row(count(raw, INK) == 0 and count(raw, OTHER) == 60 * 40, 'and taken away again, the window is as it was', '%d of them left' % count(raw, INK))

# The pool of a subsurface read in place cut under the compositor, which then draws it: it goes on, and the next client is drawn.
os.ftruncate(a_fd, 0)
send(a_sub, 1, struct.pack('<ii', 20, 30))
send(surface, 6)
events(0.5)
other = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
other.connect('/run/moonwater/wayland-0')
data = b''
ids = [1]
registry = new()
send(1, 1, struct.pack('<I', registry), to=other)
events(0.5, other)
bound = []
for interface, version in (('wl_compositor', 4), ('wl_shm', 1), ('xdg_wm_base', 1)):
    bound.append(new())
    send(registry, 0, struct.pack('<I', names[interface]) + string(interface) + struct.pack('<II', version, bound[-1]), to=other)
surface2, shell2, top2, pool2, buffer2, done = (new() for _ in range(6))
send(bound[0], 0, struct.pack('<I', surface2), to=other)
send(bound[2], 2, struct.pack('<II', shell2, surface2), to=other)
send(shell2, 1, struct.pack('<I', top2), to=other)
send(surface2, 6, to=other)
for obj, op, body in events(0.5, other):
    if obj == shell2 and op == 0:
        send(shell2, 4, body[:4], to=other)
fd = os.memfd_create('next')
os.ftruncate(fd, 300 * 200 * 4)
send(bound[1], 0, struct.pack('<Ii', pool2, 300 * 200 * 4), [fd], to=other)
send(pool2, 0, struct.pack('<Iiiiii', buffer2, 0, 300, 200, 1200, 1), to=other)
send(surface2, 1, struct.pack('<Iii', buffer2, 0, 0), to=other)
send(surface2, 3, struct.pack('<I', done), to=other)
send(surface2, 6, to=other)
row(any(obj == done for obj, op, body in events(1.5, other)), 'a subsurface\'s file cut while it is read in place, the compositor goes on and the next client is drawn')
sys.exit(failures)
