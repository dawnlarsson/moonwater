# Hostile damage on a shim client, run by test/wayland_e2e.sh against the compositor on /run/moonwater/wayland-0: rectangles that are negative,
# past the buffer, huge, and many and overlapping, on two buffers in a pool, committed several times. The compositor must keep the connection
# (the damage is clipped, not refused) and go on; prints "kept" when the client is still connected after its last commit, else "dropped".
import socket, struct, time, os

path = '/run/moonwater/wayland-0'


def msg(obj, op, *words, extra=b''):
    body = b''.join(struct.pack('<I', w & 0xffffffff) for w in words) + extra
    return struct.pack('<II', obj, ((8 + len(body)) << 16) | op) + body


def smsg(obj, op, *words):
    body = b''.join(struct.pack('<i', w) for w in words)
    return struct.pack('<II', obj, ((8 + len(body)) << 16) | op) + body


def string(text):
    raw = text.encode() + b'\0'
    return struct.pack('<I', len(raw)) + raw + b'\0' * (-len(raw) % 4)


def read_until(s, data, want):
    deadline = time.time() + 2
    while time.time() < deadline:
        data += s.recv(65536)
        at = 0
        while at + 8 <= len(data):
            obj, head = struct.unpack_from('<II', data, at)
            size = head >> 16
            if at + size > len(data):
                break
            if want(obj, head, data, at):
                return data
            at += size
        data = data[at:]
    return data


s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.settimeout(2)
s.connect(path)
s.sendall(msg(1, 1, 2))
globals_ = {}


def take(obj, head, data, at):
    if obj == 2 and head & 0xffff == 0:
        name, = struct.unpack_from('<I', data, at + 8)
        length, = struct.unpack_from('<I', data, at + 12)
        globals_[data[at + 16:at + 16 + length - 1].decode()] = name
    return all(name in globals_ for name in ('wl_compositor', 'wl_shm', 'xdg_wm_base'))


read_until(s, b'', take)
for interface, (new, version) in {'wl_compositor': (3, 4), 'wl_shm': (4, 1), 'xdg_wm_base': (5, 1)}.items():
    s.sendall(msg(2, 0, globals_[interface], extra=string(interface) + struct.pack('<II', version, new)))
s.sendall(msg(3, 0, 6) + msg(5, 2, 7, 6) + msg(7, 1, 8) + msg(6, 6))
serial = []


def configure(obj, head, data, at):
    if obj == 7 and head & 0xffff == 0:
        serial.append(struct.unpack_from('<I', data, at + 8)[0])
    return bool(serial)


read_until(s, b'', configure)
s.sendall(msg(7, 4, serial[0]))

W, H = 128, 64
size = W * H * 4 * 2
fd = os.memfd_create('damage')
os.ftruncate(fd, size)
os.pwrite(fd, b'\x40' * size, 0)
socket.send_fds(s, [msg(4, 0, 9, size)], [fd])
s.sendall(msg(9, 0, 10, 0, W, H, W * 4, 1) + msg(9, 0, 11, W * H * 4, W, H, W * 4, 1))

rects = [
    (-100, -100, 0x7fffffff, 0x7fffffff),        # huge and negative
    (0x7ffffff0, 5, 1000, 1000),                 # past the buffer
    (-5, -5, 3, 3),                              # negative corner
    (120, 60, 20, 20),                           # across the edge
]
rects += [(10 + i, 3 + i, 80, 50) for i in range(40)]  # many, overlapping
for commit in range(6):
    buffer = 10 + (commit % 2)
    out = msg(6, 1, buffer, 0, 0)
    for rect in rects[commit % 4:] + rects[:commit % 4]:
        out += smsg(6, 9, *rect)
    out += msg(6, 6)
    s.sendall(out)
    time.sleep(0.05)

# the connection still answers: a sync of the display (wl_display.sync, its callback comes back)
s.sendall(msg(1, 0, 20))
got = []


def done(obj, head, data, at):
    if obj == 20:
        got.append(True)
    return bool(got)


read_until(s, b'', done)
print('kept' if got else 'dropped')
