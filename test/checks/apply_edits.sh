#!/bin/sh
#
#       test/checks/apply_edits.sh: edits of kernel/patch/apply, run from their own text.
#
#       fold     the header fold, which the two call sites now share through
#                header_fold: the header tree is byte-identical to what the
#                two folds made by hand before it existed
#       lttpr    the amdgpu lttpr edits: a failing sudo stops the script
#                (every settings edit has its || die; these three did not),
#                and a working sudo still makes the edit
#       wave     the gfx10 wave limit: its three additions land in the compute
#                ring's funcs and nowhere else, once, and a moved line stops it
#       start    the scheduler's new-task edit leaves new threads out
#                (thread_group_leader), in a fresh tree and in one patched
#                with the text from before, which gets the line and is then
#                found applied rather than stopping the build
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

#       ---- gfx10 wave limit ----------------------------------------------
#       A gfx_v10_0.c of 7.4's shape where it counts: the gfx ring's funcs and
#       the compute ring's end their frame sizes with the same line and share
#       the mem_sync hook, so an edit placed by the first match lands in the
#       wrong struct; only the compute ring has the reset_kcq hook.
wave_dir=$work/wave
mkdir -p "$wave_dir/linux/drivers/gpu/drm/amd/amdgpu" "$wave_dir/work"
wave_c=$wave_dir/linux/drivers/gpu/drm/amd/amdgpu/gfx_v10_0.c
tab=$(printf '\t')
cat > "$wave_dir/shape.c" <<SOURCE
static const struct amdgpu_ring_funcs gfx_v10_0_ring_funcs_gfx = {
${tab}.emit_frame_size =
${tab}${tab}8 + /* gfx_v10_0_emit_mem_sync */
${tab}${tab}2, /* gfx_v10_0_ring_emit_cleaner_shader */
${tab}.emit_mem_sync = gfx_v10_0_emit_mem_sync,
};

static const struct amdgpu_ring_funcs gfx_v10_0_ring_funcs_compute = {
${tab}.emit_frame_size =
${tab}${tab}8 + /* gfx_v10_0_emit_mem_sync */
${tab}${tab}2, /* gfx_v10_0_ring_emit_cleaner_shader */
${tab}.emit_mem_sync = gfx_v10_0_emit_mem_sync,
${tab}.reset = gfx_v10_0_reset_kcq,
${tab}.emit_cleaner_shader = gfx_v10_0_ring_emit_cleaner_shader,
};
SOURCE
sed -n '/^settings_insert() {/,/^}/p' "$apply" > "$wave_dir/insert.sh"
sed -n '/^gfx10=linux/,/^fi$/p' "$apply" > "$wave_dir/edit.sh"
wave_run() {
        (
                cd "$wave_dir" || exit 1
                settings_work=$wave_dir/work
                sudo() { "$@"; }
                . "$wave_dir/insert.sh"
                . "$wave_dir/edit.sh"
        )
}
cp "$wave_dir/shape.c" "$wave_c"
wave_run > "$work/wave-1.out" 2>&1
wave_first=$?
cp "$wave_c" "$wave_dir/once.c"
wave_run > "$work/wave-2.out" 2>&1
wave_second=$?
compute_at=$(grep -n '^static const struct amdgpu_ring_funcs gfx_v10_0_ring_funcs_compute = {' "$wave_dir/once.c" | cut -d: -f1)
row "gfx10 wave limit: the function, its hook and forty dwords of frame are added" "$(
        if [ "$wave_first" = 0 ] && grep -q '^static void gfx_v10_0_emit_wave_limit(struct amdgpu_ring \*ring, bool enable)$' "$wave_dir/once.c" &&
           grep -q "^${tab}.emit_wave_limit = gfx_v10_0_emit_wave_limit,\$" "$wave_dir/once.c" &&
           [ "$(grep -c "^${tab}${tab}40 + /\* Moonwater: gfx_v10_0_emit_wave_limit, twice \*/\$" "$wave_dir/once.c")" = 1 ]
        then echo yes; else echo no; fi)"
row "gfx10 wave limit: every addition is in the compute ring's funcs, the gfx ring's are untouched" "$(
        func_at=$(grep -n '^static void gfx_v10_0_emit_wave_limit' "$wave_dir/once.c" | cut -d: -f1)
        frame_at=$(grep -n 'Moonwater: gfx_v10_0_emit_wave_limit, twice' "$wave_dir/once.c" | cut -d: -f1)
        hook_at=$(grep -n 'emit_wave_limit = gfx_v10_0_emit_wave_limit' "$wave_dir/once.c" | cut -d: -f1)
        sed -n '1,/^};$/p' "$wave_dir/shape.c" > "$wave_dir/gfx.ref"
        sed -n '1,/^};$/p' "$wave_dir/once.c" > "$wave_dir/gfx.now"
        if [ -n "$compute_at" ] && [ -n "$func_at" ] && [ -n "$frame_at" ] && [ -n "$hook_at" ] &&
           [ "$func_at" -lt "$compute_at" ] && [ "$frame_at" -gt "$compute_at" ] && [ "$hook_at" -gt "$compute_at" ] &&
           cmp -s "$wave_dir/gfx.ref" "$wave_dir/gfx.now"
        then echo yes; else echo no; fi)"
