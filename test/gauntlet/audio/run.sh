#!/bin/sh
#       The audio gauntlet: the sound server of this tree, run on its own socket with no card (audio -n, its mix to a
#       file), hammered by every library that speaks PulseAudio that the Arch bowl has, old and new, in their patterns.
#       Each case has a deadline; a case past it is a hang. Run on a machine with the Arch bowl (box), from the tree's root:
#
#           sh test/gauntlet/audio/run.sh [FILTER]        FILTER: only cases whose name has it
#
#       It builds the shell (the server is its audio applet) the way test/run builds a freestanding program, the clients
#       in the bowl, and says PASS, FAIL or HANG for each case, then the totals. Nothing is played on the machine's card.
set -u
root=$(pwd)
filter=${1:-}
work=${GAUNTLET_WORK:-/tmp/gauntlet-audio}
bowl_root=${GAUNTLET_BOWL:-/bowls/arch}
here=$root/test/gauntlet/audio
rm -rf "$work" && mkdir -p "$work" || exit 1
in_bowl() { bowl "$bowl_root" "$@"; }

#       The server: this tree's shell, called audio.
if [ -z "${GAUNTLET_SERVER:-}" ]; then
        echo "building the shell (the server)"
        gcc -O2 -static -fno-pie -no-pie -nostdlib -nostartfiles -fno-stack-protector -fno-builtin -march=x86-64 -w -T src/build/spark.ld \
                -Wl,-e,_start -Wl,--build-id=none -Wl,--no-warn-rwx-segments -DCONFIG_MOONWATER_WAYLAND \
                -o "$work/shell" programs/shell.c 2> "$work/shell.err" || { sed 's/^/  /' "$work/shell.err" | head; exit 1; }
        ln -sf shell "$work/audio"
        server=$work/audio
else
        server=$GAUNTLET_SERVER
fi
"$server" -n -S "$work/native" -C "$work/settings" -P "$work/state" -o "$work/mix.raw" > "$work/server.log" 2>&1 &
server_pid=$!
trap 'kill $server_pid 2> /dev/null' EXIT INT TERM
i=0
while [ ! -S "$work/native" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i + 1)); done
[ -S "$work/native" ] || { echo "the server did not start:"; cat "$work/server.log"; exit 1; }
export PULSE_SERVER="unix:$work/native"

