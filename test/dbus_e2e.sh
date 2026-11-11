#!/bin/sh
# The session bus (src/dbus) end to end: the sessionbus tool running on a private /run with a private /root for the settings,
# and clients that are not ours talking to it: sd-bus (busctl), libdbus (dbus-send, dbus-monitor), GDBus (gdbus and Python's Gio).
#
#     unshare -rm sh test/dbus_e2e.sh SHELL [WORKDIR]
#
# SHELL is the shell binary, reached by the name `sessionbus`. One line a row: "ok NAME" or "FAIL NAME ...", and the exit status is
# the number of failures.
shell=$1
work=${2:-$(mktemp -d)}
failures=0
mkdir -p "$work/bin"
ln -sf "$shell" "$work/bin/sessionbus"
mount -t tmpfs tmpfs /run || exit 1
mount -t tmpfs tmpfs /root || exit 1
mkdir -p /run/moonwater
address=unix:path=/run/moonwater/bus

row()
{
        if [ "$1" = ok ]; then echo "ok $2"; else echo "FAIL $2 ${3:-}"; failures=$((failures + 1)); fi
}

echo dark > /root/canvas.theme
"$work/bin/sessionbus" > "$work/bus.log" 2>&1 &
bpid=$!
i=0
while [ ! -S /run/moonwater/bus ] && [ $i -lt 100 ]; do sleep 0.1; i=$((i + 1)); done
if [ ! -S /run/moonwater/bus ]; then
        row FAIL "the session bus listens on /run/moonwater/bus" "$(tail -3 "$work/bus.log")"
        exit 1
fi
row ok "the session bus listens on /run/moonwater/bus"
mode=$(stat -c %a /run/moonwater/bus)
[ "$mode" = 666 ] && row ok "and is open to every user, as the Wayland socket is" || row FAIL "and is open to every user" "mode $mode"

[ "$(cat /run/moonwater/color-scheme 2> /dev/null)" = dark ] && row ok "the bus keeps the scheme of the moment in a file beside its socket: dark" ||
        row FAIL "the bus keeps the scheme of the moment in a file beside its socket" "$(cat /run/moonwater/color-scheme 2>&1)"
if command -v busctl > /dev/null 2>&1; then
        out=$(busctl --address="$address" call org.freedesktop.portal.Desktop /org/freedesktop/portal/desktop org.freedesktop.portal.Settings ReadOne ss org.freedesktop.appearance color-scheme 2>&1)
        [ "$out" = "v u 1" ] && row ok "sd-bus (busctl) reads the color-scheme: v u 1" || row FAIL "sd-bus (busctl) reads the color-scheme" "$out"
        out=$(busctl --address="$address" call org.freedesktop.DBus /org/freedesktop/DBus org.freedesktop.DBus GetNameOwner s org.freedesktop.portal.Desktop 2>&1)
        [ "$out" = 's ":1.1"' ] && row ok "and the portal's name is owned by :1.1" || row FAIL "and the portal's name is owned by :1.1" "$out"
        out=$(busctl --address="$address" --list 2>&1 | grep -c "^:1\.\|^org\.")
        [ "$out" -ge 2 ] && row ok "and lists the names on the bus" || row FAIL "and lists the names on the bus" "$out"
        out=$(busctl --address="$address" introspect org.freedesktop.portal.Desktop /org/freedesktop/portal/desktop 2>&1 | grep -c "SettingChanged")
        [ "$out" -ge 1 ] && row ok "and introspects the portal: SettingChanged is there" || row FAIL "and introspects the portal" "$out"
fi
if command -v dbus-send > /dev/null 2>&1; then
        out=$(dbus-send --bus="$address" --print-reply --dest=org.freedesktop.portal.Desktop /org/freedesktop/portal/desktop org.freedesktop.portal.Settings.ReadOne string:org.freedesktop.appearance string:color-scheme 2>&1)
        case $out in
        *"uint32 1"*) row ok "libdbus (dbus-send) reads the color-scheme: uint32 1" ;;
        *) row FAIL "libdbus (dbus-send) reads the color-scheme" "$out" ;;
        esac
        out=$(dbus-send --bus="$address" --print-reply --dest=org.freedesktop.DBus /org/freedesktop/DBus org.freedesktop.DBus.ListNames 2>&1)
        case $out in
        *"org.freedesktop.portal.Desktop"*) row ok "and ListNames names the portal" ;;
        *) row FAIL "and ListNames names the portal" "$out" ;;
        esac
