#!/bin/sh
# The on-screen keyboard end to end: the headless compositor (in a namespace of its own, with a private /run, /root and /dev/input), an editor
# on a file (a kit text field, which has the keys), and the keyboard. The setting (/root/keyboard.onscreen) is auto for the first
# session. The editor's field counts itself (ui_field_focus), so the compositor shows the keyboard and moves the editor up by the
# overlap; the keyboard is tapped by a pointer on the compositor's evdev fifo (test/osk_input.py); the taps are typed into the
# field, Ctrl+S (also on the fifo) saves the file, and the file is the proof. Then a terminal, which has no field, takes the focus:
# the keyboard goes away and the editor comes back down. The second session has the setting off: the keyboard stays away.
# The keyboard's own trace (osk -t) gives the keys' rectangles, and moonwater debug says what the compositor did.
#
#     unshare -rm sh test/osk_e2e.sh COMPOSITOR [WORKDIR]
#
# COMPOSITOR is the shell binary, the multi-call one: its applet names (compositor, editor, terminal, osk, moonwater) are links to it.
# One line a row: "ok NAME" or "FAIL NAME ..."; the exit status is the number of failures. The screens are kept in WORKDIR (after.img,
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

# session WORKDIR COMPOSITOR FONTS SETTING: the compositor, the editor on note.txt (asked for, so it has the keys), the keyboard; with
# the setting auto, the taps, Ctrl+S, and a terminal that takes the focus; the debug output at each step
session()
{
        work=$1
        compositor=$2
        fonts=$3
        mount -t tmpfs tmpfs /run || return 1
        mount -t tmpfs tmpfs /root || return 1
        # no host devices: the compositor takes every device in /dev/input, and a physical keyboard on the machine would be one (the
        # fifo is the only input here, and it is no keyboard)
        mount -t tmpfs tmpfs /dev/input || return 1
        printf '%s\n' "$4" > /root/keyboard.onscreen
        mkdir -p /run/moonwater "$work/bin" "$work/home"
        # the kit's faces are /usr/share/fonts/inter and ibm-plex-mono (ui.c, terminal.c); the repository's are bound over them here
        mount --bind "$fonts" /usr/share/fonts || return 1
        for name in compositor terminal editor osk moonwater; do
                ln -sf "$compositor" "$work/bin/$name"
        done
        export HOME="$work/home" PATH="$work/bin:/usr/bin:/bin" XDG_RUNTIME_DIR=/run
        cd "$work" || return 1
        mkfifo input.fifo
        bin/compositor -H 800x600 "$work/after.img" -i "$work/input.fifo" > compositor.log 2>&1 &
        cpid=$!
        sleep 2
        MOONWATER_ASKED=1 bin/editor note.txt > editor.log 2>&1 &
        sleep 3
        bin/moonwater debug > debug.editor.txt 2>&1
        case "${5:-real}" in
        copy)
                # the same program from another file: a copy of the executable, which is not the keyboard's file
                mkdir -p "$work/other"
                cp "$compositor" "$work/other/osk"
                "$work/other/osk" -t "$work/keys.txt" > osk.log 2>&1 &
                ;;
        renamed)
                # the same file, but a process named keyb (its comm), with the applet's name for its arguments
                ln -sf "$compositor" "$work/bin/keyb"
                python3 -I -c 'import os, sys; os.execv(sys.argv[1], ["osk", "-t", sys.argv[2]])' "$work/bin/keyb" "$work/keys.txt" > osk.log 2>&1 &
                ;;
        *)
                bin/osk -t "$work/keys.txt" > osk.log 2>&1 &
                ;;
        esac
        sleep 3
        bin/moonwater debug > debug.shown.txt 2>&1
        cp after.img before.img
        if [ "$4" = auto ] && [ "${5:-real}" = real ]; then
                # the keyboard's keys, typed into the field: a held Back empties the first line, a capital (Shift), the numbers page,
                # Enter, then Ctrl+S saves the file
                python3 "$here/osk_input.py" input.fifo keys.txt "text:abcdef" hold:Back:2 "text:Osk
" tap:123 tap:2 tap:ABC "text:
Done" ctrl:s > input.log 2>&1
                sleep 2
                bin/moonwater debug > debug.typed.txt 2>&1
                # a terminal has no field: it takes the focus, and the keyboard goes away with the editor's place back
                MOONWATER_ASKED=1 bin/terminal -f /usr/share/fonts/ibm-plex-mono/IBMPlexMono-Regular.ttf -b /usr/share/fonts/ibm-plex-mono/IBMPlexMono-Bold.ttf -x /bin/sh > terminal.log 2>&1 &
                sleep 3
                bin/moonwater debug > debug.terminal.txt 2>&1
        fi
        cp after.img after.shot.img
        kill "$cpid" 2> /dev/null
        wait "$cpid" 2> /dev/null
        return 0
}

if [ "${1:-}" = session ]; then
        session "$2" "$3" "$4" "$5" "${6:-}" "${7:-}" "${8:-}"
        exit $?
fi

mkdir -p "$work"
work=$(cd "$work" && pwd)
compositor=$(cd "$(dirname "$compositor")" && pwd)/$(basename "$compositor")
# each session is a namespace of its own: its files (the keyboard's trace, the screens, the logs) are in WORKDIR, or WORKDIR/off
unshare -rm sh "$self" session "$work" "$compositor" "$fonts" auto > "$work/session.out" 2>&1 < /dev/null
mkdir -p "$work/off"
unshare -rm sh "$self" session "$work/off" "$compositor" "$fonts" off > "$work/off/session.out" 2>&1 < /dev/null
# the impostors: a copy of the keyboard's file, and the same file run under another name, each started where the keyboard is
mkdir -p "$work/copy" "$work/renamed"
unshare -rm sh "$self" session "$work/copy" "$compositor" "$fonts" auto copy > "$work/copy/session.out" 2>&1 < /dev/null
unshare -rm sh "$self" session "$work/renamed" "$compositor" "$fonts" auto renamed > "$work/renamed/session.out" 2>&1 < /dev/null

