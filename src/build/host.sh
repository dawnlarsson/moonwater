#!/bin/sh
# shellcheck disable=SC2034 # this file is sourced; its variables are used by the callers
# shellcheck disable=SC3043 # local is supported by every sh this targets (dash, bash, ash)

# printf also works under dash, the /bin/sh used by Debian and Ubuntu.
ANSI=$(printf '\033[')
RESET=$ANSI'0m'
BOLD=$ANSI'1m'
RED=$ANSI'91m'
GREEN=$ANSI'92m'
YELLOW=$ANSI'93m'
BLUE=$ANSI'94m'
MAGENTA=$ANSI'95m'
CYAN=$ANSI'96m'
WHITE=$ANSI'97m'

# Aliases are not expanded inside non-interactive shells on every sh, so these
# are functions. stat takes -c on GNU and -f on BSD/macOS.
if [ "$(uname)" = "Darwin" ]; then
        stat_bytes() { stat -f %z "$@"; }
        stat_mtime() { stat -f %m "$@"; }
else
        stat_bytes() { stat -c %s "$@"; }
        stat_mtime() { stat -c %Y "$@"; }
fi


label() {
        echo $CYAN $BOLD
        # "$@" inside a string splits into one word per argument; $* joins them.
        echo "    $*"
        echo "_____________________________________________________________________________"
        echo $RESET
}

size() {
        local size
        size=$(stat_bytes "$1") || {
                echo "size: cannot stat '$1'" >&2
                return 1
        }

        # bc is not installed everywhere; integer arithmetic is always there.
        echo "$size bytes ($((size / 1024)).$(((size % 1024) * 10 / 1024)) KB, $((size / 1048576)).$(((size % 1048576) * 10 / 1048576)) MB)"
}

is_file() { [ -f "$1" ]; }

# Every path in these scripts is relative to the repository root, so running
# one from anywhere else quietly writes into the wrong place. Checked by
# looking for something only the root has, rather than by the directory's
# name, which was the old test and broke the moment the tree was rearranged.
#
# The source directories and bootstrap identify the repository root.
is_safe() {
        if [ ! -d src ] || [ ! -d kernel ] || [ ! -f build.sh ]; then
                echo "ERROR: not in the repository root." >&2
                echo "These scripts expect to run from the directory holding" >&2
                echo "build.sh, kernel/ and src/. Current directory: $(pwd)" >&2
                exit 1
        fi
}

# Anchored, with a trailing space, so `key pre` cannot also match `#> prefix`.
# shellcheck disable=SC2046 # unquoted on purpose: joins multiple matches into one line
# Multiple matching lines are still joined on purpose -- that is how
# flags accumulates across profiles.
key() {
        # set -f in a subshell: a flag such as -DX=* must not glob against the working directory.
        (set -f; echo $(grep "^#> $1 " artifacts/.config 2>/dev/null | sed "s/^#> $1 //"))
}

# For scalars like compiler or kernel_image, where two profiles both setting
# the value would silently concatenate into an unusable command.
key_one() {
        local matches
        matches=$(grep -c "^#> $1 " artifacts/.config 2>/dev/null || true)

        if [ "${matches:-0}" -gt 1 ]; then
                echo "config: '$1' is set $matches times; expected one value" >&2
                return 1
        fi

        key "$1"
}

# SSH takes shell source, not argv. Keep empty words, quotes and newlines intact.
shell_quote() {
        local argument separator=""
        for argument do
                printf "%s'" "$separator"
                printf '%s' "$argument" | sed "s/'/'\\\\''/g"
                printf "'"
                separator=" "
        done
}

line_has() { is_file "$1" && [ -n "$2" ] && grep -F -q "$2" "$1"; }

line_add() {
        line_has "$1" "$2" && return 0
        echo "$2" >>"$1"
}

line_add_padded() {
        line_has "$1" "$2" && return 0
        echo "" >>"$1"
        echo "$2" >>"$1"
        echo "" >>"$1"
}

# These compared stat_bytes -- the file SIZE -- while being named and used as
# age comparisons. build.sh gates the kernel reconfiguration on is_newer, so a
# config edit that did not grow the file was silently ignored and the previous
# configuration got built.
is_newer() {
        is_file "$1" || return 0
        is_file "$2" || return 0

        # -nt is a widely implemented extension but not POSIX; stat_mtime is
        # already branched per platform above, so compare the times directly.
        [ "$(stat_mtime "$1")" -gt "$(stat_mtime "$2")" ]
}


# is_safe is deliberately not called here. This file used to run it on being
# sourced, which made it a library that refused to load anywhere but the
# repository root -- so anything needing its helpers from elsewhere copied them
# instead, and the size formatter ended up existing three times. Whatever needs
# the guard calls it; build.sh does, first thing.
