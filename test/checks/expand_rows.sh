#!/bin/sh
#       test/checks/expand_rows.sh: the expansion rows of src/sh/expand.c, run by a freestanding build of the shell.
#
#       sh test/checks/expand_rows.sh SHELL
#
#       SHELL is programs/shell.c built as test/run builds it (freestanding, -T src/build/spark.ld). Each row runs
#       under the name bash, as the lanes do, in a directory of its own, and is held to the value bash gives. A row
#       that fails prints what it expected and what it got. The last line is the count, and the status is 1 when
#       any row failed, 2 when the rows could not be set up.
#
#       Rows: a multibyte IFS past eight characters (the byte table fell through to the lead byte), a glob 65 levels
#       deep (a depth cap refused it), an associative key and a variable name of 40 and 200 bytes under (( = )) (the
#       fixed name and key buffers refused them), a chain of 33 names that expand to one another (a count of names
#       refused it, with no reason given), and a command substitution whose pipe fails (its status was never set).

shell=$1
if [ -z "$shell" ] || [ ! -x "$shell" ]; then
        echo "usage: sh test/checks/expand_rows.sh SHELL" >&2
        exit 2
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/expand_rows.XXXXXX") || exit 2
trap 'rm -rf "$work"' EXIT
trap 'exit 130' INT TERM HUP
mkdir "$work/bin" || exit 2
ln -s "$shell" "$work/bin/bash" || exit 2
bash_=$work/bin/bash

rows=0
failures=0

#       One row: its name, the value bash gives and the value this shell gave.
row()
{
        rows=$((rows + 1))
        if [ "$3" != "$2" ]; then
                failures=$((failures + 1))
                printf 'FAIL %s: expected [%s] got [%s]\n' "$1" "$2" "$3"
        fi
}

#       Multibyte IFS: nine two-byte characters, U+00E0 to U+00E8 (C3 A0 to C3 A8), all sharing the lead byte C3.
#       Each is one separator. U+00E9 (C3 A9) is not in the set, so it must stay inside one field.
nine=$(printf '\303\240\303\241\303\242\303\243\303\244\303\245\303\246\303\247\303\250')
e_acute=$(printf '\303\251')
a_grave=$(printf '\303\240')

actual=$(env LC_ALL=C.UTF-8 LANG=C.UTF-8 nine="$nine" v="x${e_acute}y" "$bash_" -c \
        'IFS=$nine; set -- $v; echo $#')
row "multibyte IFS keeps e-acute in one field" 1 "$actual"

actual=$(env LC_ALL=C.UTF-8 LANG=C.UTF-8 nine="$nine" v="x${a_grave}y" "$bash_" -c \
        'IFS=$nine; set -- $v; echo "$#:$1:$2"')
row "multibyte IFS splits at a-grave in the ninth set" "2:x:y" "$actual"

#       A glob 65 levels deep: 65 directories named d, a file hit at the bottom, and a pattern of 65 stars.
deep=$work/deep
path=
pattern=
level=0
while [ "$level" -lt 65 ]; do
        path="${path}d/"
        pattern="${pattern}*/"
        level=$((level + 1))
done
mkdir -p "$deep/${path}" || exit 2
: > "$deep/${path}hit" || exit 2
actual=$(cd "$deep" && env P="${pattern}hit" "$bash_" -c 'set -- $P; echo "$#:$1"')
row "a glob 65 levels deep lists its match" "1:${path}hit" "$actual"

#       An associative key of 40 bytes, and a variable name of 200 bytes, under (( = )).
key=
level=0
while [ "$level" -lt 40 ]; do
        key="${key}k"
        level=$((level + 1))
done
actual=$("$bash_" -c "declare -A m; k=$key; (( m[\$k] = 2 )); echo \"\${m[\$k]}\"" 2>&1)
row "a 40-byte associative key under (( = )) stores 2" 2 "$actual"

name=
level=0
while [ "$level" -lt 200 ]; do
        name="${name}v"
        level=$((level + 1))
done
actual=$("$bash_" -c "(( $name = 3 )); eval \"echo \\\$$name\"" 2>&1)
row "a 200-byte name under (( = )) accepts 3" 3 "$actual"

#       A chain of names, a1=a2 ... aN-1=aN and aN=7, read through a1: bash prints 7. Thirty-three names is the
#       brief's row; thirty-four is the first chain the old count of 32 nested names refused.
chain_names()
{
        chain=
        level=1
        while [ "$level" -lt "$1" ]; do
                chain="${chain}a${level}=a$((level + 1)); "
                level=$((level + 1))
        done
        chain="${chain}a$1=7; echo \$((a1))"
}
chain_names 33
actual=$("$bash_" -c "$chain" 2>&1)
row "a chain of 33 names expands to 7" 7 "$actual"
chain_names 34
actual=$("$bash_" -c "$chain" 2>&1)
row "a chain of 34 names expands to 7" 7 "$actual"

#       A command substitution whose pipe cannot be made: no descriptor is left for it. The status is 125, the
#       status a failed wait gives, and is what $? must hold after the assignment.
actual=$("$bash_" -c 'ulimit -n 3; x=$(true); echo "$?"' 2>/dev/null)
row "a command substitution whose pipe fails sets status 125" 125 "$actual"

echo "$rows rows, $failures failures"
[ "$failures" -eq 0 ]
