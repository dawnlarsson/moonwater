#!/bin/sh
#
#       test/checks/build_publish.sh: build.sh --host, run against stubbed ssh and rsync.
#
#       A checkout of build.sh alone, with a git history of its own, is built
#       with --host. The ssh stub answers the remote steps and hands back an
#       image; the rsync stub stands for the copy and can edit a tracked file
#       while it runs. On Linux build.sh hands the build to its C tool, which
#       has a copy of its own (src/build/build.c); the shell's remote build is
#       the one taken where uname is not Linux, which a uname stub arranges.
#
#       Rows:
#         linux-publish   on Linux the image replaces a symlink to a directory
#         shell-dirty     on the shell path, an edit made before the build is
#                         stamped dirty
#         shell-copy      on the shell path, an edit made and kept during the
#                         copy is refused before anything is published
#         shell-reverted  on the shell path, an edit made and reverted during
#                         the copy is refused, and nothing is published
#
#       Run from the repository root: sh test/checks/build_publish.sh
#       Exits 0 when every row passes.

set -u

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd) || exit 1
work=$(mktemp -d) || exit 1
trap '[ -n "${KEEP:-}" ] || rm -rf "$work"' EXIT HUP INT TERM
nobody=65534

failed=0
row() {
        if [ "$2" = yes ]; then
                printf 'ok    %s\n' "$1"
        else
                printf 'FAIL  %s\n' "$1"
                failed=1
        fi
}

mkdir -p "$work/bin" "$work/shell-bin" "$work/tree"
cp "$root/build.sh" "$work/tree/build.sh"
printf 'notes\n' > "$work/tree/notes.txt"
#       The build tool is compiled from src/build/build.c before the Linux
#       steps, so the source is linked in. Everything else in the tree is
#       ignored, so a linked directory is never a dirty change.
ln -s "$root/src" "$work/tree/src"
ln -s "$root/kernel" "$work/tree/kernel"
(
        cd "$work/tree" &&
        git init -q &&
        printf '%s\n' '/*' '!/build.sh' '!/notes.txt' > .git/info/exclude &&
        git add build.sh notes.txt &&
        GIT_AUTHOR_NAME=check GIT_AUTHOR_EMAIL=check@example.invalid \
        GIT_COMMITTER_NAME=check GIT_COMMITTER_EMAIL=check@example.invalid \
                git commit -q -m check
) || exit 1

#       The build tool is compiled once, as build.sh compiles it, and the
#       runs below copy it as current (cp -p keeps the times build.sh
#       compares), so each run is a build of build.sh and not of the compiler.
(
        cd "$work/tree" &&
        cc -O2 -static -nostdlib -nostartfiles -fno-stack-protector -fno-builtin \
                -w -MMD -MT "$work/tree/build" -MF "$work/tree/build.d" \
                -o build src/build/build.c
) || exit 1

#       ssh answers the remote steps: the kernel's export names the image,
#       and cat of it hands the bytes back. Every command it is given is
#       logged, so a row can read the stamp the build carried.
cat > "$work/bin/ssh" <<'SCRIPT'
#!/bin/sh
printf '%s\n' "$*" >> "$CHECK_LOG"
case "$*" in
*kernel_export*) printf '%s\n' dist/image.efi ;;
*cat*) printf '%s\n' 'built image' ;;
esac
exit 0
SCRIPT

#       rsync is the copy. RSYNC_EDIT says what the copy does to notes.txt
#       while it runs: nothing, an edit that stays, or an edit that is
#       reverted before the copy ends.
cat > "$work/bin/rsync" <<'SCRIPT'
#!/bin/sh
printf '%s\n' rsync >> "$CHECK_LOG"
case "${RSYNC_EDIT:-none}" in
keep)
        printf '%s\n' edited >> notes.txt ;;
revert)
        cp notes.txt "$CHECK_WORK/notes.saved"
        printf '%s\n' edited >> notes.txt
        cp "$CHECK_WORK/notes.saved" notes.txt ;;