#       The clients, built in the bowl.
echo "building the clients"
cp "$here"/*.c "$work/"
[ -f "$work/miniaudio.h" ] || curl -sfL -o "$work/miniaudio.h" https://raw.githubusercontent.com/mackron/miniaudio/0.11.21/miniaudio.h
build() { name=$1; shift; in_bowl /usr/bin/gcc -O2 -w "$@" -o "$work/$name" -lm > "$work/$name.err" 2>&1 || echo "  $name did not build: $(head -c 300 "$work/$name.err")"; }
cd "$work" || exit 1
build pa_hammer pa_hammer.c $(in_bowl /usr/bin/pkg-config --cflags --libs libpulse)
build sdl2 sdl2.c $(in_bowl /usr/bin/pkg-config --cflags --libs sdl2)
build sdl3 libs.c -DLIB_SDL3 $(in_bowl /usr/bin/pkg-config --cflags --libs sdl3)
build sdl12 libs.c -DLIB_SDL12 -I/usr/include $(in_bowl /usr/bin/pkg-config --cflags --libs sdl12_compat 2> /dev/null || echo -lSDL)
build openal libs.c -DLIB_OPENAL $(in_bowl /usr/bin/pkg-config --cflags --libs openal)
build portaudio libs.c -DLIB_PORTAUDIO $(in_bowl /usr/bin/pkg-config --cflags --libs portaudio-2.0)
build ao libs.c -DLIB_AO $(in_bowl /usr/bin/pkg-config --cflags --libs ao)
build alsa libs.c -DLIB_ALSA $(in_bowl /usr/bin/pkg-config --cflags --libs alsa)
[ -f miniaudio.h ] && build miniaudio libs.c -DLIB_MINIAUDIO -I. -lpthread -ldl
cd "$root" || exit 1

pass=0 fail=0 hang=0
case_run()
{
        name=$1 limit=$2; shift 2
        case $name in *"$filter"*) ;; *) return ;; esac
        [ -x "$1" ] || [ "${1#/}" = "$1" ] || { echo "SKIP $name (not built)"; return; }
        in_bowl /usr/bin/timeout -k 2 "$limit" "$@" > "$work/case.out" 2>&1
        status=$?
        if [ $status -eq 0 ]; then
                pass=$((pass + 1)); echo "PASS $name"
        elif [ $status -eq 124 ] || [ $status -eq 137 ]; then
                hang=$((hang + 1)); echo "HANG $name (past ${limit}s)"; tail -3 "$work/case.out" | sed 's/^/     /'
        else
                fail=$((fail + 1)); echo "FAIL $name"; tail -4 "$work/case.out" | sed 's/^/     /'
        fi
}

w=$work
#       Raw libpulse: SDL 1.2 and 2's whole chunks, at every rate and size a game asks for, with and without the latency adjusted.
for rate in 8000 11025 22050 44100 48000 96000 192000; do
        for chunk in 256 512 1024 2048 4096 8192; do
                for t in 1 2 3 4 8; do
                        case_run "chunks $rate s16 2 $chunk x$t adjust" 12 $w/pa_hammer chunks $rate s16 2 $chunk $t 1
                done
                case_run "chunks $rate s16 2 $chunk x2 fixed" 12 $w/pa_hammer chunks $rate s16 2 $chunk 2 0
        done
done
for fmt in u8 s16 s24 s32 f32; do
        for ch in 1 2 6 8; do
                case_run "chunks 48000 $fmt $ch 1024 x2 adjust" 12 $w/pa_hammer chunks 48000 $fmt $ch 1024 2 1
                case_run "callback 48000 $fmt $ch 20ms" 12 $w/pa_hammer callback 48000 $fmt $ch 20
        done
done
for ms in 1 5 10 25 50 100 250 1000 2000; do
        case_run "callback 44100 f32 2 ${ms}ms" 12 $w/pa_hammer callback 44100 f32 2 $ms
done
for s in drain-unstarted corked-start cork-toggle flush trigger prebuf0 set-attr update-rate disconnect latency record; do
        case_run "$s" 15 $w/pa_hammer $s
done
case_run "many 32" 20 $w/pa_hammer many 32
case_run "churn 300" 30 $w/pa_hammer churn 300

#       The libraries.
for spec in "48000 s16 2 1024" "44100 s16 2 512" "44100 f32 2 4096" "22050 s16 1 2048" "48000 f32 6 1024" "96000 s32 2 8192" "11025 u8 1 256"; do
        case_run "SDL2 $spec" 40 $w/sdl2 $spec
done
for spec in "48000 2 1024" "44100 2 512" "22050 1 4096"; do
        case_run "SDL3 $spec" 40 $w/sdl3 $spec
        case_run "SDL1.2 $spec" 40 $w/sdl12 $spec
        case_run "OpenAL $spec" 40 $w/openal $spec
        case_run "PortAudio $spec" 40 $w/portaudio $spec
        case_run "libao $spec" 40 $w/ao $spec
        case_run "ALSA-pulse $spec" 40 $w/alsa $spec
        case_run "miniaudio $spec" 40 $w/miniaudio $spec
done

#       Programs.
in_bowl /usr/bin/ffmpeg -loglevel error -f lavfi -i "sine=frequency=440:duration=2" -ac 2 -y "$work/sine.wav" 2> /dev/null
case_run "paplay" 15 /usr/bin/paplay "$work/sine.wav"
case_run "pacat 10ms" 15 /bin/sh -c "/usr/bin/pacat --latency-msec=10 < $work/sine.wav"
case_run "ffmpeg to pulse" 15 /usr/bin/ffmpeg -loglevel error -re -i "$work/sine.wav" -f pulse default
case_run "mpv" 20 /usr/bin/mpv --no-video --really-quiet --ao=pulse "$work/sine.wav"
case_run "gstreamer pulsesink" 20 /usr/bin/gst-launch-1.0 -q filesrc location="$work/sine.wav" ! decodebin ! audioconvert ! pulsesink

echo "the server: $(grep -c . "$work/server.log") lines of log, mix $(wc -c < "$work/mix.raw" 2> /dev/null) bytes"
echo "pass $pass, fail $fail, hang $hang"
[ $fail -eq 0 ] && [ $hang -eq 0 ]
