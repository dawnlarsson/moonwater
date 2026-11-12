#!/bin/sh
#
#       Bootstrap for the build tool, and the half of it a Mac can run.
#
#       Usage:
#           sh build.sh                       build with the default profiles
#           sh build.sh arch/x64 debug_none   build with the profiles named
#           sh build.sh --run                 build, then boot it in a window
#           sh build.sh --run --shell         boot with the console on this terminal
#           sh build.sh --boot                boot the last image, do not rebuild
#           sh build.sh --usb                 build, then write a USB stick
#           sh build.sh --usb-update DIR      build, then copy it onto the FAT stick
#                                             mounted at DIR, erasing nothing
#           sh build.sh --clean               remove what a build produced
#           sh build.sh --host box            build on another machine over ssh
#           sh build.sh --arch arm64 --run    build and boot another architecture
#           sh build.sh --run --ram 8         boot with 8 GB of memory (default 4)
#           sh build.sh --env check           report the local x86-64 build environment
#           sh build.sh --env setup [--yes]   wrappers, and brew installs with --yes
#           sh build.sh --env prove           link an x86-64 program and run it under the VM
#
#       Where a build goes when no --host is named: on a Mac whose Lima VM
#       "default" is running, the VM (host lima-default), which has the Linux
#       and the case-sensitive disk a kernel build needs. Its ssh settings are
#       read from
#       $LIMA_HOME/default/ssh.config, LIMA_HOME defaulting to ~/.lima, the
#       same place limactl reads them. MOONWATER_NO_LIMA=1 turns that off, and
#       then a Mac without --host refuses to build. --host always wins.
#
#       Memory is what a live image keeps its bowls in, so it is the room a
#       setup has: Arch's tree is 0.6 GB on x86-64 and 2.1 GB on arm64, with
#       the download beside it, and a tmpfs holds half of the memory by default.
#
#       The architecture defaults to the machine that will run the image. A
#       build that boots (--run, --boot) is for this machine, so on an Apple
#       Silicon Mac it is arm64 and boots in a native arm64 VM under hvf, even
#       when the compiling happens on an x86 box over --host. A build that only
#       builds is for the machine doing it: the build host's own architecture.
#       --arch x64, arm64 or riscv64 (also x86_64, amd64, aarch64, arm, riscv)
#       picks one, and an arch/ profile named on the line wins over both.
#
#       The build itself is one C program, src/build/build.c, built on this
#       project's own freestanding stack. This file compiles it with one
#       command and hands over. That command is
#
#           cc -O2 -static -nostdlib -nostartfiles -fno-stack-protector \
#              -fno-builtin -w -o build src/build/build.c
#
#       (clang wants -fdefer-ts as well), and it is the whole of what a bare
#       machine needs: a C compiler and an
#       assembler, nothing linked and no library required. Run it yourself and
#       use ./build directly if you would rather not go through this file.
#
#       Why this file still exists, and why it still has shell in it below:
#       src/lib.c is assembly wearing ELF clothes. ASM_FUNC emits .type
#       and .size and names symbols without a leading underscore, so the stack
#       does not assemble under Mach-O and the tool cannot be built on a Mac
#       at all. Everything a Mac actually does here -- drive a build on another
#       machine, boot the image it fetched back, write a stick, clean up --
#       needs none of the stack, so it stays here in shell rather than growing
#       a second implementation of the whole tool in C that no machine would
#       ever run. On Linux, which is where a kernel is built, nothing below the
#       exec runs.
#
# shellcheck disable=SC2154
# shellcheck disable=SC1091
set -e

# Root must not resolve even the bootstrap's first utility through a caller
# supplied PATH.  This runs before dirname, uname, find or the compiler.  The
# Linux build wants root later, and sudo commonly preserves enough environment
# for a writable PATH to otherwise become code execution before the C tool has
# a chance to apply its own policy.
bootstrap_uid=$(/usr/bin/id -u 2>/dev/null || /bin/id -u) || exit 1
if [ "$bootstrap_uid" = 0 ]; then
        PATH=/usr/sbin:/usr/bin:/sbin:/bin
        export PATH
fi

# Sourced and compiled by this file's own path rather than a relative one, so
# that being in the wrong directory produces is_safe's explanation rather than
# a bare "No such file or directory". The working directory is deliberately
# left alone: every path a build reads is relative to it, and a test that
# builds a fixture tree elsewhere and runs this file from the repository
# depends on that.
# shellcheck disable=SC1007
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

if [ "$(uname)" = "Linux" ]; then
        tool=$here/build
        depfile=$tool.d
        source=$here/src/build/build.c
        compiler=${CC:-cc}

        # CC is useful to an ordinary build, but root must not be pointed at an
        # arbitrary pathname by inherited environment. Root takes clang when the
        # reset PATH has it: the tool is one program that includes the shell,
        # and clang builds it in about a third of the time gcc takes. Anyone
        # needing another compiler can build ./build explicitly before elevating.
        if [ "$bootstrap_uid" = 0 ]; then
                compiler=cc
                #       Only a clang that takes the defer statement: the tool has
                #       scope_exit in it (src/lib.util.c), and a clang that does not
                #       take -fdefer-ts (Ubuntu's 18 and 19) failed every build root
                #       started, on a machine whose cc would have built it.
                command -v clang >/dev/null 2>&1 &&
                        printf 'int x;\n' | clang -fdefer-ts -x c -c -o /dev/null - 2> /dev/null &&
                        compiler=clang
        fi

        # The tool is built again when it is missing, when the compiler's own
        # list of the files it was built from names one that is newer than the
        # tool or gone, or when build.sh is newer. The list is the compiler's
        # (-MMD), so an edit to a file the tool does not include costs nothing.
        tool_stale() {
                for dep in $(sed -e 's/\\$//' -e 's/^[^:]*://' "$depfile"); do
                        [ -e "$dep" ] || return 0
                        [ -n "$(find "$dep" -newer "$tool" -print -quit)" ] && return 0
                done
                [ -n "$(find "$here/build.sh" -newer "$tool" -print -quit)" ]
        }

        # Root does not exec a tool anyone else can write, whatever its
        # timestamp says: an old user-writable ./build with a future timestamp
        # would otherwise run unchanged under sudo. Only a file root owns, and
        # that no group or other can write, is taken as built.
        tool_untrusted() {
                [ -n "$(find "$tool" \( ! -user 0 -o -perm -020 -o -perm -002 \) -print -quit)" ]
        }

        if [ ! -x "$tool" ] || [ ! -f "$depfile" ] || tool_stale ||
                { [ "$bootstrap_uid" = 0 ] && tool_untrusted; }; then
                # Built beside it and renamed over, so a second build.sh
                # started meanwhile runs the old tool or the new one and
                # never a half-written file. clang has no nested functions,
                # and scope_exit is its defer statement, which it takes when
                # asked (src/lib.util.c).
                #       Probed, not assumed: some clang builds (Ubuntu's 19, for one) do not
                #       take -fdefer-ts, and test/run probes the same way.
                defer=
                case $("$compiler" --version 2> /dev/null) in
                *clang*)
                        printf 'int x;\n' | "$compiler" -fdefer-ts -x c -c -o /dev/null - 2> /dev/null &&
                                defer=-fdefer-ts
                        ;;
                esac
                # On Linux aarch64 the build tool is freestanding, so the atomics
                # must be inline: -nostdlib leaves libgcc's outline helpers out.
                outline=
                case $(uname -sm) in
                "Linux aarch64" | "Linux arm64") outline=-mno-outline-atomics ;;
                esac
                "$compiler" -O2 -static -nostdlib -nostartfiles $outline \
                        -fno-stack-protector -fno-builtin -w $defer \
                        -MMD -MT "$tool" -MF "$depfile.$$" \
                        -o "$tool.$$" "$source" &&
                        mv -f "$depfile.$$" "$depfile" &&
                        mv -f "$tool.$$" "$tool" ||
                        {
                                rm -f "$tool.$$" "$depfile.$$"
                                echo "build.sh: could not build the build tool" >&2
                                exit 1
                        }
        fi

        exec "$tool" "$@"
