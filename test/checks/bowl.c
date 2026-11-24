/*
        test/checks/bowl.c: bowls.

        A run of test/checks.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. A section is built as
        test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib -nostartfiles
        -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its
        freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name>
        test/checks.c).
*/

#ifdef CHECK_bowl
//      Bowl's roots, for this check only, somewhere a user may write, so
//      landing is tested on real directories; every spelling below is built
//      from it, as bowl.c's own are.
#define BOWL_ROOT_DIRECTORY "/tmp/moonwater-bowl-check"
#include "../../src/lib.util.c"
#include "../../src/moonwater/spark.c"
#include "../../src/sh/shell.c"

/* Use Bowl's production include context; execute only pure validation. */
#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

static fn names(void)
{
        static const struct { string_address name; p8 valid; } roots[] = {
            {null, false}, {"", false}, {"/", false}, {"/bowl", false},
            {BOWL_ROOT_DIRECTORY, false}, {BOWL_ROOT_PREFIX, false}, {BOWL_ROOT_PREFIX "a", true},
            {BOWL_ROOT_PREFIX "bin", false}, {BOWL_ROOT_PREFIX "a.b_-", true}, {BOWL_ROOT_PREFIX ".", false},
            {BOWL_ROOT_PREFIX "..", false}, {BOWL_ROOT_PREFIX "a/b", false}, {BOWL_ROOT_PREFIX "a+", false},
            {BOWL_ROOT_PREFIX "a ", false}, {BOWL_ROOT_PREFIX "a\xff", false}};

        for (positive at = 0; at < array_count(roots); at++)
                check("Bowl root prefixes and reserved names",
                      bowl_named_root(roots[at].name) == roots[at].valid);

        for (positive byte = 0; byte <= 255; byte++)
        for (positive plus = 0; plus < 2; plus++)
        {
                p8 name[] = {'x', byte, 'y', 0};
                p8 valid = !byte || (byte >= 'a' && byte <= 'z') ||
                    (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') ||
                    byte == '-' || byte == '_' || byte == '.' || (plus && byte == '+');
                check("Bowl root and command byte policies",
                      bowl_name(name, plus) == valid);
        }
}

static fn launchers(void)
{
        static string_address encoded[] = {
            "@" BOWL_ROOT_PREFIX "debian/usr/bin/jq", "@" BOWL_ROOT_PREFIX "arch/bin/sh", "@" BOWL_ROOT_PREFIX "./bin/sh",
            "@" BOWL_ROOT_PREFIX "../bin/sh", "@" BOWL_ROOT_PREFIX "bin/echo", "@" BOWL_ROOT_PREFIX "a/", "@" BOWL_ROOT_PREFIX "a",
            "@" BOWL_ROOT_PREFIX, "@" BOWL_ROOT_DIRECTORY, "@/", "@", "", "!" BOWL_ROOT_PREFIX "a/bin/sh"};
        static string_address roots[] = {BOWL_ROOT_PREFIX "debian", BOWL_ROOT_PREFIX "arch"};
        static string_address programs[] = {"/usr/bin/jq", "/bin/sh"};

        for (positive item = 0; item < array_count(encoded); item++)
        for (positive room = 0; room <= 48; room++)
        {
                p8 kept[56], line[64];
                string_address program = null;
                memory_fill(kept, 0xa5, sizeof kept);
                // bowl_launcher cuts a shebang line's newline where it is, so it is given a line of its own, as a read one is (the
                // literal itself is read-only: the write at its end was the SIGSEGV that ended every run here).
                string_copy_bounded(line, encoded[item], sizeof line);
                p8 got = bowl_launcher(line, kept + 4, room, &program);
                p8 valid = item < 2 && room > string_length((const_string)roots[item]);
                p8 intact = true;
                for (positive at = 0; at < sizeof kept; at++)
                        if (at < 4 || at >= 4 + room)
                                intact &= kept[at] == 0xa5;
                check("Bowl launcher root boundaries and destination canaries",
                      got == valid && intact &&
                          (valid ? string_equals(kept + 4, roots[item]) &&
                                       string_equals(program, programs[item])
                                 : program == null));
        }
}

static fn isolation(void)
{
        /* Pacman 7.1 [options] keywords. Isolation text is parsed as
           pacman.conf; an unrecognized directive is a warning and is not
           isolation. */
        static string_address pacman7[] = {
            "RootDir", "DBPath", "CacheDir", "HookDir", "GPGDir", "LogFile",
            "HoldPkg", "IgnorePkg", "IgnoreGroup", "Include", "Architecture",
            "XferCommand", "NoUpgrade", "NoExtract", "CleanMethod", "SigLevel",
            "LocalFileSigLevel", "RemoteFileSigLevel", "UseSyslog", "Color",
            "NoProgressBar", "CheckSpace", "VerbosePkgLists",
            "DisableDownloadTimeout", "ParallelDownloads", "DownloadUser",
            "DisableSandbox", "DisableSandboxFilesystem",
            "DisableSandboxSyscalls"};
        static string_address words[] = {
            "DisableHook", "DisableSandbox", "DisableSandboxFilesystem",
            "DisableSandboxSyscalls", "DisableSandboxSomething", "HookDir",
            "ILoveCandy", "Disable", "Architecture"};
        p8 text[512];
        p8 line[80];
        byte_store isolation = {text, sizeof(text), 0};
        positive used;
        positive at = 0;
        positive word;
        positive form;

        check("bowl isolation text fits",
              bowl_put_isolation(address_of isolation) && isolation.used &&
                  isolation.used < sizeof(text));
        used = isolation.used;
        text[used] = 0;

        while (at < used)
        {
                positive stop = at;
                positive length = 0;
                p8 name[64];
                p8 known = false;
                positive item;

                while (stop < used && text[stop] != '\n')
                        stop++;
                if (stop == at)
                {
                        at++;
                        continue;
                }

                while (at + length < stop && text[at + length] != ' ' &&
                       text[at + length] != '\t' && text[at + length] != '=')
                        length++;

                if (!length || length >= sizeof(name))
                {
                        check("bowl isolation lines have a keyword", false);
                        break;
                }

                memory_copy(name, text + at, length);
                name[length] = 0;
                for (item = 0; item < array_count(pacman7); item++)
                        if (string_equals(name, pacman7[item]))
                                known = true;
                check("every isolation directive is a Pacman 7 options keyword",
                      known);
                at = stop + (stop < used);
        }

        for (word = 0; word < array_count(words); word++)
        for (form = 0; form < 4; form++)
        {
                positive length = string_length(words[word]);
                p8 known = false;
                positive item;

                memory_fill(line, 0, sizeof(line));
                if (form == 0)
                        memory_copy(line, words[word], length + 1);
                else if (form == 1)
                {
                        memory_copy(line, words[word], length);
                        memory_copy(line + length, " = x", 5);
                }
                else if (form == 2)
                {
                        memory_copy(line, words[word], length);
                        memory_copy(line + length, "=\n", 3);
                }
                else
                {
                        line[0] = ' ';
                        memory_copy(line + 1, words[word], length + 1);
                }

                if (!bowl_isolation_keyword(line))
                        continue;
                for (item = 0; item < array_count(pacman7); item++)
                        if (string_equals(words[word], pacman7[item]))
                                known = true;
                check("every isolation directive is a Pacman 7 options keyword",
                      known);
        }
}

/* statfs reports the mount's flags, and read-only is the first of them. */
static p8 kernel_settings_read_only(string_address path)
{
        file_mount_facts facts;

        return system_call_2(syscall(statfs), (positive)path,
                             (positive)address_of facts) >= 0 &&
               (facts.flags & MS_RDONLY) != 0;
}

/* The same of a setting only some kernels have (/proc/sysrq-trigger needs
   CONFIG_MAGIC_SYSRQ): read-only where it is there, as the seal leaves it,
   and nothing to bind where it is not. */
static p8 kernel_settings_read_only_if_there(string_address path)
{
        file_mount_facts facts;
        bipolar asked = system_call_2(syscall(statfs), (positive)path,
                                      (positive)address_of facts);

        return asked == -ENOENT || (asked >= 0 && (facts.flags & MS_RDONLY) != 0);
}

/*
        The host kernel's settings stay the host's: sysfs is mounted read-only
        and /proc/sys, with the rest of its list, is bound read-only over
        itself. Made real in a user, mount, network and pid namespace of the
        check's own, where the calls bowl makes as root can be made by anyone;
        where no such namespace can be made -- a hardened host, or qemu-user,
        which is threaded -- the tables are what is checked.
*/
/* Root inside a user namespace just made, as the check's own user
   outside: without a mapping a file created on a tmpfs there has no owner
   the kernel can store, and its creation fails. */
static p8 namespace_root(positive user, positive group)
{
        static const struct { string_address path; p8 id; p8 of_group; } maps[] = {
            {"/proc/self/setgroups", false, false},
            {"/proc/self/gid_map", true, true},
            {"/proc/self/uid_map", true, false}};

        for (positive at = 0; at < array_count(maps); at++)
        {
                p8 line[64] = "deny\n";
                positive length = 5;
                bipolar handle = system_open_at(AT_FDCWD, maps[at].path,
                                                FILE_WRITE | O_CLOEXEC);

                if (maps[at].id)
                {
                        memory_copy(line, "0 ", 2);
                        length = 2 + positive_into_string(
                                         line + 2, maps[at].of_group ? group : user);
                        memory_copy(line + length, " 1\n", 3);
                        length += 3;
                }

                if (handle < 0 ||
                    system_write_all((positive)handle, line, length) != length)
                        return false;
                system_close(handle);
        }
        return true;
}

static fn kernel_settings(void)
{
        struct bowl_mount_point address_to sysfs = null;
        p8 trigger = false;

        for (positive at = 0; bowl_isolated_mounts[at].target; at++)
                if (string_equals(bowl_isolated_mounts[at].target, "/sys"))
                        sysfs = bowl_isolated_mounts + at;
        for (positive at = 0; bowl_kernel_settings[at]; at++)
                trigger |= string_equals(bowl_kernel_settings[at], "/proc/sysrq-trigger");

        check("Bowl mounts sysfs read-only",
              sysfs && (sysfs->flags & MS_RDONLY) != 0);
        check("Bowl binds /proc/sys and /proc/sysrq-trigger read-only",
              string_equals(bowl_kernel_settings[0], "/proc/sys") && trigger);
        check("but for /proc/sys/user, each user namespace's own, where bubblewrap's --disable-userns sets its new one's limit",
              string_equals(BOWL_KERNEL_SETTINGS_OWN, "user"));
        if (!sysfs)
                return;

        positive user = (positive)system_call(syscall(getuid));
        positive group = (positive)system_call(syscall(getgid));
        bipolar child = system_fork();

        if (child == 0)
        {
                if (system_call_1(syscall(unshare), CLONE_NEWUSER | CLONE_NEWNS |
                                                        CLONE_NEWNET | CLONE_NEWPID) < 0)
                        exit(77);

                if (!namespace_root(user, group))
                        exit(77);

                bipolar inside = system_fork();

                if (inside == 0)
                {
                        if (system_mount(0, "/", 0, MS_REC | MS_PRIVATE, 0) ||
                            system_mount("proc", "/proc", "proc",
                                         MS_NOSUID | MS_NOEXEC | MS_NODEV, 0) ||
                            system_mount(sysfs->source, sysfs->target,
                                         sysfs->filesystem, sysfs->flags, 0))
                                exit(77);

                        if (bowl_kernel_settings_seal())
                                exit(1);

                        b32 wrong = kernel_settings_read_only("/proc/sys/kernel") &&
                                            kernel_settings_read_only_if_there(
                                                "/proc/sysrq-trigger") &&
                                            kernel_settings_read_only("/sys/kernel") &&
                                            !kernel_settings_read_only("/proc/self")
                                        ? 0
                                        : 2;

                        /* The watch on a guest: a child that fills a 4 MiB
                           tmpfs, holds it a moment and takes it all back is
                           seen full; one that writes nothing is not. The
                           watcher asks by descriptor from a root that no
                           longer holds the path, as bowl is after its guest
                           pivots, and the writer fills through a descriptor
                           it inherited. */
                        if (system_mount("tmpfs", "/tmp", "tmpfs", 0, "size=4m"))
                                exit(wrong | 16);

                        bipolar watched = system_open_at(AT_FDCWD, "/tmp", BOWL_ROOM_OPEN);
                        bipolar fill = system_open_at_mode(AT_FDCWD, "/tmp/fill",
                                                           FILE_WRITE | O_CLOEXEC, 0600);

                        if (watched < 0 || fill < 0 ||
                            system_change_directory("/proc") ||
                            system_call_1(syscall(chroot), (positive)"."))
                                exit(wrong | 16);

                        for (positive filling = 0; filling < 2; filling++)
                        {
                                bipolar writer = system_fork();

                                if (writer == 0)
                                {
                                        static p8 block[65536];
                                        timespec hold = {0, 600000000};

                                        while (filling &&
                                               system_write_once((positive)fill, block,
                                                                 sizeof(block)) > 0)
                                                ;
                                        system_call_2(syscall(nanosleep),
                                                      (positive)address_of hold, 0);
                                        system_call_2(syscall(ftruncate), (positive)fill, 0);
                                        exit(1);
                                }

                                p64 lowest = writer > 0
                                                 ? bowl_wait_watching(writer, watched)
                                                 : 0;
                                positive ignored = 0;

                                if (writer > 0)
                                        system_wait4_retry(writer, address_of ignored, 0, null);
                                if (filling ? lowest >= BOWL_MEBIBYTE
                                            : lowest < BOWL_MEBIBYTE)
                                        wrong |= filling ? 4 : 8;
                        }

                        exit(wrong);
                }

                positive status = 0;

                if (inside < 0 ||
                    system_wait4_retry(inside, address_of status, 0, null) < 0)
                        exit(77);
                exit(wait_status_code(status));
        }

        positive status = 0;
        b32 code = child >= 0 &&
                           system_wait4_retry(child, address_of status, 0, null) >= 0
                       ? wait_status_code(status)
                       : 77;

        if (code == 77)
        {
                log_direct(str("bowl: the kernel's settings in a view NOT RUN -- "
                               "no user and mount namespace for the check\n"));
                return;
        }

        check("Bowl binds the host kernel's settings read-only in a view",
              code != 1 && !(code & 2));
        check("Bowl sees a filesystem a guest filled and emptied again",
              code != 1 && !(code & (4 | 16)));
        check("Bowl does not see a full filesystem where there was none",
              code != 1 && !(code & (8 | 16)));
}

/*
        Room for a bowl: /proc/meminfo's figures read as bytes, only at the
        start of a line and only under their own name, and a filesystem's room
        answered in bytes that fit inside it. A setup needing more than any
        filesystem holds is refused and one needing nothing is not.
*/
static fn room(void)
{
        static const struct { string_address text; string_address name; p64 bytes; } figures[] = {
            {"MemTotal:        2037804 kB\nMemAvailable:    1922780 kB\n", "MemAvailable:", 1922780 * (p64)1024},
            {"MemAvailable: 7 kB\nSwapFree: 12 kB\n", "SwapFree:", 12 * 1024},
            {"XMemAvailable: 9 kB\n", "MemAvailable:", 0},
            {"MemAvailable:\n", "MemAvailable:", 0},
            {"", "MemAvailable:", 0},
            {"Shmem: 4 kB\nMemAvailable: 1 kB", "MemAvailable:", 1024},
        };
        struct bowl_distro huge = {.label = "Huge"};
        bowl_room here;

        //      Made here as landing() makes it: a /tmp just emptied by a
        //      reboot has no root to measure, and a root that cannot be
        //      measured is let through.
        system_make_directory_at(AT_FDCWD, BOWL_ROOT_DIRECTORY, 0755);

        for (positive at = 0; at < array_count(figures); at++)
                check("Bowl reads a meminfo figure as bytes",
                      bowl_meminfo_bytes(figures[at].text, figures[at].name) ==
                          figures[at].bytes);

        check("Bowl measures the room a filesystem has",
              bowl_room_at("/", address_of here) && here.free <= here.total &&
                  (!here.in_memory || here.memory));
        check("Bowl refuses a setup no filesystem holds",
              bowl_room_short(address_of huge, (p64)1 << 62) == 1);
        check("Bowl lets a setup that needs nothing through",
              bowl_room_short(address_of huge, 0) == 0);
}

static fn archive_policy(void)
{
        p8 before = tar_extract_special;

        tar_extract_special = false;
        check("Bowl extraction rejects character devices",
              !tar_extract_type_allowed('3'));
        check("Bowl extraction rejects block devices",
              !tar_extract_type_allowed('4'));
        check("Bowl extraction rejects FIFOs",
              !tar_extract_type_allowed('6'));
        check("Bowl extraction keeps ordinary archive members",
              tar_extract_type_allowed('0') && tar_extract_type_allowed('1') &&
              tar_extract_type_allowed('2') && tar_extract_type_allowed('5'));
        tar_extract_special = before;
}

/*
        Landing a bootstrap whose tree sits one directory down -- Arch's
        root.x86_64 -- on real directories under the scratch root above.

        The hardening that made bowl_has resolve inside /bowls/NAME also
        refused every child of one, so flattening never found Arch's tree and
        every Arch setup said "archive is not a bowl bootstrap". Every check in
        this section was pure, which is how it got through. The two refusals
        after the landing are that hardening's own point and must survive the
        fix: a child that is a link out of the bowl, and a marker that is one.
*/
static fn landing_make(string_address root, string_address rest)
{
        p8 path[256];

        string_copy_bounded(path, root, sizeof path);
        string_append_bounded(path, rest, sizeof path);
        system_make_directory_at(AT_FDCWD, path, 0755);
}

static fn landing(void)
{
        p8 nested[128];
        p8 escape[128];
        p8 pointed[128];
        p8 path[256];
        p8 rel[258];
        p8 digits[24];
        positive pid = (positive)system_call_1(syscall(getpid), 0);
        bipolar made;

        digits[positive_into(digits, pid)] = end;
        system_make_directory_at(AT_FDCWD, BOWL_ROOT_DIRECTORY, 0755);
        string_copy_bounded(nested, BOWL_ROOT_PREFIX "nested", sizeof nested);
        string_append_bounded(nested, digits, sizeof nested);
        string_copy_bounded(escape, BOWL_ROOT_PREFIX "escape", sizeof escape);
        string_append_bounded(escape, digits, sizeof escape);
        string_copy_bounded(pointed, BOWL_ROOT_PREFIX "pointed", sizeof pointed);
        string_append_bounded(pointed, digits, sizeof pointed);

        landing_make(nested, "");
        landing_make(nested, "/root.x86_64");
        landing_make(nested, "/root.x86_64/usr");
        landing_make(nested, "/root.x86_64/usr/bin");
        string_copy_bounded(path, nested, sizeof path);
        string_append_bounded(path, "/root.x86_64/usr/bin/pacman", sizeof path);
        made = system_open_at_mode(AT_FDCWD, path,
                                   O_CREAT | O_WRONLY | O_CLOEXEC, 0755);
        if (made >= 0)
                system_close(made);

        check("Bowl finds a bootstrap one directory down",
              bowl_prefix_find(nested, "/usr/bin/pacman", rel) == 1 &&
                  string_equals(rel, "/root.x86_64"));
        check("and flattening it leaves the marker in the bowl's root",
              bowl_flatten(nested, "/usr/bin/pacman") == 0 &&
                  bowl_has(nested, "/usr/bin/pacman"));

        //      A child that is a link to the host's / would find the host's
        //      /bin/sh; it must not count as the bootstrap.
        landing_make(escape, "");
        string_copy_bounded(path, escape, sizeof path);
        string_append_bounded(path, "/root.x86_64", sizeof path);
        system_symbolic_link_at("/", AT_FDCWD, path);
        check("Bowl does not follow a child that links out of the bowl",
              bowl_prefix_find(escape, "/bin/sh", rel) == 0);

        //      A marker that is an absolute link resolves inside the child,
        //      where it names itself, so it is not there.
        landing_make(pointed, "");
        landing_make(pointed, "/root.x86_64");
        landing_make(pointed, "/root.x86_64/bin");
        string_copy_bounded(path, pointed, sizeof path);
        string_append_bounded(path, "/root.x86_64/bin/sh", sizeof path);
        system_symbolic_link_at("/bin/sh", AT_FDCWD, path);
        check("Bowl does not take a marker that links out of the child",
              bowl_prefix_find(pointed, "/bin/sh", rel) == 0);

        bowl_forget_path(nested);
        bowl_forget_path(escape);
        bowl_forget_path(pointed);
}

/*
        Fedora and Nix, which are not root tarballs: the rows that name them,
        the JSON of Fedora's real OCI layout, and both unpacked on real
        directories -- a two-layer image whose second layer whites out, and
        a Nix release whose directories tar must make itself.
*/
static fn unpack_path(p8 address_to into, string_address name,
                      string_address rest)
{
        p8 digits[24];

        digits[positive_into(digits, (positive)system_call_1(syscall(getpid), 0))] = end;
        string_copy_bounded(into, BOWL_ROOT_PREFIX, 256);
        string_append_bounded(into, name, 256);
        string_append_bounded(into, digits, 256);
        string_append_bounded(into, rest, 256);
}

static p8 unpack_write(string_address base, string_address rest,
                         string_address text, positive mode)
{
        p8 path[256];
        positive length = string_length(text);

        string_copy_bounded(path, base, sizeof path);
        string_append_bounded(path, rest, sizeof path);
        bipolar handle = system_open_at_mode(AT_FDCWD, path,
                                             O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC,
                                             mode);
        p8 wrote = handle >= 0 &&
                     system_write_all((positive)handle, text, length) == length;

        if (handle >= 0)
                system_close(handle);
        return wrote;
}

static fn unpack_directory(string_address base, string_address rest)
{
        p8 path[256];

        string_copy_bounded(path, base, sizeof path);
        string_append_bounded(path, rest, sizeof path);
        bowl_mkdir_parents(path);
}

static p8 unpack_says(string_address base, string_address rest,
                        string_address want)
{
        p8 path[256];
        p8 text[128];

        string_copy_bounded(path, base, sizeof path);
        string_append_bounded(path, rest, sizeof path);
        bipolar got = file_slurp(path, text, sizeof text - 1);

        if (got < 0)
                return false;
        text[got] = end;
        return string_equals(text, want);
}

static positive unpack_mode(string_address base, string_address rest)
{
        p8 path[256];
        file_facts facts;

        string_copy_bounded(path, base, sizeof path);
        string_append_bounded(path, rest, sizeof path);
        return file_look_link(path, address_of facts) ? facts.mode & 07777 : 0;
}

// Our own tar, in a child, as bowl runs it.
static p8 unpack_tar(string_address archive, string_address directory,
                       p8 gzip, string_address address_to members)
{
        string_address argv[16];
        positive n = 0;
        positive status = 0;

        argv[n++] = "tar";
        argv[n++] = "-c";
        if (gzip)
                argv[n++] = "-z";
        argv[n++] = "-f";
        argv[n++] = archive;
        argv[n++] = "-C";
        argv[n++] = directory;
        for (; *members && n + 1 < array_count(argv); members++)
                argv[n++] = *members;
        argv[n] = null;

        bipolar child = system_fork();

        if (child == 0)
        {
                program_arguments_use(argv, (b32)n);
                exit(file_tar());
        }
        return child > 0 &&
               system_wait4_retry(child, address_of status, 0, null) >= 0 &&
               !wait_status_code(status);
}

// A file moved into the layout under its own digest; its descriptor appended.
static p8 unpack_blob(string_address layout, string_address file,
                        string_address type, p8 address_to descriptor,
                        positive room, p8 address_to named)
{
        p8 hex[BOWL_DIGEST_HEX + 1];
        p8 to[256];
        p8 digits[24];
        p64 size = 0;
        bipolar handle = system_open_at(AT_FDCWD, file, FILE_READ | O_CLOEXEC);
        p8 hashed = handle >= 0 && bowl_sha256_of(handle, hex, address_of size);

        if (handle >= 0)
                system_close(handle);
        if (!hashed)
                return false;
        if (named)
                string_copy_bounded(named, hex, BOWL_DIGEST_HEX + 1);

        string_copy_bounded(to, layout, sizeof to);
        string_append_bounded(to, "/blobs/sha256/", sizeof to);
        string_append_bounded(to, hex, sizeof to);
        digits[positive_into(digits, (positive)size)] = end;

        string_append_bounded(descriptor, "{\"mediaType\":\"", room);
        string_append_bounded(descriptor, type, room);
        string_append_bounded(descriptor, "\",\"digest\":\"sha256:", room);
        string_append_bounded(descriptor, hex, room);
        string_append_bounded(descriptor, "\",\"size\":", room);
        string_append_bounded(descriptor, digits, room);
        string_append_bounded(descriptor, "}", room);
        return system_rename_at(AT_FDCWD, file, AT_FDCWD, to, 0) == 0;
}

static fn distros(void)
{
        for (positive at = 0; at < array_count(bowl_distros); at++)
        {
                const struct bowl_distro address_to row = bowl_distros + at;
                p8 isolated = true;

                for (string_address address_to program = row->expose;
                     program && *program; program++)
                        isolated &= bowl_needs_isolated(*program, null);

                check("Every setup row is a bowl with a marker, a URL and a manager",
                      bowl_named_root(row->root) && row->marker[0] == '/' &&
                          row->url &&
                          !string_compare_max(row->url, "https://", 8) &&
                          row->store && row->expose && row->expose[0] &&
                          row->archive_bytes && row->tree_bytes && row->next);
                check("A setup's manager is launched isolated", isolated);
                check("A pinned download is pinned by a whole SHA-256",
                      !row->sha256 ||
                          bowl_hex_digest(row->sha256, string_length(row->sha256)));
        }

        /*
                Each machine's table names its own builds and nobody else's,
                in the URL and in the name it keeps the download under. Arch
                Linux RISC-V spells itself archriscv, hence riscv alone.
        */
#if X64
        static string_address own[] = {"x86_64", "amd64", null};
        static string_address others[] = {"aarch64", "arm64", "riscv", null};
#elif ARM64
        static string_address own[] = {"aarch64", "arm64", null};
        static string_address others[] = {"x86_64", "amd64", "riscv", null};
#else
        static string_address own[] = {"riscv", null};
        static string_address others[] = {"x86_64", "amd64", "aarch64", "arm64", null};
#endif
        for (positive at = 0; at < array_count(bowl_distros); at++)
        {
                const struct bowl_distro address_to row = bowl_distros + at;
                p8 url_own = false;
                p8 store_own = false;
                p8 foreign = false;

                //      A release pinned by commit or by date names its build by
                //      the digest, which Debian's URL has no arch in to show.
                for (string_address address_to name = own; *name; name++)
                {
                        url_own |= string_find(row->url, *name) != null;
                        store_own |= string_find(row->store, *name) != null;
                }
                for (string_address address_to name = others; *name; name++)
                        foreign |= string_find(row->url, *name) != null ||
                                   string_find(row->store, *name) != null;
                check("Every setup downloads this machine's own build",
                      (url_own || row->sha256) && store_own && !foreign);
        }

        const struct bowl_distro address_to alpine = bowl_find_distro("alpine");
        const struct bowl_distro address_to fedora = bowl_find_distro("fedora");
        const struct bowl_distro address_to nix = bowl_find_distro("nix");

        check("Alpine's release is pinned on every machine",
              alpine && alpine->sha256);
        check("Fedora and Nix are set up, pinned, and unpacked their own way",
              fedora && nix && fedora->sha256 && nix->sha256 &&
                  fedora->unpack == bowl_extract_oci &&
                  nix->unpack == bowl_extract_nix &&
                  nix->prime == BOWL_PRIME_NIX);
}

/*
        Profiles are several bowls' worth of packages behind one name, so the
        table is checked for the mistakes that cost an install halfway through
        (a distribution with no manager words, a package that is an option, an
        argv that cannot hold the list), the words for what a person types,
        and the two things remove must never get wrong: it takes only the
        launchers that still name the bowl the profile put them in, and the
        session script it writes is a new file, never a write through a link.
*/
static fn profiles(void)
{
        const struct bowl_profile address_to profile = null;
        p8 remove = false;
        static string_address plain[] = {"bowl", "profile", "chrome", null};
        static string_address install[] = {"bowl", "profile", "chrome", "install", null};
        static string_address drop[] = {"bowl", "profile", "chrome", "remove", null};
        static string_address front[] = {"bowl", "profile", "remove", "chrome", null};
        static string_address odd[] = {"bowl", "profile", "chrome", "frob", null};
        static string_address none[] = {"bowl", "profile", "nosuch", null};
        static string_address both[] = {"bowl", "profile", "remove", "remove", null};
        static string_address many[] = {"bowl", "profile", "a", "b", "c", null};

        for (positive at = 0; at < array_count(bowl_profiles); at++)
        {
                const struct bowl_profile address_to row = bowl_profiles + at;
                p8 packages = true;
                //      A server (ssh) is started by Moonwater from its settings and has no session of its own to start.
                p8 session = row->libraries || (row->ssh && !row->session) || (row->session && row->session[0]);
                positive words = 0;

                for (const struct bowl_component address_to part = row->components;
                     part->distro; part++)
                {
                        const struct bowl_distro address_to distro =
                            bowl_find_distro(part->distro);
                        positive used = 0;

                        packages &= distro && distro->install && distro->remove &&
                                    part->packages && part->packages[0];
                        for (string_address address_to word =
                                 distro ? distro->install : null;
                             word && *word; word++)
                                used++;
                        for (string_address address_to word = part->packages;
                             word && *word; word++)
                        {
                                packages &= (*word)[0] && (*word)[0] != '-';
                                used++;
                        }
                        packages &= used + 1 <= BOWL_PROFILE_ARGV;
                        words++;
                }

                for (string_address address_to line = row->session;
                     line && *line; line++)
                        session &= !string_first_of(*line, '\n');

                check("A profile names a launcher no distribution or reserved name has",
                      bowl_name(row->name, true) && !bowl_find_distro(row->name) &&
                          !string_equals(row->name, "bin") && row->label && row->next);
                check("A profile's components are bowls with a manager and packages",
                      words && packages);
                check("A profile's session is lines of a shell script", session);
        }

        check("bowl profile NAME installs, and says which with an action",
              bowl_profile_words(3, plain, address_of profile, address_of remove) == 0 &&
                  profile && !remove &&
                  bowl_profile_words(4, install, address_of profile, address_of remove) == 0 &&
                  !remove);
        check("bowl profile NAME remove, and remove NAME, remove",
              bowl_profile_words(4, drop, address_of profile, address_of remove) == 0 &&
                  remove &&
                  bowl_profile_words(4, front, address_of profile, address_of remove) == 0 &&
                  remove && profile);
        check("An action nobody knows, a missing or extra word, are usage",
              bowl_profile_words(4, odd, address_of profile, address_of remove) ==
                      BOWL_PROFILE_WORDS_USAGE &&
                  bowl_profile_words(2, plain, address_of profile, address_of remove) ==
                      BOWL_PROFILE_WORDS_USAGE &&
                  bowl_profile_words(5, many, address_of profile, address_of remove) ==
                      BOWL_PROFILE_WORDS_USAGE);
        check("A profile nobody has is unknown, not usage",
              bowl_profile_words(3, none, address_of profile, address_of remove) ==
                      BOWL_PROFILE_WORDS_UNKNOWN &&
                  bowl_profile_words(4, both, address_of profile, address_of remove) ==
                      BOWL_PROFILE_WORDS_UNKNOWN);

        p8 header[96];
        check("The script's first lines name the profile that wrote it",
              bowl_profile_header(header, sizeof header, "chrome") &&
                  string_equals(header, "#!/bin/sh\n# bowl profile chrome\n") &&
                  !bowl_profile_header(header, 19, "chrome"));

        check("A name /bin has is taken, one it lacks is not",
              bowl_bin_taken("sh") && !bowl_bin_taken("moonwater-no-such-program"));

        {
                // No session of any profile writes into a bowl by the host's path: the host's root would follow a link the bowl's packages put there.
                b32 none_into = 1;

                for (positive p = 0; p < array_count(bowl_profiles); p++)
                        for (string_address address_to line = bowl_profiles[p].session; line && *line; line++)
                                if (strstr(*line, "> " BOWL_ROOT_PREFIX) || strstr(*line, ">" BOWL_ROOT_PREFIX) || strstr(*line, ">> " BOWL_ROOT_PREFIX) ||
                                    strstr(*line, "tee " BOWL_ROOT_PREFIX))
                                        none_into = 0;
                check("No profile's session writes a file into a bowl by the host's path (a link the bowl's packages put there is followed out of it)",
                      none_into);
        }

        check("A command is looked for in /sbin as well, where Alpine keeps udevd",
              string_equals(bowl_guest_bins[3], "/sbin"));

        /*
                The groups udev's rules name, added to a group file that has
                none of them, once, whole lines, and not to the line of another
                group that begins the same way.
        */
        {
                p8 groups[1024];
                positive used;

                string_copy_bounded(groups, "root:x:0:\nvideo:x:7:a\nvideos:x:9:\n", sizeof groups);
                used = bowl_groups_add(groups, string_length(groups), sizeof groups);
                groups[used] = end;
                check("Groups a file lacks are added after the ones it has",
                      !string_compare_max(groups, "root:x:0:\nvideo:x:7:a\nvideos:x:9:\n", 34) &&
                          string_has_prefix(groups + 34, "tty:x:5:\n") &&
                          strstr((const char *)groups, "input:x:104:\n") &&
                          strstr((const char *)groups, "render:x:105:\n"));
                check("A group the file has is not added again, nor changed",
                      !strstr((const char *)groups + 34, "video:x:") &&
                          strstr((const char *)groups, "video:x:7:a\n"));
                check("Adding to what is added changes nothing",
                      bowl_groups_add(groups, used, sizeof groups) == used);

                string_copy_bounded(groups, "root:x:0:", sizeof groups);
                used = bowl_groups_add(groups, string_length(groups), sizeof groups);
                check("A last line with no newline is ended before the groups follow",
                      string_has_prefix(groups, "root:x:0:\ntty:x:5:\n"));

                string_copy_bounded(groups, "root:x:0:\n", sizeof groups);
                used = bowl_groups_add(groups, string_length(groups), 40);
                groups[used] = end;
                check("A room too small for a line leaves that line out whole",
                      used <= 40 && groups[used - 1] == '\n' &&
                          string_has_prefix(groups, "root:x:0:\ntty:x:5:\n"));
        }

        /*
                Two bowls each hold a program. Marker lines name both, a name
                that climbs out of the directory, one nothing was exposed
                under and one too long to be a name. Only the first goes.
        */
        p8 first[256];
        p8 second[256];
        p8 marker[256];
        p8 long_name[BOWL_SHEBANG_LIMIT + 8];
        p8 text[BOWL_SHEBANG_LIMIT * 2];

        unpack_path(first, "profa", "");
        unpack_path(second, "profb", "");
        unpack_path(marker, "profa", BOWL_PROFILE_STATE "/marker");
        unpack_directory(first, "/usr/bin");
        unpack_directory(first, BOWL_PROFILE_STATE);
        unpack_directory(second, "/usr/bin");
        p8 made = unpack_write(first, "/usr/bin/tool", "#!/bin/sh\n", 0755) &&
                    unpack_write(second, "/usr/bin/tool", "#!/bin/sh\n", 0755) &&
                    bowl_expose_program(first, "/usr/bin/tool", "profatool", false) == 0 &&
                    bowl_expose_program(second, "/usr/bin/tool", "profbtool", false) == 0;
        memory_fill(long_name, 'x', sizeof long_name - 1);
        long_name[sizeof long_name - 1] = end;
        string_copy_bounded(text, "profatool\nprofbtool\n../profbtool\nprofnone\n", sizeof text);
        string_append_bounded(text, long_name, sizeof text);
        string_append_bounded(text, "\n", sizeof text);
        made &= unpack_write(first, BOWL_PROFILE_STATE "/marker", text, 0644);

        check("Two bowls' launchers stand before a remove", made);
        check("Remove reads a marker without error",
              bowl_profile_unexpose(first, "marker", marker) == 0);
        check("Remove takes a launcher that still names this bowl",
              system_access_at(AT_FDCWD, BOWL_EXPOSE_DIRECTORY "/profatool", 0) < 0);
        check("Remove leaves another bowl's launcher alone, named or climbed to",
              system_access_at(AT_FDCWD, BOWL_EXPOSE_DIRECTORY "/profbtool", 0) == 0);
        made = unpack_write(first, BOWL_PROFILE_STATE "/kept", "one\n", 0644);
        bipolar noted = bowl_profile_marker_open(first, "kept", FILE_APPEND);
        bowl_profile_note(noted, "two");
        if (noted >= 0)
                system_close(noted);
        check("A marker is added to by a second install, not started over",
              made && noted >= 0 && unpack_says(first, BOWL_PROFILE_STATE "/kept", "one\ntwo\n"));
        {
                //      A profile's generation: what the binary says it is (bowls, packages, steps), kept in the marker last.
                const struct bowl_profile address_to chrome_profile = bowl_find_profile("chrome");
                struct bowl_profile grown = *chrome_profile;
                struct bowl_profile retold = *chrome_profile;
                struct bowl_component grown_parts[2] = {chrome_profile->components[0], {null, null, 0, null}};
                string_address grown_packages[BOWL_PROFILE_ARGV];
                static string_address other_session[] = {"exec true", null};
                positive at = 0;
                p8 line[sizeof(BOWL_GENERATION_WORD) + 9];
                p32 now = bowl_profile_generation(chrome_profile);

                for (; chrome_profile->components[0].packages[at] && at + 2 < BOWL_PROFILE_ARGV; at++)
                        grown_packages[at] = chrome_profile->components[0].packages[at];
                grown_packages[at++] = "a-driver-the-image-added";
                grown_packages[at] = null;
                grown_parts[0].packages = grown_packages;
                grown.components = grown_parts;
                retold.session = other_session;
                check("A profile's generation is the same number each time it is asked, and another profile's is another",
                      now == bowl_profile_generation(chrome_profile) && now != bowl_profile_generation(bowl_find_profile("steam")));
                check("A package added to a profile's list is a new generation, and a session line changed is not",
                      bowl_profile_generation(&grown) != now && bowl_profile_generation(&retold) == now);
                bowl_generation_line(line, 0x0123abcdu);
                check("The generation line is the word and eight hex digits", string_equals(line, "generation 0123abcd\n"));
                made = unpack_write(first, BOWL_PROFILE_STATE "/gen", "package mesa held\nchrome\n", 0644);
                check("A marker from before generations (none in it) is behind: the bowl is brought up to date once",
                      made && !bowl_profile_generation_is(first, "gen", now));
                p8 lines[3 * sizeof(line)];
                p8 own[256];
                p8 own_state[256];

                //      A root of these rows' own, made whole here, so they hold whatever the rows above left.
                unpack_path(own, "profgen", "");
                unpack_path(own_state, "profgen", BOWL_PROFILE_STATE);
                (void)bowl_mkdir_parents(own_state);

                bowl_generation_line(line, now + 1);
                string_copy_bounded(lines, line, sizeof lines);
                bowl_generation_line(line, now);
                string_append_bounded(lines, line, sizeof lines);
                made = unpack_write(own, BOWL_PROFILE_STATE "/gen2", lines, 0644);
                bowl_generation_line(line, now + 1);
                string_append_bounded(lines, line, sizeof lines);
                made &= unpack_write(own, BOWL_PROFILE_STATE "/gen3", lines, 0644);
                check("The last generation in the marker is the one it is at, not an earlier one",
                      made && bowl_profile_generation_is(own, "gen2", now) && !bowl_profile_generation_is(own, "gen2", now + 1) &&
                          bowl_profile_generation_is(own, "gen3", now + 1) && !bowl_profile_generation_is(own, "gen3", now));
                check("A marker that is not there is no generation", !bowl_profile_generation_is(first, "no-such-marker", now));
        }
        p8 aimed[256];
        p8 pointer[256];
        unpack_path(aimed, "profa", "/aimed");
        unpack_path(pointer, "profa", BOWL_PROFILE_STATE "/pointer");
        made = unpack_write(first, "/aimed", "keep\n", 0644) &&
               system_symbolic_link_at(aimed, AT_FDCWD, pointer) == 0;
        bipolar through = bowl_profile_marker_open(first, "pointer", FILE_APPEND);
        if (through >= 0)
        {
                bowl_profile_note(through, "evil");
                system_close(through);
        }
        check("A marker the bowl made a link is not written through",
              made && through < 0 && unpack_says(first, "/aimed", "keep\n"));
        check("A marker that is missing is a profile that exposed nothing",
              bowl_profile_unexpose(first, "no-such-marker", marker) == 0);

        /*
                The directory of the markers made a link by the guest, to a
                directory of the host's (absolute, which the host would follow):
                a marker is not made there, not read from there and not removed
                from there, and a FIFO for a marker holds nothing. A package's
                script, as the guest's root, can make that link between an
                install's first look and its last write.
        */
        {
                p8 guest[256];
                p8 host_side[256];
                p8 state[256];

                unpack_path(guest, "profg", "");
                unpack_path(host_side, "profh", "");
                unpack_path(state, "profg", BOWL_PROFILE_STATE);
                unpack_directory(guest, "/var/lib/moonwater");
                unpack_directory(host_side, "");
                unpack_directory(guest, "/usr/bin");
                made = unpack_write(host_side, "/desktop", "profgtool\n", 0644) &&
                       unpack_write(guest, "/usr/bin/tool", "#!/bin/sh\n", 0755) &&
                       bowl_expose_program(guest, "/usr/bin/tool", "profgtool", false) == 0 &&
                       system_symbolic_link_at(host_side, AT_FDCWD, state) == 0;
                bipolar opened = bowl_profile_marker_open(guest, "fresh", FILE_APPEND);

                if (opened >= 0)
                        system_close(opened);
                check("A link the guest made of the markers' directory is not followed out of the bowl to make one",
                      made && opened < 0 && !unpack_says(host_side, "/fresh", ""));
                check("nor to read one, and the launcher a host file names stays",
                      bowl_profile_unexpose(guest, "desktop", state) == 0 &&
                          system_access_at(AT_FDCWD, BOWL_EXPOSE_DIRECTORY "/profgtool", 0) == 0);
                bowl_profile_forget(guest, "desktop");
                check("nor to remove one", unpack_says(host_side, "/desktop", "profgtool\n"));
                bowl_profile_drop(guest, "profgtool");

                system_remove_at(AT_FDCWD, state, 0);
                unpack_directory(guest, BOWL_PROFILE_STATE);
                string_append_bounded(state, "/desktop", sizeof state);
                made = system_call_4(syscall(mknodat), (positive)(bipolar)AT_FDCWD,
                                     (positive)state, 0010644, 0) == 0;
                check("A FIFO for a marker holds neither a remove nor an install",
                      made && bowl_profile_unexpose(guest, "desktop", state) == 0 &&
                          bowl_profile_marker_open(guest, "desktop", FILE_APPEND) < 0);
                bowl_forget_path(guest);
                bowl_forget_path(host_side);
        }

        /*
                The script is written where a link may have been planted: the
                link goes and the file it pointed at is not written to.
        */
        static string_address session[] = {"exec true", null};
        static const struct bowl_component part[] = {{null, null}};
        struct bowl_profile planted = {"profplant", "Planted", "next", part, session};
        p8 decoy[256];
        p8 link[256];

        unpack_path(decoy, "profdecoy", "");
        string_copy_bounded(link, BOWL_EXPOSE_DIRECTORY "/profplant", sizeof link);
        system_remove_at(AT_FDCWD, link, 0);
        made = unpack_write(decoy, "", "decoy\n", 0644) &&
               system_symbolic_link_at(decoy, AT_FDCWD, link) == 0;
        check("A link stands at the script's name before it is written", made);
        check("Writing the script succeeds over a planted link",
              bowl_profile_script(address_of planted) == 0);
        check("The planted link's target was not written to",
              unpack_says(decoy, "", "decoy\n"));
        check("The script is a new executable file that runs the session the bowl holds",
              unpack_says("", BOWL_EXPOSE_DIRECTORY "/profplant",
                          "#!/bin/sh\n# bowl profile profplant\n"
                          "exec bowl profile profplant session \"$@\"\n") &&
                  (unpack_mode("", BOWL_EXPOSE_DIRECTORY "/profplant") & 0111));

        system_remove_at(AT_FDCWD, link, 0);
        system_remove_at(AT_FDCWD, decoy, 0);
        system_remove_at(AT_FDCWD, BOWL_EXPOSE_DIRECTORY "/profbtool", 0);
        system_remove_at(AT_FDCWD, BOWL_EXPOSE_DIRECTORY "/profatool", 0);
        bowl_forget_path(first);
        bowl_forget_path(second);
}

/*
        The browser profile and what it needed of the machinery under it.
        A profile that is a program on Canvas (and not a session) must be
        what it says: Alpine's chromium with the graphics libraries, no
        display server or session in any package it names, no program of the
        bowl exposed beside its own launcher, a launcher that refuses to
        start a browser that cannot show and starts it on Wayland with no
        flag against the GPU; the names a component exposes and the names a
        session answers to are only what they say, a remove takes what an
        install wrote and no more, and a package another installed profile
        holds is left to it.
*/
static p8 chrome_has(string_address address_to list, string_address word)
{
        for (; list && *list; list++)
                if (string_equals(*list, word))
                        return true;
        return false;
}

static positive chrome_count(string_address text, string_address piece)
{
        positive found = 0;
        positive length = string_length(piece);

        for (positive at = 0; text[at]; at++)
                found += !string_compare_max(text + at, piece, length);
        return found;
}

static positive chrome_line(string_address address_to lines, string_address piece)
{
        for (positive at = 0; lines && lines[at]; at++)
                if (strstr(lines[at], piece))
                        return at + 1;
        return 0;
}

/*
        The [multilib] section: the bootstrap's pacman.conf has it commented out, and an Arch
        bowl on x86-64 gets the block once. A file that has the section uncommented, or the
        block, is not written again; a commented one or another section's name is not the
        section.
*/
static fn multilib_rows(void)
{
        static string_address stock = "[options]\nArchitecture = auto\n#[multilib]\n#Include = /etc/pacman.d/mirrorlist\n";
        static string_address named = "[options]\n[multilib]\nInclude = /etc/pacman.d/mirrorlist\n";
        static string_address other = "[options]\n[multilibx]\n# [multilib]\n";
        static string_address marked = "[options]\n" BOWL_MULTILIB_BLOCK;
        static string_address unended = "[options]\nCheckSpace";
        static string_address ended = "[options]\nCheckSpace\n[multilib]\n";
        p8 joined[512];
        positive at = string_length(stock);

        check("A stock pacman.conf, with [multilib] commented out, has no section yet",
              !bowl_multilib_has(stock, string_length(stock)));
        check("A section named and not commented is the section, and is not written again",
              bowl_multilib_has(named, string_length(named)));
        check("A commented section, or another section's name, is not the section",
              !bowl_multilib_has(other, string_length(other)));
        check("The block this writes is recognised, and the marker alone is too",
              bowl_multilib_has(marked, string_length(marked)) &&
                  string_find(BOWL_MULTILIB_BLOCK, "[multilib]\n"));
        memory_copy(joined, stock, at);
        memory_copy(joined + at, BOWL_MULTILIB_BLOCK, string_length(BOWL_MULTILIB_BLOCK));
        joined[at + string_length(BOWL_MULTILIB_BLOCK)] = end;
        check("A file with the block appended to it is not written a second time",
              bowl_multilib_has(joined, string_length(joined)));
        check("A file that does not end in a newline is no section, and the section after its newline is",
              !bowl_multilib_has(unended, string_length(unended)) &&
                  bowl_multilib_has(ended, string_length(ended)));
}

/*
        What an install records of its packages, and what a remove reads back: a local
        database entry names its package by the last two dashes, and a marker line
        "package NAME added" is one a remove takes away, "held" one it leaves. A line
        with no package word is a launcher's name, and is not one of these.
*/
static fn record_rows(void)
{
        static string_address listed[] = {"mesa", "libpulse", "lib32-mesa", null};
        p8 added[BOWL_PROFILE_ARGV] = {false};
        p8 recorded = false;
        p8 grants = bowl_profile_recorded_line("package mesa added", listed, added, address_of recorded);
        p8 held = bowl_profile_recorded_line("package libpulse held", listed, added, address_of recorded);
        p8 named = bowl_profile_recorded_line("package lib32-mesa added", listed, added, address_of recorded);
        p8 launcher_recorded = false;
        p8 launcher_line = bowl_profile_recorded_line("steam", listed, added, address_of launcher_recorded);

        check("A local database entry is its package by name: lib32-mesa is not mesa, mesa is mesa, and no version is no package",
              bowl_entry_is("lib32-mesa-26.0-1", "lib32-mesa") && !bowl_entry_is("lib32-mesa-26.0-1", "mesa") &&
                  bowl_entry_is("mesa-26.0-1", "mesa") && !bowl_entry_is("mesa-libs-26.0-1", "mesa") &&
                  !bowl_entry_is("mesa", "mesa"));
        check("A marker's package line is not a launcher's name, which bowl_name refuses",
              !bowl_name("package mesa added", true));
        check("A package line marks the packages the install added, and the bowl's own are left unmarked",
              grants && held && named && recorded && added[0] && !added[1] && added[2]);
        check("A launcher's name is no package line and marks nothing",
              !launcher_line && !launcher_recorded);
}

/*
        The build profile: what build.sh and the kernel's make ask for, the
        session that runs build.sh in the bowl as root, and the triplet's links
        made and taken away on real files (bowl_tools_at), under a root of this
        process's own so the gcc and clang lanes do not share one.
*/
static fn build_profile(void)
{
        const struct bowl_profile address_to build = bowl_find_profile("build");
        const struct bowl_component address_to part = build ? build->components : null;
        string_address address_to packages = part ? part->packages : null;
        //      build.c's "required" setting, and the packages that hold each: gpg is gnupg's, rustc rust's.
        static string_address required[] = {"bison", "flex", "bc", "gnupg", "make", "gcc", "clang", "rust", null};
        //      What the kernel's make, kernel/patch/apply and kernel/firmware/apply run, and the handoff's copy.
        static string_address asked[] = {"perl", "python3", "elfutils-dev", "openssl-dev", "zstd", "xz", "git",
                                          "curl", "patch", "gawk", "sudo", "rsync", "musl-dev", "binutils", null};
        p8 all = true;
        p8 unique = true;

        check("The build profile has one component, Alpine's, and no libraries",
              build && !build->libraries && build->session && part &&
                  string_equals(part->distro, "alpine") && !(part + 1)->distro);
        for (string_address address_to name = required; *name; name++)
                all &= chrome_has(packages, *name);
        check("The build profile holds every program build.c requires before it starts", all);
        all = true;
        for (string_address address_to name = asked; *name; name++)
                all &= chrome_has(packages, *name);
        check("The build profile holds what the kernel's make, its patch and firmware scripts and the handoff run", all);
        for (positive at = 0; packages && packages[at]; at++)
                for (positive other = at + 1; packages[other]; other++)
                        unique &= !string_equals(packages[at], packages[other]);
        check("No package the build profile names is named twice", unique);
        check("The build component exposes no program of the bowl by name: its session is the way in",
              part && part->expose && !part->expose[0] && build && !build->names);

        positive found = chrome_line(build ? build->session : null, "-f build.sh");
        positive run = chrome_line(build ? build->session : null,
                                   "exec bowl --root " BOWL_ROOT_PREFIX "alpine /bin/sh build.sh \"$@\"");
        check("The session refuses a directory with no build.sh, then runs build.sh in the Alpine bowl as root",
              found == 1 && run == 2 && build && !build->session[2]);

#if X64
        check("The build profile's triplet is the one arch/x64 names its compiler by",
              build && string_equals(build->tool_prefix, "x86_64-linux-gnu-"));
#elif ARM64
        check("The build profile's triplet is the one arch/arm names its compiler by",
              build && string_equals(build->tool_prefix, "aarch64-linux-gnu-"));
#else
        check("The build profile's triplet is the one arch/riscv names its compiler by",
              build && string_equals(build->tool_prefix, "riscv64-linux-gnu-"));
#endif
        check("The triplet's tools are the compiler and every binutil the kernel's make takes from CROSS_COMPILE",
              build && chrome_has(build->tools, "gcc") && chrome_has(build->tools, "ld") &&
                  chrome_has(build->tools, "as") && chrome_has(build->tools, "objcopy") &&
                  chrome_has(build->tools, "nm") && chrome_has(build->tools, "ar") &&
                  chrome_has(build->tools, "strip") && chrome_has(build->tools, "readelf") &&
                  chrome_has(build->tools, "objdump"));

        p8 root[256];
        p8 out[256];
        p8 path[256];
        p8 have[256];
        static string_address tools[] = {"gcc", "ld", "nm", null};
        bipolar got;

        unpack_path(root, "tools", "");
        unpack_path(out, "toolsout", "");
        bowl_forget_path(root);
        bowl_forget_path(out);
        landing_make(root, "");
        landing_make(root, "/usr");
        landing_make(root, "/usr/bin");
        unpack_write(root, "/usr/bin/gcc", "#!/bin/sh\n", 0755);
        unpack_write(root, "/usr/bin/ld", "#!/bin/sh\n", 0755);
        landing_make(root, "/usr/local");
        landing_make(root, "/usr/local/bin");
        string_copy_bounded(path, root, sizeof path);
        string_append_bounded(path, "/usr/local/bin/t-ld", sizeof path);
        system_symbolic_link_at("/opt/ld", AT_FDCWD, path);

        check("Install links the triplet's name of each tool the bowl has, and leaves a name somebody else has",
              bowl_tools_at(root, "t-", tools, true) == 1);
        string_copy_bounded(path, root, sizeof path);
        string_append_bounded(path, "/usr/local/bin/t-gcc", sizeof path);
        got = system_read_link_at(AT_FDCWD, path, have, sizeof have - 1);
        have[got > 0 ? got : 0] = end;
        check("The link names the plain tool by its path in the bowl", got > 0 && string_equals(have, "/usr/bin/gcc"));
        string_copy_bounded(path, root, sizeof path);
        string_append_bounded(path, "/usr/local/bin/t-nm", sizeof path);
        check("A tool the bowl lacks gets no link", system_read_link_at(AT_FDCWD, path, have, sizeof have) < 0);
        check("A second install makes nothing again", bowl_tools_at(root, "t-", tools, true) == 0);
        check("Remove takes the link install made, and not one it did not make",
              bowl_tools_at(root, "t-", tools, false) == 1);
        string_copy_bounded(path, root, sizeof path);
        string_append_bounded(path, "/usr/local/bin/t-ld", sizeof path);
        got = system_read_link_at(AT_FDCWD, path, have, sizeof have - 1);
        have[got > 0 ? got : 0] = end;
        check("The link somebody else made is still theirs", got > 0 && string_equals(have, "/opt/ld"));

        //      The bowl's /bin and /sbin before Moonwater's /usr: a name its /usr/bin has is found there first and gets none.
        check("The build profile puts the bowl's /bin and /sbin first in root's PATH", build && build->bins_first);
        landing_make(root, "/bin");
        landing_make(root, "/sbin");
        unpack_write(root, "/bin/sed", "#!/bin/sh\n", 0755);
        unpack_write(root, "/bin/ld", "#!/bin/sh\n", 0755);
        unpack_write(root, "/sbin/depmod", "#!/bin/sh\n", 0755);
        check("Install links each program of /bin and /sbin that /usr has not",
              bowl_bins_at(root, true) == 2);
        string_copy_bounded(path, root, sizeof path);
        string_append_bounded(path, "/usr/local/sbin/depmod", sizeof path);
        got = system_read_link_at(AT_FDCWD, path, have, sizeof have - 1);
        have[got > 0 ? got : 0] = end;
        check("An /sbin program's link is in /usr/local/sbin and names it", got > 0 && string_equals(have, "/sbin/depmod"));
        string_copy_bounded(path, root, sizeof path);
        string_append_bounded(path, "/usr/local/bin/ld", sizeof path);
        check("A /bin program /usr/bin has as well gets no link", system_read_link_at(AT_FDCWD, path, have, sizeof have) < 0);
        check("Remove takes those links away again", bowl_bins_at(root, false) == 2 && bowl_bins_at(root, true) == 2 &&
                                                         bowl_bins_at(root, false) == 2);

        //      A /usr/local that is a link out of the bowl: the directory is opened inside it, so nothing lands outside.
        landing_make(out, "");
        landing_make(out, "/bin");
        string_copy_bounded(path, root, sizeof path);
        string_append_bounded(path, "/usr/local", sizeof path);
        bowl_forget_path(path);
        system_symbolic_link_at(out, AT_FDCWD, path);
        bowl_tools_at(root, "t-", tools, true);
        string_copy_bounded(path, out, sizeof path);
        string_append_bounded(path, "/bin/t-gcc", sizeof path);
        check("A /usr/local that links out of the bowl is not followed",
              system_read_link_at(AT_FDCWD, path, have, sizeof have) < 0);

        bowl_forget_path(out);
        bowl_forget_path(root);
}

/*
        The pads a bowl the grants name is lent (bowl_pad_keeper): a sysfs bitmap read into bytes the way the kernel writes
        it, a HID report descriptor's top-level collections, the fields of a uevent, and the Steam session's SDL words.
*/
/*
        The flatpak profile: Arch's flatpak, Flathub added and its AppStream fetched after the packages (each step where the package
        manager runs), and a session under flatpak's own name that hands every verb but run to flatpak as it is, and starts an app
        on the desktop: its sockets, the system bus, Flatpak's portal and session helper, and the rootless X server.
*/
static fn flatpak_profile(void)
{
        const struct bowl_profile address_to flatpak = bowl_find_profile("flatpak");
        const struct bowl_component address_to part = flatpak ? flatpak->components : null;
        string_address address_to session = flatpak ? flatpak->session : null;
        static string_address daemons[] = {"pipewire", "wireplumber", "systemd", "xorg-server", "networkmanager", "gamemode", null};
        p8 named = true, isolated = true, absolute = true;

        check("The flatpak profile has one component, Arch's, with a session and no libraries; it holds the X server's wrapper",
              flatpak && !flatpak->libraries && session && part && string_equals(part->distro, "arch") && !(part + 1)->distro &&
                  flatpak->xwayland && !flatpak->names && !flatpak->overlay_gl && !flatpak->pwrite_shim);
        check("The flatpak component names flatpak, Xwayland and Python for the X server's wrapper, and dbus for the system bus",
              part && chrome_has(part->packages, "flatpak") && chrome_has(part->packages, "xorg-xwayland") &&
                  chrome_has(part->packages, "python") && chrome_has(part->packages, "dbus"));
        for (string_address address_to name = daemons; *name; name++)
                named &= !chrome_has(part ? part->packages : null, *name);
        check("No package the flatpak profile names is a daemon, a display server or a seat Moonwater does not run", named);
        check("The flatpak component exposes no program by name: the session is the way in", part && part->expose && !part->expose[0]);

        // The steps after the packages: Flathub added (once, from its .flatpakrepo over https) and its AppStream fetched, each a
        // flatpak verb that changes the installation, so it runs where the package manager does.
        check("The flatpak profile's steps after its packages add Flathub and fetch its AppStream, in that order",
              part && part->after && part->after[0] && part->after[1] && !part->after[2] &&
                  chrome_has(part->after[0], "remote-add") && chrome_has(part->after[0], "--if-not-exists") &&
                  chrome_has(part->after[0], "flathub") && chrome_has(part->after[0], "https://dl.flathub.org/repo/flathub.flatpakrepo") &&
                  chrome_has(part->after[1], "--appstream") && chrome_has(part->after[1], "flathub"));
        for (positive step = 0; part && part->after && part->after[step]; step++)
        {
                isolated &= bowl_needs_isolated(part->after[step][0], part->after[step]);
                absolute &= part->after[step][0][0] == '/';
        }
        check("Every step after the packages is an absolute program run where the package manager runs", isolated && absolute);
        {
                struct bowl_component bare[2];
                struct bowl_profile plain;

                if (flatpak && part)
                {
                        plain = *flatpak, bare[0] = *part, bare[0].after = null, memory_zero(&bare[1], sizeof bare[1]), plain.components = bare;
                        check("The steps after the packages are part of the profile's generation: a change to them installs again",
                              bowl_profile_generation(&plain) != bowl_profile_generation(flatpak));
                }
        }
        {
                static string_address run[] = {"flatpak", "run", "com.discordapp.Discord", null};
                static string_address flagged[] = {"flatpak", "--verbose", "install", "flathub", "org.gimp.GIMP", null};
                static string_address list[] = {"flatpak", "list", null};
                static string_address update[] = {"flatpak", "update", "-y", null};
                check("flatpak's run and list stay in a session's view, and install and update (after an option too) run isolated",
                      !bowl_needs_isolated("/usr/bin/flatpak", run) && !bowl_needs_isolated("/usr/bin/flatpak", list) &&
                          bowl_needs_isolated("/usr/bin/flatpak", flagged) && bowl_needs_isolated("/usr/bin/flatpak", update));
        }

        positive verb = chrome_line(session, "verb=; for word in \"$@\"");
        positive through = chrome_line(session, "[ \"$verb\" = run ] || exec bowl " BOWL_ROOT_PREFIX "arch /usr/bin/flatpak \"$@\"");
        positive socket = chrome_line(session, "[ -S /run/moonwater/wayland-0 ] ||");
        positive exported = chrome_line(session, "export WAYLAND_DISPLAY=/run/moonwater/wayland-0");
        positive sound = chrome_line(session, "export PULSE_SERVER=unix:/run/moonwater/pulse/native");
        positive bus = chrome_line(session, "bowl bus >/dev/null 2>&1");
        positive portal = chrome_line(session, "running flatpak-portal || { bowl " BOWL_ROOT_PREFIX "arch /usr/lib/flatpak-portal ");
        positive helper = chrome_line(session, "running flatpak-session || { bowl " BOWL_ROOT_PREFIX "arch /usr/lib/flatpak-session-helper ");
        positive wm = chrome_line(session, "if [ -S /run/moonwater/xwm/wm-0 ]; then");
        positive wrapped = chrome_line(session, "bowl " BOWL_ROOT_PREFIX "arch /usr/local/bin/xwayland-moonwater /usr/bin/flatpak \"$@\"");
        positive fallback = chrome_line(session, "[ $status -ne 125 ] && exit $status");
        positive last = 0;

        while (session && session[last + 1])
                last++;
        check("Every verb but run is flatpak's as it is, before the session looks for the desktop (an install needs no Canvas)",
              verb && through == verb + 1 && socket > through);
        check("A run names the desktop's sockets, marks the launch as the person's and clears the bowl's capabilities (bubblewrap refuses any)",
              exported > socket && strstr(session[exported - 1], "MOONWATER_ASKED=1") && strstr(session[exported - 1], "BOWL_CAPABILITIES=none") &&
                  sound > socket && bus > socket);
        check("A run starts Flatpak's portal and session helper on the bus when none runs, under the flatpak-portal switch, before the app",
              portal && helper && portal < wrapped && helper < wrapped && strstr(session[portal - 1], "wa flatpak-portal && ") &&
                  strstr(session[helper - 1], "wa flatpak-portal && ") && chrome_line(session, "running(){ for comm in /proc/[0-9]*/comm;"));
        positive share = chrome_line(session, "export BOWL_SHARE=flatpak");
        check("The portal and the app's run share the flatpak namespace; the helper, which runs what any client asks, is started before it is named",
              share && helper < share && portal > share && wrapped > share && !strstr(session[helper - 1], "BOWL_SHARE"));
        {
                static string_address named[] = {"PATH=/bin", "BOWL_SHARE=flatpak", null};
                static string_address upper[] = {"BOWL_SHARE=Flatpak", null};
                static string_address dotted[] = {"BOWL_SHARE=../x", null};
                static string_address empty[] = {"BOWL_SHARE=", null};
                static string_address none[] = {"PATH=/bin", null};
                static string_address longest[] = {"BOWL_SHARE=abcdefghijklmnopqrstuvwxyz012345", null};
                static string_address longer[] = {"BOWL_SHARE=abcdefghijklmnopqrstuvwxyz0123456", null};
                check("A share is named by letters, digits and dashes, at most 32 bytes; anything else, or none, is no share",
                      bowl_share_name(named) && string_equals(bowl_share_name(named), "flatpak") && !bowl_share_name(upper) &&
                          !bowl_share_name(dotted) && !bowl_share_name(empty) && !bowl_share_name(none) && bowl_share_name(longest) &&
                          !bowl_share_name(longer) && !bowl_share_name(null));
        }
        {
                // A share's pin must be a user namespace whose parent is this process's own: this process's own namespace is not (its
                // parent is not itself), nor is a file that is none, nor another kind of namespace.
                bipolar self = system_open_at(AT_FDCWD, "/proc/self/ns/user", FILE_READ | O_CLOEXEC);
                bipolar mounts = system_open_at(AT_FDCWD, "/proc/self/ns/mnt", FILE_READ | O_CLOEXEC);
                bipolar plain = system_open_at(AT_FDCWD, "/proc/self/status", FILE_READ | O_CLOEXEC);
                check("Only a user namespace whose parent is this one's is taken for a share's pin",
                      self >= 0 && !bowl_share_is(self, 0) && mounts >= 0 && !bowl_share_is(mounts, 0) && plain >= 0 &&
                          !bowl_share_is(plain, 0));
                if (self >= 0)
                        system_close(self);
                if (mounts >= 0)
                        system_close(mounts);
                if (plain >= 0)
                        system_close(plain);
        }
        {
                // A namespace a child of this process unshared is one: of this owner, and of no other.
                b32 ready[2];
                p8 byte = 0, path[48];
                positive status = 0, length;
                bipolar child, held = -1;

                b32 go[2] = {-1, -1};

                if (system_call_2(syscall(pipe2), (positive)ready, O_CLOEXEC) == 0 &&
                    system_call_2(syscall(pipe2), (positive)go, O_CLOEXEC) == 0)
                {
                        child = system_fork();
                        if (child == 0)
                        {
                                system_close(ready[0]), system_close(go[1]);
                                if (system_call_1(syscall(unshare), CLONE_NEWUSER) == 0)
                                        (void)system_write_once(ready[1], "x", 1), (void)system_read_once(go[0], &byte, 1);
                                exit(0);
                        }
                        system_close(ready[1]), system_close(go[0]);
                        if (child > 0 && system_read_once(ready[0], &byte, 1) == 1)
                        {
                                length = string_copy_end(path, "/proc/") - path;
                                length += positive_into_string(path + length, (positive)child);
                                string_copy_end(path + length, "/ns/user");
                                held = system_open_at(AT_FDCWD, path, FILE_READ | O_CLOEXEC);
                        }
                        system_close(go[1]); // the child's read ends, and the child with it
                        if (child > 0)
                                system_wait4_retry(child, &status, 0, null);
                        system_close(ready[0]);
                }
                if (held >= 0)
                {
                        positive owner = (positive)system_call(syscall(geteuid));
                        check("A user namespace a child made from here is a share's pin of its owner, and of no other",
                              bowl_share_is(held, owner) && !bowl_share_is(held, owner + 4242));
                        system_close(held);
                }
                else
                        log_direct(str("bowl: a share's pin NOT RUN -- no user namespace could be made here\n"));
        }
        check("The helper is looked for by the name the kernel keeps of it (its first 15 bytes)",
              string_length("flatpak-session") == 15 && !string_compare_max("flatpak-session-helper", "flatpak-session", 15));
        check("A run is under the rootless X server where Canvas has the window manager's socket, and on Wayland alone where it did not start",
              wm && wrapped == wm + 1 && fallback > wrapped && session && string_equals(session[last], "exec bowl " BOWL_ROOT_PREFIX "arch /usr/bin/flatpak \"$@\""));
        check("A Flatpak app keeps its GSettings and its runtime's Vulkan drivers: the session sets neither",
              !chrome_line(session, "GSETTINGS_BACKEND") && !chrome_line(session, "VK_DRIVER_FILES"));
#if X64 || ARM64
        check("Flathub has apps for this architecture: the flatpak profile can be installed", flatpak && !flatpak->unavailable);
#else
        check("Flathub has no apps for this architecture: the flatpak profile says so", flatpak && flatpak->unavailable);
#endif

        // A Flatpak app's exported entry is uninstalled as the app (bowl uninstall), and only an entry of that shape is.
        p8 root[BOWL_PATH_LIMIT], id[256];
        check("An exported Flatpak entry is the app of its bowl's installation",
              bowl_flatpak_entry(BOWL_ROOT_PREFIX "arch/var/lib/flatpak/exports/share/applications/com.discordapp.Discord.desktop", root,
                                 sizeof root, id, sizeof id) &&
                  string_equals(root, BOWL_ROOT_PREFIX "arch") && string_equals(id, "com.discordapp.Discord"));
        check("Nothing else is: another folder, an id with a path's step or a leading dash, a root that is no bowl, no .desktop",
              !bowl_flatpak_entry(BOWL_ROOT_PREFIX "arch/usr/share/applications/com.discordapp.Discord.desktop", root, sizeof root, id, sizeof id) &&
                  !bowl_flatpak_entry(BOWL_ROOT_PREFIX "arch/var/lib/flatpak/exports/share/applications/a/b.desktop", root, sizeof root, id, sizeof id) &&
                  !bowl_flatpak_entry(BOWL_ROOT_PREFIX "arch/var/lib/flatpak/exports/share/applications/-y.desktop", root, sizeof root, id, sizeof id) &&
                  !bowl_flatpak_entry(BOWL_ROOT_PREFIX "bin/var/lib/flatpak/exports/share/applications/x.desktop", root, sizeof root, id, sizeof id) &&
                  !bowl_flatpak_entry("/root/var/lib/flatpak/exports/share/applications/x.desktop", root, sizeof root, id, sizeof id) &&
                  !bowl_flatpak_entry(BOWL_ROOT_PREFIX "arch/var/lib/flatpak/exports/share/applications/x.desktopx", root, sizeof root, id, sizeof id));
}

static fn pad_rows(void)
{
        p8 bytes[96];
        //      capabilities/key of a pad: BTN_SOUTH (0x130) and up in the fifth word, the leading words zero and left out.
        p8 pad_keys[] = "7fdb000000000000 0 0 0 0\n";
        p8 one[] = "40\n";
        p8 none[] = "\n";
        static const p8 dualsense[] = {0x05, 0x01, 0x09, 0x05, 0xa1, 0x01, 0x85, 0x01, 0x09, 0x30, 0x09, 0x31,
                                       0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x02, 0x81, 0x02, 0xc0};
        static const p8 switch_pro[] = {0x05, 0x01, 0x15, 0x00, 0x09, 0x04, 0xa1, 0x01, 0x85, 0x30, 0x05, 0x09,
                                        0x19, 0x01, 0x29, 0x0a, 0x81, 0x02, 0xc0};
        static const p8 keyboard[] = {0x05, 0x01, 0x09, 0x06, 0xa1, 0x01, 0x05, 0x07, 0x19, 0xe0, 0x29, 0xe7,
                                      0x81, 0x02, 0xc0};
        static const p8 mouse_then_pad[] = {0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0x09, 0x01, 0xa1, 0x00, 0xc0,
                                            0xc0, 0x05, 0x01, 0x09, 0x05, 0xa1, 0x01, 0xc0};
        static const p8 nested_pad[] = {0x05, 0x01, 0x09, 0x06, 0xa1, 0x01, 0x09, 0x05, 0xa1, 0x01, 0xc0, 0xc0};
        static const p8 vendor[] = {0x06, 0x00, 0xff, 0x09, 0x01, 0xa1, 0x01, 0x81, 0x02, 0xc0};
        static const p8 long_then_pad[] = {0xfe, 0x02, 0x10, 0xaa, 0xbb, 0x05, 0x01, 0x09, 0x05, 0xa1, 0x01, 0xc0};
        static const p8 four_byte_usage[] = {0x0b, 0x05, 0x00, 0x01, 0x00, 0xa1, 0x01, 0xc0};
        static const p8 cut[] = {0x05, 0x01, 0x09};
        p8 uevent[] = "add@/devices/virtual/input/input30/event21\0ACTION=add\0DEVPATH=/devices/virtual/input/input30/event21\0"
                      "SUBSYSTEM=input\0DEVNAME=input/event21\0SEQNUM=5000\0MAJOR=13\0MINOR=85";
        const struct bowl_profile address_to steam = bowl_find_profile("steam");
        positive sdl = chrome_line(steam ? steam->session : null, "SDL_JOYSTICK_DISABLE_UDEV=1");
        positive run = chrome_line(steam ? steam->session : null, "bowl " BOWL_ROOT_PREFIX "arch /usr/bin/steam \"$@\"");

        bowl_pad_bits(pad_keys, string_length(pad_keys), bytes, sizeof(bytes));
        check("A pad's key bits read from sysfs: BTN_SOUTH and BTN_EAST set, BTN_LEFT and KEY_A not",
              (bytes[0x130 / 8] & 3) == 3 && !(bytes[0x110 / 8] & 1) && !(bytes[3] & 0x40));
        check("The pad's bits make a game's device by the compositor's rule", window_input_game(bytes, 0) == 1);
        bowl_pad_bits(one, string_length(one), bytes, 4);
        check("A properties word of 40 is INPUT_PROP_ACCELEROMETER alone", bytes[0] == 0x40 && !bytes[1]);
        bowl_pad_bits(none, string_length(none), bytes, 4);
        check("An empty bitmap is no bits", !bytes[0] && !bytes[1] && !bytes[2] && !bytes[3]);
        check("A DualSense's descriptor (a gamepad) and a Switch Pro Controller's (a joystick) are pads",
              bowl_pad_descriptor((p8 address_to)dualsense, sizeof(dualsense)) &&
                  bowl_pad_descriptor((p8 address_to)switch_pro, sizeof(switch_pro)));
        check("A keyboard, a mouse with a pad collection after it and a pad inside a keyboard are not",
              !bowl_pad_descriptor((p8 address_to)keyboard, sizeof(keyboard)) &&
                  !bowl_pad_descriptor((p8 address_to)mouse_then_pad, sizeof(mouse_then_pad)) &&
                  !bowl_pad_descriptor((p8 address_to)nested_pad, sizeof(nested_pad)));
        check("A vendor collection is no pad by its descriptor (Valve's are by their vendor), and a cut descriptor is none",
              !bowl_pad_descriptor((p8 address_to)vendor, sizeof(vendor)) &&
                  !bowl_pad_descriptor((p8 address_to)cut, sizeof(cut)));
        check("A long item is passed over, and a four-byte usage carries its own page",
              bowl_pad_descriptor((p8 address_to)long_then_pad, sizeof(long_then_pad)) &&
                  bowl_pad_descriptor((p8 address_to)four_byte_usage, sizeof(four_byte_usage)));
        check("A uevent's fields are read by their whole names",
              string_equals(bowl_pad_uevent_value(uevent, sizeof(uevent) - 1, "ACTION"), "add") &&
                  string_equals(bowl_pad_uevent_value(uevent, sizeof(uevent) - 1, "DEVNAME"), "input/event21") &&
                  string_equals(bowl_pad_uevent_value(uevent, sizeof(uevent) - 1, "MINOR"), "85") &&
                  !bowl_pad_uevent_value(uevent, sizeof(uevent) - 1, "DEV") &&
                  !bowl_pad_uevent_value(uevent, sizeof(uevent) - 1, "add@/devices/virtual/input/input30/event21"));
        check("The steam profile names its bowl in the input grants", steam && steam->input);
        check("The Steam session tells SDL to watch the directories for pads before Steam starts",
              sdl && run && sdl < run &&
                  chrome_line(steam ? steam->session : null, "SDL_HIDAPI_JOYSTICK_DISABLE_UDEV=1") == sdl &&
                  chrome_line(steam ? steam->session : null, "SDL_HIDAPI_UDEV=0") == sdl);
}

#if X64
// A little-endian word of an ELF image, bytes wide (2, 4, 8), at offset: 0 where it would run past the image's end.
static positive bowl_check_elf_word(const p8 *image, positive size, positive offset, positive bytes)
{
        positive value = 0;

        if (offset > size || bytes > size - offset)
                return 0;
        while (bytes--)
                value = (value << 8) | image[offset + bytes];
        return value;
}

// Whether an ELF shared object's .dynsym defines name as a global function: 1 it does, 0 it does not, -1 a table out of the image.
static int bowl_check_elf_exports(const p8 *image, positive size, string_address name)
{
        bool wide = size > 4 && image[4] == 2;
        positive shoff = bowl_check_elf_word(image, size, wide ? 0x28 : 0x20, wide ? 8 : 4);
        positive shentsize = bowl_check_elf_word(image, size, wide ? 0x3A : 0x2E, 2);
        positive shnum = bowl_check_elf_word(image, size, wide ? 0x3C : 0x30, 2);
        positive length = string_length(name);

        if (!shoff || shentsize < (wide ? 64u : 40u))
                return -1;
        for (positive at = 0; at < shnum; at++)
        {
                positive base = shoff + at * shentsize;
                positive offset, bytes, link, entsize, strings;

                if (bowl_check_elf_word(image, size, base + 4, 4) != 11) // SHT_DYNSYM
                        continue;
                offset = bowl_check_elf_word(image, size, base + (wide ? 0x18 : 0x10), wide ? 8 : 4);
                bytes = bowl_check_elf_word(image, size, base + (wide ? 0x20 : 0x14), wide ? 8 : 4);
                link = bowl_check_elf_word(image, size, base + (wide ? 0x28 : 0x18), 4);
                entsize = bowl_check_elf_word(image, size, base + (wide ? 0x38 : 0x24), wide ? 8 : 4);
                if (link >= shnum || entsize < (wide ? 24u : 16u) || offset > size || bytes > size - offset)
                        return -1;
                strings = bowl_check_elf_word(image, size, shoff + link * shentsize + (wide ? 0x18 : 0x10), wide ? 8 : 4);
                for (positive symbol = offset; symbol + entsize <= offset + bytes; symbol += entsize)
                {
                        positive text = strings + bowl_check_elf_word(image, size, symbol, 4);
                        positive info = bowl_check_elf_word(image, size, symbol + (wide ? 4 : 12), 1);
                        positive index = bowl_check_elf_word(image, size, symbol + (wide ? 6 : 14), 2);

                        if (text >= size || length + 1 > size - text || memory_compare(image + text, name, length) || image[text + length])
                                continue;
                        return (info & 0xF) == 2 && (info >> 4) == 1 && index != 0;
                }
                return 0;
        }
        return -1;
}
#endif

static fn steam_profile(void)
{
        const struct bowl_profile address_to steam = bowl_find_profile("steam");
        const struct bowl_profile address_to chrome = bowl_find_profile("chrome");
        const struct bowl_profile address_to graphics = bowl_find_profile("graphics");
        const struct bowl_component address_to part = steam ? steam->components : null;
        static string_address daemons[] = {
            "pipewire", "networkmanager", "gamemode", "lib32-gamemode", "systemd",
            "xorg-server", "sddm", "elogind", "seatd", "wireplumber", "greetd", null};
        p8 unique = true;
        p8 named = true;

        check("The steam profile has one component, Arch's, and no libraries",
              steam && !steam->libraries && steam->session && part &&
                  string_equals(part->distro, "arch") && !(part + 1)->distro);
        check("The steam row is its own profile, not chrome's or graphics'",
              steam && chrome && graphics && steam != chrome && steam != graphics);
        check("The steam component names steam and the packages the profile holds, none of them empty",
              part && chrome_has(part->packages, "steam") && part->packages[0] && part->packages[0][0]);
        for (positive at = 0; part && part->packages[at]; at++)
                for (positive other = at + 1; part->packages[other]; other++)
                        unique &= !string_equals(part->packages[at], part->packages[other]);
        check("No package the steam profile names is named twice", unique);
        for (string_address address_to name = daemons; *name; name++)
                named &= !chrome_has(part ? part->packages : null, *name);
        check("No package the steam profile names is a daemon, a display server or a seat Moonwater does not run",
              named);
        p8 graphic = true;

        for (string_address address_to name = bowl_graphics_arch; *name; name++)
                graphic &= chrome_has(part ? part->packages : null, *name);
        check("The steam profile holds every package of the graphics profile for Arch, which is where the GPU comes from",
              graphic);
        check("The steam profile has the 32-bit Mesa, sound and Vulkan loader packages Steam loads by name",
              chrome_has(part ? part->packages : null, "lib32-mesa") &&
                  chrome_has(part ? part->packages : null, "lib32-vulkan-radeon") &&
                  chrome_has(part ? part->packages : null, "lib32-vulkan-virtio") &&
                  chrome_has(part ? part->packages : null, "lib32-vulkan-swrast") &&
                  chrome_has(part ? part->packages : null, "lib32-libpulse") &&
                  chrome_has(part ? part->packages : null, "lib32-vulkan-icd-loader"));
        check("The steam profile has VirtIO Vulkan drivers alongside Radeon, Intel and software drivers",
              chrome_has(part ? part->packages : null, "vulkan-virtio") &&
                  chrome_has(bowl_graphics_arch, "vulkan-virtio"));
        check("The steam profile has the NVIDIA Vulkan driver the session names for a card (NVK, in 64 and 32 bits), and the graphics profile has it for Arch and for Alpine",
              chrome_has(part ? part->packages : null, "vulkan-nouveau") &&
                  chrome_has(part ? part->packages : null, "lib32-vulkan-nouveau") &&
                  chrome_has(bowl_graphics_arch, "vulkan-nouveau") && chrome_has(bowl_graphics_alpine, "mesa-vulkan-nouveau"));
        check("The steam profile has Xwayland, which the session starts beside Steam, bubblewrap for the Steam Runtime, and which for steam.sh",
              chrome_has(part ? part->packages : null, "xorg-xwayland") &&
                  chrome_has(part ? part->packages : null, "bubblewrap") &&
                  chrome_has(part ? part->packages : null, "which") &&
                  chrome_has(part ? part->packages : null, "zenity"));
        check("The steam component exposes no program of the bowl by name: its session is the way in",
              part && part->expose && !part->expose[0]);
        check("Steam's launcher is the profile's own name and no other name",
              steam && !steam->names);

        positive socket = chrome_line(steam ? steam->session : null, "-S /run/moonwater/wayland-0");
        positive exported = chrome_line(steam ? steam->session : null, "export WAYLAND_DISPLAY=/run/moonwater/wayland-0");
        positive capabilities = chrome_line(steam ? steam->session : null, "BOWL_CAPABILITIES=none");
        positive xwayland = chrome_line(steam ? steam->session : null, "/usr/bin/Xwayland \"$display\"");
        positive wait = chrome_line(steam ? steam->session : null, "-S \"$xsaving\"");
        positive display = chrome_line(steam ? steam->session : null, "export DISPLAY=\"$display\"");
        positive display_scan = chrome_line(steam ? steam->session : null, "while [ $d -le 32 ];");
        positive run = chrome_line(steam ? steam->session : null, "bowl " BOWL_ROOT_PREFIX "arch /usr/bin/steam \"$@\"");
        positive sound = chrome_line(steam ? steam->session : null, "PULSE_SERVER=unix:/run/moonwater/pulse/native");
        positive video_chosen = chrome_line(steam ? steam->session : null, "SDL_VIDEODRIVER");
        positive proton_chosen = chrome_line(steam ? steam->session : null, "PROTON_ENABLE_WAYLAND");
        positive ended = chrome_line(steam ? steam->session : null, "; exit $status");
        positive wm_socket = chrome_line(steam ? steam->session : null, "-S /run/moonwater/xwm/wm-0");
        positive wrapped = chrome_line(steam ? steam->session : null, "/usr/local/bin/xwayland-moonwater /usr/bin/steam \"$@\"");
        positive fallback = chrome_line(steam ? steam->session : null, "-ne 125");
        positive gl_probe = chrome_line(steam ? steam->session : null, "gl=software; for node in /sys/class/drm/renderD*;");
        positive sw_export = chrome_line(steam ? steam->session : null, "export LIBGL_ALWAYS_SOFTWARE=");
        positive overlay_gl = chrome_line(steam ? steam->session : null,
                                          "export LD_PRELOAD='/usr/local/$LIB/steam-overlay-gl.so'${LD_PRELOAD:+:$LD_PRELOAD}");
        check("The session preloads steam-overlay-gl.so by $LIB (lib, lib32) ahead of what was preloaded, when the bowl holds it, before Steam starts",
              overlay_gl && overlay_gl < wrapped && overlay_gl < run &&
                  strstr(steam->session[overlay_gl - 1], "[ -r " BOWL_ROOT_PREFIX "arch/usr/local/lib/steam-overlay-gl.so ] && "));
        {
                positive input_shim = chrome_line(steam ? steam->session : null,
                                                  "export LD_PRELOAD='/usr/local/$LIB/libmoonwater-input.so'${LD_PRELOAD:+:$LD_PRELOAD}");

                check("The session preloads libmoonwater-input.so by $LIB when the bowl holds it, behind its workaround switch, before Steam starts",
                      input_shim && input_shim < wrapped && input_shim < run &&
                          strstr(steam->session[input_shim - 1],
                                 "wa moonwater-input && [ -r " BOWL_ROOT_PREFIX "arch/usr/local/lib/libmoonwater-input.so ] && "));
        }
        {
                positive wsi = chrome_line(steam ? steam->session : null, "export MOONWATER_WSI=${MOONWATER_WSI:-1}");

                check("The session turns the Canvas Vulkan layer on, behind its workaround switch, when the bowl holds it, a game's own MOONWATER_WSI kept, before Steam starts",
                      wsi && wsi < wrapped && wsi < run &&
                          strstr(steam->session[wsi - 1], "wa canvas-wsi && [ -r " BOWL_ROOT_PREFIX "arch/usr/lib/libVkLayer_MOONWATER_canvas.so ] && "));
#if X64
                check("Each architecture's manifest names its own library by its path, under a layer name of its own, enabled by MOONWATER_WSI=1 and off by MOONWATER_WSI_OFF",
                      strstr(bowl_canvas_wsi_json_64, "\"library_path\": \"/usr/lib/libVkLayer_MOONWATER_canvas.so\"") &&
                          strstr(bowl_canvas_wsi_json_32, "\"library_path\": \"/usr/lib32/libVkLayer_MOONWATER_canvas.so\"") &&
                          strstr(bowl_canvas_wsi_json_64, "VK_LAYER_MOONWATER_canvas_x86_64") && strstr(bowl_canvas_wsi_json_32, "VK_LAYER_MOONWATER_canvas_i686") &&
                          strstr(bowl_canvas_wsi_json_64, "\"enable_environment\": {\"MOONWATER_WSI\": \"1\"}") &&
                          strstr(bowl_canvas_wsi_json_32, "\"disable_environment\": {\"MOONWATER_WSI_OFF\": \"1\"}"));
#endif
        }
        {
                const struct bowl_profile address_to chrome_profile = bowl_find_profile("chrome");

                check("Only the steam profile holds the overlay library",
                      steam && steam->overlay_gl && chrome_profile && !chrome_profile->overlay_gl);
        }
#if X64
        {
                // Each library is an ELF shared object of its machine (ET_DYN; i386 class 1 machine 3, x86_64 class 2 machine 62)
                // whose .dynsym defines, as global functions, the calls it stands in front of, its dlsym and its EGL swap, and
                // not vkGetInstanceProcAddr (exported, it took a Vulkan layer's chain). test/steam_overlay_check.py --blob holds the
                // bytes to a build of the source.
                static const char *const hooks[] = {"glBegin", "glDrawArrays", "glDrawElements", "glPopAttrib", "glXGetCurrentContext",
                                                    "dlsym", "eglSwapBuffers", "XOpenDisplay"};
                const p8 *const images[] = {bowl_steam_overlay_gl_32, bowl_steam_overlay_gl_64};
                const positive sizes[] = {sizeof(bowl_steam_overlay_gl_32), sizeof(bowl_steam_overlay_gl_64)};
                bool right = true;

                for (positive which = 0; which < 2; which++)
                {
                        const p8 *image = images[which];

                        right &= sizes[which] > 64 && image[0] == 0x7f && image[1] == 'E' && image[2] == 'L' &&
                                 image[3] == 'F' && image[4] == which + 1 && image[16] == 3 &&
                                 image[18] == (which ? 62 : 3) && image[19] == 0;
                        for (positive hook = 0; right && hook < sizeof(hooks) / sizeof(hooks[0]); hook++)
                                right &= bowl_check_elf_exports(image, sizes[which], hooks[hook]) == 1;
                        right &= bowl_check_elf_exports(image, sizes[which], "vkGetInstanceProcAddr") == 0;
                }
                check("The overlay libraries are i386 and x86_64 shared objects whose .dynsym exports the GL calls, glXGetCurrentContext, dlsym, eglSwapBuffers and XOpenDisplay as global functions, and not vkGetInstanceProcAddr",
                      right);
        }
        {
                // libmoonwater-input.so: the entry SDL looks for, and nothing of the C library's (the overlay's dlsym stays the one
                // in front of it) nor any SDL function (SDL takes this library's through its table, never by name).
                static const char *const absent[] = {"dlsym", "open", "mmap", "ioctl", "SDL_PollEvent", "SDL_GetRelativeMouseState"};
                const p8 *const images[] = {bowl_moonwater_input_32, bowl_moonwater_input_64};
                const positive sizes[] = {sizeof(bowl_moonwater_input_32), sizeof(bowl_moonwater_input_64)};
                bool right = true;

                for (positive which = 0; which < 2; which++)
                {
                        const p8 *image = images[which];

                        right &= sizes[which] > 64 && image[0] == 0x7f && image[1] == 'E' && image[2] == 'L' &&
                                 image[3] == 'F' && image[4] == which + 1 && image[16] == 3 &&
                                 image[18] == (which ? 62 : 3) && image[19] == 0;
                        right &= bowl_check_elf_exports(image, sizes[which], "SDL_DYNAPI_entry") == 1;
                        for (positive name = 0; right && name < sizeof(absent) / sizeof(absent[0]); name++)
                                right &= bowl_check_elf_exports(image, sizes[which], absent[name]) == 0;
                }
                check("The input libraries are i386 and x86_64 shared objects whose .dynsym exports SDL_DYNAPI_entry as a global function, and none of the C library's names or SDL's",
                      right);
        }
        {
                // libVkLayer_MOONWATER_canvas.so: the loader's negotiation alone (the manifest names it; the loader takes the rest from
                // it), never a name of the loader's, the driver's, the C library's or libwayland's (a layer that exports one takes it from
                // everything loaded after it).
                static const char *const absent[] = {"vkGetInstanceProcAddr", "vkGetDeviceProcAddr", "vkCreateInstance", "vkQueuePresentKHR",
                                                     "vkCreateSwapchainKHR", "dlsym", "malloc", "poll", "wl_display_connect", "xcb_query_tree"};
                const p8 *const images[] = {bowl_canvas_wsi_32, bowl_canvas_wsi_64};
                const positive sizes[] = {sizeof(bowl_canvas_wsi_32), sizeof(bowl_canvas_wsi_64)};
                bool right = true;

                for (positive which = 0; which < 2; which++)
                {
                        const p8 *image = images[which];

                        right &= sizes[which] > 64 && image[0] == 0x7f && image[1] == 'E' && image[2] == 'L' &&
                                 image[3] == 'F' && image[4] == which + 1 && image[16] == 3 &&
                                 image[18] == (which ? 62 : 3) && image[19] == 0;
                        right &= bowl_check_elf_exports(image, sizes[which], "vkNegotiateLoaderLayerInterfaceVersion") == 1;
                        for (positive name = 0; right && name < sizeof(absent) / sizeof(absent[0]); name++)
                                right &= bowl_check_elf_exports(image, sizes[which], absent[name]) == 0;
                }
                check("The Vulkan layers are i386 and x86_64 shared objects whose .dynsym exports vkNegotiateLoaderLayerInterfaceVersion as a global function, and none of the loader's, the C library's or libwayland's names",
                      right);
        }
        {
                // libmoonwater-c.so: an x86_64 shared object that gives the C library's memory and string names, the checked
                // ones glibc's fortified programs call, and none of the names that allocate, follow the locale or hold a jmp_buf.
                static const char *const present[] = {"memcpy", "memmove", "memset", "memcmp", "strlen", "memchr", "strcmp",
                                                      "strchr", "__memcpy_chk", "__strcpy_chk", "rawmemchr", "wmemset"};
                static const char *const absent[] = {"strdup", "malloc", "free", "setjmp", "longjmp", "strcasecmp", "tolower",
                                                     "isalpha", "atoi", "strtol", "memory_copy", "string_length", "main"};
                const p8 *image = bowl_libc_shim_64;
                bool right = sizeof(bowl_libc_shim_64) > 64 && image[0] == 0x7f && image[1] == 'E' && image[2] == 'L' &&
                             image[3] == 'F' && image[4] == 2 && image[16] == 3 && image[18] == 62 && image[19] == 0;

                for (positive name = 0; right && name < sizeof(present) / sizeof(present[0]); name++)
                        right &= bowl_check_elf_exports(image, sizeof(bowl_libc_shim_64), present[name]) == 1;
                for (positive name = 0; right && name < sizeof(absent) / sizeof(absent[0]); name++)
                        right &= bowl_check_elf_exports(image, sizeof(bowl_libc_shim_64), absent[name]) == 0;
                check("The libc shim is an x86_64 shared object whose .dynsym gives the C library's memory, string and checked names, and none that allocate, follow the locale, hold a jmp_buf or are lib.c's own",
                      right);
                image = bowl_libc_shim_32;
                check("The libc shim's i386 build is an i386 shared object that gives no name at all, so a 32-bit program keeps its C library's",
                      sizeof(bowl_libc_shim_32) > 64 && image[0] == 0x7f && image[1] == 'E' && image[4] == 1 && image[16] == 3 &&
                          image[18] == 3 && bowl_check_elf_exports(image, sizeof(bowl_libc_shim_32), "memcpy") == 0 &&
                          bowl_check_elf_exports(image, sizeof(bowl_libc_shim_32), "strlen") == 0);
        }
#endif
        {
                // Every profile's session script is held whole, however long it grows: its memfd reads back as its lines, each
                // ended with a newline, sealed against any write; and a session of 300 KiB (past the 128 KiB of one argument
                // and the 4 KiB buffer the steam session outgrew) is held whole too.
                static string_address big[2];
                static p8 line[300 * 1024];
                struct bowl_profile made = {0};
                b32 whole = 1;

                for (positive p = 0; p <= array_count(bowl_profiles); p++)
                {
                        const struct bowl_profile address_to row = p < array_count(bowl_profiles) ? bowl_profiles + p : &made;
                        bipolar handle;
                        positive want = 0, got = 0;
                        p8 piece[4096];
                        bipolar read;

                        if (row == &made)
                        {
                                for (positive at = 0; at + 1 < sizeof(line); at++)
                                        line[at] = '#';
                                line[sizeof(line) - 1] = end;
                                big[0] = (string_address)line, big[1] = null, made.session = big;
                        }
                        if (!row->session)
                                continue;
                        for (string_address address_to each = row->session; *each; each++)
                                want += string_length(*each) + 1;
                        handle = bowl_session_script(row);
                        whole &= handle >= 0;
                        if (handle < 0)
                                continue;
                        system_call_3(syscall(lseek), (positive)handle, 0, 0);
                        while ((read = system_read_once(handle, piece, sizeof(piece))) > 0)
                                got += (positive)read;
                        whole &= got == want && system_write_once(handle, "x", 1) < 0;
                        system_close(handle);
                }
                check("Every profile's session script is held whole in a sealed memfd, however long (a 300 KiB one too), and nothing writes it after",
                      whole);
        }
        check("The session looks for Canvas's socket before it starts anything, and says how to turn the shim on",
              socket == 1 && chrome_count(steam->session[0], "echo ") == 1);
        check("The session names the socket as an absolute path, and marks the launch as the person's",
              exported && exported < run && strstr(steam->session[exported - 1], "MOONWATER_ASKED=1"));
        check("The session clears the bowl's capabilities, which bubblewrap under Steam Runtime refuses",
              capabilities && capabilities < run);
        check("The sound server is named when it is up, and the session does not start a sound server of its own",
              sound && sound < run && strstr(steam->session[sound - 1], "-S /run/moonwater/pulse/native"));
        check("The session chooses no video driver for the games (no SDL_VIDEODRIVER, no PROTON_ENABLE_WAYLAND): each takes its own default",
              steam && !video_chosen && !proton_chosen);
        {
                const struct bowl_profile address_to chrome_profile = bowl_find_profile("chrome");
                positive latency = chrome_line(steam ? steam->session : null, "export DXVK_CONFIG='dxvk.latencySleep = True'");
                positive topology = chrome_line(steam ? steam->session : null, "export WINE_CPU_TOPOLOGY=$cpus");

                check("The session turns DXVK's latency sleep on unless the person set DXVK_CONFIG, behind wa dxvk-latency, before Steam starts",
                      latency && latency < run && strstr(steam->session[latency - 1], "wa dxvk-latency && [ -z \"$DXVK_CONFIG\" ]"));
                check("The session tells Wine games of the CPUs game mode pins them to, the compositor's list, unless the person set WINE_CPU_TOPOLOGY",
                      topology && topology < run && strstr(steam->session[topology - 1], "wa wine-ccd && [ -z \"$WINE_CPU_TOPOLOGY\" ]") &&
                          strstr(steam->session[topology - 1], "/run/moonwater/game-cpus"));
                check("The session starts the shader warm in the background behind wa shader-warm, after the Vulkan driver is picked, before Steam",
                      chrome_line(steam->session, "wa shader-warm && { bowl shaders >/dev/null 2>&1 & }") > chrome_line(steam->session, "VK_DRIVER_FILES") &&
                          chrome_line(steam->session, "wa shader-warm") < run);
                check("The session starts the game prewarm in the background behind wa game-prewarm, before Steam",
                      chrome_line(steam->session, "wa game-prewarm && { bowl prewarm >/dev/null 2>&1 & }") &&
                          chrome_line(steam->session, "wa game-prewarm") < run);
                check("No session says PROTON_USE_NTSYNC or PROTON_ENABLE_NTSYNC (Proton 11 opens /dev/ntsync itself)",
                      steam && !chrome_line(steam->session, "NTSYNC") && chrome_profile && !chrome_line(chrome_profile->session, "NTSYNC"));
        }
        check("Xwayland starts before Steam, and the session waits for its socket before exporting DISPLAY",
              display_scan && display_scan < xwayland && xwayland && wait && display && xwayland < wait && wait < display && display < run);
        check("The session skips vgem and vkms render nodes when probing for hardware GL",
              chrome_line(steam ? steam->session : null, "\"$node/device\"") != 0 &&
                  chrome_line(steam ? steam->session : null, "*DRIVER=vgem*|*platform:vgem*|*DRIVER=vkms*) continue") != 0);
        check("The session ends Xwayland when Steam ends, and gives Steam's exit status back",
              ended && ended > run && strstr(steam->session[ended - 1], "kill $xwayland"));
        check("The session passes the arguments it was given to Steam",
              run && strstr(steam->session[run - 1], "\"$@\""));
        check("Where Canvas listens for the window manager of Xwayland, Steam runs under xwayland-moonwater, with its arguments, "
              "and that comes before the rootful server",
              wm_socket && wrapped && wm_socket < wrapped && wrapped < xwayland &&
                  strstr(steam->session[wrapped - 1], "bowl " BOWL_ROOT_PREFIX "arch "));
        check("Status 125 of the wrapper (no X server started) is the one that lets the session start the rootful server; any other "
              "status of Steam ends the session",
              fallback && fallback > wrapped && fallback < xwayland && strstr(steam->session[fallback - 1], "exit $status"));
        check("Only the steam profile runs X programs: no other profile holds the wrapper",
              steam && steam->xwayland && bowl_find_profile("chrome") && !bowl_find_profile("chrome")->xwayland &&
                  !bowl_find_profile("graphics")->xwayland);
        check("The session gives Steam no flags of its own (its CEF sandbox runs on the bowl's user namespaces)",
              run && !strstr(steam->session[run - 1], "--no-sandbox") &&
                  !strstr(steam->session[run - 1], "-no-cef-sandbox"));
        check("The session probes the DRM render node and falls back to software GL/GSK only when no card or virgl is present",
              gl_probe && sw_export && gl_probe < sw_export && sw_export < wrapped &&
                  strstr(steam->session[sw_export - 1], "GSK_RENDERER=${GSK_RENDERER:-cairo}"));
        {
                positive zenity = chrome_line(steam ? steam->session : null, "STEAM_ZENITY=${STEAM_ZENITY:-zenity}");
                check("The session binds STEAM_ZENITY before starting Steam, so set -u never aborts on an unbound helper variable",
                      zenity && zenity < wrapped && zenity < run);
        }
        {
                positive fonts = chrome_line(steam ? steam->session : null,
                                             "FONTCONFIG_FILE=${FONTCONFIG_FILE:-/etc/fonts/fonts.conf}");
                check("The session names fontconfig's file and path before Steam starts, kept where the person set them",
                      fonts && fonts < wrapped && fonts < run &&
                          strstr(steam->session[fonts - 1], "FONTCONFIG_PATH=${FONTCONFIG_PATH:-/etc/fonts}"));
                check("The steam profile has UPower, which Steam's CEF asks the system bus for, and dbus, the bus",
                      chrome_has(part ? part->packages : null, "upower") &&
                          chrome_has(part ? part->packages : null, "dbus"));
        }
        {
                p8 arch_root[256];
                p8 debian_root[256];
                p8 bare_root[256];
                p8 source[BOWL_PATH_LIMIT];
                p8 want[BOWL_PATH_LIMIT];

                unpack_path(arch_root, "lib32arch", "");
                unpack_path(debian_root, "lib32deb", "");
                unpack_path(bare_root, "lib32bare", "");
                unpack_directory(arch_root, "/usr/lib32");
                unpack_directory(debian_root, "/lib32");
                unpack_directory(debian_root, "/usr/lib32");
                unpack_directory(bare_root, "/usr/lib");

                p8 deb_source[BOWL_PATH_LIMIT];
                p8 expected_arch[BOWL_PATH_LIMIT];
                p8 expected_deb[BOWL_PATH_LIMIT];
                bipolar arch_hit = bowl_fast_layer_source(arch_root, "/lib32", source, sizeof(source));
                bipolar deb_hit = bowl_fast_layer_source(debian_root, "/lib32", deb_source, sizeof(deb_source));
                bipolar bare_hit = bowl_fast_layer_source(bare_root, "/lib32", want, sizeof(want));

                unpack_path(expected_arch, "lib32arch", "/usr/lib32");
                unpack_path(expected_deb, "lib32deb", "/lib32");
                check("An Arch-layout bowl resolves fast-view /lib32 to <root>/usr/lib32 while a tree with /lib32 keeps it",
                      arch_hit == 1 && string_equals(source, expected_arch) &&
                          deb_hit == 1 && string_equals(deb_source, expected_deb) &&
                          bare_hit == 0 && bowl_session_host_path("/lib32/ld-linux.so.2"));
                bowl_forget_path(arch_root);
                bowl_forget_path(debian_root);
                bowl_forget_path(bare_root);
        }
        {
                const char address_to script = bowl_xwayland_script;

                check("The wrapper is a Python script, which connects to the socket Canvas listens on for the window manager",
                      string_has_prefix(script, "#!/usr/bin/python3\n") && strstr(script, "\"/run/moonwater/xwm/wm-0\"") &&
                          strstr(script, "socket.AF_UNIX") && strstr(script, "sizeof") == null);
                check("The wrapper starts Xwayland rootless with that connection as its window manager, no local socket, and no reset",
                      strstr(script, "\"-rootless\", \"-wm\", str(wm.fileno())") && strstr(script, "\"-nolisten\", \"local\"") &&
                          strstr(script, "\"-noreset\"") && strstr(script, "pass_fds=[wm.fileno()]"));
                check("The wrapper exits 125 when it cannot start the server, tries a bounded number of displays, and waits a bounded time",
                      strstr(script, "UNSTARTED = 125") && strstr(script, "DISPLAYS = 32") && strstr(script, "WAIT = 5.0") &&
                          strstr(script, "range(1, DISPLAYS + 1)"));
                check("The wrapper ends the server when its program ends, and runs the program with DISPLAY set",
                      strstr(script, "DISPLAY=display") && strstr(script, "process.terminate()") && strstr(script, "child.wait()"));
                check("The wrapper keeps a child's exit 125 from meaning the rootless X server never started",
                      strstr(script, "1 if status == UNSTARTED else status"));
                {
                        string_address handlers = strstr(script, "signal.signal(name, finish)");
                        string_address started = strstr(script, "started = start(");
                        positive kill_line = chrome_line(steam ? steam->session : null, "kill $xwayland 2>/dev/null; exit $status");

                        check("Xwayland is sent SIGTERM when the wrapper ends however it ends (PR_SET_PDEATHSIG in its child), and the wrapper's "
                              "signal handlers are set before the server is started",
                              strstr(script, "prctl(PR_SET_PDEATHSIG, signal.SIGTERM") && strstr(script, "preexec_fn=lambda: dies_with_us(parent)") &&
                                  handlers && started && handlers < started);
                        check("The server is not ended under a game still on it when Steam ends first (Steam's renderer loaded, DISPLAY the "
                              "display): the wrapper waits, and so does the rootful session before its kill",
                              strstr(script, "while games(display):") && strstr(script, "b\"gameoverlayrenderer\"") && kill_line > 1 &&
                                  strstr(steam->session[kill_line - 2], "gameoverlayrenderer") &&
                                  strstr(steam->session[kill_line - 2], "DISPLAY=$display"));
                }
                check("The wrapper is written where the bowl runs it from, and is small",
                      string_equals(BOWL_XWAYLAND_WRAPPER, "/usr/local/bin/xwayland-moonwater") && sizeof(bowl_xwayland_script) < 8192);
        }
        {
                const struct bowl_profile address_to sessions[] = {steam, chrome};
                string_address roots[] = {BOWL_ROOT_PREFIX "arch/usr/share/vulkan/icd.d/", BOWL_ROOT_PREFIX "alpine/usr/share/vulkan/icd.d/"};
                string_address starts[] = {"/usr/bin/steam \"$@\"",
                                           "exec bowl " BOWL_ROOT_PREFIX "alpine /usr/bin/chromium "};

                p8 picks[2] = {0}, kept[2] = {0}, caps[2] = {0}, homes[2] = {0};

                for (positive at = 0; at < 2; at++)
                {
                        string_address address_to lines = sessions[at] ? sessions[at]->session : null;
                        positive pick = chrome_line(lines, "/sys/class/drm/renderD*/device/vendor");
                        positive cache = chrome_line(lines, "MESA_SHADER_CACHE_MAX_SIZE=${mb}M");
                        positive run = chrome_line(lines, starts[at]);
                        string_address picked = pick ? lines[pick - 1] : (string_address)"";
                        string_address capped = cache ? lines[cache - 1] : (string_address)"";

                        picks[at] = pick && run && pick < run && strstr(picked, "export VK_DRIVER_FILES=$files") &&
                                    strstr(picked, roots[at]) && strstr(picked, "0x1002) want=radeon") &&
                                    strstr(picked, "0x8086) want='intel intel_hasvk'") && strstr(picked, "0x1af4) want=virtio") &&
                                    strstr(picked, "0x10de) want='nvidia nouveau'") &&
                                    strstr(picked, "*) continue");
                        kept[at] = string_has_prefix(picked, "[ -n \"$VK_DRIVER_FILES$VK_ICD_FILENAMES\" ] ||") &&
                                   strstr(picked, "[ -z \"$files\" ] ||") && strstr(picked, "json=${json#" BOWL_ROOT_PREFIX);
                        caps[at] = cache && run && cache < run && strstr(capped, "stat -f -c %T \"$HOME\"") &&
                                   strstr(capped, "tmpfs|ramfs)") && strstr(capped, "[ -n \"$MESA_SHADER_CACHE_MAX_SIZE\" ] ||") &&
                                   strstr(capped, "MemTotal:") &&
                                   strstr(capped, "mb=$((kb / 16384)); [ $mb -ge 128 ] || mb=128; [ $mb -le 1024 ] || mb=1024");
                        homes[at] = lines && !chrome_line(lines, "MESA_SHADER_CACHE_DIR") && !chrome_line(lines, "XDG_CACHE_HOME") &&
                                    !chrome_line(lines, "DXVK_STATE_CACHE") && !chrome_line(lines, "MESA_SHADER_CACHE_DISABLE");
                }
                check("The steam session names the Vulkan drivers of the machine's own GPUs before Steam starts", picks[0]);
                check("The chrome session names the Vulkan drivers of the machine's own GPUs before the browser starts", picks[1]);
                check("The steam and chrome sessions keep a person's own Vulkan driver list, and name none when they found none",
                      kept[0] && kept[1]);
                check("The steam and chrome sessions cap Mesa's shader cache only where $HOME is in memory, and keep a person's own cap",
                      caps[0] && caps[1]);
                check("The steam and chrome sessions name no shader cache directory: Steam's per game and Mesa's own, under $HOME, are kept",
                      homes[0] && homes[1]);
        }
        check("The steam profile makes the Steam Runtime's locale in its bowl, and no other profile does",
              steam && steam->locales && chrome && !chrome->locales && graphics && !graphics->locales &&
                  bowl_find_profile("build") &&
                  !bowl_find_profile("build")->locales && bowl_find_profile("ssh") && !bowl_find_profile("ssh")->locales);
        check("A locale.gen lists en_US.UTF-8 when the line is there whole, at the start, after a newline or before a comment",
              bowl_locale_listed("en_US.UTF-8 UTF-8\n") && bowl_locale_listed("# head\nen_US.UTF-8 UTF-8") &&
                  bowl_locale_listed("# head\n#en_US.UTF-8 UTF-8\nen_US.UTF-8 UTF-8  # mine\n"));
        check("A locale.gen does not list it when the line is commented, longer, inside another line, or not there",
              !bowl_locale_listed("#en_US.UTF-8 UTF-8\n") && !bowl_locale_listed("# en_US.UTF-8 UTF-8\n") &&
                  !bowl_locale_listed("en_US.UTF-8 UTF-8x\n") && !bowl_locale_listed("xen_US.UTF-8 UTF-8\n") &&
                  !bowl_locale_listed("de_DE.UTF-8 UTF-8\n") && !bowl_locale_listed("") && !bowl_locale_listed(null));
#if X64
        check("Steam is on this machine's architecture: the profile can be installed", steam && !steam->unavailable);
#else
        check("Steam is not on this machine's architecture: the profile says so",
              steam && steam->unavailable && string_has_prefix(steam->unavailable, "Arch has no Steam"));
#endif
}

static fn chrome_profile(void)
{
        const struct bowl_profile address_to chrome = bowl_find_profile("chrome");
        const struct bowl_profile address_to graphics = bowl_find_profile("graphics");
        const struct bowl_component address_to part = chrome ? chrome->components : null;
        static string_address banned[] = {
            "weston", "kwin", "sway", "wayfire", "hyprland", "labwc", "mutter", "xorg", "xserver",
            "xvfb", "xwayland", "sddm", "gdm", "lightdm", "elogind", "seatd", "logind", "plasma",
            "gnome", "kde", "portal", "polkit", null};
        p8 clean = part != null;
        p8 graphic = part != null;

        check("The chrome profile has Alpine's chromium as its one component",
              chrome && !chrome->libraries && chrome->session &&
                  part->distro && string_equals(part->distro, "alpine") && !part[1].distro &&
                  chrome_has(part->packages, "chromium"));

        for (string_address address_to name = part ? part->packages : null; name && *name; name++)
                for (string_address address_to word = banned; *word; word++)
                        clean &= strstr(*name, *word) == null;
        check("No package the chrome profile names is a compositor, an X server, a display manager, a seat or "
              "login daemon, a desktop environment or a portal",
              clean);

        for (string_address address_to name = graphics ? graphics->components->packages : null;
             name && *name; name++)
                graphic &= chrome_has(part ? part->packages : null, *name);
        check("The chrome profile holds every package of the graphics profile, which is where the GPU comes from",
              graphic);

        check("The chrome component exposes no program of the bowl by name: its launcher is the way in",
              part && part->expose && !part->expose[0] &&
                  !graphics->components->expose);
        check("The names a session answers to are valid, are not the profile's own, and name chromium",
              chrome && chrome->names && chrome_has(chrome->names, "chromium") &&
                  chrome_has(chrome->names, "chromium-browser") && !chrome_has(chrome->names, "chrome") &&
                  bowl_name(chrome->names[0], true) && bowl_name(chrome->names[1], true) &&
                  !bowl_find_distro(chrome->names[0]) && !bowl_find_distro(chrome->names[1]));

        positive socket = chrome_line(chrome ? chrome->session : null, "-S /run/moonwater/wayland-0");
        positive exported = chrome_line(chrome ? chrome->session : null, "export WAYLAND_DISPLAY=/run/moonwater/wayland-0");
        positive exec = chrome_line(chrome ? chrome->session : null, "exec bowl " BOWL_ROOT_PREFIX "alpine /usr/bin/chromium ");
        string_address start = exec ? chrome->session[exec - 1] : (string_address)"";
        string_address refusal = socket ? chrome->session[socket - 1] : (string_address)"";

        check("The launcher looks for Canvas's socket before it starts anything, and says how to turn the shim on",
              socket && exec && socket < exec && strstr(refusal, "exit 1") && strstr(refusal, "echo ") &&
                  strstr(refusal, "CONFIG_MOONWATER_WAYLAND") && strstr(refusal, ">&2") &&
                  chrome_count(refusal, "echo ") == 1);
        check("The launcher names the shim's socket as an absolute path and marks the launch as the person's",
              exported && exported < exec && strstr(chrome->session[exported - 1], "MOONWATER_ASKED=1"));
        positive themed = chrome_line(chrome ? chrome->session : null, "GTK_THEME=Adwaita:dark");

        check("The launcher gives GTK 3 the dark theme when the desktop is dark, before it starts the browser, and not over the person's own",
              themed && themed < exec && strstr(chrome->session[themed - 1], "-n \"$GTK_THEME\"") &&
                  strstr(chrome->session[themed - 1], "/run/moonwater/color-scheme") && strstr(chrome->session[themed - 1], "!= dark"));
        check("The launcher starts the browser with no capabilities of its namespace, which bubblewrap under GTK's file chooser refuses",
              exported && exported < exec && strstr(chrome->session[exported - 1], "BOWL_CAPABILITIES=none"));

        static string_address kept[] = {"HOME=/root", "BOWL_CAPABILITIES=all", null};
        static string_address cleared[] = {"HOME=/root", "BOWL_CAPABILITIES=none", null};
        static string_address longer[] = {"BOWL_CAPABILITIES=nonesuch", null};
        static string_address plain[] = {"HOME=/root", null};
        {
                static const struct
                {
                        const char *file;
                        const char *want; // null for none offered
                } languages[] = {{"de\n", "LANGUAGE=de"}, {"sv", "LANGUAGE=sv"}, {"uk ", "LANGUAGE=uk"}, {"en\n", null}, {"\n", null}, {"", null},
                                 {"d", null}, {"DE", null}, {"d1", null}, {"deu", null}, {"german", null}, {"\xff\xff", null}};
                char path[] = "/tmp/language-check-XXXXXX";
                int handle = mkstemp(path);
                p8 right = handle >= 0;

                for (unsigned each = 0; right && each < sizeof(languages) / sizeof(languages[0]); each++)
                {
                        string_address offered;

                        right &= ftruncate(handle, 0) == 0 && lseek(handle, 0, SEEK_SET) == 0 &&
                                 write(handle, languages[each].file, string_length((const_string)languages[each].file)) == (long)string_length((const_string)languages[each].file);
                        offered = bowl_language_offered((string_address)path);
                        right &= languages[each].want ? offered && !string_compare((string_address)offered, (string_address)languages[each].want) : !offered;
                }
                right &= !bowl_language_offered((string_address) "/tmp/language-check-missing-file");
                if (handle >= 0)
                        unlink(path), close(handle);
                check("A bowl's program is offered the language the person chose, as LANGUAGE and a code of two letters, and English or nothing or a word that is no code offers none",
                      right);
        }
        static string_address keep[] = {"BOWL_CAPABILITIES=keep", null};
        check("A program is given its capabilities only when the environment it is started with asks (all or keep), so bubblewrap and "
              "glycin under it run in a bowl",
              !bowl_capabilities_wanted(null) && !bowl_capabilities_wanted(plain) && bowl_capabilities_wanted(kept) &&
                  bowl_capabilities_wanted(keep) && !bowl_capabilities_wanted(longer) && !bowl_capabilities_wanted(cleared));
        check("Only the word none keeps other bowls' launchers off the PATH: no word, another word and all do not",
              bowl_capabilities_refused(cleared) && !bowl_capabilities_refused(null) && !bowl_capabilities_refused(plain) &&
                  !bowl_capabilities_refused(kept) && !bowl_capabilities_refused(longer));
        {
                static string_address with_expose[] = {"BOWL_CAPABILITIES=none", "PATH=/usr/local/bin:/bin:/usr/bin:" BOWL_EXPOSE_DIRECTORY ":/", "HOME=/root", null};
                static string_address slashes[] = {"BOWL_CAPABILITIES=none", "PATH=" BOWL_EXPOSE_DIRECTORY "/:/usr/bin://:/opt/bin", null};
                static string_address only_expose[] = {"BOWL_CAPABILITIES=none", "PATH=" BOWL_EXPOSE_DIRECTORY ":/", null};
                static string_address clean_none[] = {"BOWL_CAPABILITIES=none", "PATH=/bin:/usr/bin", null};
                static string_address with_caps[] = {"BOWL_CAPABILITIES=all", "PATH=/bin:/usr/bin:" BOWL_EXPOSE_DIRECTORY ":/", null};
                static string_address dup_path[] = {"BOWL_CAPABILITIES=none", "PATH=/bin:" BOWL_EXPOSE_DIRECTORY, "PATH=/bowls/bin:/", null};
                string_address address_to got;
                p8 stripped_ok = true;

                got = bowl_without_exposed_path(with_expose);
                stripped_ok &= got != with_expose && string_equals(bowl_env_value(got, "PATH"), "/usr/local/bin:/bin:/usr/bin") &&
                               string_equals(bowl_env_value(got, "HOME"), "/root");
                got = bowl_without_exposed_path(slashes);
                stripped_ok &= string_equals(bowl_env_value(got, "PATH"), "/usr/bin:/opt/bin");
                got = bowl_without_exposed_path(only_expose);
                stripped_ok &= string_equals(bowl_env_value(got, "PATH"), "/bin:/usr/bin");
                got = bowl_without_exposed_path(dup_path);
                stripped_ok &= string_equals(bowl_env_value(got, "PATH"), "/bin") && got[1] && got[2] == null;
                stripped_ok &= bowl_without_exposed_path(clean_none) == clean_none &&
                               bowl_without_exposed_path(with_caps) == with_caps;
                check("A capability-less bowl strips /bowls/bin and / from PATH so unprivileged wrappers never leak [Bowl] errors to stdout",
                      stripped_ok);
        }
        positive nodes = chrome_line(chrome ? chrome->session : null, "/dev/dri/renderD*");
        string_address grant = nodes ? chrome->session[nodes - 1] : (string_address)"";

        check("The render nodes are given to the render group the programs of a bowl run in, before the browser starts",
              nodes && nodes < exec && strstr(grant, "chown 0:" BOWL_GROUP_RENDER_TEXT " ") &&
                  strstr(grant, "chmod 0660") && !strstr(grant, "card") && !strstr(grant, "0666") &&
                  bowl_device_groups[array_count(bowl_device_groups) - 1] == BOWL_GROUP_RENDER);
        positive session_type = chrome_line(chrome ? chrome->session : null, "XDG_SESSION_TYPE=wayland");

        check("The browser is told the session is Wayland's (XDG_SESSION_TYPE), before it starts, and chooses Wayland itself: no --ozone-platform",
              session_type && session_type < exec && strstr(chrome->session[session_type - 1], "[ -n \"$XDG_SESSION_TYPE\" ] ||") &&
                  exec && !strstr(start, "--ozone-platform"));
        check("The browser is started with its own password store and quiet logs, in the bowl it was added to",
              exec && strstr(start, "--password-store=basic") &&
                  strstr(start, "--log-level=3") && start[string_length(start) - 1] == '"' &&
                  strstr(start, "\"$@\"") && bowl_named_root(BOWL_ROOT_PREFIX "alpine"));
        check("The browser is given no flag against the GPU or the sandbox: it picks its own GPU",
              exec && !strstr(start, "--disable-gpu") && !strstr(start, "--use-gl") && !strstr(start, "--use-angle") &&
                  !strstr(start, "--no-sandbox") && !strstr(start, "--disable-software-rasterizer") &&
                  !strstr(start, "--disable-features") && !strstr(start, "--in-process-gpu"));
        positive encode = chrome_line(chrome ? chrome->session : null,
                                      "wa chrome-vaapi-encode && wa_chrome_flags=\"--enable-features=AcceleratedVideoEncoder\"");
        check("The browser encodes video on the card where it has an encoder (AcceleratedVideoEncoder, under its switch), as it decodes there by default",
              exec && encode && encode < exec && strstr(start, "$wa_chrome_flags ") && strstr(start, "$wa_chrome_flags") < strstr(start, "\"$@\""));
        positive preload = chrome_line(chrome ? chrome->session : null,
                                       "LD_PRELOAD=" BOWL_CHROME_PWRITE_LIBRARY);
        check("The browser preloads its seccomp-safe pwrite shim before starting Chromium, and only Chrome carries that flag",
              preload && preload < exec &&
                  strstr(chrome->session[preload - 1],
                         BOWL_ROOT_PREFIX "alpine" BOWL_CHROME_PWRITE_LIBRARY) &&
                  chrome->pwrite_shim &&
                  bowl_find_profile("steam") &&
                  !bowl_find_profile("steam")->pwrite_shim);
#ifdef BOWL_CHROME_PWRITE_GAP
        {
                bool elf_ok = sizeof(bowl_chrome_pwrite_head) > 64 &&
                              bowl_chrome_pwrite_head[0] == 0x7f &&
                              bowl_chrome_pwrite_head[1] == 'E' &&
                              bowl_chrome_pwrite_head[2] == 'L' &&
                              bowl_chrome_pwrite_head[3] == 'F' &&
                              bowl_chrome_pwrite_head[4] == 2 &&
                              bowl_chrome_pwrite_head[5] == 1 &&
                              bowl_chrome_pwrite_head[16] == 3 &&
                              bowl_chrome_pwrite_head[17] == 0;
#if X64
                elf_ok = elf_ok && bowl_chrome_pwrite_head[18] == 0x3e &&
                         bowl_chrome_pwrite_head[19] == 0;
#elif ARM64
                elf_ok = elf_ok && bowl_chrome_pwrite_head[18] == 0xb7 &&
                         bowl_chrome_pwrite_head[19] == 0;
#elif RISCV64
                elf_ok = elf_ok && bowl_chrome_pwrite_head[18] == 0xf3 &&
                         bowl_chrome_pwrite_head[19] == 0;
#endif
                static const string_address shim_symbols[] = {
                    "__errno_location", "pwrite", "pwrite64",
                    "pwritev", "pwritev64", "pwritev2", null};
                for (positive s = 0; elf_ok && shim_symbols[s]; s++)
                {
                        positive len = string_length(shim_symbols[s]) + 1;
                        bool found = false;

                        for (positive i = 0; i + len <= sizeof(bowl_chrome_pwrite_head); i++)
                        {
                                bool match = true;

                                for (positive j = 0; j < len; j++)
                                        if (bowl_chrome_pwrite_head[i + j] != (p8)shim_symbols[s][j])
                                        {
                                                match = false;
                                                break;
                                        }
                                if (match)
                                {
                                        found = true;
                                        break;
                                }
                        }
                        elf_ok = elf_ok && found;
                }
                check("The embedded Chrome pwrite shim is a valid ELF64 shared object exporting all pwrite variants and __errno_location",
                      elf_ok && BOWL_CHROME_PWRITE_GAP > 0 &&
                          sizeof(bowl_chrome_pwrite_tail) > 0);
        }
#endif

        p8 known = true;

        for (positive at = 0; at < array_count(bowl_profiles); at++)
                known &= strstr(BOWL_PROFILES_KNOWN, bowl_profiles[at].name) != null;
        check("Every profile is named in the words said of an unknown one", known);

#if X64 || ARM64
        check("Chromium is on this machine's architecture: the profile can be installed", chrome && !chrome->unavailable);
#else
        check("Chromium is not on this machine's architecture: the profile says so",
              chrome && chrome->unavailable && string_has_prefix(chrome->unavailable, "Alpine has no Chromium"));
#endif

        /*
                A profile this machine cannot have is refused before a bowl is
                landed, and says so. The component list is empty and the profile
                has no session, so that an install that went on would end in "is
                ready" and return zero, and nothing else of the machine.
        */
        static const struct bowl_component none[] = {{null, null, 0, null}};
        struct bowl_profile gone = {"profgone", "Gone", "next", none, null, false, null,
                                    "Alpine has no profgone here\n", true};
        check("An install of a profile the machine cannot have is refused whole",
              bowl_profile_install(&gone) == 1);
        gone.unavailable = null;
        check("and the same profile with nothing in the way is let through to its steps",
              bowl_profile_install(&gone) == 0);
#if !(X64 || ARM64)
        //      The browser's own words, on a machine that is refused: its components and session are taken out of
        //      the copy, so that a refusal that failed would land nothing and write nothing.
        struct bowl_profile refused = *chrome;

        refused.components = none;
        refused.session = null;
        check("Where Alpine has no Chromium the profile is refused whole, with its own words",
              refused.unavailable && bowl_profile_install(&refused) == 1);
#endif

        /*
                What a component exposes, by name: the programs of a bowl, of
                which one is listed, are not all made reachable; a list of none
                makes none and still leaves the profile's marker.
        */
        {
                p8 root[256];
                p8 other[256];
                p8 listing[256];
                static string_address one[] = {"profxone", null};
                static string_address nothing[] = {null};

                unpack_path(root, "profx", "");
                unpack_path(other, "profy", "");
                string_copy_bounded(listing, BOWL_EXPOSE_DIRECTORY, sizeof listing);
                unpack_directory(root, "/usr/bin");
                unpack_directory(other, "/usr/bin");
                p8 stood = unpack_write(root, "/usr/bin/profxone", "#!/bin/sh\n", 0755) &&
                             unpack_write(root, "/usr/bin/profxtwo", "#!/bin/sh\n", 0755) &&
                             unpack_write(other, "/usr/bin/profxthree", "#!/bin/sh\n", 0755);

                //      Whether the link in /bin could be made is the machine's to say (a
                //      check that cannot write there still has its launchers), so the
                //      launchers are what is looked at, and a link that was made goes
                //      with the launcher it names, below.
                bowl_quiet = true;
                bowl_profile_expose_dir(root, "/usr/bin", -1, one);
                bowl_quiet = false;
                check("A component that names one program exposes that one and not the other",
                      stood && system_access_at(AT_FDCWD, BOWL_EXPOSE_DIRECTORY "/profxone", 0) == 0 &&
                          system_access_at(AT_FDCWD, BOWL_EXPOSE_DIRECTORY "/profxtwo", 0) < 0);

                bowl_quiet = true;
                bowl_profile_expose_dir(root, "/usr/bin", -1, null);
                bowl_quiet = false;
                check("A component that names none exposes every program, and leaves the one that is there alone",
                      system_access_at(AT_FDCWD, BOWL_EXPOSE_DIRECTORY "/profxone", 0) == 0 &&
                          system_access_at(AT_FDCWD, BOWL_EXPOSE_DIRECTORY "/profxtwo", 0) == 0);

                bowl_profile_drop(root, "profxone");
                bowl_profile_drop(root, "profxtwo");
                check("The names of a program of the bowl that is not listed are not asked of the machine at all",
                      bowl_name_listed(one, "profxone") && !bowl_name_listed(one, "profxtwo") &&
                          !bowl_name_listed(nothing, "profxone") && bowl_name_listed(null, "profxone"));

                bowl_quiet = true;
                p8 marked = bowl_profile_expose(other, "profymark", nothing) == 0;
                bowl_quiet = false;
                check("A list of none exposes nothing of a bowl that holds a program, and the profile is marked",
                      marked && bowl_profile_marked(other, "profymark") &&
                          system_access_at(AT_FDCWD, BOWL_EXPOSE_DIRECTORY "/profxthree", 0) < 0);
                bowl_profile_forget(other, "profymark");
                bowl_forget_path(root);
                bowl_forget_path(other);
        }

        /*
                A session under more names. The script of another profile, one
                of a bowl the profile is not installing into, and a name /bin
                has for Moonwater's own program are not taken; no launcher, the
                profile's own script, a launcher of the bowl it installs into,
                and one whose program is gone are.
        */
        {
                static const struct bowl_component here[] = {{"alpine", null, 0, null}, {null, null, 0, null}};
                static string_address aliases[] = {"profalias", null};
                struct bowl_profile mock = {"profmock", "Mock", "next", here, null, false, aliases, null, true};
                struct bowl_profile stranger = {"profstranger", "Stranger", "next", here, null, false, null, null, true};
                p8 foreign[256];
                p8 own[256];
                p8 target[256];
                p8 stood;

                unpack_path(foreign, "profz", "");
                unpack_directory(foreign, "/usr/bin");
                unpack_directory(BOWL_ROOT_PREFIX "alpine", "/usr/bin");
                string_copy_bounded(own, BOWL_ROOT_PREFIX "alpine", sizeof own);
                stood = unpack_write(foreign, "/usr/bin/tool", "#!/bin/sh\n", 0755) &&
                        unpack_write(own, "/usr/bin/tool", "#!/bin/sh\n", 0755);
                check("Two bowls hold a program each, for the session's names to be asked about", stood);

                check("A name nobody has is free for the session",
                      bowl_session_name_free(&mock, "profalias"));
                check("A name Moonwater's own /bin has is not",
                      !bowl_session_name_free(&mock, "sh"));
                check("The session is written under another name with the profile's own header and its own name to bowl",
                      bowl_profile_script_as(&mock, "profalias") == 0 &&
                          unpack_says("", BOWL_EXPOSE_DIRECTORY "/profalias",
                                      "#!/bin/sh\n# bowl profile profmock\n"
                                      "exec bowl profile profmock session \"$@\"\n") &&
                          (unpack_mode("", BOWL_EXPOSE_DIRECTORY "/profalias") & 0111));
                check("and that script is free for the same profile again, and not for another",
                      bowl_session_name_free(&mock, "profalias") && !bowl_session_name_free(&stranger, "profalias"));

                bowl_profile_script_drop(&stranger, "profalias");
                check("A profile's remove leaves a script another profile wrote",
                      system_access_at(AT_FDCWD, BOWL_EXPOSE_DIRECTORY "/profalias", 0) == 0);
                bowl_profile_script_drop(&mock, "profalias");
                check("and takes its own, under the name it was written",
                      system_access_at(AT_FDCWD, BOWL_EXPOSE_DIRECTORY "/profalias", 0) < 0);

                bowl_quiet = true;
                bowl_expose_program(foreign, "/usr/bin/tool", "profalias", false);
                bowl_quiet = false;
                check("A launcher of a bowl the profile does not install into, whose program is there, is not taken",
                      !bowl_session_name_free(&mock, "profalias"));
                system_remove_at(AT_FDCWD, BOWL_EXPOSE_DIRECTORY "/profalias", 0);

                bowl_quiet = true;
                bowl_expose_program(own, "/usr/bin/tool", "profalias", false);
                bowl_quiet = false;
                check("A launcher of the bowl the profile installs into (a manager run by hand made it) is taken",
                      bowl_session_name_free(&mock, "profalias"));
                system_remove_at(AT_FDCWD, BOWL_EXPOSE_DIRECTORY "/profalias", 0);

                bowl_quiet = true;
                bowl_expose_program(foreign, "/usr/bin/tool", "profalias", false);
                bowl_quiet = false;
                string_copy_bounded(target, foreign, sizeof target);
                string_append_bounded(target, "/usr/bin/tool", sizeof target);
                system_remove_at(AT_FDCWD, target, 0);
                check("and so is one whose program is gone",
                      bowl_session_name_free(&mock, "profalias"));
                system_remove_at(AT_FDCWD, BOWL_EXPOSE_DIRECTORY "/profalias", 0);
                bowl_forget_path(foreign);
                // The check's own Alpine root holds a file of ours and is left standing.
                string_copy_bounded(target, own, sizeof target);
                string_append_bounded(target, "/usr/bin/tool", sizeof target);
                system_remove_at(AT_FDCWD, target, 0);
        }

        /*
                Remove leaves to another installed profile what it holds as
                well: a bowl whose graphics marker is there keeps Mesa when the
                browser goes, and a package only the browser holds is the
                browser's to remove.
        */
        {
                p8 root[256];

                unpack_path(root, "profs", "");
                unpack_directory(root, BOWL_PROFILE_STATE);
                check("With no other profile installed in the bowl, nothing the browser holds is shared",
                      chrome && !bowl_profile_package_shared(chrome, "alpine", root, "mesa-egl") &&
                          !bowl_profile_package_shared(chrome, "alpine", root, "chromium"));
                check("A marker of the graphics profile makes its packages shared, and not the browser's own",
                      unpack_write(root, BOWL_PROFILE_STATE "/graphics", "", 0644) &&
                          bowl_profile_package_shared(chrome, "alpine", root, "mesa-egl") &&
                          bowl_profile_package_shared(chrome, "alpine", root, "font-dejavu") &&
                          !bowl_profile_package_shared(chrome, "alpine", root, "chromium"));
                check("A marker of the profile itself shares nothing with it, and one of another distribution shares nothing here",
                      unpack_write(root, BOWL_PROFILE_STATE "/chrome", "", 0644) &&
                          !bowl_profile_package_shared(chrome, "alpine", root, "chromium") &&
                          !bowl_profile_package_shared(chrome, "arch", root, "mesa-egl"));
                check("Only a profile that is installed in the bowl shares: the browser's marker is there, the ssh server's is not",
                      !bowl_profile_package_shared(graphics, "alpine", root, "openssh-server") &&
                          bowl_profile_package_shared(graphics, "alpine", root, "chromium"));
                bowl_forget_path(root);
        }
}

static fn json(void)
{
        //      Fedora 44's own index.json and manifest, as published.
        static const p8 index[] =
            "{\"schemaVersion\":2,\"mediaType\":\"application/vnd.oci.image.index.v1+json\","
            "\"manifests\":[{\"mediaType\":\"application/vnd.oci.image.manifest.v1+json\","
            "\"digest\":\"sha256:f1e66cdd6eff2c9ccad192f8865af9be6d69b46b3f13329d2975a2d61a1296c5\","
            "\"size\":572,\"annotations\":{\"org.opencontainers.image.ref.name\":\"fedora:44\"}}]}";
        static const p8 manifest[] =
            "{\"schemaVersion\":2,\"mediaType\":\"application/vnd.oci.image.manifest.v1+json\","
            "\"config\":{\"mediaType\":\"application/vnd.oci.image.config.v1+json\","
            "\"digest\":\"sha256:9eaa56a730854ae53f57e58c16179d09c2fa1f4157a5d69ccb39647e33c6464d\","
            "\"size\":819},\"layers\":[{\"mediaType\":\"application/vnd.oci.image.layer.v1.tar+gzip\","
            "\"digest\":\"sha256:d93cacdefe7f7526297a08879cb444d1eeb526adf9cb7e34ab1f0609759a3044\","
            "\"size\":70935316}],\"annotations\":{\"org.opencontainers.image.base.digest\":\"\","
            "\"org.opencontainers.image.base.name\":\"\","
            "\"org.opencontainers.image.created\":\"2026-04-22T13:55:28.538633259Z\"}}";
        static const p8 platforms[] =
            " { \"manifests\" : [ {\"mediaType\":\"application/vnd.oci.image.manifest.v1+json\","
            "\"digest\":\"sha256:1\",\"platform\":{\"architecture\":\"s390x\",\"os\":\"linux\"}},"
            "{\"mediaType\":\"application/vnd.oci.image.manifest.v1+json\",\"digest\":\"sha256:2\","
            "\"platform\":{\"os\":\"linux\",\"architecture\":\"" BOWL_OCI_ARCHITECTURE "\"}} ] }\n";
        static string_address broken[] = {
            "", "{", "{\"a\":1", "{\"a\":1}x", "{\"a\" 1}", "[1,]", "{\"a\":tru}",
            "\"\\x\"", "\"a\nb\"", "{\"a\":1,}", "[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]",
        };
        string_address stop = index + sizeof(index) - 1;
        string_address chosen = bowl_oci_pick(index, stop);
        string_address cursor;
        string_address layer;
        p8 text[128];
        p64 size = 0;
        positive layers = 0;

        check("Fedora's index and manifest are whole JSON",
              bowl_json_whole(index, stop) &&
                  bowl_json_whole(manifest, manifest + sizeof(manifest) - 1));
        check("Fedora's index names its one manifest",
              chosen && bowl_oci_type_is(chosen, stop, bowl_oci_manifest_types) &&
                  bowl_json_text(bowl_json_member(chosen, stop, "digest"), stop,
                                 text, sizeof text) &&
                  string_equals(text, "sha256:f1e66cdd6eff2c9ccad192f8865af9be6d69b46b3f13329d2975a2d61a1296c5") &&
                  bowl_json_count(bowl_json_member(chosen, stop, "size"), stop,
                                  address_of size) &&
                  size == 572);

        stop = manifest + sizeof(manifest) - 1;
        cursor = bowl_json_member(manifest, stop, "layers");
        while ((layer = bowl_json_next(address_of cursor, stop)))
                layers += bowl_oci_type_is(layer, stop, bowl_oci_layer_types) &&
                          bowl_json_count(bowl_json_member(layer, stop, "size"),
                                          stop, address_of size) &&
                          size == 70935316;
        check("Fedora's manifest has one gzip layer of the size it says",
              layers == 1);
        check("A member not there is not found, and the config is not a layer",
              !bowl_json_member(manifest, stop, "layer") &&
                  !bowl_oci_type_is(bowl_json_member(manifest, stop, "config"),
                                    stop, bowl_oci_layer_types));

        stop = platforms + sizeof(platforms) - 1;
        chosen = bowl_oci_pick(platforms, stop);
        check("An index with platforms gives this machine's manifest",
              bowl_json_whole(platforms, stop) && chosen &&
                  bowl_json_text(bowl_json_member(chosen, stop, "digest"), stop,
                                 text, sizeof text) &&
                  string_equals(text, "sha256:2"));

        for (positive at = 0; at < array_count(broken); at++)
                check("Malformed JSON is refused whole",
                      !bowl_json_whole(broken[at],
                                       broken[at] + string_length(broken[at])));

        string_address escaped = "\"a\\u003cb\\/c\\\"\"";
        string_address control = "\"a\\u0001\"";
        string_address wide = "\"\\u00e9\"";

        check("A string's ASCII escapes are read, and nothing else is",
              bowl_json_text(escaped, escaped + string_length(escaped), text,
                             sizeof text) &&
                  string_equals(text, "a<b/c\"") &&
                  !bowl_json_text(control, control + string_length(control),
                                  text, sizeof text) &&
                  !bowl_json_text(wide, wide + string_length(wide), text,
                                  sizeof text) &&
                  !bowl_json_text(escaped, escaped + string_length(escaped),
                                  text, 4));
}

static fn oci(void)
{
        static const p8 manifest_type[] = "application/vnd.oci.image.manifest.v1+json";
        static string_address layer_one[] = {"a", "opq", "d", "esc", "lnk", null};
        static string_address layer_two[] = {"a", "opq", "d", "new", "esc", "lnk",
                                             ".wh.lnk", null};
        static string_address layout_members[] = {"index.json", "oci-layout", "blobs", null};
        p8 base[256];
        p8 root[256];
        p8 spoiled[256];
        p8 layout[256];
        p8 path[256];
        p8 image[256];
        p8 bad[256];
        p8 layers[512] = "";
        p8 document[2048];
        p8 descriptor[256] = "";
        p8 second[BOWL_DIGEST_HEX + 1] = "";
        p8 outside[256];

        unpack_path(base, "ociwork", "");
        unpack_path(root, "oci", "");
        unpack_path(spoiled, "ocibad", "");
        unpack_path(outside, "ociout", "");
        string_copy_bounded(layout, base, sizeof layout);
        string_append_bounded(layout, "/oci", sizeof layout);
        string_copy_bounded(image, base, sizeof image);
        string_append_bounded(image, "/image.tar", sizeof image);
        string_copy_bounded(bad, base, sizeof bad);
        string_append_bounded(bad, "/bad.tar", sizeof bad);

        unpack_directory(base, "/one/a/sub");
        unpack_directory(base, "/one/opq");
        unpack_directory(base, "/one/d");
        unpack_directory(base, "/two/a");
        unpack_directory(base, "/two/opq");
        unpack_directory(base, "/two/d");
        unpack_directory(base, "/two/new");
        unpack_directory(base, "/oci/blobs/sha256");
        unpack_directory(root, "");
        unpack_directory(spoiled, "");
        unpack_directory(outside, "");

        p8 made = unpack_write(base, "/one/a/keep", "one", 0644) &&
                    unpack_write(base, "/one/a/gone", "gone", 0644) &&
                    unpack_write(base, "/one/a/sub/x", "x", 0644) &&
                    unpack_write(base, "/one/opq/old", "old", 0644) &&
                    unpack_write(base, "/one/d/f", "f", 0644) &&
                    unpack_write(base, "/two/a/keep", "two", 0644) &&
                    unpack_write(base, "/two/a/.wh.gone", "", 0644) &&
                    unpack_write(base, "/two/opq/.wh..wh..opq", "", 0644) &&
                    unpack_write(base, "/two/opq/new", "new", 0644) &&
                    unpack_write(base, "/two/new/.wh.ghost", "", 0644) &&
                    unpack_write(base, "/two/new/n", "n", 0644) &&
                    unpack_write(outside, "/victim", "victim", 0644);

        //      The lower layer's links point out of the bowl: one a directory
        //      the upper layer writes into, one a file the upper layer both
        //      whites out and writes again. Landing follows neither.
        unpack_directory(base, "/two/esc");
        made &= unpack_write(base, "/two/esc/pwned", "pwned", 0644) &&
                unpack_write(base, "/two/lnk", "lnk", 0644) &&
                unpack_write(base, "/two/.wh.lnk", "", 0644);
        string_copy_bounded(path, base, sizeof path);
        string_append_bounded(path, "/one", sizeof path);
        {
                bipolar one = system_open_at(AT_FDCWD, path,
                                             FILE_READ | O_DIRECTORY | O_CLOEXEC);
                p8 victim[256];

                string_copy_bounded(victim, outside, sizeof victim);
                string_append_bounded(victim, "/victim", sizeof victim);
                made &= one >= 0 &&
                        system_symbolic_link_at(outside, one, "esc") == 0 &&
                        system_symbolic_link_at(victim, one, "lnk") == 0;
                if (one >= 0)
                        system_close(one);
        }

        string_copy_bounded(path, base, sizeof path);
        string_append_bounded(path, "/two/d", sizeof path);
        made &= system_change_mode_at(AT_FDCWD, path, 0700) == 0;

        string_copy_bounded(path, base, sizeof path);
        string_append_bounded(path, "/one", sizeof path);
        string_copy_bounded(document, base, sizeof document);
        string_append_bounded(document, "/one.tar", sizeof document);
        made &= unpack_tar(document, path, false, layer_one) &&
                unpack_blob(layout, document, "application/vnd.oci.image.layer.v1.tar",
                            layers, sizeof layers, null);
        string_append_bounded(layers, ",", sizeof layers);
        string_copy_bounded(path, base, sizeof path);
        string_append_bounded(path, "/two", sizeof path);
        string_copy_bounded(document, base, sizeof document);
        string_append_bounded(document, "/two.tar.gz", sizeof document);
        made &= unpack_tar(document, path, true, layer_two) &&
                unpack_blob(layout, document,
                            "application/vnd.oci.image.layer.v1.tar+gzip", layers,
                            sizeof layers, second);

        string_copy_bounded(document, "{\"schemaVersion\":2,\"mediaType\":\"", sizeof document);
        string_append_bounded(document, manifest_type, sizeof document);
        string_append_bounded(document, "\",\"layers\":[", sizeof document);
        string_append_bounded(document, layers, sizeof document);
        string_append_bounded(document, "]}", sizeof document);
        made &= unpack_write(base, "/manifest.json", document, 0644);
        string_copy_bounded(path, base, sizeof path);
        string_append_bounded(path, "/manifest.json", sizeof path);
        made &= unpack_blob(layout, path, manifest_type, descriptor, sizeof descriptor,
                            null);

        //      A manifest for another machine comes first, naming a blob that
        //      is not there: taking it would fail the landing.
        string_copy_bounded(document,
                            "{\"schemaVersion\":2,\"manifests\":[{\"mediaType\":\"", sizeof document);
        string_append_bounded(document, manifest_type, sizeof document);
        string_append_bounded(document,
                              "\",\"digest\":\"sha256:0000000000000000000000000000000000000000000000000000000000000000\","
                              "\"size\":1,\"platform\":{\"architecture\":\"s390x\",\"os\":\"linux\"}},",
                              sizeof document);
        string_append_bounded(document, descriptor, sizeof document);
        string_append_bounded(document, "]}", sizeof document);
        made &= unpack_write(layout, "/index.json", document, 0644) &&
                unpack_write(layout, "/oci-layout", "{\"imageLayoutVersion\":\"1.0.0\"}", 0644) &&
                unpack_tar(image, layout, false, layout_members);

        //      The same layout with its second layer's bytes changed under
        //      the digest that names them.
        {
                string_copy_bounded(path, "/blobs/sha256/", sizeof path);
                string_append_bounded(path, second, sizeof path);
                made &= unpack_write(layout, path, "not the layer", 0644) &&
                        unpack_tar(bad, layout, false, layout_members);
        }

        check("A two-layer OCI image is made for the check", made);
        if (!made)
                return;

        check("A two-layer OCI image lands",
              bowl_extract_oci(image, root) == 0);
        check("A later layer's file replaces an earlier one's",
              unpack_says(root, "/a/keep", "two") && bowl_has(root, "/a/sub/x"));
        check("A whiteout takes a lower file away and is not left",
              !bowl_has(root, "/a/gone") && !bowl_has(root, "/a/.wh.gone"));
        check("An opaque directory hides what was under it and keeps its own",
              !bowl_has(root, "/opq/old") && unpack_says(root, "/opq/new", "new") &&
                  !bowl_has(root, "/opq/.wh..wh..opq"));
        check("A whiteout in a new directory is dropped",
              unpack_says(root, "/new/n", "n") && !bowl_has(root, "/new/.wh.ghost"));
        check("A lower layer's link out of the bowl is not written through",
              !bowl_has(outside, "/pwned") && unpack_says(outside, "/victim", "victim") &&
                  unpack_says(root, "/esc/pwned", "pwned") &&
                  unpack_says(root, "/lnk", "lnk"));
        check("A directory in both layers takes the upper one's mode",
              unpack_mode(root, "/d") == 0700 && unpack_says(root, "/d/f", "f"));
        string_copy_bounded(path, root, sizeof path);
        string_append_bounded(path, ".oci", sizeof path);
        check("The layout and the download are gone after landing",
              system_access_at(AT_FDCWD, path, 0) < 0 &&
                  system_access_at(AT_FDCWD, image, 0) < 0);

        check("A layer that is not what its digest says is refused",
              bowl_extract_oci(bad, spoiled) != 0 &&
                  !bowl_has(spoiled, "/opq/new"));

        bowl_forget_path(root);
        bowl_forget_path(spoiled);
        bowl_forget_path(base);
        bowl_forget_path(outside);
}

static fn nix(void)
{
        static string_address paths[] = {
            "/nix/store/0123456789abcdfghijklmnpqrsvwxyz-nix-2.35.2",
            "/nix/store/0123456789abcdfghijklmnpqrsvwxyz-nix-2.35.2/bin",
            "/nix/store/0123456789abcdefghijklmnpqrsvwxyz-nix",
            "/nix/store/0123456789abcdfghijklmnpqrsvwxy-nix",
            "/nix/store/0123456789abcdfghijklmnpqrsvwxyz",
            "/nix/store/0123456789abcdfghijklmnpqrsvwxyz-a\"b",
            "/nix/stor/0123456789abcdfghijklmnpqrsvwxyz-nix",
        };
        static string_address members[] = {
            "nix-9.9-x86_64-linux/install", "nix-9.9-x86_64-linux/.reginfo",
            "nix-9.9-x86_64-linux/install-multi-user",
            "nix-9.9-x86_64-linux/store/0123456789abcdfghijklmnpqrsvwxyz-nix-9.9/bin/nix",
            null};
        static const p8 script[] =
            "#!/bin/sh\n\nset -e\n"
            "nix=\"/nix/store/0123456789abcdfghijklmnpqrsvwxyz-nix-9.9\"\n"
            "cacert=\"/nix/store/zyxwvsrqpnmlkjihgfdcba9876543210-nss-cacert-1.0\"\n";
        p8 base[256];
        p8 root[256];
        p8 broken[256];
        p8 archive[256];
        p8 other[256];
        p8 found[BOWL_NIX_PATH];
        p8 cacert[BOWL_NIX_PATH];

        for (positive at = 0; at < array_count(paths); at++)
                check("A Nix store path is a hash of 32 base-32 digits and a name",
                      bowl_nix_store_path(paths[at], string_length(paths[at])) ==
                          (at == 0));

        check("The installer's nix and cacert lines are read",
              bowl_nix_assigned(script, sizeof script - 1, "nix", found) &&
                  string_equals(found, "/nix/store/0123456789abcdfghijklmnpqrsvwxyz-nix-9.9") &&
                  bowl_nix_assigned(script, sizeof script - 1, "cacert", cacert) &&
                  !bowl_nix_assigned(script, sizeof script - 1, "ni", found));

        unpack_path(base, "nixwork", "");
        unpack_path(root, "nix", "");
        unpack_path(broken, "nixbad", "");
        string_copy_bounded(archive, base, sizeof archive);
        string_append_bounded(archive, "/release.tar", sizeof archive);
        string_copy_bounded(other, base, sizeof other);
        string_append_bounded(other, "/other.tar", sizeof other);

        unpack_directory(base, "/nix-9.9-x86_64-linux/store/"
                               "0123456789abcdfghijklmnpqrsvwxyz-nix-9.9/bin");
        p8 made = unpack_write(base, "/nix-9.9-x86_64-linux/install", script, 0755) &&
                    unpack_write(base, "/nix-9.9-x86_64-linux/.reginfo", "registered\n", 0644) &&
                    unpack_write(base, "/nix-9.9-x86_64-linux/install-multi-user", "", 0755) &&
                    unpack_write(base, "/nix-9.9-x86_64-linux/store/"
                                       "0123456789abcdfghijklmnpqrsvwxyz-nix-9.9/bin/nix",
                                 "#!/bin/sh\n", 0755) &&
                    //      Files only: the release lists no directories.
                    unpack_tar(archive, base, false, members) &&
                    unpack_write(base, "/nix-9.9-x86_64-linux/install", "#!/bin/sh\n", 0755) &&
                    unpack_tar(other, base, false, members);

        check("A Nix release is made for the check", made);
        if (!made)
                return;

        check("A Nix release lands as a store at /nix/store",
              bowl_land(archive, root, "/nix/.reginfo", bowl_extract_nix) == 0 &&
                  bowl_has(root, "/nix/.reginfo") &&
                  bowl_has(root, "/nix/store/0123456789abcdfghijklmnpqrsvwxyz-nix-9.9/bin/nix"));
        check("and nothing of the release but its store is left",
              !bowl_has(root, "/install") && !bowl_has(root, "/install-multi-user") &&
                  !bowl_has(root, "/store") && !bowl_has(root, "/.reginfo"));
        check("The installer's two paths are kept for the profile",
              bowl_nix_read_seed(root, found, cacert) &&
                  string_equals(found, "/nix/store/0123456789abcdfghijklmnpqrsvwxyz-nix-9.9") &&
                  string_equals(cacert, "/nix/store/zyxwvsrqpnmlkjihgfdcba9876543210-nss-cacert-1.0"));
        check("The root and the store tar made are opened to everyone",
              unpack_mode(root, "") == 0755 && unpack_mode(root, "/nix/store") == 0755);
        check("A single-user Nix has its conf, its users and its channel",
              bowl_has(root, "/etc/nix/nix.conf") && bowl_has(root, "/etc/passwd") &&
                  bowl_has(root, "/etc/group") && bowl_has(root, "/root/.nix-channels") &&
                  bowl_has(root, "/etc/resolv.conf"));
        {
                static const struct
                {
                        string_address host;
                        string_address want;
                } resolv_case[] = {
                    {"nameserver 10.0.0.1\nnameserver 10.0.0.2\n",
                     "nameserver 10.0.0.1\nnameserver 10.0.0.2\n"},
                    {"", "nameserver 1.1.1.1\n"},
                    {"nameserver 127.0.0.53\n", "nameserver 1.1.1.1\n"},
                    {"search lan\n# nameserver 9.9.9.9\nnameserver 10.0.0.1\n",
                     "nameserver 10.0.0.1\n"},
                    /* the machine's own choice is the bowl's, public one included
                       and where it put it, not behind a second 1.1.1.1 */
                    {"nameserver 9.9.9.9\nnameserver 1.1.1.1\n",
                     "nameserver 9.9.9.9\nnameserver 1.1.1.1\n"},
                };
                p8 same = true;

                for (positive at = 0; at < array_count(resolv_case); at++)
                {
                        p8 buffer[256];
                        byte_store out = {buffer, sizeof(buffer), 0};
                        positive length = string_length(resolv_case[at].host);

                        same &= bowl_resolv_compose(address_of out,
                                                    (p8 address_to)resolv_case[at].host,
                                                    (bipolar)length) &&
                                out.used == string_length(resolv_case[at].want) &&
                                !memory_compare(out.bytes, resolv_case[at].want, out.used);
                }
                check("A bowl's resolv.conf is the machine's servers in order, and the public one only when it names none", same);
        }
        check("A conf the person changed is left alone",
              unpack_write(root, "/etc/nix/nix.conf", "sandbox = true\n", 0644) &&
                  bowl_configure(root) == 0 &&
                  unpack_says(root, "/etc/nix/nix.conf", "sandbox = true\n"));

        check("A release whose installer names no store paths is refused",
              bowl_land(other, broken, "/nix/.reginfo", bowl_extract_nix) != 0 &&
                  !bowl_has(broken, "/nix/.reginfo"));

        bowl_forget_path(root);
        bowl_forget_path(broken);
        bowl_forget_path(base);
}

/*
        A program is asked for the way it will run, inside its root. A Nix
        profile's commands are links to /nix/store, which from the host name
        nothing; asking the host path refused every one of them.
*/
static fn executable_in_root(void)
{
        p8 root[256];
        p8 path[256];

        unpack_path(root, "exec", "");
        unpack_directory(root, "/opt/real");
        unpack_directory(root, "/bin");
        p8 made = unpack_write(root, "/opt/real/tool", "#!/bin/sh\n", 0755) &&
                    unpack_write(root, "/opt/real/data", "data\n", 0644);

        string_copy_bounded(path, root, sizeof path);
        string_append_bounded(path, "/bin/tool", sizeof path);
        made &= system_symbolic_link_at("/opt/real/tool", AT_FDCWD, path) == 0;
        string_copy_bounded(path, root, sizeof path);
        string_append_bounded(path, "/bin/data", sizeof path);
        made &= system_symbolic_link_at("/opt/real/data", AT_FDCWD, path) == 0;

        check("A program behind an absolute link runs inside its root",
              made && bowl_executable_in_root(root, "/bin/tool") == 0 &&
                  bowl_executable_in_root(root, "/bin/data") < 0 &&
                  bowl_executable_in_root(root, "/bin/none") < 0);

        bowl_forget_path(root);
}

/*
        An isolated bowl's root is bound onto itself before it becomes /, and
        that bind carried every mount below it: bound MS_REC, a tmpfs, a disk
        or a host directory an operator (or a guest, last time) had mounted
        under /bowls/NAME came into the view the guest pivots into, against
        bowl_bind_ro's rule that a bowl root is a tree and not a tree of
        mounts. The real bowl_isolated_enter is run in a namespace of the
        check's own with a tmpfs mounted below the root; the pivot is made
        before populate asks for devtmpfs, which the namespace refuses, so
        what the guest would see is asked right after it.
*/
static fn isolated_root_children(void)
{
        positive user = (positive)system_call(syscall(getuid));
        positive group = (positive)system_call(syscall(getgid));
        p8 root[96] = "/tmp/bowl-children-";
        bipolar child;

        root[19 + positive_into(root + 19, (positive)system_call(syscall(getpid)))] = end;
        if (system_make_directory_at(AT_FDCWD, root, 0700) < 0)
                return;
        child = system_fork();
        if (child == 0)
        {
                p8 below[128];
                bipolar made;

                if (system_call_1(syscall(unshare), CLONE_NEWUSER | CLONE_NEWNS) < 0 ||
                    !namespace_root(user, group) ||
                    system_mount(0, "/", 0, MS_REC | MS_PRIVATE, 0) ||
                    system_mount("tmpfs", root, "tmpfs", 0, 0))
                        exit(77);
                string_copy_bounded(below, root, sizeof below);
                string_append_bounded(below, "/below", sizeof below);
                if (system_make_directory_at(AT_FDCWD, below, 0755) < 0 ||
                    system_mount("tmpfs", below, "tmpfs", 0, 0))
                        exit(77);
                string_append_bounded(below, "/mounted", sizeof below);
                made = system_open_at_mode(AT_FDCWD, below, FILE_WRITE | O_CLOEXEC, 0644);
                if (made < 0)
                        exit(77);
                system_close(made);

                (void)bowl_isolated_enter(root);
                if (system_access_at(AT_FDCWD, "/below", 0) < 0)
                        exit(2);
                exit(system_access_at(AT_FDCWD, "/below/mounted", 0) == 0 ? 1 : 0);
        }

        positive status = 0;
        b32 code = child >= 0 &&
                           system_wait4_retry(child, address_of status, 0, null) >= 0
                       ? wait_status_code(status)
                       : 77;

        system_remove_at(AT_FDCWD, root, AT_REMOVEDIR);
        if (code == 77)
        {
                log_direct(str("bowl: an isolated bowl's pivot NOT RUN -- "
                               "no user and mount namespace for the check\n"));
                return;
        }
        check("An isolated bowl pivots into its root", code != 2);
        check("An isolated bowl does not carry a mount below its root into the view",
              code == 0);
}

/*
        The user's view of a bowl, over a disk the machine mounted nodev. The
        data partition of an installed machine is, and /bowls is a bind of it,
        so every mount a launch inherits from the machine has nodev locked in
        the user namespace the launch makes, and bowl_bind_ro's remount to
        read-only, which said nothing of nodev, was refused with a bare EPERM:
        every program of every bowl, on the installed disk and not on the live
        image, where /bowls is a tmpfs and the lane's bowls disk had no such
        flag. The check is the same shape with no disk and no network: a
        tmpfs mounted nodev in a namespace of the check's own, a second user
        namespace as bowl_launch makes it, and the real bowl_bind_ro on a
        directory of the tmpfs, which must be read-only and still nodev.
*/
static fn fast_view_nodev(void)
{
        positive user = (positive)system_call(syscall(getuid));
        positive group = (positive)system_call(syscall(getgid));
        p8 root[96] = "/tmp/bowl-nodev-";
        bipolar child;

        root[16 + positive_into(root + 16, (positive)system_call(syscall(getpid)))] = end;
        if (system_make_directory_at(AT_FDCWD, root, 0700) < 0)
                return;
        child = system_fork();
        if (child == 0)
        {
                p8 source[128];
                p8 target[128];
                file_mount_facts facts;

                if (system_call_1(syscall(unshare), CLONE_NEWUSER | CLONE_NEWNS) < 0 ||
                    !namespace_root(user, group) ||
                    system_mount(0, "/", 0, MS_REC | MS_PRIVATE, 0) ||
                    system_mount("tmpfs", root, "tmpfs", MS_NODEV, 0))
                        exit(77);
                string_copy_bounded(source, root, sizeof source);
                string_append_bounded(source, "/source", sizeof source);
                string_copy_bounded(target, root, sizeof target);
                string_append_bounded(target, "/target", sizeof target);
                if (system_make_directory_at(AT_FDCWD, source, 0755) < 0 ||
                    system_make_directory_at(AT_FDCWD, target, 0755) < 0)
                        exit(77);

                //      The hardened tier (tight) drops to a real uid 1000 with the
                //      device groups before it makes its namespace, which needs the
                //      machine's root and setgroups, and a check in a user namespace of
                //      its own has neither: bowl_become_user_hardened refused and
                //      ended the child with 1, which the row read as a bind that was
                //      not read-only. The namespace that tier then makes is the same
                //      nested one, made here with the id the check has.
                if (MOONWATER_STRICT_BOWL >= STRICT_TIGHT)
                {
                        if (system_call_1(syscall(unshare), CLONE_NEWUSER | CLONE_NEWNS) < 0 ||
                            !namespace_root(0, 0))
                                exit(77);
                }
                else
                {
                        bowl_become_user();
                        if (system_call(syscall(getuid)) != BOWL_USER_ID ||
                            system_call_1(syscall(unshare), CLONE_NEWNS) < 0)
                                exit(77);
                }

                if (bowl_bind_ro(source, target))
                        exit(3);
                if (system_call_2(syscall(statfs), (positive)target,
                                  (positive)address_of facts) < 0)
                        exit(4);
                exit((facts.flags & MS_NODEV) && (facts.flags & MS_RDONLY) ? 0 : 5);
        }

        positive status = 0;
        b32 code = child >= 0 &&
                           system_wait4_retry(child, address_of status, 0, null) >= 0
                       ? wait_status_code(status)
                       : 77;

        system_remove_at(AT_FDCWD, root, AT_REMOVEDIR);
        if (code == 77)
        {
                log_direct(str("bowl: the read-only view over a nodev disk NOT RUN -- "
                               "no user and mount namespace for the check\n"));
                return;
        }
        check("A user's view binds a bowl's tree read-only over a disk mounted nodev",
              code != 3);
        check("and the bind is read-only and still nodev", code == 0);
}

/*
        What a launch that fails says. It said "/bowls/alpine: -1", the number
        of the error and nothing of the step, to a person at a console whose
        desktop would not start; it says what the kernel says, and the step.
*/
static fn launch_failure_words(void)
{
        p8 said[512] = {0};
        b32 ends[2];
        bipolar child;

        if (system_pipe(ends, O_CLOEXEC) < 0)
                return;
        child = system_fork();
        if (child == 0)
        {
                system_call_3(syscall(dup3), (positive)ends[1], 1, 0);
                bowl_fail("/bowls/alpine", -ERROR_NOT_PERMITTED);
                bowl_doing("make read-only", "/bowls/alpine/lib");
                bowl_fail_step("/bowls/alpine", -ERROR_NOT_PERMITTED);
                log_flush();
                exit(0);
        }
        system_close(ends[1]);
        positive used = 0;
        for (bipolar got; used < sizeof said - 1 &&
             (got = system_read_once(ends[0], said + used, sizeof said - 1 - used)) > 0;)
                used += (positive)got;
        system_close(ends[0]);
        system_wait4_retry(child, null, 0, null);

        check("A failed launch says the kernel's words and not the number",
              string_search(said, "Operation not permitted") &&
                  !string_search(said, ": -1"));
        check("and the step it was on, with the path",
              string_search(said, "cannot make read-only /bowls/alpine/lib"));
}

/*
        The downloads a setup takes, against a mirror that lies. The bytes a
        download leaves are checked here as bowl_setup_download checks them
        (the digest or the key a row pins), on files: right bytes, one byte
        wrong, one byte short, one byte long, another release's bytes, and
        for a signature an older signed archive (the replay a floor refuses),
        another key's signature, a bit flipped in the signature, a packet cut
        short, junk after it and a critical subpacket this does not know. The
        signatures are gpg's own, made with a throwaway 2048-bit key; every
        pinned row is checked for having a digest or a key, and the Arch Linux
        ARM key for being the key its fingerprint names.
*/
static p8 put_file(string_address path, const p8 address_to bytes,
                     positive length)
{
        bipolar handle = system_open_at_mode(
            AT_FDCWD, path, O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0600);
        p8 done = handle >= 0 &&
                    system_write_all((positive)handle, bytes, length) == length;

        if (handle >= 0)
                system_close(handle);
        return done;
}

static fn downloads(void)
{
        static const p8 archive[] = "moonwater bowl test archive\n";
        static const struct bowl_key signer = {
            "5F40E0EDD18BC9772C6354D481303DFF244CBCB8", 1785542400, 65537,
            "f1e4a224eedcd103607b82d65374a0d08d22a8d70f84379d16f8f047e4bc0873"
            "576ff2e3ded41edfaeb9f3552b9abc5b7a4f2f38f15a9b7b850620884eaa2c71"
            "5724d899f7a232bac78ce30302202af58e69a41a1b3507de4c27d91c21a0797d"
            "e0368e3c5f867d61ef4a6026461df7e215d82e123e8e87c34c5d5c9389034a4d"
            "5ecc61ccc209e9462fc7104d5ad2a55229d0e6ccdedd7ac7d4decf99743544dc"
            "e71e545a7085b6be0944af85a90b7a662321024f2fb3da60f702b1cf5770986d"
            "e8e6f00ba0d01c24ab774e76bddb0f185eb7053a56e1d79d9e993b4a6a38b230"
            "b50053004d433625bdfb7332463f81e8af60edbad759167439fd94379692ad4f",
            0};
        static const struct bowl_key other = {
            "F3FACE255553013000D17F663396A190CC8CC5BB", 1785542400, 65537,
            "b18cae83b331b6f3a64a7173d37a9ea50fb182a14a369fb20b097b7d0517fae7"
            "e69118340d3155163d1e70139b933da728b9649b0019a2b8ae2b685113f54adc"
            "4f962d2c91189b3501dd7649ff88318975b7a426ceb0694c2475bf86821fb4fa"
            "4ed9bd02de402f0a6657d6a762ac668b663d4c2537db6940f9436e6bb56c9376"
            "e37a57d3f6346994e98dd1673901f0631619b33c1c22aac3b3312c8982d188e2"
            "6f7ed846bdccc8cece966eff85f845aad626c46e9bb6d2fe1d096f7a75c02320"
            "99ba9260b1ff615bad3da031d0ac1d430a80b47ddcfdab20a243547d4fa5e1c1"
            "233abe192523ef9c21df8d686578eef4358e207f89d61fd164009f1e64536187",
            0};
        static const p8 sig_sha512[] = {0x89, 0x01, 0x4f, 0x04, 0x00, 0x01, 0x0a, 0x00, 0x39, 0x16, 0x21, 0x04, 0x5f, 0x40, 0xe0, 0xed, 0xd1, 0x8b, 0xc9, 0x77, 0x2c, 0x63, 0x54, 0xd4, 0x81, 0x30, 0x3d, 0xff, 0x24, 0x4c, 0xbc, 0xb8, 0x05, 0x02, 0x6a, 0x96, 0xbe, 0x40, 0x1b, 0x14, 0x80, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x0e, 0x6d, 0x61, 0x6e, 0x75, 0x32, 0x2c, 0x32, 0x2e, 0x35, 0x2b, 0x31, 0x2e, 0x31, 0x32, 0x2c, 0x30, 0x2c, 0x33, 0x00, 0x0a, 0x09, 0x10, 0x81, 0x30, 0x3d, 0xff, 0x24, 0x4c, 0xbc, 0xb8, 0xf2, 0x74, 0x08, 0x00, 0xa9, 0xdc, 0x2b, 0xc6, 0xf4, 0xa5, 0xdd, 0xc4, 0x4a, 0x94, 0x56, 0xda, 0xc3, 0x38, 0x46, 0x9f, 0x66, 0xa3, 0x95, 0xad, 0x41, 0x8d, 0x5a, 0xa3, 0x6b, 0xec, 0x7a, 0x9d, 0x9c, 0x65, 0xbd, 0xb1, 0xbc, 0xef, 0x58, 0xa0, 0x52, 0x99, 0xfc, 0xa8, 0x8c, 0xd8, 0x9b, 0x92, 0xc7, 0xf2, 0x71, 0xd9, 0xcd, 0x82, 0xcb, 0x92, 0x6b, 0x1a, 0x3e, 0x60, 0xd7, 0xa7, 0x12, 0xdd, 0x99, 0x57, 0xd4, 0x2b, 0xa6, 0xad, 0xea, 0x35, 0x08, 0xd8, 0x44, 0xbb, 0xde, 0xc1, 0xa8, 0x9a, 0xd2, 0x61, 0xfd, 0xd5, 0x48, 0x9f, 0xbb, 0x5c, 0x16, 0x03, 0x0a, 0x94, 0x16, 0x4a, 0xc4, 0x3d, 0x86, 0x8f, 0xf5, 0x0a, 0x13, 0xef, 0x43, 0xd6, 0xbb, 0xa7, 0xc9, 0x56, 0xdf, 0x21, 0xe0, 0xec, 0xa1, 0xf6, 0x15, 0xd5, 0x90, 0x32, 0xc6, 0xac, 0x55, 0x1e, 0x3a, 0x45, 0x00, 0x3b, 0x11, 0xa5, 0x3f, 0x3d, 0x61, 0xf0, 0xb2, 0x5c, 0x3a, 0x88, 0x6f, 0xd3, 0x2d, 0xcb, 0x8f, 0xd8, 0x70, 0x78, 0x53, 0x6e, 0x9c, 0xa7, 0x23, 0xf5, 0x35, 0x61, 0xb6, 0x57, 0xbd, 0x92, 0x8f, 0xee, 0xa8, 0x74, 0xfb, 0xb2, 0x42, 0x52, 0x66, 0x9f, 0x18, 0x2d, 0xc6, 0xa5, 0x50, 0x26, 0x60, 0xb8, 0x03, 0xbd, 0xde, 0x0f, 0x4f, 0x4a, 0xc2, 0x74, 0xe1, 0xc4, 0x19, 0xa5, 0x38, 0x28, 0xb9, 0x31, 0x34, 0x3f, 0x9d, 0xef, 0x72, 0x54, 0x34, 0x0e, 0x3e, 0xb4, 0xae, 0x06, 0x79, 0x8b, 0xe5, 0xb7, 0xb9, 0x5e, 0x57, 0x7f, 0xea, 0x62, 0x8f, 0x45, 0x1e, 0x80, 0x9d, 0x9f, 0x5e, 0x2a, 0xd0, 0x88, 0x67, 0x06, 0x73, 0xa4, 0x99, 0xa3, 0xf3, 0xe7, 0x6d, 0x2a, 0xf4, 0xa6, 0x1e, 0x8f, 0x84, 0x63, 0xc1, 0x13, 0x02, 0xf6, 0xa2, 0xf8, 0x2b, 0x51, 0x01, 0x44, 0xb4, 0x11, 0x47, 0xc6, 0xd1, 0x06, 0x7f, 0x2a, 0xed, 0x3f, 0x39, 0x82};
        static const p8 sig_sha256[] = {0x89, 0x01, 0x4f, 0x04, 0x00, 0x01, 0x08, 0x00, 0x39, 0x16, 0x21, 0x04, 0x5f, 0x40, 0xe0, 0xed, 0xd1, 0x8b, 0xc9, 0x77, 0x2c, 0x63, 0x54, 0xd4, 0x81, 0x30, 0x3d, 0xff, 0x24, 0x4c, 0xbc, 0xb8, 0x05, 0x02, 0x6a, 0x96, 0xbe, 0x40, 0x1b, 0x14, 0x80, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x0e, 0x6d, 0x61, 0x6e, 0x75, 0x32, 0x2c, 0x32, 0x2e, 0x35, 0x2b, 0x31, 0x2e, 0x31, 0x32, 0x2c, 0x30, 0x2c, 0x33, 0x00, 0x0a, 0x09, 0x10, 0x81, 0x30, 0x3d, 0xff, 0x24, 0x4c, 0xbc, 0xb8, 0xf9, 0xd8, 0x07, 0xfd, 0x12, 0x9c, 0x15, 0x9c, 0xde, 0xba, 0x04, 0x64, 0x8f, 0x58, 0xcc, 0x10, 0x86, 0x2c, 0x3e, 0xe9, 0x5b, 0x94, 0xde, 0x5a, 0x8e, 0x99, 0x2d, 0x5c, 0xc1, 0x79, 0xbc, 0x3e, 0xab, 0xe2, 0xcd, 0x8c, 0x79, 0x4d, 0xb2, 0xbc, 0x50, 0xb1, 0x61, 0x7f, 0x14, 0x42, 0x01, 0x7b, 0x1a, 0x63, 0xbd, 0x61, 0x83, 0x46, 0xf1, 0xeb, 0x16, 0xbc, 0x3b, 0x90, 0x65, 0x78, 0xe2, 0xb5, 0x0c, 0xf4, 0x1b, 0x65, 0x47, 0x12, 0x51, 0xb1, 0x04, 0x69, 0x35, 0xb8, 0x0d, 0xbf, 0x19, 0x4f, 0xc0, 0xfb, 0x17, 0xb1, 0x28, 0xaa, 0x54, 0x7d, 0x7b, 0xc9, 0x76, 0x21, 0xd5, 0xd7, 0xe3, 0x14, 0xc5, 0x20, 0xfc, 0xb6, 0x26, 0x93, 0xe9, 0x9c, 0x37, 0x2e, 0xb8, 0x8a, 0xa6, 0x05, 0x5a, 0xf5, 0x96, 0x31, 0xfa, 0x6f, 0x4f, 0xf4, 0x5e, 0xcc, 0x28, 0xbd, 0x4e, 0xa6, 0x91, 0x0c, 0x1b, 0x8c, 0x84, 0x7e, 0x62, 0x0b, 0x31, 0xe9, 0xce, 0xbe, 0x68, 0x47, 0xcd, 0x33, 0x02, 0x13, 0xe4, 0x02, 0x7e, 0x75, 0x7f, 0x57, 0x0d, 0x75, 0x18, 0x82, 0x80, 0xb2, 0xdb, 0x1a, 0x87, 0xda, 0x8d, 0x35, 0xf4, 0x6b, 0x3d, 0xb1, 0xbc, 0xae, 0x07, 0x6b, 0xaa, 0xd8, 0x55, 0xfe, 0xb6, 0xf3, 0x9b, 0xdf, 0x93, 0x33, 0xef, 0x61, 0x46, 0x44, 0xfa, 0x0e, 0xc8, 0x3a, 0xd8, 0x27, 0xd5, 0x47, 0xe7, 0x6d, 0xda, 0x27, 0x57, 0x55, 0x74, 0x78, 0x8a, 0xde, 0x94, 0xdf, 0xdc, 0xb2, 0x10, 0x28, 0xb6, 0x6d, 0x6d, 0x7e, 0xc9, 0xbf, 0xa3, 0x1e, 0x65, 0x4f, 0x58, 0x8e, 0x9c, 0xb0, 0xb8, 0x0a, 0x18, 0x9c, 0x48, 0x89, 0x5f, 0x31, 0x7a, 0x0e, 0x88, 0x53, 0xce, 0x5c, 0x39, 0x83, 0xbe, 0x77, 0xa2, 0xcb, 0xdc, 0xf2, 0x5a, 0x99, 0x05, 0x86, 0x0e, 0x32, 0xe3, 0x93, 0x2f, 0x7d, 0x6d, 0x0c, 0x91, 0x83, 0x61, 0x9f, 0x87, 0x24};
        const positive size = sizeof(archive) - 1;
        p8 copy[sizeof(sig_sha512) + 1];
        p8 hex[BOWL_DIGEST_HEX + 1];
        p8 wrong[sizeof(archive)];
        bipolar handle;
        struct bowl_key floor_ok;
        struct bowl_key floor_late;
        string_address file = BOWL_ROOT_DIRECTORY "/download";
        string_address sign = BOWL_ROOT_DIRECTORY "/download.sig";

        system_make_directory_at(AT_FDCWD, BOWL_ROOT_DIRECTORY, 0755);

        // A digest pin: the right bytes and nothing else.
        put_file(file, archive, size);
        handle = system_open_at(AT_FDCWD, file, FILE_READ | O_CLOEXEC);
        bowl_sha256_of(handle, hex, null);
        system_close(handle);
        check("A pinned digest takes the bytes it names",
              bowl_archive_digest_ok(file, (string_address)hex));
        check("A row that pins no digest takes nothing",
              !bowl_archive_digest_ok(file, null));
        put_file(file, archive, size - 1);
        check("A download one byte short of its digest is refused",
              !bowl_archive_digest_ok(file, (string_address)hex));
        put_file(file, archive, 0);
        check("An empty download is refused",
              !bowl_archive_digest_ok(file, (string_address)hex));
        memory_copy(wrong, archive, size);
        wrong[size] = 'x';
        put_file(file, wrong, size + 1);
        check("A download one byte long is refused",
              !bowl_archive_digest_ok(file, (string_address)hex));
        memory_copy(wrong, archive, size);
        wrong[3] ^= 1;
        put_file(file, wrong, size);
        check("A download with one bit changed is refused",
              !bowl_archive_digest_ok(file, (string_address)hex));
        put_file(file, archive, size);
        check("A digest in capitals is not the pinned one",
              !bowl_archive_digest_ok(file,
                                      "895661BDF6C64E91B7725874165FD05DD30C438D3FFEC661671AB5CFB261CA58"));
        check("A download that is not there is refused",
              !bowl_archive_digest_ok(BOWL_ROOT_DIRECTORY "/absent", (string_address)hex));

        // A signature pin.
        put_file(file, archive, size);
        floor_ok = signer;
        floor_ok.since = 1788264000;
        floor_late = signer;
        floor_late.since = 1788264001;
        for (positive which = 0; which < 2; which++)
        {
                const p8 address_to sig = which ? sig_sha256 : sig_sha512;
                positive length = which ? sizeof(sig_sha256) : sizeof(sig_sha512);

                put_file(sign, sig, length);
                check("A signature by the pinned key over the archive is taken",
                      bowl_signature_ok(file, sign, address_of signer));
                check("A signature made on the floor's second is taken",
                      bowl_signature_ok(file, sign, address_of floor_ok));
                check("An older signed archive is a replay and is refused",
                      !bowl_signature_ok(file, sign, address_of floor_late));
                check("A signature by another key is refused",
                      !bowl_signature_ok(file, sign, address_of other));

                put_file(file, archive, size - 1);
                check("A signed archive one byte short is refused",
                      !bowl_signature_ok(file, sign, address_of signer));
                put_file(file, wrong, size);
                check("A signed archive with a bit changed is refused",
                      !bowl_signature_ok(file, sign, address_of signer));
                put_file(file, archive, size);

                {
                        //      Every byte is covered but the unhashed area,
                        //      which bowl_signature_read does not read.
                        positive hashed = 3 + 6 + ((positive)sig[7] << 8) + sig[8];
                        positive unhashed = ((positive)sig[hashed] << 8) + sig[hashed + 1];
                        p8 refused = true;
                        p8 cut_refused = true;

                        for (positive at = 0; at < length; at++)
                        {
                                p8 free_byte = at >= hashed + 2 &&
                                                 at < hashed + 2 + unhashed;

                                memory_copy(copy, sig, length);
                                copy[at] ^= 1 << (at & 7);
                                put_file(sign, copy, length);
                                refused &= bowl_signature_ok(file, sign,
                                                             address_of signer) == free_byte;
                        }
                        check("A signature with any one bit changed is refused, "
                              "unless it is in the unhashed area", refused);
                        for (positive cut = 0; cut < length; cut += 7)
                        {
                                put_file(sign, sig, cut);
                                cut_refused &= !bowl_signature_ok(file, sign,
                                                                  address_of signer);
                        }
                        check("A signature cut short is refused", cut_refused);
                }
                memory_copy(copy, sig, length);
                copy[length] = 0;
                put_file(sign, copy, length + 1);
                check("A byte after the signature packet is refused",
                      !bowl_signature_ok(file, sign, address_of signer));
        }
        check("A signature that is not there is refused",
              !bowl_signature_ok(file, BOWL_ROOT_DIRECTORY "/absent", address_of signer));

        // The pinned key is the key its fingerprint names.
        {
                p8 made[41];

                check("The Arch Linux ARM key's numbers make its fingerprint",
                      bowl_key_fingerprint(address_of bowl_alarm_key, made) &&
                          string_equals((string_address)made,
                                        bowl_alarm_key.fingerprint));
                check("The test key's numbers make its fingerprint",
                      bowl_key_fingerprint(address_of signer, made) &&
                          string_equals((string_address)made, signer.fingerprint));
        }

        // Every row names how it is checked, and only one way.
        for (positive at = 0; at < array_count(bowl_distros); at++)
        {
                const struct bowl_distro address_to row = bowl_distros + at;
                p8 made[41];

                check("Every setup download is pinned by a digest or by a key, not neither",
                      row->sha256 || row->key);
                check("A row pins a digest or a key, not both", !(row->sha256 && row->key));
                check("A key a row names makes its own fingerprint",
                      !row->key ||
                          (bowl_key_fingerprint(row->key, made) &&
                           string_equals((string_address)made, row->key->fingerprint)));
        }

        system_remove_at(AT_FDCWD, file, 0);
        system_remove_at(AT_FDCWD, sign, 0);
}

/*
        A mirror that never stops sending, against the fetch child: a "wget"
        that is a script writing as many bytes as its URL says, run the way
        bowl_setup_fetch runs the real one. Every length up to the ceiling
        is taken whole, every length past it, by one byte or by a lot, is a
        refused download that never grew the file past the ceiling, and
        every row's ceiling holds the download it measured. The writer
        keeps its own status: SIGXFSZ is ignored, so the write past the
        ceiling fails with EFBIG and the writer says so and exits 1 rather
        than dying of the signal (153 from sh); an ignored signal stays
        ignored across exec, under qemu-user as natively.
*/
static fn download_ceiling(void)
{
        static const p8 writer[] =
            "#!/bin/sh\nhead -c \"$4\" /dev/zero > \"$3\"\nstatus=$?\n"
            "echo $status > \"$0.status\"\nexit $status\n";
        string_address fake = BOWL_ROOT_DIRECTORY "/fake-wget";
        string_address said = BOWL_ROOT_DIRECTORY "/fake-wget.status";
        string_address part = BOWL_ROOT_DIRECTORY "/fetched.part";
        p8 named = true;
        static const struct { string_address bytes; p64 ceiling; p8 whole; } rows[] = {
            {"0", 4096, true},       {"1", 4096, true},     {"4095", 4096, true},
            {"4096", 4096, true},    {"4097", 4096, false}, {"8192", 4096, false},
            {"1048576", 4096, false}, {"65536", 65536, true}, {"65537", 65536, false},
            {"200000", 65536, false},
        };
        p8 held = true;
        p8 whole = true;

        system_make_directory_at(AT_FDCWD, BOWL_ROOT_DIRECTORY, 0755);
        if (!put_file(fake, writer, sizeof(writer) - 1) ||
            system_change_mode_at(AT_FDCWD, fake, 0755) < 0)
        {
                check("The fake wget for the ceiling rows was made", false);
                return;
        }
        for (positive at = 0; at < array_count(rows); at++)
        {
                file_facts facts;
                b32 failed = bowl_setup_fetch(fake, part, rows[at].bytes,
                                              rows[at].ceiling);
                p8 there = file_look_code(AT_FDCWD, part, 0, address_of facts) >= 0;
                p64 size = there ? facts.size : 0;

                if (rows[at].whole)
                        whole &= !failed && there &&
                                 size == (p64)string_to_positive(rows[at].bytes);
                else
                {
                        p8 status[8] = {0};

                        held &= failed && size <= rows[at].ceiling;
                        named &= file_read_once_at(AT_FDCWD, said, status,
                                                   sizeof(status) - 1) > 0 &&
                                 string_equals((string_address)status, "1\n");
                }
        }
        check("A download no longer than its ceiling is taken whole", whole);
        check("A download past its ceiling is refused and stops at the ceiling", held);
        check("A write past the ceiling fails with EFBIG and is not killed by SIGXFSZ",
              named);
        for (positive at = 0; at < array_count(bowl_distros); at++)
                check("A row's ceiling holds the download it measured",
                      bowl_download_ceiling(bowl_distros + at) >
                          bowl_distros[at].archive_bytes);
        system_remove_at(AT_FDCWD, part, 0);
        system_remove_at(AT_FDCWD, fake, 0);
        system_remove_at(AT_FDCWD, said, 0);
}

/*
        A secret on a command line is not kept in the history: the two
        moonwater forms that took one there (wifi add SSID PASSWORD, link
        group NAME SECRET) are judged by history_wanted before the line is
        copied. The same verbs with "-", with no word, with allow after the
        group, a pairing code (good once, for five minutes) and other
        commands entirely are kept. The last rows are what the denylist does
        not see and says so: a wrapper's option that takes a name, bash -c,
        ssh, a verb after twelve words.
*/
static fn secret_lines(void)
{
        static const struct { string_address line; p8 secret; } lines[] = {
            {"moonwater wifi add home hunter2hunter2", true},
            {"moonwater wifi add 'my home' 'pass phrase here'", true},
            {"moonwater wifi add \"my home\" pass\\ phrase", true},
            {"  /usr/bin/moonwater wifi add home hunter2hunter2", true},
            {"ls; moonwater wifi add home hunter2hunter2", true},
            {"ls && moonwater link group lab s3cr3tsecret allow run", true},
            {"moonwater link group lab s3cr3tsecret", true},
            {"sudo moonwater wifi add home hunter2hunter2", true},
            {"sudo -E /usr/bin/moonwater wifi add home hunter2hunter2", true},
            {"LC_ALL=C moonwater link group lab s3cr3tsecret", true},
            {"HOME=/root PATH=/bin env -i moonwater wifi add home hunter2hunter2", true},
            {"command moonwater wifi add home hunter2hunter2", true},
            {"nohup moonwater link group lab s3cr3tsecret allow run", true},
            {"time exec moonwater wifi add home hunter2hunter2", true},
            {"timeout 5 moonwater wifi add home hunter2hunter2", true},
            {"nice -n 5 moonwater link group lab s3cr3tsecret", true},
            {"sudo moonwater wifi add home -", false},
            {"sudo moonwater wifi add home", false},
            {"sudo ls wifi add home hunter2hunter2", false},
            {"X=1 echo moonwater wifi add home hunter2hunter2", false},
            {"moonwater wifi add home -", false},
            {"moonwater wifi add home", false},
            {"moonwater wifi add 'my home'", false},
            {"moonwater link group lab -", false},
            {"moonwater link group lab - allow run", false},
            {"moonwater link group lab", false},
            {"moonwater link group lab allow run", false},
            {"moonwater link group leave lab", false},
            {"moonwater link group leave lab forget", false},
            {"moonwater link group", false},
            {"moonwater link office abc-def", false},
            {"moonwater link pair office AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=", false},
            {"moonwater link join lab s3cr3tsecret", false},
            {"moonwater wifi remove home", false},
            {"moonwater wifi", false},
            {"echo moonwater wifi add home hunter2hunter2 | cat", false},
            {"echo hi", false},
            {"", false},
            // What the denylist does not see.
            {"sudo -u root moonwater wifi add home hunter2hunter2", false},
            {"env -u TERM moonwater wifi add home hunter2hunter2", false},
            {"bash -c 'moonwater wifi add home hunter2hunter2'", false},
            {"ssh box moonwater wifi add home hunter2hunter2", false},
            {"sudo sudo sudo sudo sudo sudo sudo sudo sudo sudo sudo sudo moonwater wifi add home hunter2hunter2", false},
        };

        for (positive at = 0; at < array_count(lines); at++)
        {
                p8 said = history_secret_line(lines[at].line,
                                                string_length(lines[at].line));

                if (said != lines[at].secret)
                        string_format(log, "  FAIL the line: %s\n", lines[at].line);
                check("A line with a secret on it is kept out of the history",
                      said == lines[at].secret);
        }
        check("history_wanted refuses a line with a secret on it",
              !history_wanted("moonwater wifi add home hunter2hunter2", 38) &&
                  history_wanted("moonwater wifi add home -", 25));
}

#if MOONWATER_STRICT >= STRICT_TIGHT
/*
        The tight tier refuses the argument forms (host.c's wifi add and
        waterlink's link group) before they touch anything, and says why: the
        message is read back from standard error. The forms with "-" are not
        in question here, they read the terminal.
*/
static p8 secret_refused(string_address what, p8 group)
{
        static string_address wifi[] = {"moonwater", "wifi", "add", "home", "hunter2hunter2"};
        static string_address link[] = {"lab", "s3cr3tsecret"};
        p8 said[256];
        bipolar kept = (bipolar)system_call_1(syscall(dup), 2);
        bipolar file = system_open_at_mode(AT_FDCWD, "/tmp/moonwater-secret-said",
                                           O_RDWR | O_CREAT | O_TRUNC, 0600);
        b32 answer;
        bipolar got;

        system_call_3(syscall(dup3), (positive)file, 2, 0);
        answer = group ? link_group_join(link, 2) : host_wifi(wifi, 5);
        system_call_3(syscall(dup3), (positive)kept, 2, 0);
        system_close((positive)kept);
        system_close((positive)file);
        got = file_read_once_at(AT_FDCWD, "/tmp/moonwater-secret-said", said, sizeof said - 1);
        system_remove_at(AT_FDCWD, "/tmp/moonwater-secret-said", 0);
        said[got > 0 ? got : 0] = end;
        return answer != 0 && got > 0 && string_find((string_address)said, what);
}

static fn secret_arguments(void)
{
        check("The tight tier refuses a password on the command line, and says so",
              secret_refused("password on the command line is refused", false));
        check("The tight tier refuses a group secret on the command line, and says so",
              secret_refused("secret on the command line is refused", true));
}
#endif

/* A file at path holding length bytes, its mode set after the write (which
   the umask would cut): whether it was written. */
static p8 switches_file(string_address path, const p8 address_to bytes, positive length,
                          positive mode)
{
        bipolar handle;
        p8 written;

        system_remove_at(AT_FDCWD, path, 0);
        handle = system_open_at_mode(AT_FDCWD, path, FILE_WRITE | O_CLOEXEC, 0600);
        if (handle < 0)
                return false;
        written = system_write_all((positive)handle, bytes, length) == length;
        system_close(handle);
        return written && system_change_mode_at(AT_FDCWD, path, mode) >= 0;
}

/*
        moonwater security's switches as a program asks them (host.c's
        security_switches_read_at, security_switch_turned and
        security_switch_on). The published file counts only as a regular
        file, never through a link, that a group and others may not write,
        no longer than the overlay, owned by the owner asked or by the
        overflow uid, and read whole by tls_overlay_read: anything else is
        -1, which a caller takes as the tier's default. Then what each
        switch is with nothing kept and with its entry kept, at this build's
        tier: packages is on unless kept off, and off at the tight tier
        whatever is kept; sudo is off unless kept on, and off at the tight
        tier whatever is kept; TLS's are as they were.
*/
static fn switches_asked(void)
{
        static p8 file[TLS_OVERLAY_HEAD + TLS_OVERLAY_ROOM + 64];
        string_address path = (string_address) "/tmp/moonwater-bowl-check-security";
        string_address aside = (string_address) "/tmp/moonwater-bowl-check-security.kept";
        p32 me = (p32)system_call(syscall(geteuid));
        p32 both = (p32)1 << (SECURITY_SWITCH_PACKAGES_OFF - 1) |
                   (p32)1 << (SECURITY_SWITCH_SUDO_ON - 1);
        p8 tight = MOONWATER_STRICT_BOWL >= STRICT_TIGHT;
        positive used = TLS_OVERLAY_HEAD;
        positive packages = array_count(security_switches);
        positive sudo = array_count(security_switches);
        positive tls12 = array_count(security_switches);
        positive revocation = array_count(security_switches);
        p8 written;

        //      The file moonwater security publishes with both turned.
        for (p16 id = SECURITY_SWITCH_PACKAGES_OFF; id <= SECURITY_SWITCH_SUDO_ON; id++)
        {
                p8 entry[8] = {TLS_OVERLAY_LIST, TLS_OVERLAY_SWITCH, (p8)id, 0, 0, 0, 0, 0};

                memory_copy(file + used, entry, 8);
                used += 8;
        }
        memory_copy(file, TLS_OVERLAY_MAGIC, 8);
        memory_zero(file + 8, 8);
        file[8] = (p8)(used - TLS_OVERLAY_HEAD);

        written = switches_file(path, file, used, 0644);
        check("A published switch file owned by its owner and written by it alone reads",
              written && security_switches_read_at(path, me) == (bipolar)both);
        written &= system_change_mode_at(AT_FDCWD, path, 0600) >= 0;
        check("and one only its owner may read reads too",
              written && security_switches_read_at(path, me) == (bipolar)both);
        written &= system_change_mode_at(AT_FDCWD, path, 0664) >= 0;
        check("A switch file a group may write is not believed",
              written && security_switches_read_at(path, me) == -1);
        written &= system_change_mode_at(AT_FDCWD, path, 0646) >= 0;
        check("nor one others may write",
              written && security_switches_read_at(path, me) == -1);
        written &= system_change_mode_at(AT_FDCWD, path, 0644) >= 0;
        check("nor one owned by somebody else, who is not the overflow uid",
              written && security_switches_read_at(path, me + 1) == -1 &&
                  security_switches_read_at(path, me) == (bipolar)both);

        //      A link to that good file, a FIFO, a directory: none is a file.
        system_remove_at(AT_FDCWD, aside, 0);
        written = system_rename_at(AT_FDCWD, path, AT_FDCWD, aside, 0) >= 0 &&
                  system_symbolic_link_at(aside, AT_FDCWD, path) >= 0;
        check("A switch file reached through a link is not believed",
              written && security_switches_read_at(path, me) == -1 &&
                  security_switches_read_at(aside, me) == (bipolar)both);
        system_remove_at(AT_FDCWD, path, 0);
        written = system_call_4(syscall(mknodat), (positive)(bipolar)AT_FDCWD, (positive)path,
                                0010644, 0) >= 0;
        check("A FIFO at its name answers at once and is not believed",
              written && security_switches_read_at(path, me) == -1);
        system_remove_at(AT_FDCWD, path, 0);
        written = system_make_directory_at(AT_FDCWD, path, 0755) >= 0;
        check("A directory at its name is not believed",
              written && security_switches_read_at(path, me) == -1);
        system_remove_at(AT_FDCWD, path, AT_REMOVEDIR);
        system_remove_at(AT_FDCWD, aside, 0);
        check("No file is -1, the default", security_switches_read_at(path, me) == -1);

        //      Files that do not read whole.
        file[0] ^= 0x20;
        written = switches_file(path, file, used, 0644);
        file[0] ^= 0x20;
        check("A switch file whose magic is not the overlay's is not believed",
              written && security_switches_read_at(path, me) == -1);
        file[used] = 0;
        written = switches_file(path, file, used + 1, 0644);
        check("nor one with a byte after its entries",
              written && security_switches_read_at(path, me) == -1);
        file[TLS_OVERLAY_HEAD + 2] = TLS_SWITCHES + 1;
        written = switches_file(path, file, used, 0644);
        file[TLS_OVERLAY_HEAD + 2] = SECURITY_SWITCH_PACKAGES_OFF;
        check("nor one with a switch id this build has no word for",
              written && security_switches_read_at(path, me) == -1);
        memory_zero(file + used, sizeof file - used);
        written = switches_file(path, file, TLS_OVERLAY_HEAD + TLS_OVERLAY_ROOM + 1, 0644);
        check("nor one longer than the overlay",
              written && security_switches_read_at(path, me) == -1);
        file[8] = 0;
        written = switches_file(path, file, TLS_OVERLAY_HEAD, 0644);
        check("A file of no entries reads, and turns nothing",
              written && security_switches_read_at(path, me) == 0);
        system_remove_at(AT_FDCWD, path, 0);

        for (positive at = 0; at < array_count(security_switches); at++)
        {
                string_address word = security_switches[at].word;

                packages = string_equals(word, "packages") ? at : packages;
                sudo = string_equals(word, "sudo") ? at : sudo;
                tls12 = string_equals(word, "tls12") ? at : tls12;
                revocation = string_equals(word, "revocation") ? at : revocation;
        }
        check("The switches name packages and sudo, with their ids",
              packages < array_count(security_switches) && sudo < array_count(security_switches) &&
                  security_switches[packages].id == SECURITY_SWITCH_PACKAGES_OFF &&
                  security_switches[sudo].id == SECURITY_SWITCH_SUDO_ON &&
                  string_equals(security_switches[packages].standard, "on") &&
                  string_equals(security_switches[sudo].standard, "off"));
        if (packages == array_count(security_switches) || sudo == array_count(security_switches) ||
            tls12 == array_count(security_switches) ||
            revocation == array_count(security_switches))
                return;
        check("packages is on with nothing kept, and off at the tight tier",
              security_switch_turned(packages, 0) == tight);
        check("packages is off when kept off, at every tier",
              security_switch_turned(packages, both));
        check("sudo is off with nothing kept, at every tier", !security_switch_turned(sudo, 0));
        check("sudo is on when kept on, and never at the tight tier",
              security_switch_turned(sudo, both) == !tight);
        check("A switch of TLS answers its own bit and no other",
              !security_switch_turned(tls12, both) &&
                  security_switch_turned(tls12, (p32)1 << (TLS_SWITCH_TLS12_OFF - 1)) &&
                  security_switch_turned(revocation, 0) == (MOONWATER_STRICT_NET >= STRICT_TIGHT));
        check("A name that is no switch, or a switch whose words are not on and off, is never on",
              !security_switch_on("bogus") && !security_switch_on("") &&
                  !security_switch_on("revocation"));
}

/*
        The disks boot takes on the install's word are the ones on a bus
        nothing is plugged into: the place sysfs gives a disk says USB or
        Thunderbolt for the rest (host_link_external); the `removable` files
        above it are read from the real sysfs and are the install lane's to
        show (an NVMe disk in a guest is taken), and a disk that cannot be
        told is external. The only disk with this build that is not the one
        the session started from is taken when it is not external and the
        tier is not tight (host_lone_taken).
*/
static fn boot_links(void)
{
        static const struct { string_address link; p8 external; } links[] = {
            {"../../devices/pci0000:00/0000:00:14.0/usb2/2-1/2-1:1.0/host6/target6:0:0/6:0:0:0/block/sdb", true},
            {"../../devices/pci0000:00/0000:00:03.0/usb1/1-1/1-1:1.0/host0/target0:0:0/0:0:0:0/block/sda", true},
            {"../../devices/pci0000:00/0000:00:0d.2/0-0/thunderbolt/0-1/nvme/nvme1/nvme1n1", true},
            {"../../devices/pci0000:00/0000:00:1d.0/0000:3d:00.0/nvme/nvme0/nvme0n1", false},
            {"../../devices/pci0000:00/0000:00:17.0/ata1/host0/target0:0:0/0:0:0:0/block/sda", false},
            {"../../devices/platform/soc/mmc_host/mmc0/mmc0:0001/block/mmcblk0", false},
            {"../../devices/virtual/block/loop0", false},
        };

        for (positive at = 0; at < array_count(links); at++)
                check("A disk on USB or Thunderbolt is external, one on NVMe or SATA is not",
                      host_link_external(links[at].link) == links[at].external);
        check("A disk named with a path is external", host_disk_external("no/such"));
        check("A disk that is not there is external", host_disk_external("nosuchdisk9"));
        check("No disk at all is external", host_disk_external(""));
        check("A fixed disk is taken at the default tier", host_lone_taken(false, false));
        check("An external disk is asked about at the default tier", !host_lone_taken(true, false));
        check("No disk is taken at the tight tier", !host_lone_taken(false, true));
        check("An external disk is asked about at the tight tier", !host_lone_taken(true, true));

        //      host_medium_match: whether the session started from an install, by the medium number the install wrote
        //      into its image. An install stick (no number) started from none, so boot mounts nothing.
        static const p8 blank[16], ours[16] = {1, 2, 3}, theirs[16] = {9};

        check("A booted image with no medium number did not start from an install that carries one (an install stick)",
              host_medium_match(blank, ours, sizeof(ours)) == 0);
        check("A booted medium an install carries started from that install", host_medium_match(ours, ours, sizeof(ours)) == 1);
        check("A booted medium another install carries did not start from this one",
              host_medium_match(theirs, ours, sizeof(ours)) == 0);
        check("An install that carries no medium number cannot be told apart, booted number or none",
              host_medium_match(ours, blank, sizeof(ours)) == -1 && host_medium_match(blank, blank, sizeof(ours)) == -1);
}

/*
        What a Wi-Fi join asks of an access point (wifi_join_plan), at every
        tier in this one build: named networks with what each tier must do
        with them, then every combination of what an access point can offer,
        the rules a saved network can carry and the two kinds of secret, held
        to what the tiers promise. Last, the plan the join itself makes, at
        this build's MOONWATER_STRICT_NET, which the tight build of this
        check (sh test/run net) holds to the tight tier.
*/
typedef struct
{
        string_address name;
        p8 akms;
        p8 pmf;
        p8 group;
        p8 pairwise;
        p8 rules;
        p8 key_only;
        //      What the safe tier and the tight one do: the suite and its
        //      fallback, or the refusal.
        bipolar safe;
        p8 safe_fallback;
        bipolar tight;
        p8 tight_fallback;
} wifi_plan_case;

static fn wifi_plans(void)
{
        enum
        {
                C = WIFI_CIPHER_CCMP,
                T = WIFI_CIPHER_TKIP,
                OTHER_CIPHER = WIFI_CIPHER_OTHER,
                PSK = WIFI_AKM_PSK,
                S256 = WIFI_AKM_PSK_SHA256,
                SAE = WIFI_AKM_SAE,
                ANY = WIFI_AKM_OTHER,
                CAP = WIFI_PMF_CAPABLE,
                REQ = WIFI_PMF_REQUIRED,
        };
        static const wifi_plan_case cases[] = {
            {"WPA2 with no protection", PSK, 0, C, C, 0, false, 2, 0, WIFI_REFUSE_NO_PMF, 0},
            {"WPA2 with no protection saved with wpa2", PSK, 0, C, C, WIFI_RULE_WPA2, false, 2, 0,
             WIFI_REFUSE_NO_PMF, 0},
            {"WPA2 with protection", PSK, CAP, C, C, 0, false, 2, 0, WIFI_REFUSE_WPA2, 0},
            {"WPA2 with protection saved with wpa2", PSK, CAP, C, C, WIFI_RULE_WPA2, false, 2, 0, 2, 0},
            {"WPA2 that requires protection", PSK, CAP | REQ, C, C, WIFI_RULE_WPA2, false, 2, 0, 2, 0},
            {"PSK-SHA256 beside PSK with protection", PSK | S256, CAP, C, C, WIFI_RULE_WPA2, false, 6, 0,
             6, 0},
            {"PSK-SHA256 beside PSK without it", PSK | S256, 0, C, C, 0, false, 2, 0, WIFI_REFUSE_NO_PMF,
             0},
            {"PSK-SHA256 alone without protection", S256, 0, C, C, 0, false, 6, 0, WIFI_REFUSE_NO_PMF, 0},
            {"WPA3", SAE, CAP | REQ, C, C, 0, false, 8, 0, 8, 0},
            {"WPA3 with a key of hex digits", SAE, CAP | REQ, C, C, 0, true, WIFI_REFUSE_KEY_ONLY, 0,
             WIFI_REFUSE_KEY_ONLY, 0},
            {"WPA2 and WPA3", PSK | SAE, CAP, C, C, 0, false, 8, 2, 8, 0},
            {"WPA2 and WPA3 saved with wpa2", PSK | SAE, CAP, C, C, WIFI_RULE_WPA2, false, 8, 2, 8, 2},
            {"WPA2 and WPA3 with a key of hex digits", PSK | SAE, CAP, C, C, 0, true, 2, 0,
             WIFI_REFUSE_WPA2, 0},
            {"WPA2 and WPA3, joined with WPA3 before", PSK | SAE, CAP, C, C, WIFI_RULE_SAE, false, 8, 0, 8,
             0},
            {"WPA2 alone by a name once joined with WPA3", PSK, CAP, C, C, WIFI_RULE_SAE, false,
             WIFI_REFUSE_SAE_GONE, 0, WIFI_REFUSE_SAE_GONE, 0},
            {"no protection by a name that offered it", PSK, 0, C, C, WIFI_RULE_PMF, false,
             WIFI_REFUSE_PMF_GONE, 0, WIFI_REFUSE_PMF_GONE, 0},
            {"protection by a name that offered it", PSK, CAP, C, C, WIFI_RULE_PMF, false, 2, 0,
             WIFI_REFUSE_WPA2, 0},
            {"a TKIP group", PSK, 0, T, C, 0, false, WIFI_REFUSE_TKIP, 0, WIFI_REFUSE_TKIP, 0},
            {"TKIP alone pairwise", PSK, 0, C, T, 0, false, WIFI_REFUSE_TKIP, 0, WIFI_REFUSE_TKIP, 0},
            {"TKIP beside CCMP pairwise", PSK, CAP, C, C | T, WIFI_RULE_WPA2, false, 2, 0, 2, 0},
            {"another group cipher", PSK, CAP, OTHER_CIPHER, C, 0, false, WIFI_REFUSE_CIPHER, 0,
             WIFI_REFUSE_CIPHER, 0},
            {"two group ciphers between two frames", PSK, CAP, C | T, C, 0, false, WIFI_REFUSE_TKIP, 0,
             WIFI_REFUSE_TKIP, 0},
            {"FT alone", ANY, CAP, C, C, 0, false, WIFI_REFUSE_SUITE, 0, WIFI_REFUSE_SUITE, 0},
            {"nothing at all", 0, 0, C, C, 0, false, WIFI_REFUSE_SUITE, 0, WIFI_REFUSE_SUITE, 0},
        };
        wifi_plan plan;
        positive plans = 0;
        positive refusals = 0;
        p8 held = true;

        for (positive at = 0; at < array_count(cases); at++)
        {
                const wifi_plan_case address_to one = cases + at;
                wifi_rsn_caps caps = {.akms = one->akms, .pmf = one->pmf, .group = one->group,
                                      .pairwise = one->pairwise};

                for (positive tier = STRICT_REFERENCE; tier <= STRICT_LOCKED; tier++)
                {
                        bipolar want = tier >= STRICT_TIGHT ? one->tight : one->safe;
                        p8 fallback = tier >= STRICT_TIGHT ? one->tight_fallback : one->safe_fallback;
                        bipolar got = wifi_join_plan(tier, address_of caps, one->rules, one->key_only,
                                                     address_of plan);
                        p8 right = want < 0 ? got == want
                                              : !got && plan.akm == want &&
                                                    plan.fallback == fallback &&
                                                    plan.pmf == (want == 8 || one->pmf ? (CAP | REQ) : 0);

                        if (!right)
                                string_format(log, "  wifi plan: %s at tier %p gave %b\n",
                                              one->name, tier, got);
                        held &= right;
                }
        }
        check("each named network is planned as each tier says", held);
        held = true;

        //      Every offer, rule and secret, at every tier.
        for (positive tier = STRICT_REFERENCE; tier <= STRICT_LOCKED; tier++)
        for (p8 akms = 0; akms < 16; akms++)
        for (p8 pmf = 0; pmf < 4; pmf++)
        for (p8 group = 1; group < 8; group++)
        for (p8 pairwise = 0; pairwise < 8; pairwise++)
        for (p8 rules = 0; rules < 8; rules++)
        for (p8 key_only = 0; key_only < 2; key_only++)
        {
                wifi_rsn_caps caps = {.akms = akms, .pmf = pmf, .group = group, .pairwise = pairwise};
                p8 tight = tier >= STRICT_TIGHT;
                bipolar got = wifi_join_plan(tier, address_of caps, rules, key_only, address_of plan);
                wifi_plan other;
                bipolar beside = wifi_join_plan(tight ? STRICT_SAFE : STRICT_TIGHT, address_of caps,
                                                rules, key_only, address_of other);

                if (got)
                {
                        refusals++;
                        held &= got >= WIFI_REFUSE_FIRST && got <= WIFI_REFUSE_LAST &&
                                !plan.akm && !plan.pmf && !plan.fallback;
                        //      What the default tier refuses, the tight one does.
                        held &= tight || beside != 0;
                        continue;
                }
                plans++;
                //      Only CCMP, only a suite offered, never WPA3 from a key.
                held &= group == C && (pairwise & C);
                held &= plan.akm == 8 ? (akms & SAE) && !key_only
                        : plan.akm == 6 ? (akms & S256) != 0
                                        : plan.akm == 2 && (akms & PSK);
                held &= !plan.fallback || (plan.akm == 8 && (plan.fallback == 2 || plan.fallback == 6) &&
                                           (akms & (plan.fallback == 2 ? PSK : S256)));
                //      Protection: always with WPA3, with WPA2 exactly when
                //      offered, and asked as required.
                held &= plan.pmf == (plan.akm == 8 || pmf ? (CAP | REQ) : 0);
                //      What the network taught is never given up.
                held &= !(rules & WIFI_RULE_PMF) || pmf;
                held &= !(rules & WIFI_RULE_SAE) || (plan.akm == 8 && !plan.fallback);
                //      WPA3 whenever it can be had.
                held &= plan.akm == 8 || !(akms & SAE) || key_only;
                //      The tight tier: protection, and WPA2 only for a network
                //      saved with wpa2, as the plan and as the fallback.
                held &= !tight || ((plan.pmf & REQ) && (plan.akm == 8 || (rules & WIFI_RULE_WPA2)) &&
                                   (!plan.fallback || ((rules & WIFI_RULE_WPA2) && pmf)));
                //      Below it, the owner's word changes nothing.
                if (!tight)
                {
                        wifi_plan without;

                        held &= !wifi_join_plan(tier, address_of caps, rules & ~WIFI_RULE_WPA2,
                                                key_only, address_of without) &&
                                !memory_compare(address_of without, address_of plan, sizeof(plan));
                }
        }
        check("every offer, rule and secret at every tier keeps what the tiers promise", held);
        check("and both answers come up", plans > 1000 && refusals > 1000);

        //      The join's own: this build's tier.
        {
                wifi_rsn_caps plain = {.akms = PSK, .pmf = 0, .group = C, .pairwise = C};
                wifi_rsn_caps guarded = {.akms = PSK, .pmf = CAP, .group = C, .pairwise = C};

                check("the join asks this build's tier: WPA2 with no protection",
                      wifi_join_plan(MOONWATER_STRICT_NET, address_of plain, 0, false, address_of plan) ==
                          (MOONWATER_STRICT_NET >= STRICT_TIGHT ? WIFI_REFUSE_NO_PMF : 0));
                check("and WPA2 with protection, not saved with wpa2",
                      wifi_join_plan(MOONWATER_STRICT_NET, address_of guarded, 0, false,
                                     address_of plan) ==
                          (MOONWATER_STRICT_NET >= STRICT_TIGHT ? WIFI_REFUSE_WPA2 : 0));
        }
}

/*
        The Wi-Fi station apart (radio_station_run in src/sh/host.c). Its filter
        in the classic-BPF interpreter over the calls a join and its keeper make
        and the ones they must not; the statuses its broker believes of a join;
        and natively, the station confined in a user namespace of its own, where
        its netlink and EAPOL sockets are its to ask for and anything else kills
        it. Under qemu-user, which cannot filter itself, the confinement is
        refused, which is what the station then does: it joins nothing.
*/
#define SHARED_filter_run
#include "../checks.c"
#undef SHARED_filter_run

//      check() with a name made at run time.
static fn station_check(string_address name, p8 passed)
{
        checks++;
        if (!passed)
        {
                failures++;
                string_format(log, "  FAIL %s\n", name);
        }
}

static p8 station_emulated(void)
{
        return program_argument_count() > 1 &&
               !string_compare(program_argument(1), (string_address)"--emulated");
}

static fn station_filter(void)
{
        const p32 eapol = (p32)network_order_16(ETH_P_PAE);
        const struct
        {
                string_address name;
                p32 number;
                p64 arguments[6];
                p8 allowed;
        } rows[] = {
            {"recvfrom", syscall(recvfrom), {0}, true},
            {"sendto", syscall(sendto), {0}, true},
            {"ppoll", syscall(ppoll), {0}, true},
            {"clock_gettime", syscall(clock_gettime), {0}, true},
            {"getrandom", syscall(getrandom), {0}, true},
            {"close", syscall(close), {0}, true},
            {"bind", syscall(bind), {0}, true},
            {"getsockname", syscall(getsockname), {0}, true},
            {"nanosleep", syscall(nanosleep), {0}, true},
            {"exit_group", syscall(exit_group), {0}, true},
            {"a generic netlink socket", syscall(socket),
             {AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, NETLINK_GENERIC}, true},
            {"a route netlink socket", syscall(socket),
             {AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, NETLINK_ROUTE}, true},
            {"the EAPOL packet socket", syscall(socket),
             {AF_PACKET, SOCK_DGRAM | SOCK_CLOEXEC, eapol}, true},
            {"an audit netlink socket", syscall(socket),
             {AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, 9}, false},
            {"a raw netlink socket", syscall(socket),
             {AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_GENERIC}, false},
            {"a raw packet socket", syscall(socket), {AF_PACKET, SOCK_RAW, eapol}, false},
            {"a packet socket of every ethertype", syscall(socket),
             {AF_PACKET, SOCK_DGRAM, network_order_16(3)}, false},
            {"an IPv4 socket", syscall(socket), {AF_INET, SOCK_DGRAM, 0}, false},
            {"a unix socket", syscall(socket), {AF_UNIX, SOCK_STREAM, 0}, false},
            {"an IPv6 socket", syscall(socket), {AF_INET6, SOCK_DGRAM, 0}, false},
            {"a netlink group joined", syscall(setsockopt),
             {3, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP}, true},
            {"a netlink group left", syscall(setsockopt),
             {3, SOL_NETLINK, RADIO_NETLINK_DROP_MEMBERSHIP}, true},
            {"netlink extended acks", syscall(setsockopt), {3, SOL_NETLINK, NETLINK_EXT_ACK},
             true},
            {"another netlink option", syscall(setsockopt), {3, SOL_NETLINK, 3}, false},
            {"SO_BINDTODEVICE", syscall(setsockopt), {3, SOL_SOCKET, SO_BINDTODEVICE}, false},
            {"a packet socket option", syscall(setsockopt), {3, 263, 1}, false},
            {"memory to read and write", syscall(mmap), {0, 4096, 3, 0x22}, true},
            {"executable memory", syscall(mmap), {0, 4096, 5, 0x22}, false},
            {"openat", syscall(openat), {0}, false},
            {"execve", syscall(execve), {0}, false},
            {"ptrace", syscall(ptrace), {0}, false},
            {"kill", syscall(kill), {0}, false},
            {"ioctl", syscall(ioctl), {0}, false},
            {"prctl", syscall(prctl), {0}, false},
            {"mprotect", syscall(mprotect), {0}, false},
            {"seccomp", syscall(seccomp), {0}, false},
            {"capset", syscall(capset), {0}, false},
            {"setresuid", syscall(setresuid), {0}, false},
            {"unshare", syscall(unshare), {0}, false},
            {"read", syscall(read), {0}, false},
            {"write", syscall(write), {0}, false},
            {"connect", syscall(connect), {0}, false},
            {"sendmsg", syscall(sendmsg), {0}, false},
            {"recvmsg", syscall(recvmsg), {0}, false},
            {"socketpair", syscall(socketpair), {0}, false},
            {"x32's recvfrom", syscall(recvfrom) | 0x40000000u, {0}, false},
        };
        static const p64 none[6] = {0};
        net_filter filter;
        p8 landed = true;

        radio_filter_build(address_of filter);
        check("the station's filter fits its program",
              filter.used > 8 && filter.used <= NET_FILTER_STEPS);
        if (filter.used > NET_FILTER_STEPS)
                return;
        for (positive at = 0; at < filter.used; at++)
        {
                net_filter_step step = filter.steps[at];

                if (step.code == NET_FILTER_EQUAL || step.code == NET_FILTER_ANY_BIT)
                        landed = landed && at + 1 + step.jump_true < filter.used &&
                                 at + 1 + step.jump_false < filter.used;
                else if (step.code == NET_FILTER_ALWAYS)
                        landed = landed && at + 1 + step.value < filter.used;
        }
        check("every jump of the station's filter lands inside it", landed);
        check("the station's filter ends in an answer",
              filter.steps[filter.used - 1].code == NET_FILTER_RETURN);
        for (positive at = 0; at < array_count(rows); at++)
        {
                p8 name[128];
                p32 answer = net_filter_run(address_of filter, NET_APART_ARCH,
                                            rows[at].number, rows[at].arguments);

                string_copy_max_end(
                    string_copy_end(name, rows[at].allowed
                                              ? (string_address) "the station's filter allows "
                                              : (string_address) "the station's filter kills "),
                    rows[at].name, 80);
                station_check((string_address)name,
                              answer == (rows[at].allowed ? SECCOMP_RET_ALLOW
                                                          : SECCOMP_RET_KILL_PROCESS));
        }
        check("the station's filter kills a call from another architecture",
              net_filter_run(address_of filter, NET_APART_ARCH ^ 1, syscall(recvfrom), none) ==
                  SECCOMP_RET_KILL_PROCESS);
}

static fn station_statuses(void)
{
        static const struct
        {
                b64 status;
                p8 believed;
        } rows[] = {
            {0, true},
            {-13, true},
            {-113, true},
            {-4095, true},
            {-1001, true},
            {-1000 - 65535, true},
            {WIFI_REFUSE_TKIP, true},
            {WIFI_REFUSE_ELEMENTS, true},
            {WIFI_REFUSE_CONFINE, true},
            {1, false},
            {-1000 - 65536, false},
            {WIFI_REFUSE_TKIP + 1, false},
            {WIFI_REFUSE_ELEMENTS - 1, false},
            {WIFI_REFUSE_APART, false},
            {-(b64)1 << 40, false},
        };
        p8 all = true;

        for (positive at = 0; at < array_count(rows); at++)
                all = all && radio_join_status(rows[at].status) == rows[at].believed;
        check("the broker believes a join's status only as one the join returns", all);
}

#define STATION_PROBES 9

/* One probe, in a child: a user namespace of its own (its maps written by
   the parent, as unshare -r does: this kernel lets no process write its
   own), the station's confinement, then the one thing the probe is. Exit 0
   when the call came back (whatever it answered), 3 when the confinement
   was refused, 4 when there was no namespace to be had. */
static DEAD_END fn station_probe(positive which, b32 said, b32 go)
{
        p8 byte = 0;

        byte = system_call_1(syscall(unshare), 0x10000000 /* CLONE_NEWUSER */) == 0;
        if (system_write_all((positive)said, address_of byte, 1) != 1 || !byte ||
            system_read_retry((positive)go, address_of byte, 1) != 1 || !byte)
                system_call_1(syscall(exit_group), 4);
        system_close(said);
        system_close(go);
        if (!radio_station_confine())
                system_call_1(syscall(exit_group), 3);
        switch (which)
        {
        case 0:
                (void)socket_new(AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, NETLINK_GENERIC);
                break;
        case 1:
                (void)socket_new(AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, NETLINK_ROUTE);
                break;
        case 2:
                (void)socket_new(AF_PACKET, SOCK_DGRAM | SOCK_CLOEXEC,
                                 (b32)network_order_16(ETH_P_PAE));
                break;
        case 3:
                (void)system_open_at(AT_FDCWD, "/", FILE_READ | O_CLOEXEC);
                break;
        case 4:
                (void)socket_new(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
                break;
        case 5:
                (void)system_call_3(syscall(execve), (positive) "/bin/true", 0, 0);
                break;
        case 6:
                (void)system_call_4(syscall(ptrace), 0, 0, 0, 0);
                break;
        case 7:
                (void)socket_new(AF_PACKET, SOCK_RAW | SOCK_CLOEXEC,
                                 (b32)network_order_16(ETH_P_PAE));
                break;
        case 8:
                (void)system_call_5(syscall(prctl), 8 /* PR_SET_KEEPCAPS */, 1, 0, 0, 0);
                break;
        }
        system_call_1(syscall(exit_group), 0);
        __builtin_unreachable();
}

//      One of a child's maps, written from here: "0 ID 1", or the word.
static p8 station_map(bipolar child, string_address file, string_address text)
{
        p8 path[64];
        p8 address_to put = string_copy_end(path, (string_address) "/proc/");
        bipolar handle;
        positive length = string_length(text);
        p8 wrote;

        put += positive_into(put, (positive)child);
        string_copy_end(string_copy_end(put, (string_address) "/"), file);
        handle = system_open_at(AT_FDCWD, (string_address)path, FILE_WRITE | O_CLOEXEC);
        if (handle < 0)
                return false;
        wrote = system_write_all((positive)handle, text, length) == (bipolar)length;
        system_close(handle);
        return wrote;
}

static fn station_confinement(void)
{
        static const string_address names[STATION_PROBES] = {
            "opens a generic netlink socket", "opens a route netlink socket",
            "asks for the EAPOL packet socket", "dies of openat", "dies of an IPv4 socket",
            "dies of execve", "dies of ptrace", "dies of a raw packet socket", "dies of prctl"};
        p8 emulated = station_emulated();
        p8 unprivileged = false;

        //      qemu-user cannot filter itself: the confinement is refused
        //      there, before any namespace, which is what the station does.
        if (emulated)
        {
                bipolar child = system_fork();
                positive raw = 0;

                if (!child)
                        system_call_1(syscall(exit_group), radio_station_confine() ? 0 : 3);
                check("under qemu-user the station refuses rather than run unfiltered",
                      child > 0 && system_wait4_retry(child, address_of raw, 0, null) == child &&
                          (raw & 0x7f) == 0 && ((raw >> 8) & 0xff) == 3);
                log_direct(str("bowl: the station's confinement NOT RUN -- qemu-user (the refusal is checked)\n"));
                return;
        }
        for (positive at = 0; at < STATION_PROBES; at++)
        {
                b32 up[2];
                b32 down[2];
                bipolar child;
                positive raw = 0;
                p8 name[96];
                p8 byte = 0;
                p8 uid_line[32];
                p8 gid_line[32];
                p8 address_to put;

                put = string_copy_end(uid_line, (string_address) "0 ");
                put += positive_into_string(put, (positive)system_call(syscall(getuid)));
                string_copy_end(put, (string_address) " 1");
                put = string_copy_end(gid_line, (string_address) "0 ");
                put += positive_into_string(put, (positive)system_call(syscall(getgid)));
                string_copy_end(put, (string_address) " 1");
                if (system_call_2(syscall(pipe2), (positive)up, O_CLOEXEC) < 0 ||
                    system_call_2(syscall(pipe2), (positive)down, O_CLOEXEC) < 0)
                {
                        check("the station probe's pipes open", false);
                        return;
                }
                child = system_fork();
                if (!child)
                {
                        system_close(up[0]);
                        system_close(down[1]);
                        station_probe(at, up[1], down[0]);
                }
                system_close(up[1]);
                system_close(down[0]);
                //      The child waits for this byte only once it has said it
                //      has a namespace; otherwise it is gone, and a write to its
                //      pipe would be a SIGPIPE here.
                if (child > 0 && system_read_retry((positive)up[0], address_of byte, 1) == 1 &&
                    byte)
                {
                        byte = station_map(child, (string_address) "uid_map",
                                           (string_address)uid_line) &&
                               station_map(child, (string_address) "setgroups",
                                           (string_address) "deny") &&
                               station_map(child, (string_address) "gid_map",
                                           (string_address)gid_line);
                        (void)system_write_all((positive)down[1], address_of byte, 1);
                }
                system_close(up[0]);
                system_close(down[1]);
                if (child < 0 || system_wait4_retry(child, address_of raw, 0, null) != child)
                {
                        check("the station probe forks", false);
                        return;
                }
                if ((raw & 0x7f) == 0 && ((raw >> 8) & 0xff) == 4)
                {
                        unprivileged = true;
                        break;
                }
                string_copy_max_end(string_copy_end(name, (string_address) "the confined station "),
                                    names[at], 64);
                station_check((string_address)name,
                              at < 3 ? (raw & 0x7f) == 0 && ((raw >> 8) & 0xff) == 0
                                     : (raw & 0x7f) == SIGSYS);
        }
        if (unprivileged)
                log_direct(str("bowl: the station's confinement NOT RUN -- no unprivileged user namespaces\n"));
}

static fn bowl_hardened_and_limits_rows(void)
{
        check("system resource limits target NOFILE=1048576 and MEMLOCK=512M (raised for Proton's esync and the audio server's locked pages)",
              SYSTEM_NOFILE_TARGET == 1048576 && SYSTEM_MEMLOCK_TARGET == (512ULL << 20));
        check("bowl_wayland_offered returns a valid boolean without crashing",
              bowl_wayland_offered() == 0 || bowl_wayland_offered() == 1);
#if MOONWATER_STRICT_BOWL >= STRICT_TIGHT
        bowl_session_fill_started();
        check("hardened bowl refuses inherited XDG_RUNTIME_DIR=/run/user/0 unless runtime path itself is /run/user/0",
              bowl_path_same(bowl_runtime_path, BOWL_RUNTIME_DIR "0") ||
                  (bowl_session_unusable("XDG_RUNTIME_DIR=/run/user/0") &&
                   bowl_session_unusable("XDG_RUNTIME_DIR=/run/user/0/sub")));
#endif
}

/*
        The ssh server's (src/sh/ssh.c): the file written from every setting
        that changes it is the text this row spells out line by line, and no
        other; what sshd -T says is held to the settings value by value, so a
        changed or a missing value refuses; the settings, the key list and an
        address read back only as written; a public key is the shape OpenSSH
        makes and its fingerprint the one ssh-keygen -l says; the apk database
        gives a file's checksum by its folder and name; and a timed on ends at
        its second and in any other boot. With MOONWATER_SSHD (an sshd) and
        MOONWATER_SSH_STATE (a directory holding a host_ed25519_key and an
        authorized_keys), every file is also given to that sshd's -T and held
        to what it says; without them those rows say NOT RUN.
*/
static positive ssh_expected(p8 address_to into, positive room, string_address state, bool root, string_address port,
                             string_address address)
{
        static const char address_to fixed =
            "AddressFamily any\n"
            "HostKeyAlgorithms ssh-ed25519\n"
            "KexAlgorithms sntrup761x25519-sha512@openssh.com,curve25519-sha256,curve25519-sha256@libssh.org\n"
            "Ciphers aes256-gcm@openssh.com,aes128-gcm@openssh.com\n"
            "MACs hmac-sha2-512-etm@openssh.com,hmac-sha2-256-etm@openssh.com\n"
            "PubkeyAcceptedAlgorithms ssh-ed25519,sk-ssh-ed25519@openssh.com\n"
            "AllowUsers root\n"
            "AuthenticationMethods publickey\n"
            "PubkeyAuthentication yes\n"
            "PasswordAuthentication no\n"
            "KbdInteractiveAuthentication no\n"
            "PermitEmptyPasswords no\n"
            "HostbasedAuthentication no\n"
            "IgnoreRhosts yes\n"
            "AuthorizedKeysCommand none\n"
            "AuthorizedPrincipalsFile none\n"
            "AuthorizedPrincipalsCommand none\n"
            "TrustedUserCAKeys none\n"
            "StrictModes yes\n"
            "MaxAuthTries 3\n"
            "MaxSessions 4\n"
            "MaxStartups 3:50:10\n"
            "PerSourceMaxStartups 2\n"
            "LoginGraceTime 20\n"
            "ClientAliveInterval 30\n"
            "ClientAliveCountMax 3\n"
            "TCPKeepAlive no\n"
            "DisableForwarding yes\n"
            "AllowTcpForwarding no\n"
            "AllowStreamLocalForwarding no\n"
            "AllowAgentForwarding no\n"
            "X11Forwarding no\n"
            "PermitTunnel no\n"
            "GatewayPorts no\n"
            "PermitListen none\n"
            "PermitOpen none\n"
            "PermitUserEnvironment no\n"
            "PermitUserRC no\n"
            "PermitTTY yes\n"
            "PrintMotd no\n"
            "UseDNS no\n"
            "Compression no\n"
            "LogLevel VERBOSE\n"
            "PidFile none\n";

        into[0] = 0;
        string_append_bounded(into, (string_address) "# Written by moonwater ssh from its settings at every start, and overwritten:\n"
                                                     "# the server is changed with moonwater ssh, never here.\nPort ", room);
        string_append_bounded(into, port, room);
        string_append_bounded(into, (string_address) "\n", room);
        if (address[0])
        {
                string_append_bounded(into, (string_address) "ListenAddress ", room);
                string_append_bounded(into, address, room);
                string_append_bounded(into, (string_address) "\n", room);
        }
        string_append_bounded(into, (string_address) "HostKey ", room);
        string_append_bounded(into, state, room);
        string_append_bounded(into, (string_address) "/host_ed25519_key\nAuthorizedKeysFile ", room);
        string_append_bounded(into, state, room);
        string_append_bounded(into, (string_address) "/authorized_keys\nPermitRootLogin ", room);
        string_append_bounded(into, (string_address)(root ? "prohibit-password\n" : "no\n"), room);
        string_append_bounded(into, (string_address)fixed, room);
        return string_length(into);
}

// What sshd -T says of a file: each line of it in lower case, the addresses as sshd spells them, and lines it adds of its own.
static positive ssh_told_of(string_address text, p8 address_to into, positive room, string_address port, string_address address,
                            bool spelled_old)
{
        positive used = 0;

        into[0] = 0;
        string_append_bounded(into, (string_address) "x11displayoffset 10\nmodulifile /etc/ssh/moduli\n", room);
        for (string_address line = text; *line;)
        {
                string_address stop = string_first_of(line, '\n');
                positive length = stop ? (positive)(stop - line) : string_length(line);

                if (line[0] != '#' && !host_starts(line, (string_address) "ListenAddress "))
                {
                        bool keyword = true;

                        used = string_length(into);
                        for (positive at = 0; at < length && used + 2 < room; at++)
                        {
                                p8 byte = line[at];

                                keyword &= byte != ' ';
                                if (keyword && byte >= 'A' && byte <= 'Z')
                                        byte += 'a' - 'A';
                                into[used++] = byte;
                        }
                        into[used++] = '\n';
                        into[used] = 0;
                }
                line += length + (stop != null);
        }
        //      OpenSSH 9 says prohibit-password by its first name.
        if (spelled_old && string_find(into, (string_address) "permitrootlogin prohibit-password"))
        {
                string_address at = string_find(into, (string_address) "prohibit-password");
                positive rest = string_length(at + 17) + 1;

                memory_copy(at, "without-password", 16);
                for (positive byte = 0; byte < rest; byte++)
                        at[16 + byte] = at[17 + byte];
        }
        if (address[0])
        {
                bool six = string_first_of(address, ':') != null;

                string_append_bounded(into, (string_address) "listenaddress ", room);
                string_append_bounded(into, (string_address)(six ? "[" : ""), room);
                string_append_bounded(into, address, room);
                string_append_bounded(into, (string_address)(six ? "]:" : ":"), room);
                string_append_bounded(into, port, room);
                string_append_bounded(into, (string_address) "\n", room);
        }
        else
        {
                string_append_bounded(into, (string_address) "listenaddress [::]:", room);
                string_append_bounded(into, port, room);
                string_append_bounded(into, (string_address) "\nlistenaddress 0.0.0.0:", room);
                string_append_bounded(into, port, room);
                string_append_bounded(into, (string_address) "\n", room);
        }
        return string_length(into);
}

static fn ssh_rows(void)
{
        static p8 text[SSH_CONFIG_ROOM], want[SSH_CONFIG_ROOM], told[SSH_CONFIG_ROOM * 2], copy[SSH_CONFIG_ROOM * 2];
        static const struct
        {
                p16 port;
                string_address port_text;
                string_address address;
        } places[] = {{22, "22", ""}, {2222, "2222", "192.0.2.7"}, {65535, "65535", "2001:db8::7"}, {1, "1", "::1"}};
        bool exact = true, same = true, verified = true, refused = true, old_name = true, missing = true;
        positive mutants = 0, held = 0;
        string_address sshd = string_get_environment(file_environment_all(), (string_address) "MOONWATER_SSHD");
        string_address scratch = string_get_environment(file_environment_all(), (string_address) "MOONWATER_SSH_STATE");
        positive real = 0, real_held = 0;

        for (positive root = 0; root < 2; root++)
                for (positive at = 0; at < array_count(places); at++)
                {
                        ssh_settings settings;
                        p8 wrong[64];
                        positive length, told_length;

                        ssh_settings_standard(&settings);
                        settings.root = (p8)root;
                        settings.port = places[at].port;
                        string_copy_bounded(settings.address, places[at].address, sizeof(settings.address));
                        length = ssh_config_text(&settings, (string_address) "/root/ssh", text, sizeof(text));
                        exact &= length && length == ssh_expected(want, sizeof(want), (string_address) "/root/ssh", root,
                                                                   places[at].port_text, places[at].address) &&
                                 string_equals(text, want);
                        //      On or off, a limit or none, a timed on or not: the same file.
                        settings.on = true;
                        settings.limit = 5;
                        settings.until = 99;
                        string_copy_bounded(settings.boot, (string_address) "00000000-0000-0000-0000-000000000000", sizeof(settings.boot));
                        same &= ssh_config_text(&settings, (string_address) "/root/ssh", copy, sizeof(copy)) == length &&
                                string_equals(copy, text);

                        //      What sshd -T says of it is held to it, and a value changed in any line it decides refuses.
                        told_length = ssh_told_of(text, told, sizeof(told), places[at].port_text, places[at].address, false);
                        verified &= ssh_config_verify(&settings, (string_address) "/root/ssh", told, told_length, wrong, sizeof(wrong));
                        if (root)
                        {
                                told_length = ssh_told_of(text, copy, sizeof(copy), places[at].port_text, places[at].address, true);
                                old_name &= ssh_config_verify(&settings, (string_address) "/root/ssh", copy, told_length, wrong,
                                                              sizeof(wrong));
                        }
                        else
                        {
                                //      root off is no, and the old name of on is not it.
                                told_length = ssh_told_of(text, copy, sizeof(copy), places[at].port_text, places[at].address, false);
                                string_address no = string_find(copy, (string_address) "permitrootlogin no\n");

                                if (no)
                                {
                                        p8 changed[SSH_CONFIG_ROOM * 2];
                                        positive before = (positive)(no - copy);

                                        memory_copy(changed, copy, before);
                                        changed[before] = 0;
                                        string_append_bounded(changed, (string_address) "permitrootlogin without-password\n", sizeof(changed));
                                        string_append_bounded(changed, no + 19, sizeof(changed));
                                        refused &= !ssh_config_verify(&settings, (string_address) "/root/ssh", changed,
                                                                      string_length(changed), wrong, sizeof(wrong));
                                }
                                else
                                        refused = false;
                        }
                        told_length = ssh_told_of(text, told, sizeof(told), places[at].port_text, places[at].address, false);
                        for (string_address line = told; *line;)
                        {
                                string_address stop = string_first_of(line, '\n');
                                positive length_line = (positive)(stop - line);
                                positive before = (positive)(line - told);
                                p8 changed[SSH_CONFIG_ROOM * 2];
                                bool decided = !host_starts(line, (string_address) "x11displayoffset") &&
                                               !host_starts(line, (string_address) "modulifile");

                                //      The line with its value changed, and the line gone.
                                memory_copy(changed, told, before + length_line);
                                changed[before + length_line] = 0;
                                string_append_bounded(changed, (string_address) "x", sizeof(changed));
                                string_append_bounded(changed, stop, sizeof(changed));
                                if (decided)
                                {
                                        mutants++;
                                        held += !ssh_config_verify(&settings, (string_address) "/root/ssh", changed,
                                                                   string_length(changed), wrong, sizeof(wrong));
                                }
                                memory_copy(changed, told, before);
                                changed[before] = 0;
                                string_append_bounded(changed, stop + 1, sizeof(changed));
                                if (decided)
                                        missing &= !ssh_config_verify(&settings, (string_address) "/root/ssh", changed,
                                                                      string_length(changed), wrong, sizeof(wrong));
                                line = stop + 1;
                        }

                        //      And the sshd named, when one is: the file written into the scratch state, and what its -T says.
                        if (sshd && scratch)
                        {
                                p8 path[256];
                                b32 ends[2];
                                bipolar child, handle;
                                positive status = 0, got = 0;
                                bipolar read;
                                string_address words[] = {sshd, (string_address) "-T", (string_address) "-f", path, null};

                                length = ssh_config_text(&settings, scratch, text, sizeof(text));
                                (void)string_join(path, sizeof(path), scratch, (string_address) "/sshd_config");
                                handle = system_open_at_mode(AT_FDCWD, path, FILE_WRITE | O_CLOEXEC, 0600);
                                if (handle >= 0)
                                {
                                        (void)system_write_all((positive)handle, text, length);
                                        system_close(handle);
                                }
                                if (handle < 0 || system_pipe(ends, O_CLOEXEC) < 0 || (child = system_fork()) < 0)
                                        continue;
                                if (!child)
                                {
                                        system_call_3(syscall(dup3), (positive)ends[1], 1, 0);
                                        system_execute(sshd, words, file_environment_all());
                                        system_call_1(syscall(exit_group), 127);
                                }
                                system_close(ends[1]);
                                while (got + 1 < sizeof(told) &&
                                       (read = system_read_retry((positive)ends[0], told + got, sizeof(told) - 1 - got)) > 0)
                                        got += (positive)read;
                                system_close(ends[0]);
                                system_wait4_retry(child, &status, 0, null);
                                told[got] = 0;
                                real++;
                                real_held += wait_status_code(status) == 0 &&
                                             ssh_config_verify(&settings, scratch, told, got, wrong, sizeof(wrong));
                                if (wait_status_code(status) || !ssh_config_verify(&settings, scratch, told, got, wrong, sizeof(wrong)))
                                        string_format(log, "  ssh: %s -T on root %p port %p address %s: status %p, first wrong: %s\n",
                                                      sshd, root, (positive)places[at].port, places[at].address,
                                                      wait_status_code(status), (string_address)wrong);
                        }
                }
        check("ssh: the file written from each setting of root, port and address is exactly the lines spelled here", exact);
        check("ssh: whether it is on, its limit and a timed on change nothing in the file", same);
        check("ssh: what sshd -T says of the file is taken as the settings, with the addresses sshd spells", verified);
        check("ssh: root on is taken from sshd -T in either name it has (OpenSSH 9 without-password, 10 prohibit-password)", old_name);
        check("ssh: root off is refused when sshd -T says root may log in by key", refused);
        string_format(log, "  ssh: %p values sshd -T says changed one at a time, %p refused\n", mutants, held);
        check("ssh: every value sshd -T says that the settings decide, changed, refuses the start", mutants > 200 && held == mutants);
        check("ssh: every value sshd -T says that the settings decide, missing, refuses the start", missing);
        if (sshd && scratch)
        {
                string_format(log, "  ssh: %s -T held to %p of %p files\n", sshd, real_held, real);
                check("ssh: the sshd named takes every file written, and says of each what the settings decide", real == 8 && real_held == real);
        }
        else
                log_direct(str("ssh: the files given to a real sshd -T NOT RUN -- MOONWATER_SSHD and MOONWATER_SSH_STATE unset\n"));

        //      The settings, the list and the addresses read back only as they are written.
        {
                ssh_settings settings, back;
                p8 kept[512];
                positive length;
                static const char address_to bad[] = {
                    "on\nwhat 1\n", "port 0\n", "port 65536\n", "port 022\n", "limit 0\n", "limit 1441\n", "root yes\n",
                    "address 2001:DB8::7\n", "address 1.2.3.4.5\n", "until 1234 5\n", "until 00000000-0000-0000-0000-00000000000g 5\n",
                    "on \n", "address [::1]\n"};
                bool refuses = true, round = true;

                ssh_settings_standard(&settings);
                check("ssh: the standard is off, root off, port 22, every address, 60 minutes",
                      !settings.on && !settings.root && settings.port == 22 && !settings.address[0] && settings.limit == 60);
                settings.on = settings.root = true;
                settings.port = 2200;
                settings.limit = 0;
                string_copy_bounded(settings.address, (string_address) "2001:db8::7", sizeof(settings.address));
                string_copy_bounded(settings.boot, (string_address) "0123abcd-0000-1111-2222-333344445555", sizeof(settings.boot));
                settings.until = 123456;
                length = ssh_settings_text(&settings, kept, sizeof(kept));
                round &= length && ssh_settings_parse(kept, length, &back) && back.on && back.root && back.port == 2200 &&
                         !back.limit && string_equals(back.address, (string_address) "2001:db8::7") && back.until == 123456 &&
                         string_equals(back.boot, settings.boot);
                for (positive at = 0; at < sizeof(bad) / sizeof(bad[0]); at++)
                        refuses &= !ssh_settings_parse((string_address)bad[at], string_length((string_address)bad[at]), &back);
                check("ssh: the settings read back as they were written", round);
                check("ssh: a settings line that is not one of the file's is no settings at all", refuses);

                //      A timed on ends at its second, and in any boot but its own.
                check("ssh: a timed on is not over before its second, in its boot",
                      !ssh_expired_at(&settings, (string_address) "0123abcd-0000-1111-2222-333344445555", 123455));
                check("ssh: a timed on is over at its second, and in another boot whatever is left",
                      ssh_expired_at(&settings, (string_address) "0123abcd-0000-1111-2222-333344445555", 123456) &&
                          ssh_expired_at(&settings, (string_address) "ffffffff-0000-1111-2222-333344445555", 1));
                settings.until = 0;
                check("ssh: an on with no limit is never over", !ssh_expired_at(&settings, (string_address) "x", (p64)1 << 40));
        }
        {
                static const struct
                {
                        string_address typed;
                        string_address normal;
                } addresses[] = {{"192.0.2.7", "192.0.2.7"},   {"::1", "::1"},
                                 {"2001:DB8:0:0:0:0:0:7", "2001:db8::7"}, {"1:0:0:2:0:0:0:3", "1:0:0:2::3"},
                                 {"1:0:0:0:2:0:0:3", "1::2:0:0:3"},      {"0:0:0:0:0:0:0:0", "::"},
                                 {"1:2:3:4:5:6:7:0", "1:2:3:4:5:6:7:0"}, {"fe80::1%eth0", null},
                                 {"::ffff:192.0.2.7", null},             {"256.1.1.1", null},
                                 {"[::1]", null},                        {"host.example", null},
                                 {"", null}};
                bool all = true;

                for (positive at = 0; at < sizeof(addresses) / sizeof(addresses[0]); at++)
                {
                        p8 normal[48];
                        bool took = ssh_address_normal(addresses[at].typed, normal, sizeof(normal));

                        all &= addresses[at].normal ? took && string_equals(normal, addresses[at].normal) : !took;
                }
                check("ssh: an address is kept as sshd -T says it back (RFC 5952), and a zone, a bracket or a name is none", all);
        }
        {
                //      Two keys of no private half: 32 bytes counted up, as an Ed25519 and as a security key's; ssh-keygen -l said these.
                static p8 list[4096];
                static ssh_keys keys;
                ssh_key one;
                p8 print[64];
                static const char address_to ed =
                    "AAAAC3NzaC1lZDI1NTE5AAAAIAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8g";
                static const char address_to sk =
                    "AAAAGnNrLXNzaC1lZDI1NTE5QG9wZW5zc2guY29tAAAAICEiIyQlJicoKSorLC0uLzAxMjM0NTY3ODk6Ozw9Pj9AAAAABHNzaDo=";
                static const char address_to bad[] = {
                    "restrict ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8g a\n",
                    "restrict,pty ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8g\n",
                    "restrict,pty ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8g a b\n",
                    "restrict,pty ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwd a\n",
                    "restrict,pty ssh-rsa AAAAC3NzaC1lZDI1NTE5AAAAIAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8g a\n",
                    "restrict,pty ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAHwECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8= a\n",
                    "restrict,pty ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8gAA== a\n",
                    "restrict,pty sk-ssh-ed25519@openssh.com AAAAC3NzaC1lZDI1NTE5AAAAIAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8g a\n",
                    "restrict,pty ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8g -a\n",
                    "restrict,pty ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8g a\n"
                    "restrict,pty sk-ssh-ed25519@openssh.com AAAAGnNrLXNzaC1lZDI1NTE5QG9wZW5zc2guY29tAAAAICEiIyQlJicoKSorLC0uLzAxMjM0NTY3ODk6Ozw9Pj9AAAAABHNzaDo= a\n"};
                bool refuses = true;
                positive length;

                memory_zero(&one, sizeof(one));
                string_copy_bounded(one.type, (string_address) "ssh-ed25519", sizeof(one.type));
                string_copy_bounded(one.blob, (string_address)ed, sizeof(one.blob));
                check("ssh: an Ed25519 key's fingerprint is the one ssh-keygen -l says",
                      ssh_key_fingerprint(&one, print) && string_equals(print, (string_address) "SHA256:mKqU+0K8OhKmA8bBQi9Rz0Q5l7/g160hIP+rJYSTNj4"));
                string_copy_bounded(one.type, (string_address) "sk-ssh-ed25519@openssh.com", sizeof(one.type));
                string_copy_bounded(one.blob, (string_address)sk, sizeof(one.blob));
                check("ssh: a security key's fingerprint is the one ssh-keygen -l says",
                      ssh_key_fingerprint(&one, print) && string_equals(print, (string_address) "SHA256:c/3vAQK71VH3tiPw+eWPowglqzC45ljISgdYDmpjCGE"));
                (void)string_join(list, sizeof(list), (string_address) "restrict,pty ssh-ed25519 ", (string_address)ed,
                                  (string_address) " laptop\nrestrict,pty sk-ssh-ed25519@openssh.com ", (string_address)sk,
                                  (string_address) " yubikey.2\n");
                length = string_length(list);
                check("ssh: the list as moonwater ssh writes it is read back, key by key, label by label",
                      ssh_keys_parse(list, length, &keys) && keys.count == 2 &&
                          string_equals(keys.keys[0].label, (string_address) "laptop") &&
                          string_equals(keys.keys[1].label, (string_address) "yubikey.2") &&
                          ssh_keys_text(&keys, copy, sizeof(copy)) == length && string_equals(copy, list));
                for (positive at = 0; at < sizeof(bad) / sizeof(bad[0]); at++)
                        refuses &= !ssh_keys_parse((string_address)bad[at], string_length((string_address)bad[at]), &keys);
                check("ssh: a list line with other options, no label or two, a key that is not well made or not its type, or a label twice, is not the list",
                      refuses);
        }
        {
                //      The apk database, as Alpine 3.24 writes it: each R: under the F: before it, and Z: its checksum.
                static const char address_to database =
                    "C:Q1aaa\nP:openssh-keygen\nV:10.3_p1-r2\nF:usr\nF:usr/bin\nR:ssh-keygen\nZ:Q1GnzRSE5p51SMLcbJfJrcfwP/1Xw=\n\n"
                    "C:Q1bbb\nP:openssh-server\nV:10.3_p1-r2\nF:usr\nF:usr/lib\nF:usr/lib/ssh\nR:sshd-auth\nZ:Q1xWQNzLq8rui0LuT7kqZAqCHqKcA=\n"
                    "R:sshd-session\nZ:Q14kXrTPQDRhyA/177gNP2SMV6dlQ=\nF:usr/sbin\nR:sshd\na:0:0:755\nZ:Q1oUoNK0kI5i7J9YNlnrkr2kAg7Mc=\n\n"
                    "P:other\nR:sshd\nZ:Q1notthis\n";
                p8 root[96], path[160], sum[48];
                static p8 files[16][128];
                bipolar handle;
                positive count;

                (void)string_join(root, sizeof(root), (string_address)BOWL_ROOT_PREFIX, (string_address) "sshdb");
                (void)string_join(path, sizeof(path), root, (string_address) "/lib/apk/db");
                (void)bowl_mkdir_parents(path);
                (void)string_join(path, sizeof(path), root, (string_address) "/lib/apk/db/installed");
                handle = system_open_at_mode(AT_FDCWD, path, FILE_WRITE | O_CLOEXEC, 0644);
                if (handle >= 0)
                {
                        (void)system_write_all((positive)handle, (string_address)database, string_length((string_address)database));
                        system_close(handle);
                }
                check("ssh: the database gives a file its own checksum, by its folder and its name",
                      ssh_apk_checksum(root, (string_address) "usr/sbin/sshd", sum, sizeof(sum)) &&
                          string_equals(sum, (string_address) "Q1oUoNK0kI5i7J9YNlnrkr2kAg7Mc=") &&
                          ssh_apk_checksum(root, (string_address) "usr/lib/ssh/sshd-session", sum, sizeof(sum)) &&
                          string_equals(sum, (string_address) "Q14kXrTPQDRhyA/177gNP2SMV6dlQ=") &&
                          !ssh_apk_checksum(root, (string_address) "usr/bin/sshd", sum, sizeof(sum)));
                count = ssh_apk_package(root, (string_address) "openssh-server", files, 16);
                check("ssh: the files of a package are the R: lines of that package, each with its folder",
                      count == 3 && string_equals(files[0], (string_address) "/usr/lib/ssh/sshd-auth") &&
                          string_equals(files[1], (string_address) "/usr/lib/ssh/sshd-session") &&
                          string_equals(files[2], (string_address) "/usr/sbin/sshd"));
        }
        {
                //      An ELF file names its loader and libraries, and a file that is none is not taken for one.
                ssh_needed needed;
                bool elf = false;
                bipolar handle = system_open_at(AT_FDCWD, (string_address) "/proc/self/exe", FILE_READ | O_CLOEXEC);
                bool read = handle >= 0 && ssh_elf_needed(handle, &needed, &elf);

                if (handle >= 0)
                        system_close(handle);
                check("ssh: this static check is an ELF file that names no loader and no library", read && elf && !needed.loader[0] && !needed.count);
                handle = system_open_at(AT_FDCWD, (string_address) "/proc/self/cmdline", FILE_READ | O_CLOEXEC);
                read = handle >= 0 && ssh_elf_needed(handle, &needed, &elf);
                if (handle >= 0)
                        system_close(handle);
                check("ssh: a file that is no ELF is not taken for one", read && !elf);
                handle = system_open_at(AT_FDCWD, (string_address) "/usr/bin/env", FILE_READ | O_CLOEXEC);
                read = handle >= 0 && ssh_elf_needed(handle, &needed, &elf);
                if (handle >= 0)
                        system_close(handle);
                if (read && elf && needed.loader[0])
                {
                        bool libc = false;

                        for (positive at = 0; at < needed.count; at++)
                                libc |= host_starts(needed.names[at], (string_address) "libc.");
                        check("ssh: a program of the host names its loader and its libc", host_starts(needed.loader, (string_address) "/lib") && libc);
                }
                else
                        log_direct(str("ssh: a dynamic program's loader and libraries NOT RUN -- /usr/bin/env here is not one\n"));
        }
}

/* Growth in memory: no count of a command's arguments is capped. A guest
   program run with a hundred arguments still goes through the wrapper, and
   seventeen package files are each bound into the view, none passed through
   as a host path. */
static bool grow_file(string_address path, string_address text, positive length)
{
        bipolar handle = system_open_at_mode(AT_FDCWD, path,
                                             O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC,
                                             0644);

        if (handle < 0)
                return false;
        {
                bool wrote = system_write_all((positive)handle, text, length) == length;

                system_close(handle);
                return wrote;
        }
}

static fn grow_rows(void)
{
        static string_address const input_names[] = {
            BOWL_ROOT_PREFIX "growin/pkg01.deb", BOWL_ROOT_PREFIX "growin/pkg02.deb",
            BOWL_ROOT_PREFIX "growin/pkg03.deb", BOWL_ROOT_PREFIX "growin/pkg04.deb",
            BOWL_ROOT_PREFIX "growin/pkg05.deb", BOWL_ROOT_PREFIX "growin/pkg06.deb",
            BOWL_ROOT_PREFIX "growin/pkg07.deb", BOWL_ROOT_PREFIX "growin/pkg08.deb",
            BOWL_ROOT_PREFIX "growin/pkg09.deb", BOWL_ROOT_PREFIX "growin/pkg10.deb",
            BOWL_ROOT_PREFIX "growin/pkg11.deb", BOWL_ROOT_PREFIX "growin/pkg12.deb",
            BOWL_ROOT_PREFIX "growin/pkg13.deb", BOWL_ROOT_PREFIX "growin/pkg14.deb",
            BOWL_ROOT_PREFIX "growin/pkg15.deb", BOWL_ROOT_PREFIX "growin/pkg16.deb",
            BOWL_ROOT_PREFIX "growin/pkg17.deb"};
        string_address wrap_argv[104];
        string_address *wrap_av = wrap_argv;
        positive wrap_count = 100;
        string_address input_argv[19];
        string_address *input_av;
        positive bound = 0;
        bool made = true, wrapped;

        (void)bowl_mkdir_parents(BOWL_ROOT_PREFIX "growcheck/usr/bin");
        (void)bowl_mkdir_parents(BOWL_ROOT_PREFIX "growin");
        made = grow_file(BOWL_ROOT_PREFIX "growcheck/usr/bin/bigtool", "\177ELF\n", 5) && made;
        for (positive at = 0; at < 17; at++)
                made = grow_file(input_names[at], "deb\n", 4) && made;

        wrap_argv[0] = "/usr/bin/bigtool";
        for (positive at = 1; at < wrap_count; at++)
                wrap_argv[at] = "arg";
        wrap_argv[wrap_count] = null;
        wrapped = bowl_wrap_command(BOWL_ROOT_PREFIX "growcheck/usr/bin/bigtool", "/",
                                    &wrap_av, &wrap_count);
        check("wrap: a guest program run with a hundred arguments goes through the wrapper, with every argument kept after its program",
              wrapped && wrap_count == 102 && wrap_av[0] == BOWL_PROGRAM &&
                  wrap_av[101] == wrap_argv[99] && wrap_av[102] == null);

        input_argv[0] = "dpkg";
        for (positive at = 0; at < 17; at++)
                input_argv[at + 1] = input_names[at];
        input_argv[18] = null;
        input_av = bowl_inputs_take(input_argv);
        for (positive at = 1; at <= 17; at++)
                if (input_av[at] != input_argv[at])
                        bound++;
        check("inputs: seventeen package files are each bound into the view, none passed through as a host path",
              made && bound == 17);
}

/*
        The shader caches warmed again after the graphics driver changed (bowl_shaders): the driver's identity is its graphics packages'
        installed versions (pacman's local database), and the warm runs only when that is not the identity it last warmed for, and waits
        while a game Steam launched runs (its reaper's command line). A tree of the check's own stands for the bowl and for /proc.
*/
static void shaders_file(string_address path, const char *text, positive size)
{
        bipolar handle = system_open_at_mode(AT_FDCWD, path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);

        if (handle >= 0)
                system_write_once(handle, text, size), system_close(handle);
}

static fn shader_warm_rows(void)
{
        p8 root[96] = "/tmp/moonwater-bowl-check-shaders-", proc[96] = "/tmp/moonwater-bowl-check-proc-"; // this run's own (a pid each)
        static const string_address dirs[] = {"", "/var", "/var/lib", "/var/lib/pacman", "/var/lib/pacman/local",
                                              "/var/lib/pacman/local/mesa-1:26.3.1-2", "/var/lib/pacman/local/lib32-mesa-1:26.3.1-2",
                                              "/var/lib/pacman/local/vulkan-radeon-1:26.3.1-2", "/var/lib/pacman/local/llvm-libs-23.1.5-1",
                                              "/var/lib/pacman/local/mesa-utils-9.0.0-1", "/var/lib/pacman/local/zlib-1:1.3.1-2", null};
        static const char expected[] = "lib32-mesa-1:26.3.1-2\nllvm-libs-23.1.5-1\nmesa-1:26.3.1-2\nvulkan-radeon-1:26.3.1-2\n";
        static const char reaper[] = "/home/x/.local/share/Steam/ubuntu12_32/reaper\0SteamLaunch\0AppId=489830\0--\0/x/proton\0waitforexitandrun\0";
        p8 path[256], stamp[1024], again[1024];
        positive length, second, self = (positive)system_call_1(syscall(getpid), 0), at_root = string_length(root), at_proc = string_length(proc);

        at_root += positive_into_string((string_address)root + at_root, self), root[at_root] = 0;
        at_proc += positive_into_string((string_address)proc + at_proc, self), proc[at_proc] = 0;
        for (positive at = 0; dirs[at]; at++)
                if (bowl_root_path(path, sizeof path, (string_address)root, dirs[at]))
                        system_call_3(syscall(mkdirat), (positive)AT_FDCWD, (positive)path, 0700);
        length = bowl_graphics_stamp((string_address)root, stamp, sizeof stamp);
        check("shaders: the driver's identity is its graphics packages' installed versions, sorted, a line each, and no other package's",
              length == sizeof expected - 1 && !memory_compare(stamp, expected, length));
        check("shaders: the NVIDIA driver's packages are of the identity too, in 64 and 32 bits, by their names and a version after them",
              bowl_graphics_package("vulkan-nouveau-1:26.3.1-3") && bowl_graphics_package("lib32-vulkan-nouveau-1:26.3.1-3") &&
                  !bowl_graphics_package("vulkan-nouveau") && !bowl_graphics_package("vulkan-nouveau-git-1"));
        // The bowl's update: Mesa at its next version.
        bowl_root_path(path, sizeof path, (string_address)root, "/var/lib/pacman/local/mesa-1:26.3.1-2");
        system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)path, 0x200); // AT_REMOVEDIR
        bowl_root_path(path, sizeof path, (string_address)root, "/var/lib/pacman/local/mesa-1:26.3.2-1");
        system_call_3(syscall(mkdirat), (positive)AT_FDCWD, (positive)path, 0700);
        second = bowl_graphics_stamp((string_address)root, again, sizeof again);
        check("shaders: an update of Mesa is another identity", second == length && memory_compare(stamp, again, length) != 0);
        check("shaders: the caches warmed for this identity are left alone, and with no graphics driver there is nothing to warm",
              bowl_shaders_step(again, second, again, (bipolar)second, false) == BOWL_WARM_DONE &&
                  bowl_shaders_step(again, 0, stamp, (bipolar)length, false) == BOWL_WARM_DONE);
        check("shaders: after an update they are warmed at once at idle, and wait while a game runs, and the first warm is one too",
              bowl_shaders_step(again, second, stamp, (bipolar)length, false) == BOWL_WARM_RUN &&
                  bowl_shaders_step(again, second, stamp, (bipolar)length, true) == BOWL_WARM_WAIT &&
                  bowl_shaders_step(again, second, null, -2, false) == BOWL_WARM_RUN);
        system_call_3(syscall(mkdirat), (positive)AT_FDCWD, (positive)proc, 0700);
        bowl_root_path(path, sizeof path, (string_address)proc, "/4242");
        system_call_3(syscall(mkdirat), (positive)AT_FDCWD, (positive)path, 0700);
        bowl_root_path(path, sizeof path, (string_address)proc, "/4242/cmdline");
        shaders_file(path, "/usr/bin/steam\0-silent\0", 22);
        {
                b32 idle = !bowl_steam_game_running((string_address)proc), game;

                shaders_file(path, reaper, sizeof reaper - 1);
                game = bowl_steam_game_running((string_address)proc);
                check("shaders: a game Steam launched is seen by its reaper's command line, and Steam alone is no game", idle && game);
        }
        {
                positive appid = 0, reaper = bowl_steam_launch((string_address)proc, address_of appid);

                check("prewarm: the launch is its reaper's pid and the AppId its command line names", reaper == 4242 && appid == 489830);
        }
}

