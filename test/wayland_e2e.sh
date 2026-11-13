#!/bin/sh
# The Wayland shim end to end: the compositor (built with CONFIG_MOONWATER_WAYLAND) headless, real clients on libwayland.
#
#     unshare -rm sh test/wayland_e2e.sh COMPOSITOR CLIENT [WAYLAND_INFO] [WORKDIR] [INPUT]
#
# COMPOSITOR is the shell binary reached by the name `compositor`, CLIENT is test/wayland_shm.c built, WAYLAND_INFO is
# the wayland-info program when there is one, and INPUT is 1 when CLIENT was built with libxkbcommon (-DHAVE_XKB), which
# runs the seat's rows (test/wayland_input.py). It runs as root of a user namespace with a private /run, so nothing of the
# machine's is touched. One line a row: "ok NAME" or "FAIL NAME ...", and the exit status is the number of failures.
compositor=$1
client=$2
info=${3:-}
work=${4:-$(mktemp -d)}
input=${5:-}
# Options for the compositor. A machine with a render node has the headless one compose on the GPU (-G): the desktop is drawn by the card there, as it is in a
# running system, and by the processor where there is no card (WAYLAND_E2E_EXTRA= says none, whatever the machine has).
if [ -z "${WAYLAND_E2E_EXTRA+x}" ] && [ -c /dev/dri/renderD128 ]; then extra=-G; else extra=${WAYLAND_E2E_EXTRA:-}; fi
failures=0
mkdir -p "$work/bin"
ln -sf "$compositor" "$work/bin/compositor"
mount -t tmpfs tmpfs /run || exit 1
# The compositor watches /root for the settings; a private one is the screen's scale to change (the rows of wayland_scale.py).
mount -t tmpfs tmpfs /root || exit 1
mkdir -p /run/moonwater
export WAYLAND_DISPLAY=/run/moonwater/wayland-0 XDG_RUNTIME_DIR=/run

row()
{
        if [ "$1" = ok ]; then echo "ok $2"; else echo "FAIL $2 ${3:-}"; failures=$((failures + 1)); fi
}

start()
{
        rm -f /run/moonwater/wayland-0
        # shellcheck disable=SC2086
        "$work/bin/compositor" -H 800x600 "$work/scan.img" $extra > "$work/compositor.log" 2>&1 &
        cpid=$!
        i=0
        while [ ! -S /run/moonwater/wayland-0 ] && [ $i -lt 100 ]; do sleep 0.1; i=$((i + 1)); done
        [ -S /run/moonwater/wayland-0 ]
}

stop()
{
        kill "$cpid" 2> /dev/null
        wait "$cpid" 2> /dev/null
}

alive()
{
        kill -0 "$cpid" 2> /dev/null
}

# The bounding box of what is not the desktop's ink in the headless screen, as "x1 y1 x2 y2 count".
box()
{
        python3 - "$work/scan.img" 800 600 << 'PY'
import sys
raw = open(sys.argv[1], 'rb').read()
w, h = int(sys.argv[2]), int(sys.argv[3])
x1, y1, x2, y2, n = w, h, -1, -1, 0
for y in range(h - 30):
    row = raw[y * w * 4:(y + 1) * w * 4]
    for x in range(w):
        if row[x * 4:x * 4 + 3] != b'\x1e\x1e\x1e':
            n += 1
            x1, y1, x2, y2 = min(x1, x), min(y1, y), max(x2, x), max(y2, y)
print(x1, y1, x2, y2, n)
PY
}

if ! start; then
        row FAIL "the compositor listens on /run/moonwater/wayland-0" "$(tail -3 "$work/compositor.log")"
        exit 1
fi
row ok "the compositor listens on /run/moonwater/wayland-0"

# A client that draws thirty frames, each on the frame callback of the one before.
out=$("$client" -n 30 -w 320 -h 240 2>&1)
case $out in
*"frames 30"*"closed 0"*) row ok "a libwayland client draws thirty frames on frame callbacks" ;;
*) row FAIL "a libwayland client draws thirty frames on frame callbacks" "$out" ;;
esac
case $out in
*"configures 1"*"acks 1"*) row ok "its toplevel is configured once and acknowledged once" ;;
*) row FAIL "its toplevel is configured once and acknowledged once" "$out" ;;
esac

