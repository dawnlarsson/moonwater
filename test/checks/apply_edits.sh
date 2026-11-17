#!/bin/sh
#
#       test/checks/apply_edits.sh: two edits of kernel/patch/apply, run from their own text.
#
#       fold     the header fold, which the two call sites now share through
#                header_fold: the header tree is byte-identical to what the
#                two folds made by hand before it existed
#       lttpr    the amdgpu lttpr edits: a failing sudo stops the script
#                (every settings edit has its || die; these three did not),
#                and a working sudo still makes the edit
#
#       The functions and call blocks are taken from kernel/patch/apply by
#       sed, so the check runs what the script runs. Run from the repository
#       root: sh test/checks/apply_edits.sh. Exits 0 when every row passes.

set -u

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd) || exit 1
apply=${APPLY:-$root/kernel/patch/apply}
work=$(mktemp -d) || exit 1
trap 'rm -rf "$work"' EXIT HUP INT TERM

failed=0
row() {
        if [ "$2" = yes ]; then
                printf 'ok    %s\n' "$1"
        else
                printf 'FAIL  %s\n' "$1"
                failed=1
        fi
}

die() {
        echo "apply: $*" >&2
        exit 1
}

#       sudo here is the script's only use of it: cp, run as this user.
sudo() {
        "$@"
}

#       ---- fold ----------------------------------------------------------
#       The awk each fold ran before header_fold, verbatim, for the reference.
reference_fold() {
        add=$1
        awk -v add="$add" '
                /^#endif \/\* __KERNEL__ \*\/$/ && !placed {
                        while ((getline line < add) > 0)
                                print line
                        placed = 1
                }
                { print }
                END { if (!placed) exit 3 }
        ' "$fold_header" > "$fold_work" &&
        cp "$fold_work" "$fold_header"
}

fold_dir=$work/fold
mkdir -p "$fold_dir/kernel/patch"
cd "$fold_dir" || exit 1
printf '%s\n' '#define MOONWATER_FOLD_X86 1' 'static int a;' > kernel/patch/fold-x86.h
printf '%s\n' '#define MOONWATER_FOLD_X86_FORTIFY 1' '#define MORE 2' > kernel/patch/fold-x86-fortify.h
header='#ifndef __STRING_H__
#define __STRING_H__
#include <linux/types.h>
void *memcpy(void *d, const void *s, unsigned long n);
#endif /* __KERNEL__ */
#endif'
printf '%s\n' "$header" > ref.h
printf '%s\n' "$header" > new.h

#       The reference: the two folds as they were written, in order.
fold_header=$fold_dir/ref.h
fold_work=$fold_dir/ref.work
reference_fold kernel/patch/fold-x86.h
reference_fold kernel/patch/fold-x86-fortify.h

#       The new: the two call blocks, as apply has them now.
fold_header=$fold_dir/new.h
fold_work=$fold_dir/new.work
sed -n '/^header_fold() {/,/^}/p' "$apply" > "$fold_dir/helper.sh"
sed -n '/^if \[ -n "\${fold_header:-}" \]/,/^fi$/p' "$apply" > "$fold_dir/calls.sh"
if [ ! -s "$fold_dir/helper.sh" ] || [ ! -s "$fold_dir/calls.sh" ]; then
        row "fold: apply has header_fold and both call blocks" no
else
        . "$fold_dir/helper.sh"
        . "$fold_dir/calls.sh"
        if cmp -s "$fold_dir/ref.h" "$fold_dir/new.h" &&
           grep -q MOONWATER_FOLD_X86_FORTIFY "$fold_dir/new.h"
        then
                row "fold: the header tree is byte-identical to the two folds written out by hand" yes
        else
                row "fold: the header tree is byte-identical to the two folds written out by hand" no
        fi
fi

#       ---- lttpr ---------------------------------------------------------
lttpr_dir=$work/lttpr
mkdir -p "$lttpr_dir/linux/drivers/gpu/drm/amd/display/dc/link/protocols"
cd "$lttpr_dir" || exit 1
cap=linux/drivers/gpu/drm/amd/display/dc/link/protocols/link_dp_capability.c
cat > "$cap" <<'SOURCE'
	DC_LOG_ERROR("lttpr_caps phy_repeater_cnt is 0x%x, forcing it to 0x80.",
	DC_LOG_ERROR("LTTPR count is nonzero but invalid lane count reported. Assuming no LTTPR present.\n");
SOURCE

sed -n '/^settings_replace() {/,/^}/p' "$apply" > "$lttpr_dir/replace.sh"
sed -n '/^lttpr_cap=/,/^fi$/p' "$apply" > "$lttpr_dir/edit.sh"
settings_work=$lttpr_dir/work
mkdir -p "$settings_work"

#       A sudo that fails: the edit is not made, and the script must stop.
(
        cd "$lttpr_dir" || exit 1
        sudo() { return 1; }
        . "$lttpr_dir/replace.sh"
        . "$lttpr_dir/edit.sh"
        echo "not stopped"
) > "$work/lttpr-failing.out" 2>&1
failing=$?
row "lttpr: a failing sudo stops the script with a message naming the file" "$(
        if [ "$failing" != 0 ] && grep -q "could not edit linux/drivers" "$work/lttpr-failing.out" &&
           ! grep -q "not stopped" "$work/lttpr-failing.out"
        then echo yes; else echo no; fi)"

#       A sudo that works: the first edit is made.
(
        cd "$lttpr_dir" || exit 1
        sudo() { "$@"; }
        . "$lttpr_dir/replace.sh"
        . "$lttpr_dir/edit.sh"
) > "$work/lttpr-working.out" 2>&1
working=$?
row "lttpr: a working sudo makes the edit and the script goes on" "$(
        if [ "$working" = 0 ] && grep -q 'DC_LOG_DC("lttpr_caps phy_repeater_cnt' "$cap" &&
           grep -q 'DC_LOG_DC("LTTPR count is nonzero' "$cap"
        then echo yes; else echo no; fi)"

exit "$failed"
