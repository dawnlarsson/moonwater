#!/bin/sh
# A real program on the shim with a setting driven while it runs, in a private root of a distribution that has the program:
#
#     unshare -rmpf sh test/wayland_trial.sh COMPOSITOR ROOT NAME PROBE.py PROBE_ARGS... -- COMMAND...
#
# COMPOSITOR is the shell built with CONFIG_MOONWATER_WAYLAND, ROOT a root with the program (an Arch root with chromium, gtk, qt,
# foot, as test/wayland_e2e.sh's clients are not), NAME what to call the files it leaves in $TRIAL_DIR (default /tmp), PROBE.py a
# script such as test/wayland_chromium.py that gets the compositor's screen file and /root/canvas.displays among its arguments.
# The compositor is headless (TRIAL_SIZE, 1280x800), in a private /run and /root, so the machine's own are not touched.
# This is the trial the settings work was shown with; it is not a lane (it needs the root).
compositor=$1
root=$2
name=$3
probe=$4
shift 4
probe_args=
while [ "$#" -gt 0 ] && [ "$1" != -- ]; do
        probe_args="$probe_args $1"
        shift
done
shift
[ -n "$root" ] && [ -n "$name" ] || { echo "usage: sh test/wayland_trial.sh COMPOSITOR ROOT NAME PROBE.py PROBE_ARGS... -- COMMAND..." >&2; exit 2; }
dir=${TRIAL_DIR:-/tmp}
size=${TRIAL_SIZE:-1280x800}
mkdir -p "$dir/bin"
ln -sf "$compositor" "$dir/bin/compositor"
mount -t proc proc /proc || exit 1
mount -t tmpfs tmpfs /run || exit 1
mount -t tmpfs tmpfs /root || exit 1
mkdir -p /run/moonwater /run/user/0
chmod 700 /run/user/0
[ -z "$TRIAL_DISPLAYS" ] || echo "$TRIAL_DISPLAYS" > /root/canvas.displays
[ -z "$TRIAL_THEME" ] || echo "$TRIAL_THEME" > /root/canvas.theme
"$dir/bin/compositor" -H "$size" "$dir/$name.scan" $TRIAL_COMPOSITOR_ARGS > "$dir/$name.compositor" 2>&1 &
cpid=$!
# TRIAL_BUS=1 also runs the session bus (src/dbus), where the program in the root finds its settings: its address is in the program's
# environment.
if [ -n "$TRIAL_BUS" ]; then
        ln -sf "$compositor" "$dir/bin/sessionbus"
        "$dir/bin/sessionbus" $TRIAL_BUS_ARGS > "$dir/$name.bus" 2>&1 &
        bpid=$!
        TRIAL_ENV="$TRIAL_ENV DBUS_SESSION_BUS_ADDRESS=${TRIAL_BUS_ADDRESS:-unix:path=/run/moonwater/bus}"
fi
# TRIAL_PRE is a command to run beside them (a proxy that logs what a program says to the bus, say).
if [ -n "$TRIAL_PRE" ]; then
        sh -c "$TRIAL_PRE" &
        sleep 0.5
fi
i=0
while [ ! -S /run/moonwater/wayland-0 ] && [ $i -lt 100 ]; do sleep 0.1; i=$((i + 1)); done
mount --rbind /dev "$root/dev"
mount --rbind /sys "$root/sys"
mount -t proc proc "$root/proc"
mount --bind /run "$root/run"
mount -t tmpfs tmpfs "$root/tmp"
# What the program in the root writes for the probe to read: TRIAL_DIR is /trial there.
mkdir -p "$root/trial"
mount --bind "$dir" "$root/trial"
# shellcheck disable=SC2086
chroot "$root" /usr/bin/env HOME=/tmp XDG_RUNTIME_DIR=/run/user/0 WAYLAND_DISPLAY=/run/moonwater/wayland-0 MOONWATER_ASKED=1 \
        LANG=C.UTF-8 PATH=/usr/local/bin:/usr/bin:/bin $TRIAL_ENV "$@" > "$dir/$name.log" 2>&1 &
client=$!
# shellcheck disable=SC2086
python3 "$probe" $probe_args > "$dir/$name.probe" 2>&1
status=$?
kill $client 2> /dev/null
sleep 1
kill -9 $client 2> /dev/null
if kill -0 $cpid 2> /dev/null; then echo "compositor alive" >> "$dir/$name.probe"; else echo "COMPOSITOR DIED" >> "$dir/$name.probe"; status=1; fi
kill $cpid $bpid 2> /dev/null
exit $status
