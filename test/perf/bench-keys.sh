#!/bin/sh
# bench-keys.sh MACHINE on|off [KEYS] [GAP] [omni]   (ember's tool, landed by ash into test/perf: the latency stage columns)
#
#     sh test/perf/bench-keys.sh box on 60 0.2            one TSV row to stdout (`bench-keys.sh --header` for the header line)
#     sh test/perf/bench-keys.sh grassy-rhino off 60 0.2 omni
#
# keylat-cycle.py under run2.sh on the machine (a fresh Chromium, a textarea page; "omni" types into the omnibox instead, never Enter
# there), which makes `moonwater canvas keys` stamp every key; keys.txt is pulled back and reduced to one row. Leaves canvas gpu on.
# Locks: LOCKS dir (default $TMPDIR/moonwater-locks); a young lock held by someone else makes it stop with exit 3. MW: the moonwater.
# Columns: machine gpu path scenario keys seen missing whole_p50_ms whole_p90_ms whole_max_ms, then p50 and p90 in microseconds of
# each stage: read (kernel report to the compositor's read), sent (to the window's client), flushed (socket written), commit (Chromium's:
# to the client's next picture of the window), present (the card or processor compose), shown (the vblank wait).
HERE=$(cd "$(dirname "$0")" && pwd)
MW=${MW:-$HOME/.local/bin/moonwater}
LOCKS=${LOCKS:-${TMPDIR:-/tmp}/moonwater-locks}
if [ "$1" = "--header" ]; then
	echo "machine	gpu	path	scenario	keys	seen	missing	whole_p50_ms	whole_p90_ms	whole_max_ms	read_p50	read_p90	sent_p50	sent_p90	flushed_p50	flushed_p90	commit_p50	commit_p90	present_p50	present_p90	shown_p50	shown_p90"
	exit 0
fi
m=$1; gpu=${2:-on}; n=${3:-60}; gap=${4:-0.2}; mode=${5:-page}
[ -n "$m" ] || { echo "usage: bench-keys.sh MACHINE on|off [KEYS] [GAP] [omni]" >&2; exit 2; }
mkdir -p "$LOCKS"
L="$LOCKS/$m"
if [ -e "$L" ] && [ -z "$(find "$L" -mmin +20 2>/dev/null)" ]; then echo "bench-keys: $m is locked: $(cat "$L")" >&2; exit 3; fi
echo "ash $(date +%H:%M) bench-keys $gpu" > "$L"
trap 'rm -f "$L"' EXIT INT TERM
$MW link "$m" "mkdir -p /tmp/cperf" >/dev/null 2>&1
$MW link push "$m" "$HERE/keylat-cycle.py" /tmp/cperf/keylat-cycle.py >/dev/null 2>&1
$MW link "$m" "moonwater canvas gpu $gpu" >/dev/null 2>&1
omni=""; [ "$mode" = omni ] && omni="OMNI=1 "
out=$($MW link "$m" "rm -f /run/moonwater/record/keys.txt; ${omni}sh /tmp/cperf/run2.sh keylat-cycle.py $n $gap" 2>&1)
keys=$($MW link "$m" 'cat /run/moonwater/record/keys.txt' 2>/dev/null)
$MW link "$m" "moonwater canvas gpu on" >/dev/null 2>&1
path=processor; [ "$m" = box ] && path=card
scen=textarea; [ "$mode" = omni ] && scen=omnibox
printf '%s\n%s\n' "$(printf '%s\n' "$out" | grep '"keys"' | tail -1)" "$keys" | python3 -I -c '
import json, sys
m, gpu, path, scen = sys.argv[1:5]
lines = sys.stdin.read().split("\n")
j = json.loads(lines[0]) if lines[0].startswith("{") else {}
rows = [[float(v) for v in l.split()] for l in lines[1:] if l.strip() and not l.startswith("#") and len(l.split()) == 8]
cols = [[r[i] for r in rows] for i in range(1, 8)]
def p(v, q):
    v = sorted(v)
    return "%.1f" % v[min(len(v) - 1, int(q * len(v)))] if v else ""
def ms(v, q):
    v = sorted(v)
    return "%.2f" % (v[min(len(v) - 1, int(q * len(v)))] / 1000) if v else ""
row = [m, gpu, path, scen, j.get("keys", ""), j.get("seen", ""), j.get("missing", ""),
       ms(cols[6], 0.5), ms(cols[6], 0.9), ms(cols[6], 1.0)]
for i in range(6):
    row += [p(cols[i], 0.5), p(cols[i], 0.9)]
print("\t".join(str(v) for v in row))
' "$m" "$gpu" "$path" "$scen"
