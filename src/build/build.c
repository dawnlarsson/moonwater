/*
        The build tool.

        Bootstrap it with one command, from the repository root:

            cc -O2 -static -nostdlib -nostartfiles -fno-stack-protector \
               -fno-builtin -w -o build src/build/build.c

        That is the whole of what a bare machine needs: a C compiler and an
        assembler. Nothing is linked, no library is required, and the binary
        it produces is what builds everything else. build.sh runs exactly that
        line and then hands over, so `sh build.sh` still works and still means
        the same thing.

        This owns configuration, marked assembly, Spark linking and kernel
        build orchestration. kbuild and the external compiler tools remain
        its execution backends. src/build/host.sh contains helpers used by
        the macOS entry and the remaining kernel/remote shell operations.

        Where a utility exists in this tree it is called rather than spawned.
        cp, ln, rm, mkdir, mknod, chmod and find here are the same
        functions the image ships, invoked in this address space through the
        registry in src/sh/builtin.c. That is deliberate: if our cp is wrong,
        the build breaks, which is the only way a userspace gets exercised by
        something that cares. They are called one at a time and never from a
        thread -- the tools keep static arenas and must not share an address
        space concurrently.

        Nothing in the code path below spells a path, a version, a flag set or
        an architecture. Every one of those is a setting, and the settings
        this project answers with are in one table at the top. Point them
        somewhere else and the same guarantees apply to another tree.
*/

#define STANDARD_APPLETS
#define STANDARD_KEEP_SPOOL
#include "../lib.util.c"
#include "../moonwater/spark.c"
#include "../sh/shell.c"

/*
        The settings.

        One table, read by name, with this project's answers as the defaults.
        An optional build.conf beside build.sh overrides any of them, and
        --set name=value on the command line overrides that, so nothing below
        this block ever names a path, a version, a flag set or an
        architecture.
*/
#define BUILD_SETTING_ROOM 128

//      Beside build.sh, and optional: this repository ships none.
#define BUILD_SETTINGS_FILE "build.conf"

typedef struct build_setting
{
        string_address name;
        string_address value;
} build_setting;

static build_setting build_settings[BUILD_SETTING_ROOM] = {
        /*      What the built system calls itself. */
        {"name", "moonwater"},
        {"version", "25"},
        {"full_name", "moonwater-25"},

        /*      Where a build puts things. */
        {"artifacts", "artifacts"},
        {"image_root", "fs"},
        {"output", "dist"},
        {"kernel_tree", "linux"},
        {"profile_root", "kernel/profile"},

        /*      The kernel this tree builds on, and where it comes from.

                The signature is pinned here rather than downloaded next to
                the tarball. Fetching both would still verify, but only that
                the archive is signed by a trusted key -- pinning ties the
                build to this exact release, so a validly signed but different
                kernel cannot be substituted.

                To move to a new release: take the .sign file from the
                mirror's linux-VERSION.tar.sign and paste it here along with
                the version. */
        {"kernel_version", "7.2.8"},
        {"kernel_mirror", "https://cdn.kernel.org/pub/linux/kernel"},
        {"kernel_keys", "torvalds@kernel.org gregkh@kernel.org"},
        {"kernel_signature",
         "-----BEGIN PGP SIGNATURE-----\n"
         "Comment: This signature is for the .tar version of the archive\n"
         "Comment: git archive --format tar --prefix=linux-7.2.8/ v7.2.8\n"
         "Comment: git version 2.55.0\n"
         "\n"
         "iQIzBAABCgAdFiEEZH8oZUiU471FcZm+ONu9yGCSaT4FAmq2h9kACgkQONu9yGCS\n"
         "aT7KOxAApH+1Ya5tF2JhFtUWm8EqNZ9Ai9i6IkZ8h8OECVvhQs4KiutLVy6gLJpE\n"
         "xh7ywEXWf6dFZqBW3pTHDlmLC+zAiqQpXKvU+BJnoVorE0d61BoQZlPaMFesIEJ7\n"
         "+qp4kvzN+3LUVU0n1PZLDLaPq/L71TkEnioT5U0uEJcBN3C/YhFil4jfRGzvJxPu\n"
         "+VXUnMTdC6MMuZaODbXBQP+OUfzVTnsL3Vr495kY68oqj3mOdixggpTKX9/YncP0\n"
         "4f7du8U28dtg5FUt+vGpVtDWHcDVjIrUvioH7CYZDecDkrsNxtJkGUJysDL66IYd\n"
         "ybHRcxKfbIlb0cADtE0yKy+QaV4ydi55kWrrxpl+Up/i9TvEqtg3yll2zBlXQHuG\n"
         "P+Fz+rS4DiesMj0gGpqP0XgOEOffjilM1HNSwlYw/18LRroBK4Wga1wS4uPbvzK5\n"
         "rapAZsJnpv4r9V8PqFSRZWMmcuBT4ZSN3IjeSiiWZ4fQa5POVntHGZ6Oiu+Dqpui\n"
         "SRDNNr1PnOl4g/7xW4xuGKPnfauMhtxjz6EnlxDLnkRyP6ftAhYoohoQ+UAbrJ1u\n"
         "VV0ExYB+n5h1LBxTbM37sykYzDucDNZs7wiAx2m1u4XjBxc9/FxSgCdaQCs4cGpl\n"
         "Y6KlHCyOMxd4gPfx6VW/d1OU0/ZdTXkaOv+CJRGGZVgqMJLoTWU=\n"
         "=FDcG\n"
         "-----END PGP SIGNATURE-----\n"},

        /*      Linking a freestanding binary of this tree's own shape.
                The head and tail are separate so the whole-program flags land
                where they always have. Flag order does not change the output,
                but a diff of two build logs should not claim it did. */
        {"link_script", "src/build/spark.ld"},
        {"entry", "_start"},
        {"freestanding_source", "src/main.c"},
        {"freestanding_output", "bin"},
        {"freestanding_flags",
         "-static -s -flto -nostdlib -nostartfiles -ffreestanding -fno-builtin"
         " -Qn -Wl,--build-id=none -Wl,--gc-sections -Wl,--strip-all"
         " -Wl,--strip-debug -Wl,-x -Wl,-s -Wl,--no-warn-rwx-segments"
         " -Wl,-nmagic -O2"},
        {"whole_program_flags", "-fwhole-program -fipa-pta"},
        {"freestanding_flags_tail",
         "-fno-asynchronous-unwind-tables -fomit-frame-pointer"
         " -fno-stack-protector -fno-semantic-interposition"
         " -D_FORTIFY_SOURCE=0 -fno-unwind-tables -fno-plt -fno-PIE -fno-pie"
         " -fno-stack-clash-protection"},

        /*      The ISA floor the library promises, and what must not be in
                it. `build floor` proves both against the ELF attributes of an
                object it compiles at that floor, and the standard lane in
                test/run reads its answer.

                Why these extensions, for this tree: A is part of the public
                surface because the atomic macros lower to it. F and D are part
                of the assembly itself -- string_format and fast_sin use both
                precisions. get_cpu_time reads the time CSR, so Zicntr and its
                Zicsr dependency are explicit too. Software fallbacks for those
                would be a different implementation, not something a compile
                check should pretend exists. C is forbidden rather than merely
                unrequested: the shipped routines must assemble without
                compressed instructions, and a toolchain defaulting to rv64gc
                would put them back without a word. */
        {"floor_arch", "riscv64"},
        {"floor_prefix", "rv64"},
        {"floor_march", "rv64imafd_zicsr_zicntr"},
        {"floor_mabi", "lp64d"},
        {"floor_source", "src/lib.c"},
        {"floor_require", "i m a f d zicsr zicntr"},
        {"floor_forbid", "c zca zcb zcd zcf zcmp zcmt"},

        /*      What the build needs before it starts. */
        {"required", "bison flex bc gpg make gcc clang rustc"},

        /*      The sources and scripts a build reads. */
        {"tool_registry", "src/sh/tools.inc"},
        {"switches_kconfig", "src/moonwater/Kconfig.switches"},
        {"builtin_registry", "src/sh/builtin.c"},
        {"shell_source", "programs/shell"},
        {"utilities_source", "programs/utilities"},
        {"monitor_source", "programs/monitor.sh"},
        {"patch_script", "kernel/patch/apply"},
        {"replace_script", "kernel/replace/apply"},
        {"firmware_script", "kernel/firmware/apply"},

        /*      Booting the built image, and where the module's own build
                products land beside its source. */
        {"emulator", "qemu-system-x86_64"},
        {"emulator_flags", "-m 2G -smp 2 -cpu Nehalem"},
        {"emulator_devices",
         "-vga none -device virtio-gpu-pci -device qemu-xhci"
         " -device usb-tablet -device usb-kbd"
         " -netdev user,id=net0 -device virtio-net-pci,netdev=net0"
         " -device virtio-rng-pci -no-reboot"},
        {"kernel_cmdline", "console=ttyS0 drm_client_lib.active="},

        /*      The same for the other two, on QEMU's virt machine: no default
                display to turn off, a PL011 on arm64, and a GICv3 there so
                hvf and kvm can accelerate the interrupt controller. */
        {"emulator_arm64", "qemu-system-aarch64"},
        {"emulator_flags_arm64", "-machine virt,gic-version=3 -cpu max,pauth-impdef=on -m 2G -smp 2"},
        {"emulator_devices_arm64",
         "-device virtio-gpu-pci -device qemu-xhci"
         " -device usb-tablet -device usb-kbd"
         " -netdev user,id=net0 -device virtio-net-pci,netdev=net0"
         " -device virtio-rng-pci -no-reboot"},
        {"kernel_cmdline_arm64", "console=ttyAMA0 drm_client_lib.active="},
        {"emulator_riscv64", "qemu-system-riscv64"},
        {"emulator_flags_riscv64", "-machine virt -cpu rv64 -m 2G -smp 2"},
        {"emulator_devices_riscv64",
         "-device virtio-gpu-pci -device qemu-xhci"
         " -device usb-tablet -device usb-kbd"
         " -netdev user,id=net0 -device virtio-net-pci,netdev=net0"
         " -device virtio-rng-pci -no-reboot"},
        {"kernel_cmdline_riscv64", "console=ttyS0 drm_client_lib.active="},
        {"default_image", "dist/bootx64.efi"},
        {"default_image_arm64", "dist/bootaa64.efi"},
        {"default_image_riscv64", "dist/bootriscv64.efi"},
        {"module_root", "src"},
        {"clean_patterns",
         "[!.]*.a [!.]*.o [!.]*.o.d [!.]*.cmd [!.]*.order"
         " [!.]*.S [!.]*.asm_tmp"},

        /*      What a remote build does not need a copy of: the upstream
                kernel tree, its artifacts and the built filesystem are large
                and none of them belong to this checkout. */
        {"remote_excludes", ".git .claude linux artifacts fs dist"},

        /*      The image's own layout: the directories every build makes and
                the device nodes it boots with, as name, type, major, minor.
                The spark minor has to match SPARK_DEVICE_MINOR in src/moonwater/spark.c,
                and the floodlight one FLOODLIGHT_DEVICE_MINOR in
                src/moonwater/floodlight.c. Both are fixed rather than allocated because
                the node is made here, before there is a devtmpfs to make it. */
        {"image_directories",
         "sys proc dev tmp run etc root bin sbin usr lib lib64 var opt bowls/bin"},
        {"image_nodes",
         "dev/tty c 5 0"
         " dev/console c 5 1"
         " dev/null c 1 3"
         " dev/zero c 1 5"
         " dev/random c 1 8"
         " dev/urandom c 1 9"
         " dev/kmsg c 1 11"
         " dev/spark c 10 250"
         " dev/floodlight c 10 249"},

        /*      The profiles composed ahead of whatever was asked for, in this
                order, so the last two win the choices the earlier ones touch.
                The architecture's profile follows them and is not in either
                list: it is the machine's own unless --arch or an arch/ profile
                on the line says otherwise. */
        {"profiles_always", "any general gpu guests latency prod"},
        //      sec_default is the security tier a plain build ships; a tier
        //      named on the line (sec_reference, sec_hardened, sec_locked)
        //      takes its place with the rest of this list.
        {"profiles_default", "debug_none limbo desktop wifi sec_default serial"},

        {null, null},
};

static string_address build_setting_get(string_address name)
{
        for (positive at = 0; build_settings[at].name; at++)
                if (word_is(build_settings[at].name, name))
                        return build_settings[at].value;

        return null;
}

static bool build_setting_set(string_address name, string_address value)
{
        positive at = 0;

        while (build_settings[at].name)
        {
                if (word_is(build_settings[at].name, name))
                {
                        build_settings[at].value = value;
                        return true;
                }
                at++;
        }

        if (at + 1 >= BUILD_SETTING_ROOM)
                return false;

        build_settings[at].name = name;
        build_settings[at].value = value;
        build_settings[at + 1].name = null;
        build_settings[at + 1].value = null;

        return true;
}

/*
        Text.

        A build assembles a great many short strings -- paths, command lines,
        flag lists -- and it is one command from start to finish, so an arena
        that only ever grows is the whole allocator this needs. Nothing is
        freed and nothing is reused, which is the point: every string this
        hands out stays valid until the command ends.

        The first version was a ring of thirty two buffers handed out in turn,
        on the reasoning that no string outlives the step that made it. Three
        separate bugs said otherwise and only one of them was visible. `build
        config` reported writing to the last profile it had read. The assembly
        splitter wrote its temporary file under a recycled name and renamed
        that to the right place, so the output was correct and the debris was
        not. And the key reader took a buffer per line of a two thousand line
        configuration, which recycled the very marker it was matching against,
        so every key came back empty and the compiler was invoked as its own
        directory. A ring is a bet that the author remembers its rule at every
        call site. This does not need the bet.

        The ceiling is a fixed array rather than a growing one, so a build that
        asks for too much stops and says so instead of failing later in a way
        that looks like something else.
*/
#define BUILD_TEXT_ROOM (16 << 20)

//      One line's worth of words, which is what every splitter asks for.
#define BUILD_WORD_ROOM 8192

static p8 build_text_arena[BUILD_TEXT_ROOM];
static memory_arena build_text_storage = {build_text_arena, BUILD_TEXT_ROOM, 0};

static b32 build_die(string_address text);

static p8 address_to build_text_take(positive want)
{
        p8 address_to answer = memory_arena_take(address_of build_text_storage,
                                                want, 1);
        if (!answer)
                build_die("build: ran out of room for text");
        answer[0] = end;
        return answer;
}

//      A terminated copy of a span out of a buffer the next read overwrites.
static string_address build_text_keep(string_address text, positive length)
{
        p8 address_to into = build_text_take(length + 1);

        memory_copy(into, text, length);
        into[length] = end;
        return (string_address)into;
}

//      Only --watch runs more than one build in one process, and it is the
//      one caller that has to give the arena back.

/*
        Join, with the pieces named rather than counted.

        A null argument ends the list, so a caller can pass a value it knows
        may be absent and get the shorter string instead of a crash. The list
        is walked twice -- once to measure, once to copy -- so the arena is
        asked for exactly what the answer needs.
*/
static string_address build_join(string_address first, ...)
{
        positive total = 0;
        string_address piece = first;
        p8 address_to into;
        p8 address_to at;
        var_args rest;
        var_args measure;

        var_list(rest, first);
        var_list_copy(rest, measure);

        while (piece)
        {
                total += string_length(piece);
                piece = var_list_get(measure, string_address);
        }

        var_list_end(measure);

        into = build_text_take(total + 1);
        at = into;
        piece = first;

        while (piece)
        {
                positive length = string_length(piece);

                memory_copy(at, piece, length);
                at += length;
                piece = var_list_get(rest, string_address);
        }

        var_list_end(rest);
        *at = end;

        return (string_address)into;
}

static string_address build_number(positive value)
{
        p8 address_to into = build_text_take(32);
        positive length = positive_into(into, value);

        into[length] = end;

        return (string_address)into;
}

//      A setting used as a path, which is every path this tool writes.
static string_address build_in(string_address setting, string_address name)
{
        string_address root = build_setting_get(setting);

        return name ? build_join(root, "/", name, null) : root;
}

/*
        Diagnostics.

        The colours and the shape of a label are what the shell build printed,
        because a build log people have read for years is an interface too.
        $'\033[' was a bash extension the old src/build/host.sh had to work around;
        here the byte is just a byte.
*/
#define BUILD_RESET "\033[0m"
#define BUILD_BOLD "\033[1m"
#define BUILD_RED "\033[91m"
#define BUILD_GREEN "\033[92m"
#define BUILD_YELLOW "\033[93m"
#define BUILD_CYAN "\033[96m"

static fn build_say(string_address text)
{
        string_format(log, BUILD_CYAN BUILD_BOLD "%s" BUILD_RESET "\n", text);
        log_flush();
}

static fn build_label(string_address colour, string_address text)
{
        string_format(log, "%s %s\n", BUILD_CYAN, BUILD_BOLD);
        string_format(log, "    %s%s\n", colour, text);
        string_format(log,
                      "_____________________________________________________________________________\n");
        string_format(log, "%s\n", BUILD_RESET);
        log_flush();
}

static b32 build_die(string_address text)
{
        string_format(log_error, BUILD_RED "build failed: %s" BUILD_RESET "\n",
                      text);
        exit(1);

        return 1;
}

/*
        A size, in the three units the old size helper printed.

        Integer arithmetic on purpose: bc is not installed everywhere, and the
        tenths are a remainder scaled by ten rather than a division that would
        need a floating point unit this may not have.
*/
static fn build_size(string_address path)
{
        error_stat status;

        if (stat(path, address_of status) < 0)
        {
                string_format(log_error, "size: cannot stat '%s'\n", path);
                return;
        }

        positive bytes = (positive)status.st_size;

        string_format(log, "%p bytes (%p.%p KB, %p.%p MB)\n", bytes,
                      bytes / 1024, ((bytes % 1024) * 10) / 1024,
                      bytes / 1048576, ((bytes % 1048576) * 10) / 1048576);
        log_flush();
}

/*
        Where a build runs.

        Every path this tool writes is relative to the repository root, so
        running it from anywhere else quietly writes into the wrong place.
        Checked by looking for what only a root has rather than by its name,
        which is the test src/build/host.sh settled on after the tree was rearranged
        twice underneath the old one.
*/
static bool build_is_directory(string_address path)
{
        error_stat status;

        return stat(path, address_of status) >= 0 &&
               (status.st_mode & S_IFMT) == S_IFDIR;
}

static bool build_is_file(string_address path)
{
        error_stat status;

        return stat(path, address_of status) >= 0 &&
               (status.st_mode & S_IFMT) == S_IFREG;
}

static positive build_modified(string_address path)
{
        error_stat status;

        if (stat(path, address_of status) < 0)
                return 0;

        return (positive)status.st_mtime;
}

//      True when the first is newer, and when either is missing -- the caller
//      is gating "does this need redoing", and a missing file always does.
//      src/build/host.sh's version of this compared file SIZES while being named and
//      used as an age comparison, so a config edit that did not grow the file
//      was silently ignored and the previous configuration got built.
static bool build_is_newer(string_address first, string_address second)
{
        if (!build_is_file(first) || !build_is_file(second))
                return true;

        return build_modified(first) > build_modified(second);
}

/*
        Calling a utility of this image.

        The registry in src/sh/builtin.c dispatches on argv[0], so a call is
        the argument vector swapped for one of ours, the tool run, and the
        caller's vector put back. That is what shell_kill already does for the
        one builtin that borrows a utility's parser; this is the same move
        with the utility named rather than implied.

        One at a time and never from a thread: the tools keep static arenas
        and must not share an address space concurrently.
*/
static b32 build_tool_words(string_address address_to words)
{
        string_address address_to saved = program_argument_list();
        b32 saved_count = program_argument_count();
        b32 count = (b32)pointer_vector_count(words);
        b32 answer;

        //      Ours buffers its output through the same log this does. Flush
        //      before and after or the tool's bytes land inside a line of the
        //      build log, or after the step that produced them.
        log_flush();
        program_arguments_use(words, count);
        answer = shell_tool_as_called();
        program_arguments_use(saved, saved_count);
        log_flush();

        //      -1 is the registry saying the name is not a tool's, which for
        //      a caller that named one is a programming error, not a status.
        return answer < 0 ? 127 : answer;
}

#define BUILD_ARGUMENT_ROOM 512

//      A null-ended argument list, as build_tool and build_run are called,
//      into a vector of BUILD_ARGUMENT_ROOM.
static fn build_vector_of(string_address address_to words, string_address piece,
                          var_args rest)
{
        positive count = 0;

        while (piece && count + 1 < BUILD_ARGUMENT_ROOM)
        {
                words[count++] = piece;
                piece = var_list_get(rest, string_address);
        }

        words[count] = null;
}

static b32 build_tool(string_address name, ...)
{
        string_address words[BUILD_ARGUMENT_ROOM];
        var_args rest;

        var_list(rest, name);
        build_vector_of((string_address address_to)words, name, rest);
        var_list_end(rest);

        return build_tool_words((string_address address_to)words);
}

//      Named below, defined below that: the spawn helpers need it and it
//      needs the text ring, so one of the two orders has to be broken.
static string_address build_resolve(string_address name);
static string_address build_resolve_privileged(string_address name);

#define BUILD_PRIVILEGED_PATH \
        "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
#define BUILD_PRIVILEGED_ENV_PATH "PATH=" BUILD_PRIVILEGED_PATH

static string_address address_to build_environment_privileged(
    string_address address_to source)
{
        positive have = pointer_vector_count(source);
        string_address address_to answer =
            (string_address address_to)build_text_take(
                (have + 2) * sizeof(string_address));
        positive kept = 0;

        for (positive at = 0; at < have; at++)
                if (!string_has_prefix(source[at], "PATH="))
                        answer[kept++] = source[at];

        answer[kept++] = BUILD_PRIVILEGED_ENV_PATH;
        answer[kept] = null;
        return answer;
}

/*
        Spawning something that is not ours.

        The toolchain, make, tar, curl, gpg, ssh, rsync and QEMU stay separate
        programs: they are not this tree's to reimplement and driving them is
        what a build tool is for. Everything below goes through build_start so
        that a failed command is a status rather than a shell's opinion of one.
*/
static b32 build_wait(b32 child)
{
        b32 status = 0;

        if (child < 0)
                return -1;

        if (system_wait4_retry(child, address_of status, 0, null) < 0)
                return -1;

        return (b32)wait_status_code((positive)status);
}

/*
        execve takes a path, not a name.

        Everything the shell build invoked -- gcc, make, tar, ssh -- it named
        and the shell found along PATH. A cross compiler is named
        x86_64-linux-gnu-gcc in the configuration and lives in /usr/bin, so
        handing that name straight to execve got 127 and "compilation failed"
        with nothing above it to say why.

        A working directory, an environment, a muzzle or somewhere else for
        stdout: the shell build reached for a subshell whenever it needed one
        of them -- `( cd linux && make ... )`, `env $make_flags sh ...`,
        `> /dev/null`, `$(...)`. Each was a process whose only job was to
        change one thing about the next one. Here they are fields, and every
        child this program starts goes through the one fork below.
*/
typedef struct build_command
{
        string_address address_to words;
        string_address directory;
        string_address address_to environment;
        bool quiet;
        bool privileged;
        //      Descriptor for the child's stdout, 0 to leave it alone. Opened
        //      close-on-exec, so the child keeps only the copy on 1.
        b32 output;
} build_command;

static bool build_root()
{
        return geteuid() == 0;
}

static b32 build_start(build_command address_to what)
{
        string_address raised[BUILD_ARGUMENT_ROOM];
        string_address address_to words = what->words;
        string_address command;
        string_address path;
        string_address address_to environment =
            what->environment ? what->environment : environ;
        bool root = build_root();
        bool trusted = root || what->privileged;
        b32 child;

        /*
                Any command which is privileged now, or will become privileged,
                is resolved independently of the caller's PATH.  A build
                commonly starts as `sudo sh build.sh`; in that case every
                child is already root, not only the few operations carrying
                the privileged flag.

                The child also receives a fixed PATH.  Resolving /usr/bin/make
                safely but letting that root make resolve cc or sh through an
                inherited writable PATH is the same bug one process later.

                When elevation is still needed, sudo itself comes from that
                trusted path and is handed the already-resolved command.  This
                also avoids a fake sudo earlier in PATH collecting a password.
        */
        command = trusted ? build_resolve_privileged(words[0])
                          : build_resolve(words[0]);

        if (!command)
                return string_report(log_error, -1, "build: %s not found\n", words[0]);

        if (what->privileged && !root)
        {
                string_address sudo = build_resolve_privileged("sudo");
                positive count = 0;

                if (!sudo)
                        return string_report(log_error, -1,
                                             "build: sudo not found in trusted system paths\n");

                raised[count++] = sudo;
                raised[count++] = command;

                for (positive at = 1;
                     words[at] && count + 1 < BUILD_ARGUMENT_ROOM; at++)
                        raised[count++] = words[at];

                raised[count] = null;
                words = (string_address address_to)raised;
                path = sudo;
        }
        else
                path = command;

        if (trusted)
                environment = build_environment_privileged(environment);

        //      BUILD_TRACE prints every command before it runs. A build tool
        //      that drives six other programs has to be able to say exactly
        //      what it asked them, or a failure is a guess.
        if (string_get_environment(environ, "BUILD_TRACE"))
        {
                string_format(log, "+ %s", path);

                for (positive at = 1; words[at]; at++)
                        string_format(log, " %s", words[at]);

                string_format(log, "\n");
        }

        log_flush();
        child = fork();

        if (child == 0)
        {
                b32 sink = what->quiet ? open("/dev/null", O_WRONLY | O_CLOEXEC, 0)
                                       : what->output;

                if (what->directory && chdir(what->directory) < 0)
                        exit(126);

                if (sink > 0 && dup2(sink, 1) < 0)
                        exit(127);

                execve(path, words, environment);
                //      exec only returns having failed, and this is the child:
                //      leaving would run the rest of the build twice.
                exit(127);
        }

        return child;
}

