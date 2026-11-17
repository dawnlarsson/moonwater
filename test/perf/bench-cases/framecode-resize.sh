# framecode-resize (cedar, round 2): the resize storm. A framecode window animates (its frame number in pixels, every frame committed)
# while moonwater window move resizes it through eight sizes, at random moments mid-animation; after each resize both i915/amdgpu
# scanouts are dumped (test/perf/scandump.sh) and the window's code bands are read from each. A buffer is right when its bands agree and
# are a frame the client committed around the dump; stale is a step where BOTH buffers are wrong (a buffer not on the screen may lag one
# present legitimately). Rows: wrong_frames_stale_both, wrong_frames_one_buffer, resize_judged, resize_both_ok, resize_covered,
# resize_size_mismatch. Runs on any machine with python3 in its bowl; no browser.
case_framecode_resize() {
	m=$1 g=$2 p=$3
	take "$m" framecode-resize || return 0
	bowl=$(on "$m" 'test -x /bowls/arch/usr/bin/python3 && echo /bowls/arch || { test -x /bowls/alpine/usr/bin/python3 && echo /bowls/alpine; }' | tail -1)
	if [ -z "$bowl" ] || [ "${bowl#/bowls/}" = "$bowl" ]; then
		put "$m" "$g" "$p" case-framecode-resize skip "no bowl with python3" 1
		drop "$m"
		return 0
	fi
	for f in resize-storm.py framecode-keep.py scandump.sh fc-run.sh; do
		$MW link push "$m" "$HERE/$f" /tmp/cperf/$f < /dev/null > /dev/null 2>&1
	done
	on "$m" "moonwater canvas gpu $g" > /dev/null
	row=$(timeout 300 $MW link "$m" "mkdir -p /tmp/cperf/runs/storm; cd /tmp/cperf && MACHINE=$m GPU=$g PATH_LABEL=$p BOWL=$bowl sh /tmp/cperf/fc-run.sh resize-storm.py ${STORM_STEPS:-60} 2>/dev/null | tail -1" < /dev/null 2>&1)
	if [ -z "$row" ]; then
		put "$m" "$g" "$p" case-framecode-resize skip "no row" 1
	else
		printf '%s\n' "$row" | python3 -I -c '
import json, sys
r = json.loads(sys.stdin.read())
out = sys.argv[1:]
for k, metric in (("both_wrong", "wrong_frames_stale_both"), ("one_buffer_wrong", "wrong_frames_one_buffer"), ("judged", "resize_judged"),
                  ("both_ok", "resize_both_ok"), ("covered", "resize_covered"), ("size_mismatch", "resize_size_mismatch")):
    print("\t".join([out[0], out[1], out[2], "case-framecode-resize", metric, str(r[k])]))
' "$m" "$g" "$p" > "$OUT/raw/framecode-resize-$m-$g.tsv"
		cat "$OUT/raw/framecode-resize-$m-$g.tsv" >> "$LONG"
	fi
	on "$m" "moonwater canvas gpu on" > /dev/null
	drop "$m"
}
