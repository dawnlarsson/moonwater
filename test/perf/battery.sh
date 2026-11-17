#!/bin/sh
# Chromium hammered whole (battery.py: its tabs, omnibox, menus, pages, wheel, links, a held corner dragged and the compositor's own
# sizes, through a uinput keyboard and tablet), with nobody at the machine:
#
#     sh /tmp/cperf/battery.sh NAME [ROUNDS]      on the machine, pushed there by run.sh's way (moonwater link push)
#
# This half runs outside the bowl: a fresh Chromium with DevTools on 127.0.0.1:9222, then battery.py under the Arch bowl's python3, and
# a loop that answers its asks of the machine (it writes /tmp/cperf/cmd.*.sh; the output comes back as .done). MW names the moonwater
# to ask (a hand-started compositor's own, whose table it reads), moonwater by default. The rows are kept in /tmp/cperf/runs/NAME.
name=${1:-battery}
rounds=${2:-1}
MW=${MW:-moonwater}
out=/tmp/cperf/runs/$name
mkdir -p "$out"
rm -f /tmp/cperf/cmd.*
for d in /proc/[0-9]*; do
        case $(cat "$d/comm" 2>/dev/null) in chromium) kill "${d#/proc/}" 2>/dev/null ;; esac
done
sleep 2
rm -rf /tmp/cperf-auto
# What is typed into the omnibox stays on the machine: no search suggestions asked of the search engine as the keys go in.
mkdir -p /tmp/cperf-auto/Default
printf '{"search":{"suggest_enabled":false},"alternate_error_pages":{"enabled":false},"safebrowsing":{"enabled":false}}' > /tmp/cperf-auto/Default/Preferences
(chrome --remote-debugging-port=9222 --user-data-dir=/tmp/cperf-auto --no-first-run --no-default-browser-check about:blank > "$out/chrome.log" 2>&1 < /dev/null &)
sleep 8
: > /tmp/cperf/battery.run
(
        while [ -e /tmp/cperf/battery.run ]; do
                for c in /tmp/cperf/cmd.*.sh; do
                        [ -e "$c" ] || continue
                        sed "s#^moonwater #$MW #" "$c" > "$c.run"
                        rm -f "$c"
                        sh "$c.run" > "${c%.sh}.out" 2>&1
                        mv "${c%.sh}.out" "${c%.sh}.done"
                        rm -f "$c.run"
                done
                sleep 0.05
        done
) &
helper=$!
# The real /dev/uinput, opened here and handed in as 7 and 8: the bowl's own is /dev/null, so no program in a bowl injects input.
bowl /bowls/arch /usr/bin/python3 /tmp/cperf/battery.py "$rounds" > "$out/rows.jsonl" 2> "$out/battery.err" 7<>/dev/uinput 8<>/dev/uinput
rm -f /tmp/cperf/battery.run
wait $helper 2>/dev/null
cat "$out/rows.jsonl"
tail -5 "$out/battery.err"