esac
exit 0
SCRIPT

#       The shell path is taken where uname is not Linux: this stub says
#       Darwin and passes every other question to the real uname.
cat > "$work/shell-bin/uname" <<'SCRIPT'
#!/bin/sh
case "$*" in
"" | -s) echo Darwin ;;
*) exec /usr/bin/uname "$@" ;;
esac
SCRIPT
ln -s "$work/bin/ssh" "$work/shell-bin/ssh"
ln -s "$work/bin/rsync" "$work/shell-bin/rsync"
chmod +x "$work/bin/ssh" "$work/bin/rsync" "$work/shell-bin/uname"

#       run NAME BIN EDIT DIRTY: a fresh build of the checkout with the stubs
#       in BIN on the PATH, and a dist that holds a symlink to a directory
#       where the image goes. DIRTY=yes makes an uncommitted edit before the
#       build.
run() {
        name=$1 bin=$2 edit=$3 dirty=$4
        tree=$work/$name
        rm -rf "$tree"
        cp -Rp "$work/tree" "$tree"
        mkdir -m 700 "$tree/dist" "$tree/dist/target" || exit 1
        ln -s target "$tree/dist/image.efi"
        if [ "$dirty" = yes ]; then
                printf '%s\n' uncommitted >> "$tree/notes.txt"
        fi
        CHECK_LOG=$work/$name.log
        CHECK_WORK=$work
        : > "$CHECK_LOG"
        #       build.sh, run as root, resets PATH to the system's, which
        #       would pass over the stubs. As nobody it keeps the caller's
        #       PATH, and the trees it writes into are nobody's.
        chown -R "$nobody:$nobody" "$work" && chmod 755 "$work" || exit 1
        (
                cd "$tree" &&
                setpriv --reuid="$nobody" --regid="$nobody" --clear-groups \
                        env HOME="$work" PATH="$bin:$PATH" RSYNC_EDIT="$edit" \
                        CHECK_LOG="$CHECK_LOG" CHECK_WORK="$work" \
                        sh build.sh --host stubhost --arch x64 > "$work/$name.out" 2>&1
        )
        printf '%s\n' "$?" > "$work/$name.status"
}

run linux-publish "$work/bin" none no
row "linux-publish: on Linux the image replaces a symlink to a directory, and the directory is left alone" "$(
        if [ "$(cat "$work/linux-publish.status")" = 0 ] &&
           [ -f "$work/linux-publish/dist/image.efi" ] && [ ! -L "$work/linux-publish/dist/image.efi" ] &&
           [ -d "$work/linux-publish/dist/target" ] && [ ! -L "$work/linux-publish/dist/target" ]
        then echo yes; else echo no; fi)"

#       The stamp is what the log of the remote build carries; the publish
#       after it is not what this row is about.
run shell-dirty "$work/shell-bin" none yes
row "shell-dirty: on the shell path, an edit made before the build is stamped dirty" "$(
        if grep -q 'MOONWATER_COMMIT_DIRTY=1' "$work/shell-dirty.log"
        then echo yes; else echo no; fi)"

run shell-copy "$work/shell-bin" keep no
row "shell-copy: on the shell path, an edit made and kept during the copy is refused, and nothing is published" "$(
        if [ "$(cat "$work/shell-copy.status")" != 0 ] &&
           grep -q 'changed while it was copied' "$work/shell-copy.out" &&
           [ -L "$work/shell-copy/dist/image.efi" ]
        then echo yes; else echo no; fi)"

run shell-reverted "$work/shell-bin" revert no
row "shell-reverted: on the shell path, an edit made and reverted during the copy is refused, and nothing is published" "$(
        if [ "$(cat "$work/shell-reverted.status")" != 0 ] &&
           grep -q 'changed while it was copied' "$work/shell-reverted.out" &&
           [ -L "$work/shell-reverted/dist/image.efi" ]
        then echo yes; else echo no; fi)"

exit "$failed"
