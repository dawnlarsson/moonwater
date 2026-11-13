# Every client the socket takes, each leaving the compositor the most descriptors a client may have sent and not used, run by
# test/wayland_e2e.sh against the compositor on /run/moonwater/wayland-0 with the compositor's pid as its argument. The descriptors
# are the compositor's to hold, in its own table, and the kernel numbers each with the lowest free number: so with enough of them
# held a client's descriptor was number 900 or 901, which are the clipboard's two selections' (window.c's WINDOWS_CLIP_FD: a copy is
# put there with dup3, which closes what is there, and a paste opens that number of the compositor's), and at the kernel's 1024 the
# table was full, so the clients after that were dropped and the socket's accept failed with the socket left readable
# (MW-2026-196).
#
# Prints one line: "kept" when every client is still answered, the two numbers are /dev/null's or a selection's and the compositor
# is not turning its loop without a wait; otherwise what was found, which is the row's failure.
import array, os, select, socket, struct, sys, time

PATH = '/run/moonwater/wayland-0'
CLIENTS = 32   # wayland.c's WAYLAND_CLIENTS
EACH = 28      # wayland.c's WAYLAND_FDS
pid = int(sys.argv[1])


def answered(s, wait):
    # wl_display.sync, and its callback's done or anything else the compositor says.
    try:
        s.sendall(struct.pack('<III', 1, 12 << 16 | 0, 0xfffe))
    except OSError:
        return False
    if not select.select([s], [], [], wait)[0]:
        return False
    try:
        got = s.recv(4096)
    except OSError:
        return False
    # A protocol error (wl_display.error, object 1 opcode 0) is an answer of the compositor's too, but the client is then gone.
    return bool(got) and not (len(got) >= 8 and struct.unpack_from('<II', got) [0] == 1 and struct.unpack_from('<II', got)[1] & 0xffff == 0)


def ticks():
    fields = open('/proc/%d/stat' % pid).read().rsplit(') ', 1)[1].split()
    return int(fields[11]) + int(fields[12])


def link(number):
    try:
        return os.readlink('/proc/%d/fd/%d' % (pid, number))
    except OSError:
        return 'none'


zero = os.open('/dev/zero', os.O_RDONLY)
clients = []
for at in range(CLIENTS):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        s.connect(PATH)
        # wl_display.sync with the descriptors beside it: no request of it takes one, so they wait in the compositor.
        s.sendmsg([struct.pack('<III', 1, 12 << 16 | 0, 2)], [(socket.SOL_SOCKET, socket.SCM_RIGHTS, array.array('i', [zero] * EACH))])
    except OSError:
        pass
    clients.append(s)
time.sleep(0.5)
held = len(os.listdir('/proc/%d/fd' % pid))
alive = sum(answered(s, 2) for s in clients)
numbers = [link(900), link(901)]
before = ticks()
time.sleep(1.0)
spent = ticks() - before
found = []
if alive != CLIENTS:
    found.append('%d of %d clients are still answered' % (alive, CLIENTS))
for number, what in zip((900, 901), numbers):
    if what != '/dev/null' and not what.startswith('/memfd:'):
        found.append('descriptor %d of the compositor is %s' % (number, what))
if spent > 20:
    found.append('the compositor ran %d ticks of a second with nothing to do' % spent)
print('kept' if not found else '; '.join(found) + ' (%d descriptors held)' % held)