fi

#
#       Everything below here is the Mac.
#
# shellcheck disable=SC1091
. "$here/src/build/host.sh"

is_safe

die() {
        echo "$RED""build failed: $*""$RESET" >&2
        exit 1
}

say() { printf '%s%s%s\n' "$CYAN$BOLD" "$*" "$RESET"; }

usage() {
        sed -n '/^#       Usage:/,/^#       The build itself/p' "$here/build.sh" |
                sed '$d' | sed 's/^#       //; s/^#$//'
}

#
#       Architectures, by the three names each has: what --arch and uname say,
#       the profile that builds it, and the image that profile exports.
#
arch_name() {
        case $1 in
        x64 | x86_64 | amd64 | x86-64) echo x64 ;;
        arm64 | aarch64 | arm) echo arm64 ;;
        riscv64 | riscv) echo riscv64 ;;
        *) return 1 ;;
        esac
}

arch_profile() {
        case $1 in
        x64) echo arch/x64 ;;
        arm64) echo arch/arm ;;
        riscv64) echo arch/riscv ;;
        esac
}

arch_of_profile() {
        case $1 in
        arch/x64*) echo x64 ;;
        arch/arm*) echo arm64 ;;
        arch/riscv*) echo riscv64 ;;
        esac
}

arch_image() {
        case $1 in
        x64) echo dist/bootx64.efi ;;
        arm64) echo dist/bootaa64.efi ;;
        riscv64) echo dist/bootriscv64.efi ;;
        esac
}

#
#       Arguments.
#
#       Anything that is not an option is a profile name, so the two can be
#       mixed in any order: sh build.sh --run desktop.
#
host=${MOONWATER_BUILD_HOST:-}
host_given=0
#
#       One build directory per source tree and architecture, not one per
#       machine. The suffix is a checksum of this tree's own path, so the same
#       checkout always gets the same directory and two checkouts never share
#       one; the architecture's profile comes last, so switching between arm64
#       and x64 does not throw one kernel build away for the other. The tool
#       computes the same name with the same checksum, so a directory made by
#       one is found by the other.
#
#       It lives under the build host's ~/.cache, relative to the home an ssh
#       command starts in, rather than /tmp: the build runs as root and leaves
#       a couple of gigabytes of root-owned files, and on a box whose /tmp is
#       a RAM disk those filled it and could not be removed without sudo.
#
tree_mark=$(printf '%s' "$here" | cksum | cut -d' ' -f1)
remote=${MOONWATER_BUILD_DIR:-}
arch_asked=""
do_run=0
do_build=1
do_clean=0
do_usb=0
do_usb_update=0
usb_dir=""
console=0
image=""
ram=4

# Whole gigabytes, 1 to 4096, as "8" or "8G"; says the number or fails.
ram_gigs() {
        gigs=${1%[Gg]}
        case "$gigs" in
        '' | *[!0-9]* | 0*) return 1 ;;
        esac
        [ "${#gigs}" -le 4 ] && [ "$gigs" -le 4096 ] || return 1
        printf '%s' "$gigs"
}

#       --env check|setup|prove sets up and proves the local build environment for other
#       architectures on this machine, with no service: the toolchains are the
#       machine's own (Homebrew's llvm, lld and qemu, the Lima VM already here).
#       check reports what is present; setup installs what is missing, but only with
#       --yes, and otherwise says what it would run; prove compiles a x86-64 program,
#       links it with lld and runs it under the VM's qemu-user. Nothing under the
#       source tree is written, and the wrappers live in ~/.cache/mw-x86-proof.
mw_env_dir=$HOME/.cache/mw-x86-proof
mw_env_lld=/opt/homebrew/opt/lld/bin/ld.lld
mw_env_clang=/opt/homebrew/opt/llvm/bin/clang

env_say() { printf '%s\n' "$*"; }

env_check()
{
        env_missing=0
        for formula in llvm lld qemu; do
                if brew list --versions "$formula" >/dev/null 2>&1; then
                        env_say "ok       brew $formula"
                else
                        env_say "missing  brew $formula"
                        env_missing=1
                fi
        done
        if [ -x "$mw_env_lld" ]; then env_say "ok       ld.lld"; else env_say "missing  ld.lld"; env_missing=1; fi
        if limactl list 2>/dev/null | grep -q 'default.*Running.*aarch64'; then
                env_say "ok       Lima VM default is running"
                if limactl shell default -- bash -lc 'command -v qemu-x86_64 >/dev/null' 2>/dev/null; then
                        env_say "ok       qemu-x86_64 in the VM"
                else
                        env_say "missing  qemu-x86_64 in the VM"
                        env_missing=1
                fi
        else
                env_say "missing  Lima VM default (limactl start default)"
                env_missing=1
        fi
        [ "$env_missing" = 0 ]
}

