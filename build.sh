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
#           sh build.sh --groupkey NAME       every image from now on joins link group
#                                             NAME at boot (none: from now on no group)
#           sh build.sh --clean               remove what a build produced
#           sh build.sh --host box            build on another machine over ssh
#           sh build.sh --on box              build on a linked machine, in its build bowl
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
#       then a Mac without --host refuses to build. --host and --on always win.
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
        umask 022
        unset LD_PRELOAD LD_LIBRARY_PATH LD_AUDIT LD_CONFIG \
                LD_DYNAMIC_WEAK LD_ORIGIN_PATH GCONV_PATH \
                ENV BASH_ENV CDPATH IFS SHELLOPTS PROMPT_COMMAND \
                GCC_EXEC_PREFIX COMPILER_PATH CPATH C_INCLUDE_PATH \
                CPLUS_INCLUDE_PATH OBJC_INCLUDE_PATH LIBRARY_PATH \
                CC CXX CLANG HOSTCC HOSTCXX HOSTCFLAGS HOSTLDFLAGS \
                CFLAGS LDFLAGS CPPFLAGS KCFLAGS KCPPFLAGS KAFLAGS \
                MAKEFLAGS GNUMAKEFLAGS MFLAGS SPARK_CPPFLAGS \
                GNUPGHOME GPG_AGENT_INFO \
                GIT_EXEC_PATH GIT_SSH GIT_SSH_COMMAND GIT_TEMPLATE_DIR \
                GIT_CONFIG GIT_CONFIG_GLOBAL GIT_CONFIG_SYSTEM \
                GIT_CONFIG_PARAMETERS GIT_CONFIG_COUNT GIT_EXTERNAL_DIFF \
                TMPDIR TEMP TMP
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
                        case "$dep" in
                        -* | '!' | '(' | ')') return 0 ;;
                        esac
                        [ -e "$dep" ] || return 0
                        [ -n "$(find "$dep" -newer "$tool" -print -quit)" ] && return 0
                done
                [ -n "$(find "$here/build.sh" -newer "$tool" -print -quit)" ]
        }

        # Root does not exec a tool anyone else can write, whatever its
        # timestamp says: an old user-writable ./build with a future timestamp
        # would otherwise run unchanged under sudo. Only a regular file root
        # owns, and that no group or other can write, is taken as built (and
        # its depfile must be root-owned and non-group/world-writable too).
        tool_untrusted() {
                [ -L "$tool" ] || [ -L "$depfile" ] && return 0
                [ -n "$(find "$tool" \( ! -user 0 -o -perm -020 -o -perm -002 \) -print -quit)" ] && return 0
                [ -n "$(find "$depfile" \( ! -user 0 -o -perm -020 -o -perm -002 \) -print -quit)" ]
        }

        if [ ! -x "$tool" ] || [ ! -f "$depfile" ] ||
                { [ "$bootstrap_uid" = 0 ] && tool_untrusted; } || tool_stale; then
                # Built in a private staging directory beside it and renamed
                # over, so a second build.sh started meanwhile runs the old
                # tool or the new one and never a half-written file, and a
                # user-writable checkout cannot race predictable PID names.
                # clang has no nested functions, and scope_exit is its defer
                # statement, which it takes when asked (src/lib.util.c).
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
                tool_stage=$(mktemp -d "$here/.build-tool.XXXXXX") || {
                        echo "build.sh: could not create staging directory" >&2
                        exit 1
                }
                chmod 0700 "$tool_stage" &&
                        "$compiler" -O2 -static -nostdlib -nostartfiles $outline \
                                -fno-stack-protector -fno-builtin -w $defer \
                                -MMD -MT "$tool" -MF "$tool_stage/build.d" \
                                -o "$tool_stage/build" "$source" &&
                        mv -fT "$tool_stage/build.d" "$depfile" &&
                        mv -fT "$tool_stage/build" "$tool" &&
                        rmdir "$tool_stage" ||
                        {
                                rm -rf "$tool_stage"
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
link_host=""
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
group_asked=""
console=0
image=""
image_sum=""
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
        --groupkey)
                [ "$remaining" -gt 0 ] || die "--groupkey wants a group's name, or none"
                group_asked=$1
                shift
                remaining=$((remaining - 1))
                ;;
        --groupkey=*) group_asked=${argument#--groupkey=} ;;
        --host)
                [ "$remaining" -gt 0 ] || die "--host wants a machine to build on"
                host=$1
                host_given=1
                shift
                remaining=$((remaining - 1))
                ;;
        --host=*) host=${argument#--host=}; host_given=1 ;;
        --on)
                [ "$remaining" -gt 0 ] || die "--on wants a linked machine to build on"
                link_host=$1
                [ -n "$link_host" ] || die "--on wants a linked machine to build on"
                shift
                remaining=$((remaining - 1))
                ;;
        --on=*)
                link_host=${argument#--on=}
                [ -n "$link_host" ] || die "--on wants a linked machine to build on"
                ;;
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
#       A linked machine is a name of the link's (moonwater link lists them):
#       letters, digits, '.', '-' and '_', and never a dash first, since it is
#       a word of the link client's command line.
case $link_host in
-* | *[!A-Za-z0-9._-]*) die "not a linked machine to build on: $link_host" ;;
esac
#       Two machines named for one build is a mistake, not a choice. A
#       MOONWATER_BUILD_HOST in the environment is not a naming: --on wins.
[ -z "$link_host" ] || [ "$host_given" = 0 ] ||
        die "--on and --host both name a machine to build on: name one"
