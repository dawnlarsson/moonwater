# A pool on a file whose pages the client serves itself and never does, run by test/wayland_e2e.sh against the compositor on
# /run/moonwater/wayland-0: the file is on a FUSE mount this script serves (its READ is never answered), given to wl_shm.create_pool
# and shown as a window. A compositor that maps it reads the picture by asking this client, and waits in the page fault for as long as
# the client likes: no other client is answered, and it cannot be ended, not by SIGKILL, until the mount goes (MW-2026-195).
#
# Prints one word: "refused" when the pool was refused (wl_shm's invalid_fd) and a second client is answered, "frozen" when a second
# client got no answer within three seconds, "answers" when the pool was taken and the compositor answers all the same, and
# "NOT RUN reason" where no FUSE mount can be made (no /dev/fuse, or the mount is refused): that is not a pass.
import array, ctypes, os, select, socket, struct, sys, threading, time

PATH = '/run/moonwater/wayland-0'
MOUNT = '/run/moonwater-held'
SIZE = 256 * 256 * 4


def not_run(why):
    print('NOT RUN ' + why)
    sys.exit(0)


try:
    dev = os.open('/dev/fuse', os.O_RDWR)
except OSError as error:
    not_run('no /dev/fuse to open (%s)' % error.strerror)
os.makedirs(MOUNT, exist_ok=True)
libc = ctypes.CDLL(None, use_errno=True)
if libc.mount(b'held', MOUNT.encode(), b'fuse', 6, ('fd=%d,rootmode=40000,user_id=0,group_id=0' % dev).encode()) != 0:  # MS_NOSUID | MS_NODEV
    not_run('a FUSE mount is refused here (errno %d)' % ctypes.get_errno())

held = []
stop = False


def attr(ino, mode, size):
    return struct.pack('<QQQQQQIIIIIIIIII', ino, size, (size + 511) // 512, 0, 0, 0, 0, 0, 0, mode, 1, 0, 0, 0, 4096, 0)


def reply(unique, body=b'', error=0):
    try:
        os.write(dev, struct.pack('<IiQ', 16 + len(body), error, unique) + body)
    except OSError:
        pass


def serve():
    # The kernel's requests: a root directory with one file, pool, of SIZE bytes; everything is answered but the file's READ.
    while not stop:
        if not select.select([dev], [], [], 0.05)[0]:
            continue
        try:
            raw = os.read(dev, 1 << 20)
        except OSError:
            return
        length, op, unique, node = struct.unpack_from('<IIQQ', raw)
        body = raw[40:length]
        if op == 26:      # INIT
            major, minor, readahead, flags = struct.unpack_from('<IIII', body)
            reply(unique, struct.pack('<IIIIHHIIHHI', 7, min(minor, 31), readahead, 0, 12, 9, 1 << 17, 1, 32, 0, 0) + b'\0' * 28)
        elif op == 1:     # LOOKUP
            if body.rstrip(b'\0') == b'pool':
                reply(unique, struct.pack('<QQQQII', 2, 1, 3600, 3600, 0, 0) + attr(2, 0o100644, SIZE))
            else:
                reply(unique, error=-2)
        elif op == 3:     # GETATTR
            reply(unique, struct.pack('<QII', 3600, 0, 0) + (attr(1, 0o40755, 0) if node == 1 else attr(2, 0o100644, SIZE)))
        elif op == 14:    # OPEN
            reply(unique, struct.pack('<QII', 1, 0, 0))
        elif op == 15:    # READ: held
            held.append(unique)
        elif op in (2, 36):   # FORGET, INTERRUPT: no reply is due
            pass
        elif op in (18, 25):  # RELEASE, FLUSH
            reply(unique)
        else:
            reply(unique, error=-38)


server = threading.Thread(target=serve, daemon=True)
server.start()


class Client:
    def __init__(self):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.connect(PATH)
        self.next = 1
        self.data = b''
        self.seen = []

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
            while len(self.data) >= 8:
                obj, head = struct.unpack_from('<II', self.data)
                if len(self.data) < head >> 16:
                    break
                event = (obj, head & 0xffff, self.data[8:head >> 16])
                self.data = self.data[head >> 16:]
                self.seen.append(event)
                if want(*event):
                    return event
        return None

    def sync(self, wait):
        done = self.new()
        self.send(1, 0, struct.pack('<I', done))
        return self.until(lambda obj, op, body: obj == done, wait) is not None


def string(text):
    raw = text.encode() + b'\0'
    return struct.pack('<I', len(raw)) + raw + b'\0' * (-len(raw) % 4)


fd = os.open(MOUNT + '/pool', os.O_RDONLY)
first = Client()
registry = first.new()
first.send(1, 1, struct.pack('<I', registry))
first.sync(3)
names = {}
for obj, op, body in first.seen:
    if obj == registry and op == 0:
        name, length = struct.unpack_from('<II', body)
        names[body[8:8 + length - 1].decode()] = name
bound = {}
for interface in ('wl_compositor', 'wl_shm', 'xdg_wm_base'):
    bound[interface] = first.new()
    first.send(registry, 0, struct.pack('<I', names[interface]) + string(interface) + struct.pack('<II', 1, bound[interface]))
surface, shell, top, pool, buffer = (first.new() for _ in range(5))
first.send(bound['wl_compositor'], 0, struct.pack('<I', surface))
first.send(bound['xdg_wm_base'], 2, struct.pack('<II', shell, surface))
first.send(shell, 1, struct.pack('<I', top))
first.send(surface, 6)
configure = first.until(lambda obj, op, body: obj == shell and op == 0, 3)
try:  # a client that is refused is dropped, and what it sends after that finds the socket closed
    first.send(shell, 4, configure[2][:4] if configure else struct.pack('<I', 0))
    first.send(bound['wl_shm'], 0, struct.pack('<Ii', pool, SIZE), [fd])
    os.close(fd)  # while the server still answers its flush
    first.send(pool, 0, struct.pack('<Iiiiii', buffer, 0, 256, 256, 1024, 1))
    first.send(surface, 1, struct.pack('<Iii', buffer, 0, 0))
    first.send(surface, 6)
except OSError:
    pass
time.sleep(0.5)

# A second client asks the compositor anything at all.
try:
    answered = Client().sync(3)
except OSError:
    answered = False
first.until(lambda obj, op, body: False, 0.2)
refused = any(obj == 1 and op == 0 and struct.unpack_from('<I', body, 4)[0] == 2 for obj, op, body in first.seen)  # wl_shm.error.invalid_fd
print('frozen' if not answered else 'refused' if refused else 'answers')
sys.stdout.flush()
# The compositor is let go: the server stops and the device is closed, which ends the mount, and the reads it holds fail.
stop = True
server.join()
os.close(dev)
os._exit(0)
