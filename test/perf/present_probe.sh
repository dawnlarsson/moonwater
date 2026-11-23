#!/bin/sh
# Build test/perf/present_probe.c in a bowl that has libwayland, gbm and wayland-protocols (the alpine bowl:
# apk add build-base wayland-dev wayland-protocols mesa-dev), into OUT (default /tmp/present_probe).
#   bowl /bowls/alpine /bin/busybox sh -c "cd TREE && sh test/perf/present_probe.sh [OUT]"
set -eu
PATH=/usr/bin:/bin:$PATH # the bowl's own tools first (the host's wayland-scanner cannot see the bowl's protocols)
out=${1:-/tmp/present_probe}
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
p=${PROTOCOLS:-/usr/share/wayland-protocols}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
for x in stable/xdg-shell/xdg-shell.xml:xdg-shell stable/presentation-time/presentation-time.xml:presentation-time \
         stable/linux-dmabuf/linux-dmabuf-v1.xml:linux-dmabuf staging/tearing-control/tearing-control-v1.xml:tearing-control; do
        wayland-scanner client-header "$p/${x%%:*}" "$work/${x##*:}-client.h"
        wayland-scanner private-code "$p/${x%%:*}" "$work/${x##*:}-code.c"
done
cc -std=gnu11 -O2 -Wall -Wextra -I"$work" $(pkg-config --cflags gbm wayland-client) -o "$out" "$here/present_probe.c" "$work"/*-code.c \
        $(pkg-config --libs gbm wayland-client)
echo "present_probe: built $out"
