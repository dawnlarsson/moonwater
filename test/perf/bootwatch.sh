#!/bin/sh
# From as early in a boot as the link answers: every half second Shmem, the uptime, and the processes holding the most shared memory; at
# LIMIT_KB of Shmem the compositor is stopped (SIGSTOP, so it keeps what it holds and the OOM killer has nothing to take) and its maps kept.
limit=${1:-31457280}
out=/run/bootwatch.txt
: > $out
t=0
while [ $t -lt 240 ]; do
        t=$((t + 1))
        read up idle < /proc/uptime
        shm=$(grep Shmem: /proc/meminfo | tr -s ' ' | cut -d' ' -f2)
        top=$(for d in /proc/[0-9]*; do s=$(grep RssShmem $d/status 2>/dev/null | tr -s ' ' | cut -d' ' -f2); [ "${s:-0}" -gt 20000 ] && echo "$(cat $d/comm):${d#/proc/}=$s"; done | tr '\n' ' ')
        echo "up=$up shmem_kb=$shm $top" >> $out
        if [ "${shm:-0}" -gt "$limit" ]; then
                for d in /proc/[0-9]*; do
                        if [ "$(cat $d/comm 2>/dev/null)" = compositor ]; then
                                cp $d/maps /run/bootwatch.maps 2>/dev/null
                                ls -la $d/fd > /run/bootwatch.fds 2>/dev/null
                                kill -STOP ${d#/proc/}
                                echo "LIMIT: stopped compositor ${d#/proc/} at $shm kB" >> $out
                        fi
                done
                break
        fi
        sleep 0.5
done
echo done >> $out
