# A window asked to close three times, and kept each time (a program that asks "unsaved changes?" and is told to cancel), run by
# test/wayland_e2e.sh against a headless compositor that reads one more input device, the FIFO named as the one argument: Alt-F4 is
# written to it as a person's keys. The window's client is sent xdg_toplevel.close for each ask.
#
# Prints "asked 1 1 1" for a close sent at each of the three, and whatever else was counted otherwise ("asked 1 0 0": asked once and
# never again, which is what a row that kept its ask for good did).
import array, os, select, socket, struct, sys, time

EV_SYN, EV_KEY = 0, 1
KEY_LEFTALT, KEY_F4 = 56, 62
clock = [1_000_000_000]
fifo = os.open(sys.argv[1], os.O_WRONLY)
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect('/run/moonwater/wayland-0')
ids = [1]
data = b''


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


def key(code, value):
    clock[0] += 20_000
    stamp = struct.pack('qq', clock[0] // 1_000_000, clock[0] % 1_000_000)
    os.write(fifo, stamp + struct.pack('HHi', EV_KEY, code, value) + stamp + struct.pack('HHi', EV_SYN, 0, 0))
    time.sleep(0.05)


registry = new()
send(1, 1, struct.pack('<I', registry))
names = {}
for obj, op, body in events(0.5):
    if obj == registry and op == 0:
        name, length = struct.unpack_from('<II', body)
        names[body[8:8 + length - 1].decode()] = name
bound = []
for interface in ('wl_compositor', 'wl_shm', 'xdg_wm_base'):
    bound.append(new())
    send(registry, 0, struct.pack('<I', names[interface]) + string(interface) + struct.pack('<II', 1, bound[-1]))
compositor, shm, wm = bound
surface, shell, top, pool, buffer = (new() for _ in range(5))
send(compositor, 0, struct.pack('<I', surface))
send(wm, 2, struct.pack('<II', shell, surface))
send(shell, 1, struct.pack('<I', top))
send(surface, 6)
for obj, op, body in events(0.5):
    if obj == shell and op == 0:
        send(shell, 4, body[:4])
fd = os.memfd_create('pool')
os.ftruncate(fd, 300 * 200 * 4)
send(shm, 0, struct.pack('<Ii', pool, 300 * 200 * 4), [fd])
send(pool, 0, struct.pack('<Iiiiii', buffer, 0, 300, 200, 1200, 1))
send(surface, 1, struct.pack('<Iii', buffer, 0, 0))
send(surface, 6)
events(0.5)
asked = []
for ask in range(3):
    key(KEY_LEFTALT, 1)
    key(KEY_F4, 1)
    key(KEY_F4, 0)
    key(KEY_LEFTALT, 0)
    asked.append(sum(1 for obj, op, body in events(0.6) if obj == top and op == 1))  # xdg_toplevel.close
print('asked ' + ' '.join(str(count) for count in asked))
