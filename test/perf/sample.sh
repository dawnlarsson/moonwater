#!/bin/sh
# Every thread of the compositor, Chromium and Xwayland, about five times a second for SECONDS:
#   T uptime
#   pid tid comm state ticks(utime+stime) syscall-number wchan
# sh /tmp/cperf/sample.sh NAME SECONDS
name=${1:?name}
secs=${2:-40}
out=/tmp/cperf/runs/$name.samples
: > "$out"
scan() {
        pids=
        for d in /proc/[0-9]*; do
                c=$(cat "$d/comm" 2>/dev/null) || continue
                case $c in compositor|chromium*|chrome*|Xwayland|steam*|hl2*) pids="$pids ${d#/proc/}" ;; esac
        done
}
n=0
read up rest < /proc/uptime
end=${up%.*}
end=$((end + secs))
while :; do
        read up rest < /proc/uptime
        [ "${up%.*}" -ge "$end" ] && break
        [ $((n % 5)) -eq 0 ] && scan
        n=$((n + 1))
        echo "T $up" >> "$out"
        for p in $pids; do
                for t in /proc/$p/task/*; do
                        [ -r "$t/stat" ] || continue
                        s=$(cat "$t/stat" 2>/dev/null) || continue
                        rest=${s#*) }
                        comm=${s#*(}; comm=${comm%%)*}
                        set -- $rest
                        st=$1; shift 10; ticks="$1 $2"
                        sc=$(cut -d' ' -f1 "$t/syscall" 2>/dev/null)
                        wc=$(cat "$t/wchan" 2>/dev/null)
                        echo "$p ${t##*/} $(echo $comm | tr ' ' _) $st $ticks ${sc:-?} ${wc:-0}" >> "$out"
                        if [ "$comm" = compositor ] && [ "${t##*/}" = "$p" ]; then
                                echo "K $(tr '\n' ';' < "$t/stack" 2>/dev/null)" >> "$out"
                        fi
                done
        done
        sleep 0.2
done
echo done >> "$out"
