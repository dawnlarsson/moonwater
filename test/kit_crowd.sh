#!/bin/sh
# sh test/kit_crowd.sh SHELL N [CGROUP_MAX]: a headless compositor and N kit windows (surface) at once, in a mount namespace with a /run of its own.
# SHELL is a shell built with CONFIG_MOONWATER_WAYLAND (as test/wayland_stress/run.sh builds it), run as root.
# With CGROUP_MAX (bytes, 134217728 for 128 MiB), the windows are started in a memory cgroup (v1) of that limit, the compositor outside it:
# the kernel kills windows as the limit is met, and the compositor gives their rows back and goes on.
# Prints how many windows were given a row, the rows in use, and whether the compositor is alive, with its RSS, threads and descriptors.
S=${CROWD_WORK:-${TMPDIR:-/tmp}/kit-crowd.$(id -u)}
if [ -z "$CROWD_INSIDE" ]; then
        export CROWD_INSIDE=1
        if [ -n "$3" ]; then
                # A memory cgroup of the limit, which the windows (not the compositor) are started in.
                C=$(sed -n 's/^[0-9]*:memory://p' /proc/self/cgroup)
                G=/sys/fs/cgroup/memory$C/crowd
                mkdir -p $G
                echo 0 > $G/memory.swappiness 2> /dev/null
                echo $3 > $G/memory.limit_in_bytes || exit 1
                export CROWD_CGROUP=$G
                echo "the windows in the memory cgroup $G, limit $(cat $G/memory.limit_in_bytes)"
        fi
        exec unshare -m sh "$0" "$@"
fi
B=$1
N=${2:-1000}
LIMIT=$3
W=$S
rm -rf $W
mkdir -p $W/bin $W/log
mount --make-rprivate / 2> /dev/null
mount -t tmpfs tmpfs /run || exit 1
mount -t tmpfs tmpfs /root || exit 1
ln -s "$B" $W/bin/compositor
ln -s "$B" $W/bin/surface
$W/bin/compositor -H 1280x720 $W/scan.img > $W/log/compositor 2>&1 &
cp=$!
t=0
while [ ! -e /run/moonwater/windows ] && [ $t -lt 100 ]; do sleep 0.1; t=$((t + 1)); done
echo "compositor $cp up after $t tenths"
started=$(date +%s)
# The windows, and nothing before them, are charged to the cgroup: this shell moves into it once the compositor runs.
[ -n "$CROWD_CGROUP" ] && echo $$ > $CROWD_CGROUP/cgroup.procs
i=0
while [ $i -lt $N ]; do
        $W/bin/surface -s 600 -w 32 -h 24 -x $((i % 40 * 30)) -y $((i / 40 % 25 * 26)) -T w$i > $W/log/s.$i 2>&1 &
        i=$((i + 1))
done
t=0
while [ $t -lt 240 ]; do
        got=$(cat $W/log/s.* 2> /dev/null | grep -c "^surface: [0-9]* at")
        [ "$got" -ge "$N" ] && break
        # No window more for 20 s: what was refused or killed is not coming.
        [ "$got" = "${last:-}" ] && still=$((${still:-0} + 1)) || still=0
        last=$got
        [ "$still" -ge 20 ] && break
        sleep 1
        t=$((t + 1))
done
echo "windows given a row: $got of $N after $(( $(date +%s) - started )) s"
echo "rows distinct: $(cat $W/log/s.* | sed -n 's/^surface: \([0-9]*\) at.*/\1/p' | sort -u | wc -l), highest $(cat $W/log/s.* | sed -n 's/^surface: \([0-9]*\) at.*/\1/p' | sort -n | tail -1)"
echo "refused: $(cat $W/log/s.* | grep -c 'no window')"
cat $W/log/s.* | grep "no window" | sort | uniq -c | head -3
if kill -0 $cp 2> /dev/null; then
        echo "compositor alive: rss $(awk '/VmRSS/{print $2}' /proc/$cp/status) kB, threads $(awk '/Threads/{print $2}' /proc/$cp/status), descriptors $(ls /proc/$cp/fd | wc -l)"
else
        echo "compositor GONE"
fi
grep -v "^$" $W/log/compositor | tail -5
echo "windows shown: $(grep -l "^surface: [0-9]* at" $W/log/s.* | wc -l); oom kills in the cgroup: $(sed -n 's/oom_kill //p' $CROWD_CGROUP/memory.oom_control 2> /dev/null)"
echo "surfaces still running: $(pgrep -f "$W/bin/surface" | wc -l)"
pkill -f "$W/bin/surface"
sleep 1
kill $cp 2> /dev/null
wait 2> /dev/null
