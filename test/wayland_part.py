# A window drawn a part at a time from buffers in turn, as a toolkit does it, run by test/wayland_e2e.sh against the headless compositor
# on /run/moonwater/wayland-0 (800 by 600, its screen the file named as the one argument). Each buffer holds the whole picture and each
# commit damages only what differs from the one before, so a compositor that reads the buffer where it is and draws again only the
# damage shows what one that copies the damage shows. The screen is read after each commit.
#
#   a square drawn in the second buffer, damaged alone: the square is on the screen and the rest of the window is as it was;
#   the square moved in the third buffer, both places damaged (two rectangles): only the new place has it;
#   the first buffer again, all of it another colour and all of it damaged, and then a part from the second: each is what the screen shows;
#   then that buffer's file is cut under the compositor while it is the picture, and a second client still draws.
#
# One line a row, "ok NAME" or "FAIL NAME detail"; the exit status is the number of failures.
import array, os, select, socket, struct, sys, time

SCAN = sys.argv[1]
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect('/run/moonwater/wayland-0')
ids = [1]
data = b''
failures = 0
W, H = 240, 160
GROUND, INK, OTHER = (0x31, 0x52, 0x73), (0x10, 0xf0, 0x20), (0xe0, 0x30, 0xd0)   # blue, green, red of each


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


def picture(ground, squares):
    """The bytes of a W by H picture of one colour with squares (x, y, side, colour) on it."""
    rows = [bytearray(bytes(ground) + b'\xff') * W for _ in range(H)]
    for x, y, side, colour in squares:
        for r in range(y, y + side):
            rows[r][x * 4:(x + side) * 4] = (bytes(colour) + b'\xff') * side
    return b''.join(bytes(r) for r in rows)


def count(colour):
    raw = open(SCAN, 'rb').read()
    want = bytes(colour)
    return sum(1 for at in range(0, len(raw), 4) if raw[at:at + 3] == want)


registry = new()
send(1, 1, struct.pack('<I', registry))
names = {}
for obj, op, body in events(0.5):
    if obj == registry and op == 0:
        name, length = struct.unpack_from('<II', body)
        names[body[8:8 + length - 1].decode()] = name
bound = []
for interface, version in (('wl_compositor', 4), ('wl_shm', 1), ('xdg_wm_base', 1)):
    bound.append(new())
    send(registry, 0, struct.pack('<I', names[interface]) + string(interface) + struct.pack('<II', version, bound[-1]))
compositor, shm, wm = bound
surface, shell, top = new(), new(), new()
send(compositor, 0, struct.pack('<I', surface))
send(wm, 2, struct.pack('<II', shell, surface))
send(shell, 1, struct.pack('<I', top))
send(surface, 6)
for obj, op, body in events(0.5):
    if obj == shell and op == 0:
        send(shell, 4, body[:4])

files, buffers = [], []
for at in range(3):
    fd = os.memfd_create('picture')
    os.ftruncate(fd, W * H * 4)
    pool, buffer = new(), new()
    send(shm, 0, struct.pack('<Ii', pool, W * H * 4), [fd])
    send(pool, 0, struct.pack('<Iiiiii', buffer, 0, W, H, W * 4, 1))
    files.append(fd)
    buffers.append(buffer)


def commit(at, content, rects):
    os.pwrite(files[at], content, 0)
    send(surface, 1, struct.pack('<Iii', buffers[at], 0, 0))
    for rect in rects:
        send(surface, 9, struct.pack('<iiii', *rect))
    send(surface, 6)
    events(0.4)


commit(0, picture(GROUND, []), [(0, 0, W, H)])
whole = count(GROUND)
row(whole >= W * H - 2 * (W + H) and count(INK) == 0, 'the window is on the screen in its ground colour', '%d pixels of it' % whole)
commit(1, picture(GROUND, [(20, 30, 40, INK)]), [(20, 30, 40, 40)])
row(count(INK) == 1600 and count(GROUND) == whole - 1600, 'a square in the second buffer, damaged alone, is on the screen and the rest is as it was',
    '%d of the square, %d of the ground of %d' % (count(INK), count(GROUND), whole))
commit(2, picture(GROUND, [(150, 90, 30, INK)]), [(20, 30, 40, 40), (150, 90, 30, 30)])
row(count(INK) == 900 and count(GROUND) == whole - 900, 'the square moved in the third buffer, both places damaged, is only where it now is',
    '%d of the square, %d of the ground of %d' % (count(INK), count(GROUND), whole))
commit(0, picture(OTHER, [(8, 8, 20, INK)]), [(0, 0, W, H)])
row(count(OTHER) == whole - 400 and count(INK) == 400 and count(GROUND) == 0, 'the first buffer again, all of it another colour and all of it damaged, is the whole window',
    '%d of the new colour of %d, %d of the square' % (count(OTHER), whole - 400, count(INK)))
commit(1, picture(OTHER, [(8, 8, 20, INK), (100, 100, 10, GROUND)]), [(100, 100, 10, 10)])
row(count(OTHER) == whole - 500 and count(GROUND) == 100, 'and a part after the whole, from another buffer, is that part alone', '%d of the colour, %d of the part' % (count(OTHER), count(GROUND)))

# The picture's file cut while it is the picture; a window over it and gone has it drawn again, and the next client draws.
os.ftruncate(files[1], 0)
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
row(any(obj == done for obj, op, body in events(1.5, other)), 'its file cut while it is the picture, the compositor goes on and the next client is drawn')
sys.exit(failures)