[ -z "$link_host" ] || host=""

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
#       What the image says about this checkout, as environment words for the
#       remote build: the commit it is at (twelve hex digits), that commit's
#       date, and whether the tree has changes. Read before the copy and again
#       after it (build_remote compares them): the copy is of the tree as it
#       was while it ran.
checkout_carry() {
        commit=$(git -C "$here" rev-parse --short=12 HEAD 2>/dev/null) || return 1
        carry=
        case "$commit" in
        [0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f])
                carry="MOONWATER_COMMIT=$commit"
                commit_date=$(git -C "$here" log -1 --format=%cI 2>/dev/null | tr -cd '0-9TZ:+-')
                [ -z "$commit_date" ] || carry="$carry MOONWATER_COMMIT_DATE=$commit_date"
                [ -z "$(git -C "$here" status --porcelain 2>/dev/null | head -1)" ] ||
                        carry="$carry MOONWATER_COMMIT_DIRTY=1"
                ;;
        esac
        printf '%s\n' "$carry"
}

#       The stage on the build host, made safe and entered before anything
#       runs there: sh -c "$remote_stage" sh STAGE COMMAND... Both --host and
#       --on run every command of a build through it.
#
#       Resolve the parent first, create only the final directory, and pin
#       the command's working directory before it runs.  The marker prevents
#       a path swapped to some other private directory from being accepted
#       merely because that directory happens to have the right owner and
#       mode. An existing owner-only stage is tightened to 0700 and claimed;
#       group- or other-writable directories are refused.
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
        #       The stamp is read before the copy and again after it, and a
        #       tracked file written while the copy ran (its time is after the
        #       marker's) is refused too: an edit made and reverted during the
        #       copy leaves the tree as it was at both readings, and the copy
        #       holds it.
        if carry_before=$(checkout_carry); then
                :
        else
                say "No git checkout here: the image will say commit unknown"
                carry_before=
        fi
        copy_marker=$(mktemp) || die "could not make a marker for the copy"
        #       A second more, so that a write during the copy is later than
        #       the marker whatever the clock's resolution: file times are
        #       coarser than a copy's first moments, and an edit in the same
        #       tick as the marker would not show.
        sleep 1
        rsync_status=0
        rsync -az --delete --no-perms ${rsync_rsh:+-e "$rsync_rsh"} \
                --exclude '/.moonwater-stage-v1' \
                --exclude '.git' \
                --exclude '.claude' \
                --exclude '/.moonwater-groups' \
                --exclude 'linux' \
                --exclude 'artifacts' \
                --exclude 'fs' \
                --exclude 'dist' \
                ./ "$host:$remote_target" || rsync_status=$?
        if [ -n "$rsync_link" ]; then
                rm -f "$rsync_link/ssh.config"
                rmdir "$rsync_link"
        fi
        #       A file the copy sends (rsync's excludes, below) written after
        #       the marker: find takes its paths before its tests.
        changed_during_copy=$(cd "$here" && find . \( -name .git -o -name .claude -o -name linux -o -name artifacts \
                -o -name fs -o -name dist \) -prune -o -type f -newer "$copy_marker" -print 2>/dev/null | head -1)
        if carry_after=$(checkout_carry); then :; else carry_after=; fi
        rm -f -- "$copy_marker"
        [ "$rsync_status" = 0 ] || die "copying the tree failed"
        [ -z "$changed_during_copy" ] && [ "$carry_after" = "$carry_before" ] ||
                die "the checkout changed while it was copied to $host: build again once it is still"

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
        # The copy above leaves .git behind, so the remote has no commit to read: the stamp read before the copy
        # goes with it, for the image to say.
        [ -z "$carry_before" ] || carry="$carry $carry_before"

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
        image_check

        if [ -n "$group_name" ]; then
                say "Stamping $image with link group $group_name"
                # shellcheck disable=SC2029
                ssh "$host" "$(remote_command sudo sh -c "$group_stamp_script" sh "$image" "$(image_arch "$image" || echo unknown)")" < "$group_key" ||
                        die "stamping $image with link group $group_name failed on $host"
        fi

        say "Fetching $image"
        ssh_image() { ssh -n "$host" "$(remote_command cat -- "$image")"; }
        image_receive ssh_image
}
#
#       The image a build host named, checked before any of it is a path
#       here: under dist/, one line, and no ., .. or empty step. Sets
#       relative, its path under dist/.
#
image_check() {
        carriage_return=$(printf '\r')
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
}