static b32 build_execute(build_command address_to what)
{
        return build_wait(build_start(what));
}

static b32 build_run_words(string_address address_to words)
{
        build_command what = {.words = words};

        return build_execute(address_of what);
}

static b32 build_run(string_address name, ...)
{
        string_address words[BUILD_ARGUMENT_ROOM];
        var_args rest;

        var_list(rest, name);
        build_vector_of((string_address address_to)words, name, rest);
        var_list_end(rest);

        return build_run_words((string_address address_to)words);
}

/*
        Reading what another program said.

        A pipe, a child with its output on the write end, and the parent
        reading until the end. Only stdout is collected: the callers here are
        asking a question -- which accelerators does this QEMU have, what is
        the entry point of that ELF -- and a tool's complaints belong on the
        terminal where somebody can see them.
*/
static bipolar build_capture_words(string_address address_to words,
                                   p8 address_to into, positive capacity)
{
        build_command what = {.words = words};
        b32 pair[2];
        b32 child;
        positive used = 0;

        if (!capacity)
                return -1;

        into[0] = end;

        if (system_pipe(pair, O_CLOEXEC) < 0)
                return -1;

        what.output = pair[1];
        child = build_start(address_of what);
        close(pair[1]);

        if (child < 0)
        {
                close(pair[0]);
                return -1;
        }

        while (used + 1 < capacity)
        {
                bipolar got = read(pair[0], into + used, capacity - used - 1);

                if (got <= 0)
                        break;

                used += (positive)got;
        }

        into[used] = end;
        close(pair[0]);

        //      The status is discarded on purpose: every caller wants the
        //      bytes, and a program that says nothing and fails says the same
        //      thing to them as one that says nothing and succeeds.
        build_wait(child);

        return (bipolar)used;
}

/*
        Is that program installed.

        `command -v` in one function -- and it is the shell's own, because the
        shell needs exactly this answer before it can run anything typed
        without a slash. A name with a separator in it is a path and is asked
        about directly; anything else is walked along PATH, an empty component
        standing for the working directory, which is what the shell would have
        done and what the build asks about before it decides to install a
        compiler.

        PATH is read out of the environment rather than left to the callee's
        fallbacks: this program never starts the shell's variable table, and a
        build with no PATH at all is one that should say the tool is missing
        rather than guess at /bin.
*/
static string_address build_resolve_from(string_address name,
                                         string_address path)
{
        p8 found[BUILD_WORD_ROOM];

        if (!name || !*name || (!path && !string_first_of(name, '/')))
                return null;

        if (!shell_find_in_path_mode(name, found, sizeof(found),
                                     ACCESS_EXECUTE, false, path))
                return null;

        return build_text_keep((string_address)found, string_length(found));
}

static string_address build_resolve(string_address name)
{
        return build_resolve_from(name, string_get_environment(environ, "PATH"));
}

static string_address build_resolve_privileged(string_address name)
{
        return build_resolve_from(name, BUILD_PRIVILEGED_PATH);
}

static bool build_have(string_address name)
{
        return (build_root() ? build_resolve_privileged(name)
                             : build_resolve(name)) != null;
}

/*
        Whole files.

        A configuration, a profile and a generated assembly source are all
        read entire and walked in memory: they are tens of kilobytes, the
        walks want to look backwards as well as forwards, and a build that
        cannot hold linux/.config in memory cannot build a kernel either.
*/
#define BUILD_FILE_ROOM (4 << 20)

static p8 build_file_one[BUILD_FILE_ROOM];
static p8 build_file_two[BUILD_FILE_ROOM];

//      The composed configuration gets a buffer nothing else touches. It was
//      read into the first of the two above, and the verifier and the spark
//      packer both read files into that one -- so by the time the userspace
//      build asked for the architecture, the answer had been overwritten by a
//      kernel configuration and every key came back empty.
static p8 build_config_buffer[BUILD_FILE_ROOM];

static bool build_write_file(string_address path, string_address data,
                             positive length)
{
        bipolar handle = system_open_output_at(AT_FDCWD, path, true, 0644);
        positive written;

        if (handle < 0)
                return false;

        written = system_write_all((positive)handle, data, length);
        bipolar closed = system_close(handle);
        return written == length && closed >= 0;
}

/*
        Lines.

        Every file this reads is a line-oriented one, and every walk over it
        wants the line without its newline and the place the next one starts.
        The buffer is written into rather than copied out of: a slurped file
        is ours, and terminating each line in place is one store against a
        copy per line.
*/
typedef struct build_lines
{
        string_address at;
        string_address line;
        positive length;
        p8 address_to store;
} build_lines;

static fn build_lines_open(build_lines address_to walk, string_address buffer)
{
        walk->at = buffer;
        walk->line = null;
        walk->length = 0;
        walk->store = null;
}

/*
        The next line, without a newline and without writing anything.

        Terminating each line in place would be cheaper and is what the first
        version did, which meant a second walk over the same buffer saw a file
        of one line. Nothing here mutates what it was handed.
*/
static bool build_lines_next(build_lines address_to walk)
{
        string_address stop;

        if (!walk->at || !*walk->at)
                return false;

        stop = string_first_of_or_end(walk->at, '\n');
        walk->line = walk->at;
        walk->length = (positive)(stop - walk->at);
        walk->at = stop + (*stop != end);

        return true;
}

//      The words of one line, collapsed the way an unquoted shell expansion
//      collapses them: runs of blanks are one separator and the ends are
//      trimmed. src/build/host.sh's `key` was exactly `echo $(...)`, so anything
//      reading a key got this and nothing else. The newline is in the
//      separator set because the line is a span of a larger buffer and is not
//      terminated.
static positive build_words_of(string_address line, positive bound,
                               string_address address_to into, positive room,
                               p8 address_to store, positive store_room)
{
        positive count = 0;
        positive used = 0;
        positive at = 0;

        while (at < bound && count < room)
        {
                positive length = 0;

                if (at < bound)
                        at += string_span_max(line + at, bound - at,
                                              string_set_blanks);

                while (at + length < bound && line[at + length] != ' ' &&
                       line[at + length] != '\t')
                        length++;

                if (!length)
                        break;

                if (used + length + 1 > store_room)
                        break;

                memory_copy(store + used, line + at, length);
                store[used + length] = end;
                into[count++] = (string_address)(store + used);
                used += length + 1;
                at += length;
        }

        return count;
}

//      The next line and its words. The walk takes one store the first time
//      it is asked, and every line's words reuse it.
static bool build_lines_words(build_lines address_to walk,
                              string_address address_to words,
                              positive address_to count)
{
        if (!build_lines_next(walk))
                return false;

        if (!walk->store)
                walk->store = build_text_take(BUILD_WORD_ROOM);

        address_to count = build_words_of(walk->line, walk->length, words,
                                          BUILD_ARGUMENT_ROOM, walk->store,
                                          BUILD_WORD_ROOM);
        return true;
}

/*
        A tree that is not this one.

        Every setting above is this project's answer, and there are two ways to
        give another tree's. A build.conf beside build.sh holds one
        `name value` per line -- the same shape as the `#>` keys a profile
        carries, because a reader who knows one knows the other -- and
        --set name=value on the command line wins over it. Neither is required
        and this repository ships neither, so the table is what runs here.

        A name the table does not have is added rather than refused: a tree
        with its own steps has its own settings, and this is where they live.
*/
static fn build_settings_read()
{
        build_lines walk;

        if (file_slurp(BUILD_SETTINGS_FILE, build_file_two,
                        BUILD_FILE_ROOM) < 0)
                return;

        build_lines_open(address_of walk, (string_address)build_file_two);

        while (build_lines_next(address_of walk))
        {
                positive at = 0;
                positive name_length = 0;
                string_address name;
                string_address value;

                if (at < walk.length)
                        at += string_span_max(walk.line + at,
                                              walk.length - at,
                                              string_set_blanks);

                if (at >= walk.length || walk.line[at] == '#')
                        continue;

                while (at + name_length < walk.length &&
                       !byte_is_blank(walk.line[at + name_length]))
                        name_length++;

                //      Copied out of the file buffer, which the next thing to
                //      read a file will overwrite.
                name = build_text_keep(walk.line + at, name_length);
                at += name_length;

                if (at < walk.length)
                        at += string_span_max(walk.line + at,
                                              walk.length - at,
                                              string_set_blanks);

                value = build_text_keep(walk.line + at, walk.length - at);
                build_setting_set(name, value);
        }
}

/*
        The keys.

        A profile may carry lines the kernel's own configuration language has
        no room for -- which compiler to use, what to name the built image,
        what to run before and after -- and they ride in comments the kernel
        ignores:

            #> compiler gcc

        Anchored, with the trailing space, so `key pre` cannot also match
        `#> prefix`. Multiple matching lines join into one line on purpose:
        that is how a flag list accumulates across composed profiles.
*/
static string_address build_key_from(string_address buffer, string_address name,
                                     positive address_to matched)
{
        byte_store joined = {build_text_take(BUILD_WORD_ROOM), BUILD_WORD_ROOM - 1, 0};
        p8 address_to store = build_text_take(BUILD_WORD_ROOM);
        string_address marker = build_join("#> ", name, " ", null);
        positive marker_length = string_length(marker);
        build_lines walk;
        positive seen = 0;

        build_lines_open(address_of walk, buffer);

        while (build_lines_next(address_of walk))
        {
                string_address words[BUILD_ARGUMENT_ROOM];
                positive count;

                if (walk.length < marker_length ||
                    memory_compare(walk.line, marker, marker_length))
                        continue;

                seen++;
                count = build_words_of(walk.line + marker_length,
                                       walk.length - marker_length,
                                       (string_address address_to)words,
                                       BUILD_ARGUMENT_ROOM, store,
                                       BUILD_WORD_ROOM);

                for (positive at = 0; at < count; at++)
                {
                        if (joined.used)
                                byte_store_append_span(address_of joined, " ", 1);

                        byte_store_append_span(address_of joined, words[at],
                                               string_length(words[at]));
                }
        }

        joined.bytes[joined.used] = end;

        if (matched)
                address_to matched = seen;

        return (string_address)joined.bytes;
}

static bool build_config_loaded;

//      artifacts/.config is read once and every key comes out of that copy.
//      The shell asked grep afresh for each of them, which was a process and
//      a re-read per key and made the answers able to disagree with each
//      other if a profile step rewrote the file in between.
static bool build_config_load()
{
        string_address path = build_in("artifacts", ".config");

        build_config_loaded = file_slurp(path, build_config_buffer,
                                          BUILD_FILE_ROOM) >= 0;

        if (!build_config_loaded)
                build_config_buffer[0] = end;

        return build_config_loaded;
}

static string_address build_key(string_address name)
{
        if (!build_config_loaded)
                return "";

        return build_key_from((string_address)build_config_buffer, name, null);
}

/*
        For scalars -- compiler, arch, kernel_image -- where two profiles both
        setting the value would silently concatenate into an unusable command.
*/
static string_address build_key_one(string_address name, bool address_to good)
{
        positive matched = 0;
        string_address answer;

        if (good)
                address_to good = true;

        if (!build_config_loaded)
                return "";

        answer = build_key_from((string_address)build_config_buffer, name,
                                address_of matched);

        if (matched > 1)
        {
                string_format(log_error,
                              "config: '%s' is set %p times; expected one value\n",
                              name, matched);

                if (good)
                        address_to good = false;

                return "";
        }

        return answer;
}

/*
        Composing a configuration out of profiles.

        The profiles are concatenated into one fragment, so the kernel's own
        merge_config sees a single file and can only compare it against the
        kernel's defaults -- it has no way to say that "gpu" and "console"
        asked for opposite things. The last value written wins, silently. This
        is the only place that comparison can be made, so it is made here.
*/
#define BUILD_PAIR_ROOM 16384

typedef struct build_pair
{
        string_address name;
        positive name_length;
        string_address value;
        positive value_length;
} build_pair;

static build_pair build_pairs[BUILD_PAIR_ROOM];
static positive build_pair_order[BUILD_PAIR_ROOM];
static positive build_pair_scratch[BUILD_PAIR_ROOM];

static bipolar build_pair_compare(positive left, positive right)
{
        build_pair address_to one = address_of build_pairs[left];
        build_pair address_to two = address_of build_pairs[right];
        positive shortest = one->name_length < two->name_length
                                    ? one->name_length
                                    : two->name_length;
        bipolar answer = memory_compare(one->name, two->name, shortest);

        if (answer)
                return answer;

        if (one->name_length != two->name_length)
                return one->name_length < two->name_length ? -1 : 1;

        shortest = one->value_length < two->value_length ? one->value_length
                                                         : two->value_length;
        answer = memory_compare(one->value, two->value, shortest);

        if (answer)
                return answer;

        if (one->value_length == two->value_length)
                return 0;

        return one->value_length < two->value_length ? -1 : 1;
}

//      Bottom-up merge, because the order this produces is the order the
//      report is read in and a comparison sort that is not stable would make
//      two runs over the same profiles disagree about which line came first.
static fn build_pair_sort(positive count)
{
        for (positive at = 0; at < count; at++)
                build_pair_order[at] = at;
        positive address_to sorted = array_merge_sort(
            build_pair_order, build_pair_scratch, count, build_pair_compare);
        if (sorted != build_pair_order)
                memory_copy_apart(build_pair_order, sorted,
                                   count * sizeof(build_pair_order[0]));
}

//      CONFIG_NAME=value, and only that. The name is CONFIG_ followed by a
//      run of capitals, digits and underscores that reaches an equals sign;
//      anything else on the line -- a comment, an "is not set", a lower case
//      letter in the middle of the name -- is not a setting this can compare.
static positive build_symbol_end(string_address line, positive at,
                                 positive length)
{
        while (at < length && ((line[at] >= 'A' && line[at] <= 'Z') ||
                               byte_is_digit(line[at]) || line[at] == '_'))
                at++;

        return at;
}

static bool build_config_pair(string_address line, positive length,
                              build_pair address_to into)
{
        positive at;

        if (length < 8 || !string_has_prefix(line, "CONFIG_"))
                return false;

        at = build_symbol_end(line, 7, length);

        if (at >= length || line[at] != '=' || at == 7)
                return false;

        into->name = line;
        into->name_length = at;
        into->value = line + at + 1;
        into->value_length = length - at - 1;

        return true;
}

static fn build_write_field(string_address text, positive width)
{
        positive length = string_length(text);

        string_format(log, "%s", text);

        while (length < width)
        {
                string_format(log, " ");
                length++;
        }
}

/*
        Where two profiles disagree.

        Every distinct name/value pair, sorted, deduplicated, and any name left
        with more than one is a disagreement. Each is then asked of every
        profile in turn -- the last value in a profile is that profile's
        answer, which is how merge_config resolves them too.
*/
static fn build_config_conflicts(string_address text,
                                 string_address address_to profiles,
                                 positive profile_count)
{
        build_lines walk;
        positive count = 0;
        bool announced = false;

        build_lines_open(address_of walk, text);

        while (build_lines_next(address_of walk) && count < BUILD_PAIR_ROOM)
                if (build_config_pair(walk.line, walk.length,
                                      address_of build_pairs[count]))
                        count++;

        build_pair_sort(count);

        for (positive at = 0; at < count;)
        {
                positive first = build_pair_order[at];
                positive distinct = 1;
                positive step = at + 1;

                //      Distinct pairs only: the same option set to the same
                //      value by two profiles is agreement, not conflict.
                while (step < count)
                {
                        positive here = build_pair_order[step];
                        positive before = build_pair_order[step - 1];

                        if (build_pairs[here].name_length !=
                                    build_pairs[first].name_length ||
                            memory_compare(build_pairs[here].name,
                                           build_pairs[first].name,
                                           build_pairs[first].name_length))
                                break;

                        if (build_pair_compare(here, before))
                                distinct++;

                        step++;
                }

                if (distinct > 1)
                {
                        string_address name = build_text_keep(
                                build_pairs[first].name, build_pairs[first].name_length);

                        if (!announced)
                        {
                                string_format(log, "\n");
                                string_format(log,
                                              "Profiles disagree -- the last value wins:\n");
                                announced = true;
                        }

                        string_format(log, "  %s\n", name);

                        for (positive which = 0; which < profile_count; which++)
                        {
                                string_address path =
                                        build_join(build_setting_get("profile_root"),
                                                   "/", profiles[which], null);
                                build_lines profile_walk;
                                string_address value = "";
                                bool found = false;

                                if (file_slurp(path, build_file_two,
                                                BUILD_FILE_ROOM) < 0)
                                        continue;

                                build_lines_open(address_of profile_walk,
                                                 (string_address)build_file_two);

                                while (build_lines_next(address_of profile_walk))
                                {
                                        positive want = string_length(name);
                                        positive have;

                                        if (profile_walk.length <= want ||
                                            memory_compare(profile_walk.line,
                                                           name, want) ||
                                            profile_walk.line[want] != '=')
                                                continue;

                                        have = profile_walk.length - want - 1;
                                        value = build_text_keep(
                                                profile_walk.line + want + 1, have);
                                        found = have > 0;
                                }

                                if (!found)
                                        continue;

                                string_format(log, "      ");
                                build_write_field(profiles[which], 14);
                                string_format(log, " %s\n", value);
                        }
                }

                at = step;
        }

        if (announced)
                string_format(log, "\n");

        log_flush();
}

/*
        The fragment merge_config is handed: the composed configuration with
        only the last assignment of each option.

        The composed file keeps every profile's lines, which is what the
        disagreement report above reads. merge_config appends a fragment as it
        stands, though, so each repeat reached olddefconfig, and olddefconfig
        said "override: reassigning to symbol" for it -- eighty-nine lines, each
        an agreement or a disagreement already reported. Kconfig keeps the last
        assignment, and a choice member's last assignment is also the one that
        sets its priority, so dropping the earlier ones leaves it nothing to
        decide differently.
*/
static bool build_config_name(string_address line, positive length,
                              build_pair address_to into)
{
        positive at;

        if (build_config_pair(line, length, into))
                return true;

        if (length <= 9 || !string_has_prefix(line, "# CONFIG_"))
                return false;

        at = build_symbol_end(line, 9, length);

        if (at == 9 || !memory_is_word(line + at, length - at, " is not set"))
                return false;

        into->name = line + 2;
        into->name_length = at - 2;
        into->value = null;
        into->value_length = 0;

        return true;
}

static bool build_config_fragment(string_address from, string_address to)
{
        build_lines walk;
        positive count = 0;
        positive seen = 0;
        positive used = 0;

        if (file_slurp(from, build_file_two, BUILD_FILE_ROOM) < 0)
                return false;

        build_lines_open(address_of walk, (string_address)build_file_two);

        while (build_lines_next(address_of walk))
        {
                if (count == BUILD_PAIR_ROOM)
                        return false;

                if (build_config_name(walk.line, walk.length,
                                      address_of build_pairs[count]))
                        count++;
        }

        build_lines_open(address_of walk, (string_address)build_file_two);

        while (build_lines_next(address_of walk))
        {
                build_pair here;
                bool later = false;

                if (build_config_name(walk.line, walk.length, address_of here))
                {
                        seen++;

                        for (positive after = seen; after < count && !later; after++)
                                later = build_pairs[after].name_length == here.name_length &&
                                        !memory_compare(build_pairs[after].name,
                                                        here.name, here.name_length);
                }

                if (later)
                        continue;

                memory_copy(build_file_one + used, walk.line, walk.length);
                used += walk.length;
                build_file_one[used++] = '\n';
        }

        return build_write_file(to, (string_address)build_file_one, used);
}

static b32 build_config(string_address address_to profiles, positive count)
{
        string_address artifacts = build_setting_get("artifacts");
        string_address target = build_join(artifacts, "/.config", null);
        string_address information = build_join(artifacts, "/info", null);
        bool missing = false;
        positive used = 0;

        if (!count)
        {
                string_format(log_error,
                              "Usage: build config <profile1> [profile2] [profile3] ...\n");
                return string_report(log_error, 1,
                                     "Example: build config any arch/x64 debug_none\n");
        }

        //      A missing profile used to print a warning and carry on,
        //      producing a config silently missing whole feature sets. Every
        //      one is checked before anything is written.
        for (positive at = 0; at < count; at++)
        {
                string_address path = build_join(build_setting_get("profile_root"),
                                                 "/", profiles[at], null);

                if (build_is_file(path))
                        continue;

                string_format(log_error, "config: no such profile: profile/%s\n",
                              profiles[at]);
                missing = true;
        }

        log_flush();

        if (missing)
                return 1;

        {
                string_address banner = "# Auto generated, do not edit.\n";

                used = string_length(banner);
                memory_copy(build_file_one, banner, used);
        }

        {
                bipolar got = file_slurp(information, build_file_two,
                                          BUILD_FILE_ROOM);

                if (got > 0)
                {
                        memory_copy(build_file_one + used, build_file_two,
                                    (positive)got);
                        used += (positive)got;
                }
        }

        for (positive at = 0; at < count; at++)
        {
                string_address path = build_join(build_setting_get("profile_root"),
                                                 "/", profiles[at], null);
                bipolar got;

                build_file_one[used++] = '\n';
                string_format(log, "Adding profile: %s\n", profiles[at]);
                got = file_slurp(path, build_file_two, BUILD_FILE_ROOM);

                if (got < 0)
                        return build_die(build_join("cannot read profile ",
                                                    profiles[at], null));

                if (used + (positive)got + 1 >= BUILD_FILE_ROOM)
                        return build_die("the composed configuration is too large");

                memory_copy(build_file_one + used, build_file_two,
                            (positive)got);
                used += (positive)got;
        }

        build_file_one[used] = end;
        log_flush();

        if (!build_write_file(target, (string_address)build_file_one, used))
                return build_die(build_join("cannot write ", target, null));

        build_config_conflicts((string_address)build_file_one, profiles, count);

        string_format(log, "Configuration generated at %s\n", target);
        log_flush();

        //      Every key below now comes out of what was just written.
        build_config_load();

        return 0;
}

/*
        Where the built kernel disagrees with what the profiles asked for.

        merge_config and olddefconfig drop unmet options silently, so a profile
        can ask for a driver, get no warning, and produce a kernel without it
        -- which shows up later as hardware that does not work.

        The other direction is quieter and was missed for longer. A great many
        kernel options read

                bool "Something" if EXPERT
                default y

        which means that without CONFIG_EXPERT the symbol is invisible, forced
        on, and a profile line saying =n is discarded without a word. That is
        how a kernel built from a profile named debug_none shipped SLUB_DEBUG
        for as long as it did.

        Profiles are read in the order they are composed and the last request
        for an option wins, which is how merge_config resolves them too, so a
        profile deliberately overriding an earlier one is not reported.
*/
#define BUILD_REQUEST_ROOM 8192

typedef struct build_request
{
        string_address name;
        positive name_length;
        p8 setting;
        string_address profile;
} build_request;

static build_request build_requests[BUILD_REQUEST_ROOM];
static build_request build_effective[BUILD_REQUEST_ROOM];

//      Names outlive the buffer the profile was read into and there are
//      thousands of them, so their pool resets with the profile request list.
#define BUILD_NAME_ROOM (1 << 20)

static p8 build_name_arena[BUILD_NAME_ROOM];
static memory_arena build_name_storage = {build_name_arena, BUILD_NAME_ROOM, 0};

static string_address build_name_keep(string_address text, positive length)
{
        if (length == positive_max)
                return null;
        p8 address_to into = memory_arena_take(address_of build_name_storage,
                                              length + 1, 1);
        if (!into)
                return null;
        memory_copy(into, text, length);
        into[length] = end;
        return (string_address)into;
}
static string_address build_built[BUILD_PAIR_ROOM];
static positive build_built_length[BUILD_PAIR_ROOM];

//      Only the tri-state settings can be checked this way. A string or an
//      integer option is left alone: it has no "present or absent" reading.
static bool build_tristate(string_address line, positive length,
                           build_request address_to into)
{
        build_pair pair;

        if (!build_config_pair(line, length, address_of pair))
                return false;

        if (pair.value_length != 1)
                return false;

        if (pair.value[0] != 'y' && pair.value[0] != 'm' && pair.value[0] != 'n')
                return false;

        into->name = pair.name;
        into->name_length = pair.name_length;
        into->setting = pair.value[0];

        return true;
}

