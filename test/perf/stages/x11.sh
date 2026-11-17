#!/bin/sh
# stages/x11.sh MACHINE GPU PATH (ember, round 2): two X programs at once, each its own Xwayland (one per program run, the wrapper's
# way), and an OpenGL ES 2 window through Xwayland. Ground truth from the SCANOUTS (x11/scan-dump.sh as root, checked on the Mac by
# x11/check.py), never from `moonwater canvas shot`. Rows on stdout, long form: machine gpu path scenario metric value.
#   x11  wrong_pixels, wrong_rows  the X window's final picture, both scanout buffers (the worst), corners and pointer excluded
#        visible                   1 when both X windows are in the compositor's list (the two-server bug: 0 before the fix)
#   gl   counter_on_screen, stale_frames  the GL window's frame counter on the screen, against the last frame it printed
#   skip (metric) with a reason when the machine has no Xwayland in its bowl or is locked (exit 0)
# The two windows do not overlap (xtest 60,60 640x400; xgl 60,520 480x240; a 1680x1050 screen).
# Env: MW (the moonwater), LOCKS (the team's locks dir), X11_SECS (how long each client animates, 12).
HERE=$(cd "$(dirname "$0")/.." && pwd)
MW=${MW:-$HOME/.local/bin/moonwater}
LOCKS=${LOCKS:-${TMPDIR:-/tmp}/moonwater-locks}
SECS=${X11_SECS:-12}
m=$1; g=$2; p=$3
[ -n "$m" ] || { echo "usage: x11.sh MACHINE GPU PATH" >&2; exit 2; }
out=$(mktemp -d "${TMPDIR:-/tmp}/x11.XXXXXX") || exit 1
# The screen's size as the machine has it (`moonwater canvas` names the primary screen: "DP-1: 1920 by 1080, ...").
sw=; sh=
read -r sw sh <<EOF
$($MW link "$1" 'moonwater canvas' 2>/dev/null | sed -n 's/.*: \([0-9]*\) by \([0-9]*\),.*/\1 \2/p' | head -1)
EOF
[ -n "$sw" ] || sw=1920 sh=1080
row() { printf '%s\t%s\t%s\tx11\t%s\t%s\n' "$m" "$g" "$p" "$1" "$2"; }
skip() { printf '%s\t%s\t%s\tx11\tskip\t%s\n' "$m" "$g" "$p" "$1"; rm -rf "$out"; exit 0; }
case $m in box) bowl=/bowls/arch ;; *) bowl=/bowls/alpine ;; esac
$MW link "$m" "test -x $bowl/usr/local/bin/xwayland-moonwater && test -x $bowl/usr/bin/Xwayland && echo have" 2>/dev/null | grep -q have || skip xwayland-bowl
L=$LOCKS/$m
if [ -e "$L" ] && [ -z "$(find "$L" -mmin +20 2>/dev/null)" ]; then skip "locked: $(cat "$L")"; fi
mkdir -p "$LOCKS" && echo "ember $(date +%H:%M) x11 stage $g" > "$L"
trap 'rm -f "$L"; [ -n "$X11_KEEP" ] || rm -rf "$out"' EXIT INT TERM
[ -n "$X11_KEEP" ] && echo "x11 work dir: $out" >&2
$MW link "$m" "moonwater canvas gpu $g" >/dev/null 2>&1
for f in xclient.py xgl.py; do $MW link push "$m" "$HERE/x11/$f" "/tmp/cperf/$f" >/dev/null 2>&1; done
$MW link push "$m" "$HERE/x11/scan-dump.sh" /tmp/cperf/scan-dump.sh >/dev/null 2>&1
# Clients of an earlier run (same titles) are killed first: the windows are then this run's only ones.
$MW link "$m" 'moonwater window | sed -n "s/.*pid \([0-9]*\), at .*: \(xtest\|xgl\)$/\1/p" > /tmp/ember-x11-old.txt; for q in $(cat /tmp/ember-x11-old.txt); do kill $q; done' >/dev/null 2>&1
sleep 2
# Each program is its own Xwayland (the wrapper starts one per run); the second one's window must still be managed.
$MW link "$m" "rm -f /tmp/ember-x11-xa.log /tmp/ember-x11-xb.log; setsid sh -c 'bowl $bowl /usr/local/bin/xwayland-moonwater /usr/bin/python3 /tmp/cperf/xclient.py $SECS > /tmp/ember-x11-xa.log 2>/dev/null' </dev/null >/dev/null 2>&1 &" >/dev/null 2>&1
sleep 5
$MW link "$m" "setsid sh -c 'bowl $bowl /usr/local/bin/xwayland-moonwater /usr/bin/python3 /tmp/cperf/xgl.py $SECS > /tmp/ember-x11-xb.log 2>/dev/null' </dev/null >/dev/null 2>&1 &" >/dev/null 2>&1
sleep 6
$MW link "$m" 'moonwater window' > "$out/windows.txt" 2>/dev/null
xpid=$(sed -n 's/.*pid \([0-9]*\), at .*: xtest$/\1/p' "$out/windows.txt" | tail -1)
gpid=$(sed -n 's/.*pid \([0-9]*\), at .*: xgl$/\1/p' "$out/windows.txt" | tail -1)
if [ -n "$xpid" ] && [ -n "$gpid" ]; then vis=1; else vis=0; fi
row visible "$vis"
[ -n "$xpid" ] && $MW link "$m" "moonwater window move $xpid at 60,60" >/dev/null 2>&1
[ -n "$gpid" ] && $MW link "$m" "moonwater window move $gpid at 60,520" >/dev/null 2>&1
sleep "$SECS"
$MW link "$m" 'moonwater window' > "$out/windows.txt" 2>/dev/null
$MW link "$m" "moonwater window > /tmp/ember-x11-windows.txt" >/dev/null 2>&1
$MW link "$m" "sh /tmp/cperf/scan-dump.sh > /dev/null 2>&1; moonwater window > /tmp/ember-x11-windows.txt; tail -1 /tmp/ember-x11-xb.log > /tmp/ember-x11-last.txt" >/dev/null 2>&1
$MW link "$m" 'gzip -c /tmp/ember-x11-windows.txt | base64 -w0' 2>/dev/null | base64 -d 2>/dev/null | gunzip > "$out/windows.txt" 2>/dev/null
$MW link "$m" 'gzip -c /tmp/ember-x11-xb.log | base64 -w0' 2>/dev/null | base64 -d 2>/dev/null | gunzip > "$out/gl.log" 2>/dev/null
dumps=""
n=0
while [ $n -lt 4 ]; do
	$MW link "$m" "test -e /tmp/ember-scan-$n.raw && gzip -c /tmp/ember-scan-$n.raw | base64 -w0" 2>/dev/null | base64 -d 2>/dev/null | gunzip > "$out/scan-$n.raw" 2>/dev/null
	[ -s "$out/scan-$n.raw" ] || { rm -f "$out/scan-$n.raw"; break; }
	dumps="$dumps $out/scan-$n.raw"
	n=$((n + 1))
