#!/bin/sh
# The animations, sampled as the screen is written (test/gpu_anim_flicker.py, test/gpu_scan_sample.c): the headless compositor
# composes on the card (-G) in three sessions with the same settings and mixer and the same input over the evdev fifo:
#   card       canvas gpu on, the apps draw their lists (the card composes every frame)
#   cardoff    the card composes, the lists are off (touch /run/moonwater/lists-off): the apps paint pixels
#   processor  canvas gpu off: the processor composes every frame
# A sampler copies the scan image as fast as it can while the steps run; each distinct frame is torn (a band of one state and the rest
# of another), foreign (a colour seen in no other frame) or flipflop (a state that goes away and comes back). The counts are per step, in
# each session, in the table the script prints and in WORKDIR/MODE.classify.txt.
#
#     unshare -rm sh test/gpu_anim_flicker.sh COMPOSITOR [WORKDIR]
#
# COMPOSITOR is the shell binary (its applet names are links to it, as in test/gpu_lists_e2e.sh). One line a row: "ok NAME" or "FAIL NAME ...".
# The exit status is the number of failures; the counts are information until a session's rows say otherwise.

# session MODE WORKDIR FONTS: one headless compositor and its sampler, the apps, the steps, then the samples' classification
session()
{
        mode=$1
        work=$2
        fonts=$3
        mount -t tmpfs tmpfs /run
        mount -t tmpfs tmpfs /root
        mkdir -p /dev/pts && mount -t devpts -o newinstance,ptmxmode=0666 devpts /dev/pts && mount --bind /dev/pts/ptmx /dev/ptmx || echo "no pty for the terminal" >&2
        mount --bind "$fonts" /usr/share/fonts || return 1
        mkdir -p /run/moonwater
        fonts_mono=/usr/share/fonts/ibm-plex-mono/IBMPlexMono-Regular.ttf fonts_bold=/usr/share/fonts/ibm-plex-mono/IBMPlexMono-Bold.ttf
        cd "$work" || return 1
        case $mode in
        cardoff) : > /run/moonwater/lists-off ;;
        processor) echo off > /root/canvas.gpu ;;
        esac
        mkdir -p "$work/$mode"
        mkfifo "$work/input.fifo"
        bin/compositor -H 800x600 "$work/$mode.img" -G -i "$work/input.fifo" > "$work/$mode.compositor.log" 2>&1 &
        cpid=$!
        # the sampler starts before the first frame: it waits for the image (a minute at most)
        "$work/gpu_scan_sample" "$work/$mode.img" 800 600 "$work/$mode.samples" > "$work/$mode.sampler.out" 2>&1 &
        spid=$!
        sleep 2
        for app in $APPS; do
                case $app in
                terminal) bin/terminal -f "$fonts_mono" -b "$fonts_bold" -x /bin/sh > "$work/$mode.$app.log" 2>&1 & ;;
                editor) bin/editor > "$work/$mode.$app.log" 2>&1 & ;;
                *) bin/$app > "$work/$mode.$app.log" 2>&1 & ;;
                esac
        done
        i=0
        while [ $i -lt 60 ] && [ "$(bin/moonwater debug 2> /dev/null | grep -c '^  window')" -lt "$napps" ]; do
                sleep 0.5
                i=$((i + 1))
        done
        sleep 6
        bin/moonwater debug > "$work/$mode.canvas.txt" 2>&1
        python3 "$selfdir/test/gpu_anim_flicker.py" drive "$work/$mode" "$work/input.fifo" "$work/$mode.canvas.txt" > "$work/$mode.steps.out" 2>&1
        bin/moonwater debug > "$work/$mode.touched.txt" 2>&1
        cp "$work/$mode.img" "$work/$mode.final.img"
        kill -TERM "$spid" 2> /dev/null
        wait "$spid" 2> /dev/null
        kill "$cpid" 2> /dev/null
        wait "$cpid" 2> /dev/null
        python3 "$selfdir/test/gpu_anim_flicker.py" classify "$work/$mode.samples" "$work/$mode/steps.tsv" > "$work/$mode.classify.txt" 2>&1
        return 0
}

if [ "${1:-}" = session ]; then
        session "$2" "$3" "$4"
        exit 0
fi

compositor=$1
work=${2:-$(mktemp -d)}
APPS=${APPS:-"settings mixer"}
export APPS
napps=$(echo $APPS | wc -w)
failures=0
mkdir -p "$work/bin"
compositor_path=$(cd "$(dirname "$compositor")" && pwd)/$(basename "$compositor")
for name in compositor settings launcher mixer moonwater editor terminal identify; do
        ln -sf "$compositor_path" "$work/bin/$name"
done
selfdir=$(cd "$(dirname "$0")/.." && pwd)
self=$(cd "$(dirname "$0")" && pwd)/$(basename "$0")
export selfdir napps
${CC:-gcc} -std=gnu11 -O2 -Wall -Wextra -o "$work/gpu_scan_sample" "$selfdir/test/gpu_scan_sample.c" || { echo "gpu_scan_sample did not build" >&2; exit 1; }

fonts=$(cd "$selfdir/fonts" && pwd)
unshare -rm sh "$self" session card "$work" "$fonts" > "$work/card.session.out" 2>&1
unshare -rm sh "$self" session cardoff "$work" "$fonts" > "$work/cardoff.session.out" 2>&1
unshare -rm sh "$self" session processor "$work" "$fonts" > "$work/processor.session.out" 2>&1

for mode in card cardoff processor; do
        echo "== $mode"
        cat "$work/$mode.classify.txt" 2> /dev/null || echo "no classification ($(tail -1 "$work/$mode.session.out" 2> /dev/null))"
done
exit "$failures"
