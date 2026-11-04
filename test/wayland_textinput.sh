#!/bin/sh
# The on-screen keyboard and a Wayland text field (zwp_text_input_v3) end to end: the headless compositor, the keyboard (osk), and
# test/wayland_textinput.c, a libwayland window with a text input on its seat. The field's enable and disable show and hide the
# keyboard through the window table's field count, which a kit field counts in the same place; a field that is destroyed, that dies
# while enabled, or that leaves the keyboard gives its count back; the content purpose picks the keyboard's page (the numbers, the
# email page with @ and .com, the letters); the cursor rectangle keeps the window's cursor above the keyboard; the keys the keyboard
# sends reach the field, with Shift for a capital as a person's would; and a hostile request ends its client and nothing else.
#
#     unshare -rm sh test/wayland_textinput.sh COMPOSITOR CLIENT FONTS [WORKDIR]
#
# COMPOSITOR is the shell binary (the applet names compositor, osk, moonwater and terminal are links to it), CLIENT is
# test/wayland_textinput.c built, FONTS the repository's fonts directory. One line a row: "ok NAME" or "FAIL NAME ...", and the
# exit status is the number of failures.

compositor=$1
client=$2
fonts=$(cd "$3" && pwd)
work=${4:-$(mktemp -d)}
here=$(cd "$(dirname "$0")" && pwd)
failures=0
trap '' PIPE

row()
{
        if [ "$1" = ok ]; then echo "ok $2"; else echo "FAIL $2 ${3:-}"; failures=$((failures + 1)); fi
}

# wait_for FILE TEXT SECONDS: TEXT is in FILE within SECONDS (tenths are polled)
wait_for()
{
        n=0
        while [ "$n" -lt "$(($3 * 10))" ]; do
                grep -q "$2" "$1" 2> /dev/null && return 0
                sleep 0.1
                n=$((n + 1))
        done
        return 1
}

# send TEXT: a command to the client (its standard input is the pipe on descriptor 5)
send()
{
        printf '%s\n' "$1" >&5 2> /dev/null || true
}

# start NAME TITLE WIDTH HEIGHT: a client, its commands on NAME.cmd and its output on NAME.log; it is the one descriptor 5 writes to
start()
{
        exec 5>&-
        mkfifo "$work/$1.cmd"
        # MOONWATER_ASKED=1: the person asked for it, so its first window has the keys (a launch from the desktop has it too)
        MOONWATER_ASKED=1 "$client" -t "$2" -W "$3" -H "$4" ${5:-} < "$work/$1.cmd" > "$work/$1.log" 2>&1 &
        eval "pid_$1=\$!"
        exec 5> "$work/$1.cmd"
}

# debug FILE: moonwater debug's reading into FILE
debug()
{
        "$work/bin/moonwater" debug > "$work/$1" 2>&1
}

# the mounts of the namespace: the desktop's /run and /root, no devices of the machine, the repository's fonts over the system's
mount -t tmpfs tmpfs /run || exit 1
mount -t tmpfs tmpfs /root || exit 1
mount -t tmpfs tmpfs /dev/input || exit 1
mount --bind "$fonts" /usr/share/fonts || exit 1
mkdir -p /run/moonwater "$work/bin" "$work/home"
for name in compositor osk moonwater terminal; do
        ln -sf "$compositor" "$work/bin/$name"
done
export HOME="$work/home" PATH="$work/bin:/usr/bin:/bin" XDG_RUNTIME_DIR=/run WAYLAND_DISPLAY=/run/moonwater/wayland-0
printf '%s\n' auto > /root/keyboard.onscreen
cd "$work" || exit 1
mkfifo input.fifo

bin/compositor -H 800x600 "$work/after.img" -i "$work/input.fifo" > compositor.log 2>&1 &
cpid=$!
sleep 2
bin/moonwater keyboard start > start.txt 2>&1
bin/osk -t "$work/keys.txt" > osk.log 2>&1 &
sleep 2