env_setup()
{
        for formula in llvm lld qemu; do
                brew list --versions "$formula" >/dev/null 2>&1 && continue
                if [ "$env_yes" = 1 ]; then
                        env_say "installing brew $formula"
                        brew install "$formula" || return 1
                else
                        env_say "would run: brew install $formula   (add --yes to run it)"
                fi
        done
        mkdir -p "$mw_env_dir/bin" || return 1
        for arch in x86_64 aarch64 riscv64; do
                printf '%s\n' '#!/bin/sh' \
                        "exec limactl shell --workdir \"\$PWD\" default -- qemu-$arch \"\$@\"" \
                        > "$mw_env_dir/bin/qemu-$arch" || return 1
                chmod +x "$mw_env_dir/bin/qemu-$arch"
        done
        env_say "wrappers in $mw_env_dir/bin (qemu-x86_64, qemu-aarch64, qemu-riscv64)"
}

env_prove()
{
        mkdir -p "$mw_env_dir" || return 1
        cd "$mw_env_dir" || return 1
        cat > prove.c <<'PROVE'
void _start(void) { __asm__ volatile("mov $60, %eax\n mov $7, %edi\n syscall"); }
PROVE
        "$mw_env_clang" --target=x86_64-linux-gnu -O2 -static -nostdlib -fuse-ld=lld \
                -o prove prove.c || {
                env_say "FAIL  x86-64 link with lld"; return 1; }
        env_say "ok       x86-64 ELF linked by lld"
        code=0
        "$mw_env_dir/bin/qemu-x86_64" ./prove || code=$?
        [ "$code" = 7 ] && env_say "ok       ran under the VM's qemu-x86_64 (exit 7)" || {
                env_say "FAIL  expected exit 7, got $code"; return 1; }
}

env_command()
{
        case "$1" in
        check) env_check ;;
        setup) env_setup ;;
        prove) env_check >/dev/null 2>&1; env_prove ;;
        *) die "--env wants check, setup or prove" ;;
        esac
}

remaining=$#
while [ "$remaining" -gt 0 ]; do
        argument=$1
        shift
        remaining=$((remaining - 1))
        case "$argument" in
        --clean) do_clean=1 ;;
        --env)
                [ "$remaining" -gt 0 ] || die "--env wants check, setup or prove"
                env_cmd=$1
                shift
                remaining=$((remaining - 1))
                ;;
        --yes) env_yes=1 ;;
        --run) do_run=1 ;;
        --boot) do_run=1; do_build=0 ;;
        --shell) console=1 ;;
        --usb) do_usb=1 ;;
        --usb-update)
                [ "$remaining" -gt 0 ] || die "--usb-update wants the directory a FAT stick is mounted at"
                usb_dir=$1
                shift
                remaining=$((remaining - 1))
                do_usb_update=1
                ;;
        --usb-update=*) usb_dir=${argument#--usb-update=}; do_usb_update=1 ;;
        --host)
                [ "$remaining" -gt 0 ] || die "--host wants a machine to build on"
                host=$1
                host_given=1
                shift
                remaining=$((remaining - 1))
                ;;
        --host=*) host=${argument#--host=}; host_given=1 ;;
        --arch)
                [ "$remaining" -gt 0 ] || die "--arch wants x64, arm64 or riscv64"
                arch_asked=$(arch_name "$1") || die "unknown architecture $1 -- x64, arm64 or riscv64"
                shift
                remaining=$((remaining - 1))
                ;;
        --arch=*)
                arch_asked=$(arch_name "${argument#--arch=}") ||
                        die "unknown architecture ${argument#--arch=} -- x64, arm64 or riscv64"
                ;;
        --ram)
                [ "$remaining" -gt 0 ] || die "--ram wants gigabytes of memory, 1 to 4096"
                ram=$(ram_gigs "$1") || die "--ram $1 is not 1 to 4096 gigabytes"
                shift
                remaining=$((remaining - 1))
                ;;
        --ram=*)
                ram=$(ram_gigs "${argument#--ram=}") ||
                        die "--ram ${argument#--ram=} is not 1 to 4096 gigabytes"
                ;;
        -h | --help) usage; exit 0 ;;
        --*) die "unknown option $argument" ;;
        *) set -- "$@" "$argument" ;;
        esac
done

if [ -n "${env_cmd:-}" ]; then
        env_command "$env_cmd"
        exit $?
fi

#       An option after --usb-update is a word forgotten, not a directory,
#       and is refused before anything is built.
case $do_usb_update:$usb_dir in
1: | 1:-*) die "--usb-update wants the directory a FAT stick is mounted at" ;;
esac

#       The machine to build on is a word of ssh's and rsync's command lines,
#       where one that begins with a dash is an option: -oProxyCommand=CMD
#       runs CMD on this machine before anything is connected. A name is
#       never that, and none holds a blank or a control byte.
case $host in
-* | *[[:space:][:cntrl:]]*) die "not a machine to build on: $host" ;;
esac
#       --host with nothing after it names no machine, and must not fall through
#       to the Lima VM the way an unnamed build does.
[ "$host_given" = 0 ] || [ -n "$host" ] || die "--host wants a machine to build on"