static b32 build_verify_config(string_address config,
                               string_address address_to profiles,
                               positive profile_count)
{
        positive built = 0;
        positive requested = 0;
        positive effective = 0;
        positive missing = 0;
        positive lingering = 0;
        build_lines walk;

        build_name_storage.used = 0;

        if (!build_is_file(config))
                return string_report(log_error, 1, "verify_config: no such config: %s\n",
                                     config);

        if (file_slurp(config, build_file_one, BUILD_FILE_ROOM) < 0)
                return string_report(log_error, 1, "verify_config: cannot read %s\n",
                                     config);

        build_lines_open(address_of walk, (string_address)build_file_one);

        while (build_lines_next(address_of walk) && built < BUILD_PAIR_ROOM)
        {
                build_pair pair;

                if (!build_config_pair(walk.line, walk.length, address_of pair))
                        continue;

                if (pair.value_length != 1 ||
                    (pair.value[0] != 'y' && pair.value[0] != 'm'))
                        continue;

                build_built[built] = pair.name;
                build_built_length[built] = pair.name_length;
                built++;
        }

        //      The profiles are read into the second buffer one at a time, so
        //      a request keeps the name of the profile it came from but not a
        //      pointer into a buffer about to be reused. The names go into the
        //      text ring, which is why the room here is bounded.
        for (positive at = 0; at < profile_count; at++)
        {
                string_address path = build_join(build_setting_get("profile_root"),
                                                 "/", profiles[at], null);
                build_lines profile_walk;

                if (!build_is_file(path))
                        continue;

                if (file_slurp(path, build_file_two, BUILD_FILE_ROOM) < 0)
                        continue;

                build_lines_open(address_of profile_walk,
                                 (string_address)build_file_two);

                while (build_lines_next(address_of profile_walk) &&
                       requested < BUILD_REQUEST_ROOM)
                {
                        build_request one;
                        string_address keep;

                        if (!build_tristate(profile_walk.line,
                                            profile_walk.length,
                                            address_of one))
                                continue;

                        keep = build_name_keep(one.name, one.name_length);

                        if (!keep)
                                continue;

                        one.name = keep;
                        one.profile = profiles[at];
                        build_requests[requested++] = one;
                }
        }

        //      Last request wins, so read the list backwards and keep the
        //      first sighting. The order that leaves is the reverse of the
        //      request order, and it is the order the report is printed in --
        //      the shell this replaces did exactly the same walk, so a report
        //      that used to be read top to bottom still reads that way.
        for (positive back = requested; back > 0; back--)
        {
                build_request address_to one = address_of build_requests[back - 1];
                bool seen = false;

                for (positive kept = 0; kept < effective && !seen; kept++)
                        seen = build_effective[kept].name_length == one->name_length &&
                               !memory_compare(build_effective[kept].name, one->name,
                                               one->name_length);

                if (seen || effective >= BUILD_REQUEST_ROOM)
                        continue;

                build_effective[effective++] = address_to one;
        }

        {
                byte_store dropped = {build_file_two, BUILD_FILE_ROOM / 2 - 1, 0};
                byte_store forced = {build_file_two + BUILD_FILE_ROOM / 2,
                                     BUILD_FILE_ROOM / 2 - 1, 0};

                for (positive at = 0; at < effective; at++)
                {
                        build_request address_to one = address_of build_effective[at];
                        byte_store address_to list = null;
                        bool present = false;

                        for (positive which = 0; which < built && !present; which++)
                                present = build_built_length[which] == one->name_length &&
                                          !memory_compare(build_built[which], one->name,
                                                          one->name_length);

                        if ((one->setting == 'y' || one->setting == 'm') && !present)
                        {
                                list = address_of dropped;
                                missing++;
                        }
                        else if (one->setting == 'n' && present)
                        {
                                list = address_of forced;
                                lingering++;
                        }

                        if (list)
                        {
                                string_address line = build_join("  ", one->name, "  (",
                                                                 one->profile, ")\n", null);

                                byte_store_append_exact(list, line, string_length(line));
                        }
                }

                dropped.bytes[dropped.used] = end;
                forced.bytes[forced.used] = end;

                if (missing)
                {
                        string_format(log, BUILD_YELLOW
                                      "Requested but not in the built kernel:"
                                      BUILD_RESET "\n");
                        string_format(log, "%s", (string_address)dropped.bytes);
                }

                if (lingering)
                {
                        string_format(log, BUILD_YELLOW
                                      "Asked to be off but built in anyway:"
                                      BUILD_RESET "\n");
                        string_format(log, "%s", (string_address)forced.bytes);
                }
        }

        if (!missing && !lingering)
        {
                string_format(log, "All %p requested options took effect.\n",
                              effective);
                log_flush();
                return 0;
        }

        string_format(log, "\n");

        if (missing)
                string_format(log,
                              "%p of %p requested options were dropped -- usually an\n"
                              "unmet dependency, or a symbol renamed or removed in this kernel version.\n",
                              missing, effective);

        if (lingering)
                string_format(log,
                              "%p of %p options could not be turned off -- usually a\n"
                              "symbol that is invisible and forced on without CONFIG_EXPERT.\n",
                              lingering, effective);

        string_format(log, "Check either with:\n");
        string_format(log, "  grep -rn '^config OPTION$' -A5 linux/*/Kconfig*\n");
        log_flush();

        //      Not fatal: a profile composed for one architecture will
        //      legitimately carry options another cannot satisfy.
        return 0;
}

/*
        Glue -- one assembly source, every architecture.

        A .asm file holds the assembly for every architecture at once, split
        into blocks. This translates one down to a .S for a single
        architecture, which the kernel's own build then hands to whatever
        assembler that toolchain provides. Nothing here assembles anything and
        nothing here rewrites an instruction: a block holds the native syntax
        of its architecture verbatim, so real kernel assembly can be pasted in
        unchanged and an error from the assembler names an instruction that
        was actually written.

        The directives -- each of which is only a comment to the assembler,
        and none of which reach it:

            #> arch <name> [name ...]   begin a block for these architectures
            #> arch other               begin the block for every architecture
                                        no other block claimed
            #> shared                   go back to emitting for all of them

        A #> shared also closes the run of blocks before it, so a file can hold
        more than one function: each run gets its own architectures and its own
        "other". Everything before the first #> arch is shared.

        The file is read twice: once to find out whether any block claims this
        architecture, which is what decides whether the "other" block is
        emitted, and once to write. Deciding it at the end instead would move
        the "other" block to the end of the output.
*/
#define BUILD_ASM_GROUPS 512

typedef struct build_asm_group
{
        bool matched;
        bool other;
} build_asm_group;

static build_asm_group build_asm_groups[BUILD_ASM_GROUPS];
static positive build_asm_group_count;

static string_address build_arch_name(string_address word);

/*      An architecture as the asm grouper spells it, from the one table that
        says which words name one: this kept a table of its own, and x86-64
        and arm were a machine to build.sh and to --arch and an unknown
        architecture in a "#> arch" line. */
static string_address build_asm_normalize(string_address name)
{
        string_address machine = build_arch_name(name);

        if (!machine)
                return null;
        if (word_is(machine, "x64"))
                return "x86_64";
        if (word_is(machine, "arm64"))
                return "aarch64";
        return machine;
}

static bool build_asm_fail(string_address source, positive line,
                           string_address message)
{
        string_format(log_error, "asm: %s:%p: %s\n", source, line, message);

        return false;
}

static bool build_asm_pass(string_address text, string_address target,
                           string_address source, positive pass,
                           p8 address_to into, positive address_to used,
                           positive room)
{
        build_lines walk;
        positive line_number = 0;
        positive group = 0;
        bool open_group = false;
        bool emitting = false;
        bool marker = false;

        if (pass == 2)
        {
                for (positive at = 0; at < build_asm_group_count; at++)
                {
                        if (build_asm_groups[at].matched ||
                            build_asm_groups[at].other)
                                continue;

                        string_format(log_error, "asm: %s has no block for %s\n",
                                      source, target);
                        string_format(log_error,
                                      "asm: add \"#> arch %s\", or \"#> arch other\" to say there is nothing to do here\n",
                                      target);

                        return false;
                }

                //      What comes before the first #> arch belongs to everyone.
                emitting = true;
                marker = true;
        }

        build_lines_open(address_of walk, text);

        while (build_lines_next(address_of walk))
        {
                positive lead = 0;

                line_number++;

                if (lead < walk.length)
                        lead += string_span_max(walk.line + lead,
                                                walk.length - lead,
                                                string_set_blanks);

                //      A directive is any line whose first non blank is "#>".
                //      Keying on that rather than on column one lets a block be
                //      indented with the code it introduces.
                if (lead + 1 < walk.length && walk.line[lead] == '#' &&
                    walk.line[lead + 1] == '>')
                {
                        string_address words[BUILD_ARGUMENT_ROOM];
                        p8 address_to store = build_text_take(BUILD_WORD_ROOM);
                        positive at = lead + 2;
                        positive stop = walk.length;
                        positive count;
                        bool claimed = false;

                        if (at < stop)
                                at += string_span_max(walk.line + at,
                                                      stop - at,
                                                      string_set_blanks);

                        while (stop > at && byte_is_blank(walk.line[stop - 1]))
                                stop--;

                        count = build_words_of(walk.line + at, stop - at,
                                               (string_address address_to)words,
                                               BUILD_ARGUMENT_ROOM, store,
                                               BUILD_WORD_ROOM);

                        if (!count)
                                return build_asm_fail(source, line_number,
                                                      "#> with no directive");

                        if (word_is(words[0], "shared"))
                        {
                                if (count != 1)
                                        return build_asm_fail(source, line_number,
                                                              "#> shared takes no arguments");

                                emitting = true;
                                marker = true;

                                //      A #> shared closes the run of blocks
                                //      before it, so the next #> arch opens a
                                //      new one. That is what lets one file hold
                                //      more than one function.
                                open_group = false;
                                continue;
                        }

                        if (!word_is(words[0], "arch"))
                                return build_asm_fail(source, line_number,
                                                      build_join("unknown directive: #> ",
                                                                 words[0], null));

                        if (count < 2)
                                return build_asm_fail(source, line_number,
                                                      "#> arch names no architecture");

                        if (!open_group)
                        {
                                open_group = true;
                                group++;

                                if (group > BUILD_ASM_GROUPS)
                                        return build_asm_fail(source, line_number,
                                                              "too many blocks in one file");

                                if (pass == 1 && group > build_asm_group_count)
                                        build_asm_group_count = group;
                        }

                        if (word_is(words[1], "other"))
                        {
                                if (count != 2)
                                        return build_asm_fail(source, line_number,
                                                              "#> arch other cannot be combined with an architecture");

                                if (pass == 1)
                                {
                                        if (build_asm_groups[group - 1].other)
                                                return build_asm_fail(source, line_number,
                                                                      "a second #> arch other in one run of blocks");

                                        build_asm_groups[group - 1].other = true;
                                }

                                emitting = !build_asm_groups[group - 1].matched;
                                marker = true;
                                continue;
                        }

                        for (positive which = 1; which < count; which++)
                        {
                                string_address name;

                                if (word_is(words[which], "other"))
                                        return build_asm_fail(source, line_number,
                                                              "#> arch other cannot be combined with an architecture");

                                name = build_asm_normalize(words[which]);

                                if (!name)
                                        return build_asm_fail(source, line_number,
                                                              build_join("unknown architecture: ",
                                                                         words[which], null));

                                if (word_is(name, target))
                                        claimed = true;
                        }

                        if (pass == 1 && claimed)
                                build_asm_groups[group - 1].matched = true;

                        emitting = claimed;
                        marker = true;
                        continue;
                }

                //      The first reading is only after the directives above;
                //      its bodies are nothing.
                if (pass == 1)
                        continue;

                if (!emitting)
                {
                        marker = true;
                        continue;
                }

                /*
                        The assembler is told which line of the .asm this came
                        from, so a diagnostic names the file that was written
                        rather than the one that was generated. One marker per
                        run of kept lines is enough.

                        .linefile, and not the "# 12 \"file\"" form a .S would
                        normally carry, because that form does not survive the
                        trip: the output is preprocessed before it is
                        assembled, and cpp rewrites a # line that follows a
                        macro expansion to start with a space so the next stage
                        cannot read it as a directive. Every function here
                        opens with SYM_FUNC_START, which is a macro, so the
                        marker that matters most was precisely the one being
                        discarded. It has to stay one line: a marker says what
                        line the NEXT line is.
                */
                if (marker)
                {
                        //      Not text_line: that name is text.c's line
                        //      store, which a macro reads through.
                        string_address marker_line = build_join("\tasm_line(",
                                                              build_number(line_number),
                                                              ", \"", source,
                                                              "\")\n", null);
                        positive length = string_length(marker_line);

                        if (address_to used + length >= room)
                                return false;

                        memory_copy(into + address_to used, marker_line, length);
                        address_to used += length;
                        marker = false;
                }

                /*
                        A prose comment goes out as //, not #.

                        These files reach the C preprocessor, where a line
                        beginning with # is a directive. A comment starting
                        "#\tif that byte is the one asked for" is read as an
                        #if, and the error names a token in the middle of an
                        English sentence. # followed by whitespace is a comment
                        and # followed by a word is a directive, which is what
                        tells them apart.
                */
                {
                        bool prose = lead < walk.length &&
                                     walk.line[lead] == '#' &&
                                     (lead + 1 == walk.length ||
                                      byte_is_blank(walk.line[lead + 1]));
                        positive fill = address_to used;

                        if (fill + walk.length + 3 >= room)
                                return false;

                        if (prose)
                        {
                                memory_copy(into + fill, walk.line, lead);
                                fill += lead;
                                into[fill++] = '/';
                                into[fill++] = '/';
                                memory_copy(into + fill, walk.line + lead + 1,
                                            walk.length - lead - 1);
                                fill += walk.length - lead - 1;
                        }
                        else
                        {
                                memory_copy(into + fill, walk.line, walk.length);
                                fill += walk.length;
                        }

                        into[fill++] = '\n';
                        address_to used = fill;
                }
        }

        return true;
}

static b32 build_asm(string_address arch, string_address input,
                     string_address output)
{
        string_address target;
        string_address temporary = build_join(output, ".asm_tmp", null);
        positive used = 0;

        if (!arch || !*arch)
        {
                string_format(log_error,
                              "asm: no target architecture given for %s\n", input);
                return string_report(log_error, 1,
                                     "asm: nothing in the kernel config names an architecture this knows\n");
        }

        if (!build_is_file(input))
                return string_report(log_error, 1, "asm: no such file: %s\n", input);

        target = build_asm_normalize(arch);

        if (!target)
                return string_report(log_error, 1, "asm: unknown architecture: %s\n", arch);

        if (file_slurp(input, build_file_one, BUILD_FILE_ROOM) < 0)
                return string_report(log_error, 1, "asm: cannot read %s\n", input);

        build_asm_group_count = 0;
        memory_zero(build_asm_groups, sizeof(build_asm_groups));

        if (!build_asm_pass((string_address)build_file_one, target, input, 1,
                            build_file_two, address_of used, BUILD_FILE_ROOM))
                return 1;

        /*
                The banner is a C++ comment on purpose: the output is always a
                .S, so the preprocessor removes it before any assembler has an
                opinion about which character starts a comment.

                asm_line is how the line markers in the body reach an assembler
                that will take one. The clang assembler does not know
                .linefile and would stop on it, so the macro is the directive
                under one and nothing under the other.
        */
        {
                string_address banner = build_join(
                        "// Generated by build asm from ", input, " for ", arch,
                        ". Do not edit.\n"
                        "// Edit the .asm and build again; this file is overwritten.\n"
                        "#ifdef __clang__\n"
                        "#define asm_line(number, file)\n"
                        "#else\n"
                        "#define asm_line(number, file) .linefile number file\n"
                        "#endif\n"
                        "#ifdef MOONWATER_FREESTANDING_ASM\n"
                        "#define SYM_FUNC_START(name) .globl name ; .balign 16 ; name:\n"
                        "#define SYM_FUNC_END(name) .size name, . - name\n"
                        "#define EXPORT_SYMBOL(name)\n"
                        "#define RET ret\n"
                        "#endif\n",
                        null);

                used = string_length(banner);
                memory_copy(build_file_two, banner, used);
        }

        if (!build_asm_pass((string_address)build_file_one, target, input, 2,
                            build_file_two, address_of used, BUILD_FILE_ROOM))
                return 1;

        //      Written beside the output so the rename is atomic and a failed
        //      run leaves the previous .S alone rather than a half written one
        //      the build would trust.
        if (!build_write_file(temporary, (string_address)build_file_two, used))
                return string_report(log_error, 1, "asm: cannot write %s\n", temporary);

        if (rename(temporary, output) < 0)
        {
                unlink(temporary);
                return string_report(log_error, 1, "asm: cannot rename %s to %s\n",
                                     temporary, output);
        }

        return 0;
}

//      The spelling printf gives %#x, which is what the packer's line has
//      always shown. Zero would print bare there; nothing here is ever zero,
//      because the caller refuses an image whose base or entry is.
static string_address build_hex(positive value)
{
        p8 address_to into = build_text_take(32);
        positive length;

        into[0] = '0';
        into[1] = 'x';
        length = positive_into_base(into + 2, value, 16, false);
        into[2 + length] = end;

        return (string_address)into;
}

//      Six letters out of the kernel's own entropy. The retry and the error
//      that survives it are system_random_fill's, asked with no flags so it
//      waits for the pool rather than mixing a fallback in: this names a
//      directory a privileged build is about to trust.
static bool build_random_marks(p8 address_to marks)
{
        bipolar got = system_random_fill(marks, SPOOL_TEMPLATE_MARKS, 0);

        if (got)
        {
                errno = (b32)-got;
                return false;
        }

        memory_translate(marks, SPOOL_TEMPLATE_MARKS,
                         (address_any)spool_name_table);
        return true;
}

//      A working directory nobody else has. Privileged execution ignores
//      environment-selected parents and requires blocking kernel entropy;
//      ordinary builds retain the standard temporary-family helper.
static string_address build_temporary_directory(string_address tag)
{
        string_address root = string_get_environment(environ, "TMPDIR");
        string_address path;
        positive user = (positive)getuid();
        positive effective_user = (positive)geteuid();
        positive group = (positive)getgid();
        positive effective_group = (positive)getegid();
        bool privileged = effective_user == 0 || effective_user != user ||
                          effective_group != group;

        if (privileged || !root || !*root)
                root = "/tmp";

        path = build_join(root, "/", tag, ".XXXXXX", null);

        if (!privileged)
        {
                string_address made = mkdtemp(path);

                if (!made)
                        return null;
                if (chmod(made, 0700) >= 0)
                        return made;

                rmdir(made);
                return null;
        }

        for (positive attempt = 0; attempt < TMP_MAX; attempt++)
        {
                p8 address_to marks = (p8 address_to)path +
                                      string_length(path) -
                                      SPOOL_TEMPLATE_MARKS;

                if (!build_random_marks(marks))
                        return null;

                bipolar made = system_make_directory_exact_at(
                    AT_FDCWD, path, 0700);

                if (made >= 0)
                        return path;

                if (made != -EEXIST)
                        return null;
        }

        errno = EEXIST;
        return null;
}

//      Removing a tree is our rm, called rather than spawned. If ours is
//      wrong the build leaves debris, which is the point of using it.
static fn build_remove_tree(string_address path)
{
        build_tool("rm", "-rf", path, null);
}

//      Zeroes from where the region's content ended to where the page it
//      occupies does. Every spark region is a whole number of pages.
static bool build_pad(b32 handle, positive from, positive to)
{
        p8 zeroes[4096];

        memory_zero(zeroes, sizeof(zeroes));

        while (from < to)
        {
                positive want = to - from;
                bipolar put;

                if (want > sizeof(zeroes))
                        want = sizeof(zeroes);

                put = write(handle, zeroes, want);

                if (put <= 0)
                        return false;

                from += (positive)put;
        }

        return true;
}

/*
        Splitting a value that arrived as one word.

        Flag lists ride in the configuration as a single key -- "#> flags -a -b"
        -- and reach a compiler as separate arguments. The shell got that by
        leaving the expansion unquoted, which is also how it got the empty
        string turning into no argument at all rather than one empty one.
*/
static positive build_split(string_address text, string_address address_to into,
                            positive room)
{
        p8 address_to store = build_text_take(BUILD_WORD_ROOM);

        if (!text)
                return 0;

        return build_words_of(text, string_length(text), into, room, store,
                              BUILD_WORD_ROOM);
}

//      Appending split words onto an argument vector under construction.
static positive build_add_split(string_address address_to words, positive count,
                                positive room, string_address text)
{
        string_address pieces[BUILD_ARGUMENT_ROOM];
        positive found = build_split(text, (string_address address_to)pieces,
                                     BUILD_ARGUMENT_ROOM);

        for (positive at = 0; at < found && count + 1 < room; at++)
                words[count++] = pieces[at];

        return count;
}

/*
        Linking a program of this tree's own shape.

        Two link recipes live here because the tree has two kinds of program.
        The freestanding one is an ordinary static ELF and takes link-time
        optimisation. The spark one must not: -flto discards the section
        layout the linker script depends on, and the packer below reads that
        layout back out of the object.

        Neither recipe names this project. The script, the entry symbol and
        the per-architecture flags are settings; a tree with another linker
        script and another entry gets the same two recipes.
*/
static string_address build_compiler()
{
        string_address named = build_key_one("compiler", null);

        if (named && *named)
                return named;

        named = string_get_environment(environ, "CC");

        return named && *named ? named : (string_address)"gcc";
}

/*
        binutils has to match the compiler, not the machine this runs on.

        objdump, readelf and objcopy all read the ELF the compiler just
        produced. Building for another architecture with the host's copies
        gets as far as "objcopy: Unable to recognise the architecture of the
        input file", so the prefix comes off the compiler's own name:
        aarch64-linux-gnu-gcc means aarch64-linux-gnu-objcopy. A plain "gcc"
        leaves the prefix empty, which is the native case.

        Prefer the matching tool, fall back to the host's.
        x86_64-linux-gnu-gcc is a perfectly ordinary way to name a native
        compiler and there is usually no x86_64-linux-gnu-objdump beside it,
        only objdump -- which is the same program. Insisting on the prefix
        breaks the native build to fix the cross one.
*/
static string_address build_binutil(string_address compiler,
                                    string_address name)
{
        p8 address_to prefix = build_text_take(string_length(compiler) + 1);
        positive length = string_length(compiler);
        string_address candidate;

        if (length > 3 && !memory_compare(compiler + length - 3, "gcc", 3))
                length -= 3;
        else if (length > 5 && !memory_compare(compiler + length - 5, "clang", 5))
                length -= 5;
        else if (length > 2 && !memory_compare(compiler + length - 2, "cc", 2))
                length -= 2;

        memory_copy(prefix, compiler, length);
        prefix[length] = end;
        candidate = build_join((string_address)prefix, name, null);

        if (build_have(candidate))
                return candidate;

        if (build_have(name))
                return name;

        string_format(log_error, "spark: neither %s%s nor %s found\n",
                      (string_address)prefix, name, name);

        return null;
}

/*
        Compiling C to the flat spark format.

        The layout is described in src/moonwater/spark.c, which the kernel loader
        includes too, so the two sides cannot drift apart -- and this file
        includes it as well, so the header written here is that struct rather
        than a second description of it. Every region is a whole number of
        pages; see the linker script for why.
*/
static bool build_hex_field(string_address text, positive length,
                             positive address_to answer)
{
        if (!length)
                return false;
        positive prefix = length > 2 && text[0] == '0' &&
                          (text[1] == 'x' || text[1] == 'X') ? 2 : 0;
        positive used;
        positive value = string_digits_hexadecimal_max(
            text + prefix, length - prefix, address_of used);
        if (!used || used != length - prefix)
                return false;
        address_to answer = value;
        return true;
}

//      objdump -h has a stable column layout; readelf -S splits "[ 1]" into
//      two fields for single digit indices and one for double, which does not
//      parse. The table is read once: invoking objdump and awk once per field
//      made the packer parse the same ELF five times.
static bool build_section(string_address table, string_address name,
                          positive address_to size, positive address_to where)
{
        string_address words[BUILD_ARGUMENT_ROOM];
        positive count;
        build_lines walk;

        address_to size = 0;
        address_to where = 0;

        build_lines_open(address_of walk, table);

        while (build_lines_words(address_of walk, (string_address address_to)words,
                                 address_of count))
        {
                if (count < 4 || !word_is(words[1], name))
                        continue;

                return build_hex_field(words[2], string_length(words[2]), size) &&
                       build_hex_field(words[3], string_length(words[3]), where);
        }

        return true;
}

static positive build_page_up(positive value)
{
        return ((value + SPARK_PAGE - 1) / SPARK_PAGE) * SPARK_PAGE;
}