/*
        A game's files read in as Steam launches it (bowl_prewarm): the launch's DIRs (steamapps/common/DIR in its reaper's command line, the
        runtime's and the game's), the game's shader caches less Steam's Fossilize databases, and its prefix, taken through the reaper's root.
        A library of the check's own, and a /proc of its own whose reaper is this process (the launch goes on while it lives) and whose root
        is the machine's.
*/
static void prewarm_tree_file(string_address root, string_address leaf, positive size)
{
        static char bytes[8192];
        p8 path[512];
        positive length = string_length(root);

        memory_copy(path, root, length);
        for (positive at = 0; leaf[at]; at++)
        {
                path[length++] = (p8)leaf[at], path[length] = 0;
                if (leaf[at] == '/')
                        path[length - 1] = 0, system_call_3(syscall(mkdirat), (positive)AT_FDCWD, (positive)path, 0700), path[length - 1] = '/';
        }
        path[length] = 0;
        shaders_file((string_address)path, bytes, size);
}

static fn prewarm_rows(void)
{
        p8 root[96] = "/tmp/moonwater-bowl-check-prewarm-", proc[160], line[1024], path[256];
        positive self = (positive)system_call_1(syscall(getpid), 0), at = string_length(root), size = 0, seen;

        at += positive_into_string((string_address)root + at, self), root[at] = 0;
        system_call_3(syscall(mkdirat), (positive)AT_FDCWD, (positive)root, 0700);
        prewarm_tree_file((string_address)root, "/lib/steamapps/common/Runtime/entry", 100);
        prewarm_tree_file((string_address)root, "/lib/steamapps/common/Game/game.exe", 300);
        prewarm_tree_file((string_address)root, "/lib/steamapps/common/Game/data/a.bsa", 5000);
        prewarm_tree_file((string_address)root, "/lib/steamapps/shadercache/489830/mesa_shader_cache_sf/cache", 700);
        prewarm_tree_file((string_address)root, "/lib/steamapps/shadercache/489830/fozpipelinesv6/steam.foz", 900);
        prewarm_tree_file((string_address)root, "/lib/steamapps/compatdata/489830/pfx/x.dll", 50);
        prewarm_tree_file((string_address)root, "/lib/steamapps/common/Other/never", 4000);
        // The /proc: this process's command line is the reaper's, and its root is the machine's /
        bowl_root_path(proc, sizeof proc, (string_address)root, "/proc");
        system_call_3(syscall(mkdirat), (positive)AT_FDCWD, (positive)proc, 0700);
        at = string_length(proc), proc[at++] = '/', at += positive_into_string((string_address)proc + at, self), proc[at] = 0;
        system_call_3(syscall(mkdirat), (positive)AT_FDCWD, (positive)proc, 0700);
        bowl_root_path(path, sizeof path, (string_address)proc, "/root");
        system_call_3(syscall(symlinkat), (positive)"/", (positive)AT_FDCWD, (positive)path);
        {
                static const char *const words[] = {"reaper", "SteamLaunch", "AppId=489830", "--", "/lib/steamapps/common/Runtime/entry", "--",
                                                    "/lib/steamapps/common/Runtime/entry", "/lib/steamapps/common/Game/game.exe", null};

                for (positive k = 0; words[k]; k++)
                {
                        positive length = string_length(words[k]);

                        if (words[k][0] == '/')
                                memory_copy(line + size, root, string_length(root)), size += string_length(root);
                        memory_copy(line + size, words[k], length), size += length, line[size++] = 0;
                }
        }
        bowl_root_path(path, sizeof path, (string_address)proc, "/cmdline");
        shaders_file((string_address)path, (const char *)line, size);
        proc[string_length(proc) - positive_into_string((string_address)line, self) - 1] = 0;
        seen = bowl_prewarm_launch((string_address)proc, self, 489830);
        check("prewarm: a launch reads in its DIRs, the game's shader caches less the Fossilize databases, and its prefix, and nothing else",
              seen == 100 + 300 + 5000 + 700 + 50);
        check("prewarm: a DIR's path is the word's up to DIR, and a word under no steamapps/common is none",
              bowl_prewarm_common((const p8 *)"/a/steamapps/common/Proton 11.0/proton", 38) == 31 &&
                  bowl_prewarm_common((const p8 *)"/a/steamapps/common/Game", 24) == 24 && bowl_prewarm_common((const p8 *)"/a/b/c", 6) == 0);
}