#
#       Fetching $image into dist/ here: $1 is the command whose output is the
#       image. Published whole or not at all, by a rename out of a private
#       stage, and only when it is what image_sum says, when that is set.
#
image_receive() {

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
                #       And what the caller asked to go with them (--on: the build
                #       host's private directory).
                [ -z "${fetch_also:-}" ] || "$fetch_also"
        }
        trap fetch_cleanup 0 HUP INT TERM
        (umask 077; set -C; : > "$fetched") ||
                die "could not create a temporary image"
        exec 9<> "$fetched" || die "could not pin the temporary image"
        artifact_same_file "$fetched" /dev/fd/9 ||
                die "temporary image changed while it was opened"

        if ! "$1" >&9; then
                die "could not fetch the built image"
        fi
        #       What the build host said the image is, when it said: a fetch
        #       that ended early or brought other bytes is not published.
        if [ -n "$image_sum" ]; then
                fetched_sum=$(shasum -a 256 < "$fetched" | cut -d' ' -f1)
                [ "$fetched_sum" = "$image_sum" ] ||
                        die "the image arrived damaged: its sha256 is $fetched_sum, and the build host's $image_sum"
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

#
#       Building on a linked machine (--on NAME).
#
#       The machine is one this Mac is linked with (moonwater link), a
#       Moonwater with the build profile installed (bowl profile build
#       install), and the build runs in that bowl, as root, on every core it
#       has, at idle priority so the desktop there does not wait on it. No
#       ssh and no rsync on this side: the link runs one command at a
#       time and sends a file whole or not at all, so the tree goes as one
#       archive, the files rsync would have sent and no others, with its
#       SHA-256. There it is unpacked beside the stage and made the stage's by
#       the bowl's rsync with the same excludes, so what the build directory
#       keeps (the kernel tree, artifacts/, fs/, dist/, the stage's marker and
#       its .moonwater-groups) and what it drops are what --host's copy keeps
#       and drops. The stage is the same directory --host would use, under the
#       home the link's commands start in, made safe by the same script.
#
#       The image comes back as base64 on the command's output (the link's
#       pull does not answer a Mac) and is published only when its SHA-256 is
#       the one the build host read off the file, after the stamp. Commands
#       stay under the link's 1024 bytes: what is longer (the stage script,
#       the stamp, the steps below) goes as files into a private directory
#       of the build host's /tmp, which is removed when this ends.
#
#       MOONWATER_BUILD_BOWL names another bowl than /bowls/alpine.
#
link_driver='set -eu
say() { printf "%s\n" "$*" >&2; }
phase=$1
bowl_root=$2
shift 2
case $phase in
check)
        case $(uname -m) in
        x86_64) triplet=x86_64-linux-gnu- ;;
        aarch64 | arm64) triplet=aarch64-linux-gnu- ;;
        riscv64) triplet=riscv64-linux-gnu- ;;
        *) say "build: this machine is $(uname -m), which nothing here builds on"; exit 69 ;;
        esac
        if [ ! -x "$bowl_root/usr/bin/gcc" ] || [ ! -L "$bowl_root/usr/local/bin/${triplet}gcc" ]; then
                say "build: no build bowl at $bowl_root here: bowl profile build install makes it"
                exit 69
        fi
        command -v flock > /dev/null 2>&1 || { say "build: no flock(1) here, which the build lock needs"; exit 69; }
        command -v bowl > /dev/null 2>&1 || { say "build: no bowl here"; exit 69; }
        uname -m
        ;;
