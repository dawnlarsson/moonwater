#!/bin/sh
# test/audio_counter.sh BIN DIR: a counter stream (test/audio_counter.py) plays through the sound server's file sink with paplay,
# at several client write sizes and buffer attributes, and the file sink's output must hold every frame of it in order: a frame
# dropped, repeated or silenced inside the stream fails the row. The float cases are what a browser sends (48 kHz float32).
# Run by lane_audio; prints each case and exits 1 when any case fails.
set -u
bin=$1
d=$2
me=$(cd "$(dirname "$0")" && pwd)/audio_counter.py
status=0

python3 "$me" selftest || { echo "  FAILED: the checker's self-test"; exit 1; }

# case NAME RATE FORMAT [paplay arguments]: one stream of two seconds, then the check of the sink's file. The socket is in /tmp:
# a unix socket path is at most 108 bytes, and the lane's directory is longer than that.
case_run()
{
        name=$1
        rate=$2
        fmt=$3
        shift 3
        case_dir="$d/$name"
        sock=$(mktemp -u /tmp/mw-counter.XXXXXX)
        rm -rf "$case_dir"
        mkdir -p "$case_dir/settings" || return 1
        echo 100 > "$case_dir/settings/audio.volume"
        echo off > "$case_dir/settings/audio.mute"
        python3 "$me" gen "$rate" 2 "$fmt" "$case_dir/in.raw" || return 1
        case $fmt in
        f32) pformat=float32le ;;
        *) pformat=s16le ;;
        esac
        $bin -n -r "$rate" -o "$case_dir/out.raw" -S "$sock" -C "$case_dir/settings" -P "$case_dir/state" > "$case_dir/log" 2>&1 &
        server=$!
        for _ in 1 2 3 4 5 6 7 8 9 10
        do
                [ -S "$sock" ] && break
                sleep 1
        done
        if [ ! -S "$sock" ]
        then
                echo "  $name: the server did not listen"
                kill -TERM $server
                wait $server
                rm -f "$sock"
                return 1
        fi
        sleep 1
        # a client that does not finish in twenty seconds (a stream of two) is a failure, not a hang of the lane
        PULSE_SERVER="unix:$sock" timeout 20 paplay --raw --format="$pformat" --channels=2 --rate="$rate" "$@" "$case_dir/in.raw"
        played=$?
        sleep 2
        kill -TERM $server
        wait $server
        rm -f "$sock"
        if [ "$played" = 0 ] && python3 "$me" check "$case_dir/out.raw" "$case_dir/in.raw" "$fmt"
        then
                echo "  ok: $name, the stream arrived whole ($*)"
                return 0
        fi
        echo "  FAILED: $name ($*)"
        return 1
}

case_run s16-44-default 44100 s16 || status=1
case_run s16-48-process2 48000 s16 --process-time-msec=2 || status=1
case_run s16-44-latency10 44100 s16 --latency-msec=10 || status=1
case_run s16-48-latency400 48000 s16 --latency-msec=400 || status=1
case_run f32-48-process2 48000 f32 --process-time-msec=2 || status=1
case_run f32-48-default 48000 f32 || status=1
exit $status