#       Which architecture.
#
#       An arch/ profile on the line is the whole answer, and the build is
#       handed the line as it is. Otherwise --arch, and otherwise this machine
#       when the image is going to be booted here. When neither holds, target
#       stays empty until the build host has been asked what it is.
#
profile_arch=""
for argument do
        case "$argument" in
        arch/*) profile_arch=$argument ;;
        esac
done

if [ -n "$profile_arch" ]; then
        target=$(arch_of_profile "$profile_arch")
        [ -z "$arch_asked" ] || [ "$arch_asked" = "$target" ] ||
                say "$profile_arch on the line wins over --arch $arch_asked"
elif [ -n "$arch_asked" ]; then
        target=$arch_asked
elif [ "$do_run" -eq 1 ]; then
        target=$(arch_name "$(uname -m)") ||
                die "this machine is $(uname -m), which nothing here builds for -- name one with --arch"
else
        target=""
fi

#       Building somewhere else.
#
#       Optional, and only reached when a host was named. The remote command
#       carries no --host of its own and ssh does not forward the environment,
#       so the build over there is an ordinary local one and this cannot
#       recurse.
#
build_remote() {
        carriage_return=$(printf '\r')

        # Asked only when nothing on this side decided: a plain build is for
        # the machine doing it.
        if [ -z "$target" ]; then
                built_on=$(ssh -n -o BatchMode=yes -o ConnectTimeout=20 \
                        "$host" uname -m 2>/dev/null) ||
                        die "cannot reach $host over ssh"
                target=$(arch_name "$built_on") || target=x64
        fi

        if [ -z "$remote" ]; then
                flavour=${profile_arch:-$(arch_profile "$target")}
                remote=.cache/moonwater/$(basename "$here")-$tree_mark-${flavour#arch/}
        fi

        case "$remote" in
        *'
'*|*"$carriage_return"*)
                die "the remote build directory contains a line break" ;;
        esac

        say "Checking $host"
        ssh -n -o BatchMode=yes -o ConnectTimeout=20 "$host" true 2>/dev/null ||
                die "cannot reach $host over ssh"

        # Resolve the parent first, create only the final directory, and pin
        # the command's working directory before it runs.  The marker prevents
        # a path swapped to some other private directory from being accepted
        # merely because that directory happens to have the right owner and
        # mode. An existing owner-only stage is tightened to 0700 and claimed;
        # group- or other-writable directories are refused.
remote_stage='set -eu
fail() { printf "build: refusing unsafe remote stage: %s\n" "$stage" >&2; exit 73; }
owner_of() { stat -c %u -- "$1" 2>/dev/null || stat -f %u "$1"; }
mode_of() { stat -c %a -- "$1" 2>/dev/null || stat -f %Lp "$1"; }
stage=$1
shift
parent=$(dirname -- "$stage") || exit 73
name=$(basename -- "$stage") || exit 73
case $name in ""|.|..) fail ;; esac
[ -d "$parent" ] || (umask 077; mkdir -p -- "$parent") || fail
parent=$(CDPATH= cd -P -- "$parent" && pwd -P) || fail
uid=$(id -u) || fail
parent_owner=$(owner_of "$parent") || fail
case $parent_owner in "$uid"|0) ;; *) fail ;; esac
parent_mode=$(mode_of "$parent") || fail
case $parent_mode in
[0-7][0145][0145]|[0-7][0-7][0145][0145]|[1357][0-7][0-7][0-7]) ;;
*) fail ;;
esac
stage=$parent/$name
[ ! -L "$stage" ] || fail
if [ ! -e "$stage" ]; then
        umask 077
        mkdir -m 700 -- "$stage" || fail
fi
[ -d "$stage" ] && [ ! -L "$stage" ] || fail
[ "$(owner_of "$stage")" = "$uid" ] || fail
mode=$(mode_of "$stage") || fail
case $mode in [0-7][0145][0145]|[0-7][0-7][0145][0145]) ;; *) fail ;; esac
cd -P -- "$stage" || fail
[ "$(pwd -P)" = "$stage" ] || fail
[ "$(owner_of .)" = "$uid" ] || fail
chmod 700 . || fail
[ "$(mode_of .)" = 700 ] || fail
marker=.moonwater-stage-v1
if [ ! -e "$marker" ] && [ ! -L "$marker" ]; then
        (umask 077; set -C; printf "%s\n" moonwater-stage-v1 > "$marker") || fail
fi
[ -f "$marker" ] && [ ! -L "$marker" ] || fail
[ "$(owner_of "$marker")" = "$uid" ] || fail
[ "$(mode_of "$marker")" = 600 ] || fail
[ "$(cat -- "$marker")" = moonwater-stage-v1 ] || fail
exec "$@"'

        remote_command() {
                shell_quote sh -c "$remote_stage" sh "$remote" "$@"
        }

        # What a remote build produced is on the remote, and root owns its
        # kernel tree, since that build runs under sudo: cleaning here would
        # remove this side's output and leave the tree that needed it. So the
        # remote cleans itself, the same way it builds.
        if [ "$do_clean" -eq 1 ]; then
                say "Cleaning $host:$remote"
                #       A directory that never had a tree (an architecture not
                #       built yet, or the stage made just now) has nothing to
                #       clean, and "sh build.sh" is not in it to run.
                # shellcheck disable=SC2029
                ssh -n "$host" "$(remote_command sh -c 'if [ -f build.sh ]; then exec sudo sh build.sh --clean; fi')" ||
                        die "cleaning failed on $host"
                return 0
        fi

        # The build takes its lock with flock(1), from util-linux. Without it
        # every remote build would fail at the lock, so say which tool is
        # missing here rather than leave the build's own error to explain it.
        ssh -n -o BatchMode=yes "$host" 'command -v flock >/dev/null 2>&1' ||
                die "$host has no flock(1), which the build lock needs (util-linux)"

        say "Copying the tree to $host:$remote"

        # Prepare the stage over ssh first. macOS openrsync splits
        # --rsync-path on spaces and drops the quotes, so a `sh -c` wrapper
        # cannot be the remote rsync. Its destination is still remote-shell
        # source: quote the complete path before giving it to rsync, rather
        # than letting spaces or metacharacters become another command.
        ssh -n -o BatchMode=yes "$host" "$(remote_command true)" ||
                die "cannot prepare $host:$remote"
        remote_target=$(shell_quote "$remote/")

        # The kernel source, its artifacts and the built filesystem stay on
        # the build host: they are large, and none of them belong to this
        # checkout. linux/ is the upstream tree, not part of this repository.
        #       rsync splits its -e command at every blank and reads no quotes
        #       (openrsync: a home such as /Users/Jane Doe breaks it), so Lima's
        #       ssh settings are reached through a link in a fresh directory, and
        #       the link's path has no blank in it. The link goes once rsync has.
        rsync_link=
        rsync_rsh=
        if [ -n "$lima_ssh" ]; then
                rsync_link=$(mktemp -d) && ln -s "$lima_ssh" "$rsync_link/ssh.config" ||
                        die "could not link the Lima ssh settings"
                rsync_rsh="ssh -F $rsync_link/ssh.config"
        fi
        rsync_status=0
        rsync -az --delete --no-perms ${rsync_rsh:+-e "$rsync_rsh"} \
                --exclude '/.moonwater-stage-v1' \
                --exclude '.git' \
                --exclude '.claude' \
                --exclude 'linux' \
                --exclude 'artifacts' \
                --exclude 'fs' \
                --exclude 'dist' \
                ./ "$host:$remote_target" || rsync_status=$?
        if [ -n "$rsync_link" ]; then
                rm -f "$rsync_link/ssh.config"
                rmdir "$rsync_link"
        fi
        [ "$rsync_status" = 0 ] || die "copying the tree failed"

        # The architecture travels as --arch rather than as a profile, which
        # would replace the default set rather than swap its arch/ member.
        if [ -z "$profile_arch" ]; then
                set -- --arch "$target" "$@"
        fi

        say "Building on $host: $*"
        # -n so the build does not swallow this script's stdin. Without it the
        # USB prompts below read nothing, because ssh forwards whatever is on
        # stdin to the remote command.
        # shellcheck disable=SC2029,SC2086
        #
        #       sudo drops the environment, so anything the remote build has
        #       to know is named here. env rather than a VAR=value prefix,
        #       which sudo only passes when it has been configured to.
        #
        carry=""
        [ -z "${MOONWATER_STOCK:-}" ] || carry="MOONWATER_STOCK=1"

        # One build at a time on the build host: the build holds /tmp/moonwater-build.lock, so builds from several
        # trees queue here rather than running out of memory together (a kernel build wants most of an 8 GB VM).
        # The lock is taken by this file's build only. A tree whose build.sh predates it runs its build without
        # the lock, and the clean above and the image fetch below take none either.
        # shellcheck disable=SC2029
        ssh -n "$host" "$(remote_command flock /tmp/moonwater-build.lock sudo env $carry sh build.sh "$@")" ||
                die "the build failed on $host"

        # The host which built the configured profile is authoritative about
        # its export.  A stale local artifacts/.config may describe another
        # architecture entirely (bootaa64.efi, or a Pi's kernel8.img).
        image=$(ssh -n "$host" \
                "$(remote_command ./build key-one kernel_export)") ||
                die "could not identify the built image"
        case "$image" in
        *'
'*|*"$carriage_return"*) die "remote build reported an invalid image path" ;;
        dist/*) ;;
        *) die "remote build reported an invalid image path: $image" ;;
        esac

        relative=${image#dist/}
        case "/$relative/" in
        *'/../'*|*'/./'*|*'//'*)
                die "remote build reported an invalid image path: $image" ;;
        esac

        say "Fetching $image"

        artifact_owner_of() {
                stat -c %u -- "$1" 2>/dev/null || stat -f %u -- "$1"
        }
        artifact_mode_of() {
                stat -c %a -- "$1" 2>/dev/null || stat -f %Lp -- "$1"
        }
        artifact_same_file() {
                if [ "$(uname -s)" = Darwin ]; then
                        /bin/zsh -c '[[ $1 -ef $2 ]]' same-file "$1" "$2"
                else
                        [ "$1" -ef "$2" ]
                fi
        }
        artifact_directory_safe() {
                [ -d "$1" ] && [ ! -L "$1" ] || return 1
                [ "$(artifact_owner_of "$1")" = "$(id -u)" ] || return 1
                artifact_mode=$(artifact_mode_of "$1") || return 1
                case $artifact_mode in
                [0-7][0145][0145]|[0-7][0-7][0145][0145]) return 0 ;;
                *) return 1 ;;
                esac
        }

        # A sibling can only be pinned safely when every directory which can
        # replace it is private to this user.  Check the checkout before
        # creating dist, then check each component as it is entered.
        artifact_directory_safe . ||
                die "the local checkout is writable by another user"
        [ ! -L dist ] || die "local output directory is a symbolic link"
        if [ ! -e dist ]; then
                (umask 077; mkdir -- dist) ||
                        die "could not create the local output directory"
        fi
        artifact_directory_safe dist ||
                die "local output directory is not private to this user"

        artifact_parent=dist
        artifact_leaf=${relative##*/}
        artifact_parts=${relative%/*}
        [ "$artifact_parts" != "$relative" ] || artifact_parts=

        while [ -n "$artifact_parts" ]; do
                case "$artifact_parts" in
                */*) artifact_part=${artifact_parts%%/*}
                     artifact_parts=${artifact_parts#*/} ;;
                *) artifact_part=$artifact_parts; artifact_parts= ;;
                esac

                artifact_next=$artifact_parent/$artifact_part
                [ ! -L "$artifact_next" ] ||
                        die "local image parent is a symbolic link: $artifact_next"
                if [ ! -e "$artifact_next" ]; then
                        (umask 077; mkdir -- "$artifact_next") ||
                                die "could not create local image parent: $artifact_next"
                fi
                artifact_directory_safe "$artifact_next" ||
                        die "local image parent is not private: $artifact_next"
                artifact_parent=$artifact_next
        done

        # Keep the name hidden in an owner-only directory and give ssh an
        # already-open descriptor.  ssh never reopens the pathname, which
        # closes the mktemp-to-redirection replacement window.
        fetched_stage=$(mktemp -d "$artifact_parent/.moonwater-fetch.XXXXXX") ||
                die "could not create a private image stage"
        chmod 0700 "$fetched_stage" || die "could not secure the image stage"
        artifact_directory_safe "$fetched_stage" ||
                die "temporary image stage is not private"
        fetched=$fetched_stage/image
        fetch_cleanup() {
                exec 9>&- || :
                [ -z "${fetched:-}" ] || rm -f -- "$fetched"
                [ -z "${fetched_stage:-}" ] || rmdir -- "$fetched_stage" 2>/dev/null || :
        }
        trap fetch_cleanup 0 HUP INT TERM
        (umask 077; set -C; : > "$fetched") ||
                die "could not create a temporary image"
        exec 9<> "$fetched" || die "could not pin the temporary image"
        artifact_same_file "$fetched" /dev/fd/9 ||
                die "temporary image changed while it was opened"

        if ! ssh -n "$host" \
                "$(remote_command cat -- "$image")" >&9; then
                die "could not fetch the built image"
        fi

        [ -f "$fetched" ] && [ ! -L "$fetched" ] &&
                artifact_same_file "$fetched" /dev/fd/9 ||
                die "temporary image changed while it was fetched"
        chmod 0644 /dev/fd/9 || die "could not set image permissions"
        artifact_destination=$artifact_parent/$artifact_leaf
        # BSD mv -h replaces a destination link itself, including a link to a
        # directory. Keep the source descriptor open across that atomic
        # rename and bind the published name back to it afterwards, so a
        # helper which changes either pathname cannot make different bytes a
        # successful result.
        { [ ! -d "$artifact_destination" ] ||
          [ -L "$artifact_destination" ]; } ||
                die "the local image destination is a directory"
        /bin/mv -fh -- "$fetched" "$artifact_destination" ||
                die "could not publish the built image"
        [ -f "$artifact_destination" ] &&
                [ ! -L "$artifact_destination" ] &&
                artifact_same_file "$artifact_destination" /dev/fd/9 ||
                die "published image changed during publication"
        exec 9>&-
        fetched=
        rmdir -- "$fetched_stage" || die "could not remove the image stage"
        fetched_stage=
        trap - 0 HUP INT TERM
}
build_local() {
        die "building a kernel wants a Linux toolchain and a case
sensitive filesystem, and this is $(uname). Name a machine that has them with
--host, or set MOONWATER_BUILD_HOST."
}