# the field: 320 by 400, put at the top of the screen, where the keyboard (its top at 318) overlaps it and it has no room above the
# keyboard to be moved up into. Its press starts a move (-M), and the pointer drags it 150 down, so the field has the room the
# cursor rows move it up into.
start field Field 320 400 -M
wait_for field.log ready 10
wait_for field.log enter 5
python3 -I "$here/osk_input.py" input.fifo keys.txt drag:100:100:100:250 > keys.drag.log 2>&1
sleep 1
debug debug.drag.txt
dragged=$(python3 -I "$here/osk_debug.py" place debug.drag.txt Field | awk '{ print $2 }')
if [ -n "$dragged" ] && [ "$dragged" -ge 100 ]; then
        row ok "a press that starts a move drags the window down (its top at $dragged, room above the keyboard for the rows below)"
else
        row FAIL "a press that starts a move drags the window down" "its top at ${dragged:-none}; $(tail -2 field.log | tr '\n' ' ')"
fi
if wait_for field.log ready 10; then row ok "the text field's window is drawn"; else row FAIL "the text field's window is drawn" "$(tail -2 field.log)"; fi
wait_for field.log enter 5
debug debug.0.txt
if [ "$(python3 -I "$here/osk_debug.py" keyboard debug.0.txt | cut -d' ' -f1,2)" = "hidden 0" ]; then
        row ok "a window with no text field and no enable: the keyboard is hidden with no field counted"
else
        row FAIL "a window with no text field and no enable: the keyboard is hidden" "$(python3 -I "$here/osk_debug.py" keyboard debug.0.txt)"
fi

# (a) enable and commit on the focused window: the keyboard is shown, the commit is answered by done with its serial
send enable
send commit
wait_for field.log "done 1" 3
sleep 1
debug debug.1.txt
if [ "$(python3 -I "$here/osk_debug.py" keyboard debug.1.txt | cut -d' ' -f1,2)" = "shown 1" ]; then
        row ok "enable on the focused surface shows the keyboard (one field counted)"
else
        row FAIL "enable on the focused surface shows the keyboard" "debug: $(python3 -I "$here/osk_debug.py" keyboard debug.1.txt)"
fi
if grep -q "^done 1$" field.log; then
        row ok "a commit is answered with done and its serial"
else
        row FAIL "a commit is answered with done and its serial" "$(tr '\n' ' ' < field.log)"
fi

# the keys the keyboard sends reach the field: a capital (Shift held for its letter, as a person's), then a small letter
python3 -I "$here/osk_input.py" input.fifo keys.txt text:Ab > keys.input.log 2>&1
sleep 1
if grep -q "^key 42 1 0$" field.log && grep -q "^key 30 1 1$" field.log && grep -q "^key 30 0 1$" field.log; then
        row ok "a capital typed on the keyboard reaches the field as Shift and its letter"
else
        row FAIL "a capital typed on the keyboard reaches the field as Shift and its letter" "$(grep '^key' field.log | head -8 | tr '\n' ' ')"
fi
if grep -q "^key 48 1 0$" field.log; then
        row ok "a small letter typed on the keyboard reaches the field as its key"
else
        row FAIL "a small letter typed on the keyboard reaches the field as its key" "$(grep '^key' field.log | tail -4 | tr '\n' ' ')"
fi

# (a) disable and commit: the keyboard is hidden and the count is given back
send disable
send commit
sleep 1
debug debug.2.txt
if [ "$(python3 -I "$here/osk_debug.py" keyboard debug.2.txt | cut -d' ' -f1,2)" = "hidden 0" ]; then
        row ok "disable on the focused surface hides the keyboard (its count is given back)"
else
        row FAIL "disable on the focused surface hides the keyboard" "debug: $(python3 -I "$here/osk_debug.py" keyboard debug.2.txt)"
fi

