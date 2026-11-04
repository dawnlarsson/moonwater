#!/bin/sh
# test/audio_rates.sh BIN DIR CARD: the sound server's output runs at the rate of the first stream that plays on an idle output
# (src/audio/service.c, audio_rate_choose), so a stream at a rate the card takes is passed through, sample for sample; a stream of
# another rate that plays at the same time is resampled to it; and an output idle for five seconds takes the next first stream's
# rate again. Run by lane_audio with CARD "-n" (no card: the server's file sink is the output, and the samples are compared) or
# "-d C:D" (a card: the rates are read from the server's log and from pactl). Prints each check and exits 1 on any failure.
set -u
bin=$1
d=$2
card=$3
status=0

verdict()
{
        if [ "$1" = 0 ]
        then
                echo "  ok: $CHECK_NAME"
        else
                echo "  FAILED: $CHECK_NAME"
                status=1
                # what the server and the client said, so a failure can be read from the lane's output
                for f in "$d"/log1 "$d"/log2 "$d"/sinks1 "$d"/sinks2a "$d"/sinks2b
                do
                        [ -f "$f" ] && sed "s|^|    $(basename "$f"): |" "$f" | tail -n 12
                done
        fi
}

rm -rf "$d"
mkdir -p "$d/settings" || exit 1
echo 100 > "$d/settings/audio.volume"
echo off > "$d/settings/audio.mute"
python3 - "$d" <<'PY' || exit 1
import math, struct, sys
d = sys.argv[1]
for name, rate, seconds in (("in44", 44100, 2.0), ("in48", 48000, 1.0)):
    frames = int(rate * seconds)
    data = bytearray()
    for i in range(frames):
        left = 0.25 * math.sin(2 * math.pi * 440 * i / rate) + 0.125 * math.sin(2 * math.pi * 1000 * i / rate)
        right = 0.25 * math.sin(2 * math.pi * 660 * i / rate)
        data += struct.pack("<hh", round(left * 32767), round(right * 32767))
    with open(d + "/" + name + ".raw", "wb") as f:
        f.write(bytes(data))
PY

# serve RUN: the server for one run (its output a file of its own in -n mode), up once its socket is there
serve()
{
        out=
        [ "$card" = -n ] && out="-o $d/out$1.raw"
        rm -f "$d/sock"
        $bin $card $out -S "$d/sock" -C "$d/settings" -P "$d/state" > "$d/log$1" 2>&1 &
        server=$!
        for _ in 1 2 3 4 5 6 7 8 9 10
        do
                [ -S "$d/sock" ] && break
                sleep 1
        done
        [ -S "$d/sock" ]
}

# play RATE NAME: a stream of the file NAME.raw (the 2-second or 1-second one) at RATE, from the server
play()
{
        paplay --raw --format=s16le --channels=2 --rate="$1" "$d/$2.raw"
}

export PULSE_SERVER="unix:$d/sock"

# run 1: one stream at 44100 on its own
serve 1 || { echo "the server did not listen"; exit 1; }
play 44100 in44 &
p=$!
sleep 1
pactl list sinks > "$d/sinks1" 2>&1
wait $p
sleep 1
kill -TERM $server
wait $server

CHECK_NAME="a stream at 44100 Hz on an idle output runs the output at 44100 Hz"
grep -q "runs at 44100 Hz" "$d/log1"
verdict $?
CHECK_NAME="the sink reports 44100 Hz while that stream plays"
grep -q "44100Hz" "$d/sinks1"
verdict $?
if [ "$card" = -n ]
then
        # Every 64-frame piece of the sound the file sink holds is a piece of the stream, unaltered: a resampled or gained stream
        # has none. (The client's chunks are sometimes dropped whole by the server, so the pieces are counted, not the offsets.)
        CHECK_NAME="the file sink holds the stream sample for sample (no piece of its sound is altered; at most 2 pieces in 100 are not the stream's)"
        python3 - "$d/in44.raw" "$d/out1.raw" <<'PY'
import sys
src = open(sys.argv[1], "rb").read()
out = open(sys.argv[2], "rb").read()
piece = 256
pieces = [out[i:i + piece] for i in range(0, len(out) - piece + 1, piece)]
sound = [p for p in pieces if any(p)]
other = sum(1 for p in sound if p not in src)
print("    %d pieces of sound, %d not the stream's" % (len(sound), other))
sys.exit(0 if len(sound) > 1000 and other * 100 <= 2 * len(sound) else 1)
PY
        verdict $?
fi

# run 2: a stream at 44100, a stream at 48000 that plays over it, the output idle for seven seconds, then a stream at 48000
serve 2 || { echo "the server did not listen"; exit 1; }
play 44100 in44 &
p1=$!
sleep 0.5
play 48000 in48 &
p2=$!
sleep 0.5
pactl list sinks > "$d/sinks2a" 2>&1
wait $p2
wait $p1
sleep 7
play 48000 in48 &
p3=$!
sleep 0.5
pactl list sinks > "$d/sinks2b" 2>&1
wait $p3
sleep 1
kill -TERM $server
wait $server

CHECK_NAME="a second stream at another rate does not move the output while the first plays (44100 Hz kept)"
grep -q "44100Hz" "$d/sinks2a" && ! grep -q "48000Hz" "$d/sinks2a"
verdict $?
CHECK_NAME="the output takes its rate again after it has been idle for five seconds (48000 Hz)"
grep -q "48000Hz" "$d/sinks2b"
verdict $?
CHECK_NAME="exactly two streams began on an idle output, at 44100 Hz and then at 48000 Hz"
[ "$(grep -c "begins on an idle output" "$d/log2")" = 2 ] && grep -q "runs at 44100 Hz" "$d/log2" && grep -q "runs at 48000 Hz" "$d/log2"
verdict $?
if [ "$card" != -n ]
then
        CHECK_NAME="the card itself is reopened at 44100 Hz and then at 48000 Hz"
        grep -q "the card runs at 44100 Hz" "$d/log2" && grep -q "the card runs at 48000 Hz" "$d/log2"
        verdict $?
fi

exit $status
