#!/bin/sh
# One build's whole check on box, with nobody at it:
#   sh /tmp/cperf/auto.sh NAME
# Chromium started fresh with DevTools, every scenario of auto.py driven through it, the compositor and Chromium profiled the whole while,
# and moonwater debug's GPU and client lines before and after. Everything to /tmp/cperf/runs/NAME/report.txt (and printed).
name=${1:-auto}
out=/tmp/cperf/runs/$name
mkdir -p "$out"
for d in /proc/[0-9]*; do
        case $(cat "$d/comm" 2>/dev/null) in chromium) kill "${d#/proc/}" 2>/dev/null ;; esac
done
sleep 2
rm -rf /tmp/cperf-auto
chrome --remote-debugging-port=9222 --user-data-dir=/tmp/cperf-auto --no-first-run about:blank > "$out/chrome.log" 2>&1 < /dev/null &
sleep 8
moonwater debug 2>/dev/null | grep -E '^  (gpu|metrics|presents|client|issue +canvas)' > "$out/debug.before"
moonwater profile compositor 60 > "$out/profile.compositor" 2>&1 &
p1=$!
moonwater profile chromium 60 > "$out/profile.chromium" 2>&1 &
p2=$!
bowl /bowls/arch /usr/bin/python3 /tmp/cperf/auto.py > "$out/scenarios.jsonl" 2> "$out/auto.err"
# The profiles alone: Chromium, started above, runs on.
wait $p1 $p2
moonwater debug 2>/dev/null | grep -E '^  (gpu|metrics|presents|client|issue +canvas)' > "$out/debug.after"
{
        echo "== $name: $(uname -v)"
        echo "== scenarios"; cat "$out/scenarios.jsonl"; tail -3 "$out/auto.err"
        echo "== compositor"; cat "$out/profile.compositor"
        echo "== chromium"; grep -v '^process [0-9]*$' "$out/profile.chromium"
        echo "== debug before"; cat "$out/debug.before"
        echo "== debug after"; cat "$out/debug.after"
} > "$out/report.txt"
cat "$out/report.txt"
