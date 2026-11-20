#!/bin/sh
#       The window gauntlet in its namespace (run.sh execs it under unshare -rm): private /run, /tmp and /root, the compositor
#       headless, Xwayland under its window manager, the cases in the bowl's root by chroot.
set -u
work=$1 xroot=$2 filter=$3
mount --make-rprivate / 2> /dev/null
mount -t tmpfs tmpfs /run || exit 1
mount -t tmpfs tmpfs /tmp || exit 1
mount -t tmpfs tmpfs /root || exit 1
mkdir -p /run/moonwater "$xroot/trial" "$xroot/run" "$xroot/tmp" "$xroot/dev"
mount --bind /run "$xroot/run"
mount --bind "$work" "$xroot/trial"
mount --rbind /dev "$xroot/dev" 2> /dev/null
mount -t tmpfs tmpfs "$xroot/tmp"
mkdir -p "$xroot/tmp/.X11-unix" && chmod 1777 "$xroot/tmp/.X11-unix"

rm -f "$work/in.fifo" && mkfifo "$work/in.fifo"
"$work/compositor" -H 800x600 "$work/scan.img" -i "$work/in.fifo" > "$work/compositor.log" 2>&1 &
cpid=$!
exec 9> "$work/in.fifo" # the input FIFO held open, so the compositor never reads its end
i=0
while [ ! -S /run/moonwater/xwm/wm-0 ] && [ $i -lt 100 ]; do sleep 0.1; i=$((i + 1)); done
[ -S /run/moonwater/xwm/wm-0 ] || { echo "the compositor did not start:"; tail -5 "$work/compositor.log"; exit 1; }
#       The driver runs in the bowl's root with its own Python (the host's python3 may be a bowl's program, in a namespace of its
#       own where chroot is refused), so the clients and Xwayland are started directly there.
chroot "$xroot" /usr/bin/python3 -I /trial/driver.py /trial "" "$filter" 2>&1
status=$?
kill "$cpid" 2> /dev/null
wait "$cpid" 2> /dev/null
echo "compositor log: $(grep -c . "$work/compositor.log") lines; the last:"
tail -5 "$work/compositor.log" | sed 's/^/  /'
exit $status