#
#       A Mac with its Lima VM running builds in that VM when no machine was
#       named: the kernel wants Linux and a case-sensitive disk, and the VM
#       has both. Lima's own ssh settings are used, and ~/.ssh is left alone.
#       The settings are $LIMA_HOME/default/ssh.config (~/.lima when LIMA_HOME
#       is unset), where limactl keeps the VM. MOONWATER_NO_LIMA=1 keeps the
#       refusal below.
#
lima_home=${LIMA_HOME:-$HOME/.lima}
lima_ssh=
if [ -z "$host" ] && [ "$(uname -s)" = Darwin ] && [ -z "${MOONWATER_NO_LIMA:-}" ] &&
        command -v limactl > /dev/null 2>&1 && [ -f "$lima_home/default/ssh.config" ] &&
        limactl list --format '{{.Name}} {{.Status}}' 2> /dev/null | grep -qx 'default Running'; then
        lima_ssh="$lima_home/default/ssh.config"
        host=lima-default
fi
if [ -n "$lima_ssh" ]; then
        ssh() { command ssh -F "$lima_ssh" "$@"; }
fi

#
#       Removing what a build produced.
#
#       artifacts/ keeps the downloaded kernel tarball and is left alone on
#       purpose: throwing it away means fetching a hundred and fifty megabytes
#       again to get back where you were.
#
if [ "$do_clean" -eq 1 ] && [ -n "$host" ]; then
        build_remote
        exit 0
