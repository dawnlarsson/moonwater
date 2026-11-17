#!/bin/sh
# bench-ticks.sh (on the machine): the utime+stime ticks of the compositor and of Chromium's processes, one line: "compositor N chromium M".
c=0; h=0
for d in /proc/[0-9]*; do
	case $(cat "$d/comm" 2>/dev/null) in
	compositor) t=$(sed 's/.*) //' "$d/stat" 2>/dev/null | cut -d' ' -f12,13); c=$((c + ${t% *} + ${t#* })) ;;
	chromium*|chrome*) t=$(sed 's/.*) //' "$d/stat" 2>/dev/null | cut -d' ' -f12,13); h=$((h + ${t% *} + ${t#* })) ;;
	esac
done
echo "compositor $c chromium $h"
