#!/bin/sh
# test/audio_load.sh BIN DIR CARD: a low-latency stream (its buffer is 10 ms, so the card runs with the short periods) plays through
# the sound server on CARD for six seconds while all but four CPUs are busy (stress-ng when it is there, else a loop per CPU), the
# server and the client keeping four. The server counts its underruns when it ends, and that count must be 0: the card is kept
# filled on time under load. The volume is 0, so a card that is a speaker stays silent (the work is the same).
set -u
bin=$1
d=$2
card=$3
status=0
hogs=$(nproc)
hogs=$((hogs - 4))
[ "$hogs" -ge 1 ] || hogs=1

rm -rf "$d"
mkdir -p "$d/settings" || exit 1
echo 0 > "$d/settings/audio.volume"
echo off > "$d/settings/audio.mute"
python3 - "$d/in.raw" <<'PY' || exit 1
import math, struct, sys
rate = 44100
data = bytearray()
for i in range(rate * 6):
    v = round(0.25 * math.sin(2 * math.pi * 440 * i / rate) * 32767)
    data += struct.pack("<hh", v, v)
with open(sys.argv[1], "wb") as f:
    f.write(bytes(data))
PY

$bin $card -S "$d/sock" -C "$d/settings" -P "$d/state" > "$d/log" 2>&1 &
server=$!
for _ in 1 2 3 4 5 6 7 8 9 10
do
        [ -S "$d/sock" ] && break
        sleep 1
done
[ -S "$d/sock" ] || { echo "the server did not listen"; kill -TERM $server; exit 1; }

# every CPU busy while the stream plays
if command -v stress-ng > /dev/null 2>&1
then
        stress-ng --cpu "$hogs" --timeout 10s > /dev/null 2>&1 &
        busy=$!
        busy_loops=
else
        busy=
        busy_loops=
        for _ in $(seq 1 "$hogs")
        do
                sh -c 'while :; do :; done' &
                busy_loops="$busy_loops $!"
        done
fi

PULSE_SERVER="unix:$d/sock" paplay --raw --format=s16le --channels=2 --rate=44100 --latency-msec=10 "$d/in.raw"
play=$?

[ -n "$busy" ] && kill "$busy" 2> /dev/null
for pid in $busy_loops
do
        kill "$pid" 2> /dev/null
done
kill -TERM $server
wait $server

echo "  the stream played: paplay status $play; the server's log:"
tail -n 6 "$d/log" | sed 's/^/    /'
if [ "$play" = 0 ] && grep -q "ended after .* 0 underruns" "$d/log" && grep -q "the card runs at 44100 Hz" "$d/log"
then
        echo "  ok: under full CPU load the low-latency output ran with no underrun"
else
        echo "  FAILED: the low-latency output under full CPU load"
        status=1
fi
exit $status