static b32 build_spark(string_address source, string_address output,
                       string_address mode)
{
        string_address compiler = build_compiler();
        string_address arch = build_key_one("arch", null);
        string_address script = build_setting_get("link_script");
        string_address entry_flag;
        string_address objdump;
        string_address objcopy;
        string_address readelf;
        string_address words[BUILD_ARGUMENT_ROOM];
        string_address work;
        string_address elf;
        string_address text_binary;
        string_address data_binary;
        positive count = 0;
        positive text_bytes = 0;
        positive text_where = 0;
        positive data_bytes = 0;
        positive data_where = 0;
        positive bss_bytes = 0;
        positive bss_where = 0;
        positive base;
        positive entry = 0;
        positive text_end;
        positive text_size;
        positive data_size;
        positive bss_size;
        string_address hook = null;
        struct header head;

        if (!arch || !*arch)
                return string_report(log_error, 1, "spark: no '#> arch' in %s\n",
                                     build_in("artifacts", ".config"));

        objdump = build_binutil(compiler, "objdump");
        readelf = build_binutil(compiler, "readelf");
        objcopy = build_binutil(compiler, "objcopy");

        if (!objdump || !readelf || !objcopy)
                return 1;

        if (!build_is_file(script))
                return string_report(log_error, 1, "spark: missing linker script at %s\n",
                                     script);

        build_label(BUILD_YELLOW, "EXPERIMENTAL! C compiled to spark format");
        string_format(log, BUILD_BOLD "Compiling %s" BUILD_RESET "\n", output);
        log_flush();

        work = build_temporary_directory("spark");

        if (!work)
                return build_die("spark: cannot make a working directory");

        elf = build_join(work, "/image.elf", null);
        text_binary = build_join(work, "/text.bin", null);
        data_binary = build_join(work, "/data.bin", null);
        entry_flag = build_join("-Wl,-e,", build_setting_get("entry"), null);

        /*
                The coverage profile (`#> coverage on`, x86_64 only). Every
                basic block of the program calls the hook test/checks.c keeps
                for `sh test/run coverage`, built here to make and record
                into /coverage.map on the guest's RAM root, where PID 1 is
                already able to write. The linked ELF, symbols and lines
                kept, goes to dist/shell.coverage.elf, which is what the
                report reads the guest's record against -- so the strip
                flags are dropped for it, and nothing else changes.
        */
        if (mode && word_is(mode, "coverage"))
        {
                hook = build_join(work, "/coverage.o", null);

                if (!word_is(arch, "x86_64"))
                {
                        build_remove_tree(work);
                        return string_report(log_error, 1,
                                             "spark: coverage records are taken on "
                                             "x86_64, and this is %s\n", arch);
                }

                if (build_run(compiler, "-O2", "-c", "-march=x86-64",
                              "-fno-stack-protector", "-fno-builtin",
                              "-DCOVERAGE_hook", "-DCOVERAGE_CREATE",
                              "-DCOVERAGE_PATH=\"/coverage.map\"",
                              "test/checks.c", "-o", hook, null))
                {
                        build_remove_tree(work);
                        return string_report(log_error, 1,
                                             "spark: the coverage hook did not build\n");
                }
        }

        words[count++] = compiler;
        words[count++] = build_join(source, ".c", null);
        words[count++] = "-o";
        words[count++] = elf;

        //      -flto discards the section layout the linker script depends on,
        //      and everything below reads that layout back.
        {
                string_address pieces[BUILD_ARGUMENT_ROOM];
                positive found = build_split(build_key("program_flags"),
                                             (string_address address_to)pieces,
                                             BUILD_ARGUMENT_ROOM);
                bool keep_lines = mode && word_is(mode, "coverage");

                for (positive at = 0; at < found && count + 1 < BUILD_ARGUMENT_ROOM;
                     at++)
                        if (!word_is(pieces[at], "-flto") &&
                            !(keep_lines && (word_is(pieces[at], "-s") ||
                                             word_is(pieces[at], "-Wl,--strip-all") ||
                                             word_is(pieces[at], "-Wl,--strip-debug") ||
                                             word_is(pieces[at], "-Wl,-x") ||
                                             word_is(pieces[at], "-Wl,-s"))))
                                words[count++] = pieces[at];
        }

        if (mode && word_is(mode, "debug"))
                words[count++] = "-g";

        if (hook)
        {
                words[count++] = "-g";
                words[count++] = "-fsanitize-coverage=trace-pc";
                words[count++] = hook;
        }

        //      The configuration header the image build wrote, then what
        //      the caller's environment adds: SPARK_CPPFLAGS=-DBENCH_...
        //      ./build spark is how the benchmarks in test/checks.c are
        //      built.
        {
                string_address config = build_setting_get("spark_config");

                if (config && *config && count + 1 < BUILD_ARGUMENT_ROOM)
                        words[count++] = config;
        }
        count = build_add_split((string_address address_to)words, count,
                                BUILD_ARGUMENT_ROOM,
                                string_get_environment(environ,
                                                       "SPARK_CPPFLAGS"));

        words[count++] = "-static";
        words[count++] = "-nostdlib";
        words[count++] = "-nostartfiles";
        words[count++] = "-T";
        words[count++] = script;
        words[count++] = "-Wl,--build-id=none";
        words[count++] = entry_flag;
        words[count++] = "-Wl,--no-warn-rwx-segments";
        words[count] = null;

        if (build_run_words((string_address address_to)words))
        {
                build_remove_tree(work);
                return string_report(log_error, 1, "spark: compilation failed\n");
        }

        count = 0;
        words[count++] = objdump;
        words[count++] = "-h";
        words[count++] = elf;
        words[count] = null;

        if (build_capture_words((string_address address_to)words, build_file_one,
                                BUILD_FILE_ROOM) < 0)
        {
                build_remove_tree(work);
                return 1;
        }

        build_section((string_address)build_file_one, ".text",
                      address_of text_bytes, address_of text_where);
        build_section((string_address)build_file_one, ".data",
                      address_of data_bytes, address_of data_where);
        build_section((string_address)build_file_one, ".bss",
                      address_of bss_bytes, address_of bss_where);

        count = 0;
        words[count++] = readelf;
        words[count++] = "-h";
        words[count++] = "-W";
        words[count++] = elf;
        words[count] = null;

        if (build_capture_words((string_address address_to)words, build_file_two,
                                BUILD_FILE_ROOM) >= 0)
        {
                string_address found[BUILD_ARGUMENT_ROOM];
                positive parts;
                build_lines walk;

                build_lines_open(address_of walk, (string_address)build_file_two);

                while (build_lines_words(address_of walk,
                                         (string_address address_to)found,
                                         address_of parts))
                {
                        if (parts &&
                            memory_search(walk.line, walk.length, "Entry point", 11))
                                build_hex_field(found[parts - 1],
                                                string_length(found[parts - 1]),
                                                address_of entry);
                }
        }

        //      The image is mapped from SPARK_HEADER_SIZE bytes before .text:
        //      that is where the header sits, and the linker script reserves
        //      exactly that much.
        base = text_where - SPARK_HEADER_SIZE;

        if (!text_where || !entry)
        {
                build_remove_tree(work);
                return string_report(log_error, 1,
                                     "spark: could not read base/entry from the linked image\n");
        }

        //      A program need not have every section: duck has no .data at
        //      all. Text runs up to whichever region actually follows it, or
        //      to its own end if none does.
        if (data_bytes > 0)
                text_end = data_where;
        else if (bss_bytes > 0)
                text_end = bss_where;
        else
                text_end = text_where + text_bytes;

        text_size = build_page_up(text_end - base);
        data_size = build_page_up(data_bytes);
        bss_size = build_page_up(bss_bytes);

        if (!text_size)
        {
                build_remove_tree(work);
                return string_report(log_error, 1,
                                     "spark: computed a non positive text size (%p)\n",
                                     text_size);
        }

        if (build_run(objcopy, "-O", "binary", "--only-section=.text", elf,
                      text_binary, null))
        {
                build_remove_tree(work);
                return 1;
        }

        if (build_run(objcopy, "-O", "binary", "--only-section=.data", elf,
                      data_binary, null))
                build_write_file(data_binary, "", 0);

        head.magic = SPARK_MAGIC;
        head.version = SPARK_VERSION;
        head.flags = 0;
        head.base = base;
        head.entry = entry;
        head.text_size = text_size;
        head.data_size = data_size;
        head.bss_size = bss_size;
        head.reserved[0] = 0;
        head.reserved[1] = 0;

        {
                b32 handle = open(output, O_WRONLY | O_CREAT | O_TRUNC, 0755);
                bipolar got;
                bool good;

                if (handle < 0)
                {
                        build_remove_tree(work);
                        return string_report(log_error, 1, "spark: cannot write %s\n",
                                             output);
                }

                //      The header occupies the first SPARK_HEADER_SIZE bytes of
                //      the text region itself, so the image carries no page
                //      that nothing maps.
                good = write(handle, address_of head, SPARK_HEADER_SIZE) ==
                       SPARK_HEADER_SIZE;
                got = file_slurp(text_binary, build_file_one, BUILD_FILE_ROOM);

                if (got > 0)
                        good = good && write(handle, build_file_one,
                                             (positive)got) == got;

                good = good && build_pad(handle, (positive)(got > 0 ? got : 0),
                                         text_size - SPARK_HEADER_SIZE);

                if (data_size > 0)
                {
                        got = file_slurp(data_binary, build_file_one,
                                          BUILD_FILE_ROOM);

                        if (got > 0)
                                good = good && write(handle, build_file_one,
                                                     (positive)got) == got;

                        good = good && build_pad(handle,
                                                 (positive)(got > 0 ? got : 0),
                                                 data_size);
                }

                close(handle);

                if (good && hook &&
                    (build_tool("mkdir", "-p", "dist", null) ||
                     build_tool("cp", elf, "dist/shell.coverage.elf", null)))
                {
                        build_remove_tree(work);
                        return string_report(log_error, 1,
                                             "spark: cannot keep the coverage ELF\n");
                }

                build_remove_tree(work);

                if (!good)
                        return string_report(log_error, 1, "spark: writing %s failed\n",
                                             output);
        }

        string_format(log, "spark: base=%s entry=%s text=%p data=%p bss=%p\n",
                      build_hex(base), build_hex(entry), text_size,
                      data_size, bss_size);
        log_flush();
        build_size(output);
        string_format(log, "\n");
        log_flush();

        return 0;
}

//      SIGTERM. The shell's header names the three signals it traps and this
//      is not one of them, so it is spelled here rather than borrowed.
#define BUILD_SIGNAL_TERMINATE 15

//      getcwd has no wrapper in the standard layer, and the only caller is
//      the default output name, which is the working directory's own.
static string_address build_working_directory()
{
        p8 address_to into = build_text_take(4096);
        bipolar got = (bipolar)system_call_2(syscall(getcwd), (positive)into,
                                             4096);

        if (got <= 0)
                return ".";

        into[got ? got - 1 : 0] = end;

        return (string_address)into;
}

static string_address build_directory_of(string_address path)
{
        string_address copy = build_join(path, null);
        p8 address_to cut = (p8 address_to)string_last_of(copy, '/');

        if (!cut)
                return ".";

        address_to cut = end;

        return copy[0] ? copy : (string_address)"/";
}

static string_address build_name_of(string_address path)
{
        string_address cut = string_last_of(path, '/');

        return cut ? cut + 1 : path;
}

/*
        Building one freestanding binary, and optionally running it.

        This is the ordinary static link, not the spark one: it takes link
        time optimisation, which the spark path must not, because -flto
        discards the section layout that linker script depends on.
*/
static string_address build_whole_program_flags(string_address compiler)
{
        string_address words[4];

        words[0] = compiler;
        words[1] = "--version";
        words[2] = null;

        if (build_capture_words((string_address address_to)words, build_file_two,
                                BUILD_FILE_ROOM) < 0)
                return "";

        {
                positive length = string_length((string_address)build_file_two);

                //      clang first: it answers "clang version" and also names
                //      GCC nowhere, while gcc's banner says gcc and GCC both.
                if (memory_search(build_file_two, length, "clang", 5) ||
                    memory_search(build_file_two, length, "Clang", 5))
                        return "";

                if (memory_search(build_file_two, length, "gcc", 3) ||
                    memory_search(build_file_two, length, "GCC", 3))
                        return build_setting_get("whole_program_flags");
        }

        return "";
}

static b32 build_freestanding_link(string_address source, string_address output,
                                   bool loud)
{
        string_address compiler = build_compiler();
        string_address words[BUILD_ARGUMENT_ROOM];
        positive count = 0;

        build_tool("mkdir", "-p", build_directory_of(output), null);

        words[count++] = compiler;
        words[count++] = source;
        words[count++] = "-o";
        words[count++] = output;
        count = build_add_split((string_address address_to)words, count,
                                BUILD_ARGUMENT_ROOM,
                                build_setting_get("freestanding_flags"));
        count = build_add_split((string_address address_to)words, count,
                                BUILD_ARGUMENT_ROOM,
                                build_whole_program_flags(compiler));
        count = build_add_split((string_address address_to)words, count,
                                BUILD_ARGUMENT_ROOM,
                                build_setting_get("freestanding_flags_tail"));
        words[count++] = build_join("-Wl,-e,", build_setting_get("entry"), null);
        words[count] = null;

        if (build_run_words((string_address address_to)words))
                return string_report(log_error, 1, "build: compilation failed\n");

        build_tool("chmod", "+x", output, null);

        if (loud)
                build_size(output);

        return 0;
}

/*
        Watching.

        The pid of what was started is tracked rather than matched by name.
        Matching on the output path used to catch anything whose command line
        merely contained it -- including the watcher and this program.
*/
static b32 build_watch_application;
static b32 build_watch_watcher;

static fn build_stop(b32 address_to child)
{
        if (address_to child <= 0)
                return;

        kill(address_to child, BUILD_SIGNAL_TERMINATE);
        build_wait(address_to child);
        address_to child = 0;
}

/*
        Being told to stop.

        Both children are ours and neither ends on its own: the watcher runs
        until it is killed and the replacement runs until it is replaced. A
        watch that exits without taking them leaves two processes spinning on
        somebody's machine, which is what the shell's EXIT/INT/TERM traps were
        there to prevent. The statuses are the shell's too -- 130 for an
        interrupt, 143 for a termination -- because that is what a caller
        reads to tell one from the other.
*/
static fn build_watch_caught(b32 number)
{
        build_stop(address_of build_watch_application);
        build_stop(address_of build_watch_watcher);
        exit(number == SIGNAL_INTERRUPT ? 130 : 143);
}

static b32 build_freestanding(string_address address_to arguments, positive count)
{
        string_address source = null;
        string_address output = null;
        bool loud = false;
        bool run = false;
        bool watch = false;
        bool options = true;
        positive positional = 0;

        for (positive at = 0; at < count; at++)
        {
                string_address word = arguments[at];

                if (options)
                {
                        if (word_is(word, "-v"))
                        {
                                loud = true;
                                continue;
                        }

                        if (word_is(word, "--run"))
                        {
                                run = true;
                                continue;
                        }

                        if (word_is(word, "--watch"))
                        {
                                run = true;
                                watch = true;
                                continue;
                        }

                        if (word_is(word, "--"))
                        {
                                options = false;
                                continue;
                        }

                        if (word[0] == '-' && word[1] == '-')
                                return string_report(log_error, 1,
                                                     "build: unknown option %s\n", word);
                }

                if (positional == 0)
                        source = word;
                else if (positional == 1)
                        output = word;
                else
                        return string_report(log_error, 1, "build: too many paths\n");

                positional++;
        }

        if (!source)
                source = build_setting_get("freestanding_source");

        if (!output)
                output = build_join(build_setting_get("freestanding_output"), "/",
                                    build_name_of(build_working_directory()),
                                    null);

        //      The compiler accepts a bare output filename; executing it must
        //      still refer to this directory rather than searching PATH for a
        //      different program.
        if (output[0] != '/' &&
            !(output[0] == '.' && (output[1] == '/' ||
                                   (output[1] == '.' && output[2] == '/'))))
                output = build_join("./", output, null);

        if (!build_is_file(source))
                return string_report(log_error, 1, "build: no such source file: %s\n",
                                     source);

        if (!watch)
        {
                b32 answer = build_freestanding_link(source, output, loud);

                if (answer || !run)
                        return answer;

                answer = build_run(output, null);

                if (answer)
                {
                        string_format(log, "Exited with %p\n", (positive)answer);
                        log_flush();
                }

                return answer;
        }

        {
                string_address watcher;
                string_address directory = build_directory_of(source);
                string_address words[10];
                b32 pair[2];
                build_command watch = {.words = (string_address address_to)words};
                positive at = 0;

                if (build_have("inotifywait"))
                        watcher = "inotifywait";
                else if (build_have("fswatch"))
                        watcher = "fswatch";
                else
                        return string_report(log_error, 1,
                                             "build: --watch needs inotifywait (inotify-tools) or fswatch\n");

                if (system_pipe(pair, O_CLOEXEC) < 0)
                        return 1;

                words[at++] = watcher;

                if (word_is(watcher, "inotifywait"))
                {
                        words[at++] = "-q";
                        words[at++] = "-m";
                        words[at++] = "-r";
                        words[at++] = "-e";
                        words[at++] = "modify,create,delete,move";
                }
                else
                        words[at++] = "-r";

                words[at++] = directory;
                words[at] = null;
                watch.output = pair[1];
                build_watch_watcher = build_start(address_of watch);
                close(pair[1]);
                system_signal_install(SIGNAL_INTERRUPT,
                                      (positive)build_watch_caught,
                                      SIGNAL_CATCH_FLAGS, SIGNAL_CATCH_RESTORER,
                                      null);
                system_signal_install(BUILD_SIGNAL_TERMINATE,
                                      (positive)build_watch_caught,
                                      SIGNAL_CATCH_FLAGS, SIGNAL_CATCH_RESTORER,
                                      null);

                while (true)
                {
                        p8 byte = 0;
                        bipolar got;

                        build_stop(address_of build_watch_application);
                        string_format(log, "\033[H\033[2J");
                        log_flush();

                        if (!build_freestanding_link(source, output, loud))
                        {
                                string_address only[2] = {output, null};
                                build_command run = {.words = only};

                                build_watch_application = build_start(address_of run);
                        }
                        else
                                string_format(log_error,
                                              "build: waiting for the next change\n");

                        //      One line of the watcher's output is one change.
                        //      A byte at a time, because a buffered read can
                        //      hold two events and rebuild once for both.
                        do
                                got = read(pair[0], address_of byte, 1);
                        while (got == 1 && byte != '\n');

                        if (got <= 0)
                                break;
                }

                build_stop(address_of build_watch_application);
                build_stop(address_of build_watch_watcher);
                close(pair[0]);
        }

        return 0;
}

/*
        The ISA floor, proved rather than asserted.

        Compile the library at the floor it advertises and read the ISA
        attribute back out of the object. A normal toolchain defaults to
        something richer -- rv64gc, say -- which would let both a compressed
        instruction and an extension above the floor enter unnoticed. The
        explicit march makes the assembler reject those; the attribute check
        proves a driver default did not put them back.

        This is the piece nothing else does, and it is why it belongs in the
        tool rather than in a test lane: what must be present and what must be
        absent are settings, so another tree points them at its own floor and
        gets the same proof. It answers 2 when nothing here has that back end,
        which is a skip and not a pass -- a check that manufactures a pass when
        it cannot run is worse than no check.

        An extension is present when the ISA string carries it as its own
        component: gcc writes rv64i2p1_m2p0_a2p1_f2p2_d2p2_zicsr2p0_zicntr2p0,
        so the components are separated by underscores and each is a name
        followed by its version. The first carries the rvNN base in front of
        it. Reading it this way rather than by substring is what keeps "a"
        from matching the "a" inside "zicsr" -- and keeps "c" from matching
        the one in "zicntr", which is the check that matters most here.
*/
static bool build_isa_holds(string_address isa, string_address name)
{
        positive at = 0;
        positive length = string_length(isa);
        positive want = string_length(name);

        //      Past the rvNN that opens the string; everything after is
        //      components separated by underscores.
        if (length > 2 && isa[0] == 'r' && isa[1] == 'v')
        {
                at = 2;

                if (at < length)
                        at += string_span_max(isa + at, length - at,
                                              string_set_digits);
        }

        while (at < length)
        {
                positive letters = string_span_max(isa + at, length - at,
                                                   string_set_alpha);

                if (letters == want && !memory_compare(isa + at, name, want))
                        return true;

                //      On to the next underscore, or the end.
                at += memory_span_without_byte(isa + at, '_', length - at);

                at += memory_span_byte(isa + at, '_', length - at);
        }

        return false;
}

static b32 build_floor(string_address arch)
{
        string_address source = build_setting_get("floor_source");
        string_address march = build_setting_get("floor_march");
        string_address mabi = build_setting_get("floor_mabi");
        string_address require = build_setting_get("floor_require");
        string_address forbid = build_setting_get("floor_forbid");
        string_address prefix = build_setting_get("floor_prefix");
        string_address compiler = null;
        string_address reader = null;
        string_address target = null;
        string_address work;
        string_address object;
        string_address words[BUILD_ARGUMENT_ROOM];
        string_address attributes = null;
        positive count = 0;
        b32 answer = 0;

        if (!arch || !*arch)
                arch = build_setting_get("floor_arch");

        {
                string_address named = build_join(arch, "-linux-gnu-gcc", null);

                if (build_have(named))
                {
                        compiler = named;
                        reader = build_join(arch, "-linux-gnu-readelf", null);
                }
        }

        work = build_temporary_directory("floor");

        if (!work)
                return build_die("floor: cannot make a working directory");

        object = build_join(work, "/floor.o", null);

        if (!compiler)
        {
                //      Apple clang does not carry every back end. Try the
                //      clang on PATH, then the two usual package manager
                //      locations; an empty translation unit separates an
                //      unavailable target from a real failure to compile.
                string_address tries[4];
                string_address named = string_get_environment(environ, "CLANG");
                positive which = 0;

                tries[which++] = named && *named ? named : (string_address)"clang";
                tries[which++] = "/opt/homebrew/opt/llvm/bin/clang";
                tries[which++] = "/usr/local/opt/llvm/bin/clang";
                tries[which] = null;

                target = build_join("--target=", arch, "-unknown-linux-gnu", null);

                for (which = 0; tries[which]; which++)
                {
                        if (!build_have(tries[which]))
                                continue;

                        if (build_run(tries[which], target,
                                      build_join("-march=", march, null),
                                      build_join("-mabi=", mabi, null),
                                      "-x", "c", "-c", "-o",
                                      build_join(work, "/probe.o", null),
                                      "/dev/null", null))
                                continue;

                        compiler = tries[which];
                        reader = build_join(build_directory_of(
                                                    build_resolve(tries[which])),
                                            "/llvm-readelf", null);
                        break;
                }

                if (!compiler)
                {
                        build_remove_tree(work);
                        string_format(log,
                                      "%s floor: NOT RUN -- no compiler with a %s back end\n",
                                      arch, arch);
                        log_flush();
                        return 2;
                }
        }

        if (!build_have(reader))
        {
                if (build_have("llvm-readelf"))
                        reader = "llvm-readelf";
                else if (build_have("readelf"))
                        reader = "readelf";
                else
                {
                        build_remove_tree(work);
                        string_format(log,
                                      "%s floor: NOT RUN -- no ELF attribute reader\n",
                                      arch);
                        log_flush();
                        return 2;
                }
        }

        words[count++] = compiler;

        if (target)
                words[count++] = target;

        words[count++] = build_join("-march=", march, null);
        words[count++] = build_join("-mabi=", mabi, null);
        words[count++] = "-c";
        words[count++] = "-O2";
        words[count++] = "-DSTANDARD_NO_PLATFORM";
        words[count++] = "-ffreestanding";
        words[count++] = "-fno-builtin";
        words[count++] = "-fno-stack-protector";
        words[count++] = "-w";
        words[count++] = "-o";
        words[count++] = object;
        words[count++] = source;
        words[count] = null;

        if (build_run_words((string_address address_to)words))
        {
                build_remove_tree(work);
                string_format(log, "%s does not compile at the %s floor\n",
                              source, arch);
                log_flush();
                return 1;
        }

        count = 0;
        words[count++] = reader;
        words[count++] = "-A";
        words[count++] = object;
        words[count] = null;

        if (build_capture_words((string_address address_to)words, build_file_one,
                                BUILD_FILE_ROOM) < 0)
        {
                build_remove_tree(work);
                string_format(log, "the %s ELF attributes could not be read\n",
                              arch);
                log_flush();
                return 1;
        }

        build_remove_tree(work);

        //      GNU readelf calls this Tag_RISCV_arch and quotes the value;
        //      llvm-readelf prints a TagName line and then a Value. Both put
        //      the ISA string in a word of its own, so the word is what this
        //      looks for rather than either layout.
        {
                build_lines walk;
                string_address found[BUILD_ARGUMENT_ROOM];
                positive parts;
                positive wanted = string_length(prefix);

                build_lines_open(address_of walk, (string_address)build_file_one);

                while (!attributes &&
                       build_lines_words(address_of walk,
                                         (string_address address_to)found,
                                         address_of parts))
                {
                        for (positive at = 0; at < parts; at++)
                        {
                                string_address word = found[at];
                                positive length = string_length(word);

                                if (length > 1 && word[0] == '"')
                                {
                                        length -= 1 + (word[length - 1] == '"');
                                        word = build_text_keep(word + 1, length);
                                }

                                if (length > wanted &&
                                    !memory_compare(word, prefix, wanted))
                                {
                                        attributes = word;
                                        break;
                                }
                        }
                }
        }

        if (!attributes)
        {
                string_format(log, "%s ELF floor: missing attribute\n", arch);
                log_flush();
                return 1;
        }

        {
                string_address wanted[BUILD_ARGUMENT_ROOM];
                positive parts = build_split(require, (string_address address_to)wanted,
                                             BUILD_ARGUMENT_ROOM);

                for (positive at = 0; at < parts; at++)
                        if (!build_isa_holds(attributes, wanted[at]))
                        {
                                string_format(log, "%s ELF floor lacks %s: %s\n",
                                              arch, wanted[at], attributes);
                                log_flush();
                                answer = 1;
                        }
        }

        {
                string_address banned[BUILD_ARGUMENT_ROOM];
                positive parts = build_split(forbid, (string_address address_to)banned,
                                             BUILD_ARGUMENT_ROOM);

                for (positive at = 0; at < parts; at++)
                        if (build_isa_holds(attributes, banned[at]))
                        {
                                string_format(log,
                                              "%s ELF floor unexpectedly requires %s: %s\n",
                                              arch, banned[at], attributes);
                                log_flush();
                                answer = 1;
                        }
        }

        if (!answer)
        {
                string_format(log, "%s floor: %s\n", arch, attributes);
                log_flush();
        }

        return answer;
}