# With -G the card composed the desktop the client drew into: the compositor says so once on its log, or says why it did not (a card its kernels are not
# for is no failure; a card that drew its self-test wrong is).
case $extra in
*-G*)
        if grep -q "the GPU composes the desktop" "$work/compositor.log"; then
                row ok "the GPU composed the desktop the client drew into"
        elif grep -q "the GPU is not trusted" "$work/compositor.log"; then
                row FAIL "the GPU composed the desktop the client drew into" "$(grep "compositor: the GPU" "$work/compositor.log" | head -1)"
        elif grep -q "no card the GPU's kernels are for" "$work/compositor.log"; then
                row ok "the GPU composed the desktop the client drew into (skipped: the card is not one the kernels are for)"
        else
                row FAIL "the GPU composed the desktop the client drew into" "the compositor said nothing of it"
        fi ;;
esac

# The picture is on the screen: a window of the size it drew.
"$client" -n 1000000 -s -w 320 -h 240 -q > /dev/null 2>&1 &
shown=$!
sleep 1
set -- $(box)
kill "$shown" 2> /dev/null
wait "$shown" 2> /dev/null
if [ "$3" -ge 0 ] && [ $(($3 - $1 + 1)) -ge 318 ] && [ $(($3 - $1 + 1)) -le 322 ] && [ $(($4 - $2 + 1)) -ge 238 ] && [ $(($4 - $2 + 1)) -le 242 ]; then
        row ok "the window is on the screen at the size it drew ($1,$2 to $3,$4)"
else
        row FAIL "the window is on the screen at the size it drew" "box $*"
fi

# A subsurface is part of the window's picture: 64 by 48 in its own colour, 10 and 20 into the window.
"$client" -n 1000000 -s -u -w 320 -h 240 -q > /dev/null 2>&1 &
shown=$!
sleep 1
set -- $(box)
wx=$1
wy=$2
sub=$(python3 - "$work/scan.img" 800 600 << 'PY'
import sys
raw = open(sys.argv[1], 'rb').read()
w, h = int(sys.argv[2]), int(sys.argv[3])
x1, y1, x2, y2, n = w, h, -1, -1, 0
for y in range(h):
    row = raw[y * w * 4:(y + 1) * w * 4]
    for x in range(w):
        if row[x * 4:x * 4 + 3] == b'\x20\xff\x10':
            n += 1
            x1, y1, x2, y2 = min(x1, x), min(y1, y), max(x2, x), max(y2, y)
print(x1, y1, x2, y2, n)
PY
)
kill "$shown" 2> /dev/null
wait "$shown" 2> /dev/null
set -- $sub
# The window's line is one pixel round the picture, so its picture begins at the box's corner or one in.
dx=$(($1 - wx))
dy=$(($2 - wy))
if [ "$5" = 3072 ] && [ $(($3 - $1 + 1)) = 64 ] && [ $(($4 - $2 + 1)) = 48 ] && [ $dx -ge 10 ] && [ $dx -le 11 ] && [ $dy -ge 20 ] && [ $dy -le 21 ]; then
        row ok "a subsurface is on the screen at its place in the window ($sub in the window at $wx,$wy)"
else
        row FAIL "a subsurface is on the screen at its place in the window" "window $wx,$wy, subsurface $sub"
fi

