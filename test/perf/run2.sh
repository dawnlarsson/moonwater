#!/bin/sh
# run2.sh SCRIPT ARGS...: Chromium fresh, the helper loop answering battery asks with /tmp/mw/moonwater, SCRIPT under the Arch bowl.
for d in /proc/[0-9]*; do case $(cat $d/comm 2>/dev/null) in chromium) kill ${d#/proc/} 2>/dev/null;; esac; done; sleep 2
rm -f /tmp/cperf/cmd.* /tmp/cperf/scroll.go
: > /tmp/cperf/battery.run
(while [ -e /tmp/cperf/battery.run ]; do for c in /tmp/cperf/cmd.*.sh; do [ -e "$c" ] || continue; sed "s#^moonwater #${MW:-moonwater} #" "$c" > "$c.run"; rm -f "$c"; sh "$c.run" > "${c%.sh}.out" 2>&1; mv "${c%.sh}.out" "${c%.sh}.done"; rm -f "$c.run"; done; sleep 0.05; done) &
rm -rf /tmp/cperf-auto; mkdir -p /tmp/cperf-auto/Default; printf '{"search":{"suggest_enabled":false},"alternate_error_pages":{"enabled":false},"safebrowsing":{"enabled":false}}' > /tmp/cperf-auto/Default/Preferences; (env ${CHROME_ENV:-X=1} chrome ${CHROME_FLAGS:-} --remote-debugging-port=9222 --user-data-dir=/tmp/cperf-auto --no-first-run --no-default-browser-check about:blank >/tmp/cperf/run2-chrome.log 2>&1 </dev/null &)
sleep 8
s=$1; shift
bowl /bowls/arch /usr/bin/python3 /tmp/cperf/$s "$@" 7<>/dev/uinput 8<>/dev/uinput 2>/tmp/cperf/run2.err | tail -3
tail -3 /tmp/cperf/run2.err
rm -f /tmp/cperf/battery.run