/*
        A key whose value is a command.

        pre and post carry shell source, not an argument vector -- a profile
        writes `#> post sh path/to/setup.sh` -- so this is the one
        place a shell is still the right thing to run. Nothing is passed to
        it but the text.
*/
static b32 build_shell_key(string_address name)
{
        string_address value = build_key(name);

        if (!value || !*value)
                return 0;

        return build_run("sh", "-c", value, null);
}

/*
        Installing a compiler that is not there.

        Checks which system this is and picks the command that installs
        things on it. Kept because the shell build had it and a first build on
        a fresh machine is where it earns its place.
*/
static bool build_install(string_address what)
{
        string_address words[BUILD_ARGUMENT_ROOM];
        string_address command = null;
        positive count = 0;
        bool privileged = true;
        b32 status;

        if (build_is_file("/etc/debian_version"))
                command = "apt-get install";
        else if (build_is_file("/etc/redhat-release"))
                command = "yum install";
        else if (build_is_file("/etc/arch-release"))
                command = "pacman -S";
        else if (build_is_file("/etc/alpine-release"))
                command = "apk add";
        else if (build_is_file("/etc/SuSE-release"))
                command = "zypper install";
        else if (build_is_file("/etc/gentoo-release"))
                command = "emerge";
        else if (build_have("brew"))
        {
                command = "brew install";
                privileged = false;
        }
        else
        {
                string_format(log,
                              "Unknown distribution, unable to set up build environment.\n");
                log_flush();
                exit(1);
        }

        count = build_add_split((string_address address_to)words, 0,
                                BUILD_ARGUMENT_ROOM, command);
        words[count++] = what;
        words[count] = null;

        build_command request = {
            .words = (string_address address_to)words,
            .privileged = privileged,
        };
        status = build_execute(address_of request);

        return status == 0;
}

//      environ with a few more entries on the end, for the two places the
//      shell wrote `env NAME=value command`.
static string_address address_to build_environment_with(string_address address_to extra,
                                                        positive count)
{
        positive have = pointer_vector_count(environ);
        string_address address_to answer;

        answer = (string_address address_to)build_text_take(
                (have + count + 1) * sizeof(string_address));

        memory_copy(answer, environ, have * sizeof(string_address));
        memory_copy(answer + have, extra, count * sizeof(string_address));
        answer[have + count] = null;

        return answer;
}

/*
        The tool registry as a file rather than as this program's own table.

        src/sh/tools.inc is the compiled dispatch registry and the installed
        surface both, including the category each name belongs to. This
        program has the table linked in, but filtered by its own build's
        component macros and without the categories, so the image's surface is
        read from the source of truth instead.
*/
typedef struct build_tool_entry
{
        string_address category;
        string_address name;
} build_tool_entry;

#define BUILD_TOOL_ROOM 512

static build_tool_entry build_tool_table[BUILD_TOOL_ROOM];
static positive build_tool_count;

static bool build_tools_read()
{
        build_lines walk;
        p8 address_to store;

        if (build_tool_count)
                return true;

        if (file_slurp(build_setting_get("tool_registry"), build_file_two,
                        BUILD_FILE_ROOM) < 0)
                return false;

        store = build_text_take(BUILD_WORD_ROOM);
        build_lines_open(address_of walk, (string_address)build_file_two);

        while (build_lines_next(address_of walk) &&
               build_tool_count < BUILD_TOOL_ROOM)
        {
                string_address words[BUILD_ARGUMENT_ROOM];
                positive parts;
                positive length = walk.length;
                p8 address_to flat = build_text_take(length + 1);

                //      The shell split on "[(),[:space:]]+", so the separators
                //      are turned into blanks and the ordinary word splitter
                //      does the rest.
                for (positive which = 0; which < length; which++)
                {
                        p8 byte = walk.line[which];

                        flat[which] = (byte == '(' || byte == ')' || byte == ',')
                                              ? ' '
                                              : byte;
                }

                flat[length] = end;
                parts = build_words_of((string_address)flat, length,
                                       (string_address address_to)words,
                                       BUILD_ARGUMENT_ROOM, store,
                                       BUILD_WORD_ROOM);

                if (parts < 3 || !word_is(words[0], "SHELL_TOOL"))
                        continue;

                build_tool_table[build_tool_count].category =
                        build_join(words[1], null);
                build_tool_table[build_tool_count].name = build_join(words[2], null);
                build_tool_count++;
        }

        return true;
}

/*
        The builtins as the shell's own table spells them: every
        SHELL_BUILTIN(KEY, "name", function) row of shell_commands[] in
        src/sh/builtin.c is a switch, and every SHELL_BUILTIN_CORE row is
        counted and has none.
*/
typedef struct build_builtin_entry
{
        string_address key;
        string_address name;
} build_builtin_entry;

#define BUILD_BUILTIN_ROOM 256

static build_builtin_entry build_builtin_table[BUILD_BUILTIN_ROOM];
static positive build_builtin_count;
static positive build_builtin_core;
static bool build_builtins_ready;

static bool build_builtins_read()
{
        build_lines walk;
        p8 address_to store;

        if (build_builtins_ready)
                return true;

        if (file_slurp(build_setting_get("builtin_registry"), build_file_two,
                        BUILD_FILE_ROOM) < 0)
                return false;

        store = build_text_take(BUILD_WORD_ROOM);
        build_lines_open(address_of walk, (string_address)build_file_two);

        while (build_lines_next(address_of walk) &&
               build_builtin_count < BUILD_BUILTIN_ROOM)
        {
                string_address words[BUILD_ARGUMENT_ROOM];
                positive parts;
                positive length = walk.length;
                p8 address_to flat = build_text_take(length + 1);

                //      Only rows as the table writes them, four spaces in;
                //      the macro definitions above it start at the margin.
                if (length < 17 || memory_compare(walk.line, "    SHELL_BUILTIN", 17))
                        continue;

                for (positive which = 0; which < length; which++)
                {
                        p8 byte = walk.line[which];

                        flat[which] = (byte == '(' || byte == ')' || byte == ',' ||
                                       byte == '"')
                                              ? ' '
                                              : byte;
                }

                flat[length] = end;
                parts = build_words_of((string_address)flat, length,
                                       (string_address address_to)words,
                                       BUILD_ARGUMENT_ROOM, store,
                                       BUILD_WORD_ROOM);

                if (parts == 3 && word_is(words[0], "SHELL_BUILTIN_CORE"))
                        build_builtin_core++;
                else if (parts == 4 && word_is(words[0], "SHELL_BUILTIN"))
                {
                        build_builtin_table[build_builtin_count].key =
                                build_join(words[1], null);
                        build_builtin_table[build_builtin_count].name =
                                build_join(words[2], null);
                        build_builtin_count++;
                }
        }

        build_builtins_ready = build_builtin_count > 0;

        return build_builtins_ready;
}

/*
        The build.

        Everything below is build.sh's local path, step for step and label for
        label. Where it ran a utility, this calls ours; where it ran the
        toolchain, make, tar or QEMU, this spawns those.
*/
static bool build_moon_core;
static bool build_moon_shell;
static bool build_moon_utilities;
static bool build_moon_util_linux;
static bool build_moon_shell_monitor;
//      Not a switch but a level, so it is read past the =y test the
//      switches share rather than through it.
static positive build_moon_strict;

/*
        The per-tool and per-builtin switches the .config named, as the part
        after CONFIG_MOONWATER_ -- TOOL_CAT, BUILTIN_ECHO -- and their value.
        A switch the file does not name takes MOONWATER_TOOLS_ALL or
        MOONWATER_BUILTINS_ALL, which is its Kconfig default: that is what
        lets a profile fragment on its own, read by config-header, mean
        what it means once composed.
*/
#define BUILD_SWITCH_ROOM 1024

static string_address build_switch_name[BUILD_SWITCH_ROOM];
static bool build_switch_value[BUILD_SWITCH_ROOM];
static positive build_switch_count;
static bool build_moon_tools_all;
static bool build_moon_builtins_all;

/*
        Floodlight's configuration, for the shell's copy of it: whether the
        register is built, its policy string as the .config spells it (a C
        string literal already), and the dangerous-flag switches, which are
        kept with the tool and builtin switches and default to y. The list
        is src/moonwater/Kconfig's "Floodlight: dangerous flags" menu, and
        the floodlight harness fails the build if the two differ.
*/
static bool build_moon_floodlight;
static string_address build_floodlight_policy;
static string_address const build_floodlight_switches[] = {
    "AWK_SPAWN", "SHELL_ESCAPES", "FIND_EXEC", "FIND_DELETE", "FIND_WRITE",
    "XARGS", "SPLIT_FILTER", "SORT_COMPRESS", "ENV_SPLIT", "LAUNCHERS",
    "NAMESPACES", "NETWORK_FETCH", null};

static string_address build_upper(string_address name)
{
        positive length = string_length(name);
        p8 address_to into = build_text_take(length + 1);

        memory_copy_end(into, name, length);
        memory_to_upper_ascii(into, length);

        return (string_address)into;
}

//      The value the .config gives a switch, the last line naming it, or
//      fallback when none does.
static bool build_switch_find(string_address want, bool fallback)
{
        for (positive at = build_switch_count; at > 0; at--)
                if (word_is(build_switch_name[at - 1], want))
                        return build_switch_value[at - 1];

        return fallback;
}

//      kind is "TOOL_" or "BUILTIN_", key the upper-case name.
static bool build_switch_on(string_address kind, string_address key)
{
        return build_switch_find(build_join(kind, key, null),
                                 word_is(kind, "TOOL_")
                                     ? build_moon_tools_all
                                     : build_moon_builtins_all);
}

/*
        The SYSTEM tools the kernel and init reach by absolute path: /init
        is what the kernel execs, /term is SPARK_TERMINAL_PROGRAM, and
        /moonwater is the settle program system.c runs at every boot. Each
        is a boot that stops if it is missing, so none has a switch.
*/
static bool build_tool_fixed(string_address name)
{
        return word_is(name, "init") || word_is(name, "term") ||
               word_is(name, "moonwater");
}

//      An existing build tree has no lines for newly added symbols until
//      olddefconfig next runs; their Kconfig defaults are y, while the core
//      must be explicitly built in for the initial filesystem to use it.
static fn build_components(string_address config)
{
        build_lines walk;

        build_moon_core = false;
        build_moon_shell = true;
        build_moon_utilities = true;
        build_moon_util_linux = true;
        build_moon_shell_monitor = true;
        build_moon_strict = STRICT_SAFE;
        build_moon_tools_all = true;
        build_moon_builtins_all = true;
        build_moon_floodlight = true;
        build_floodlight_policy = null;
        build_switch_count = 0;

        if (file_slurp(config, build_file_two, BUILD_FILE_ROOM) < 0)
                return;

        build_lines_open(address_of walk, (string_address)build_file_two);

        while (build_lines_next(address_of walk))
        {
                build_pair pair;
                string_address name;
                positive length;
                bool on;

                if (!build_config_name(walk.line, walk.length, address_of pair) ||
                    !string_has_prefix(pair.name, "CONFIG_MOONWATER_"))
                        continue;

                name = pair.name + 17;
                length = pair.name_length - 17;

                //      The strictness level is a number, so it is taken here
                //      rather than through the =y test below. A value that is
                //      not a level leaves the default standing, the way an
                //      unreadable switch does.
                if (memory_is_word(name, length, "STRICT"))
                {
                        positive used = 0;
                        positive level;

                        if (!pair.value)
                                continue;

                        level = string_digits_max(pair.value, pair.value_length,
                                                  address_of used);

                        if (used == pair.value_length && level <= STRICT_TIGHT)
                                build_moon_strict = level;

                        continue;
                }

                //      The policy string is a value, not a switch, and is kept
                //      as written: Kconfig quotes it the way C does.
                if (memory_is_word(name, length, "FLOODLIGHT_POLICY"))
                {
                        build_floodlight_policy =
                            pair.value && pair.value_length >= 2 &&
                                    pair.value[0] == '"'
                                ? build_text_keep(pair.value, pair.value_length)
                                : null;
                        continue;
                }

                //      =y is on, and "is not set" and =n are off: a composed
                //      .config says the first, a profile may say the second.
                //      Any other value, =m included, leaves the default
                //      alone.
                on = pair.value && memory_is_word(pair.value, pair.value_length, "y");

                if (pair.value && !on &&
                    !memory_is_word(pair.value, pair.value_length, "n"))
                        continue;

                if (memory_is_word(name, length, "CORE"))
                        build_moon_core = on;
                else if (memory_is_word(name, length, "SHELL"))
                        build_moon_shell = on;
                else if (memory_is_word(name, length, "UTILITIES"))
                        build_moon_utilities = on;
                else if (memory_is_word(name, length, "UTIL_LINUX"))
                        build_moon_util_linux = on;
                else if (memory_is_word(name, length, "SHELL_MONITOR"))
                        build_moon_shell_monitor = on;
                else if (memory_is_word(name, length, "TOOLS_ALL"))
                        build_moon_tools_all = on;
                else if (memory_is_word(name, length, "BUILTINS_ALL"))
                        build_moon_builtins_all = on;
                else if (memory_is_word(name, length, "FLOODLIGHT"))
                        build_moon_floodlight = on;
                else if ((string_has_prefix(name, "TOOL_") ||
                          string_has_prefix(name, "BUILTIN_") ||
                          string_has_prefix(name, "FLOODLIGHT_")) &&
                         build_switch_count < BUILD_SWITCH_ROOM)
                {
                        build_switch_name[build_switch_count] =
                                build_text_keep(name, length);
                        build_switch_value[build_switch_count++] = on;
                }
        }
}

/*
        What the programs are told about this configuration: one header,
        artifacts/moonwater_config.h, which src/lib.util.c includes when the
        compile names it through MOONWATER_CONFIG.

        The component switches used to travel as -D flags in a setting that
        build_spark never read -- the C port of the build read an
        environment variable instead -- so UTILITIES=n, UTIL_LINUX=n,
        SHELL_MONITOR=n and every STRICT level but the default removed
        symlinks and left the applets running from the shell. The header
        carries what the table has to hold, as counts the tool table is
        asserted against, and a record whose bytes are searched for in the
        built image, so a header that stops reaching the compiler stops the
        build instead of shipping the default.
*/
static bool build_utility_program;

static bool build_category_on(string_address category)
{
        bool utilities = build_moon_utilities;

        if (word_is(category, "SYSTEM"))
                return !build_utility_program;

        if (word_is(category, "GENERAL"))
                return utilities;

        if (word_is(category, "UTIL_BIN") || word_is(category, "UTIL_SBIN"))
                return utilities && build_moon_util_linux;

        if (word_is(category, "MONITOR"))
                return utilities && build_moon_shell_monitor &&
                       !build_utility_program;

        return false;
}

//      Whether a tools.inc row's own switch leaves it built.
static bool build_tool_switched(build_tool_entry address_to one)
{
        return build_tool_fixed(one->name) ||
               build_switch_on("TOOL_", build_upper(one->name));
}

//      Whether a tools.inc row is in the program being built.
static bool build_tool_on(build_tool_entry address_to one)
{
        return build_category_on(one->category) && build_tool_switched(one);
}

static string_address build_config_header(string_address from,
                                          string_address address_to record)
{
        string_address text = build_join("/* Generated by build from ", from,
                                         ". Do not edit. */\n", null);
        positive tools = 0;
        positive system = 0;

        if (!build_moon_utilities)
                text = build_join(text, "#define SHELL_NO_UTILITIES 1\n", null);
        if (!build_moon_utilities || !build_moon_util_linux)
                text = build_join(text, "#define SHELL_NO_UTIL_LINUX 1\n", null);
        if (!build_category_on("MONITOR"))
                text = build_join(text, "#define SHELL_NO_MONITOR 1\n", null);

        text = build_join(text, "#define MOONWATER_STRICT ",
                          build_number(build_moon_strict), "\n", null);

        //      SYSTEM rows are counted apart: the utility-only program
        //      drops them itself, so the one header serves either program.
        //      A tool switched off is named for the gate in builtin.c by its
        //      own spelling, which is the token tools.inc hands the macro.
        for (positive at = 0; at < build_tool_count; at++)
        {
                build_tool_entry address_to one = address_of build_tool_table[at];
                bool system_row = word_is(one->category, "SYSTEM");
                bool on;

                if (!system_row && !build_category_on(one->category))
                        continue;

                on = build_tool_switched(one);

                if (!on)
                        text = build_join(text, "#define MOONWATER_TOOL_OFF_",
                                          one->name, " 1\n", null);
                else if (system_row)
                        system++;
                else
                        tools++;
        }

        {
                positive builtins = build_builtin_core;

                for (positive at = 0; at < build_builtin_count; at++)
                {
                        build_builtin_entry address_to one =
                                address_of build_builtin_table[at];

                        if (build_switch_on("BUILTIN_", one->key))
                                builtins++;
                        else
                                text = build_join(text,
                                                  "#define MOONWATER_BUILTIN_OFF_",
                                                  one->key, " 1\n", null);
                }

                text = build_join(text, "#define MOONWATER_CONFIG_BUILTINS ",
                                  build_number(builtins), "\n", null);
        }

        //      Floodlight's rows for the shell's copy, spelled as autoconf.h
        //      spells them for floodlight.c: the string whenever the
        //      register is built, and every switch that is y. The shell reads
        //      the switches only when the string is defined and takes an
        //      absent one as off, so the two always travel together.
        if (build_moon_floodlight)
        {
                text = build_join(text, "#define CONFIG_MOONWATER_FLOODLIGHT_POLICY ",
                                  build_floodlight_policy ? build_floodlight_policy
                                                          : (string_address)"\"\"",
                                  "\n", null);
                for (positive at = 0; build_floodlight_switches[at]; at++)
                {
                        string_address want = build_join(
                            "FLOODLIGHT_", build_floodlight_switches[at], null);

                        if (build_switch_find(want, true))
                                text = build_join(text, "#define CONFIG_MOONWATER_",
                                                  want, " 1\n", null);
                }
        }

        text = build_join(text, "#define MOONWATER_CONFIG_TOOLS ",
                          build_number(tools), "\n",
                          "#define MOONWATER_CONFIG_SYSTEM_TOOLS ",
                          build_number(system), "\n", null);

        address_to record = build_join("moonwater-config ",
                                       build_hex(memory_hash_33(text,
                                                                string_length(text))),
                                       null);

        return build_join(text, "#define MOONWATER_CONFIG_RECORD \"",
                          address_to record, "\"\n", null);
}

/*
        The policy string, read here as the shell and floodlight.c will read
        it at boot. A string neither can read refuses every program, /init
        among them, so a typo in menuconfig made an image that could not
        start; it stops the build instead, naming the string. A flag row
        whose option does not begin with - can never match a word, so it is
        refused too: it reads as a restriction and restricts nothing.
*/
static b32 build_floodlight_policy_check()
{
        static floodlight_row rows[FLOODLIGHT_CONFIGURED];
        string_address quoted = build_floodlight_policy;
        positive count = 0;
        positive length;
        positive used = 0;
        bool whole = true;
        p8 address_to text;

        if (!build_moon_floodlight || !quoted)
                return 0;

        //      Kconfig writes the string as C does: quoted, and a backslash
        //      in front of each backslash and quote inside. The header
        //      carries it as written, so anything else -- a quote that ends
        //      it early -- would be C of its own in the shell's source.
        length = string_length(quoted);
        text = build_text_take(length);
        for (positive at = 1; at + 1 < length; at++)
        {
                if (quoted[at] == '\\')
                {
                        whole = whole && at + 2 < length;
                        at++;
                }
                else if (quoted[at] == '"')
                        whole = false;
                text[used++] = (p8)quoted[at];
        }
        text[used] = end;

        if (!whole || length < 2 || quoted[length - 1] != '"')
                return build_die(build_join(
                    "CONFIG_MOONWATER_FLOODLIGHT_POLICY is not one quoted "
                    "string: ", quoted, null));

        if (!floodlight_policy_take((string_address)text, rows,
                                    address_of count))
                return build_die(build_join(
                    "CONFIG_MOONWATER_FLOODLIGHT_POLICY does not read, and "
                    "would refuse every program at boot: ", quoted, null));

        for (positive at = 0; at < count; at++)
                if (rows[at].setting == FLOODLIGHT_FLAG &&
                    rows[at].detail[0] != '-')
                        return build_die(build_join(
                            "CONFIG_MOONWATER_FLOODLIGHT_POLICY: ",
                            (string_address)rows[at].subject, " flag ",
                            (string_address)rows[at].detail,
                            " names no option; an option begins with -",
                            null));

        return 0;
}

/*
        Write the header for the configuration now held and point the next
        spark build at it. The path is absolute because a quoted include is
        looked up beside the file that asks for it, not where the compiler
        runs.
*/
static b32 build_config_header_install(string_address from,
                                       string_address address_to record)
{
        string_address path = build_in("artifacts", "moonwater_config.h");
        string_address text;

        if (build_floodlight_policy_check())
                return 1;

        text = build_config_header(from, record);

        if (!build_write_file(path, text, string_length(text)))
                return build_die(build_join("cannot write ", path, null));

        if (path[0] != '/')
                path = build_join(build_working_directory(), "/", path, null);

        //      One word, not a list to split: the path may hold a blank.
        build_setting_set("spark_config",
                          build_join("-DMOONWATER_CONFIG=\"", path, "\"", null));

        return 0;
}

//      The built image must carry the record the header asked for, or the
//      header never reached the compile.
static b32 build_config_record_check(string_address image,
                                     string_address record)
{
        if (build_tool("grep", "-q", "-a", "-F", record, image, null))
                return build_die(build_join(image, " was not built from "
                                            "artifacts/moonwater_config.h "
                                            "(no '", record, "' in it)", null));

        return 0;
}

//      build config-header <config> <header> [utility]: the header an
//      image build would write for that configuration, for a lane to build
//      a program against without a kernel tree. Any symbol the file does
//      not name keeps its Kconfig default, so a profile fragment on its
//      own reads as that profile over the defaults.
static b32 build_config_header_write(string_address config, string_address output,
                                     string_address kind)
{
        string_address record;
        string_address text;

        if (!build_is_file(config))
                return string_report(log_error, 1, "config-header: no such file: %s\n",
                                     config);

        if (kind && !word_is(kind, "utility"))
                return string_report(log_error, 1,
                                     "config-header: '%s' is not utility\n", kind);

        build_components(config);

        if (!build_tools_read() || !build_builtins_read())
                return build_die("cannot read the tool or builtin registry");

        build_utility_program = kind != null;
        if (build_floodlight_policy_check())
                return 1;
        text = build_config_header(config, address_of record);

        if (!build_write_file(output, text, string_length(text)))
                return build_die(build_join("cannot write ", output, null));

        return 0;
}

static b32 build_link(string_address target, string_address path)
{
        return build_tool("ln", "-sf", target, path, null);
}

/*
        Every name a tool is installed under, linked to applet, or with dry
        printed one path to a line under the image root instead: that is
        build surface, which is how a lane sees what an image would link
        without building one. The SYSTEM names are the shell's, at the top
        level; every other enabled tool is linked at the top level and under
        the /bin or /sbin its category conventionally lives in, for absolute
        commands and /usr/bin/env shebangs. A tool whose switch is off is
        linked nowhere.
*/
static b32 build_tool_link(string_address image, string_address target,
                           string_address path, bool dry)
{
        if (dry)
        {
                string_format(log, "%s\n", path);
                return 0;
        }

        if (build_link(target, build_join(image, "/", path, null)))
                return build_die(build_join("linking /", path, null));

        return 0;
}