# A client that draws into dma-bufs from the GPU's render node (where the machine has one): its buffer is read through the descriptor,
# and the window shows its pixels. The one pixel of 255, 234, 0 is at the right edge of the picture, 220 rows down.
if "$client" -b -n 1 -s -w 64 -h 64 -q > /dev/null 2>&1; then
        "$client" -b -n 1000000 -s -w 320 -h 240 -q > /dev/null 2>&1 &
        shown=$!
        sleep 1
        set -- $(box)
        wx=$1
        wy=$2
        wx2=$3
        wy2=$4
        yellow=$(python3 - "$work/scan.img" 800 600 << 'PY'
import sys
raw = open(sys.argv[1], 'rb').read()
w, h = int(sys.argv[2]), int(sys.argv[3])
found = []
for y in range(h):
    row = raw[y * w * 4:(y + 1) * w * 4]
    for x in range(w):
        if row[x * 4:x * 4 + 3] == b'\x00\xea\xff':
            found.append((x, y))
print(len(found), found[0][0] if found else -1, found[0][1] if found else -1)
PY
)
        kill "$shown" 2> /dev/null
        wait "$shown" 2> /dev/null
        set -- $yellow
        if [ "$1" = 1 ] && [ "$2" -ge $((wx2 - 1)) ] && [ "$2" -le "$wx2" ] && [ "$3" -ge $((wy + 220)) ] && [ "$3" -le $((wy + 222)) ]; then
                row ok "a window of dma-bufs is on the screen with the pixels the client drew ($yellow in $wx,$wy to $wx2,$wy2)"
        else
                row FAIL "a window of dma-bufs is on the screen with the pixels the client drew" "pixel $yellow, window $wx,$wy to $wx2,$wy2"
        fi
        out=$("$client" -b -n 30 -w 200 -h 120 2>&1)
        case $out in
        *"frames 30"*) row ok "a client of dma-bufs draws thirty frames, each when the last is released" ;;
        *) row FAIL "a client of dma-bufs draws thirty frames, each when the last is released" "$out" ;;
        esac
fi

# Damage only: the same picture comes of drawing a rectangle at a time.
out=$("$client" -n 20 -w 200 -h 120 -d 2>&1)
case $out in
*"frames 20"*) row ok "a client that damages only what changed draws twenty frames" ;;
*) row FAIL "a client that damages only what changed draws twenty frames" "$out" ;;
esac

# Maximize and fullscreen asked before the window is shown.
out=$("$client" -n 5 -w 200 -h 120 -m 2>&1)
case $out in
*"frames 5"*) row ok "a window that asks to be maximized is shown and keeps drawing" ;;
*) row FAIL "a window that asks to be maximized is shown and keeps drawing" "$out" ;;
esac

# wayland-info lists the globals and says nothing wrong of them.
if [ -n "$info" ]; then
        out=$("$info" 2>&1)
        case $out in
        *wl_compositor*wl_shm*xdg_wm_base*) row ok "wayland-info lists the compositor, shm and xdg_wm_base" ;;
        *) row FAIL "wayland-info lists the compositor, shm and xdg_wm_base" "$out" ;;
        esac
        case $out in
        *"protocol error"*) row FAIL "wayland-info meets no protocol error" "$out" ;;
        *) row ok "wayland-info meets no protocol error" ;;
        esac
fi

# Two clients at once, and one killed in the middle of its drawing: the other is not told and the compositor goes on.
"$client" -n 200 -w 160 -h 120 -q > "$work/a.out" 2>&1 &
first=$!
"$client" -n 200 -w 160 -h 120 -q > "$work/b.out" 2>&1 &
second=$!
sleep 0.5
kill -9 "$first" 2> /dev/null
wait "$first" 2> /dev/null
wait "$second"
second_status=$?
if alive && [ "$second_status" = 0 ]; then
        row ok "a client killed mid-draw does not stop the compositor or the other client"
else
        row FAIL "a client killed mid-draw does not stop the compositor or the other client" "status $second_status, alive $(alive && echo yes || echo no)"
fi

# What a hostile client sends: garbage, a message of an impossible size, a header and then silence, a descriptor storm.
python3 - << 'PY'
import os, socket, struct, time
path = '/run/moonwater/wayland-0'
def connect():
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(0.3)
    s.connect(path)
    return s
for blob in (os.urandom(4096), struct.pack('<II', 1, (7 << 16) | 0), struct.pack('<II', 1, (65535 << 16) | 0), struct.pack('<II', 1, (12 << 16) | 1), b'\x00' * 3, struct.pack('<III', 1, (12 << 16) | 1, 0xffffffff)):
    s = connect()
    try:
        s.sendall(blob)
        time.sleep(0.05)
        s.recv(65536)
    except OSError:
        pass
    s.close()
# many connections held open at once, none speaking
held = []
for _ in range(40):
    try:
        held.append(connect())
    except OSError:
        break
time.sleep(0.2)
for s in held:
    s.close()