fi
if [ "$do_clean" -eq 1 ]; then
        # The paths below are relative: run from another directory, this would
        # remove that directory's own linux/ or dist/. Clean this tree instead.
        cd -- "$here" || die "cannot enter $here to clean it"
        say "Removing build output"
        rm -rf dist fs linux \
                artifacts/merge.config artifacts/.config artifacts/info \
                artifacts/asm.applied artifacts/asm.arch artifacts/asm.requested
        rm -f src/moonwater/*.a src/moonwater/*.o src/moonwater/*.o.d \
                src/moonwater/*.cmd src/moonwater/*.order \
                src/canvas/*.a src/canvas/*.o src/canvas/*.o.d \
                src/canvas/*.cmd src/canvas/*.order
        # The .S `build asm` generates from each .asm, which kbuild writes here
        # because src/moonwater/ is the kernel tree's kernel/moonwater.
        # (src/canvas/ is kernel/canvas, and has none.)
        rm -f src/moonwater/*.S src/moonwater/*.asm_tmp
        exit 0
fi

if [ "$do_build" -eq 1 ]; then
        if [ -n "$host" ]; then
                build_remote "$@"
        else
                build_local "$@"
        fi
fi

[ "$do_usb" -eq 1 ] || [ "$do_usb_update" -eq 1 ] || [ "$do_run" -eq 1 ] || exit 0

#
#       Where the image ended up. A remote build sets this from its own
#       generated configuration. --boot without a build takes the image the
#       architecture's profile exports, since a Mac keeps no configuration of
#       its own, and otherwise asks the local one and falls back to x86 EFI.
#
if [ -z "$image" ] && [ -n "$target" ] && [ -z "$profile_arch" ]; then
        image=$(arch_image "$target")
fi
if [ -z "$image" ]; then
        image=$(key_one kernel_export 2>/dev/null || true)
fi
[ -n "$image" ] || image="dist/bootx64.efi"

[ -f "$image" ] ||
        die "no image at $image -- build one first, or drop --boot${target:+ (this is the $target image; --arch picks another)}"

#
#       An update onto a FAT stick already mounted, erasing nothing.
#
#       An installed machine takes a build from any FAT stick that carries the
#       image at its root as moonwater/bootx64.efi (bootaa64.efi,
#       bootriscv64.efi), without starting it: moonwater setup update usb. So
#       the image goes there beside whatever the stick holds, under a name of
#       its own until it is whole, with its SHA-256 beside it as shasum writes
#       one, and the build it says is said: x86's from its setup header, the
#       others' from the kernel's banner, as the machine reads them. On Linux
#       build_usb_update in src/build/build.c does the same.
#
image_build() {
        case "$1" in
        *bootx64.efi)
                at=$(od -An -tu2 -j526 -N2 "$1" | tr -d ' ')
                [ -n "$at" ] && [ "$at" -gt 0 ] || return 1
                dd if="$1" bs=1 skip=$((512 + at)) count=255 2>/dev/null | tr '\000' '\n' | head -1
                ;;
        *)
                LC_ALL=C tr -c '[:print:]' '\n' < "$1" | grep -m1 '^Linux version .*) #[0-9]' |
                        sed -E 's/^Linux version ([^ ]+) \(([^)]*)\) \(.*\) (#[0-9].*)$/\1 (\2) \3/'
                ;;
        esac
}

if [ "$do_usb_update" -eq 1 ]; then
        name=$(basename "$image")
        case "$name" in
        bootx64.efi | bootaa64.efi | bootriscv64.efi) ;;
        *) die "$image is not an image a machine takes from a stick: bootx64.efi, bootaa64.efi or bootriscv64.efi" ;;
        esac
        [ -d "$usb_dir" ] || die "$usb_dir is not a directory: mount the stick and name its mount point"
        info=$(diskutil info "$usb_dir" 2>/dev/null) ||
                die "$usb_dir is not where a disk is mounted: name the stick's mount point"
        mounted=$(echo "$info" | awk -F': *' '/Mount Point:/ { print $2; exit }')
        kind=$(echo "$info" | awk -F': *' '/File System Personality:/ { print $2; exit }')
        [ "$mounted" = "$(cd "$usb_dir" && pwd -P)" ] ||
                die "$usb_dir is not where a FAT stick is mounted: name its mount point"
        case "$kind" in
        *FAT*) ;;
        *) die "$usb_dir is ${kind:-an unknown filesystem}, not FAT: format the stick FAT32 first" ;;
        esac
        build_id=$(image_build "$image")
        [ -n "$build_id" ] || die "$image names no build, so no machine would take it"

        target=$usb_dir/moonwater/$name
        say "Writing $target"
        mkdir -p "$usb_dir/moonwater" || die "could not make $usb_dir/moonwater"
        rm -f "$target.new" "$target.sha256.new"
        if ! cp "$image" "$target.new" || ! cmp -s "$image" "$target.new"; then
                rm -f "$target.new"
                die "could not copy $image whole to the stick"
        fi
        mv -f "$target.new" "$target" &&
                (cd "$usb_dir/moonwater" && shasum -a 256 "$name" > "$name.sha256.new" &&
                        mv -f "$name.sha256.new" "$name.sha256") ||
                die "could not write $target.sha256"
        sync

        printf '\n  %s\n\n' "$build_id"
        echo "is on the stick at $target, its SHA-256 beside it; nothing else on the"
        echo "stick was changed. On the installed machine, with the stick in:"
        echo
        echo "  moonwater setup              names the build it carries beside the one running"
        echo "  moonwater setup update usb   writes it over the install, keeping its data"
        echo
        echo "then reboot. moonwater setup rollback starts the build before it again."
        exit 0
fi

#
#       Writing to a USB stick.
#
#       The image is already an EFI application -- the kernel is built with the
#       EFI stub, which is why it is called bootx64.efi -- so firmware can load
#       it directly and there is no bootloader to install. It goes at the path
#       the UEFI spec reserves for removable media, \EFI\BOOT\BOOTX64.EFI,
#       which is what a machine looks for when told to boot from USB.
#
if [ "$do_usb" -eq 1 ]; then
        # bootx64.efi, bootaa64.efi or bootriscv64.efi: the removable-media name for the
        # machine the image is for is the image's own name, upper case.
        case "$image" in
        *.efi) efi_name=$(basename "$image" | tr '[:lower:]' '[:upper:]') ;;
        *) die "$image is not an EFI application, so firmware cannot boot it from a stick" ;;
        esac
        #
        #       On anything without diskutil this lists the candidates and
        #       prints the command rather than running it. Writing to a raw
        #       block device with the wrong name destroys the wrong disk, and
        #       the checks below that make that hard are diskutil's -- there
        #       is no honest way to claim the same care against an untested
        #       lsblk and dd, so the last step stays in your hands.
        #
        if ! command -v diskutil >/dev/null 2>&1; then
                say "Removable disks"
                if command -v lsblk >/dev/null 2>&1; then
                        lsblk -dno NAME,SIZE,RM,MODEL 2>/dev/null |
                                awk '$3 == 1 { printf "  /dev/%s  %s  %s\n", $1, $2, $4 }'
                else
                        echo "  (lsblk is missing; find the device yourself)"
                fi
                echo
                echo "Write it with, replacing sdX with the stick:"
                echo
                echo "  sudo mkfs.vfat -F32 /dev/sdX1        # after partitioning it GPT/ESP"
                echo "  sudo mount /dev/sdX1 /mnt"
                echo "  sudo mkdir -p /mnt/EFI/BOOT"
                echo "  sudo cp $image /mnt/EFI/BOOT/$efi_name"
                echo "  sudo umount /mnt"
                echo
                echo "Check the device name twice. This erases whatever it names."
                exit 0
        fi

        say "Removable disks"

        # external and physical together exclude internal drives and disk
        # images, so nothing here can be the machine you are sitting at.
        disks=$(diskutil list external physical 2>/dev/null |
                awk '/^\/dev\/disk/ { print $1 }')

        [ -n "$disks" ] || die "no removable disk found -- plug the stick in first"

        index=0
        for disk in $disks; do
                index=$((index + 1))
                name=$(diskutil info "$disk" 2>/dev/null |
                        awk -F": *" '/Device \/ Media Name/ { print $2; exit }')
                size=$(diskutil info "$disk" 2>/dev/null |
                        awk -F": *" '/Disk Size/ { print $2; exit }')
                printf "  %d) %-12s %-28s %s\n" "$index" "$disk" "${name:-unknown}" "${size:-}"
        done

        printf "\nWhich one? (number, or anything else to stop) "
        read -r choice

        case "$choice" in
        ''|*[!0-9]*) die "nothing written" ;;
        esac

        target=$(echo "$disks" | sed -n "${choice}p")
        [ -n "$target" ] || die "no disk $choice in that list"

        # Ask about the chosen disk directly rather than trusting the listing:
        # the two are separate moments, and a mistake here erases the wrong
        # drive. Which field says so varies between macOS versions, so this
        # wants positive evidence from one of them and a contradiction from
        # none -- anything unrecognised is refused rather than assumed safe.
        info=$(diskutil info "$target" 2>/dev/null)
        location=$(echo "$info" | awk -F": *" '/Device Location:/ { print $2; exit }')
        removable=$(echo "$info" | awk -F": *" '/Removable Media:/ { print $2; exit }')
        internal=$(echo "$info" | awk -F": *" '/^ *Internal:/ { print $2; exit }')

        [ "$internal" != "Yes" ] || die "$target is an internal disk -- refusing"

        case "${location:-}${removable:-}" in
        *External* | *Removable*) ;;
        *) die "$target does not look removable (location ${location:-unknown}, media ${removable:-unknown}) -- refusing" ;;
        esac

        name=$(echo "$info" | awk -F": *" '/Device \/ Media Name/ { print $2; exit }')
        size=$(echo "$info" | awk -F": *" '/Disk Size/ { print $2; exit }')

        # The number above was the choice. Asking for the name as well made a
        # second decision out of one, and typing a disk name is not a safety
        # check -- what keeps this off the wrong drive is the refusal above to
        # touch anything internal or not removable.
        printf "\n%sThis erases %s (%s, %s) completely.%s\n" \
                "$RED$BOLD" "$target" "${name:-unknown}" "${size:-unknown size}" "$RESET"
        printf "Enter to write, anything else to stop: "
        read -r confirmation

        [ -z "$confirmation" ] || die "nothing written"

        say "Erasing $target"
        diskutil unmountDisk "$target" >/dev/null 2>&1
        diskutil eraseDisk FAT32 MOONWATER GPT "$target" ||
                die "could not format $target"

        volume="/Volumes/MOONWATER"
        [ -d "$volume" ] || die "formatted, but $volume did not appear"

        say "Writing the image"
        mkdir -p "$volume/EFI/BOOT" || die "could not create $volume/EFI/BOOT"
        cp "$image" "$volume/EFI/BOOT/$efi_name" || die "could not copy the image"
        sync

        diskutil eject "$target" >/dev/null 2>&1

        say "Done -- $target is bootable and safe to unplug"
        echo
        echo "On the machine: boot it, choose the USB stick from the firmware"
        echo "boot menu, and make sure it is booting UEFI rather than legacy."
        echo "Secure Boot has to be off: this kernel is not signed."
        exit 0
fi

#
#       Booting it here.
#
#       The machine is the architecture the image was built for: q35-style x86
#       under qemu-system-x86_64, or QEMU's virt machine for arm64 and riscv64.
#       Each gets hardware acceleration when the host is the same architecture
#       -- hvf on a Mac, kvm on Linux -- and full emulation otherwise, which
#       works and is slow: an x86 image on an Apple Silicon Mac is TCG.
#
[ -n "$target" ] || target=x64

case "$target" in
x64) emulator=qemu-system-x86_64; console_device=ttyS0 ;;
arm64) emulator=qemu-system-aarch64; console_device=ttyAMA0 ;;
riscv64) emulator=qemu-system-riscv64; console_device=ttyS0 ;;
esac
case "$image" in
*/kernel8.img) die "$image is a Raspberry Pi image; QEMU's virt machine cannot boot it -- build --arch arm64 for a VM" ;;
esac

