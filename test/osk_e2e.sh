#!/bin/sh
# The on-screen keyboard end to end: the headless compositor (in a namespace of its own, with a private /run and /root), a terminal
# that has the keys (asked for, running /bin/sh), and the keyboard. The keyboard is tapped by a pointer on the compositor's evdev fifo
# (test/osk_input.py), and what the shell does with the taps is the proof: a file the typed command makes, in the terminal's
# directory. The keyboard's own trace (osk -t) gives the keys' rectangles.
#
#     unshare -rm sh test/osk_e2e.sh COMPOSITOR [WORKDIR]
#
# COMPOSITOR is the shell binary, the multi-call one: its applet names (compositor, terminal, osk, moonwater) are links to it. One
# line a row: "ok NAME" or "FAIL NAME ..."; the exit status is the number of failures. The screens are kept in WORKDIR (after.img,
# a 800 by 600 screen of 32-bit pixels, and its PNG is for looking at).

compositor=$1
work=${2:-$(mktemp -d)}
failures=0
self=$(cd "$(dirname "$0")" && pwd)/$(basename "$0")
here=$(cd "$(dirname "$0")" && pwd)
fonts=$(cd "$(dirname "$0")/../fonts" && pwd)

row()
{
        if [ "$1" = ok ]; then echo "ok $2"; else echo "FAIL $2 ${3:-}"; failures=$((failures + 1)); fi
}

# session WORKDIR COMPOSITOR FONTS: the compositor, the terminal and the keyboard; the taps; the screen after them; the debug output before and after
session()
{
        work=$1
        compositor=$2
        fonts=$3
        mount -t tmpfs tmpfs /run || return 1
        mount -t tmpfs tmpfs /root || return 1
        mkdir -p /run/moonwater "$work/bin" "$work/home"
        # the kit's faces are /usr/share/fonts/inter and ibm-plex-mono (ui.c, terminal.c); the repository's are bound over them here
        mount --bind "$fonts" /usr/share/fonts || return 1
        for name in compositor terminal osk moonwater; do
                ln -sf "$compositor" "$work/bin/$name"
        done
        export HOME="$work/home" PATH="$work/bin:/usr/bin:/bin" XDG_RUNTIME_DIR=/run
        cd "$work" || return 1
        mkfifo input.fifo
        bin/compositor -H 800x600 "$work/after.img" -i "$work/input.fifo" > compositor.log 2>&1 &
        cpid=$!
        sleep 2
        # the terminal is asked for, so it has the keys: its shell is /bin/sh, which the keys are typed into
        MOONWATER_ASKED=1 bin/terminal -f /usr/share/fonts/ibm-plex-mono/IBMPlexMono-Regular.ttf -b /usr/share/fonts/ibm-plex-mono/IBMPlexMono-Bold.ttf -x /bin/sh > terminal.log 2>&1 &
        sleep 3
        bin/osk -t "$work/keys.txt" > osk.log 2>&1 &
        sleep 3
        bin/moonwater debug > debug.before.txt 2>&1
        cp after.img before.img
        # the keyboard's keys, typed: a command with a capital (Shift), the numbers page, and a held Back that empties a line
        python3 "$here/osk_input.py" input.fifo keys.txt "text:touch Osk
" "text:touch A" tap:123 tap:2 tap:ABC "text:
" "text:touch Hold" "text:abcdef" hold:Back:3 "text:touch Done
" > input.log 2>&1
        sleep 3
        bin/moonwater debug > debug.after.txt 2>&1
        cp after.img after.shot.img
        kill "$cpid" 2> /dev/null
        wait "$cpid" 2> /dev/null
        return 0
}

if [ "${1:-}" = session ]; then
        session "$2" "$3" "$4"
        exit $?
fi

mkdir -p "$work"
work=$(cd "$work" && pwd)
compositor=$(cd "$(dirname "$compositor")" && pwd)/$(basename "$compositor")
# the session is a namespace of its own: its files (the keyboard's trace, the screens, the logs) are in WORKDIR
unshare -rm sh "$self" session "$work" "$compositor" "$fonts" > "$work/session.out" 2>&1 < /dev/null

# the rows: the keyboard is a window of the desktop, at the bottom of the screen, above the panel and across the middle
if grep -q '^  window [0-9]* *Keyboard at ' "$work/debug.before.txt" 2> /dev/null; then
        row ok "the keyboard is a window of the desktop"
else
        row FAIL "the keyboard is a window of the desktop" "$(grep '^  window' "$work/debug.before.txt" 2> /dev/null | head -3 | tr '\n' ';')"
fi
placed=$(grep -m1 '^  window [0-9]* *Keyboard at ' "$work/debug.before.txt" 2> /dev/null | sed 's/.* at \([0-9-]*\),\([0-9-]*\), \([0-9]*\)x\([0-9]*\)/\1 \2 \3 \4/')
set -- $placed
if [ $# = 4 ] && [ "$(( $1 + ($3 / 2) ))" -eq 400 ] && [ "$(( $2 + $4 ))" -eq 576 ]; then
        row ok "the keyboard is centred across the screen and docked above the panel"
else
        row FAIL "the keyboard is centred across the screen and docked above the panel" "placed: ${placed:-none}"
fi
letters=$(awk '$1 == 0' "$work/keys.txt" 2> /dev/null | wc -l)
if [ "${letters:-0}" -eq 33 ] && [ "$(awk '$1 == 1' "$work/keys.txt" | wc -l)" -gt 0 ] && [ "$(awk '$1 == 2' "$work/keys.txt" | wc -l)" -gt 0 ]; then
        row ok "the keyboard has its keys on every page (the letters page has 33)"
else
        row FAIL "the keyboard has its keys on every page (the letters page has 33)" "letters ${letters:-0}"
fi

# typing: the terminal runs what the keyboard typed, a capital and Enter included, and the taps on the keyboard moved no keys
if [ -f "$work/Osk" ]; then
        row ok "typed on the keyboard: a command with a capital (Shift) and Enter runs in the terminal"
else
        row FAIL "typed on the keyboard: a command with a capital (Shift) and Enter runs in the terminal" "no file Osk in $work ($(ls "$work" | tr '\n' ' '))"
fi
if [ -f "$work/A2" ]; then
        row ok "the numbers page types digits, and ABC goes back to the letters"
else
        row FAIL "the numbers page types digits, and ABC goes back to the letters" "no file A2"
fi
if [ -f "$work/Done" ] && [ ! -f "$work/Holdabcdef" ]; then
        row ok "a held Back repeats until the line is empty"
else
        row FAIL "a held Back repeats until the line is empty" "Done: $([ -f "$work/Done" ] && echo yes || echo no)"
fi

# the screen: drawn, the keyboard and the terminal on it, and the compositor alive after the taps
if [ -s "$work/after.shot.img" ] && [ "$(tr -d '\000' < "$work/after.shot.img" | wc -c)" -gt 0 ]; then
        row ok "the screen after the typing is drawn"
else
        row FAIL "the screen after the typing is drawn" "no screen"
fi
if grep -q '^  window [0-9]* *sh at ' "$work/debug.after.txt" 2> /dev/null && grep -q '^  window [0-9]* *Keyboard at ' "$work/debug.after.txt" 2> /dev/null; then
        row ok "after the typing the terminal and the keyboard are both windows"
else
        row FAIL "after the typing the terminal and the keyboard are both windows" "$(grep '^  window' "$work/debug.after.txt" 2> /dev/null | tr '\n' ';')"
fi
exit "$failures"
