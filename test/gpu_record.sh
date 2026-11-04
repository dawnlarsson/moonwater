#!/bin/sh
# The present record (src/canvas/record.h, moonwater canvas record SECONDS and report): the headless compositor with the pointer moved over
# its evdev fifo (test/gpu_anim_flicker.py's Pointer, so every move is a damaged present), and moonwater asking for a recording and a report.
# The rows: hostile arguments are usage and start nothing; a recording of one second is taken, and done by a present after its seconds; the
# report names the presents, the paths, the flips; the ring keeps 240 of more presents (its wraparound); moonwater debug has the last record.
#
#     unshare -rm sh test/gpu_record.sh COMPOSITOR [WORKDIR]
#
# COMPOSITOR is the shell binary, whose applet names are links to it. One line a row: "ok NAME" or "FAIL NAME ..."; the exit status is the
# number of failures. The session is a namespace of its own with a private /run and /root.

# sweep COUNT: COUNT pointer moves in one direction (a step each, wrapping at 600): a frame that is between two equal ones is then a change,
# not a return (the back and forth of drive is a flash by definition, and a clean screen must not have one)
sweep()
{
        python3 -I -c "
import os, sys, time
sys.path.insert(0, '$selfdir/test')
import gpu_anim_flicker as g
fd = os.open('$work/input.fifo', os.O_WRONLY)
p = g.Pointer(fd)
for i in range($1):
    p.move(100 + (i * 6) % 600, 300, 0.002)
    time.sleep(0.02)
os.close(fd)
"
}

# drive COUNT: COUNT pointer moves over the fifo, each a present
drive()
{
        python3 -I -c "
import os, sys
sys.path.insert(0, '$selfdir/test')
import gpu_anim_flicker as g
fd = os.open('$work/input.fifo', os.O_WRONLY)
p = g.Pointer(fd)
for i in range($1):
    p.move(200 if i % 2 else 600, 300, 0.002)
os.close(fd)
"
}

row()
{
        name=$1
        shift
        if "$@"; then
                echo "ok $name"
        else
                echo "FAIL $name"
                failures=$((failures + 1))
        fi
}

