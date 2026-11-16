#!/bin/sh
# The Wayland stress bench: Moonwater's compositor against weston, KWin and Mutter, every scenario, one table.
#
#     sh test/wayland_stress/run.sh [--real] [--out FILE] [--seconds S] [--only a,b] [--compositors a,b] [--baseline OLD.json]
#
# Headless (the default): the clients are built (build.sh), Moonwater's compositor is built from programs/shell.c with
# CONFIG_MOONWATER_WAYLAND (or MW_COMPOSITOR names one built already), and run.py runs every compositor headless at 1920x1080
# in a user namespace with a private /run, /root and /tmp/.X11-unix, so nothing of the machine's is touched.
#
# --real is the owner's machine, one command from a Moonwater session or from a text console:
#
#     sh test/wayland_stress/run.sh --real
#
# Moonwater is measured on its card (the live session, or started on the card when there is none), and each reference on
# its DRM backend when the card is free, nested in the session when it is not (said in a NOTE). The results land in
# ~/wayland-stress/real-HOST-DATE.txt with a .json beside it, unless --out says where.
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
real=
out=
pass=
while [ $# -gt 0 ]; do
        case $1 in
        --real) real=1 ;;
        --out) out=$2; shift ;;
        --seconds | --only | --compositors | --baseline | --edge) pass="$pass $1 $2"; shift ;;
        --inside) inside=1 ;;
        *) echo "usage: sh test/wayland_stress/run.sh [--real] [--out FILE] [--seconds S] [--only a,b] [--compositors a,b] [--baseline OLD.json]" >&2; exit 2 ;;
        esac
        shift
done
work=${STRESS_WORK:-${TMPDIR:-/tmp}/wayland-stress.$(id -u)}
mkdir -p "$work"
if [ -z "$out" ]; then
        if [ -n "$real" ]; then
                out=$HOME/wayland-stress/real-$(uname -n)-$(date +%Y%m%d-%H%M).txt
        else
                out=$work/headless-$(date +%Y%m%d-%H%M).txt
        fi
fi

if [ -z "${inside:-}" ]; then
        sh "$here/build.sh" "$work/bin" || exit 1
        compositor=${MW_COMPOSITOR:-}
        if [ -z "$compositor" ] && [ -n "$real" ]; then
                compositor=$(command -v compositor || true)
        fi
        if [ -z "$compositor" ]; then
                compositor=$work/shell-wayland
                [ -r "$root/artifacts/gpu_kernels.inc" ] || (cd "$root" && sh build.sh gpu-kernels > "$work/gpu-kernels.log" 2>&1) || {
                        echo "the GPU kernels (artifacts/gpu_kernels.inc) did not build: $work/gpu-kernels.log" >&2; exit 1; }
                (cd "$root" && ${CC:-gcc} -O2 -static -nostdlib -nostartfiles -fno-stack-protector -fno-builtin -march=x86-64 \
                        -DCONFIG_MOONWATER_WAYLAND -w -T src/build/spark.ld -Wl,-e,_start -Wl,--build-id=none -Wl,--no-warn-rwx-segments \
                        -o "$compositor" programs/shell.c) || { echo "Moonwater's compositor did not build" >&2; exit 1; }
        fi
        export MW_COMPOSITOR=$compositor STRESS_WORK=$work
        if [ -z "$real" ]; then
                # shellcheck disable=SC2086
                exec unshare -rm sh "$here/run.sh" --inside --out "$out" $pass
        fi
fi
if [ -z "$real" ]; then
        mount --make-rprivate / 2> /dev/null
        mount -t tmpfs tmpfs /run || exit 1
        mount -t tmpfs tmpfs /root || exit 1
        mkdir -p /tmp/.X11-unix
        mount -t tmpfs -o mode=1777 tmpfs /tmp/.X11-unix || exit 1
fi
# shellcheck disable=SC2086
exec python3 -I "$here/run.py" --bin "$work/bin" --moonwater "$MW_COMPOSITOR" --work "$work/run" --out "$out" ${real:+--real} $pass