command -v "$emulator" >/dev/null 2>&1 ||
        die "$emulator is not installed"

#       drm_client_lib.active= stops the fbdev client claiming the display.
#       It has to be built (DRM_CLIENT_LIB depends on it) but it must not take
#       the screen, or the compositor is drawing underneath something else.
cmdline="console=$console_device drm_client_lib.active="

#       When the image was written here, since --boot takes whatever was
#       fetched last for this architecture, and an image older than the
#       tree is not the tree. uname -v in the guest says when its kernel was
#       built.
say "Booting $image ($target), written $(date -r "$image" '+%Y-%m-%d %H:%M' 2>/dev/null || echo 'at a time this date cannot say')"
size "$image"

#       virtio-gpu rather than the default VGA: it is the only device here that
#       offers a hardware cursor plane, which is what lets the compositor move
#       the pointer without repainting anything.
#
#       usb-tablet reports absolute positions, so the pointer inside the guest
#       follows the one on the host instead of drifting.
#
#       -vga none matters on x86: without it QEMU also creates a standard VGA
#       device, the window shows that one because it is the boot VGA, and the
#       compositor ends up drawing on the other card where nobody can see it.
#       The virt machines have no default display to turn off.
#
#       -cpu Nehalem, not the default, and this is a requirement rather than a
#       preference. The kernel is compiled -march=x86-64-v2, whose floor is
#       Nehalem, and QEMU's default model is qemu64 -- SSE3-era, no POPCNT.
#       There are 334 popcnt instructions in vmlinux, so on the default model
#       the image takes an invalid opcode before the console exists and prints
#       nothing whatsoever. This line is what stands between that and here.
#
#       This comment used to say the image booted on the default too. It does
#       not, and did not; see kernel/profile/arch/x64.
#
#       xres/yres are the visible screen in backing pixels. Canvas takes
#       seventy percent of that at the panel's refresh; cocoa then sizes
#       the window in points (pixels / backingScaleFactor) and centres it,
#       which is seventy percent of the DIP screen on a Retina panel.
#
#       virtio-net on QEMU user networking is how the guest reaches the
#       bowl mirrors. Without a NIC, setup cannot wget. virtio-rng feeds the
#       kernel's generator on a processor with no instruction for it, which
#       is most arm64 ones and every x86 model before Ivy Bridge.
gpu_device=virtio-gpu-pci
screen_px=$(osascript -l JavaScript -e '
ObjC.import("AppKit");
var s = $.NSScreen.mainScreen;
if (!s) throw "no screen";
var f = s.visibleFrame;
var b = s.backingScaleFactor;
Math.round(f.size.width * b) + " " + Math.round(f.size.height * b);
' 2>/dev/null || true)
xres=${screen_px%% *}
yres=${screen_px#* }
case "$xres" in
*[!0-9]* | "") xres= ;;
esac
case "$yres" in
*[!0-9]* | "") yres= ;;
esac
if [ -n "$xres" ] && [ -n "$yres" ] && [ "$xres" -ge 640 ] && [ "$yres" -ge 480 ]; then
        gpu_device=virtio-gpu-pci,xres=$xres,yres=$yres