# descriptors without a message
s = connect()
for _ in range(40):
    try:
        socket.send_fds(s, [b'x' * 8], [os.open('/dev/null', os.O_RDONLY)] * 4)
    except OSError:
        break
time.sleep(0.2)
s.close()
PY
# More descriptors with one message than the shim takes (28) and fewer than the compositor's control buffer holds (36): the kernel
# puts all of them in the compositor, and every one is closed again when the client is gone (8 a message stayed open, fb6f5ae0).
leaked=$(python3 - "$cpid" << 'PY'
import os, socket, struct, sys, time
fds = lambda: len(os.listdir('/proc/%s/fd' % sys.argv[1]))
time.sleep(0.2)
before = fds()
for _ in range(4):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(1)
    s.connect('/run/moonwater/wayland-0')
    null = os.open('/dev/null', os.O_RDONLY)
    socket.send_fds(s, [struct.pack('<III', 1, (12 << 16) | 0, 2)], [null] * 36)
    os.close(null)
    try:
        while s.recv(65536):
            pass
    except OSError:
        pass
    s.close()
time.sleep(0.5)
print(fds() - before)
PY
)
[ "$leaked" = 0 ] && row ok "a client that sends more descriptors than the shim takes leaves none open in the compositor" ||
        row FAIL "a client that sends more descriptors than the shim takes leaves none open in the compositor" "$leaked more open after four such clients"
if alive; then
        out=$("$client" -n 5 -q 2>&1)
        status=$?
        [ "$status" = 0 ] && row ok "after garbage, oversized messages, silent connections and descriptor storms a client still works" ||
                row FAIL "after hostile clients a client still works" "$out"
else
        row FAIL "the compositor survives hostile clients" "$(tail -3 "$work/compositor.log")"
fi

# A client that cuts its pool's file under the compositor while it is shown: the read that follows raises SIGBUS in the
# compositor, which maps zeros over the pool and drops that client; the compositor and the next client are untouched.
cut=$(python3 - << 'PY'
import os, socket, struct, time
path = '/run/moonwater/wayland-0'

def msg(obj, op, *words, extra=b''):
    body = b''.join(struct.pack('<I', w) for w in words) + extra
    return struct.pack('<II', obj, ((8 + len(body)) << 16) | op) + body

def string(text):
    raw = text.encode() + b'\0'
    return struct.pack('<I', len(raw)) + raw + b'\0' * (-len(raw) % 4)

s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.settimeout(2)
s.connect(path)
s.sendall(msg(1, 1, 2))
data = b''
globals_ = {}
deadline = time.time() + 2
while time.time() < deadline and len(globals_) < 3:
    data += s.recv(65536)
    at = 0
    while at + 8 <= len(data):
        obj, head = struct.unpack_from('<II', data, at)
        size = head >> 16
        if at + size > len(data):
            break
        if obj == 2 and head & 0xffff == 0:
            name, = struct.unpack_from('<I', data, at + 8)
            length, = struct.unpack_from('<I', data, at + 12)
            globals_[data[at + 16:at + 16 + length - 1].decode()] = name
        at += size
    data = data[at:]
ids = {'wl_compositor': (3, 4), 'wl_shm': (4, 1), 'xdg_wm_base': (5, 1)}
for interface, (new, version) in ids.items():
    s.sendall(msg(2, 0, globals_[interface], extra=string(interface) + struct.pack('<II', version, new)))
s.sendall(msg(3, 0, 6))              # create_surface
s.sendall(msg(5, 2, 7, 6))           # get_xdg_surface
s.sendall(msg(7, 1, 8))              # get_toplevel
s.sendall(msg(6, 6))                 # commit: the initial one
serial = None
while serial is None:
    data += s.recv(65536)
    at = 0
    while at + 8 <= len(data):
        obj, head = struct.unpack_from('<II', data, at)
        size = head >> 16
        if at + size > len(data):
            break
        if obj == 7 and head & 0xffff == 0:
            serial, = struct.unpack_from('<I', data, at + 8)
        at += size
    data = data[at:]
s.sendall(msg(7, 4, serial))         # ack_configure
fd = os.memfd_create('cut')
os.ftruncate(fd, 64 * 64 * 4)
os.pwrite(fd, b'\x7f' * (64 * 64 * 4), 0)
socket.send_fds(s, [msg(4, 0, 9, 64 * 64 * 4)], [fd])    # create_pool
s.sendall(msg(9, 0, 10, 0, 64, 64, 256, 1))              # create_buffer, xrgb8888
s.sendall(msg(6, 1, 10, 0, 0) + msg(6, 9, 0, 0, 64, 64) + msg(6, 6))
time.sleep(0.3)
os.ftruncate(fd, 0)                                       # the file is cut while the window is shown
s.sendall(msg(6, 1, 10, 0, 0) + msg(6, 9, 0, 0, 64, 64) + msg(6, 6))
dropped = False
try:
    for _ in range(20):
        if s.recv(65536) == b'':
            dropped = True
            break
except (ConnectionResetError, BrokenPipeError):
    dropped = True
except socket.timeout:
    pass
print('dropped' if dropped else 'kept')
PY
)
if alive && [ "$cut" = dropped ]; then
        out=$("$client" -n 5 -q 2>&1)
        [ "$?" = 0 ] && row ok "a pool cut under the compositor's read is a client dropped, not a compositor ended" ||
                row FAIL "a pool cut under the compositor's read is a client dropped" "$out"