static fn security_remediation_rows(void)
{
        positive self = (positive)system_call_1(syscall(getpid), 0);

        /* BOWL-001: bowl_extract_nix must not follow a symlink /store to chmod host files. */
        {
                static const p8 script[] =
                    "#!/bin/sh\n"
                    "nix=\"/nix/store/0123456789abcdfghijklmnpqrsvwxyz-nix-9.9\"\n"
                    "cacert=\"/nix/store/zyxwvsrqpnmlkjihgfdcba9876543210-nss-cacert-1.0\"\n";
                static string_address members[] = {
                    "nix-sym/install", "nix-sym/.reginfo", "nix-sym/store", null};
                p8 victim[128] = "/tmp/moonwater-bowl-check-nix-victim-";
                p8 base[256], root[256], archive[256], linkpath[256];
                positive vlen = string_length((string_address)victim);
                bipolar vfd;

                vlen += positive_into_string((string_address)victim + vlen, self);
                victim[vlen] = 0;
                system_remove_at(AT_FDCWD, (string_address)victim, 0);
                vfd = system_open_at_mode(AT_FDCWD, (string_address)victim,
                                          FILE_WRITE | FILE_CREATE | FILE_TRUNCATE | O_CLOEXEC, 0600);
                if (vfd >= 0)
                {
                        system_call_2(syscall(fchmod), (positive)vfd, 0600);
                        system_close(vfd);
                }

                unpack_path(base, "nixsymwork", "");
                unpack_path(root, "nixsymroot", "");
                bowl_forget_path(base);
                bowl_forget_path(root);
                unpack_directory(base, "/nix-sym");
                string_copy_bounded(archive, base, sizeof archive);
                string_append_bounded(archive, "/sym.tar", sizeof archive);
                string_copy_bounded(linkpath, base, sizeof linkpath);
                string_append_bounded(linkpath, "/nix-sym/store", sizeof linkpath);
                system_remove_at(AT_FDCWD, (string_address)linkpath, 0);
                p8 made = vfd >= 0 &&
                            unpack_write(base, "/nix-sym/install", script, 0755) &&
                            unpack_write(base, "/nix-sym/.reginfo", "registered\n", 0644) &&
                            system_symbolic_link_at((string_address)victim, AT_FDCWD, (string_address)linkpath) == 0 &&
                            unpack_tar(archive, base, false, members);
                b32 landed = 0;
                if (made)
                {
                        bowl_quiet = true;
                        landed = bowl_land(archive, root, "/nix/.reginfo", bowl_extract_nix);
                        bowl_quiet = false;
                }
                positive victim_mode = unpack_mode((string_address)victim, "");
                check("BOWL-001: bowl_extract_nix rejects symlink /store and does not chmod host target",
                      made && landed != 0 && victim_mode == 0600);
                system_remove_at(AT_FDCWD, (string_address)victim, 0);
                bowl_forget_path(base);
                bowl_forget_path(root);
        }

        /* BOWL-002: bowl_accounts_add must reject non-canonical UID 0 / 1000 spellings, CR, and NUL. */
        {
                static const char host[] =
                    "root:x:0:0:root:/root:/bin/sh\n"
                    "user:x:1000:1000:user:/root:/bin/sh\n";
                static const char evil[] =
                    "evil0:x:00:0::/root:/bin/sh\n"
                    "evil1000:x:01000:1000::/:/bin/sh\n"
                    "plus0:x:+0:0::/:/bin/sh\n"
                    "wrap0:x:4294967296:0::/:/bin/sh\n"
                    "cruser:x:200:200::/:/bin/sh\r\n";
                static const p8 nul_line[] = "nuluser:x:201:201:\0:/:/bin/sh\n";
                static const char good[] = "polkitd:x:102:102::/:/sbin/nologin\n";
                p8 text[512];
                positive init_used = sizeof(host) - 1;
                positive after_evil, after_nul, after_good;

                memory_copy(text, host, init_used);
                after_evil = bowl_accounts_add(text, init_used, sizeof text,
                                               (string_address)evil, sizeof(evil) - 1, "guest");
                after_nul = bowl_accounts_add(text, init_used, sizeof text,
                                              (string_address)nul_line, sizeof(nul_line) - 1, "guest");
                after_good = bowl_accounts_add(text, init_used, sizeof text,
                                               (string_address)good, sizeof(good) - 1, "guest");
                check("BOWL-002: bowl_accounts_add rejects non-canonical UID 0/1000, overflow, CR, and NUL",
                      after_evil == init_used && after_nul == init_used && after_good > init_used);
        }

        /* BOWL-003: bowl_prewarm must reject '..' traversal, intermediate symlinks, deep recursion, and FIFOs. */
        {
                p8 base[128] = "/tmp/moonwater-bowl-check-prewarm-sec-";
                p8 proc[192], proc_pid[224], path[512], line[1024];
                positive blen = string_length((string_address)base);
                positive seen_dotdot, seen_sym, seen_deep;

                blen += positive_into_string((string_address)base + blen, self);
                base[blen] = 0;
                bowl_forget_path((string_address)base);
                system_call_3(syscall(mkdirat), (positive)AT_FDCWD, (positive)base, 0700);

                bowl_root_path(proc, sizeof proc, (string_address)base, "/proc");
                system_call_3(syscall(mkdirat), (positive)AT_FDCWD, (positive)proc, 0700);
                memory_copy(proc_pid, proc, string_length((string_address)proc));
                positive plen = string_length((string_address)proc);
                proc_pid[plen++] = '/';
                plen += positive_into_string((string_address)proc_pid + plen, self);
                proc_pid[plen] = 0;
                system_call_3(syscall(mkdirat), (positive)AT_FDCWD, (positive)proc_pid, 0700);
                bowl_root_path(path, sizeof path, (string_address)proc_pid, "/root");
                system_call_3(syscall(symlinkat), (positive)"/", (positive)AT_FDCWD, (positive)path);

                /* Subcase A: '..' traversal out of steamapps/common/ */
                prewarm_tree_file((string_address)base, "/lib1/steamapps/secret", 200);
                prewarm_tree_file((string_address)base, "/lib1/steamapps/common/placeholder", 10);
                {
                        positive sz = 0;
                        static const char prefix[] = "reaper\0SteamLaunch\0AppId=1\0";
                        static const char suffix[] = "/lib1/steamapps/common/..";

                        memory_copy(line + sz, prefix, sizeof(prefix) - 1);
                        sz += sizeof(prefix) - 1;
                        memory_copy(line + sz, base, blen);
                        sz += blen;
                        memory_copy(line + sz, suffix, sizeof(suffix));
                        sz += sizeof(suffix);
                        bowl_root_path(path, sizeof path, (string_address)proc_pid, "/cmdline");
                        shaders_file((string_address)path, (const char *)line, sz);
                }
                seen_dotdot = bowl_prewarm_launch((string_address)proc, self, 1);

                /* Subcase B: intermediate symlink steamapps/common -> outside */
                prewarm_tree_file((string_address)base, "/outside/Game/game.exe", 300);
                prewarm_tree_file((string_address)base, "/lib2/steamapps/marker", 10);
                {
                        p8 target[256];
                        bowl_root_path(target, sizeof target, (string_address)base, "/outside");
                        bowl_root_path(path, sizeof path, (string_address)base, "/lib2/steamapps/common");
                        system_call_3(syscall(symlinkat), (positive)target, (positive)AT_FDCWD, (positive)path);

                        positive sz = 0;
                        static const char prefix[] = "reaper\0SteamLaunch\0AppId=2\0";
                        static const char suffix[] = "/lib2/steamapps/common/Game/game.exe";

                        memory_copy(line + sz, prefix, sizeof(prefix) - 1);
                        sz += sizeof(prefix) - 1;
                        memory_copy(line + sz, base, blen);
                        sz += blen;
                        memory_copy(line + sz, suffix, sizeof(suffix));
                        sz += sizeof(suffix);
                        bowl_root_path(path, sizeof path, (string_address)proc_pid, "/cmdline");
                        shaders_file((string_address)path, (const char *)line, sz);
                }
                seen_sym = bowl_prewarm_launch((string_address)proc, self, 2);

                /* Subcase C: deep recursion > 32 levels */
                {
                        p8 deep_leaf[256] = "/lib3/steamapps/common/Deep";
                        positive dlen = string_length((string_address)deep_leaf);
                        for (positive lvl = 0; lvl < 40; lvl++)
                        {
                                deep_leaf[dlen++] = '/';
                                deep_leaf[dlen++] = 'd';
                        }
                        memory_copy(deep_leaf + dlen, "/deep.bin", sizeof("/deep.bin"));
                        prewarm_tree_file((string_address)base, (string_address)deep_leaf, 400);

                        positive sz = 0;
                        static const char prefix[] = "reaper\0SteamLaunch\0AppId=3\0";
                        static const char suffix[] = "/lib3/steamapps/common/Deep/entry";

                        memory_copy(line + sz, prefix, sizeof(prefix) - 1);
                        sz += sizeof(prefix) - 1;
                        memory_copy(line + sz, base, blen);
                        sz += blen;
                        memory_copy(line + sz, suffix, sizeof(suffix));
                        sz += sizeof(suffix);
                        bowl_root_path(path, sizeof path, (string_address)proc_pid, "/cmdline");
                        shaders_file((string_address)path, (const char *)line, sz);
                }
                seen_deep = bowl_prewarm_launch((string_address)proc, self, 3);

                /* Subcase D: FIFO must not block bowl_prewarm_file */
                p8 fifo_ok = false;
                {
                        p8 fdir[256], fpath[256];
                        bowl_root_path(fdir, sizeof fdir, (string_address)base, "/fifodir");
                        system_call_3(syscall(mkdirat), (positive)AT_FDCWD, (positive)fdir, 0700);
                        bowl_root_path(fpath, sizeof fpath, (string_address)fdir, "/pipe");
                        if (system_call_4(syscall(mknodat), (positive)AT_FDCWD, (positive)fpath, 0010600, 0) == 0)
                        {
                                bipolar dirfd = system_open_at(AT_FDCWD, (string_address)fdir, FILE_READ | O_DIRECTORY | O_CLOEXEC);
                                if (dirfd >= 0)
                                {
                                        bipolar child = system_fork();
                                        if (child == 0)
                                        {
                                                struct bowl_prewarm w;
                                                memory_fill(address_of w, 0, sizeof w);
                                                w.reaper = self;
                                                w.budget = 1 << 20;
                                                bowl_prewarm_file(address_of w, dirfd, "pipe");
                                                exit(0);
                                        }
                                        if (child > 0)
                                        {
                                                positive status = 0;
                                                for (positive tries = 0; tries < 25; tries++)
                                                {
                                                        bipolar got = system_call_4(syscall(wait4), (positive)child,
                                                                                    (positive)address_of status, 1 /* WNOHANG */, 0);
                                                        if (got == child)
                                                        {
                                                                fifo_ok = !wait_status_code(status);
                                                                break;
                                                        }
                                                        struct { bipolar sec; bipolar nsec; } ts = {0, 10000000};
                                                        system_call_2(syscall(nanosleep), (positive)address_of ts, 0);
                                                }
                                                if (!fifo_ok)
                                                {
                                                        system_call_2(syscall(kill), (positive)child, 9);
                                                        system_wait4_retry(child, address_of status, 0, null);
                                                }
                                        }
                                        system_close(dirfd);
                                }
                        }
                }

                check("BOWL-003: bowl_prewarm refuses '..', intermediate symlinks, deep recursion, and FIFO blocking",
                      seen_dotdot == 0 && seen_sym == 0 && seen_deep == 0 && fifo_ok);
                bowl_forget_path((string_address)base);
        }

        /* BOWL-004: bowl_guest_install must pin dirfd and reject paths escaping directory_path. */
        {
                static const struct bowl_component comps[] = {{"arch", null, 0, null}, {null, null, 0, null}};
                struct bowl_profile prof;
                p8 host_dir[128] = "/tmp/moonwater-bowl-check-install-host-";
                p8 pwn1[192], pwn2[192], sym1[256], sym2[256], good[256];
                positive hlen = string_length((string_address)host_dir);
                const struct bowl_distro address_to arch = bowl_find_distro("arch");
                p8 had_arch = arch && bowl_has(arch->root, arch->marker);

                memory_fill(address_of prof, 0, sizeof prof);
                prof.components = comps;

                hlen += positive_into_string((string_address)host_dir + hlen, self);
                host_dir[hlen] = 0;
                bowl_forget_path((string_address)host_dir);
                system_call_3(syscall(mkdirat), (positive)AT_FDCWD, (positive)host_dir, 0700);

                if (arch)
                {
                        if (!had_arch)
                        {
                                unpack_directory(arch->root, "/usr/bin");
                                unpack_write(arch->root, "/usr/bin/pacman", "#!/bin/sh\n", 0755);
                        }
                        unpack_directory(arch->root, "/sec_real_dir");
                        bowl_root_path(sym1, sizeof sym1, arch->root, "/sec_sym_escape");
                        bowl_root_path(sym2, sizeof sym2, arch->root, "/sec_sym_dir");
                        system_remove_at(AT_FDCWD, (string_address)sym1, 0);
                        system_remove_at(AT_FDCWD, (string_address)sym2, 0);
                        system_symbolic_link_at((string_address)host_dir, AT_FDCWD, (string_address)sym1);
                        system_symbolic_link_at((string_address)host_dir, AT_FDCWD, (string_address)sym2);

                        bowl_guest_install(address_of prof, "/sec_real_dir", "/sec_sym_escape/pwn1",
                                           (const p8 address_to)"evil", 4, 0, null, 0);
                        bowl_guest_install(address_of prof, "/sec_sym_dir", "/sec_sym_dir/pwn2",
                                           (const p8 address_to)"evil", 4, 0, null, 0);
                        bowl_guest_install(address_of prof, "/sec_real_dir", "/sec_real_dir/good.so",
                                           (const p8 address_to)"ok", 2, 0, null, 0);

                        bowl_root_path(pwn1, sizeof pwn1, (string_address)host_dir, "/pwn1");
                        bowl_root_path(pwn2, sizeof pwn2, (string_address)host_dir, "/pwn2");
                        bowl_root_path(good, sizeof good, arch->root, "/sec_real_dir/good.so");

                        check("BOWL-004: bowl_guest_install anchors to pinned dirfd and rejects symlink escape",
                              system_access_at(AT_FDCWD, (string_address)pwn1, 0) < 0 &&
                                  system_access_at(AT_FDCWD, (string_address)pwn2, 0) < 0 &&
                                  system_access_at(AT_FDCWD, (string_address)good, 0) == 0 &&
                                  unpack_mode(arch->root, "/sec_real_dir/good.so") == 0755);

                        system_remove_at(AT_FDCWD, (string_address)sym1, 0);
                        system_remove_at(AT_FDCWD, (string_address)sym2, 0);
                        bowl_root_path(sym1, sizeof sym1, arch->root, "/sec_real_dir");
                        bowl_forget_path((string_address)sym1);
                        if (!had_arch)
                                bowl_forget_path(arch->root);
                }
                bowl_forget_path((string_address)host_dir);
        }

        /* BOWL-005: bowl_graphics_stamp, bowl_profile_held, and bowl_profile_expose_dir must not follow symlinks out of root. */
        {
                p8 host_dir[128] = "/tmp/moonwater-bowl-check-escape-host-";
                p8 root[256], path[256], stamp[256];
                positive hlen = string_length((string_address)host_dir);
                static string_address pkgs[] = {"mesa", null};
                bool held[2] = {false, false};

                hlen += positive_into_string((string_address)host_dir + hlen, self);
                host_dir[hlen] = 0;
                bowl_forget_path((string_address)host_dir);
                unpack_directory((string_address)host_dir, "/mesa-24.0-1");
                unpack_write((string_address)host_dir, "/escapeprog", "#!/bin/sh\n", 0755);

                unpack_path(root, "symescape", "");
                bowl_forget_path(root);
                unpack_directory(root, "/var/lib/pacman");
                unpack_directory(root, "/usr");
                bowl_root_path(path, sizeof path, root, "/var/lib/pacman/local");
                system_symbolic_link_at((string_address)host_dir, AT_FDCWD, (string_address)path);
                bowl_root_path(path, sizeof path, root, "/usr/bin");
                system_symbolic_link_at((string_address)host_dir, AT_FDCWD, (string_address)path);

                positive stamp_len = bowl_graphics_stamp(root, stamp, sizeof stamp);
                bowl_profile_held(root, pkgs, held);
                bowl_quiet = true;
                positive exposed = bowl_profile_expose_dir(root, "/usr/bin", -1, null);
                bowl_quiet = false;

                check("BOWL-005: bowl_graphics_stamp, bowl_profile_held, and bowl_profile_expose_dir confine symlinks to root",
                      stamp_len == 0 && !held[0] && exposed == 0);

                bowl_forget_path(root);
                bowl_forget_path((string_address)host_dir);
        }
}

b32 main(void)
{
        shader_warm_rows();
        prewarm_rows();
        security_remediation_rows();
        ssh_rows();
        names();
        launchers();
        isolation();
        kernel_settings();
        isolated_root_children();
        fast_view_nodev();
        launch_failure_words();
        room();
        archive_policy();
        landing();
        distros();
        profiles();
        chrome_profile();
        build_profile();
        multilib_rows();
        record_rows();
        steam_profile();
        flatpak_profile();
        pad_rows();
        downloads();
        download_ceiling();
        secret_lines();
#if MOONWATER_STRICT >= STRICT_TIGHT
        secret_arguments();
#endif
        switches_asked();
        boot_links();
        wifi_plans();
        station_filter();
        station_statuses();
        station_confinement();
        json();
        oci();
        nix();
        executable_in_root();
        bowl_hardened_and_limits_rows();
        grow_rows();
        return test_report(null);
}
#endif /* CHECK_bowl */