fi
if command -v gdbus > /dev/null 2>&1; then
        out=$(gdbus call --address "$address" --dest org.freedesktop.portal.Desktop --object-path /org/freedesktop/portal/desktop --method org.freedesktop.portal.Settings.ReadAll "['org.freedesktop.appearance']" 2>&1)
        case $out in
        *"'color-scheme': <uint32 1>"*) row ok "GDBus (gdbus) reads all of org.freedesktop.appearance" ;;
        *) row FAIL "GDBus (gdbus) reads all of org.freedesktop.appearance" "$out" ;;
        esac
        gdbus monitor --address "$address" --dest org.freedesktop.portal.Desktop > "$work/monitor.out" 2>&1 &
        mpid=$!
        sleep 1
        echo light > /root/canvas.theme
        sleep 2
        [ "$(cat /run/moonwater/color-scheme 2> /dev/null)" = light ] && row ok "and the file says light once the theme is" || row FAIL "and the file says light once the theme is" "$(cat /run/moonwater/color-scheme 2>&1)"
        kill $mpid 2> /dev/null
        wait $mpid 2> /dev/null
        if grep -q "SettingChanged.*color-scheme.*uint32 2" "$work/monitor.out"; then
                row ok "gdbus monitor sees the theme turn light: SettingChanged color-scheme 2"
        else
                row FAIL "gdbus monitor sees the theme turn light" "$(tail -3 "$work/monitor.out")"
        fi
        echo dark > /root/canvas.theme
        sleep 1.5
fi
if python3 -c 'import gi' 2> /dev/null; then
        python3 "$(dirname "$0")/dbus_gio.py" "$address" /root > "$work/gio.out" 2>&1
        result=$?
        cut -c1-500 "$work/gio.out"
        failures=$((failures + result))
else
        echo "skipped: no PyGObject"
fi

# Noise at the socket: bytes that are no authentication, a flood, a connection left silent, and the bus is as it was.
python3 - "$address" << 'PY'
import os, socket, sys, time
path = sys.argv[1].split("=", 1)[1]
def raw(data, read=False):
    s = socket.socket(socket.AF_UNIX)
    s.connect(path)
    s.settimeout(2)
    try:
        s.sendall(data)
        got = s.recv(4096) if read else b""
    except (OSError, socket.timeout):
        got = b""
    s.close()
    return got
raw(b"GET / HTTP/1.1\r\n\r\n")
raw(os.urandom(100000))
raw(b"\0" + b"AUTH EXTERNAL " + b"zz" * 300 + b"\r\n")
raw(b"\0AUTH ANONYMOUS\r\nBEGIN\r\n" + os.urandom(3000))
good = raw(b"\0AUTH ANONYMOUS\r\n", read=True)
print("ok the bus answers an authentication after noise" if good.startswith(b"OK ") else "FAIL the bus answers an authentication after noise %r" % good)
idle = []
for _ in range(40):
    s = socket.socket(socket.AF_UNIX)
    s.connect(path)
    idle.append(s)
for s in idle:
    s.close()
PY
sleep 0.5
echo dark > /root/canvas.theme
sleep 1.5
if kill -0 $bpid 2> /dev/null; then row ok "the bus is alive after noise, a flood of connections and bytes that are no protocol"; else row FAIL "the bus is alive after noise" "$(tail -3 "$work/bus.log")"; fi
out=$(busctl --address="$address" call org.freedesktop.portal.Desktop /org/freedesktop/portal/desktop org.freedesktop.portal.Settings ReadOne ss org.freedesktop.appearance color-scheme 2>&1)
[ "$out" = "v u 1" ] && row ok "and still answers" || row FAIL "and still answers" "$out"

# A FIFO where a setting's file is (2026-11-04): the bus opened each file to read it and would wait on a FIFO for a writer for ever.
rm -f /root/canvas.motion
mkfifo /root/canvas.motion
echo light > /root/canvas.theme
sleep 1.5
out=$(timeout 5 busctl --address="$address" call org.freedesktop.portal.Desktop /org/freedesktop/portal/desktop org.freedesktop.portal.Settings ReadOne ss org.freedesktop.appearance color-scheme 2>&1)
[ "$out" = "v u 2" ] && row ok "a FIFO where a setting is does not hold the bus: it reads the theme turn light and answers" || row FAIL "a FIFO where a setting is does not hold the bus" "$out"
rm -f /root/canvas.motion
echo dark > /root/canvas.theme
sleep 1.5

# The bus ends when asked, and takes its socket with it.
kill -TERM $bpid 2> /dev/null
i=0
while kill -0 $bpid 2> /dev/null && [ $i -lt 30 ]; do sleep 0.1; i=$((i + 1)); done
if [ ! -S /run/moonwater/bus ] && ! kill -0 $bpid 2> /dev/null; then row ok "the bus ends on SIGTERM and removes its socket"; else row FAIL "the bus ends on SIGTERM and removes its socket"; fi
exit "$failures"