else
        row FAIL "a pool cut under the compositor's read does not end the compositor" "$(tail -3 "$work/compositor.log")"
fi

# A client that grows a pool whose picture the compositor shows in place (a whole-window buffer is read where the client
# keeps it): the pool is the middle one of three, so it moves when it grows, and the window is then drawn again with no
# commit of its own. The picture is read where the pool now is; read where it was, the compositor ended (SIGSEGV).
grown=$(python3 - << 'PY' 2>&1
import array, os, select, socket, struct, time
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect('/run/moonwater/wayland-0')
ids = [1]
held = b''

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
    global held
    out = []
    end = time.time() + wait
    while time.time() < end and select.select([s], [], [], max(0, end - time.time()))[0]:
        got = s.recv(65536)
        if not got:
            break
        held += got
        while len(held) >= 8:
            obj, head = struct.unpack_from('<II', held)
            if len(held) < head >> 16:
                break
            out.append((obj, head & 0xffff, held[8:head >> 16]))
            held = held[head >> 16:]
    return out

def string(text):
    raw = text.encode() + b'\0'
    return struct.pack('<I', len(raw)) + raw + b'\0' * (-len(raw) % 4)

def pool(size, ink):
    fd = os.memfd_create('pool')
    os.ftruncate(fd, size)
    os.pwrite(fd, bytes([ink]) * size, 0)
    at = new()
    send(shm, 0, struct.pack('<Ii', at, size), [fd])
    return fd, at

def window():
    surface, shell, top = new(), new(), new()
    send(compositor, 0, struct.pack('<I', surface))
    send(wm, 2, struct.pack('<II', shell, surface))
    send(shell, 1, struct.pack('<I', top))
    send(surface, 6)
    for obj, op, body in events(0.5):
        if obj == shell and op == 0:
            send(shell, 4, body[:4])
    return surface, shell, top

def show(surface, of, width, height):
    buffer = new()
    send(of, 0, struct.pack('<Iiiiii', buffer, 0, width, height, width * 4, 1))
    send(surface, 1, struct.pack('<Iii', buffer, 0, 0))
    send(surface, 6)

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
side, size = 256, 256 * 256 * 4
# Three pools, mapped one under another, and the window's picture in the middle one: it has no room to grow where it is.
first = pool(size, 0x80)
middle = pool(size, 0x40)
last = pool(size, 0x80)
surface, shell, top = window()
show(surface, middle[1], side, side)
events(0.5)
os.ftruncate(middle[0], size * 16)
send(middle[1], 2, struct.pack('<i', size * 16))
events(0.3)
# The window is drawn again without a commit of its own: a second window over it, and gone.
over = window()
show(over[0], first[1], side, side)
events(0.5)
send(over[2], 0)
send(over[1], 0)
send(over[0], 0)
refused = any(obj == 1 and op == 0 for obj, op, body in events(1.0))
print('refused' if refused else 'kept')
PY
)
if alive && [ "$grown" = kept ]; then
        row ok "a pool grown under a picture shown in place moves the picture with it, and the compositor goes on"
