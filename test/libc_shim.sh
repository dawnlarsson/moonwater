#!/bin/sh
# test/libc_shim.sh [bench]: src/shim/libc.c built, held to its rules, and held against both C libraries, on a machine with the
# alpine (musl) and arch (glibc) bowls. Run from the root of a tree (the box: after the tree is unpacked).
#   1. No C: the shim compiled to LLVM IR has no function body of its own (every routine is lib.c's or the shim's assembly).
#   2. The library: no undefined name, and exactly the names src/shim/libc.map gives.
#   3. test/libc_shim.c, built in each bowl against that bowl's C library, run there with the library beside it.
#   bench: and each side's ticks a call for the hot names. LIBC_SHIM_OUT=FILE keeps the library built and tested.
set -u
# One directory the machine and its bowls see at the same path: a bowl's /root is the machine's, and its /tmp, /var/tmp
# and the rest of its tree are its own (and read only). bowl finds the program it is given in the bowl's own tree, so a
# program kept here is started by the bowl's sh.
in="$HOME/.cache/libc_shim.$$"
mkdir -p "$in/src/shim" "$in/test" || exit 2
trap 'rm -rf "$in"' EXIT
work="$in"
cc_alpine="bowl /bowls/alpine /usr/bin/clang"
flags="-target x86_64-linux-gnu -std=gnu11 -O2 -fPIC -ffreestanding -fno-builtin -fno-stack-protector -fvisibility=hidden -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-ident -ffunction-sections -fdata-sections"
cp src/lib.c "$in/src/" && cp src/shim/libc.c src/shim/libc.map "$in/src/shim/" && cp test/libc_shim.c "$in/test/" || exit 2

$cc_alpine $flags -S -emit-llvm -o "$in/libc.ll" "$in/src/shim/libc.c" 2> "$work/ir.log" || { echo "libc_shim: FAIL the IR did not build"; head -20 "$work/ir.log"; exit 1; }
bodies=$(grep -c '^define ' "$in/libc.ll")
[ "$bodies" = 0 ] || { echo "libc_shim: FAIL $bodies C function bodies in the shim:"; grep '^define ' "$in/libc.ll" | head; exit 1; }
echo "libc_shim: no C (0 function bodies in the IR)"

$cc_alpine $flags -shared -nostdlib -fuse-ld=lld -Wl,-soname,libmoonwater-c.so,--version-script=$in/src/shim/libc.map,--gc-sections,-z,now,-Bsymbolic,--hash-style=both,--build-id=none,-s \
        -o "$in/libmoonwater-c.so" "$in/src/shim/libc.c" 2> "$work/so.log" || { echo "libc_shim: FAIL the library did not build"; head -20 "$work/so.log"; exit 1; }
so="$in/libmoonwater-c.so"
undefined=$(nm -D "$so" | awk '$1 == "U" {print $2}')
[ -z "$undefined" ] || { echo "libc_shim: FAIL undefined names: $undefined"; exit 1; }
nm -D --defined-only "$so" | awk '{print $3}' | sort > "$work/exported"
sed -n '/global:/,/local:/p' src/shim/libc.map | sed 's|/\*.*\*/||g; s/global://; s/local://' | tr ';' '\n' | tr -d ' \t' | grep . | sort > "$work/mapped"
diff "$work/mapped" "$work/exported" > "$work/names.diff" || { echo "libc_shim: FAIL exports differ from src/shim/libc.map (< map, > library):"; cat "$work/names.diff"; exit 1; }
echo "libc_shim: $(wc -l < "$work/exported") names, none undefined, $(stat -c %s "$so") bytes, sha256 $(sha256sum "$so" | cut -c1-16)"
# LIBC_SHIM_OUT=FILE keeps the library that was tested, the one src/bowl.c carries.
[ -n "${LIBC_SHIM_OUT:-}" ] && cp "$so" "$LIBC_SHIM_OUT"

status=0
bowl /bowls/alpine /usr/bin/cc -O1 -o "$in/t-alpine" "$in/test/libc_shim.c" 2>&1 | head -20
bowl /bowls/arch /usr/bin/cc -O1 -o "$in/t-arch" "$in/test/libc_shim.c" -ldl 2>&1 | head -20
for b in alpine arch; do
        [ -x "$in/t-$b" ] || { echo "libc_shim: FAIL the test did not build in $b"; status=1; continue; }
        printf 'libc_shim: %s (%s) ' "$b" "$( [ $b = alpine ] && echo musl || echo glibc)"
        bowl /bowls/$b /bin/sh -c "exec $in/t-$b $in/libmoonwater-c.so" > "$work/$b.out" 2>&1 || status=1
        tail -62 "$work/$b.out"
        [ "${1:-}" = bench ] && taskset -c 2 bowl /bowls/$b /bin/sh -c "exec $in/t-$b $in/libmoonwater-c.so bench"
done
exit $status