static b32 build_tool_links(string_address image, string_address applet, bool dry)
{
        for (positive at = 0; at < build_tool_count; at++)
        {
                build_tool_entry address_to one = address_of build_tool_table[at];

                if (word_is(one->category, "SYSTEM") && build_tool_on(one) &&
                    build_tool_link(image, applet, one->name, dry))
                        return 1;
        }

        if (!build_moon_core || !build_moon_utilities)
                return 0;

        for (positive at = 0; at < build_tool_count; at++)
        {
                build_tool_entry address_to one = address_of build_tool_table[at];

                if (!word_is(one->category, "SYSTEM") && build_tool_on(one) &&
                    build_tool_link(image, applet, one->name, dry))
                        return 1;
        }

        for (positive at = 0; at < build_tool_count; at++)
        {
                build_tool_entry address_to one = address_of build_tool_table[at];
                string_address directory = null;

                if (!build_tool_on(one))
                        continue;

                if (word_is(one->category, "GENERAL") ||
                    word_is(one->category, "UTIL_BIN"))
                        directory = "bin";
                else if (word_is(one->category, "UTIL_SBIN"))
                        directory = "sbin";

                if (directory &&
                    build_tool_link(image, build_join("../", applet, null),
                                    build_join(directory, "/", one->name, null),
                                    dry))
                        return 1;
        }

        return 0;
}

/*
        The Kconfig switches, written from the sources they switch.

        One bool per tools.inc row, CONFIG_MOONWATER_TOOL_<NAME>, in a menu
        per category that depends on the category's own switch, each
        defaulting to MOONWATER_TOOLS_ALL: a profile that wants an allow
        list turns that off and names what it keeps. The file is checked in
        so menuconfig and a profile author can read it; `build switches`
        rewrites it and `build switches check` is the kit lane's gate that
        it still says what the sources say.
*/
static string_address build_switch_menu(string_address title,
                                        string_address depends,
                                        string_address address_to names,
                                        positive count, string_address kind,
                                        string_address all)
{
        string_address text = build_join("\nmenu \"", title, "\"\n",
                                         "    depends on ", depends, "\n", null);

        //      By name, which is how a reader looks for one.
        for (positive at = 1; at < count; at++)
                for (positive back = at; back > 0 &&
                     string_compare(names[back - 1], names[back]) > 0; back--)
                {
                        string_address held = names[back];

                        names[back] = names[back - 1];
                        names[back - 1] = held;
                }

        for (positive at = 0; at < count; at++)
                text = build_join(text, "\nconfig MOONWATER_", kind,
                                  build_upper(names[at]), "\n",
                                  "    bool \"", names[at], "\"\n",
                                  "    default ", all, "\n", null);

        return build_join(text, "\nendmenu\n", null);
}

static string_address build_switches_text()
{
        static const struct
        {
                string_address title;
                string_address depends;
                string_address first;
                string_address second;
        } menus[] = {
            {"Tools: general utilities", "MOONWATER_UTILITIES", "GENERAL", null},
            {"Tools: util-linux utilities", "MOONWATER_UTIL_LINUX", "UTIL_BIN",
             "UTIL_SBIN"},
            {"Tools: the resource monitor", "MOONWATER_SHELL_MONITOR", "MONITOR",
             null},
            {"Tools: the shell's own programs", "MOONWATER_SHELL", "SYSTEM", null},
        };
        string_address text =
            "# Generated by `build switches` from src/sh/tools.inc and the\n"
            "# shell_commands[] table in src/sh/builtin.c. Do not edit: the kit\n"
            "# lane fails when this file and its sources disagree.\n"
            "\n"
            "config MOONWATER_TOOLS_ALL\n"
            "    bool \"Build every tool unless it is switched off below\"\n"
            "    depends on MOONWATER_CORE=y\n"
            "    default y\n"
            "    help\n"
            "      The default of every MOONWATER_TOOL_ switch. Leave it on and\n"
            "      switch single tools off; turn it off and switch on the ones\n"
            "      to keep, which is how kernel/profile/sec_locked is an allow\n"
            "      list. A tool switched off is compiled out of the shell's\n"
            "      table and linked nowhere, so its name is not found.\n"
            "\n"
            "      /init, /term and /moonwater have no switch: the kernel and\n"
            "      init run them by path, and an image without one does not\n"
            "      boot.\n";

        for (positive menu = 0; menu < array_count(menus); menu++)
        {
                string_address names[BUILD_TOOL_ROOM];
                positive count = 0;

                for (positive at = 0; at < build_tool_count; at++)
                {
                        build_tool_entry address_to one = address_of build_tool_table[at];

                        if (build_tool_fixed(one->name))
                                continue;

                        if (word_is(one->category, menus[menu].first) ||
                            (menus[menu].second &&
                             word_is(one->category, menus[menu].second)))
                                names[count++] = one->name;
                }

                text = build_join(text,
                                  build_switch_menu(menus[menu].title,
                                                    menus[menu].depends,
                                                    (string_address address_to)names,
                                                    count, "TOOL_",
                                                    "MOONWATER_TOOLS_ALL"),
                                  null);
        }

        text = build_join(text,
            "\nconfig MOONWATER_BUILTINS_ALL\n"
            "    bool \"Build every builtin unless it is switched off below\"\n"
            "    depends on MOONWATER_SHELL\n"
            "    default y\n"
            "    help\n"
            "      The default of every MOONWATER_BUILTIN_ switch, as\n"
            "      MOONWATER_TOOLS_ALL is for the tools. A builtin switched off\n"
            "      is compiled out of the shell's table and its name is not\n"
            "      found, unless a tool of that name is still built: blkid,\n"
            "      findfs, findmnt, kill, mount, mountpoint and umount are\n"
            "      in both tables, and each has its own switch.\n"
            "\n"
            "      The core has no switch: POSIX's special builtins (. : break\n"
            "      continue eval exec exit export readonly return set shift\n"
            "      times trap unset), which are the language rather than\n"
            "      commands it runs, with cd, true and false. The comment over\n"
            "      shell_commands[] in src/sh/builtin.c says why.\n"
            "\nmenu \"Shell builtins\"\n"
            "    depends on MOONWATER_SHELL\n", null);

        for (positive at = 0; at < build_builtin_count; at++)
                text = build_join(text, "\nconfig MOONWATER_BUILTIN_",
                                  build_builtin_table[at].key, "\n",
                                  "    bool \"", build_builtin_table[at].name, "\"\n",
                                  "    default MOONWATER_BUILTINS_ALL\n", null);

        return build_join(text, "\nendmenu\n", null);
}

//      build switches [check]
static b32 build_switches(string_address mode)
{
        string_address path = build_setting_get("switches_kconfig");
        string_address text;
        bipolar got;

        if (mode && !word_is(mode, "check"))
                return string_report(log_error, 1, "switches: '%s' is not check\n",
                                     mode);

        if (!build_tools_read() || !build_builtins_read())
                return build_die("cannot read the tool or builtin registry");

        text = build_switches_text();

        if (!mode)
        {
                if (!build_write_file(path, text, string_length(text)))
                        return build_die(build_join("cannot write ", path, null));

                return 0;
        }

        got = file_slurp(path, build_file_one, BUILD_FILE_ROOM);

        if (got < 0 || (positive)got != string_length(text) ||
            memory_compare(build_file_one, text, (positive)got))
                return string_report(log_error, 1,
                                     "switches: %s does not match its sources; "
                                     "run ./build switches\n", path);

        return 0;
}

//      build surface <config> [utility]: the names an image built from that
//      configuration links, one path a line, relative to the image root.
static b32 build_surface(string_address config, string_address kind)
{
        if (!build_is_file(config))
                return string_report(log_error, 1, "surface: no such file: %s\n",
                                     config);

        if (kind && !word_is(kind, "utility"))
                return string_report(log_error, 1,
                                     "surface: '%s' is not utility\n", kind);

        build_components(config);

        if (!build_tools_read())
                return build_die("cannot read the tool registry");

        //      What build_userspace would build: the utility-only program
        //      when asked, or when the shell is off.
        build_utility_program = kind != null || !build_moon_shell;

        if (!build_moon_core || (build_utility_program && !build_moon_utilities))
                return 0;

        if (build_tool_links(".", "shell", true))
                return 1;

        log_flush();
        return 0;
}

static b32 build_kernel_source()
{
        string_address artifacts = build_setting_get("artifacts");
        string_address tree = build_setting_get("kernel_tree");
        string_address version = build_setting_get("kernel_version");
        string_address series;
        string_address archive;
        string_address tarball;
        string_address download;
        string_address required = build_setting_get("required");
        string_address names[BUILD_ARGUMENT_ROOM];
        positive count = build_split(required, (string_address address_to)names,
                                     BUILD_ARGUMENT_ROOM);
        bool present;

        //      Derived rather than written out, so moving to another release
        //      means editing the version and the signature and nothing else.
        //      kernel.org lays every series out under vMAJOR.x.
        series = build_join("v",
                            build_text_keep(version,
                                            (positive)(string_first_of_or_end(version, '.') -
                                                       version)),
                            ".x", null);

        tarball = build_join(artifacts, "/linux-", version, ".tar", null);
        archive = build_join(tarball, ".xz", null);
        download = build_join(build_setting_get("kernel_mirror"), "/", series,
                              "/linux-", version, ".tar.xz", null);

        //      Checked here rather than after the early exit below, which is
        //      where it used to sit -- so it never ran on any build after the
        //      first.
        for (positive at = 0; at < count; at++)
                if (!build_have(names[at]))
                {
                        string_format(log_error,
                                      "%s is required to build the kernel. Please install it and try again.\n",
                                      names[at]);
                        exit(1);
                }

        //      A Makefile is the marker that the tree is really there. Testing
        //      only for the directory treated an empty or half extracted tree
        //      as a finished extraction.
        present = build_is_file(build_join(tree, "/Makefile", null));

        if (present)
        {
                string_format(log, "%s Kernel already extracted... %s\n",
                              BUILD_BOLD, BUILD_RESET);
                log_flush();
        }

        build_tool("mkdir", "-p", artifacts, null);
        build_tool("mkdir", "-p", tree, null);

        if (present)
                return 0;

        if (!build_is_file(archive))
        {
                //      curl, and not our own fetch: fetch speaks HTTP without
                //      TLS and does not follow redirects, and this URL is
                //      https and redirects. The signature below is what makes
                //      the download trustworthy either way, but a fetch that
                //      cannot reach the mirror at all is not a substitute.
                if (build_run("curl", "-fL", download, "-o", archive, null))
                {
                        string_format(log_error, "ERROR: failed to download %s\n",
                                      download);
                        build_tool("rm", "-f", archive, null);
                        exit(1);
                }
        }

        string_format(log, "%s Checking kernel signature %s\n", BUILD_BOLD,
                      BUILD_RESET);
        log_flush();

        /*
                Every step below gates the next one. None of these exit
                statuses were checked before, so a failed download, a failed
                key fetch or a failed signature verification all still ended in
                a compiled kernel.
        */
        {
                string_address keys = build_setting_get("kernel_keys");
                string_address words[BUILD_ARGUMENT_ROOM] = {"gpg", "--locate-keys"};
                positive at = build_add_split((string_address address_to)words, 2,
                                              BUILD_ARGUMENT_ROOM, keys);

                words[at] = null;

                if (build_run_words((string_address address_to)words))
                {
                        string_format(log_error,
                                      BUILD_RED
                                      "ERROR: could not fetch the kernel signing keys (%s).\n",
                                      keys);
                        string_format(log_error,
                                      "Refusing to build an unverified kernel." BUILD_RESET "\n");
                        exit(1);
                }
        }

        if (build_run("unxz", "-k", archive, null))
        {
                string_format(log_error,
                              BUILD_RED "ERROR: could not decompress %s" BUILD_RESET "\n",
                              archive);
                exit(1);
        }

        {
                string_address signature = build_setting_get("kernel_signature");

                build_write_file(build_join(tarball, ".sign", null), signature,
                                 string_length(signature));
        }

        if (build_run("gpg", "--verify", build_join(tarball, ".sign", null),
                      tarball, null))
        {
                string_format(log_error,
                              BUILD_RED "ERROR: SIGNATURE VERIFICATION FAILED for %s\n",
                              tarball);
                string_format(log_error,
                              "The archive does not match the signature pinned in this tool.\n");
                string_format(log_error,
                              "Refusing to extract or build it. Delete %s and retry."
                              BUILD_RESET "\n", archive);
                build_tool("rm", "-f", tarball, null);
                exit(1);
        }

        if (build_run("tar", "-xf", tarball, "--strip-components=1", "-C", tree,
                      null))
        {
                string_format(log_error,
                              BUILD_RED "ERROR: could not extract %s" BUILD_RESET "\n",
                              tarball);
                build_tool("rm", "-f", tarball, null);
                exit(1);
        }

        build_tool("rm", tarball, null);
        string_format(log, "%s Kernel extracted to %s %s\n", BUILD_BOLD, tree,
                      BUILD_RESET);
        log_flush();

        return 0;
}

#define COVERAGE_DUMP                                                      \
        "#!/bin/sh\n"                                                        \
        "echo begin | base64 > /dev/ttyS1\n"                                 \
        "head -c 16777216 /coverage.map | gzip -c | base64 > /dev/ttyS1\n"   \
        "echo ok | base64 > /dev/ttyS1\n"

static b32 build_userspace()
{
        string_address image = build_setting_get("image_root");
        string_address applet = null;
        string_address shell_mode = null;
        string_address config = build_join(build_setting_get("kernel_tree"),
                                           "/.config", null);
        string_address record = null;

        /*
                What was here last time, gone.

                Nothing ever removed a build product from the image tree, so
                anything that stopped being built stayed in it forever: an 857
                kilobyte binary from a fortnight ago was still shipping, along
                with every program that had since become a name for the shell.
                Only the top level and only files and links -- the directories
                below hold the device nodes and are made once.
        */
        {
                string_address where[4];

                where[0] = image;
                where[1] = build_join(image, "/bin", null);
                where[2] = build_join(image, "/sbin", null);
                where[3] = build_join(image, "/usr", null);

                for (positive at = 0; at < 4; at++)
                        if (build_tool("find", where[at], "-maxdepth", "1", "(",
                                       "-type", "f", "-o", "-type", "l", ")",
                                       "-delete", null))
                                return build_die(build_join("clearing ",
                                                            where[at], null));

                /*
                        /bowls is a runtime mount, not a place to ship
                        archives. A leftover bootstrap tarball here is packed
                        into the initramfs.
                */
                if (build_tool("find",
                               build_join(image, BOWL_ROOT_DIRECTORY, null),
                               "-maxdepth", "1", "(", "-type", "f", "-o",
                               "-type", "l", ")", "-delete", null))
                        return build_die("clearing leftover bowl files");

                if (build_tool("rm", "-f",
                               build_join(image,
                                          "/root/archlinux-bootstrap-x86_64.tar.zst",
                                          null),
                               null))
                        return build_die("clearing leftover root archive");
        }

        /*
                The firmware the profiles name, under /lib/firmware, and
                nothing a previous build left there. Every build, since a
                build without a firmware line must still clear the last one's.
        */
        if (build_run("sh", build_setting_get("firmware_script"), null))
                return build_die("firmware");

        build_components(config);

        if (!build_tools_read() || !build_builtins_read())
                return build_die("cannot read the tool or builtin registry");

        //      kernel/profile/coverage: /shell built to record which of its
        //      blocks the guest reached, for `sh test/run coverage`. Any
        //      other value is refused rather than read as off.
        {
                string_address asked = build_key_one("coverage", null);

                if (word_is(asked, "on"))
                        shell_mode = "coverage";
                else if (*asked && !word_is(asked, "off"))
                        return build_die(build_join("coverage: '", asked,
                                                    "' is neither on nor off", null));
        }

        if (build_moon_core && build_moon_shell)
        {
                //      Every program in the default image is spark, including
                //      the one the kernel execs as /init, so no ELF is loaded
                //      on its boot path.
                build_utility_program = false;

                if (build_config_header_install(config, address_of record))
                        return 1;

                if (build_spark(build_setting_get("shell_source"),
                                build_join(image, "/shell", null), shell_mode))
                        return build_die("building the shell");

                if (build_config_record_check(build_join(image, "/shell", null),
                                              record))
                        return 1;

                applet = "shell";

                //      What a coverage lane without a serial shell types
                //      before it stops the machine: the record, gzipped and in
                //      base64, out of the second UART between two short
                //      streams, begin and ok, so what the firmware wrote there
                //      first is left out and a record cut short is known to
                //      be. test/run's coverage_dump is the same three lines.
                if (shell_mode &&
                    (!build_write_file(build_join(image, "/coverage-dump", null),
                                      COVERAGE_DUMP, sizeof(COVERAGE_DUMP) - 1) ||
                     build_tool("chmod", "0755",
                                build_join(image, "/coverage-dump", null), null)))
                        return build_die("installing /coverage-dump");

                //      Scripts need a real interpreter path: /shell is the
                //      image's binary, but a #!/bin/sh shebang is resolved by
                //      the kernel before the shell gets any say.
                {
                        string_address names[3];

                        names[0] = "sh";
                        names[1] = "dash";
                        names[2] = "bash";

                        for (positive at = 0; at < 3; at++)
                                if (build_link("../shell",
                                               build_join(image, "/bin/", names[at],
                                                          null)))
                                        return build_die(build_join("linking /bin/",
                                                                    names[at], null));
                }

                if (build_link("../bin", build_join(image, "/usr/bin", null)))
                        return build_die("linking /usr/bin");

                if (build_link("../sbin", build_join(image, "/usr/sbin", null)))
                        return build_die("linking /usr/sbin");


                if (build_moon_shell_monitor && build_moon_utilities)
                {
                        string_address monitor = build_setting_get("monitor_source");

                        if (build_tool("cp", monitor,
                                       build_join(image, "/monitor.sh", null),
                                       null))
                                return build_die("installing /monitor.sh");

                        if (build_tool("chmod", "0755",
                                       build_join(image, "/monitor.sh", null),
                                       null))
                                return build_die("making /monitor.sh executable");

                        if (build_link("monitor.sh",
                                       build_join(image, "/mointor.sh", null)) ||
                            build_link("../monitor.sh",
                                       build_join(image, "/bin/monitor.sh", null)) ||
                            build_link("../monitor.sh",
                                       build_join(image, "/bin/mointor.sh", null)))
                                return build_die("linking the monitor");
                }
        }
        else if (build_moon_core && build_moon_utilities)
        {
                build_utility_program = true;

                if (build_config_header_install(config, address_of record))
                        return 1;

                if (build_spark(build_setting_get("utilities_source"),
                                build_join(image, "/shell", null), shell_mode))
                        return build_die("building the utilities");

                if (build_config_record_check(build_join(image, "/shell", null),
                                              record))
                        return 1;

                //      The kernel's SPAWN_TOOL ABI accelerates through this
                //      fixed path. This binary has no shell fallback.
                applet = "shell";

                if (build_link("../bin", build_join(image, "/usr/bin", null)))
                        return build_die("linking /usr/bin");

                if (build_link("../sbin", build_join(image, "/usr/sbin", null)))
                        return build_die("linking /usr/sbin");
        }

        /*
                Utilities are one multicall Spark program under other names.

                With the shell present they share its binary. A utility-only
                image has the same dispatch table but no shell fallback.
        */
        if (applet && build_tool_links(image, applet, false))
                return 1;

        return 0;
}