else
        row FAIL "a pool grown under a picture shown in place does not end the compositor" "$grown $(tail -3 "$work/compositor.log")"
fi

# The seat, end to end: a pointer, a wheel and keys written to a FIFO the compositor reads as one more input device, and a
# client with libxkbcommon saying what it was told, on three layouts.
if [ -n "$input" ]; then
        for layout in us de fr; do
                stop
                rm -f "$work/in.fifo"
                mkfifo "$work/in.fifo"
                extra="-i $work/in.fifo -l $layout"
                if ! start; then
                        row FAIL "the compositor starts with a keyboard layout ($layout)" "$(tail -3 "$work/compositor.log")"
                        continue
                fi
                MOONWATER_ASKED=1 "$client" -n 1000000 -s -v -w 320 -h 240 > "$work/client.log" 2>&1 &
                shown=$!
                python3 "$(dirname "$0")/wayland_input.py" "$work/in.fifo" "$work/client.log" "$layout" > "$work/input.out" 2>&1
                result=$?
                sed "s/\$/ ($layout)/" "$work/input.out" | cut -c1-400
                failures=$((failures + result))
                kill "$shown" 2> /dev/null
                wait "$shown" 2> /dev/null
                if ! alive; then
                        row FAIL "the compositor survives a person's pointer and keys ($layout)" "$(tail -3 "$work/compositor.log")"
                fi
        done
        # The clipboard, end to end: one client copies at a key, another pastes at one.
        stop
        rm -f "$work/in.fifo"
        mkfifo "$work/in.fifo"
        extra="-i $work/in.fifo -l us"
        if start; then
                python3 "$(dirname "$0")/wayland_clip.py" "$work/in.fifo" "$client" "$work" > "$work/clip.out" 2>&1
                result=$?
                cut -c1-400 "$work/clip.out"
                failures=$((failures + result))
                alive || row FAIL "the compositor survives clients that copy and paste" "$(tail -3 "$work/compositor.log")"
        else
                row FAIL "the compositor starts for the clipboard rows" "$(tail -3 "$work/compositor.log")"
        fi
        # A pointer lock, end to end: a client that locks the pointer is told the hand's counts as they come.
        if "$client" -L -n 1 -s -w 64 -h 64 -q > /dev/null 2>&1; then
                stop
                rm -f "$work/in.fifo"
                mkfifo "$work/in.fifo"
                extra="-i $work/in.fifo -l us"
                if start; then
                        MOONWATER_ASKED=1 "$client" -L -v -n 1000000 -s -w 320 -h 240 > "$work/game.log" 2>&1 &
                        shown=$!
                        python3 "$(dirname "$0")/wayland_game.py" "$work/in.fifo" "$work/game.log" > "$work/game.out" 2>&1
                        result=$?
                        cut -c1-400 "$work/game.out"
                        failures=$((failures + result))
                        kill "$shown" 2> /dev/null
                        wait "$shown" 2> /dev/null
                        alive || row FAIL "the compositor survives a client that locks the pointer" "$(tail -3 "$work/compositor.log")"
                else
                        row FAIL "the compositor starts for the pointer lock rows" "$(tail -3 "$work/compositor.log")"
                fi
        fi
        extra=
        stop
        start
fi

# A window asked to close and kept, three times (Alt-F4 written to an input FIFO, test/wayland_close.py): its client is sent the close each
# time. The row's ask was never taken off it, so a program that answered the first with a cancel could not be closed by the window's button again.
stop
rm -f "$work/close.fifo"
mkfifo "$work/close.fifo"
close_extra=$extra
extra="$extra -i $work/close.fifo -l us"
if start; then
        out=$(MOONWATER_ASKED=1 python3 -I "$(dirname "$0")/wayland_close.py" "$work/close.fifo" 2>&1)
        if alive && [ "$out" = "asked 1 1 1" ]; then
                row ok "a window asked to close three times and kept is sent xdg_toplevel.close three times"
        else
                row FAIL "a window asked to close three times and kept is sent xdg_toplevel.close three times" "$out $(tail -3 "$work/compositor.log")"
        fi
