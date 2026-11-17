# framecode (cedar, round 2): a plain Wayland client that draws its frame number in pixels and films its own window (test/perf/framecode.py).
# Two modes: full (every frame a whole new picture) and bands (a correct client whose background never changes: only the two code bands
# are damaged). Rows: the wrong-frame counts (torn, colour, older frames after newer), superseded frames (committed, never shown), and the
# commit-to-screen latency (p50, p99, max). Runs on any machine with python3 in its bowl; no browser.
case_framecode() {
	m=$1 g=$2 p=$3
	take "$m" framecode || return 0
	bowl=$(on "$m" 'test -x /bowls/arch/usr/bin/python3 && echo /bowls/arch || { test -x /bowls/alpine/usr/bin/python3 && echo /bowls/alpine; }' | tail -1)
	if [ -z "$bowl" ] || [ "${bowl#/bowls/}" = "$bowl" ]; then
		put "$m" "$g" "$p" case-framecode skip "no bowl with python3" 1
		drop "$m"
		return 0
	fi
	$MW link push "$m" "$HERE/framecode.py" /tmp/cperf/framecode.py < /dev/null > /dev/null 2>&1
	$MW link push "$m" "$HERE/fc-run.sh" /tmp/cperf/fc-run.sh < /dev/null > /dev/null 2>&1
	on "$m" "moonwater canvas gpu $g" > /dev/null
	for mode in full bands; do
		# timeout: a link that does not return (seen on grassy-rhino with a link queued behind another) must not stall the whole bench
		row=$(timeout 240 $MW link "$m" "cd /tmp/cperf && BOWL=$bowl MODE=$mode MACHINE=$m GPU=$g sh /tmp/cperf/fc-run.sh framecode.py 240 0 2>/dev/null | tail -1" < /dev/null 2>&1)
		[ -n "$row" ] || { put "$m" "$g" "$p" "case-framecode-$mode" skip "no row" 1; continue; }
		printf '%s\n' "$row" | python3 -I -c '
import json, sys
r = json.loads(sys.stdin.read())
out = sys.argv[1]
for k, metric in (("torn", "wrong_frames_torn"), ("colour", "wrong_frames_wrong"), ("backwards", "wrong_frames_older"),
                  ("superseded", "superseded"), ("p50_ms", "latency_p50_ms"), ("p99_ms", "latency_p99_ms"), ("max_ms", "latency_max_ms"),
                  ("over_50ms", "late_over_50ms")):
    print("\t".join([sys.argv[2], sys.argv[3], sys.argv[4], "case-framecode-" + r["mode"], metric, str(r[k])]))
' "$m" "$g" "$p" "$mode" > "$OUT/raw/framecode-$m-$g-$mode.tsv"
		cat "$OUT/raw/framecode-$m-$g-$mode.tsv" >> "$LONG"
	done
	on "$m" "moonwater canvas gpu on" > /dev/null
	drop "$m"
}