# the keyboard is a window of the desktop, at the bottom of the screen, above the panel and across the middle
if grep -q '^  window [0-9]* *Keyboard at ' "$work/debug.shown.txt" 2> /dev/null; then
        row ok "the keyboard is a window of the desktop"
else
        row FAIL "the keyboard is a window of the desktop" "$(grep '^  window' "$work/debug.shown.txt" 2> /dev/null | head -3 | tr '\n' ';')"
fi
placed=$(python3 -I "$here/osk_debug.py" place "$work/debug.shown.txt" Keyboard)
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

# the setting auto and a text field with the keys: the keyboard is shown, and the field's window is moved up by the overlap with it
shown=$(python3 -I "$here/osk_debug.py" keyboard "$work/debug.shown.txt")
if [ "$shown" = "shown 1 a_touch_or_a_pen none" ] || [ "$shown" = "shown 1 the_mouse none" ]; then
        row ok "with the setting auto and an editor's field focused, the keyboard is shown (the debug line says so, one field has the keys)"
else
        row FAIL "with the setting auto and an editor's field focused, the keyboard is shown" "debug: $shown"
fi
before=$(python3 -I "$here/osk_debug.py" place "$work/debug.editor.txt" Editor)
after=$(python3 -I "$here/osk_debug.py" place "$work/debug.shown.txt" Editor)
set -- $before
ex=$1 ey=$2 ew=$3 eh=$4
set -- $placed
kx=$1 ky=$2
set -- $after
ay=$2
# the editor's bottom goes past the keyboard's top, so it moves up by the overlap: to where it clears the keyboard, or as high as the screen
# lets it (the top edge, a pixel down at most, when the editor is taller than the room above the keyboard)
if [ -n "$ey" ] && [ "$ey" -gt "$((ky - eh))" ] && [ "$ay" -lt "$ey" ] && { [ "$((ay + eh))" -le "$ky" ] || [ "$ay" -le 1 ]; }; then
        row ok "the editor is moved up by the overlap with the keyboard (from $ey to $ay, the keyboard at $ky)"
elif [ -n "$ey" ] && [ "$ey" -le "$((ky - eh))" ] && [ "$ay" -eq "$ey" ]; then
        row ok "the editor clears the keyboard, so it is not moved"
else
        row FAIL "the editor is moved up by the overlap with the keyboard" "editor at $ey, height $eh, after $ay, keyboard at $ky (before ${before:-none}, after ${after:-none})"
fi

# a tap on the keyboard keeps the focus: the taps typed into the field and Ctrl+S saved the file (a capital, the numbers page, Enter,
# and a held Back that empties the first line, which had abcdef on it)
if [ "$(cat "$work/note.txt" 2> /dev/null)" = "$(printf 'Osk\n2\nDone')" ]; then
        row ok "typed on the keyboard into the editor's field and saved with Ctrl+S: a capital (Shift), the numbers page, Enter, and a held Back that emptied the line"
else
        row FAIL "typed on the keyboard into the editor's field and saved with Ctrl+S" "note.txt: $(cat "$work/note.txt" 2> /dev/null | tr '\n' '|')"
fi

# a terminal, which has no field, takes the focus: the keyboard goes away, and the editor is moved back to where it was
terminal=$(python3 -I "$here/osk_debug.py" keyboard "$work/debug.terminal.txt")
case "$terminal" in
hidden\ 0\ *) row ok "a terminal, with no text field, takes the focus and the keyboard is hidden" ;;
*) row FAIL "a terminal, with no text field, takes the focus and the keyboard is hidden" "debug: $terminal" ;;
esac
back=$(python3 -I "$here/osk_debug.py" place "$work/debug.terminal.txt" Editor)
if [ "$back" = "$ex $ey $ew $eh" ]; then
        row ok "the editor is moved back to where it was when the keyboard is hidden"
else
        row FAIL "the editor is moved back to where it was when the keyboard is hidden" "editor $ex $ey was, now ${back:-none}"
fi

# the setting off: a text field does not show the keyboard, whatever the field
off=$(python3 -I "$here/osk_debug.py" keyboard "$work/off/debug.shown.txt")
case "$off" in
hidden\ 1\ *) row ok "with the setting off, an editor's field with the keys does not show the keyboard" ;;
*) row FAIL "with the setting off, an editor's field with the keys does not show the keyboard" "debug: $off" ;;
esac

# the screen: drawn after the typing, and the compositor alive after the taps
if [ -s "$work/after.shot.img" ] && [ "$(tr -d '\000' < "$work/after.shot.img" | wc -c)" -gt 0 ]; then
        row ok "the screen after the typing is drawn"
else
        row FAIL "the screen after the typing is drawn" "no screen"
fi
# a program that is not the keyboard's executable or is not named osk has no keyboard role: its flags are dropped, so it is no keyboard,
# and with the setting auto and a field focused the keyboard is not shown for it
for who in copy renamed; do
        line=$(python3 -I "$here/osk_debug.py" keyboard "$work/$who/debug.shown.txt")
        case "$line" in
        hidden\ 1\ *) row ok "a program that asks for the keyboard's flags from $( [ "$who" = copy ] && echo "a copy of the executable" || echo "the same executable under another name") has no keyboard role (no keyboard is shown)" ;;
        *) row FAIL "a program that asks for the keyboard's flags from $who has no keyboard role" "debug: $line" ;;
        esac
done
exit "$failures"