fi

accelerators=$("$emulator" -accel help 2>/dev/null || true)
native=no
[ "$(arch_name "$(uname -m)" 2>/dev/null)" = "$target" ] && native=yes

case "$target" in
x64)
        set -- -cpu Nehalem -vga none
        ;;
arm64)
        # gic-version=3: a GICv2 stops at eight CPUs and has no ITS.
        # pauth-impdef only matters emulated, where the architected pointer
        # authentication algorithm makes a boot several times slower; with
        # hvf or kvm -cpu host below replaces the model.
        set -- -machine virt,gic-version=3 -cpu max,pauth-impdef=on
        ;;
riscv64)
        set -- -machine virt -cpu rv64
        ;;
esac

set -- "$@" \
        -m "${ram}G" \
        -smp 2 \
        -kernel "$image" \
        -device "$gpu_device" \
        -device qemu-xhci -device usb-tablet -device usb-kbd \
        -netdev user,id=net0 \
        -device virtio-net-pci,netdev=net0 \
        -device virtio-rng-pci \
        -no-reboot

#       Sound: a card for the guest to play to, an HDA controller with an output-only codec on the host's own audio backend
#       (coreaudio on a Mac, otherwise the first of pipewire, pa, alsa and sdl this QEMU has). Output only, so the VM never opens
#       the host's microphone.
#       QEMU's own audio timer ticks every 10 ms, which is longer than the sound server's card buffer (10.7 ms): it starved
#       the buffer and the tone had holes in it. timer-period=1000 (microseconds) ticks every millisecond. The line before this one had no audio device at all, and the guest's sound server had nothing to
#       open: its mixer showed a volume and a device, and nothing was heard. No backend here, no device, as before.
audio_drivers=$("$emulator" -audiodev help 2>/dev/null || true)
if [ "$(uname)" = Darwin ]; then
        audio_wanted="coreaudio"
else
        audio_wanted="pipewire pa alsa sdl"
fi
for candidate in $audio_wanted; do
        if echo "$audio_drivers" | grep -qw "$candidate"; then
                set -- "$@" -audiodev "$candidate,id=snd0,timer-period=1000" -device intel-hda -device hda-output,audiodev=snd0
                break
        fi
done

# Hardware acceleration where this QEMU has it and the guest is this
# machine's own architecture: hvf on macOS, kvm on Linux. -cpu host
# replaces the model above, which is what you want when the guest is running
# on the real one.
if [ "$native" = yes ] && echo "$accelerators" | grep -qw hvf; then
        set -- "$@" -accel hvf -cpu host
elif [ "$native" = yes ] && echo "$accelerators" | grep -qw kvm && [ -w /dev/kvm ]; then
        set -- "$@" -accel kvm -cpu host
fi

if [ "$console" -eq 1 ]; then
        say "Console on this terminal, ctrl-a x to quit"
        exec "$emulator" "$@" -append "$cmdline" -display none -serial mon:stdio
fi

# cocoa is the macOS window; elsewhere prefer gtk and fall back to sdl.
display=cocoa
if [ "$(uname)" != "Darwin" ]; then
        displays=$("$emulator" -display help 2>/dev/null || true)
        if echo "$displays" | grep -qw gtk; then
                display=gtk
        elif echo "$displays" | grep -qw sdl; then
                display=sdl
        else
                die "this QEMU has no graphical display backend -- use --shell"
        fi
fi

say "Window opening, ctrl-alt-g releases the mouse"
exec "$emulator" "$@" -append "$cmdline" -display "$display" -serial mon:stdio