static b32 build_local(string_address address_to profiles, positive count,
                       string_address arch_profile)
{
        string_address artifacts = build_setting_get("artifacts");
        string_address image = build_setting_get("image_root");
        string_address tree = build_setting_get("kernel_tree");
        string_address output = build_setting_get("output");
        string_address make_flags;
        string_address compiler;
        string_address kernel_image;
        string_address kernel_export;
        string_address chosen[BUILD_ARGUMENT_ROOM];
        positive chosen_count = 0;

        //      Only this path needs it. Booting an image, writing a stick and
        //      driving a build on another machine all run as you.
        if (!build_root())
        {
                build_label("", BUILD_YELLOW " WARNING !!!");
                string_format(log_error,
                              "Building here wants root: sudo sh build.sh\n");
                string_format(log, "\n");
                log_flush();
        }

        {
                file_machine machine;
                if (!file_machine_read(address_of machine))
                {
                        log_error("uname: cannot read system name\n", 0);
                        machine.system[0] = end;
                }
                string_address system = machine.system;

                if (!word_is(system, "Linux"))
                        return build_die(build_join(
                                "building a kernel wants a Linux toolchain and a case\n"
                                "sensitive filesystem, and this is ", system,
                                ". Name a machine that has them with\n"
                                "--host, or set MOONWATER_BUILD_HOST.", null));
        }

        build_label("", "REPOSITORY SETUP");
        string_format(log, "Building %s\n", build_setting_get("full_name"));
        log_flush();

        if (build_tool("mkdir", "-p", artifacts, image, output, null))
                return build_die("repository setup");

        build_label("", "DISTRO INFO");

        {
                string_address text = build_join(
                        "CONFIG_LOCALVERSION=\"", build_setting_get("full_name"),
                        "\"\nCONFIG_DEFAULT_HOSTNAME=\"",
                        build_setting_get("name"), "-box\"\n", null);

                build_write_file(build_join(artifacts, "/info", null), text,
                                 string_length(text));
        }

        /*
                The device nodes the image boots with. mknod fails when the
                node already exists, which made every rebuild after the first
                noisy and, under set -e, fatal -- so each is created only when
                missing.

                tmp, etc and root because everything expects them to be there:
                a redirection into /tmp is the first thing anybody tries. The
                empty runtime directories are mount targets for Bowl's fast
                merged view; /bowls is where distribution roots live.
        */
        {
                string_address directories = build_setting_get("image_directories");
                string_address names[BUILD_ARGUMENT_ROOM];
                positive many = build_split(directories,
                                            (string_address address_to)names,
                                            BUILD_ARGUMENT_ROOM);
                string_address words[BUILD_ARGUMENT_ROOM];
                positive at = 0;

                words[at++] = "mkdir";
                words[at++] = "-p";

                for (positive which = 0; which < many; which++)
                        words[at++] = build_join(image, "/", names[which], null);

                words[at] = null;

                if (build_tool_words((string_address address_to)words))
                        return build_die("filesystem setup");
        }

        {
                string_address nodes = build_setting_get("image_nodes");
                string_address names[BUILD_ARGUMENT_ROOM];
                positive many = build_split(nodes, (string_address address_to)names,
                                            BUILD_ARGUMENT_ROOM);

                //      path, type, major, minor: a short last entry would
                //      hand mknod a pointer the splitter never wrote.
                if (many % 4)
                        return build_die("image_nodes wants path type major minor per node");

                for (positive at = 0; at + 3 < many; at += 4)
                {
                        string_address path = build_join(image, "/", names[at],
                                                         null);

                        if (build_is_file(path) || build_is_directory(path) ||
                            access(path, 0) >= 0)
                                continue;

                        if (build_tool("mknod", path, names[at + 1], names[at + 2],
                                       names[at + 3], null))
                                return build_die(build_join("making ", path, null));
                }
        }

        build_label("", "KERNEL SOURCE");

        if (build_kernel_source())
                return 1;

        build_label("", "KERNEL CONFIGURATION");

        {
                chosen_count = build_add_split((string_address address_to)chosen,
                                               chosen_count, BUILD_ARGUMENT_ROOM,
                                               build_setting_get("profiles_always"));

                //      Null when an arch/ profile is on the line, which is
                //      then the whole answer.
                if (arch_profile)
                        chosen[chosen_count++] = arch_profile;

                if (!count)
                {
                        /*
                                serial is last on purpose, and it is not
                                optional. The boot lane of test/run drives the
                                image over a serial line and reads its answers
                                back, so a default image without the 8250 has
                                no way to be tested at all: the kernel comes
                                up, runs /init, and says nothing. Last, because
                                it also asks for the loglevel and the
                                timestamps that make the transcript readable,
                                and debug_none quietens both.
                        */
                        chosen_count = build_add_split(
                                (string_address address_to)chosen, chosen_count,
                                BUILD_ARGUMENT_ROOM,
                                build_setting_get("profiles_default"));
                }
                else
                        for (positive at = 0; at < count; at++)
                                chosen[chosen_count++] = profiles[at];

                chosen[chosen_count] = null;
        }

        /*
                Always compose the selected profiles. Reusing the last
                configuration made a plain default build inherit whichever
                special profile had run before it -- notably leaving Canvas
                disabled after a server build even though a bare build promises
                the defaults. Composition preserves incremental builds when the
                result is unchanged, so deterministic selection costs no
                rebuild by itself.
        */
        if (build_config((string_address address_to)chosen, chosen_count))
                return build_die("configuration");

        make_flags = build_key("make_flags");

        build_label("", "BUILD ENVIRONMENT CHECK");
        compiler = build_key("compiler");

        if (!build_have(compiler))
        {
                build_label("", BUILD_YELLOW " WARNING !!!");
                string_format(log, "%s not found. Attempting to install it.\n\n",
                              compiler);
                log_flush();

                if (!build_install(compiler))
                        string_format(log,
                                      "ERROR: Unable to install %s. Please install it manually.\n",
                                      compiler);
        }
        else
        {
                string_format(log, "Using compiler: %s%s\n", BUILD_BOLD, compiler);
                log_flush();
        }

        build_label("", "KERNEL CONFIG");

        /*
                Every edit made to Linux's own source lives in the patch
                script, because everything that touches the kernel belongs
                under kernel/ and nothing else does. What was once here was a
                hundred and eighty lines of claims, displacements and grafts,
                which is the answer to "what did you change about the kernel"
                and was findable only by reading a build script.
        */
        if (build_run("sh", build_setting_get("patch_script"), null))
                return build_die("patching the kernel source");

        if (build_is_newer(build_join(artifacts, "/.config", null),
                           build_join(tree, "/.config", null)))
        {
                string_address extra[BUILD_ARGUMENT_ROOM];
                positive many = build_split(make_flags,
                                            (string_address address_to)extra,
                                            BUILD_ARGUMENT_ROOM);
                string_address words[BUILD_ARGUMENT_ROOM];
                build_command what = {.directory = tree, .quiet = true, .privileged = true};
                positive at;

                at = 0;
                words[at++] = "make";
                words[at++] = "allnoconfig";

                for (positive which = 0; which < many; which++)
                        words[at++] = extra[which];

                words[at] = null;
                what.words = (string_address address_to)words;

                if (build_execute(address_of what))
                        return build_die("kernel configuration");

                /*
                        Quiet: merge_config compares the combined fragment
                        against an allnoconfig baseline and calls most of what
                        the profiles ask for a "redefinition" -- 125 lines
                        meaning nothing. The composition step reports the
                        disagreements that matter, between profiles.

                        make_flags go in the environment, not on the command
                        line: merge_config.sh reads trailing arguments as
                        fragment paths, so "ARCH=arm64" became a file that did
                        not exist and stopped every cross build.
                */
                if (!build_config_fragment(build_join(artifacts, "/.config", null),
                                           build_join(artifacts, "/merge.config", null)))
                        return build_die("writing artifacts/merge.config");

                at = 0;
                words[at++] = "sh";
                words[at++] = "scripts/kconfig/merge_config.sh";
                words[at++] = "-m";
                words[at++] = ".config";
                words[at++] = build_join("../", artifacts, "/merge.config", null);
                words[at] = null;

                what.words = (string_address address_to)words;
                what.privileged = false;
                what.environment = build_environment_with(
                        (string_address address_to)extra, many);

                if (build_execute(address_of what))
                        return build_die("kernel configuration");

                at = 0;
                words[at++] = "make";
                words[at++] = "olddefconfig";

                for (positive which = 0; which < many; which++)
                        words[at++] = extra[which];

                words[at] = null;
                what.words = (string_address address_to)words;
                what.privileged = true;
                what.environment = null;

                if (build_execute(address_of what))
                        return build_die("kernel configuration");
        }
        else
        {
                string_format(log, "No changes\n");
                log_flush();
        }

        build_label("", "CONFIGURATION CHECK");

        /*
                merge_config and olddefconfig drop unmet options without a
                word, so anything a profile asked for and did not get is
                reported here rather than discovered later as hardware that
                does not work.  Verify the same composed profile list that
                produced this .config; passing an empty list made the normal
                build silently report "All 0 requested options took effect."
        */
        build_verify_config(build_join(tree, "/.config", null),
                            (string_address address_to)chosen, chosen_count);

        build_label("", "ASSEMBLY");

        //      Where a profile asked for a .asm from src/ to stand in for a
        //      file the kernel already builds. The .asm files that belong to
        //      the module rather than to the kernel need nothing here -- the
        //      module's Makefile builds those as part of it.
        {
                string_address words[3];
                build_command what = {.privileged = true};

                words[0] = "sh";
                words[1] = build_setting_get("replace_script");
                words[2] = null;
                what.words = (string_address address_to)words;

                if (build_execute(address_of what))
                        return build_die("assembly");
        }

        build_label("", "PRE BUILD");
        if (build_shell_key("pre"))
                return build_die("pre-build hook");

        build_label("", "USER SPACE BUILD");

        if (build_userspace())
                return 1;

        build_label("", "KERNEL BUILD");

        make_flags = build_key("make_flags");

        //      The kernel used to be built with whatever its own Makefile
        //      chose, because nothing reached the C compiler. KCFLAGS is that
        //      gap closed.
        {
                string_address cores;
                string_address kernel_cflags = build_key("kernel_cflags");
                string_address words[BUILD_ARGUMENT_ROOM];
                build_command what = {.directory = tree};
                positive at = 0;
                bool good = true;

                cores = build_number(nproc_count(false, 0));
                kernel_image = build_key_one("kernel_image", address_of good);
                kernel_export = build_key_one("kernel_export", address_of good);

                if (!good || !*kernel_image || !*kernel_export)
                        return build_die(
                                "kernel_image / kernel_export not set in the configuration");

                words[at++] = "make";
                words[at++] = build_join("-j", cores, null);
                at = build_add_split((string_address address_to)words, at,
                                     BUILD_ARGUMENT_ROOM, make_flags);

                words[at++] = build_join("KCFLAGS=", kernel_cflags, null);
                words[at] = null;
                what.words = (string_address address_to)words;

                /*
                        make's exit status was discarded once, so a failed
                        build fell through to the copy below and shipped
                        whatever image was left over from the run before.

                        KCPPFLAGS, KAFLAGS, LDFLAGS and RUSTFLAGS were passed
                        here too, from keys no profile has ever set -- four
                        empty variables handed to make on every build.
                */
                if (build_execute(address_of what))
                        return build_die("kernel build");
        }

        if (!build_is_file(kernel_image))
                return build_die(build_join("expected image '", kernel_image,
                                            "' was not produced", null));

        build_tool("mkdir", "-p", build_directory_of(kernel_export), null);

        {
                string_address words[4];
                build_command what = {.privileged = true};

                words[0] = "cp";
                words[1] = kernel_image;
                words[2] = kernel_export;
                words[3] = null;
                what.words = (string_address address_to)words;

                //      Our cp, unless we are not root, in which case the copy
                //      into a root-owned directory needs sudo and sudo needs a
                //      program to run.
                if (build_root())
                {
                        if (build_tool("cp", kernel_image, kernel_export, null))
                                return build_die("exporting the kernel image");
                }
                else if (build_execute(address_of what))
                        return build_die("exporting the kernel image");
        }

        build_label("", "POST BUILD");
        if (build_shell_key("post"))
                return build_die("post-build hook");
        string_format(log, "%sDone Building Kernel%s\n", BUILD_BOLD, BUILD_GREEN);
        log_flush();
        build_size(build_key("kernel_export"));
        string_format(log, "%s\n", BUILD_RESET);
        log_flush();

        return 0;
}

//      grep -qw, for the one-word-per-line answers QEMU gives to -accel help
//      and -display help.
static bool build_word_listed(string_address text, string_address word)
{
        build_lines walk;
        string_address found[BUILD_ARGUMENT_ROOM];
        positive parts;

        if (!text)
                return false;

        build_lines_open(address_of walk, text);

        while (build_lines_words(address_of walk, (string_address address_to)found,
                                 address_of parts))
        {
                for (positive at = 0; at < parts; at++)
                        if (word_is(found[at], word))
                                return true;
        }

        return false;
}

/*
        The same checksum the shell took of this tree's path.

        One build directory per source tree, not one per machine. This used to
        be one path for everybody: two people, or two sessions, or a person and
        an agent building at the same time wrote their objects and their image
        into the same place and neither was told. An incremental build then
        reuses whatever is there -- the userspace half from one tree and the
        kernel module from another, linked into one image that matches no
        checkout anybody has, and every measurement taken off it is about a
        tree that does not exist.

        The suffix is a checksum of this tree's own path, so the same checkout
        always gets the same directory and two checkouts never share one. It is
        our cksum, called as a function on the bytes rather than run on a file
        written only to be checksummed, and it is the POSIX one, so a directory
        made by the shell build is the directory this finds.
*/
static positive build_path_mark(string_address text)
{
        positive length = string_length(text);

        return (positive)(p32)~cksum_crc_length(hash_crc32_msb(0, text, length),
                                                length);
}

//      ssh takes shell source, not an argument vector. Keep empty words,
//      quotes and newlines intact.
static string_address build_quote(string_address address_to words, positive count)
{
        positive total = 0;
        p8 address_to into;
        p8 address_to at;

        for (positive which = 0; which < count; which++)
                total += string_length(words[which]) * 4 + 4;

        into = build_text_take(total + 1);
        at = into;

        for (positive which = 0; which < count; which++)
        {
                string_address word = words[which];

                if (which)
                        *at++ = ' ';

                *at++ = '\'';

                for (positive step = 0; word[step]; step++)
                {
                        if (word[step] != '\'')
                        {
                                *at++ = word[step];
                                continue;
                        }

                        *at++ = '\'';
                        *at++ = '\\';
                        *at++ = '\'';
                        *at++ = '\'';
                }

                *at++ = '\'';
        }

        *at = end;

        return (string_address)into;
}

/*
        Architectures, by the three names each goes by: what --arch and uname
        say, the profile that builds it, and back from a profile named on the
        line. The shell half of build.sh keeps the same table for the Mac.
*/
static string_address build_arch_name(string_address word)
{
        if (word_is(word, "x64") || word_is(word, "x86_64") ||
            word_is(word, "amd64") || word_is(word, "x86-64"))
                return "x64";
        if (word_is(word, "arm64") || word_is(word, "aarch64") ||
            word_is(word, "arm"))
                return "arm64";
        if (word_is(word, "riscv64") || word_is(word, "riscv"))
                return "riscv64";
        return null;
}

static string_address build_arch_profile(string_address arch)
{
        if (word_is(arch, "arm64"))
                return "arch/arm";
        if (word_is(arch, "riscv64"))
                return "arch/riscv";
        return "arch/x64";
}

static string_address build_arch_of_profile(string_address profile)
{
        if (string_has_prefix(profile, "arch/arm"))
                return "arm64";
        if (string_has_prefix(profile, "arch/riscv"))
                return "riscv64";
        return "x64";
}

//      This machine, which is also the default for anything booted on it.
static string_address build_arch_here(void)
{
        file_machine machine;
        string_address name;

        if (!file_machine_read(address_of machine))
                return "x64";
        name = build_arch_name((string_address)machine.machine);
        return name ? name : (string_address)"x64";
}

/*
        Building somewhere else.

        Only reached when a host was named. The remote command carries no host
        of its own and ssh does not forward the environment, so the build over
        there is an ordinary local one and this cannot recurse.
*/
static string_address build_remote_image;

/*
        Every remote entry point uses this same gate.  It resolves the parent,
        refuses a final symlink, requires a private directory owned by the SSH
        identity, pins that directory as the command's working directory and
        checks a private marker before executing anything from it.  The marker
        keeps a swapped path to another owner-private directory from becoming
        a valid build stage by accident.
*/
static string_address build_remote_stage_script =
    "set -eu\n"
    "fail() { printf \"build: refusing unsafe remote stage: %s\\n\" \"$stage\" >&2; exit 73; }\n"
    "owner_of() { stat -c %u -- \"$1\" 2>/dev/null || stat -f %u \"$1\"; }\n"
    "mode_of() { stat -c %a -- \"$1\" 2>/dev/null || stat -f %Lp \"$1\"; }\n"
    "stage=$1\n"
    "shift\n"
    "parent=$(dirname -- \"$stage\") || exit 73\n"
    "name=$(basename -- \"$stage\") || exit 73\n"
    "case $name in \"\"|.|..) fail ;; esac\n"
    "[ -d \"$parent\" ] || (umask 077; mkdir -p -- \"$parent\") || fail\n"
    "parent=$(CDPATH= cd -P -- \"$parent\" && pwd -P) || fail\n"
    "uid=$(id -u) || fail\n"
    "parent_owner=$(owner_of \"$parent\") || fail\n"
    "case $parent_owner in \"$uid\"|0) ;; *) fail ;; esac\n"
    "parent_mode=$(mode_of \"$parent\") || fail\n"
    "case $parent_mode in\n"
    "[0-7][0145][0145]|[0-7][0-7][0145][0145]|[1357][0-7][0-7][0-7]) ;;\n"
    "*) fail ;;\n"
    "esac\n"
    "stage=$parent/$name\n"
    "[ ! -L \"$stage\" ] || fail\n"
    "if [ ! -e \"$stage\" ]; then\n"
    "        umask 077\n"
    "        mkdir -m 700 -- \"$stage\" || fail\n"
    "fi\n"
    "[ -d \"$stage\" ] && [ ! -L \"$stage\" ] || fail\n"
    "[ \"$(owner_of \"$stage\")\" = \"$uid\" ] || fail\n"
    "mode=$(mode_of \"$stage\") || fail\n"
    "case $mode in [0-7][0145][0145]|[0-7][0-7][0145][0145]) ;; *) fail ;; esac\n"
    "cd -P -- \"$stage\" || fail\n"
    "[ \"$(pwd -P)\" = \"$stage\" ] || fail\n"
    "[ \"$(owner_of .)\" = \"$uid\" ] || fail\n"
    "chmod 700 . || fail\n"
    "[ \"$(mode_of .)\" = 700 ] || fail\n"
    "marker=.moonwater-stage-v1\n"
    "if [ ! -e \"$marker\" ] && [ ! -L \"$marker\" ]; then\n"
    "        (umask 077; set -C; printf \"%s\\n\" moonwater-stage-v1 > \"$marker\") || fail\n"
    "fi\n"
    "[ -f \"$marker\" ] && [ ! -L \"$marker\" ] || fail\n"
    "[ \"$(owner_of \"$marker\")\" = \"$uid\" ] || fail\n"
    "[ \"$(mode_of \"$marker\")\" = 600 ] || fail\n"
    "[ \"$(cat -- \"$marker\")\" = moonwater-stage-v1 ] || fail\n"
    "exec \"$@\"\n";

static string_address build_remote_command(
    string_address remote, string_address address_to words, positive count)
{
        string_address command[BUILD_ARGUMENT_ROOM];
        positive at = 0;

        if (count + 5 >= BUILD_ARGUMENT_ROOM)
        {
                build_die("too many remote command arguments");
                return "";
        }

        command[at++] = "sh";
        command[at++] = "-c";
        command[at++] = build_remote_stage_script;
        command[at++] = "sh";
        command[at++] = remote;

        for (positive which = 0; which < count; which++)
                command[at++] = words[which];

        command[at] = null;
        return build_quote((string_address address_to)command, at);
}

static bool build_remote_image_valid(string_address image)
{
        string_address output = build_setting_get("output");
        positive output_length = string_length(output);
        positive image_length = string_length(image);
        positive at = 0;

        if (!output_length || output[output_length - 1] == '/' ||
            image_length <= output_length + 1 ||
            memory_compare(image, output, output_length) ||
            image[output_length] != '/')
                return false;

        if (image[0] == '/')
                at++;

        while (image[at])
        {
                positive start = at;

                while (image[at] && image[at] != '/')
                {
                        if (image[at] == '\n' || image[at] == '\r')
                                return false;
                        at++;
                }

                if (at == start ||
                    (at - start == 1 && image[start] == '.') ||
                    (at - start == 2 && image[start] == '.' &&
                     image[start + 1] == '.'))
                        return false;

                if (image[at] == '/' && !image[++at])
                        return false;
        }

        return true;
}

#define BUILD_FETCH_STAGE ".moonwater-fetch.XXXXXX"

/* A fetched image remains trustworthy by pathname only while each directory
   which can replace the next component is controlled by this user or root. */
static bool build_output_directory_safe(bipolar directory)
{
        error_stat status;
        p32 user = (p32)geteuid();

        return fstat((b32)directory, address_of status) >= 0 &&
               (status.st_mode & S_IFMT) == S_IFDIR &&
               !(status.st_mode & 0022) &&
               (status.st_uid == user || status.st_uid == 0);
}

/* Claim a mode-0700 directory beside the final image and then hold it by
   descriptor.  The random exclusive mkdir prevents a co-tenant from naming
   the object first; the owner/mode check detects replacement between mkdir
   and open in a writable parent.  Once open, a fixed payload name inside is
   private and every later operation is descriptor-relative. */
static bipolar build_remote_fetch_stage(bipolar parent, p8 address_to name,
                                        positive room)
{
        const positive length = sizeof(BUILD_FETCH_STAGE) - 1;

        if (room <= length)
                return -EINVAL;

        memory_copy_end(name, BUILD_FETCH_STAGE, length);

        for (positive attempt = 0; attempt < TMP_MAX; attempt++)
        {
                error_stat status;
                bipolar made;
                bipolar stage;

                if (!build_random_marks(name + length - SPOOL_TEMPLATE_MARKS))
                        return -EIO;

                made = system_make_directory_exact_at(parent, name, 0700);
                if (made == -EEXIST)
                        continue;
                if (made < 0)
                        return made;

                stage = system_open_at(parent, name,
                                       O_PATH | O_DIRECTORY | O_NOFOLLOW |
                                           O_CLOEXEC);
                if (stage < 0)
                {
                        system_remove_at(parent, name, AT_REMOVEDIR);
                        return stage;
                }

                if (fstat((b32)stage, address_of status) < 0 ||
                    (status.st_mode & S_IFMT) != S_IFDIR ||
                    (status.st_mode & 07777) != 0700 ||
                    status.st_uid != (p32)geteuid())
                {
                        system_close(stage);
                        system_remove_at(parent, name, AT_REMOVEDIR);
                        return -EACCES;
                }

                return stage;
        }

        return -EEXIST;
}

/* Stream inside a private directory, sync and mode the completed bytes, then
   rename over the name itself.  Parent components and the private stage are
   held through no-follow descriptors, so neither an intermediate, temporary
   nor final symlink can redirect publication outside the configured tree. */
static b32 build_remote_fetch(string_address host, string_address remote,
                              string_address image)
{
        p8 leaf[256];
        p8 stage_name[64];
        bipolar parent = system_open_parent_nofollow_checked(
            AT_FDCWD, image, true, 0755, leaf, sizeof(leaf),
            build_output_directory_safe);
        bipolar stage;
        bipolar handle;
        build_command fetch = {0};
        bool failed;
        bool renamed = false;
        file_facts opened;
        file_facts named;
        string_address request[3] = {"cat", "--", image};
        string_address words[5];

        if (parent < 0)
                return build_die("could not prepare the local image path");

        stage = build_remote_fetch_stage(parent, stage_name,
                                         sizeof(stage_name));
        if (stage < 0)
        {
                system_close(parent);
                return build_die("could not create a private image stage");
        }

        handle = system_open_output_at(stage, "image", false, 0600);
        if (handle < 0)
        {
                system_close(stage);
                system_remove_at(parent, stage_name, AT_REMOVEDIR);
                system_close(parent);
                return build_die("could not create a temporary image");
        }

        words[0] = "ssh";
        words[1] = "-n";
        words[2] = host;
        words[3] = build_remote_command(
            remote, (string_address address_to)request, 3);
        words[4] = null;
        fetch.words = (string_address address_to)words;
        fetch.output = (b32)handle;

        failed = build_execute(address_of fetch) != 0;
        if (!failed && system_call_2(syscall(fchmod), (positive)handle,
                                     0644) < 0)
                failed = true;
        if (!failed &&
            system_call_1(syscall(fsync), (positive)handle) < 0)
                failed = true;

        /* The writer receives the already-open descriptor, but a same-user
           helper can still unlink the private name and put another inode in
           its place. Check immediately before the rename, then keep the
           descriptor through it and bind the published name back to that
           inode. A substitution at either pathname is therefore a failed
           fetch rather than an accepted artifact. */
        if (!failed &&
            (!file_look(handle, "", AT_EMPTY_PATH, address_of opened) ||
             !file_look(stage, "image", AT_SYMLINK_NOFOLLOW,
                        address_of named) ||
             !file_same_identity(address_of opened, address_of named) ||
             (named.mode & MODE_FORMAT) != MODE_FILE))
                failed = true;
        if (!failed)
        {
                if (system_rename_at(stage, "image", parent, leaf, 0) < 0)
                        failed = true;
                else
                        renamed = true;
        }
        if (!failed &&
            (!file_look(parent, leaf, AT_SYMLINK_NOFOLLOW,
                        address_of named) ||
             !file_same_identity(address_of opened, address_of named) ||
             (named.mode & MODE_FORMAT) != MODE_FILE))
                failed = true;
        if (system_close(handle) < 0)
                failed = true;
        if (failed)
        {
                system_remove_at(stage, "image", 0);
                if (renamed &&
                    file_look(parent, leaf, AT_SYMLINK_NOFOLLOW,
                              address_of named) &&
                    file_same_identity(address_of opened, address_of named))
                        system_remove_at(parent, leaf, 0);
        }
        system_close(stage);
        system_remove_at(parent, stage_name, AT_REMOVEDIR);
        system_close(parent);

        return failed ? build_die("could not fetch the built image") : 0;
}

static b32 build_remote(string_address host, string_address remote,
                        string_address address_to profiles, positive count,
                        string_address target, string_address profile_arch,
                        bool clean)
{
        string_address arguments;
        string_address stock = string_get_environment(environ, "MOONWATER_STOCK");
        string_address sent[BUILD_ARGUMENT_ROOM];
        positive many = 0;

        /* Five fixed build words, --arch and its name, plus the five-word
           staging front must fit. */
        if (count + 12 >= BUILD_ARGUMENT_ROOM)
                return build_die("too many remote build arguments");

        build_say(build_join("Checking ", host, null));

        /*
                Asked only when nothing on this side decided: a plain build is
                for the machine doing it.
        */
        if (!target)
        {
                string_address words[] = {
                    "ssh", "-n", "-o", "BatchMode=yes", "-o",
                    "ConnectTimeout=20", host, "uname", "-m", null};
                bipolar got = build_capture_words(words, build_file_two,
                                                  BUILD_FILE_ROOM);
                positive length;

                if (got < 0)
                        return build_die(build_join("cannot reach ", host,
                                                    " over ssh", null));
                length = string_length((string_address)build_file_two);
                while (length && (build_file_two[length - 1] == '\n' ||
                                  build_file_two[length - 1] == '\r'))
                        build_file_two[--length] = end;
                target = build_arch_name((string_address)build_file_two);
                if (!target)
                        target = "x64";
        }
        else if (build_run("ssh", "-n", "-o", "BatchMode=yes", "-o",
                           "ConnectTimeout=20", host, "true", null))
                return build_die(build_join("cannot reach ", host,
                                            " over ssh", null));

        /*
                Under the build host's ~/.cache, relative to the home an ssh
                command starts in, and one per architecture's profile, as
                build.sh names it.
        */
        if (!remote || !*remote)
        {
                string_address flavour = profile_arch ? profile_arch
                                                      : build_arch_profile(target);

                remote = build_join(".cache/", build_setting_get("name"), "/",
                                    build_name_of(build_working_directory()),
                                    "-",
                                    build_number(build_path_mark(
                                            build_working_directory())),
                                    "-", flavour + 5, null);
        }

        //      The architecture travels as --arch rather than as a profile,
        //      which would replace the default set rather than swap its arch/
        //      member.
        if (!profile_arch)
        {
                sent[many++] = "--arch";
                sent[many++] = target;
        }
        for (positive which = 0; which < count; which++)
                sent[many++] = profiles[which];
        profiles = (string_address address_to)sent;
        count = many;
        arguments = build_quote(profiles, count);

        /*
                What a remote build produced is on the remote, and root owns
                its kernel tree, since that build runs under sudo: cleaning
                here would remove this side's output and leave the tree that
                needed it. So the remote cleans itself, the same way it builds.
        */
        if (clean)
        {
                string_address request[] = {"sudo", "sh", "build.sh", "--clean"};

                build_say(build_join("Cleaning ", host, ":", remote, null));
                if (build_run("ssh", "-n", host,
                              build_remote_command(
                                  remote, (string_address address_to)request, 4),
                              null))
                        return build_die(build_join("cleaning failed on ", host,
                                                    null));
                return 0;
        }

        build_say(build_join("Copying the tree to ", host, ":", remote, null));

        /*
                The kernel source, its artifacts and the built filesystem stay
                on the build host: they are large, and none of them belong to
                this checkout. The upstream tree is not part of this
                repository.
        */
        {
                string_address words[BUILD_ARGUMENT_ROOM];
                string_address excluded = build_setting_get("remote_excludes");
                string_address names[BUILD_ARGUMENT_ROOM];
                positive many = build_split(excluded, (string_address address_to)names,
                                            BUILD_ARGUMENT_ROOM);
                positive at = 0;

                words[at++] = "rsync";
                words[at++] = "-az";
                words[at++] = "--delete";
                words[at++] = "--no-perms";

                {
                        string_address request[1] = {"rsync"};

                        words[at++] = build_join(
                            "--rsync-path=",
                            build_remote_command(
                                remote, (string_address address_to)request, 1),
                            null);
                }

                words[at++] = "--exclude";
                words[at++] = "/.moonwater-stage-v1";

                for (positive which = 0; which < many; which++)
                {
                        if (at + 3 >= BUILD_ARGUMENT_ROOM)
                                return build_die("too many remote exclusions");
                        words[at++] = "--exclude";
                        words[at++] = names[which];
                }

                if (at + 2 >= BUILD_ARGUMENT_ROOM)
                        return build_die("too many remote exclusions");
                words[at++] = "./";
                words[at++] = build_join(host, ":./", null);
                words[at] = null;

                if (build_run_words((string_address address_to)words))
                        return build_die("copying the tree failed");
        }

        build_say(build_join("Building on ", host, ": ", arguments, null));

        /*
                -n so the build does not swallow this program's stdin. Without
                it the USB prompts read nothing, because ssh forwards whatever
                is on stdin to the remote command.

                sudo drops the environment, so anything the remote build has to
                know is named here. env rather than a VAR=value prefix, which
                sudo only passes when it has been configured to.
        */
        {
                string_address request[BUILD_ARGUMENT_ROOM];
                positive at = 0;

                request[at++] = "sudo";
                request[at++] = "env";
                if (stock && *stock)
                        request[at++] = "MOONWATER_STOCK=1";
                request[at++] = "sh";
                request[at++] = "build.sh";
                for (positive which = 0; which < count; which++)
                        request[at++] = profiles[which];

                if (build_run(
                        "ssh", "-n", host,
                        build_remote_command(
                            remote, (string_address address_to)request, at),
                        null))
                        return build_die(build_join("the build failed on ", host,
                                                    null));
        }

        /*
                The host which built the configured profile is authoritative
                about its export. A stale local configuration may describe
                another architecture entirely -- bootaa64.efi, or a Pi's
                kernel8.img.
        */
        {
                string_address request[3] = {
                    "./build", "key-one", "kernel_export"};
                string_address words[5];
                positive at = 0;

                words[at++] = "ssh";
                words[at++] = "-n";
                words[at++] = host;
                words[at++] = build_remote_command(
                    remote, (string_address address_to)request, 3);
                words[at] = null;

                if (build_capture_words((string_address address_to)words,
                                        build_file_two, BUILD_FILE_ROOM) < 0)
                        return build_die("could not identify the built image");

                {
                        positive length = string_length((string_address)build_file_two);

                        while (length && (build_file_two[length - 1] == '\n' ||
                                          build_file_two[length - 1] == '\r'))
                                build_file_two[--length] = end;

                        build_remote_image = build_join((string_address)build_file_two,
                                                        null);
                }
        }

        if (!build_remote_image_valid(build_remote_image))
                return build_die(build_join(
                    "remote build reported an invalid image path: ",
                    build_remote_image, null));

        build_say(build_join("Fetching ", build_remote_image, null));
        return build_remote_fetch(host, remote, build_remote_image);
}

