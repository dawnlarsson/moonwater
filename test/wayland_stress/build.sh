#!/bin/sh
# Builds the stress bench's clients into OUT (test tooling: libwayland-client, libX11 and xcb, and SDL2 and GLFW where their
# development files are; nothing here is part of src/). A client whose libraries are not here is left out with a NOTE.
#
#     sh test/wayland_stress/build.sh OUT
out=${1:?usage: sh test/wayland_stress/build.sh OUT}
here=$(cd "$(dirname "$0")" && pwd)
cc=${CC:-gcc}
mkdir -p "$out/proto"
warn="-std=gnu11 -O2 -Wall -Wextra -Werror"
pkg-config --exists wayland-client || { echo "NOTE no libwayland-client development files: nothing is built"; exit 2; }
proto=$(pkg-config --variable=pkgdatadir wayland-protocols)
code=
for xml in stable/xdg-shell/xdg-shell stable/presentation-time/presentation-time \
        unstable/relative-pointer/relative-pointer-unstable-v1 unstable/pointer-constraints/pointer-constraints-unstable-v1 \
        stable/viewporter/viewporter stable/linux-dmabuf/linux-dmabuf-v1; do
        name=${xml##*/}
        [ -r "$proto/$xml.xml" ] || { echo "NOTE no $xml.xml: nothing is built"; exit 2; }
        wayland-scanner client-header "$proto/$xml.xml" "$out/proto/$name-client.h" || exit 2
        wayland-scanner private-code "$proto/$xml.xml" "$out/proto/$name-code.c" || exit 2
        case $name in linux-dmabuf-v1 | viewporter) ;; *) code="$code $out/proto/$name-code.c" ;; esac
done
dmabuf=
if pkg-config --exists gbm; then
        dmabuf="-DHAVE_DMABUF $(pkg-config --cflags gbm) $out/proto/linux-dmabuf-v1-code.c $(pkg-config --libs gbm)"
else
        echo "NOTE no gbm development files: wl-stress is built without the dmabuf mode"
fi
# shellcheck disable=SC2086
$cc $warn -I"$out/proto" "$here/stress.c" $code $dmabuf $(pkg-config --cflags --libs wayland-client) -lm -lpthread -o "$out/wl-stress" || exit 1
# shellcheck disable=SC2086
$cc $warn -I"$out/proto" "$here/edge.c" $code "$out/proto/viewporter-code.c" $(pkg-config --cflags --libs wayland-client) -o "$out/wl-edge" || exit 1
if pkg-config --exists x11 x11-xcb xcb-present; then
        glx=
        pkg-config --exists gl && glx="-DHAVE_GLX $(pkg-config --libs gl)"
        # shellcheck disable=SC2086
        $cc $warn "$here/xstress.c" $(pkg-config --cflags --libs x11 x11-xcb xcb xcb-present) $glx -lm -o "$out/x-stress" || exit 1
else
        echo "NOTE no libX11, X11-xcb or xcb-present development files: x-stress is not built"
fi
if pkg-config --exists sdl2; then
        # shellcheck disable=SC2086
        $cc $warn "$here/sdl_anim.c" $(pkg-config --cflags --libs sdl2) -lm -o "$out/sdl-anim" || exit 1
else
        echo "NOTE no SDL2 development files: sdl-anim is not built"
fi
if pkg-config --exists glfw3 glesv2; then
        # shellcheck disable=SC2086
        $cc $warn "$here/glfw_anim.c" $(pkg-config --cflags --libs glfw3 glesv2) -lm -o "$out/glfw-anim" || exit 1
else
        echo "NOTE no GLFW or GLESv2 development files: glfw-anim is not built"
fi
exit 0