else
        row FAIL "the compositor starts for the close rows" "$(tail -3 "$work/compositor.log")"
fi
extra=$close_extra
stop
start

# The screen's scale changed while clients run, end to end (they follow it as toolkits do: wl_output, xdg-output, the fractional
# scale and the viewporter, and as a client that knows whole scales only).
"$client" -S -n 1 -s -q > /dev/null 2>&1
if [ "$?" != 2 ]; then
        stop
        extra=
        if start; then
                python3 "$(dirname "$0")/wayland_scale.py" "$client" "$work/scan.img" "$work" > "$work/scale.out" 2>&1
                result=$?
                cut -c1-600 "$work/scale.out"
                failures=$((failures + result))
                alive || row FAIL "the compositor survives the screen's scale changing under clients" "$(tail -3 "$work/compositor.log")"
        else
                row FAIL "the compositor starts for the scale rows" "$(tail -3 "$work/compositor.log")"
        fi
fi

# A window's picture the card reads where the client's buffer is, and the processor reads the same: one window composed on the card and then by the
# processor (the same client, still), the screen the same byte for byte. Without a card there is one composition and nothing to compare.
case $extra in
*-G*)
        "$client" -n 1000000 -s -w 320 -h 240 -q > /dev/null 2>&1 &
        held=$!
        sleep 1
        cp "$work/scan.img" "$work/card.img"
        kill "$held" 2> /dev/null
        wait "$held" 2> /dev/null
        stop
        extra=
        if start; then
                "$client" -n 1000000 -s -w 320 -h 240 -q > /dev/null 2>&1 &
                held=$!
                sleep 1
                cp "$work/scan.img" "$work/processor.img"
                kill "$held" 2> /dev/null
                wait "$held" 2> /dev/null
                if cmp -s "$work/card.img" "$work/processor.img"; then
                        row ok "the card's picture of a client's window is the processor's, byte for byte"
                else
                        row FAIL "the card's picture of a client's window is the processor's, byte for byte" "the screens differ"
                fi
                stop
        else
                row FAIL "the compositor starts without the card for the comparison" "$(tail -3 "$work/compositor.log")"
        fi
        extra=-G
        start || row FAIL "the compositor starts again after the comparison" "$(tail -3 "$work/compositor.log")" ;;
esac

# A client that cuts the files of its buffers under the compositor once five frames are drawn, and goes: the compositor reads a picture that is
# gone (zeros over the pool, the read that faulted goes on), goes on, and shows the next client.
out=$("$client" -T -n 30 -w 200 -h 120 2>&1)
sleep 1
if alive; then
        out=$("$client" -n 5 -w 200 -h 120 2>&1)
        case $out in
        *"frames 5"*) row ok "a client that cuts its buffers' files under the compositor leaves it running, and the next client is shown" ;;
        *) row FAIL "a client that cuts its buffers' files under the compositor leaves it running, and the next client is shown" "$out" ;;
        esac
else
        row FAIL "a client that cuts its buffers' files under the compositor leaves it running" "$(tail -3 "$work/compositor.log")"
fi

# The damage a client sends that is negative, past the buffer, huge and many and overlapping is clipped, not refused: the connection is kept and the
# compositor goes on (test/wayland_damage.py).
out=$(python3 -I "$(dirname "$0")/wayland_damage.py" 2>&1)
if alive && [ "$out" = kept ]; then
        row ok "hostile damage (negative, past the buffer, huge, overlapping) is clipped: the client is kept and the compositor goes on"
else
        row FAIL "hostile damage (negative, past the buffer, huge, overlapping) is clipped" "$out $(tail -3 "$work/compositor.log")"
fi

# What the protocol lets a client do that the shim got wrong (test/wayland_conform.py): a buffer destroyed between its attach and the commit,
# damage wholly outside the buffer of a window with a subsurface, and a client of two buffers that is given them back, shown and put away.
out=$(python3 -I "$(dirname "$0")/wayland_conform.py" "$work/scan.img" 2>&1)
echo "$out" | while read -r verdict rest; do
        case $verdict in ok | FAIL) echo "$verdict $rest" ;; esac