unpack)
        tarball=$1
        case $tarball in /tmp/moonwater-handoff.*/tree.tgz) ;; *) say "build: not a tree: $tarball"; exit 2 ;; esac
        have=$(sha256sum < "$tarball" | cut -d" " -f1)
        if [ "$have" != "$2" ]; then
                say "build: the tree arrived damaged: its sha256 is $have, and the one sent $2"
                exit 1
        fi
        tree=${tarball%/tree.tgz}/tree
        mkdir -- "$tree"
        bowl --root "$bowl_root" /bin/tar -xzf "$tarball" -C "$tree" --no-same-owner
        rm -f -- "$tarball"
        bowl --root "$bowl_root" /usr/bin/rsync -a --delete --no-perms \
                --exclude /.moonwater-stage-v1 --exclude .git --exclude .claude \
                --exclude /.moonwater-groups --exclude linux --exclude artifacts \
                --exclude fs --exclude dist "$tree/" ./
        ;;
build)
        #       At idle priority, which every compiler under it inherits: the
        #       build has every core when nothing else wants one, and the
        #       desktop there stays as quick as it was while it runs
        #       (SCHED_IDLE, or nice 19 where chrt is not, and the disk at
        #       its idle class).
        idle=
        if chrt -i 0 true 2> /dev/null; then
                idle="chrt -i 0"
        elif nice -n 19 true 2> /dev/null; then
                idle="nice -n 19"
        fi
        ! ionice -c 3 true 2> /dev/null || idle="ionice -c 3 $idle"
        exec $idle flock /tmp/moonwater-build.lock bowl --root "$bowl_root" /bin/sh build.sh "$@"
        ;;
clean)
        [ -f build.sh ] || exit 0
        exec bowl --root "$bowl_root" /bin/sh build.sh --clean
        ;;
image) exec ./build key-one kernel_export ;;
stamp) exec sh "${0%/*}/stamp.sh" "$@" ;;
sum)
        [ -f "$1" ] && [ ! -L "$1" ] || { say "build: no image at $1"; exit 1; }
        printf "%s %s\n" "$(sha256sum < "$1" | cut -d" " -f1)" "$(wc -c < "$1" | tr -d " ")"
        ;;
send) exec base64 -w0 < "$1" ;;
*) say "build: no step $phase"; exit 2 ;;
esac'

