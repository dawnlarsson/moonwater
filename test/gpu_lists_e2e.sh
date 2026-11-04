#!/bin/sh
# The apps' draw lists on the card, end to end: the headless compositor composes on the card (-G) with the kit applets started the same way in two
# sessions, once with canvas gpu on (the apps are in list state 1) and once with it off (the apps paint pixels); the two screens compared. In the
# card's session, canvas gpu is turned off and on while the apps run, and an applet is killed mid-publish.
#
#     unshare -rm sh test/gpu_lists_e2e.sh COMPOSITOR [WORKDIR]
#
# COMPOSITOR is the shell binary, the multi-call one: its applet names (settings, launcher, mixer, moonwater, compositor) are links to it. Every
# session is a namespace of its own with a private /run and /root. One line a row: "ok NAME" or "FAIL NAME ..."; the exit status is the number of
# failures. The compositor's option order is its own: -H WxH FILE takes the image as its next argument, so -G comes after the image.

# settle IMG: wait until the screen file stops changing (two equal checksums a second apart), at most 40 seconds
settle()
{
        # the apps' first paints come after their windows: four equal readings in a row, a second apart, and at least five seconds
        last=
        same=0
        i=0
        sleep 5
        while [ $i -lt 40 ]; do
                sum=$(cksum < "$1" 2> /dev/null)
                if [ -n "$sum" ] && [ "$sum" = "$last" ]; then
                        same=$((same + 1))
                        [ $same -ge 4 ] && return 0
                else
                        same=0
                fi
                last=$sum
                sleep 1
                i=$((i + 1))
        done
        return 0
}