session()
{
        work=$1
        compositor=$2
        fonts=$3
        failures=0
        mount -t tmpfs tmpfs /run && mount -t tmpfs tmpfs /root || { echo "FAIL mounts"; return 1; }
        mount --bind "$fonts" /usr/share/fonts || { echo "FAIL fonts"; return 1; }
        mkdir -p /run/moonwater
        cd "$work" || return 1
        mkdir -p bin
        for name in compositor moonwater settings; do ln -sf "$compositor" "bin/$name"; done
        mkfifo "$work/input.fifo"
        bin/compositor -H 800x600 "$work/record.img" -i "$work/input.fifo" > "$work/compositor.log" 2>&1 &
        cpid=$!
        sleep 2
        # a settings window: its paints are the presents (a pointer over nothing draws no frame)
        bin/settings > "$work/settings.log" 2>&1 &
        i=0
        while [ $i -lt 60 ] && [ "$(bin/moonwater debug 2> /dev/null | grep -c '^  window')" -lt 1 ]; do
                sleep 0.5
                i=$((i + 1))
        done
        drive 60
        i=0
        # the compositor makes its record directory at its first look; a look is every eighth present
        while [ $i -lt 40 ] && [ ! -d /run/moonwater/record ]; do
                drive 8
                i=$((i + 1))
        done

        # hostile arguments: usage, and no recording started
        for arg in 0 11 99999999999 text 1x -1; do
                bin/moonwater canvas record "$arg" > "$work/hostile.out" 2>&1
                row "hostile $arg starts nothing" test ! -e /run/moonwater/record/start
        done
        bin/moonwater canvas record 1 2 > "$work/hostile.out" 2>&1
        row "hostile extra word starts nothing" test ! -e /run/moonwater/record/start

        # a one-second recording: asked for, taken by a look, done by a present after its seconds
        bin/moonwater canvas record 1 > "$work/start.out" 2>&1
        row "recording asked for" grep -q "recording 1 s" "$work/start.out"
        sweep 80
        # the recording ends at the first present after its seconds: sweep until the state says so (the sweep is one way, a clean scene)
        i=0
        while [ $i -lt 40 ] && ! grep -q "presents recorded" /run/moonwater/record/state 2> /dev/null; do
                sweep 5
                i=$((i + 1))
        done
        row "recording taken by the compositor" test ! -e /run/moonwater/record/start
        row "recording done with its counts" grep -q "presents recorded" /run/moonwater/record/state
        row "state counts presents" grep -Eq "^last: [1-9][0-9]* presents recorded" /run/moonwater/record/state

        # the report, asked for while presents run (the compositor takes the request at a look)
        drive 40 &
        dpid=$!
        bin/moonwater canvas record report > "$work/report.out" 2>&1
        wait "$dpid"
        row "report names the paths" grep -q "^paths " "$work/report.out"
        row "report names the flips" grep -q "^flips " "$work/report.out"
        row "report says the recording is done" grep -q "recording    done" "$work/report.out"
        # a clean sweep has no flash, size jump or blank; a tear in it is reported by its count and frame (headless, under investigation)
        row "clean recording has no flash, size or blank" grep -Eq "^classes      tear [0-9]+, flash 0, size jump 0, blank 0;" "$work/report.out"
        row "report request taken" test ! -e /run/moonwater/record/report

        # wraparound: more than the ring's size of presents, and the ring keeps the newest 240
        # a present is a frame, not a move: the moves go on until the ring is full (its newest 240 kept, the rest counted in all)
        n=0
        while [ $n -lt 25 ]; do
                drive 100
                bin/moonwater canvas record report > "$work/ring.out" 2>&1
                grep -q "^presents      240 in the ring of 240" "$work/ring.out" && break
                n=$((n + 1))
        done
        row "ring keeps 240" grep -q "^presents      240 in the ring of 240" "$work/ring.out"
        row "ring counts all presents" awk '/ in all$/ { split($0, f, ", "); n = f[2]; sub(/ in all/, "", n); if (n + 0 > 240) ok = 1 } END { exit ok ? 0 : 1 }' "$work/ring.out"

        # the check's tear: the injected frame is classed as a tear, written as a PPM, and the file is taken
        : > /run/moonwater/record/inject
        bin/moonwater canvas record 1 > "$work/inject.out" 2>&1
        drive 80
        bin/moonwater canvas record report > "$work/inject.report" 2>&1
        row "injected tear is classed" grep -Eq "^classes      tear [1-9]" "$work/inject.report"
        row "injected tear is written as a PPM" sh -c 'f=$(ls /run/moonwater/record/frame-*-tear.ppm 2>/dev/null | head -n 1); [ -n "$f" ] && head -c 2 "$f" | grep -q P6'
        row "injected file is taken" test ! -e /run/moonwater/record/inject

        # moonwater debug: the last record line
        bin/moonwater debug > "$work/debug.out" 2>&1
        row "debug has the last record" grep -q "last record" "$work/debug.out"

        kill "$cpid" 2> /dev/null
        wait "$cpid" 2> /dev/null
        return "$failures"
}

if [ "${1:-}" = session ]; then
        session "$2" "$3" "$4"
        exit $?
fi

compositor=$1
work=${2:-$(mktemp -d)}
selfdir=$(cd "$(dirname "$0")/.." && pwd)
self=$(cd "$(dirname "$0")" && pwd)/$(basename "$0")
export selfdir
case $compositor in /*) ;; *) compositor=$(pwd)/$compositor ;; esac
case $work in /*) ;; *) work=$(pwd)/$work ;; esac
mkdir -p "$work"
fonts=$(cd "$selfdir/fonts" && pwd)
unshare -rm sh "$self" session "$work" "$compositor" "$fonts" > "$work/session.out" 2>&1
status=$?
cat "$work/session.out"
exit "$status"