row "gfx10 wave limit: a second run changes nothing" "$(
        if [ "$wave_second" = 0 ] && cmp -s "$wave_dir/once.c" "$wave_c"; then echo yes; else echo no; fi)"
#       The compute ring's frame no longer ends on the cleaner shader's line
#       (the gfx ring's still does): the edit must stop, not land in the gfx ring.
awk -v t="$tab" '/gfx_v10_0_ring_funcs_compute/ { c = 1 } c && index($0, t t "2, /* gfx_v10_0_ring_emit_cleaner_shader */") == 1 { print t t "2, /* moved */"; next } { print }' "$wave_dir/shape.c" > "$wave_c"
wave_run > "$work/wave-moved.out" 2>&1
wave_moved=$?
row "gfx10 wave limit: a compute frame that no longer ends where the edit is placed stops the script" "$(
        if [ "$wave_moved" != 0 ] && grep -q "the compute ring's frame size no longer ends where this edit is placed" "$work/wave-moved.out" &&
           ! grep -q 'Moonwater: gfx_v10_0_emit_wave_limit, twice' "$wave_c"
        then echo yes; else echo no; fi)"

#       ---- start ---------------------------------------------------------
start_dir=$work/start
mkdir -p "$start_dir/linux/kernel/sched" "$start_dir/work"
cd "$start_dir" || exit 1
tab=$(printf '\t')
settings_work=$start_dir/work
sed -n '/^settings_replace() {/,/^}/p' "$apply" > "$start_dir/replace.sh"
sed -n '/^settings_insert() {/,/^}/p' "$apply" > "$start_dir/insert.sh"
sed -n '/^cat > "\$settings_work\/start" <<EOF$/,/"\$settings_work\/start" scheduler$/p' "$apply" > "$start_dir/edit.sh"
fair=linux/kernel/sched/fair.c
new_line="${tab}     ((wake_flags & WF_FORK) && thread_group_leader(p) && this_rq()->nr_running <= 1)) &&"
old_line="${tab}     ((wake_flags & WF_FORK) && this_rq()->nr_running <= 1)) &&"
printf 'static int select_task_rq_fair(struct task_struct *p, int prev_cpu, int wake_flags)\n{\n%slockdep_assert_held(&p->pi_lock);\n%sreturn cpu;\n}\n' "$tab" "$tab" > "$fair"
(
        . "$start_dir/replace.sh"
        . "$start_dir/insert.sh"
        . "$start_dir/edit.sh"
) > "$work/start-fresh.out" 2>&1
fresh=$?
row "start: a fresh tree's new-task edit starts only a new process where it was made, not a new thread" "$(
        if [ "$fresh" = 0 ] && [ "$(grep -c -F -- "$new_line" "$fair")" = 1 ] && ! grep -q -F -- "$old_line" "$fair"
        then echo yes; else echo no; fi)"
#       The tree patched before: the block as it was, the old line in it.
printf 'static int select_task_rq_fair(struct task_struct *p, int prev_cpu, int wake_flags)\n{\n%slockdep_assert_held(&p->pi_lock);\n' "$tab" > "$fair"
sed -n '/^cat > "\$settings_work\/start" <<EOF$/,/^EOF$/p' "$apply" | sed '1d;$d' | sed "s/\${tab}/$tab/g" |
        awk -v old="$old_line" -v new="$new_line" '$0 == new { print old; next } { print }' >> "$fair"
printf '%sreturn cpu;\n}\n' "$tab" >> "$fair"
had_old=$(grep -c -F -- "$old_line" "$fair")
(
        . "$start_dir/replace.sh"
        . "$start_dir/insert.sh"
        . "$start_dir/edit.sh"
        . "$start_dir/edit.sh"
) > "$work/start-stale.out" 2>&1
stale=$?
row "start: a tree patched with the old text gets the line, and the edit (run twice) is then found applied" "$(
        if [ "$had_old" = 1 ] && [ "$stale" = 0 ] && [ "$(grep -c -F -- "$new_line" "$fair")" = 1 ] && ! grep -q -F -- "$old_line" "$fair" &&
           [ "$(grep -c -F 'Moonwater: a new task starts on the CPU' "$fair")" = 1 ]
        then echo yes; else echo no; fi)"

exit "$failed"
