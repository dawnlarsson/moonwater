#!/bin/sh
# The band's launcher button end to end: the headless compositor (in a namespace of its own, with a private /run, /root and
# /dev/input) says launcher_button down the machine script's FIFO (/run/moonwater/keys.fire) when the button is pressed, and this
# answers it as main.moonwater.sh's desktop_keys_answer does: the launcher, -x -a, started beside the button. The button is pressed
# by a pointer on the compositor's evdev fifo, one writer for the whole session and one count a report (no acceleration); moonwater
# debug says what windows there are and where. A second press on the button puts the launcher away, as the compositor does with the
# launcher up.
#
#     unshare -rm sh test/desktop_e2e.sh COMPOSITOR [WORKDIR]
#
# COMPOSITOR is the shell binary, the multi-call one: its applet names (compositor, launcher, moonwater) are links to it. One line a
# row: "ok NAME" or "FAIL NAME ..."; the exit status is the number of failures. The screens are kept in WORKDIR (launcher.up.img, the
# launcher up, and launcher.img at the end: 1280 by 800 screens of 32-bit pixels). DESKTOP_NO_ANSWER=1 leaves the FIFO unanswered,
# which is what the headless harness was before this: the button is said and nothing starts, and the rows after the first are red.

compositor=$1
work=${2:-$(mktemp -d)}
failures=0
self=$(cd "$(dirname "$0")" && pwd)/$(basename "$0")
fonts=$(cd "$(dirname "$0")/../fonts" && pwd)

row()
{
        if [ "$1" = ok ]; then echo "ok $2"; else echo "FAIL $2 ${3:-}"; failures=$((failures + 1)); fi
}

session()
{
        work=$1
        compositor=$2
        fonts=$3
        mount -t tmpfs tmpfs /run || return 1
        mount -t tmpfs tmpfs /root || return 1
        # no host devices: the fifo is the only input here
        [ ! -d /dev/input ] || mount -t tmpfs tmpfs /dev/input || return 1
        mount --bind "$fonts" /usr/share/fonts || return 1
        mkdir -p /run/moonwater "$work/bin" "$work/home"
        for name in compositor launcher moonwater shell; do
                ln -sf "$compositor" "$work/bin/$name"
        done
        export HOME="$work/home" PATH="$work/bin:/usr/bin:/bin" XDG_RUNTIME_DIR=/run
        cd "$work" || return 1
        face=/usr/share/fonts/inter/Inter-Regular.ttf
        # the machine script's FIFO, and its answer to the launcher's button (main.moonwater.sh: desktop_keys_list, desktop_keys_answer)
        : > /run/moonwater/keys
        mkfifo -m 600 /run/moonwater/keys.fire
        if [ -z "${DESKTOP_NO_ANSWER:-}" ]; then
                (
                        while :; do
                                while IFS= read -r pressed; do
                                        echo "$pressed" >> said.txt
                                        case $pressed in
                                        launcher_button) bin/launcher -x -a -f "$face" > launcher.log 2>&1 & ;;
                                        esac
                                done < /run/moonwater/keys.fire
                        done
                ) &
        else
                (while :; do cat /run/moonwater/keys.fire >> said.txt; done) &
        fi
        mkfifo input.fifo
        bin/compositor -H 1280x800 "$work/launcher.img" -i "$work/input.fifo" -f "$face" > compositor.log 2>&1 &
        cpid=$!
        python3 -I - "$work" << 'PY'
import os, struct, subprocess, sys, time
work = sys.argv[1]

def send(kind, code, value):
    now = time.time()
    os.write(fd, struct.pack("qqHHi", int(now), int((now - int(now)) * 1000000), kind, code, value))

def debug(into):
    said = subprocess.run(["bin/moonwater", "debug"], capture_output=True, text=True, timeout=20).stdout
    open(into, "w").write(said)
    return said

def until(test, seconds):
    end = time.time() + seconds
    while time.time() < end:
        if test():
            return True
        time.sleep(0.5)
    return False

def said():
    try:
        return open("said.txt").read().split().count("launcher_button")
    except OSError:
        return 0

def press():
    send(1, 0x110, 1), send(0, 0, 0)
    send(1, 0x110, 0), send(0, 0, 0)

# the compositor is up when moonwater debug answers with its screen (a loaded machine is slow to start it)
until(lambda: "screen 0" in debug("debug.start.txt"), 30)
time.sleep(2)
fd = os.open(work + "/input.fifo", os.O_WRONLY)
time.sleep(1)
# the pointer to the screen's corner, then to the band's launcher button: the band is along the bottom at 100 percent and the button at
# its start, the first 32 pixels of the last 21 rows. A report a little after the one before: reports read together are one movement,
# which the compositor's acceleration makes a faster one.
for _ in range(2000):
    send(2, 0, -1), send(0, 0, 0), send(2, 1, -1), send(0, 0, 0)
for _ in range(18):
    send(2, 0, 1), send(0, 0, 0)
    time.sleep(0.002)
for _ in range(790):
    send(2, 1, 1), send(0, 0, 0)
    time.sleep(0.002)
time.sleep(0.3)
# pressed again while nothing was said of it (a press the compositor took before its band was drawn), three times at most
for _ in range(3):
    press()
    if until(lambda: said() >= 1, 3):
        break
until(lambda: "Launcher at" in debug("debug.up.txt"), 20)
time.sleep(1)
subprocess.run(["cp", "launcher.img", "launcher.up.img"])
press()
until(lambda: "Launcher at" not in debug("debug.away.txt"), 10)
PY
        kill "$cpid" 2> /dev/null
        sleep 0.5
        kill -9 "$cpid" 2> /dev/null
        return 0
}

if [ "${1:-}" = --session ]; then
        session "$2" "$3" "$4"
        exit $?
fi

mkdir -p "$work"
unshare -rm sh "$self" --session "$work" "$compositor" "$fonts" > "$work/session.log" 2>&1
said=$(grep -c '^launcher_button$' "$work/said.txt" 2> /dev/null)
if [ "${said:-0}" -ge 1 ]; then row ok "launcher button: a press on the band's launcher button says launcher_button to the machine script"
else row fail "launcher button: a press on the band's launcher button says launcher_button to the machine script" "(said: $(cat "$work/said.txt" 2> /dev/null))"; fi
# window N  Launcher at X,Y, WxH
place=$(sed -n 's/^ *window [0-9]* *Launcher at \(-*[0-9]*\),\(-*[0-9]*\), \([0-9]*\)x\([0-9]*\).*/\1 \2 \3 \4/p' "$work/debug.up.txt" 2> /dev/null | head -n 1)
if [ -n "$place" ]; then row ok "launcher button: the answer to it starts the launcher, a window of the desktop"
else row fail "launcher button: the answer to it starts the launcher, a window of the desktop" "(debug: $(grep -c window "$work/debug.up.txt" 2> /dev/null) windows)"; fi
set -- $place
if [ -n "$place" ] && [ "$1" -ge 0 ] && [ "$1" -lt 32 ] && [ $(($2 + $4)) -le 780 ] && [ $(($2 + $4)) -ge 740 ]; then
        row ok "launcher button: the launcher opens beside the button, above the bottom band, its left edge at the band's start"
else
        row fail "launcher button: the launcher opens beside the button, above the bottom band, its left edge at the band's start" "(at $place)"
fi
if [ -n "$place" ] && ! grep -q 'Launcher at' "$work/debug.away.txt" 2> /dev/null; then
        row ok "launcher button: a second press on the button puts the launcher away"
else
        row fail "launcher button: a second press on the button puts the launcher away"
fi
exit "$failures"
