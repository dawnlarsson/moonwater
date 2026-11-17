#!/bin/sh
# xscan-dump.sh (ember, round 2): run ON THE MACHINE as root. Dumps the live compositor's screen-sized card mappings (i915 gem or
# dri/card, as scanwatch.sh finds them) to /tmp/ember-scan-N.raw, and the window list to /tmp/ember-windows.txt. Read-only.
p=$(for d in /proc/[0-9]*; do [ "$(cat $d/comm 2>/dev/null)" = compositor ] && [ "$(cut -d' ' -f3 $d/stat)" != Z ] && echo ${d#/proc/}; done | head -1)
moonwater window > /tmp/ember-windows.txt 2>/dev/null
n=0
for r in $(awk '$6 ~ /gem|dri\/card/ {print $1}' /proc/$p/maps); do
  s=$((0x${r%-*})); e=$((0x${r#*-})); z=$((e - s))
  [ $z -lt 4000000 ] && continue
  dd if=/proc/$p/mem of=/tmp/ember-scan-$n.raw bs=4096 skip=$((s / 4096)) count=$((z / 4096)) 2>/dev/null
  echo "map $n $r" >> /tmp/ember-windows.txt
  n=$((n + 1))
done
echo "compositor $p maps $n"