done
failures=$((failures + $(echo "$out" | grep -c '^FAIL')))
if ! alive || [ "$(echo "$out" | grep -c '^ok')" != 4 ]; then
        row FAIL "the conformance client ran its four rows and the compositor goes on" "$(echo "$out" | tail -2) $(tail -3 "$work/compositor.log")"
fi

# A window drawn a part at a time from three buffers in turn (test/wayland_part.py): each commit's damage is read from the buffer where it is,
# and the screen shows what a copy of the damage would; then the picture's file is cut under the compositor, which goes on.
out=$(python3 -I "$(dirname "$0")/wayland_part.py" "$work/scan.img" 2>&1)
echo "$out" | while read -r verdict rest; do
        case $verdict in ok | FAIL) echo "$verdict $rest" ;; esac
done
failures=$((failures + $(echo "$out" | grep -c '^FAIL')))
if ! alive || [ "$(echo "$out" | grep -c '^ok')" != 6 ]; then
        row FAIL "the client that draws a part at a time ran its six rows and the compositor goes on" "$(echo "$out" | tail -2) $(tail -3 "$work/compositor.log")"
fi

# A pool on a file whose pages the client serves itself and never does (FUSE, test/wayland_held.py): the pool is refused, and the compositor
# answers the next client. Mapped, its read of the picture waited on the client for as long as the client liked (MW-2026-195).
out=$(python3 -I "$(dirname "$0")/wayland_held.py" 2>&1)
case $out in
refused) if alive; then row ok "a pool on a file the client serves itself (FUSE) is refused, and the compositor answers the next client"
        else row FAIL "a pool on a file the client serves itself (FUSE) is refused" "the compositor is gone: $(tail -3 "$work/compositor.log")"; fi ;;
"NOT RUN"*) echo "NOT RUN a pool on a file the client serves itself (FUSE) is refused: ${out#NOT RUN }" ;;
*) row FAIL "a pool on a file the client serves itself (FUSE) is refused, and the compositor answers the next client" "$out" ;;
esac

# A window whose damage is a rectangle the card copies (over its threshold): the card's copy into the picture is the processor's, byte for byte, and
# moonwater debug says the card copied rectangles (not the processor).
case $extra in
*-G*)
        ln -sf "$compositor" "$work/bin/moonwater"
        "$client" -D -K -n 20 -w 640 -h 600 -q > /dev/null 2>&1 &
        held=$!
        sleep 2
        cp "$work/scan.img" "$work/card-damage.img"
        "$work/bin/moonwater" debug > "$work/debug-damage.txt" 2>&1
        kill "$held" 2> /dev/null
        wait "$held" 2> /dev/null
        stop
        extra=
        if start; then
                "$client" -D -K -n 20 -w 640 -h 600 -q > /dev/null 2>&1 &
                held=$!
                sleep 2
                cp "$work/scan.img" "$work/processor-damage.img"
                kill "$held" 2> /dev/null
                wait "$held" 2> /dev/null
                if cmp -s "$work/card-damage.img" "$work/processor-damage.img"; then
                        row ok "the card's copy of a window's damage is the processor's, byte for byte"
                else
                        row FAIL "the card's copy of a window's damage is the processor's, byte for byte" "the screens differ"
                fi
                stop
        else
                row FAIL "the compositor starts without the card for the damage comparison" "$(tail -3 "$work/compositor.log")"
        fi
        extra=-G
        start || row FAIL "the compositor starts again after the damage comparison" "$(tail -3 "$work/compositor.log")"
        if grep -q "client rects *[1-9][0-9]* damage rectangles copied by the card" "$work/debug-damage.txt" 2> /dev/null; then
                row ok "the card copied damage rectangles into the picture (moonwater debug counts them)"
        else
                row FAIL "the card copied damage rectangles into the picture (moonwater debug counts them)" "$(grep "client" "$work/debug-damage.txt" | head -2)"
        fi ;;
esac

# The compositor ends when asked, and takes its socket with it.
stop
if [ ! -S /run/moonwater/wayland-0 ]; then
        row ok "the compositor removes its socket when it ends"
else
        row FAIL "the compositor removes its socket when it ends"
fi

exit "$failures"
