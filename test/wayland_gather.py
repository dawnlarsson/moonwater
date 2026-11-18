# A window's subsurfaces that are turned, flipped, cropped at an offset or scaled, run by test/wayland_e2e.sh against the headless compositor on
# /run/moonwater/wayland-0 (800 by 600, its screen the file named as the first argument; the second is the shell reached as `moonwater`, whose debug
# page counts the layers). Each such subsurface is a layer gathered into a picture of its own (by the card with the GPU, by the processor without it)
# and composed over its window, where it was laid into the window's picture by the processor before: what the screen shows is each pixel where
# Weston's weston_transformed_coord and the viewporter put it (the source cell whose centre is the point's), and nothing is laid into the table.
#
#   seven subsurfaces of one pattern, set_buffer_transform 1 to 7 (90, 180, 270 and the flipped four): each turned and flipped as Weston turns it;
#   a viewport's destination twice the buffer's size, one half of it, a source at an offset scaled up, a source of fractions, ABGR turned 180,
#   translucent ARGB turned 270 laid over the window as premultiplied alpha is: each pixel where it belongs;
#   a turned one's commit alone (desynchronized) of another buffer: the screen has the new picture;
#   none of them was laid into the window's picture (the debug page's client layers line);
#   y-inverted dma-bufs made by /dev/udmabuf, one turned 90 too: each pixel where it belongs;
#   three windows whose own buffers are turned (one ABGR), or cropped and scaled by their viewport: laid into their pictures by the card where it
#   composes, by the processor's walk where it does not.
#
# One line a row, "ok NAME" or "FAIL NAME detail"; the exit status is the number of failures.
import array, os, select, socket, struct, subprocess, sys, time

SCAN = sys.argv[1]
MOONWATER = sys.argv[2] if len(sys.argv) > 2 else ''
SW, SH = 800, 600
W, H = 300, 200
GROUND = (0x31, 0x52, 0x73)
BW, BH = 48, 32
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect('/run/moonwater/wayland-0')
ids = [1]
data = b''
failures = 0


def row(ok, name, detail=''):
    global failures
    print(('ok ' + name) if ok else ('FAIL %s %s' % (name, detail)))
    sys.stdout.flush()
    failures += 0 if ok else 1


def new():
    ids[0] += 1
    return ids[0]


def send(obj, op, body=b'', fds=()):
    text = struct.pack('<II', obj, (8 + len(body)) << 16 | op) + body
    if fds:
        s.sendmsg([text], [(socket.SOL_SOCKET, socket.SCM_RIGHTS, array.array('i', fds))])
    else:
        s.sendall(text)


def events(wait):
    global data
    out = []
    end = time.time() + wait
    while time.time() < end:
        if not select.select([s], [], [], max(0, end - time.time()))[0]:
            continue
        got = s.recv(65536)
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


def layers():
    """The debug page's client layers: laid into the table by the processor, gathered by the card, by the processor (None with no shell to ask)."""
    if not MOONWATER:
        return None
    out = subprocess.run([MOONWATER, 'debug'], capture_output=True, text=True).stdout
    for line in out.splitlines():
        if 'client layers' in line:
            words = line.split()
            return int(words[2]), int(words[words.index('card,') - 4]), int(words[words.index('card,') + 1])
    return 0, 0, 0


def screen():
    return open(SCAN, 'rb').read()


def place(raw):
    """The window's top left on the screen: the leftmost and the topmost pixel of its ground (its corners are round)."""
    x1, y1, want = SW, SH, bytes((GROUND[2], GROUND[1], GROUND[0]))
    for y in range(SH):
        line = raw[y * SW * 4:(y + 1) * SW * 4]
        for x in range(SW):
            if line[x * 4:x * 4 + 3] == want:
                x1, y1 = min(x1, x), min(y1, y)
    return (x1, y1) if x1 < SW else None


def at(raw, origin, x, y):
    i = ((origin[1] + y) * SW + origin[0] + x) * 4
    return (raw[i + 2], raw[i + 1], raw[i])


def pattern(x, y, salt=0):
    """The buffer's pixel at x, y as red, green and blue: every one of its 48 by 32 another colour."""
    return ((x * 5 + 1 + salt) & 255, (y * 7 + 2) & 255, (x * 3 + y * 11 + 40 + salt) & 255)