# (e) enable twice counts once, and a disable without an enable counts nothing: the counts stay sane (none below zero)
send enable
send commit
send enable
send commit
sleep 1
debug debug.3.txt
counted=$(python3 -I "$here/osk_debug.py" keyboard debug.3.txt | cut -d' ' -f2)
send disable
send commit
send disable
send commit
sleep 1
debug debug.4.txt
if [ "$counted" = 1 ] && [ "$(python3 -I "$here/osk_debug.py" keyboard debug.4.txt | cut -d' ' -f1,2)" = "hidden 0" ]; then
        row ok "enable twice counts one field, and disable twice (or without an enable) leaves none"
else
        row FAIL "enable twice counts one field, and disable without an enable leaves none" "counted $counted; debug: $(python3 -I "$here/osk_debug.py" keyboard debug.4.txt)"
fi

# (c) the content purpose: a number opens the numbers page (its '1' key reaches the field as 1), and the debug line says so
send "purpose 3"
send enable
send commit
sleep 1
debug debug.5.txt
python3 -I "$here/osk_input.py" input.fifo keys.txt cell:1:0:0 > keys.cell.log 2>&1
sleep 1
if [ "$(python3 -I "$here/osk_debug.py" text debug.5.txt)" = "3 0" ] || [ "$(python3 -I "$here/osk_debug.py" text debug.5.txt | cut -d' ' -f1)" = 3 ]; then
        row ok "the focus's text field says its content purpose (a number) in moonwater debug"
else
        row FAIL "the focus's text field says its content purpose in moonwater debug" "debug: $(python3 -I "$here/osk_debug.py" text debug.5.txt)"
fi
if grep -q "^key 2 1 0$" field.log; then
        row ok "content type number selects the numbers page: its '1' key reaches the field"
else
        row FAIL "content type number selects the numbers page" "$(grep '^key' field.log | tail -3 | tr '\n' ' ')"
fi

# (c) an email field: the symbols page with @ (Shift and 2) and .com, a press of each of its keys
send "purpose 6"
send commit
sleep 1
python3 -I "$here/osk_input.py" input.fifo keys.txt cell:3:1:0 > keys.at.log 2>&1
sleep 1
if grep -q "^key 3 1 1$" field.log; then
        row ok "content type email selects the symbols page: @ reaches the field as Shift and 2"
else
        row FAIL "content type email selects the symbols page: @ reaches the field as Shift and 2" "$(grep '^key' field.log | tail -3 | tr '\n' ' ')"
fi
python3 -I "$here/osk_input.py" input.fifo keys.txt cell:3:1:9 > keys.com.log 2>&1
sleep 1
if [ "$(grep '^key' field.log | tail -8 | tr '\n' ' ')" = "key 52 1 0 key 52 0 0 key 46 1 0 key 46 0 0 key 24 1 0 key 24 0 0 key 50 1 0 key 50 0 0 " ]; then
        row ok ".com is one key that types dot, c, o and m"
else
        row FAIL ".com is one key that types dot, c, o and m" "$(grep '^key' field.log | tail -8 | tr '\n' ' ')"
fi

# (c) a password: the letters page, the keyboard still shown (no suggestions to show)
send "purpose 8"
send commit
sleep 1
python3 -I "$here/osk_input.py" input.fifo keys.txt cell:0:0:0 > keys.pw.log 2>&1
sleep 1
debug debug.6.txt
if grep -q "^key 16 1 0$" field.log && [ "$(python3 -I "$here/osk_debug.py" keyboard debug.6.txt | cut -d' ' -f1,2)" = "shown 1" ]; then
        row ok "a password field shows the letters page, and the keyboard stays shown"
else
        row FAIL "a password field shows the letters page" "debug: $(python3 -I "$here/osk_debug.py" keyboard debug.6.txt)"
fi
send "purpose 0"
send commit