done
# Our two clients only (their wrappers end their servers with them); nothing else on the machine is touched.
# Keys into the GL window (a left click focuses it, then 25 presses): `canvas keys` stamps each stage on the machine, keys.txt pulled back.
kcli=moonwater
$MW link "$m" 'test -x /tmp/mw/moonwater && echo y' 2>/dev/null | grep -q y && kcli=/tmp/mw/moonwater
$MW link push "$m" "$HERE/x11/focus-keys.py" /tmp/cperf/xfocus-keys.py >/dev/null 2>&1
$MW link push "$m" "$HERE/battery.py" /tmp/cperf/battery.py >/dev/null 2>&1
$MW link "$m" "rm -f /run/moonwater/record/keys.txt; $kcli canvas keys 14 >/dev/null 2>&1; XFOCUS_WINDOWS=/tmp/ember-x11-windows.txt bowl $bowl /usr/bin/python3 /tmp/cperf/xfocus-keys.py 25 0.2 7<>/dev/uinput 8<>/dev/uinput >/dev/null 2>&1" >/dev/null 2>&1
sleep 10
$MW link "$m" 'gzip -c /run/moonwater/record/keys.txt | base64 -w0' 2>/dev/null | base64 -d 2>/dev/null | gunzip > "$out/keys.txt" 2>/dev/null
$MW link "$m" "kill $xpid $gpid 2>/dev/null" >/dev/null 2>&1
$MW link "$m" "moonwater canvas gpu on" >/dev/null 2>&1
if [ -z "$dumps" ]; then row x11_check no_dump; exit 0; fi
# shellcheck disable=SC2086
python3 -I "$HERE/x11/check.py" x11 "$out/windows.txt" "$sw" "$sh" $dumps 2>"$out/x11.err" | while read metric value; do row "$metric" "$value"; done
# the GL window's counter, the same dumps
python3 -I "$HERE/x11/check.py" gl "$out/windows.txt" "$sw" "$out/gl.log" $dumps 2>>"$out/x11.err" | while read metric value; do printf '%s\t%s\t%s\tgl\t%s\t%s\n' "$m" "$g" "$p" "$metric" "$value"; done
if [ -s "$out/keys.txt" ]; then python3 -I "$HERE/x11/check.py" keys "$out/keys.txt" | while read metric value; do printf '%s\t%s\t%s\tx11-keys\t%s\t%s\n' "$m" "$g" "$p" "$metric" "$value"; done; fi
exit 0
