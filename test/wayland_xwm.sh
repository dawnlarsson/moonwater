#!/bin/sh
# The window manager of Xwayland on the Wayland shim: the compositor accepts Xwayland's window manager connection on its socket
# (/run/moonwater/xwm/wm-0), redirects the root's subwindows with Composite, and an Xlib client's window gets its surface, which
# is what a rootless Xwayland makes of a mapped window. Run in a user namespace, as wayland_xwayland.sh is:
#
#     unshare -rm sh test/wayland_xwm.sh COMPOSITOR XROOT XWMCLIENT WORKDIR
#
# COMPOSITOR is the shell binary reached by the name `compositor` (CONFIG_MOONWATER_WAYLAND), XROOT the root Xwayland runs in
# (/ or an Arch root with xorg-xwayland), XWMCLIENT test/wayland_xwm_client.c built against that root's X11 headers and libX11.
# One line a row: "ok NAME" or "FAIL NAME ...", and the exit status is the number of failures.
compositor=$1
xroot=$2
xclient=$3
work=$4
[ -n "$xroot" ] && [ -n "$work" ] || { echo "usage: sh test/wayland_xwm.sh COMPOSITOR XROOT XWMCLIENT WORKDIR" >&2; exit 2; }
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

if [ "$xroot" != / ]; then
        mkdir -p "$xroot/trial" "$xroot/run" "$xroot/tmp"
        mount --bind /run "$xroot/run"
        mount --bind "$work" "$xroot/trial"
        mount -t tmpfs tmpfs "$xroot/tmp"
        mkdir -p "$xroot/tmp/.X11-unix"
        chmod 1777 "$xroot/tmp/.X11-unix"
        clientpath=/trial/$(basename "$xclient")
else
        clientpath=$xclient
fi
cp "$xclient" "$work/$(basename "$xclient")" 2> /dev/null

rm -f "$work/in.fifo"
mkfifo "$work/in.fifo"
"$work/bin/compositor" -H 800x600 "$work/scan.img" -i "$work/in.fifo" > "$work/compositor.log" 2>&1 &
cpid=$!
echo "$cpid" > "$work/cpid"
i=0
while [ ! -S /run/moonwater/xwm/wm-0 ] && [ $i -lt 100 ]; do sleep 0.1; i=$((i + 1)); done
if [ ! -S /run/moonwater/xwm/wm-0 ]; then
        row FAIL "the compositor listens for the window manager of Xwayland" "$(tail -3 "$work/compositor.log")"
        kill "$cpid" 2> /dev/null
        exit 1
fi
row ok "the compositor listens for the window manager of Xwayland"

python3 -I "$here/wayland_xwm.py" "$work" "$xroot" "$clientpath" > "$work/rows.out" 2>&1
status=$?
grep -v '^[0-9]* failed$' "$work/rows.out"
if [ "$status" -ne 0 ] && ! grep -q '^FAIL' "$work/rows.out"; then
        row FAIL "the window manager rows ran" "$(tail -3 "$work/rows.out")"
fi
failures=$((failures + $(grep -c '^FAIL' "$work/rows.out")))

kill "$cpid" 2> /dev/null
wait "$cpid" 2> /dev/null
exit "$failures"