# session MODE WORKDIR: one headless compositor, the three applets, the screen as it settles; the card's session also toggles and kills
session()
{
        mode=$1
        work=$2
        fonts=$3
        mount -t tmpfs tmpfs /run
        mount -t tmpfs tmpfs /root
        # the kit's faces are /usr/share/fonts/inter and ibm-plex-mono (ui.c); a machine without them loads none, and an app with no font exits. The
        # repository's fonts are bound over the directory in this namespace only.
        mount --bind "$fonts" /usr/share/fonts || return 1
        mkdir -p /run/moonwater
        cd "$work" || return 1
        if [ "$mode" = off ]; then
                echo off > /root/canvas.gpu
        else
                : > /run/moonwater/lists # the lists are off without it (the compositor looks for it)
        fi
        # the input: the evdev fifo (-i), written by the flicker detector as a device would write it
        mkfifo "$work/input.fifo"
        bin/compositor -H 800x600 "$work/$mode.img" -G -i "$work/input.fifo" > "$work/$mode.compositor.log" 2>&1 &
        cpid=$!
        sleep 2
        # the same three applets, the same way, in the same order
        # APPS: the applets, started in this order (default settings and mixer: the launcher's popup comes and goes at a time the session does not
        # set, so it is not in the strict comparison; LAUNCHER=1 adds it, as its own comparison)
        napps=0
        for app in $APPS; do
                bin/$app > "$work/$mode.$app.log" 2>&1 &
                lpid=$!
                napps=$((napps + 1))
        done
        i=0
        while [ $i -lt 60 ] && [ "$(bin/moonwater debug 2> /dev/null | grep -c '^  window')" -lt "$napps" ]; do
                sleep 0.5
                i=$((i + 1))
        done
        settle "$work/$mode.img"
        bin/moonwater debug > "$work/$mode.canvas.txt" 2>&1
        cp "$work/$mode.img" "$work/$mode.settled.img"
        if [ "$mode" = card ]; then
                # canvas gpu off, then on, while the apps run: each repaints in the new mode, and the screen is the one before
                bin/moonwater canvas gpu off > /dev/null 2>&1
                settle "$work/$mode.img"
                cp "$work/$mode.img" "$work/toggle-off.img"
                bin/moonwater canvas gpu on > /dev/null 2>&1
                settle "$work/$mode.img"
                cp "$work/$mode.img" "$work/toggle-on.img"
                # the last applet killed while the compositor is publishing a frame (the setting is turned off at the same moment)
                bin/moonwater canvas gpu off > /dev/null 2>&1 &
                togpid=$!
                kill -9 "$lpid" 2> /dev/null
                wait "$togpid"
                bin/moonwater canvas gpu on > /dev/null 2>&1
                settle "$work/$mode.img"
                cp "$work/$mode.img" "$work/killed.img"
                if kill -0 "$cpid" 2> /dev/null; then echo alive > "$work/killed.alive"; else echo dead > "$work/killed.alive"; fi
                bin/moonwater debug > "$work/killed.canvas.txt" 2>&1
        fi
        # the flicker detector, after the screens above (it leaves the filter focused and its caret blinking): input over the fifo (hover, click,
        # wheel, typing), every distinct screen kept in order; the card session with its lists on and the off session (the processor paints) are the same input and the same reference
        python3 "$(dirname "$fonts")/test/gpu_lists_flicker.py" "$work/$mode.img" "$work/input.fifo" "$work/flicker-$mode" > "$work/flicker-$mode.txt" 2>&1
        bin/moonwater debug > "$work/$mode.touched.txt" 2>&1 # the lists drawn, refused and switched back to pixels under that input
        kill "$cpid" 2> /dev/null
        wait "$cpid" 2> /dev/null
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
napps_parent=$(echo $APPS | wc -w)
failures=0
mkdir -p "$work/bin"
compositor_path=$(cd "$(dirname "$compositor")" && pwd)/$(basename "$compositor")
for name in compositor settings launcher mixer moonwater; do
        ln -sf "$compositor_path" "$work/bin/$name"
done
self=$(cd "$(dirname "$0")" && pwd)/$(basename "$0")

row()
{
        if [ "$1" = ok ]; then echo "ok $2"; else echo "FAIL $2 ${3:-}"; failures=$((failures + 1)); fi
}

fonts=$(cd "$(dirname "$0")/../fonts" && pwd)
unshare -rm sh "$self" session card "$work" "$fonts" > "$work/card.session.out" 2>&1
unshare -rm sh "$self" session off "$work" "$fonts" > "$work/off.session.out" 2>&1

lists_card=$(grep -m1 '^  lists .*windows' "$work/card.canvas.txt" 2> /dev/null | sed 's/^  lists *\([0-9]*\) windows.*/\1/')
refused_card=$(grep -m1 '^  lists .*windows' "$work/card.canvas.txt" 2> /dev/null | sed 's/.*, \([0-9]*\) refused.*/\1/')
lists_off=$(grep -m1 '^  lists .*windows' "$work/off.canvas.txt" 2> /dev/null | sed 's/^  lists *\([0-9]*\) windows.*/\1/')
windows_card=$(grep -c '^  window' "$work/card.canvas.txt" 2> /dev/null)

if [ "${windows_card:-0}" -ge "$napps_parent" ]; then row ok "the applets are windows of the desktop with the card"; else row FAIL "the applets are windows of the desktop with the card" "$windows_card windows ($(tail -1 "$work/card.session.out" 2> /dev/null))"; fi
if [ "${lists_card:-0}" -ge "$napps_parent" ]; then row ok "the apps are in list state 1 while the card composes"; else row FAIL "the apps are in list state 1 while the card composes" "lists ${lists_card:-none}"; fi
if [ "${refused_card:-1}" = 0 ]; then row ok "the compositor refused no list"; else row FAIL "the compositor refused no list" "refused ${refused_card:-none}"; fi
touched_refused=$(grep -m1 '^  lists .*windows' "$work/card.touched.txt" 2> /dev/null | sed 's/.*, \([0-9]*\) refused.*/\1/')
touched_switched=$(grep -m1 '^  list switched' "$work/card.touched.txt" 2> /dev/null | sed 's/^  list switched *\([0-9]*\),.*/\1/')
if [ "${touched_refused:-1}" = 0 ] && [ "${touched_switched:-0}" = 0 ]; then row ok "hover, click, wheel and typing: no list refused, no window switched between its list and its pixels"; else row FAIL "hover, click, wheel and typing: no list refused, no window switched between its list and its pixels" "refused ${touched_refused:-none}, switched ${touched_switched:-0} ($(grep '^  list' "$work/card.touched.txt" 2> /dev/null | tr -s ' ' | tr '\n' ';'))"; fi
if [ "${lists_off:-1}" = 0 ]; then row ok "with canvas gpu off the apps paint pixels (no list)"; else row FAIL "with canvas gpu off the apps paint pixels (no list)" "lists ${lists_off:-none}"; fi

# the two screens: the same pixels; a difference is written out as PPM for the report
diff_count=$(cmp -l "$work/card.settled.img" "$work/off.settled.img" 2> /dev/null | wc -l)
if [ "${diff_count:-1}" = 0 ]; then
        row ok "the card's screen and the processor's are the same pixels"
else
        row FAIL "the card's screen and the processor's are the same pixels" "$diff_count bytes differ (see $work/card.ppm, $work/off.ppm)"
        python3 - "$work/card.settled.img" "$work/card.ppm" "$work/off.settled.img" "$work/off.ppm" 2> /dev/null <<'PY'
import sys
def ppm(src, out):
    data = open(src, 'rb').read()
    open(out, 'wb').write(b'P6\n800 600\n255\n' + bytes(b for i in range(0, len(data), 4) for b in (data[i + 2], data[i + 1], data[i])))
ppm(sys.argv[1], sys.argv[2])
ppm(sys.argv[3], sys.argv[4])
PY
fi

# the toggle: each screen after the setting changed is the screen before (the apps repainted in the mode they are in)
if [ -s "$work/card.settled.img" ] && [ -s "$work/toggle-off.img" ] && [ -s "$work/toggle-on.img" ]; then
        changed_off=$(cmp -l "$work/card.settled.img" "$work/toggle-off.img" 2> /dev/null | wc -l)
        changed_on=$(cmp -l "$work/card.settled.img" "$work/toggle-on.img" 2> /dev/null | wc -l)
        if [ "$changed_off" -le 64 ]; then row ok "canvas gpu off while the apps run: they repaint the same screen"; else row FAIL "canvas gpu off while the apps run: they repaint the same screen" "$changed_off bytes changed"; fi
        if [ "$changed_on" -le 64 ]; then row ok "canvas gpu on again: the same screen"; else row FAIL "canvas gpu on again: the same screen" "$changed_on bytes changed"; fi
else
        row FAIL "the toggle produced its screens" "$(tail -1 "$work/card.session.out" 2> /dev/null)"
fi

# the kill: the compositor is alive, and the screen after it is drawn (not blank)
if [ "$(cat "$work/killed.alive" 2> /dev/null)" = alive ]; then row ok "an applet killed mid-publish: the compositor lives on"; else row FAIL "an applet killed mid-publish: the compositor lives on" "$(cat "$work/killed.alive" 2> /dev/null || echo no-record)"; fi
if [ -s "$work/killed.img" ] && [ "$(tr -d '\000' < "$work/killed.img" | wc -c)" -gt 0 ]; then row ok "the screen after the kill is drawn"; else row FAIL "the screen after the kill is drawn" "no screen"; fi

# the reasons the card was refused, and the GPU's counts, from moonwater canvas in each session
grep '^  gpu' "$work/card.canvas.txt" 2> /dev/null | sed 's/^/card: /'
grep '^  gpu' "$work/off.canvas.txt" 2> /dev/null | sed 's/^/off:  /'
exit "$failures"
