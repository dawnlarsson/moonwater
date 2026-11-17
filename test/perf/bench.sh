#!/bin/sh
# sh test/perf/bench.sh [MACHINE...]
#
# The benchmark table (ash): one command over the linked machines, one row per machine x gpu x path x scenario x metric. Each stage
# runs only where the machine has the Chromium tools (/tmp/cperf); a machine without them gets a SKIP line, not a failure.
#   keys   typing into a Chromium textarea: key to vblank by stage (bench-keys.sh). box: the card on and off; the Intel boxes: the processor.
#   stale  the consent page (BENCH_URL, default google.com), BENCH_ROUNDS fresh Chromium rounds, each idle a few seconds and then a
#          pointer move over the desktop (no page input): the stale pixels the idle screen kept in the window (stale2.py).
#   resize a long resize (longresize.py, BENCH_RESIZE_SECS seconds on BENCH_RESIZE_URL): frames per second Chromium drew, the worst
#          time it was behind the compositor's size, its longest answer to DevTools, and the stuck count.
#   cpu    the compositor's ticks over each stale round and Chromium's over the round (bench-ticks.sh), and moonwater debug's
#          presents, late and missed presents, the card's and the processor's present times, at the start and the end of the stage.
# Env: BENCH_ROUNDS (6), BENCH_KEYS (60), BENCH_GAP (0.2), BENCH_URL, BENCH_OUT (default $TMPDIR/moonwater-bench-STAMP), LOCKS (the
# team's locks dir when there is one; default $TMPDIR/moonwater-locks), MW (the moonwater; default ~/.local/bin/moonwater).
# Output: $BENCH_OUT/table.tsv (long form: machine gpu path scenario metric value), table.md (the table), raw/ (every stage's output).
# Each stage takes locks/MACHINE while it runs (bench-keys.sh takes its own); a young lock held by someone else skips the stage.
# Canvas gpu is put back on after each stage. Chromium is started and closed by run2.sh (a throwaway profile, search suggestions off).
HERE=$(cd "$(dirname "$0")" && pwd)
MW=${MW:-$HOME/.local/bin/moonwater}
ROUNDS=${BENCH_ROUNDS:-6}; KEYS=${BENCH_KEYS:-60}; GAP=${BENCH_GAP:-0.2}
RSECS=${BENCH_RESIZE_SECS:-20}; RURL=${BENCH_RESIZE_URL:-https://en.wikipedia.org/wiki/Main_Page}
URL=${BENCH_URL:-https://www.google.com/}
TMP=${TMPDIR:-/tmp}
OUT=${BENCH_OUT:-$TMP/moonwater-bench-$(date +%Y%m%d-%H%M%S)}
LOCKS=${LOCKS:-$TMP/moonwater-locks}
export MW LOCKS
mkdir -p "$OUT/raw" "$LOCKS" || exit 1
LONG=$OUT/table.tsv
: > "$LONG"
if [ $# -gt 0 ]; then MACHINES="$*"; else MACHINES="box grassy-rhino special-magpie"; fi

# One long-form row: machine gpu path scenario metric value.
put() { printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$1" "$2" "$3" "$4" "$5" "$6" >> "$LONG"; }
say() { echo "bench: $*" >&2; }

# Takes the machine's lock for one stage; fails when someone else holds a young one.
take() {
	if [ -e "$LOCKS/$1" ] && [ -z "$(find "$LOCKS/$1" -mmin +20 2>/dev/null)" ]; then say "$1 locked: $(cat "$LOCKS/$1")"; return 1; fi
	echo "ash $(date +%H:%M) bench $2" > "$LOCKS/$1"
}
drop() { rm -f "$LOCKS/$1"; }

# A remote command's output (the link joins the words, so one quoted string).
on() { $MW link "$1" "$2" < /dev/null 2>&1; }

# moonwater debug's counters for one stage: $1 machine, $2 gpu, $3 path, $4 "start" or "end".
debug_put() {
	on "$1" 'moonwater debug 2>/dev/null | grep -E "^  (gpu |metrics|presents)"' > "$OUT/raw/debug-$1-$2-$4.txt"
	python3 -I -c '
import re, sys
text = open(sys.argv[1]).read()
vals = [("presents_late_per_s", r"(\d+) late a second"), ("presents_missed_per_s", r"(\d+) missed their vblank"),
        ("card_present_mean_us", r"the card.s present: (\d+) us mean"), ("cpu_present_mean_us", r"processor.s: (\d+) us mean"),
        ("gpu_presents_total", r"the GPU composed (\d+) presents"), ("processor_presents_total", r"the processor (\d+)")]
for name, pat in vals:
    x = re.search(pat, text)
    if x:
        print("\t".join([sys.argv[2], sys.argv[3], sys.argv[4], "debug-" + sys.argv[5], name, x.group(1)]))
' "$OUT/raw/debug-$1-$2-$4.txt" "$1" "$2" "$3" "$4" >> "$LONG"
}

stage_keys() {
	m=$1; g=$2
	if [ "$m" = box ]; then path=card; else path=processor; fi
	sh "$HERE/bench-keys.sh" "$m" "$g" "$KEYS" "$GAP" > "$OUT/raw/keys-$m-$g.tsv" 2> "$OUT/raw/keys-$m-$g.err"
	rc=$?
	[ $rc -eq 0 ] || { say "keys $m $g: exit $rc (see $OUT/raw/keys-$m-$g.err)"; return; }
	python3 -I -c '
import subprocess, sys
hdr = subprocess.run(["sh", sys.argv[2], "--header"], capture_output=True, text=True).stdout.rstrip("\n").split("\t")
for line in open(sys.argv[1]).read().split("\n"):
    row = line.split("\t")
    if len(row) != len(hdr): continue
    for k, v in zip(hdr, row):
        if k in ("machine", "gpu", "path", "scenario") or v == "": continue
        print("\t".join([row[0], row[1], row[2], row[3], k, v]))
' "$OUT/raw/keys-$m-$g.tsv" "$HERE/bench-keys.sh" >> "$LONG"
}

stage_stale() {
	m=$1; g=$2
	if [ "$m" = box ]; then path=card; else path=processor; fi
	take "$m" "stale $g" || return
	$MW link push "$m" "$HERE/stale2.py" /tmp/cperf/stale2.py < /dev/null > /dev/null 2>&1
	$MW link push "$m" "$HERE/bench-ticks.sh" /tmp/cperf/bench-ticks.sh < /dev/null > /dev/null 2>&1
	on "$m" "moonwater canvas gpu $g" > /dev/null
	debug_put "$m" "$g" "$path" start
	i=0
	while [ $i -lt "$ROUNDS" ]; do
		i=$((i + 1))
		before=$(on "$m" "sh /tmp/cperf/bench-ticks.sh" | grep compositor | tail -1)
		res=$(on "$m" "sh /tmp/cperf/run2.sh stale2.py $URL 1" | grep '"stale_px"' | tail -1)
		after=$(on "$m" "sh /tmp/cperf/bench-ticks.sh" | grep compositor | tail -1)
		printf '%s\n' "$res" > "$OUT/raw/stale-$m-$g-$i.jsonl"
		printf 'before %s\nafter %s\n' "$before" "$after" > "$OUT/raw/ticks-$m-$g-$i.txt"
		sc="consent-round$i"
		if [ -z "$res" ]; then say "stale $m $g round $i: no result"; continue; fi
		px=$(printf '%s\n' "$res" | python3 -I -c 'import json, sys; print(json.loads(sys.stdin.read().strip().split("\n")[-1])["stale_px"])' 2>/dev/null)
		[ -n "$px" ] && put "$m" "$g" "$path" "$sc" stale_px "$px"
		# ticks lines read "compositor N chromium M": fields 2 and 4
		b=$(printf '%s\n' "$before" | cut -d' ' -f2); a=$(printf '%s\n' "$after" | cut -d' ' -f2)
		c=$(printf '%s\n' "$after" | cut -d' ' -f4)
		[ -n "$b" ] && [ -n "$a" ] && put "$m" "$g" "$path" "$sc" compositor_ticks "$((a - b))"
		[ -n "$c" ] && put "$m" "$g" "$path" "$sc" chromium_ticks "$c"
	done
	debug_put "$m" "$g" "$path" end
	on "$m" "moonwater canvas gpu on" > /dev/null
	drop "$m"
}

# A long resize: longresize.py for RSECS seconds on RURL (the window's corner held, the hand going round); the frames Chromium drew, the
# longest answer to DevTools, and the times its window was behind the compositor's size, from its JSON line.
stage_resize() {
	m=$1; g=$2
	if [ "$m" = box ]; then path=card; else path=processor; fi
	take "$m" "resize $g" || return
	$MW link push "$m" "$HERE/longresize.py" /tmp/cperf/longresize.py < /dev/null > /dev/null 2>&1
	on "$m" "moonwater canvas gpu $g" > /dev/null
	res=$(on "$m" "sh /tmp/cperf/run2.sh longresize.py $RSECS $RURL" | grep '"stuck_count"' | tail -1)
	printf '%s\n' "$res" > "$OUT/raw/resize-$m-$g.jsonl"
	on "$m" "moonwater canvas gpu on" > /dev/null
	drop "$m"
	[ -n "$res" ] || { say "resize $m $g: no result"; return; }
	printf '%s\n' "$res" | python3 -I -c '
import json, sys
j = json.loads(sys.stdin.read().strip().split("\n")[-1])
out = {"resize_frames_per_s": round((j.get("frames") or 0) / max(1, j["seconds"]), 1), "resize_worst_behind_s": j["worst_behind_s"],
       "resize_worst_answer_ms": j["worst_answer_ms"], "resize_stuck_count": j["stuck_count"]}
for k, v in out.items():
    print("\t".join([sys.argv[1], sys.argv[2], sys.argv[3], "resize", k, str(v)]))
' "$m" "$g" "$path" >> "$LONG"
}

# The Chromium tools (run2.sh, battery.py, wayland_chromium.py) pushed to /tmp/cperf, and the Arch bowl they run in; a machine
# without the bowl is a SKIP line for every stage.
prepare() {
	$MW link "$1" "mkdir -p /tmp/cperf" < /dev/null > /dev/null 2>&1
	$MW link push "$1" "$HERE/run2.sh" /tmp/cperf/run2.sh < /dev/null > /dev/null 2>&1
	$MW link push "$1" "$HERE/battery.py" /tmp/cperf/battery.py < /dev/null > /dev/null 2>&1
	$MW link push "$1" "$HERE/../wayland_chromium.py" /tmp/cperf/wayland_chromium.py < /dev/null > /dev/null 2>&1
	on "$1" "test -x /bowls/arch/usr/bin/python3 && echo have" | grep -q have
}

# Plug-in cases (round 2): every test/perf/bench-cases/NAME.sh defines case_NAME MACHINE GPU PATH; see bench-cases/README.txt.
run_cases() {
	for f in "$HERE"/bench-cases/*.sh; do
		[ -e "$f" ] || continue
		name=$(basename "$f" .sh)
		. "$f"
		"case_$name" "$1" "$2" "$3" || say "case $name $1 $2: failed"
	done
}

for m in $MACHINES; do
	if ! on "$m" 'echo up' | grep -q up; then say "$m: not reachable"; put "$m" - - skip reachable 0; continue; fi
	if ! prepare "$m"; then say "$m: no Arch bowl with python3 (the Chromium tools cannot run there yet)"; put "$m" - - skip bowl 0; continue; fi
	if [ "$m" = box ]; then gpus="on off"; else gpus="off"; fi
	for g in $gpus; do
		stage_keys "$m" "$g"
		stage_stale "$m" "$g"
		stage_resize "$m" "$g"
		if [ "$m" = box ]; then cpath=card; else cpath=processor; fi
		run_cases "$m" "$g" "$cpath"
	done
	# Chromium of the throwaway profile closed, and the card's canvas gpu on, as the machine was found.
	on "$m" 'for d in /proc/[0-9]*; do grep -q cperf-auto $d/cmdline 2>/dev/null && kill ${d#/proc/} 2>/dev/null; done; moonwater canvas gpu on' > /dev/null
done
python3 -I "$HERE/bench-table.py" "$LONG" > "$OUT/table.md" && cat "$OUT/table.md"
say "output in $OUT"
