#!/bin/sh
#       The window gauntlet: the compositor of this tree, headless (800 by 600, its screen a file) in a private /run, /tmp and
#       /root, with Xwayland under its window manager, and the windowing libraries the Arch bowl has (GLFW, SDL 2 through
#       sdl2-compat, SDL 2 as Steam's runtime ships it, SDL 3, raylib) each run on Wayland and on X11, with the card's GL and
#       with llvmpipe. Each case has a deadline; past it is a hang. On box, from the tree's root:
#
#           sh test/gauntlet/window/run.sh [FILTER]
set -u
root=$(pwd)
filter=${1:-}
work=${GAUNTLET_WORK:-/var/tmp/gauntlet-window}
xroot=${GAUNTLET_BOWL:-/bowls/arch}
here=$root/test/gauntlet/window
rm -rf "$work" && mkdir -p "$work" || exit 1

if [ -z "${GAUNTLET_SHELL:-}" ]; then
        echo "building the shell (the compositor)"
        gcc -O2 -static -fno-pie -no-pie -nostdlib -nostartfiles -fno-stack-protector -fno-builtin -march=x86-64 -w -T src/build/spark.ld \
                -Wl,-e,_start -Wl,--build-id=none -Wl,--no-warn-rwx-segments -DCONFIG_MOONWATER_WAYLAND \
                -o "$work/shell" programs/shell.c 2> "$work/shell.err" || { sed 's/^/  /' "$work/shell.err" | head; exit 1; }
else
        cp "$GAUNTLET_SHELL" "$work/shell"
fi
ln -sf shell "$work/compositor"

echo "building the clients"
cp "$here"/*.c "$here"/*.py "$work/"
cd "$work" || exit 1
in_bowl() { bowl "$xroot" "$@"; }
build() { name=$1; shift; in_bowl /usr/bin/gcc -O2 -w "$@" -o "$work/$name" -lm > "$work/$name.err" 2>&1 || echo "  $name did not build: $(head -c 300 "$work/$name.err")"; }
build glfw glfw.c $(in_bowl /usr/bin/pkg-config --cflags --libs glfw3) -lGLESv2
build sdl sdl.c $(in_bowl /usr/bin/pkg-config --cflags --libs sdl2)
#       Steam's own SDL 2 (not sdl2-compat), its libraries copied beside the client.
steamrt=$(ls -d /root/.local/share/Steam/steamrt64/pv-runtime/steam-runtime-steamrt/steamrt3c_platform_*/files/lib/x86_64-linux-gnu 2> /dev/null | tail -1)
if [ -n "$steamrt" ]; then
        mkdir -p "$work/sdl2real"
        cp "$steamrt"/libSDL2-2.0.so.0* "$work/sdl2real/" 2> /dev/null
fi
cd "$root" || exit 1

exec unshare -rm sh "$here/inside.sh" "$work" "$xroot" "$filter"
