#!/bin/sh
# scandump.sh PREFIX: the live compositor's two scanouts (gem or dri card mappings of the screen's size) dumped to PREFIX-1.raw and
# PREFIX-2.raw (whole mappings, row stride 7680 on a 1920-wide screen). Read-only; run outside the bowl. cedar, long-lived repro.
p=
for d in /proc/[0-9]*; do
	[ "$(cat $d/comm 2>/dev/null)" = compositor ] || continue
	if awk '$6 ~ /gem|dri\/card/ {found=1} END {exit !found}' $d/maps 2>/dev/null; then p=${d#/proc/}; break; fi
done
[ -n "$p" ] || { echo "no compositor scanout mapped"; exit 1; }
n=0
for r in $(awk '$6 ~ /gem|dri\/card/ {print $1}' /proc/$p/maps); do
	s=$((0x${r%-*})); e=$((0x${r#*-})); z=$((e - s))
	[ $z -lt 4000000 ] && continue
	n=$((n + 1))
	dd if=/proc/$p/mem of=$1-$n.raw bs=4096 skip=$((s / 4096)) count=$((z / 4096)) 2>/dev/null
done
echo "scanouts $n pid $p"