def source(transform, sx, sy, sw, sh, tw, th, ox, oy):
    """The buffer's pixel drawn at ox, oy of a surface of tw by th: the source cell whose centre is the point's, in the viewport's source of the
    turned buffer (sx, sy, sw, sh in 1/256 pixels), turned back as libweston's weston_transformed_coord does."""
    turned_w, turned_h = (BH, BW) if transform & 1 else (BW, BH)
    tx = min(max((sx + ((ox * 2 + 1) * sw) // (2 * tw)) >> 8, 0), turned_w - 1)
    ty = min(max((sy + ((oy * 2 + 1) * sh) // (2 * th)) >> 8, 0), turned_h - 1)
    return {0: (tx, ty), 1: (BW - 1 - ty, tx), 2: (BW - 1 - tx, BH - 1 - ty), 3: (ty, BH - 1 - tx), 4: (BW - 1 - tx, ty),
            5: (BW - 1 - ty, BH - 1 - tx), 6: (tx, BH - 1 - ty), 7: (ty, tx)}[transform]


registry = new()
send(1, 1, struct.pack('<I', registry))
names = {}
for obj, op, body in events(0.5):
    if obj == registry and op == 0:
        name, length = struct.unpack_from('<II', body)
        names[body[8:8 + length - 1].decode()] = name
bound = []
for interface, version in (('wl_compositor', 4), ('wl_shm', 1), ('xdg_wm_base', 1), ('wl_subcompositor', 1), ('wp_viewporter', 1)):
    bound.append(new())
    send(registry, 0, struct.pack('<I', names[interface]) + string(interface) + struct.pack('<II', version, bound[-1]))
compositor, shm, wm, subcompositor, viewporter = bound
dmabuf = None
if 'zwp_linux_dmabuf_v1' in names:
    dmabuf = new()
    send(registry, 0, struct.pack('<I', names['zwp_linux_dmabuf_v1']) + string('zwp_linux_dmabuf_v1') + struct.pack('<II', 3, dmabuf))
surface, shell, top = new(), new(), new()
send(compositor, 0, struct.pack('<I', surface))
send(wm, 2, struct.pack('<II', shell, surface))
send(shell, 1, struct.pack('<I', top))
send(surface, 6)
for obj, op, body in events(0.5):
    if obj == shell and op == 0:
        send(shell, 4, body[:4])


def pool(width, height, pixels, form):
    """A buffer of width by height of the pixels given (red, green, blue, alpha a pixel, premultiplied), in form (0 ARGB, 1 XRGB, or a fourcc)."""
    fd = os.memfd_create('gather')
    raw = bytearray()
    for r, g, b, a in pixels:
        raw += bytes((r, g, b, a)) if form == 0x34324241 else bytes((b, g, r, a))
    os.ftruncate(fd, len(raw))
    os.pwrite(fd, bytes(raw), 0)
    made, buffer = new(), new()
    send(shm, 0, struct.pack('<Ii', made, len(raw)), [fd])
    send(made, 0, struct.pack('<IiiiiI', buffer, 0, width, height, width * 4, form))
    return fd, buffer


def udmabuf(width, height, pixels, flags):
    """A linear XRGB dma-buf of width by height of the pixels given, made of a sealed memfd by /dev/udmabuf (flags 1: y_invert), or None where
    the machine has no udmabuf or the compositor no linux-dmabuf."""
    if dmabuf is None or not os.path.exists('/dev/udmabuf'):
        return None
    import fcntl
    size = (width * height * 4 + 4095) & ~4095
    fd = os.memfd_create('gather-dmabuf', os.MFD_ALLOW_SEALING)
    os.ftruncate(fd, size)
    os.pwrite(fd, b''.join(bytes((b, g, r, a)) for r, g, b, a in pixels), 0)
    fcntl.fcntl(fd, fcntl.F_ADD_SEALS, fcntl.F_SEAL_SHRINK)
    try:
        dev = os.open('/dev/udmabuf', os.O_RDWR)
        made = fcntl.ioctl(dev, 0x40187542, struct.pack('<IIQQ', fd, 1, 0, size))  # UDMABUF_CREATE, UDMABUF_FLAGS_CLOEXEC
        os.close(dev)
    except OSError:
        return None
    buf_fd = made if isinstance(made, int) else None
    if buf_fd is None or buf_fd < 0:
        return None
    params, buffer = new(), new()
    send(dmabuf, 1, struct.pack('<I', params))
    send(params, 1, struct.pack('<IIIII', 0, 0, width * 4, 0, 0), [buf_fd])
    send(params, 3, struct.pack('<IiiII', buffer, width, height, 0x34325258, flags))
    return buffer


def patterned(salt=0, alpha=0xff, form=1):
    return pool(BW, BH, [tuple(c * alpha // 255 for c in pattern(x, y, salt)) + (alpha,) for y in range(BH) for x in range(BW)], form)


def child(x, y, transform=0, viewport=None, salt=0, alpha=0xff, form=1, buffer=None):
    """A subsurface of the window at (x, y) showing the pattern, turned by transform, and with a viewport (source in 1/256 pixels or None, and a
    destination or None) when one is given; committed, shown with the window's next commit."""
    made = new()
    send(compositor, 0, struct.pack('<I', made))
    sub = new()
    send(subcompositor, 1, struct.pack('<III', sub, made, surface))
    send(sub, 1, struct.pack('<ii', x, y))
    if transform:
        send(made, 7, struct.pack('<i', transform))
    view = None
    if viewport:
        view = new()
        send(viewporter, 1, struct.pack('<II', view, made))
        crop, size = viewport
        if crop:
            send(view, 1, struct.pack('<iiii', *crop))
        if size:
            send(view, 2, struct.pack('<ii', *size))
    if buffer is None:
        fd, buffer = patterned(salt, alpha, form)
    send(made, 1, struct.pack('<Iii', buffer, 0, 0))
    send(made, 9, struct.pack('<iiii', 0, 0, BW, BH))
    send(made, 6)
    return made, sub, view


def gone(made, sub, view):
    if view:
        send(view, 0)
    send(sub, 0)
    send(made, 0)


def check(raw, origin, x, y, transform, crop, size, salt=0, alpha=0xff, inverted=False):
    """The pixels of a subsurface at (x, y) of the window that are not what they should be (each against its source cell), and the count of all."""
    turned_w, turned_h = (BH, BW) if transform & 1 else (BW, BH)
    sx, sy, sw, sh = crop if crop else (0, 0, turned_w * 256, turned_h * 256)
    tw, th = size if size else ((sw >> 8, sh >> 8) if crop else (turned_w, turned_h))
    wrong = []
    for oy in range(th):
        for ox in range(tw):
            bx, by = source(transform, sx, sy, sw, sh, tw, th, ox, oy)
            want = pattern(bx, BH - 1 - by if inverted else by, salt)
            if alpha != 0xff:
                want = tuple(c * alpha // 255 + (GROUND[k] * (255 - alpha) + 127) // 255 for k, c in enumerate(want))
            got = at(raw, origin, x + ox, y + oy)
            if any(abs(got[k] - want[k]) > (2 if alpha != 0xff else 0) for k in range(3)):
                wrong.append((ox, oy, got, want))
    return wrong, tw * th


root_fd, root_buffer = pool(W, H, [GROUND + (0xff,)] * (W * H), 1)
send(surface, 1, struct.pack('<Iii', root_buffer, 0, 0))
send(surface, 9, struct.pack('<iiii', 0, 0, W, H))
send(surface, 6)
events(0.5)
raw = screen()
origin = place(raw)
row(origin is not None, 'the window is on the screen in its ground colour', 'nowhere')
origin = origin or (0, 0)
before = layers()

# The seven turns and flips of one buffer, each a subsurface of its own.
spots = [(24 + (k % 4) * 64, 24 + (k // 4) * 72) for k in range(7)]
turned = [child(spots[k][0], spots[k][1], transform=k + 1) for k in range(7)]
send(surface, 6)
events(0.6)
raw = screen()
bad = []
for k in range(7):
    wrong, total = check(raw, origin, spots[k][0], spots[k][1], k + 1, None, None)
    if wrong:
        bad.append('transform %d: %d of %d wrong, the first %s' % (k + 1, len(wrong), total, wrong[0]))
row(not bad, 'seven subsurfaces turned and flipped (buffer transforms 1 to 7) are on the screen as Weston turns them', '; '.join(bad))
row(at(raw, origin, 20, 20) == GROUND and at(raw, origin, 24 + 32 + 1, 24) == GROUND and at(raw, origin, 24 + 64 + 48, 24) == GROUND,
    'the window shows round them', '%s %s %s' % (at(raw, origin, 20, 20), at(raw, origin, 57, 24), at(raw, origin, 136, 24)))

# One turned, its commit alone (desynchronized) of another buffer: the screen has the new picture.
made, sub, view = turned[0]
send(sub, 5)
fd, other = patterned(salt=60)
send(made, 1, struct.pack('<Iii', other, 0, 0))
send(made, 9, struct.pack('<iiii', 0, 0, BW, BH))
send(made, 6)
events(0.5)
raw = screen()
wrong, total = check(raw, origin, spots[0][0], spots[0][1], 1, None, None, salt=60)
row(not wrong, 'a turned subsurface\'s commit alone, another buffer, is what the screen shows', '%d of %d wrong, the first %s' % (len(wrong), total, wrong[:1]))
for made, sub, view in turned:
    gone(made, sub, view)
send(surface, 6)
events(0.5)

# The viewport's sizes and sources, a format of red and blue the other way round turned, and a translucent one turned.
cases = [
    ('a viewport\'s destination twice the buffer\'s size', 16, 16, 0, None, (96, 64), 0xff, 1),
    ('a viewport\'s destination half the buffer\'s size', 128, 16, 0, None, (24, 16), 0xff, 1),
    ('a source at an offset scaled up', 168, 16, 0, (8 * 256, 4 * 256, 20 * 256, 12 * 256), (40, 24), 0xff, 1),
    ('a source of fractions of a pixel', 16, 96, 0, (1152, 576, 30 * 256 + 128, 20 * 256 + 64), (61, 41), 0xff, 1),
    ('an ABGR buffer turned 180', 96, 96, 2, None, None, 0xff, 0x34324241),
    ('a translucent ARGB buffer turned 270, over the window', 168, 96, 3, None, None, 0x80, 0),
]
made_cases = [child(x, y, transform=t, viewport=(crop, size) if crop or size else None, alpha=a, form=f) for _, x, y, t, crop, size, a, f in cases]
send(surface, 6)
events(0.6)
raw = screen()
for (name, x, y, t, crop, size, a, f) in cases:
    wrong, total = check(raw, origin, x, y, t, crop, size, alpha=a)
    row(not wrong, name + ' is on the screen where each of its pixels belongs', '%d of %d wrong, the first %s' % (len(wrong), total, wrong[:1]))
after = layers()
if before is None:
    print('NOT RUN none of them was laid into the window\'s picture: no shell named to read the debug page from')
else:
    row(after[0] == before[0] and after[1] + after[2] > before[1] + before[2],
        'none of them was laid into the window\'s picture by the processor: each was gathered (moonwater debug counts them)',
        'laid %d then %d, gathered %d then %d' % (before[0], after[0], before[1] + before[2], after[1] + after[2]))
for made, sub, view in made_cases:
    gone(made, sub, view)

# Dma-bufs (udmabuf), y-inverted (the buffer's rows bottom first): one as it is, one turned 90 as well. The card gathers them where it composes;
# without it they are laid into the window's picture by the processor, which turns them the same.
flat = [pattern(x, BH - 1 - y) + (0xff,) for y in range(BH) for x in range(BW)]
inverted = [(name, x, y, t, udmabuf(BW, BH, flat, 1)) for name, x, y, t in (('a y-inverted dma-buf', 24, 24, 0), ('a y-inverted dma-buf turned 90', 120, 24, 1))]
if any(b is None for _, _, _, _, b in inverted):
    for name, _, _, _, _ in inverted:
        print('NOT RUN %s is on the screen where each of its pixels belongs: no /dev/udmabuf or no linux-dmabuf' % name)
else:
    made_dma = [child(x, y, transform=t, buffer=b) for _, x, y, t, b in inverted]
    send(surface, 6)
    events(0.6)
    raw = screen()
    for name, x, y, t, b in inverted:
        wrong, total = check(raw, origin, x, y, t, None, None)
        row(not wrong, name + ' is on the screen where each of its pixels belongs', '%d of %d wrong, the first %s' % (len(wrong), total, wrong[:1]))
    for made, sub, view in made_dma:
        gone(made, sub, view)
send(top, 0)
send(shell, 0)
send(surface, 0)
events(0.5)

# A window whose own buffer is turned, and one whose own viewport crops and scales it: laid into the window's picture by the card where it
# composes (op 7 by the same tables, a rectangle of the card's in the debug page) and by the processor's walk where it does not, the same pixels.
BORDER = (0x10, 0x20, 0x30)


def bordered(x, y):
    return BORDER if x < 2 or y < 2 or x >= BW - 2 or y >= BH - 2 else pattern(x, y)


def find(raw, colour):
    want = bytes((colour[2], colour[1], colour[0]))
    xs, ys = [], []
    for y in range(SH):
        line = raw[y * SW * 4:(y + 1) * SW * 4]
        for x in range(SW):
            if line[x * 4:x * 4 + 3] == want:
                xs.append(x), ys.append(y)
    return (min(xs), min(ys)) if xs else None


def rects():
    """The debug page's client damage rectangles copied by the card and by the processor, and the presents the card drew (None with no shell)."""
    if not MOONWATER:
        return None
    out = subprocess.run([MOONWATER, 'debug'], capture_output=True, text=True).stdout
    card = cpu = drawn = 0
    for line in out.splitlines():
        words = line.split()
        if 'client rects' in line:
            card, cpu = int(words[2]), int(words[words.index('card,') + 1])
        if 'the GPU composed' in line:
            drawn = int(words[words.index('composed') + 1])
    return card, cpu, drawn


windows = [('a window whose own buffer is flipped and turned 90', 5, None, None, 1),
           ('a window whose own viewport crops at an offset and scales it', 0, (4 * 256, 2 * 256, 40 * 256, 28 * 256), (100, 70), 1),
           ('a window of an ABGR buffer turned 180', 2, None, None, 0x34324241)]
counted = rects()
for name, transform, crop, size, form in windows:
    own, own_shell, own_top = new(), new(), new()
    send(compositor, 0, struct.pack('<I', own))
    send(wm, 2, struct.pack('<II', own_shell, own))
    send(own_shell, 1, struct.pack('<I', own_top))
    if transform:
        send(own, 7, struct.pack('<i', transform))
    view = None
    if crop or size:
        view = new()
        send(viewporter, 1, struct.pack('<II', view, own))
        if crop:
            send(view, 1, struct.pack('<iiii', *crop))
        if size:
            send(view, 2, struct.pack('<ii', *size))
    send(own, 6)
    for obj, op, body in events(0.5):
        if obj == own_shell and op == 0:
            send(own_shell, 4, body[:4])
    fd, buffer = pool(BW, BH, [bordered(x, y) + (0xff,) for y in range(BH) for x in range(BW)], form)
    send(own, 1, struct.pack('<Iii', buffer, 0, 0))
    send(own, 9, struct.pack('<iiii', 0, 0, BW, BH))
    send(own, 6)
    events(0.6)
    raw = screen()
    corner = None
    turned_w, turned_h = (BH, BW) if transform & 1 else (BW, BH)
    sx, sy, sw, sh = crop if crop else (0, 0, turned_w * 256, turned_h * 256)
    tw, th = size if size else (turned_w, turned_h)
    wrong, total = [], tw * th
    # The window's top left: where the first cell it shows is, found as the leftmost and topmost pixel of that cell's colour near the top
    # left of what is drawn (the border with no crop, the pattern's own colour with one).
    # A cell at the window's left edge, half way down (the round corners leave it shown): the border's colour is the window's edge all round, and
    # a pattern's cell (every one another colour) is a block of the screen whose top is the first row drawn from it.
    first = bordered(*source(transform, sx, sy, sw, sh, tw, th, 0, th // 2))
    corner = find(raw, first)
    if corner and first != BORDER:
        top = min(oy for oy in range(th) if bordered(*source(transform, sx, sy, sw, sh, tw, th, 0, oy)) == first)
        corner = (corner[0], corner[1] - top)
    if corner is None:
        row(False, name + ' is on the screen where each of its pixels belongs', 'not found')
    else:
        for oy in range(th):
            for ox in range(tw):
                # The window's round corners are the compositor's: the squares they are drawn in are left out.
                if (ox < 16 or ox >= tw - 16) and (oy < 16 or oy >= th - 16):
                    continue
                want = bordered(*source(transform, sx, sy, sw, sh, tw, th, ox, oy))
                got = at(raw, corner, ox, oy)
                if got != want:
                    wrong.append((ox, oy, got, want))
        row(not wrong, name + ' is on the screen where each of its pixels belongs', '%d of %d wrong at %s, the first %s' % (len(wrong), total, corner, wrong[:1]))
    if view:
        send(view, 0)
    send(own_top, 0)
    send(own_shell, 0)
    send(own, 0)
    events(0.4)
done = rects()
if counted is None:
    print('NOT RUN the card laid the turned windows: no shell named to read the debug page from')
elif done[2] == counted[2]:
    print('NOT RUN the card laid the turned windows: the card drew no present (no -G, or no card)')
else:
    row(done[0] - counted[0] >= 3 and done[1] == counted[1], 'the card laid the three windows\' pictures, the processor none (moonwater debug counts them)',
        'card %d then %d, processor %d then %d' % (counted[0], done[0], counted[1], done[1]))
sys.exit(failures)