build_link() {
        link_started=$(date +%s)
        link=$(command -v moonwater 2> /dev/null || true)
        [ -n "$link" ] || link=$HOME/.local/bin/moonwater
        [ -x "$link" ] && grep -aq 'push NAME FILE PATH' "$link" ||
                die "--on wants the link client: moonwater on PATH or in ~/.local/bin (src/waterlink/mac.c), with link push"
        bowl_root=${MOONWATER_BUILD_BOWL:-/bowls/alpine}
        case $bowl_root in
        /bowls/*/* | /bowls/ | *[!A-Za-z0-9._/-]*) die "MOONWATER_BUILD_BOWL is a bowl, /bowls/NAME: $bowl_root" ;;
        /bowls/?*) ;;
        *) die "MOONWATER_BUILD_BOWL is a bowl, /bowls/NAME: $bowl_root" ;;
        esac

        #       One command there, its output here. The link's commands are at
        #       most 1024 bytes, so one that is longer is refused here, saying
        #       so, rather than failing there with nothing said.
        link_run() {
                [ "${#1}" -lt 1000 ] || die "a command for $link_host is ${#1} bytes, more than the link takes"
                "$link" link "$link_host" "$1"
        }
        link_quiet() { link_run "$1" < /dev/null; }
        #       Its output as one line, and the status the command's: a pipe
        #       into tr would have had tr's.
        link_say() {
                link_said=$(link_quiet "$1") || return
                printf '%s\n' "$link_said" | tr -d '\r'
        }

        handoff=""
        link_local=""
        link_cleanup() {
                [ -z "$link_local" ] || rm -rf -- "$link_local"
                link_local=""
                case $handoff in
                /tmp/moonwater-handoff.?*) link_quiet "rm -rf -- $(shell_quote "$handoff")" > /dev/null 2>&1 || : ;;
                esac
                handoff=""
        }
        trap link_cleanup 0
        trap 'exit 130' INT
        trap 'exit 143' HUP TERM

        say "Checking $link_host"
        reply=$(link_say 'echo moonwater-link-ok' 2> /dev/null) || reply=""
        [ "$reply" = moonwater-link-ok ] ||
                die "cannot reach $link_host over the link: moonwater link doctor says why"

        handoff=$(link_say 'umask 077 && mktemp -d /tmp/moonwater-handoff.XXXXXX') ||
                die "could not make a private directory on $link_host"
        case $handoff in
        /tmp/moonwater-handoff.*[!A-Za-z0-9]* | /tmp/moonwater-handoff.) handoff=""; die "$link_host made an odd private directory" ;;
        /tmp/moonwater-handoff.*) ;;
        *) handoff=""; die "could not make a private directory on $link_host" ;;
        esac
        link_local=$(mktemp -d) || die "could not make a private directory here"
        [ -n "$link_local" ] && [ -d "$link_local" ] || die "could not make a private directory here"
        printf '%s\n' "$remote_stage" > "$link_local/stage.sh" &&
                printf '%s\n' "$link_driver" > "$link_local/driver.sh" &&
                printf '%s\n' "$group_stamp_script" > "$link_local/stamp.sh" ||
                die "could not write the build steps"
        for step in stage driver stamp; do
                "$link" link push "$link_host" "$link_local/$step.sh" "$handoff/$step.sh" < /dev/null > /dev/null ||
                        die "could not send the build steps to $link_host"
        done

        #       Every command there: in the stage, by the stage script, then a step.
        link_step() { shell_quote sh "$handoff/stage.sh" "$remote" sh "$handoff/driver.sh" "$@"; }

        built_on=$(link_say "$(shell_quote sh "$handoff/driver.sh" check "$bowl_root")") ||
                die "$link_host cannot build"
        built_on=$(arch_name "$built_on") || die "$link_host is a machine nothing here builds on"
        #       The bowl's compilers are its own machine's (bowl profile build):
        #       an image for another one is not built there, and is refused
        #       before the tree is sent.
        if [ -z "$target" ]; then
                target=$built_on
        elif [ "$target" != "$built_on" ]; then
                die "$link_host builds $built_on images only, and this one is for $target: --arch $built_on builds there"
        fi
        if [ -z "$remote" ]; then
                flavour=${profile_arch:-$(arch_profile "$target")}
                remote=.cache/moonwater/$(basename "$here")-$tree_mark-${flavour#arch/}
        fi
        case "$remote" in
        *[[:space:][:cntrl:]]*) die "the remote build directory holds a blank or a control character" ;;
        esac

        if [ "$do_clean" -eq 1 ]; then
                say "Cleaning $link_host:$remote"
                link_quiet "$(link_step clean "$bowl_root")" || die "cleaning failed on $link_host"
                return 0
        fi

        say "Copying the tree to $link_host:$remote"
        if carry_before=$(checkout_carry); then
                :
        else
                say "No git checkout here: the image will say commit unknown"
                carry_before=
        fi
        copy_marker=$(mktemp) || die "could not make a marker for the copy"
        #       A second more, as for --host: a write in the same tick as the
        #       marker would not show.
        sleep 1
        #       The files rsync would send: find takes its paths before its
        #       tests, and a directory is named by itself (tar -n), so an empty
        #       one goes too and nothing under an excluded name does.
        copy_status=0
        find . \( -name .git -o -name .claude -o -name linux -o -name artifacts -o -name fs -o -name dist \) -prune -o \
                \( -path ./.moonwater-stage-v1 -o -path ./.moonwater-groups \) -prune -o -print 2> /dev/null |
                sed 1d > "$link_local/files" || copy_status=1
        COPYFILE_DISABLE=1 tar --no-xattrs --no-mac-metadata --no-acls --no-fflags -cnzf "$link_local/tree.tgz" \
                -T "$link_local/files" 2> "$link_local/tar.log" || copy_status=1
        changed_during_copy=$(find . \( -name .git -o -name .claude -o -name linux -o -name artifacts \
                -o -name fs -o -name dist \) -prune -o -type f -newer "$copy_marker" -print 2>/dev/null | head -1)
        if carry_after=$(checkout_carry); then :; else carry_after=; fi
        rm -f -- "$copy_marker"
        [ "$copy_status" = 0 ] || die "packing the tree failed: $(head -3 "$link_local/tar.log")"
        [ -z "$changed_during_copy" ] && [ "$carry_after" = "$carry_before" ] ||
                die "the checkout changed while it was packed for $link_host: build again once it is still"
        tree_sum=$(shasum -a 256 < "$link_local/tree.tgz" | cut -d' ' -f1)
        "$link" link push "$link_host" "$link_local/tree.tgz" "$handoff/tree.tgz" < /dev/null > /dev/null ||
                die "could not send the tree to $link_host"
        link_quiet "$(link_step unpack "$bowl_root" "$handoff/tree.tgz" "$tree_sum")" ||
                die "could not make $link_host:$remote the tree sent"

        if [ -z "$profile_arch" ]; then
                set -- --arch "$target" "$@"
        fi
        say "Building on $link_host: $*"
        carry=""
        [ -z "${MOONWATER_STOCK:-}" ] || carry="MOONWATER_STOCK=1"
        [ -z "$carry_before" ] || carry="$carry $carry_before"
        # shellcheck disable=SC2086
        link_quiet "env $carry $(link_step build "$bowl_root" "$@")" ||
                die "the build failed on $link_host"

        image=$(link_say "$(link_step image "$bowl_root")") ||
                die "could not identify the built image"
        image_check

        if [ -n "$group_name" ]; then
                say "Stamping $image with link group $group_name"
                link_run "$(link_step stamp "$bowl_root" "$image" "$(image_arch "$image" || echo unknown)")" < "$group_key" ||
                        die "stamping $image with link group $group_name failed on $link_host"
        fi

        #       Read after the stamp, which writes the image.
        image_said=$(link_say "$(link_step sum "$bowl_root" "$image")") ||
                die "could not read the built image on $link_host"
        image_sum=${image_said%% *}
        image_size=${image_said#* }
        case $image_sum:$image_size in
        *[!0-9a-f:]*:* | *:*[!0-9]* | :* | *:) die "$link_host said odd things of the image: $image_said" ;;
        esac
        [ "${#image_sum}" = 64 ] || die "$link_host said odd things of the image: $image_said"
        image_host_name=$link_host

        say "Fetching $image ($((image_size / 1048576)) MiB)"
        link_image() { link_quiet "$(link_step send "$bowl_root" "$image")" | base64 -d; }
        fetch_also=link_cleanup
        image_receive link_image
        fetch_also=""
        link_cleanup
        trap - 0 INT HUP TERM
        say "Built on $link_host in $(( $(date +%s) - link_started )) s: $image, sha256 $image_sum"
}

build_local() {
        die "building a kernel wants a Linux toolchain and a case
sensitive filesystem, and this is $(uname). Name a machine that has them with
--host, or set MOONWATER_BUILD_HOST."
}

#
#       The link group the images join (--groupkey NAME): its key is made here
#       the first time, kept in .moonwater-groups/NAME.key (owner only, never
#       committed and never sent with the tree), and the name is remembered,
#       so every image built from then on -- and so every stick written and
#       every system installed or updated from one -- starts in the group,
#       with no code typed. --groupkey none stops it. The image is stamped by
#       its own program (artifacts/shell.elf, under qemu-user on a build host
#       of another processor), the key on standard input. ANYBODY WITH A STICK
#       OR AN IMAGE FROM THIS TREE CAN JOIN THE GROUP; moonwater link group new
#       NAME on a member makes a new key and leaves the old sticks out.
#
groups_dir=$here/.moonwater-groups
group_name=""
group_key=""
if [ -n "$group_asked" ]; then
        case $group_asked in
        none)
                rm -f "$groups_dir/default"
                say "No link group: images from now on join none"
                ;;
        *)
                case $group_asked in
                *[!A-Za-z0-9._-]* | "" | .* | -*) die "--groupkey: a group's name is letters, digits, '.', '-' and '_', or none" ;;
                esac
                [ "${#group_asked}" -le 31 ] || die "--groupkey: a group's name is at most 31 characters"
                (umask 077; mkdir -p "$groups_dir") || die "could not make $groups_dir"
                chmod 700 "$groups_dir"
                if [ ! -s "$groups_dir/$group_asked.key" ]; then
                        made=$(head -c 32 /dev/urandom | base64 | tr -d '\n') && [ "${#made}" = 44 ] ||
                                die "could not make a key for group $group_asked"
                        (umask 077; printf '%s:%s\n' "$group_asked" "$made" > "$groups_dir/$group_asked.key") ||
                                die "could not keep the key of group $group_asked"
                        made=""
                        say "Made link group $group_asked: its key is $groups_dir/$group_asked.key"
                        # This machine joins it too, so it reaches every machine the images start; a moonwater
                        # from before link group new and stamp (whose link group NAME is another verb) is told how to.
                        joiner=$(command -v moonwater 2> /dev/null || true)
                        [ -n "$joiner" ] || joiner=$HOME/.local/bin/moonwater
                        if [ -x "$joiner" ] && grep -aqE 'group stamp IMAGE|carries group' "$joiner" &&
                                "$joiner" link group add - < "$groups_dir/$group_asked.key"; then
                                say "This machine is in group $group_asked"
                        else
                                say "This machine is not in group $group_asked yet: moonwater link group add - < $groups_dir/$group_asked.key"
                        fi
                fi
                printf '%s\n' "$group_asked" > "$groups_dir/default"
                ;;
        esac
fi
if [ -s "$groups_dir/default" ]; then
        group_name=$(cat "$groups_dir/default")
        case $group_name in
        *[!A-Za-z0-9._-]* | "" | .* | -*) die "invalid link group name in $groups_dir/default" ;;
        esac
        group_key=$groups_dir/$group_name.key
        [ -s "$group_key" ] || die "link group $group_name has no key at $group_key: --groupkey $group_name makes one, --groupkey none stops it"
fi

#       The stamp, run where the image is (the build host, under sudo when the build ran there): the image's own program with
#       argv[0] moonwater, natively or through qemu-user, the key on standard input. $1 the image, $2 its architecture.
group_stamp_script='set -eu
image=$1
case $2 in x64) cpu=x86_64 ;; arm64) cpu=aarch64 ;; *) cpu=$2 ;; esac
[ -f artifacts/shell.elf ] || { echo "build: no artifacts/shell.elf to stamp $image with" >&2; exit 1; }
ln -sf shell.elf artifacts/moonwater
case $(uname -m) in
"$cpu") exec artifacts/moonwater link group stamp "$image" - ;;
*) command -v "qemu-$cpu" > /dev/null || { echo "build: qemu-$cpu is wanted to stamp a $2 image here" >&2; exit 1; }
   exec "qemu-$cpu" artifacts/moonwater link group stamp "$image" - ;;
esac'

#       The architecture an image is for, from its name.
image_arch() {
        case $1 in
        *bootx64.efi) echo x64 ;;
        *bootaa64.efi | *kernel8.img) echo arm64 ;;
        *bootriscv64.efi) echo riscv64 ;;
        *) return 1 ;;
        esac
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
if [ -z "$host" ] && [ -z "$link_host" ] && [ "$(uname -s)" = Darwin ] && [ -z "${MOONWATER_NO_LIMA:-}" ] &&
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
if [ "$do_clean" -eq 1 ] && [ -n "$link_host" ]; then
        build_link
        exit 0
fi
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
        if [ -n "$link_host" ]; then
                build_link "$@"
        elif [ -n "$host" ]; then
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

#       A build here is stamped here (a build on another machine was, before its image came: build_remote over ssh,
#       build_link on a linked machine, which brings no artifacts/shell.elf back to stamp with).
if [ -n "$group_name" ] && [ "$do_build" -eq 1 ] && [ -z "$host" ] && [ -z "$link_host" ] && [ -f "$image" ]; then
        say "Stamping $image with link group $group_name"
        stamp_as=
        [ -w "$image" ] || stamp_as=sudo
        (cd "$here" && $stamp_as sh -c "$group_stamp_script" sh "$image" "$(image_arch "$image" || echo unknown)") < "$group_key" ||
                die "stamping $image with link group $group_name failed"
fi

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
        diskutil unmountDisk force "$target" >/dev/null 2>&1 || true
        if ! diskutil eraseDisk FAT32 MOONWATER GPT "$target"; then
                sleep 1
                diskutil unmountDisk force "$target" >/dev/null 2>&1 || true
                diskutil eraseDisk FAT32 MOONWATER GPT "$target" ||
                        die "could not format $target"
        fi

        volume=""
        tries=0
        while [ "$tries" -lt 10 ]; do
                for slice in "${target}s2" "${target}s1"; do
                        mp=$(diskutil info "$slice" 2>/dev/null |
                                awk -F": *" '/Mount Point:/ { print $2; exit }')
                        if [ -n "$mp" ] && [ -d "$mp" ]; then
                                volume="$mp"
                                break
                        fi
                done
                [ -n "$volume" ] && break
                diskutil mountDisk "$target" >/dev/null 2>&1 || true
                sleep 1
                tries=$((tries + 1))
        done
        if [ -z "$volume" ] && [ -d "/Volumes/MOONWATER" ]; then
                volume="/Volumes/MOONWATER"
        fi
        [ -n "$volume" ] && [ -d "$volume" ] ||
                die "formatted, but the new MOONWATER volume did not mount"

        say "Writing the image"
        mkdir -p "$volume/EFI/BOOT" || die "could not create $volume/EFI/BOOT"
        cp "$image" "$volume/EFI/BOOT/$efi_name" || die "could not copy the image"
        sync

        if ! diskutil eject "$target" >/dev/null 2>&1; then
                sleep 1
                diskutil unmountDisk force "$target" >/dev/null 2>&1 || true
                diskutil eject "$target" >/dev/null 2>&1 || true
        fi

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