# (d) the cursor rectangle near the bottom of the window (its bottom at 390 of 400) moves the window up for the keyboard. The keyboard
# is hidden first, so the window is where it was before any keyboard moved it; then the cursor is set and the field is enabled.
send disable
send commit
sleep 1.5
debug debug.7.txt
before=$(python3 -I "$here/osk_debug.py" place debug.7.txt Field | awk '{ print $2 }')
send "caret 10 360 10 30"
send enable
send commit
sleep 1.5
debug debug.8.txt
kb=$(python3 -I "$here/osk_debug.py" place debug.8.txt Keyboard | awk '{ print $2 }')
after=$(python3 -I "$here/osk_debug.py" place debug.8.txt Field | awk '{ print $2 }')
text=$(python3 -I "$here/osk_debug.py" text debug.8.txt)
if [ -n "$before" ] && [ -n "$after" ] && [ -n "$kb" ] && [ "$after" -lt "$before" ] && { [ "$((after + 390))" -le "$kb" ] || [ "$after" -le 1 ]; }; then
        row ok "a cursor near the window's bottom moves the window up, its cursor above the keyboard (from $before to $after, keyboard at $kb)"
else
        row FAIL "a cursor near the window's bottom moves the window up for the keyboard" "window $before then ${after:-none}, keyboard at ${kb:-none}, text $text"
fi
if [ "$text" = "0 390" ]; then
        row ok "moonwater debug says the cursor's bottom (390) for the field"
else
        row FAIL "moonwater debug says the cursor's bottom (390) for the field" "debug: $text"
fi

# (d) a cursor above the keyboard leaves the window where it was (its own bottom overlaps the keyboard, the cursor's does not)
send "caret 10 10 10 20"
send commit
sleep 1.5
debug debug.9.txt
back=$(python3 -I "$here/osk_debug.py" place debug.9.txt Field | awk '{ print $2 }')
if [ "$back" = "$before" ]; then
        row ok "a cursor above the keyboard leaves the window where it was ($back)"
else
        row FAIL "a cursor above the keyboard leaves the window where it was" "was $before, now ${back:-none}"
fi

# (f) a destroyed text input gives its count back, and a new one with the keys counts again
send "caret 10 360 10 30"
send commit
send destroy
sleep 1
debug debug.10.txt
if [ "$(python3 -I "$here/osk_debug.py" keyboard debug.10.txt | cut -d' ' -f1,2)" = "hidden 0" ]; then
        row ok "destroying the text input gives its count back (the keyboard is hidden)"
else
        row FAIL "destroying the text input gives its count back" "debug: $(python3 -I "$here/osk_debug.py" keyboard debug.10.txt)"
fi
send new
send enable
send commit
sleep 1
debug debug.11.txt
if [ "$(python3 -I "$here/osk_debug.py" keyboard debug.11.txt | cut -d' ' -f1,2)" = "shown 1" ]; then
        row ok "a new text input with the keys counts again: the keyboard is shown"
else
        row FAIL "a new text input with the keys counts again" "debug: $(python3 -I "$here/osk_debug.py" keyboard debug.11.txt)"
fi

# leaving: a terminal, which has no field, takes the keys: the field is told leave, its count goes, and the keyboard is hidden
MOONWATER_ASKED=1 bin/terminal -f /usr/share/fonts/ibm-plex-mono/IBMPlexMono-Regular.ttf -b /usr/share/fonts/ibm-plex-mono/IBMPlexMono-Bold.ttf -x /bin/sh > terminal.log 2>&1 &
sleep 3
debug debug.12.txt
if [ "$(python3 -I "$here/osk_debug.py" keyboard debug.12.txt | cut -d' ' -f1,2)" = "hidden 0" ] && grep -q "^leave$" field.log; then
        row ok "the window that loses the keys is told leave, its count goes, and the keyboard is hidden"
else
        row FAIL "the window that loses the keys is told leave and its count goes" "debug: $(python3 -I "$here/osk_debug.py" keyboard debug.12.txt), log: $(tail -2 field.log | tr '\n' ' ')"
