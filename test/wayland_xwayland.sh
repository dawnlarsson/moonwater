#!/bin/sh
# The X clients on the Wayland shim: Xwayland as a client of the compositor (rootful: one window that holds the X screen), and an
# Xlib client in it, end to end, on the headless compositor.
#
#     unshare -rm sh test/wayland_xwayland.sh COMPOSITOR XROOT XCLIENT WORKDIR
#
# COMPOSITOR is the shell binary reached by the name `compositor` (built with CONFIG_MOONWATER_WAYLAND), XROOT the root Xwayland
# runs in (/ when the machine has one; otherwise an Arch root with xorg-xwayland and xorg-xprop in it), XCLIENT test/wayland_xclient.c
# built against that root's X11 headers and libX11 (a binary the root can run). It runs as root of a user namespace with a private
# /run, and its own /tmp in the root, so the X server's socket and the nothing of the machine's are touched. One line a row: "ok
# NAME" or "FAIL NAME ...", and the exit status is the number of failures.
compositor=$1
xroot=$2
xclient=$3
work=$4
[ -n "$xroot" ] && [ -n "$work" ] || { echo "usage: sh test/wayland_xwayland.sh COMPOSITOR XROOT XCLIENT WORKDIR" >&2; exit 2; }
here=$(dirname "$0")
failures=0
mount --make-rprivate / 2> /dev/null
mount -t tmpfs tmpfs /run || exit 1
mount -t tmpfs tmpfs /root || exit 1
mount -t tmpfs tmpfs /tmp || exit 1
mkdir -p /run/moonwater "$work/bin"
ln -sf "$compositor" "$work/bin/compositor"
export WAYLAND_DISPLAY=/run/moonwater/wayland-0 XDG_RUNTIME_DIR=/run

row()
{
        if [ "$1" = ok ]; then echo "ok $2"; else echo "FAIL $2 ${3:-}"; failures=$((failures + 1)); fi
}

# The root sees the compositor's socket (/run), the trial directory (this work directory) and a /tmp of its own, where the X socket
# goes. Nothing else of the machine is bound in.
if [ "$xroot" != / ]; then
        mkdir -p "$xroot/trial" "$xroot/run" "$xroot/tmp"
        mount --bind /run "$xroot/run"
        mount --bind "$work" "$xroot/trial"
        mount -t tmpfs tmpfs "$xroot/tmp"
        mkdir -p "$xroot/tmp/.X11-unix"
        chmod 1777 "$xroot/tmp/.X11-unix"
        inside=$xroot
        clientpath=/trial/$(basename "$xclient")
else
        inside=
        clientpath=$xclient
fi
xenv="HOME=/tmp XDG_RUNTIME_DIR=/run/user/0 WAYLAND_DISPLAY=$WAYLAND_DISPLAY PATH=/usr/local/bin:/usr/bin:/bin LANG=C.UTF-8"
cp "$xclient" "$work/$(basename "$xclient")" 2> /dev/null

# The compositor, headless, with the evdev FIFO (-i) the rows write to.
rm -f "$work/in.fifo"
mkfifo "$work/in.fifo"
"$work/bin/compositor" -H 800x600 "$work/scan.img" -i "$work/in.fifo" > "$work/compositor.log" 2>&1 &
cpid=$!
i=0
while [ ! -S /run/moonwater/wayland-0 ] && [ $i -lt 100 ]; do sleep 0.1; i=$((i + 1)); done
if [ ! -S /run/moonwater/wayland-0 ]; then
        row FAIL "the compositor listens on /run/moonwater/wayland-0" "$(tail -3 "$work/compositor.log")"
        exit 1
fi
row ok "the compositor listens on /run/moonwater/wayland-0"

# Xwayland, rootful, on display :1 with the glamor off (no card in a namespace); -verbose says what it does on its log.
if [ -n "$inside" ]; then
        chroot "$xroot" /usr/bin/env -i $xenv /usr/bin/Xwayland :1 -fullscreen -glamor off -nolisten local -verbose 3 > "$work/xwayland.log" 2>&1 &
else
        Xwayland :1 -fullscreen -glamor off -nolisten local -verbose 3 > "$work/xwayland.log" 2>&1 &
fi
xpid=$!
i=0
while [ ! -S "${xroot%/}/tmp/.X11-unix/X1" ] && [ ! -S /tmp/.X11-unix/X1 ] && [ $i -lt 100 ]; do sleep 0.1; i=$((i + 1)); done
if ! kill -0 "$xpid" 2> /dev/null; then
        row FAIL "Xwayland runs as a client of the compositor" "$(tail -3 "$work/xwayland.log")"
        kill "$cpid" 2> /dev/null
        exit 1
fi
row ok "Xwayland runs as a client of the compositor"

# No abstract socket: a process of the network namespace could connect to an abstract one, so the X server listens on its file in its
# own /tmp only (-nolisten local takes the abstract one out).
if grep -q "@/tmp/.X11-unix/X1" /proc/net/unix 2> /dev/null; then
        row FAIL "the X server listens on no abstract socket" "@/tmp/.X11-unix/X1 is in /proc/net/unix"
else
        row ok "the X server listens on no abstract socket"
fi

# The root's properties through the X socket: xprop reads the root window (the keyboard's rules are one of them).
if [ -n "$inside" ]; then
        props=$(chroot "$xroot" /usr/bin/env -i $xenv DISPLAY=:1 timeout 10 /usr/bin/xprop -root 2>&1)
else
        props=$(DISPLAY=:1 timeout 10 xprop -root 2>&1)
fi
case $props in
*_XKB_RULES_NAMES*) row ok "xprop reads the root window through the X socket" ;;
*) row FAIL "xprop reads the root window through the X socket" "$props" ;;
esac

# The X client: an Xlib program in the same root, its output in the work directory.
if [ -n "$inside" ]; then
        chroot "$xroot" /usr/bin/env -i $xenv DISPLAY=:1 "$clientpath" -w 800 -h 600 -t 40 > "$work/xclient.log" 2>&1 &
else
        DISPLAY=:1 "$xclient" -w 800 -h 600 -t 40 > "$work/xclient.log" 2>&1 &
fi
xclpid=$!
python3 -I "$here/wayland_xwayland.py" "$work/in.fifo" "$work/xclient.log" "$work/scan.img" 800 600 > "$work/rows.out" 2>&1
status=$?
grep -v '^[0-9]* failed$' "$work/rows.out"
if [ "$status" -ne 0 ] && ! grep -q '^FAIL' "$work/rows.out"; then
        row FAIL "the X rows ran" "$(tail -3 "$work/rows.out")"
fi
failures=$((failures + $(grep -c '^FAIL' "$work/rows.out")))

kill "$xclpid" 2> /dev/null
kill "$xpid" 2> /dev/null
kill "$cpid" 2> /dev/null
wait "$xpid" "$cpid" 2> /dev/null
exit "$failures"