/*
        Removing what a build produced.

        The artifacts directory keeps the downloaded kernel tarball and is left
        alone on purpose: throwing it away means fetching a hundred and fifty
        megabytes again to get back where you were.
*/
static b32 build_clean()
{
        string_address artifacts = build_setting_get("artifacts");
        string_address leftovers = build_setting_get("clean_patterns");
        string_address names[BUILD_ARGUMENT_ROOM];
        positive many;

        build_say("Removing build output");

        build_tool("rm", "-rf", build_setting_get("output"),
                   build_setting_get("image_root"),
                   build_setting_get("kernel_tree"),
                   build_join(artifacts, "/merge.config", null),
                   build_join(artifacts, "/.config", null),
                   build_join(artifacts, "/info", null),
                   build_join(artifacts, "/asm.applied", null),
                   build_join(artifacts, "/asm.arch", null),
                   build_join(artifacts, "/asm.requested", null), null);

        /*
                The build products beside the module's source, including the
                .S each .asm becomes -- which kbuild writes there because that
                directory is the kernel tree's own module directory.

                The patterns begin [!.] because a shell glob does not match a
                leading dot and find's -name does. kbuild's own .o.cmd files
                are dotfiles and the shell never removed them; neither does
                this.
        */
        many = build_split(leftovers, (string_address address_to)names,
                           BUILD_ARGUMENT_ROOM);

        for (positive at = 0; at < many; at++)
                build_tool("find", build_setting_get("module_root"), "-maxdepth",
                           "1", "-name", names[at], "-delete", null);

        return 0;
}

/*
        Writing to a USB stick.

        The image is already an EFI application -- the kernel is built with the
        EFI stub, which is why it is called bootx64.efi -- so firmware can load
        it directly and there is no bootloader to install. It goes at the path
        the UEFI spec reserves for removable media, \\EFI\\BOOT\\BOOTX64.EFI,
        which is what a machine looks for when told to boot from USB.

        This lists the candidates and prints the command rather than running
        it. Writing to a raw block device with the wrong name destroys the
        wrong disk, and there is no honest way to claim care against an
        untested lsblk and dd, so the last step stays in your hands.
*/
static b32 build_usb(string_address image)
{
        build_say("Removable disks");

        if (build_have("lsblk"))
        {
                string_address words[8];
                positive at = 0;
                build_lines walk;
                string_address found[BUILD_ARGUMENT_ROOM];
                positive parts;

                words[at++] = "lsblk";
                words[at++] = "-dno";
                words[at++] = "NAME,SIZE,RM,MODEL";
                words[at] = null;

                if (build_capture_words((string_address address_to)words,
                                        build_file_one, BUILD_FILE_ROOM) >= 0)
                {
                        build_lines_open(address_of walk,
                                         (string_address)build_file_one);

                        while (build_lines_words(address_of walk,
                                                 (string_address address_to)found,
                                                 address_of parts))
                        {
                                if (parts < 3 || !word_is(found[2], "1"))
                                        continue;

                                string_format(log, "  /dev/%s  %s  %s\n",
                                              found[0], found[1],
                                              parts > 3 ? found[3]
                                                        : (string_address)"");
                        }
                }
        }
        else
                string_format(log, "  (lsblk is missing; find the device yourself)\n");

        string_format(log, "\n");
        string_format(log, "Write it with, replacing sdX with the stick:\n");
        string_format(log, "\n");
        string_format(log,
                      "  sudo mkfs.vfat -F32 /dev/sdX1        # after partitioning it GPT/ESP\n");
        string_format(log, "  sudo mount /dev/sdX1 /mnt\n");
        string_format(log, "  sudo mkdir -p /mnt/EFI/BOOT\n");
        {
                //      bootx64.efi, bootaa64.efi or bootriscv64.efi: the removable-media name
                //      for the machine the image is for is its own, in capitals.
                string_address name = build_join(build_name_of(image), null);

                for (positive at = 0; name[at]; at++)
                        if (name[at] >= 'a' && name[at] <= 'z')
                                name[at] -= 'a' - 'A';
                string_format(log, "  sudo cp %s /mnt/EFI/BOOT/%s\n", image, name);
        }
        string_format(log, "  sudo umount /mnt\n");
        string_format(log, "\n");
        string_format(log,
                      "Check the device name twice. This erases whatever it names.\n");
        log_flush();

        return 0;
}

/*
        Booting it here.

        Every one of these flags is a requirement rather than a preference, and
        they are settings so another tree can boot its own image with its own:

        virtio-gpu rather than the default VGA, because it is the only device
        here that offers a hardware cursor plane, which is what lets the
        compositor move the pointer without repainting anything. usb-tablet
        reports absolute positions, so the pointer inside the guest follows the
        one on the host instead of drifting. -vga none matters: without it QEMU
        also creates a standard VGA device, the window shows that one because
        it is the boot VGA, and the compositor ends up drawing on the other
        card where nobody can see it.

        -cpu Nehalem, not the default. The kernel is compiled -march=x86-64-v2,
        whose floor is Nehalem, and QEMU's default model is qemu64 -- SSE3-era,
        no POPCNT. There are 334 popcnt instructions in vmlinux, so on the
        default model the image takes an invalid opcode before the console
        exists and prints nothing whatsoever. This line is what stands between
        that and here.

        drm_client_lib.active= on the command line stops the fbdev client
        claiming the display. It has to be built, but it must not take the
        screen, or the compositor is drawing underneath something else.
*/
static string_address build_boot_setting(string_address name,
                                         string_address arch)
{
        string_address value = null;

        if (!word_is(arch, "x64"))
                value = build_setting_get(build_join(name, "_", arch, null));
        return value ? value : build_setting_get(name);
}

static b32 build_boot(string_address image, bool console, string_address arch)
{
        string_address emulator = build_boot_setting("emulator", arch);
        bool native = word_is(build_arch_here(), arch);
        string_address words[BUILD_ARGUMENT_ROOM];
        string_address accelerators = "";
        string_address display = null;
        positive count = 0;

        if (!build_have(emulator))
                return build_die(build_join(emulator, " is not installed", null));

        positive length = string_length(image);

        if (length >= 12 && !memory_compare(image + length - 12, "/kernel8.img", 12))
                return build_die(build_join(
                        image, " is a Raspberry Pi image; QEMU's virt machine "
                        "cannot boot it -- build --arch arm64 for a VM", null));

        build_say(build_join("Booting ", image, " (", arch, ")", null));
        build_size(image);

        words[count++] = emulator;
        count = build_add_split((string_address address_to)words, count,
                                BUILD_ARGUMENT_ROOM,
                                build_boot_setting("emulator_flags", arch));
        words[count++] = "-kernel";
        words[count++] = image;
        count = build_add_split((string_address address_to)words, count,
                                BUILD_ARGUMENT_ROOM,
                                build_boot_setting("emulator_devices", arch));

        //      Hardware acceleration where this QEMU has it and the guest is
        //      this machine's own architecture; anything else is emulated.
        //      -cpu host replaces the model above, which is what you want
        //      when the guest is running on the real one.
        if (native)
        {
                string_address ask[4];

                ask[0] = emulator;
                ask[1] = "-accel";
                ask[2] = "help";
                ask[3] = null;

                if (build_capture_words((string_address address_to)ask,
                                        build_file_one, BUILD_FILE_ROOM) >= 0)
                        accelerators = (string_address)build_file_one;
        }

        if (build_word_listed(accelerators, "hvf"))
        {
                words[count++] = "-accel";
                words[count++] = "hvf";
                words[count++] = "-cpu";
                words[count++] = "host";
        }
        else if (build_word_listed(accelerators, "kvm") &&
                 access("/dev/kvm", W_OK) >= 0)
        {
                words[count++] = "-accel";
                words[count++] = "kvm";
                words[count++] = "-cpu";
                words[count++] = "host";
        }

        words[count++] = "-append";
        words[count++] = build_boot_setting("kernel_cmdline", arch);

        if (console)
        {
                build_say("Console on this terminal, ctrl-a x to quit");
                words[count++] = "-display";
                words[count++] = "none";
                words[count++] = "-serial";
                words[count++] = "mon:stdio";
                words[count] = null;

                return build_run_words((string_address address_to)words);
        }

        {
                string_address ask[4];
                string_address available = "";

                ask[0] = emulator;
                ask[1] = "-display";
                ask[2] = "help";
                ask[3] = null;

                if (build_capture_words((string_address address_to)ask,
                                        build_file_two, BUILD_FILE_ROOM) >= 0)
                        available = (string_address)build_file_two;

                if (build_word_listed(available, "gtk"))
                        display = "gtk";
                else if (build_word_listed(available, "sdl"))
                        display = "sdl";
                else
                        return build_die(
                                "this QEMU has no graphical display backend -- use --shell");
        }

        build_say("Window opening, ctrl-alt-g releases the mouse");
        words[count++] = "-display";
        words[count++] = display;
        words[count++] = "-serial";
        words[count++] = "mon:stdio";
        words[count] = null;

        return build_run_words((string_address address_to)words);
}

/*
        Every path in this tool is relative to the repository root, so running
        it from anywhere else quietly writes into the wrong place. Checked by
        looking for what only a root has rather than by its name, which is the
        build inputs identify: src and kernel hold the sources and build.sh
        is the bootstrap entry.
*/
static fn build_is_safe()
{
        if (build_is_directory("src") && build_is_directory("kernel") &&
            build_is_file("build.sh"))
                return;

        string_format(log_error, "ERROR: not in the repository root.\n");
        string_format(log_error,
                      "This tool expects to run from the directory holding\n");
        string_format(log_error, "build.sh, kernel/ and src/.\n");
        exit(1);
}

static fn build_usage()
{
        string_format(log,
                      "Builds a Moonwater image, and optionally boots or writes it.\n"
                      "\n"
                      "    build                       build with the default profiles\n"
                      "    build arch/x64 debug_none   build with the profiles named\n"
                      "    build --run                 build, then boot it in a window\n"
                      "    build --run --shell         boot with the console on this terminal\n"
                      "    build --boot                boot the last image, do not rebuild\n"
                      "    build --usb                 build, then write a USB stick\n"
                      "    build --clean               remove what a build produced\n"
                      "    build --host box            build on another machine over ssh\n"
                      "    build --arch arm64 --run    build and boot another architecture\n"
                      "\n"
                      "The architecture defaults to the machine that will run the image:\n"
                      "this one for --run and --boot, even when --host compiles it, and\n"
                      "otherwise the machine building it. --arch x64, arm64 or riscv64\n"
                      "(also x86_64, amd64, aarch64, arm, riscv) picks one, and an arch/\n"
                      "profile on the line wins over both.\n"
                      "\n"
                      "Build operations:\n"
                      "\n"
                      "    build config <profile ...>              compose artifacts/.config\n"
                      "    build verify-config <config> [profile ...]  what the profiles did not get\n"
                      "    build asm <arch> <in.asm> <out.S>       one architecture out of a .asm\n"
                      "    build spark <source> <output> [debug]   link a spark program\n"
                      "    build freestanding [-v] [--run] [--watch] [source] [output]\n"
                      "    build floor [arch]                      verify the ISA floor\n"
                      "    build key <name>                        a value from artifacts/.config\n"
                      "    build switches [check]                  write or check the Kconfig switches\n"
                      "    build surface <config> [utility]        the names that .config installs\n"
                      "    build config-header <config> <header> [utility]\n"
                      "                                            the header a .config gives the programs\n"
                      "\n"
                      "--set name=value overrides one setting, anywhere on the line.\n"
                      "    build key-one <name>                    the same, refusing two\n"
                      "    build size <path>                       bytes, KB and MB\n");
        log_flush();
}

/* Command metadata owns validation and setup order. Profiles are resolved
   before this table, so a profile can still shadow a command name. */
#define BUILD_COMMANDS(X) \
    X(CONFIG, "config", 2, 0, true, true, null) \
    X(VERIFY_CONFIG, "verify-config", 3, 0, true, false, \
      "verify_config: usage: build verify-config <built .config> [profile ...]\n") \
    X(ASM, "asm", 5, 5, false, false, \
      "asm: usage: asm <arch> <input.asm> <output.S>\n") \
    X(SPARK, "spark", 4, 0, true, true, \
      "spark: usage: spark <source_without_extension> <output> [debug]\n") \
    X(KEY, "key", 3, 0, true, true, null) \
    X(KEY_ONE, "key-one", 3, 0, true, true, null) \
    X(SIZE, "size", 3, 0, false, false, null) \
    X(SWITCHES, "switches", 2, 3, true, false, \
      "switches: usage: build switches [check]\n") \
    X(SURFACE, "surface", 3, 4, true, false, \
      "surface: usage: build surface <config> [utility]\n") \
    X(CONFIG_HEADER, "config-header", 4, 5, true, false, \
      "config-header: usage: build config-header <config> <header> [utility]\n") \
    X(FREESTANDING, "freestanding", 2, 0, false, true, null) \
    X(FLOOR, "floor", 2, 0, true, false, null) \
    X(HELP, "--help", 2, 0, false, false, null) \
    X(HELP_SHORT, "-h", 2, 0, false, false, null)
#define BUILD_COMMAND_ID(id, ...) BUILD_##id,
enum { BUILD_COMMANDS(BUILD_COMMAND_ID) };
#undef BUILD_COMMAND_ID

static const struct
{
        string_address name;
        p8 minimum, maximum;
        bool safe, config;
        string_address usage;
} build_commands[] = {
#define BUILD_COMMAND_ROW(id, ...) {__VA_ARGS__},
        BUILD_COMMANDS(BUILD_COMMAND_ROW)
#undef BUILD_COMMAND_ROW
};
#undef BUILD_COMMANDS

static string_address build_words_kept[BUILD_ARGUMENT_ROOM];

b32 main()
{
        string_address address_to arguments = program_argument_list();
        positive count = (positive)program_argument_count();
        string_address command;

        /*
                --set name=value, read before anything else looks at the
                arguments, so it reaches every sub-command and not only the
                build. The words are taken out of the vector here rather than
                skipped in each parser below, which is the whole reason this
                is one pass over the arguments and not five.
        */
        build_settings_read();

        {
                positive kept = 0;

                for (positive at = 0; at < count && kept + 1 < BUILD_ARGUMENT_ROOM;
                     at++)
                {
                        string_address word = arguments[at];
                        string_address pair = null;
                        p8 address_to cut;

                        if (at && word_is(word, "--set") && at + 1 < count)
                                pair = arguments[++at];
                        else if (at && string_has_prefix(word, "--set="))
                                pair = word + 6;
                        else
                        {
                                build_words_kept[kept++] = word;
                                continue;
                        }

                        pair = build_join(pair, null);
                        cut = (p8 address_to)string_first_of(pair, '=');

                        if (!cut)
                                return build_die("--set wants name=value");

                        address_to cut = end;
                        build_setting_set(pair, (string_address)(cut + 1));
                }

                build_words_kept[kept] = null;
                arguments = (string_address address_to)build_words_kept;
                count = kept;
        }

        command = count > 1 ? arguments[1] : null;

        //      A sub-command is only a sub-command when it cannot also be a
        //      profile: the build takes profile names as bare words, and a
        //      profile added later must not silently become a mode.
        if (command)
        {
                string_address profile = build_join(build_setting_get("profile_root"),
                                                    "/", command, null);

                if (build_is_file(profile))
                        command = null;
        }

        for (positive mode = 0; command && mode < array_count(build_commands); mode++)
        {
                if (!word_is(command, build_commands[mode].name))
                        continue;
                if (build_commands[mode].safe)
                        build_is_safe();
                if (build_commands[mode].config)
                        build_config_load();
                if (count < build_commands[mode].minimum ||
                    (build_commands[mode].maximum && count > build_commands[mode].maximum))
                {
                        if (build_commands[mode].usage)
                                string_format(log_error, "%s", build_commands[mode].usage);
                        return 1;
                }

                switch (mode)
                {
                case BUILD_CONFIG:
                        return build_config(arguments + 2, count - 2);
                case BUILD_VERIFY_CONFIG:
                        return build_verify_config(arguments[2], arguments + 3, count - 3);
                case BUILD_ASM:
                        return build_asm(arguments[2], arguments[3], arguments[4]);
                case BUILD_SPARK:
                        return build_spark(arguments[2], arguments[3], count > 4 ? arguments[4] : null);
                case BUILD_KEY:
                case BUILD_KEY_ONE:
                {
                        bool good = true;
                        string_address answer = mode == BUILD_KEY
                            ? build_key(arguments[2]) : build_key_one(arguments[2], address_of good);
                        if (!good)
                                return 1;
                        string_format(log, "%s\n", answer);
                        log_flush();
                        return 0;
                }
                case BUILD_SIZE:
                        build_size(arguments[2]);
                        return 0;
                case BUILD_SWITCHES:
                        return build_switches(count > 2 ? arguments[2] : null);
                case BUILD_SURFACE:
                        return build_surface(arguments[2], count > 3 ? arguments[3] : null);
                case BUILD_CONFIG_HEADER:
                        return build_config_header_write(arguments[2], arguments[3],
                                                         count > 4 ? arguments[4] : null);
                case BUILD_FREESTANDING:
                        return build_freestanding(arguments + 2, count - 2);
                case BUILD_FLOOR:
                        return build_floor(count > 2 ? arguments[2] : null);
                case BUILD_HELP:
                case BUILD_HELP_SHORT:
                        build_usage();
                        return 0;
                }
        }

        /*
                The build.

                Anything that is not an option is a profile name, so the two
                can be mixed in any order: `build --run desktop`.
        */
        {
                string_address profiles[BUILD_ARGUMENT_ROOM];
                string_address host = string_get_environment(environ,
                                                             "MOONWATER_BUILD_HOST");
                string_address remote = string_get_environment(environ,
                                                               "MOONWATER_BUILD_DIR");
                string_address image = null;
                positive chosen = 0;
                bool run = false;
                bool make = true;
                bool clean = false;
                bool usb = false;
                bool console = false;
                string_address arch_asked = null;
                string_address profile_arch = null;
                string_address target = null;

                build_is_safe();

                for (positive at = 1; at < count; at++)
                {
                        string_address word = arguments[at];

                        if (word_is(word, "--clean"))
                                clean = true;
                        else if (word_is(word, "--run"))
                                run = true;
                        else if (word_is(word, "--boot"))
                        {
                                run = true;
                                make = false;
                        }
                        else if (word_is(word, "--shell"))
                                console = true;
                        else if (word_is(word, "--usb"))
                                usb = true;
                        else if (word_is(word, "--host"))
                        {
                                if (at + 1 >= count)
                                        return build_die(
                                                "--host wants a machine to build on");

                                host = arguments[++at];
                        }
                        else if (string_has_prefix(word, "--host="))
                                host = word + 7;
                        else if (word_is(word, "--arch") ||
                                 string_has_prefix(word, "--arch="))
                        {
                                string_address name = word + 7;

                                if (!word[6])
                                {
                                        if (at + 1 >= count)
                                                return build_die(
                                                        "--arch wants x64, arm64 or riscv64");
                                        name = arguments[++at];
                                }

                                arch_asked = build_arch_name(name);
                                if (!arch_asked)
                                        return build_die(build_join(
                                                "unknown architecture ", name,
                                                " -- x64, arm64 or riscv64", null));
                        }
                        else if (word[0] == '-' && word[1] == '-')
                                return build_die(build_join("unknown option ",
                                                            word, null));
                        else if (chosen + 1 < BUILD_ARGUMENT_ROOM)
                                profiles[chosen++] = word;
                }

                profiles[chosen] = null;

                /*
                        Which architecture. An arch/ profile on the line is the
                        whole answer. Otherwise --arch, and otherwise this
                        machine -- except for a remote build that is not going
                        to be booted here, which is for the machine building
                        it and is asked of that machine.
                */
                for (positive which = 0; which < chosen; which++)
                        if (string_has_prefix(profiles[which], "arch/"))
                                profile_arch = profiles[which];

                if (profile_arch)
                        target = build_arch_of_profile(profile_arch);
                else if (arch_asked)
                        target = arch_asked;
                else if (!(host && *host) || run)
                        target = build_arch_here();

                if (clean)
                        return host && *host
                                   ? build_remote(host, remote,
                                                  (string_address address_to)profiles,
                                                  chosen, target, profile_arch, true)
                                   : build_clean();

                build_config_load();

                if (make)
                {
                        if (host && *host)
                        {
                                if (build_remote(host, remote,
                                                 (string_address address_to)profiles,
                                                 chosen, target, profile_arch, false))
                                        return 1;

                                image = build_remote_image;
                        }
                        else if (build_local((string_address address_to)profiles,
                                             chosen,
                                             profile_arch ? null
                                                          : build_arch_profile(target)))
                                return 1;
                }

                if (!usb && !run)
                        return 0;

                /*
                        Where the image ended up. A remote build set this from
                        its own generated configuration. A local build, or
                        --boot without a build, asks the local configuration
                        -- loaded above, and reloaded by build_config after it
                        rewrites it -- and finally falls back to the default
                        export.
                */
                if (!image && !make)
                {
                        /*
                                --boot boots the architecture asked for, or
                                else whatever the local configuration last
                                built, told by its arch line.
                        */
                        string_address built = build_key_one("arch", null);

                        if (arch_asked || !built || !*built ||
                            !build_arch_name(built))
                                image = build_boot_setting("default_image",
                                                           target ? target : (string_address)"x64");
                        else
                        {
                                target = build_arch_name(built);
                                image = build_key_one("kernel_export", null);
                        }
                }

                if (!image)
                        image = build_key_one("kernel_export", null);

                if (!image || !*image)
                        image = build_boot_setting("default_image",
                                                   target ? target : (string_address)"x64");

                if (!build_is_file(image))
                        return build_die(build_join("no image at ", image,
                                                    " -- build one first, or drop --boot",
                                                    null));

                if (usb)
                        return build_usb(image);

                return build_boot(image, console, target ? target : (string_address)"x64");
        }
}
