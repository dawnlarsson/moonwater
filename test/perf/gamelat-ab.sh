#!/bin/sh
# gamelat-ab.sh LABEL BINARY|image RATES SECONDS (gameinput): the gamelat probe's scenarios on one compositor, one JSON row each to
# /tmp/cperf/runs/gamelat/LABEL.jsonl. BINARY is swapped in with swapcomp.sh (image: the running one); restore is the caller's.
L=$1; B=$2; R=${3:-1000,8000}; T=${4:-2}
O=/tmp/cperf/runs/gamelat; mkdir -p $O; : > $O/$L.jsonl
[ "$B" != image ] && sh /tmp/swapcomp.sh $B && sleep 2
run() { env "$@" MACHINE=$(hostname) timeout 120 bowl /bowls/alpine /usr/bin/python3 /tmp/cperf/gamelat.py $MODE $R $T 7<>/dev/uinput 8<>/dev/uinput 2>/dev/null | sed "s/^{/{\"label\": \"$L\", \"case\": \"$CASE\", /" >> $O/$L.jsonl; sleep 1; }
[ -n "$BOWL" ] || BOWL=/bowls/alpine
MODE=wl CASE=wl-lock run LOCK=lock KEYS=30
MODE=wl CASE=wl-confine run LOCK=confine KEYS=0
MODE=wl CASE=wl-free run LOCK=none KEYS=0
MODE=x CASE=x-grab-warp run XGRAB=1 XWARP=1 KEYS=30
MODE=x CASE=x-free run XGRAB=0 XWARP=0 KEYS=0
if [ -e /bowls/alpine/usr/lib/libSDL2-2.0.so.0 ]; then
  MODE=sdl CASE=sdl-wayland run SDL_VIDEODRIVER=wayland KEYS=30
  MODE=sdl CASE=sdl-x11 run SDL_VIDEODRIVER=x11 KEYS=30
fi
cat $O/$L.jsonl | wc -l