fi
exec 5>&-

# (b) a client that dies while enabled: its count goes, and the keyboard hides (the terminal keeps the keys, so the dying window
# takes them first)
start dying Dying 320 240
wait_for dying.log ready 10
wait_for dying.log enter 5
send_dying() { printf '%s\n' "$1" >&5 2> /dev/null || true; }
send_dying enable
send_dying commit
sleep 1
debug debug.13.txt
shown=$(python3 -I "$here/osk_debug.py" keyboard debug.13.txt | cut -d' ' -f1,2)
kill -9 "$pid_dying" 2> /dev/null
wait "$pid_dying" 2> /dev/null
exec 5>&-
sleep 1
debug debug.14.txt
if [ "$shown" = "shown 1" ] && [ "$(python3 -I "$here/osk_debug.py" keyboard debug.14.txt | cut -d' ' -f1,2)" = "hidden 0" ]; then
        row ok "a client that dies while enabled releases its count: the keyboard hides"
else
        row FAIL "a client that dies while enabled releases its count" "before $shown; debug: $(python3 -I "$here/osk_debug.py" keyboard debug.14.txt)"
fi

# (e) hostile requests: huge cursor numbers and an unknown purpose (the field stays counted once, the numbers are none), a
# disable without an enable, then a request to an object that is not there, which ends the client with a protocol error
start hostile Hostile 320 240
wait_for hostile.log ready 10
wait_for hostile.log enter 5
send_hostile() { printf '%s\n' "$1" >&5 2> /dev/null || true; }
send_hostile "purpose 99"
send_hostile "caret 0 2147483647 0 2147483647"
send_hostile commit
send_hostile "caret -2147483648 -2147483648 2147483647 2147483647"
send_hostile commit
send_hostile "caret -5 -5 -5 -5"
send_hostile commit
send_hostile disable
send_hostile commit
send_hostile enable
send_hostile commit
send_hostile sync
sleep 1
debug debug.15.txt
if grep -q "^sync$" hostile.log && [ "$(python3 -I "$here/osk_debug.py" keyboard debug.15.txt | cut -d' ' -f1,2)" = "shown 1" ] && \
   [ "$(python3 -I "$here/osk_debug.py" text debug.15.txt)" = "0 0" ]; then
        row ok "hostile numbers: a huge or negative cursor is none, an unknown purpose is normal, and the client still answers"
else
        row FAIL "hostile numbers: a huge or negative cursor is none, an unknown purpose is normal, and the client still answers" \
                "log $(tail -2 hostile.log | tr '\n' ' '); debug: $(python3 -I "$here/osk_debug.py" keyboard debug.15.txt) $(python3 -I "$here/osk_debug.py" text debug.15.txt)"
fi
send_hostile "raw 4242 0 1"
sleep 1
exec 5>&-
debug debug.16.txt
if grep -q "^error$" hostile.log && [ "$(python3 -I "$here/osk_debug.py" keyboard debug.16.txt | cut -d' ' -f1,2)" = "hidden 0" ]; then
        row ok "a request to an object that is not there ends its client, and its count goes with it"
else
        row FAIL "a request to an object that is not there ends its client" "log $(tail -2 hostile.log | tr '\n' ' '); debug: $(python3 -I "$here/osk_debug.py" keyboard debug.16.txt)"
fi

# (f) the compositor is alive after all of it, and answers moonwater debug
if kill -0 "$cpid" 2> /dev/null && debug debug.final.txt && grep -q "^  keyboard" debug.final.txt; then
        row ok "the compositor stays alive after all of these rows, and answers moonwater debug"
else
        row FAIL "the compositor stays alive after all of these rows" "$(tail -3 compositor.log)"
fi

kill "$cpid" 2> /dev/null
wait "$cpid" 2> /dev/null
exit "$failures"
