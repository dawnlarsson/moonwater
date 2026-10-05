/*
        Bowl runs a distribution package on the Moonwater kernel.

        Bowl is the mounts and the exec, not a userspace. POSIX tools are
        Moonwater applets; a package wins by running against those, not by
        replacing them.

        There are two entry shapes because package management and ordinary
        commands want different things:

          fast        bind only the loader and libc directories the guest
                      binary needs, then exec it by its path under the bowl
                      root; Moonwater /bin stays; no supervisor fork.

          isolated    pivot into the complete distribution root with private
                      PID, UTS and IPC views; use this for apt, pacman, apk,
                      dnf and nix while they fill a tree.

        `bowl setup <name>` is the new-install command: it becomes root,
        installs /bowl, lands that distribution, and puts its manager on
        PATH. Typing pacman afterwards is isolated by the program name, not
        by a flag the person has to remember.

        Neither is instruction emulation or a syscall proxy. Once setup is
        complete, the program is an ordinary native process on this kernel.
*/

#define bowl_label TERM_BOLD "[Bowl]" TERM_RESET " "

static const p8 bowl_usage_text[] = bowl_label
    "usage: bowl setup <name>\n"
    bowl_label "       bowl [--fast|--isolated] <root> [program [argument...]]\n"
    bowl_label "       bowl expose <root> <program> [name]\n"
    bowl_label "       bowl profile [<name> [install|remove]]\n";

#define BOWL_NATIVE_SHELL "/shell"
/* Where bowl roots live, said once. Everything else that needs to know --
   including the shell's own path handling -- spells it from here. A check
   build points it at a scratch directory so landing can be tested on real
   files without owning /bowls. */
#ifndef BOWL_ROOT_DIRECTORY
#define BOWL_ROOT_DIRECTORY "/bowls"
#endif
#define BOWL_ROOT_PREFIX BOWL_ROOT_DIRECTORY "/"
#define BOWL_EXPOSE_DIRECTORY BOWL_ROOT_PREFIX "bin"
#define BOWL_DEFAULT_PATH "/bin:/usr/bin:" BOWL_EXPOSE_DIRECTORY ":/"
#define BOWL_EXPOSE_PREFIX "#!/bowl @"
#define BOWL_PROGRAM "/bowl"
#define BOWL_PATH_LIMIT 4096
#define BOWL_SHEBANG_LIMIT 256
#define BOWL_ACCESS_EXECUTE 1
#define BOWL_WRAP_ARGV 96

struct bowl_mount_point
{
        string_address source;
        string_address target;
        string_address filesystem;
        positive flags;
};

/*
        Filesystems expected by a complete isolated root.

        sysfs is read-only: a guest reads what devices there are and changes
        nothing about them, and the kernel's own knobs under /sys/kernel,
        /sys/power and /sys/module are the host's.
*/
static struct bowl_mount_point bowl_isolated_mounts[] = {
    {"proc", "/proc", "proc", MS_NOSUID | MS_NOEXEC | MS_NODEV},
    {"sysfs", "/sys", "sysfs", MS_NOSUID | MS_NOEXEC | MS_NODEV | MS_RDONLY},
    {"devtmpfs", "/dev", "devtmpfs", MS_NOSUID},
    {"devpts", "/dev/pts", "devpts", MS_NOSUID | MS_NOEXEC},
    {"tmpfs", "/dev/shm", "tmpfs", MS_NOSUID | MS_NODEV},
    {"tmpfs", "/run", "tmpfs", MS_NOSUID | MS_NODEV},
    {"tmpfs", "/tmp", "tmpfs", MS_NOSUID | MS_NODEV},
    {null, null, null, 0},
};

/*
        Fast view: libraries, not commands.

        The guest binary is executed at /bowls/NAME/usr/bin/jq. ld.so still
        looks for the interpreter and DT_NEEDED names under /lib and /usr/lib,
        which an empty Moonwater host does not have, so those trees are bound
        read-only from the bowl. /bin, /usr/bin and /opt stay Moonwater's:
        overlaying them hid every native applet.

        /etc and /var stay Moonwater's, as do /home, /root, /tmp, /run, /dev,
        /proc and /sys, except the /etc trees desktop and TLS programs read:
        /etc/xdg for weston.ini, /etc/fonts for fontconfig, /etc/ssl and
        /etc/pki for the guest's own certificate store. Overlaying all of
        /etc hid Moonwater's own names.

        Each launch unshares a mount namespace before these binds, so a
        Debian glibc bowl cannot replace Arch's loader in another process.
        The host /lib is untouched.

        /usr/share is the data tree desktop programs read: Weston, GTK
        schemas, icons, mime. ncurses as root ignores $TERMINFO and reads
        only the directory it was built with, so /usr/share/terminfo is
        still listed for a bowl that has the database but no broader share
        tree. /usr/libexec is weston-desktop-shell and the rest of the
        helpers compiled next to the libraries.

        /nix is a Nix bowl's store. Everything Nix installs names its loader
        and libraries by /nix/store paths, so without it a program from
        a Nix profile runs only in the isolated view.
*/
static string_address bowl_fast_layers[] = {
    "/lib",
    "/lib64",
    "/usr/lib",
    "/usr/lib64",
    "/usr/share",
    "/usr/share/terminfo",
    "/usr/libexec",
    "/usr/local/lib",
    "/usr/local/share",
    "/etc/xdg",
    "/etc/fonts",
    "/etc/ssl",
    "/etc/pki",
    "/nix",
    null,
};

static bipolar bowl_open_directory(string_address path, bool create,
                                   bool address_to made);

static bipolar bowl_mkdir(string_address path)
{
        bipolar handle = bowl_open_directory(path, true, null);

        if (handle < 0)
                return handle;
        return system_close(handle);
}

/*
        Pin every component with O_NOFOLLOW so a name such as /tmp/x -> /etc
        cannot redirect mkdir or chmod into a host tree the session denylist
        already refused by string. Missing components are created only
        relative to the directory already held.

        create says how much of a path that is not there may be made:

          BOWL_MAKE_NONE   none of it; the walk is a lookup
          BOWL_MAKE_LEAF   the last component, and only under a parent that
                           is already there
          BOWL_MAKE_CHAIN  every component, which is for a path this file
                           chose rather than one it was handed

        made says whether the last component is one this walk created, which
        is the only directory a session may go on to change the mode of: a
        path that was already there belongs to whoever put it there.
*/
#define BOWL_MAKE_NONE 0
#define BOWL_MAKE_LEAF 1
#define BOWL_MAKE_CHAIN 2

static bipolar bowl_open_directory(string_address path, p8 create,
                                   bool address_to made)
{
        p8 name[256];
        bipolar held;
        positive flags = FILE_READ | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;
        string_address at = path;

        if (made)
                address_to made = false;
        if (!path || !string_get(path))
                return -22;

        /*  A path that is there is one system call, not three for every
            component: the kernel refuses a symlink at any step itself
            (RESOLVE_NO_SYMLINKS is the walk's O_NOFOLLOW on each, and no
            magic link either), and what it hands back is the same
            descriptor the walk ends on. A path that is not there, one the
            kernel refuses, and one with a .. or a name too long to be one
            go the long way, which makes what it makes and says what it
            said: it is the only one of the two that does either. */
        {
                bool plain = true;
                positive run = 0;
                bool dots = true;

                for (string_address walk = path;; walk++)
                {
                        if (!string_get(walk) || string_is(walk, '/'))
                        {
                                if (run >= 255 || (run == 2 && dots))
                                        plain = false;
                                run = 0;
                                dots = true;
                                if (!string_get(walk))
                                        break;
                                continue;
                        }
                        dots = dots && string_is(walk, '.');
                        run++;
                }
                if (plain)
                {
                        held = system_open_resolved(AT_FDCWD, path, flags,
                                                    SYSTEM_RESOLVE_NO_SYMLINKS |
                                                        SYSTEM_RESOLVE_NO_MAGICLINKS);
                        if (held >= 0)
                                return held;
                }
        }

        held = system_open_at(AT_FDCWD,
                              path[0] == '/' ? (string_address)"/"
                                             : (string_address)".",
                              flags);
        if (held < 0)
                return held;

        if (path[0] == '/')
                at++;

        while (string_get(at))
        {
                positive n = 0;
                bipolar next;

                at += string_span_of_set(at, "/");
                if (!string_get(at))
                        break;
                while (string_get(at) && string_not(at, '/') &&
                       n + 1 < sizeof(name))
                        name[n++] = string_get(at++);
                if (string_get(at) && string_not(at, '/'))
                {
                        system_close(held);
                        return -36;
                }
                name[n] = end;
                if (n == 1 && name[0] == '.')
                        continue;
                if (n == 2 && name[0] == '.' && name[1] == '.')
                {
                        system_close(held);
                        return -22;
                }
                string_address after = at;
                bool here = false;
                bool last;

                after += string_span_of_set(after, "/");
                last = !string_get(after);

                next = system_open_at(held, name, flags);
                if (next < 0 && next == -ERROR_NO_ENTRY &&
                    (create == BOWL_MAKE_CHAIN ||
                     (create == BOWL_MAKE_LEAF && last)))
                {
                        bipolar fresh = system_make_directory_at(held, name,
                                                                 0755);

                        here = fresh >= 0;
                        next = fresh < 0 && fresh != -ERROR_EXISTS
                                   ? fresh
                                   : system_open_at(held, name, flags);
                }
                system_close(held);
                if (next < 0)
                        return next;
                held = next;
                /* The last component to get here is the leaf. */
                if (made)
                        address_to made = here;
        }

        return held;
}

// A mount point below a directory Moonwater itself may not have.
static bipolar bowl_mkdir_parents_made(string_address path, p8 create,
                                       bool address_to made)
{
        bipolar handle = bowl_open_directory(path, create, made);

        if (handle < 0)
                return handle;
        system_close(handle);
        return 0;
}

static bipolar bowl_mkdir_parents(string_address path)
{
        return bowl_mkdir_parents_made(path, BOWL_MAKE_CHAIN, null);
}

/*
        Open a path as though root were /. Absolute symlinks therefore remain
        inside the bowl, and relative symlinks cannot climb above it. This is
        the pathname rule isolated mode gets from pivot_root, made explicit for
        setup and fast mode where the host root is still mounted.
*/
static bipolar bowl_open_in_root(string_address root, string_address path,
                                 positive flags)
{
        bipolar root_handle = bowl_open_directory(root, false, null);
        bipolar opened;

        if (root_handle < 0)
                return root_handle;

        opened = system_open_resolved(root_handle, path, flags,
                                      SYSTEM_RESOLVE_IN_ROOT |
                                          SYSTEM_RESOLVE_NO_MAGICLINKS);
        system_close(root_handle);
        return opened;
}

/*
        Whether a guest program can be run, asked the way it will be run: as
        though root were /. A Nix profile is links to absolute /nix/store
        paths, which from the host name nothing, so asking the host path
        refused every program a Nix bowl has.
*/
static bipolar bowl_executable_in_root(string_address root,
                                       string_address program)
{
        bipolar handle = bowl_open_in_root(root, program, O_PATH | O_CLOEXEC);
        bipolar failed;

        if (handle < 0)
                return handle;

        failed = system_call_4(syscall(faccessat2), (positive)handle,
                               (positive) "", BOWL_ACCESS_EXECUTE,
                               AT_EMPTY_PATH);
        system_close(handle);
        return failed;
}

static bool bowl_root_path(p8 address_to into, positive room,
                           string_address root, string_address path)
{
        positive root_length = string_length(root);
        positive path_length = string_length(path);

        if (!room || root_length >= room || path_length >= room - root_length)
                return false;

        memory_copy(into, root, root_length);
        memory_copy(into + root_length, path, path_length + 1);
        return true;
}

static bool bowl_name(string_address name, bool plus)
{
        if (!name || !*name || string_equals(name, ".") ||
            string_equals(name, ".."))
                return false;

        while (*name)
        {
                if (!byte_is_alnum(*name) && *name != '-' && *name != '_' &&
                    *name != '.' && !(plus && *name == '+'))
                        return false;
                name++;
        }

        return true;
}

/* . or .. as a component, or //, so a path cannot walk out of its root.
   The session environment asks the same question of HOME and
   XDG_RUNTIME_DIR, and both standalone harness windows compile this one
   walk rather than each keeping a copy of it. */
static bool bowl_path_steps(string_address path)
{
        if (!path || path[0] != '/')
                return true;

        for (; *path; path++)
        {
                if (*path != '/')
                        continue;
                if (path[1] == '/')
                        return true;
                if (path[1] == '.' &&
                    (!path[2] || path[2] == '/' ||
                     (path[2] == '.' && (!path[3] || path[3] == '/'))))
                        return true;
        }

        return false;
}

/* A named root has exactly the form /bowls/NAME. */
static bool bowl_named_root(string_address root)
{
        positive prefix = sizeof(BOWL_ROOT_PREFIX) - 1;
        string_address name;

        if (!root || string_compare_max(root, BOWL_ROOT_PREFIX, prefix) ||
            !root[prefix])
                return false;

        name = root + prefix;
        return !string_equals(name, "bin") && bowl_name(name, false);
}

/* /bowls/NAME/PROGRAM: the named root copied into root, and where PROGRAM
   begins, or null when it is not that shape or does not fit. */
static string_address bowl_split_root(string_address path, p8 address_to root,
                                      positive room)
{
        positive prefix = sizeof(BOWL_ROOT_PREFIX) - 1;
        string_address program;
        positive root_length;

        if (!path || string_compare_max(path, BOWL_ROOT_PREFIX, prefix))
                return null;

        program = string_first_of(path + prefix, '/');
        if (!program || !program[1])
                return null;

        root_length = (positive)(program - path);
        if (root_length >= room)
                return null;

        memory_copy(root, path, root_length);
        root[root_length] = end;
        return bowl_named_root(root) ? program : null;
}

/* Parse @/bowls/NAME/PROGRAM from a shebang invocation. */
static bool bowl_launcher(string_address encoded, p8 address_to root,
                          positive room,
                          string_address address_to program_out)
{
        string_address program;

        if (!encoded || encoded[0] != '@')
                return false;

        /* A shebang file keeps a newline; the kernel does not pass it. */
        for (string_address at = encoded; *at; at++)
                if (*at == '\n' || *at == '\r')
                {
                        *at = end;
                        break;
                }

        program = bowl_split_root(encoded + 1, root, room);
        if (!program || bowl_path_steps(program))
                return false;

        address_to program_out = program;
        return true;
}

static bool bowl_shebang_target(string_address line, p8 address_to root,
                                positive room,
                                string_address address_to program_out)
{
        if (!line)
                return false;

        string_address marker = string_first_of(line, '@');
        line = marker ? marker : line + string_length(line);

        return bowl_launcher(line, root, room, program_out);
}

static bool bowl_needs_isolated(string_address program)
{
        static string_address managers[] = {
            "pacman", "pacman-key", "pacman-conf", "makepkg", "repo-add",
            "repo-remove", "apt", "apt-get", "apt-cache", "apt-cdrom",
            "apt-config", "apt-key", "apt-mark", "aptitude", "dpkg",
            "dpkg-deb", "dpkg-query", "dpkg-reconfigure", "dpkg-divert",
            "apk", "dnf", "dnf5", "rpm", "yum", "nix", "nix-env",
            "nix-build", "nix-shell", "nix-channel", "nix-store",
            "nix-instantiate", "nix-collect-garbage"};
        p8 name[256];

        if (!program || program[0] != '/')
                return false;

        path_tail_copy(name, sizeof(name), program);
        return string_table_find(name, managers, sizeof(managers[0]),
                                 array_count(managers)) < array_count(managers);
}

static b32 bowl_usage(void)
{
        log((address_any)bowl_usage_text, sizeof(bowl_usage_text) - 1);
        log_flush();
        return 1;
}

/* Set while a pass does many things that may each fail and says so once: a
   profile exposing hundreds of programs, the session making what is missing. */
static bool bowl_quiet;

static b32 bowl_fail(string_address what, bipolar failed)
{
        if (bowl_quiet)
                return 1;
        string_format(log, bowl_label "%s: %b\n", what, failed);
        log_flush();
        return 1;
}

static b32 bowl_refuse(string_address message)
{
        string_format(log, bowl_label "%s", message);
        log_flush();
        return 1;
}

/*
        Bind a tree over a host path and remount it read-only.

        One mount with MS_RDONLY is ignored on bind; the kernel takes the
        readonly bit from a remount of the same target.
*/
static bipolar bowl_bind_ro(string_address source, string_address target)
{
        /*
                Bowl roots are unpacked directory trees, not mount trees.
                A recursive bind would also clone any mount an operator or a
                compromised guest had placed below one of those directories.
                The bind remount below makes this mount read-only, but does not
                reliably turn every cloned child mount read-only on older mount
                APIs.  Do not import child mounts in the first place.
        */
        bipolar failed = system_mount(source, target, 0, MS_BIND, 0);

        if (failed)
                return failed;

        return system_mount(0, target, 0,
                            MS_BIND | MS_REMOUNT | MS_RDONLY, 0);
}

/*
        Install one kernel-interpreted launcher.

        #!/bowl @/bowls/debian/usr/bin/jq

        Linux passes the encoded target, then the launcher path, then the
        caller's original arguments to /bowl. There is no intermediate shell,
        generated ELF file or per-command runtime. The launcher is the whole
        system-wide installation and is intentionally created O_EXCL.
*/
static b32 bowl_expose_program(string_address root, string_address program,
                               string_address name, bool exclusive)
{
        p8 installed[BOWL_PATH_LIMIT];
        p8 launcher[BOWL_PATH_LIMIT];
        p8 inferred[256];
        p8 line[BOWL_SHEBANG_LIMIT];
        positive prefix_length = sizeof(BOWL_EXPOSE_PREFIX) - 1;
        positive root_length;
        positive program_length;
        positive name_length;
        positive line_length;
        bipolar handle;
        bipolar failed;

        if (!bowl_named_root(root) || !program || program[0] != '/' ||
            !program[1])
                return bowl_refuse("expose needs /bowls/NAME and an absolute "
                                   "program path\n");

        for (string_address at = program; *at; at++)
                if (*at <= ' ')
                        return bowl_refuse("whitespace cannot be encoded in "
                                           "an exposed path\n");

        if (bowl_path_steps(program))
                return bowl_refuse("an exposed path cannot contain . or ..\n");

        if (!name || !name[0])
        {
                path_tail_copy(inferred, sizeof(inferred), program);
                name = inferred;
        }

        if (!bowl_name(name, true))
                return bowl_refuse("invalid command name\n");

        root_length = string_length(root);
        program_length = string_length(program);
        name_length = string_length(name);
        line_length = prefix_length + root_length + program_length + 1;

        if (line_length >= sizeof(line) ||
            !bowl_root_path(installed, sizeof(installed), root, program) ||
            sizeof(BOWL_EXPOSE_DIRECTORY) + name_length >= sizeof(launcher))
                return bowl_refuse("exposed path is too long\n");

        failed = bowl_executable_in_root(root, program);
        if (failed < 0)
                return bowl_fail(installed, failed);

        failed = bowl_mkdir(BOWL_ROOT_DIRECTORY);
        if (!failed)
                failed = bowl_mkdir(BOWL_EXPOSE_DIRECTORY);
        if (failed < 0)
                return bowl_fail(BOWL_EXPOSE_DIRECTORY, failed);

        path_join(launcher, sizeof(launcher), BOWL_EXPOSE_DIRECTORY, name);

        if (!exclusive && system_access_at(AT_FDCWD, launcher, 0) >= 0)
        {
                p8 existing_root[BOWL_PATH_LIMIT];
                p8 guest[BOWL_PATH_LIMIT];
                string_address existing_program = null;
                bipolar reader = system_open_at(AT_FDCWD, launcher,
                                                FILE_READ | O_CLOEXEC);
                bipolar got = reader < 0
                    ? reader
                    : system_read_retry((positive)reader, line,
                                        sizeof(line) - 1);

                if (reader >= 0)
                        system_close(reader);
                if (got > 0)
                        line[got] = end;
                else
                        line[0] = end;

                bool ours = bowl_shebang_target(line, existing_root,
                                                sizeof(existing_root),
                                                address_of existing_program);

                if (ours && string_equals(existing_root, root))
                        return 0;

                if (ours &&
                    bowl_root_path(guest, sizeof(guest), existing_root,
                                   existing_program) &&
                    system_access_at(AT_FDCWD, guest, BOWL_ACCESS_EXECUTE) >=
                        0)
                {
                        if (!bowl_quiet)
                        {
                                string_format(log,
                                              bowl_label
                                              "%s is already exposed from %s\n",
                                              name, existing_root);
                                log_flush();
                        }
                        return 1;
                }

                /* Garbage or a launcher whose guest file is gone: this root
                   may take the name. */
                system_remove_at(AT_FDCWD, launcher, 0);
        }

        memory_copy(line, BOWL_EXPOSE_PREFIX, prefix_length);
        memory_copy(line + prefix_length, root, root_length);
        memory_copy(line + prefix_length + root_length, program,
                    program_length);
        line[line_length - 1] = '\n';
        line[line_length] = end;

        handle = system_open_at_mode(
            AT_FDCWD, launcher,
            FILE_WRITE | FILE_EXCLUSIVE | O_CLOEXEC, 0755);

        if (handle < 0)
                return bowl_fail(launcher, handle);

        if (system_write_all((positive)handle, line, line_length) !=
            line_length)
        {
                system_close(handle);
                system_remove_at(AT_FDCWD, launcher, 0);
                return bowl_refuse("could not write launcher\n");
        }

        failed = system_call_2(syscall(fchmod), (positive)handle, 0755);
        system_close(handle);

        if (failed < 0)
        {
                system_remove_at(AT_FDCWD, launcher, 0);
                return bowl_fail(launcher, failed);
        }

        if (!bowl_quiet)
        {
                string_format(log, bowl_label "%s -> %s%s\n", launcher, root,
                              program);
                log_flush();
        }
        return 0;
}

static b32 bowl_expose(positive count, string_address address_to arguments)
{
        if (count < 4 || count > 5)
                return bowl_usage();

        return bowl_expose_program(arguments[2], arguments[3],
                                   count == 5 ? arguments[4] : null, true);
}

static string_address bowl_guest_bins[] = {
    "/usr/bin", "/usr/sbin", "/bin", "/root/.nix-profile/bin", null};

static bool bowl_split_guest_path(string_address path, p8 address_to root,
                                  positive root_room, p8 address_to program,
                                  positive program_room)
{
        string_address rest = bowl_split_root(path, root, root_room);

        if (!rest || string_length(rest) >= program_room)
                return false;

        memory_copy(program, rest, string_length(rest) + 1);
        return true;
}

static bool bowl_file_elf(string_address path)
{
        p8 head[4];
        bipolar got = file_read_once_at(AT_FDCWD, path, head, 4);

        return got == 4 && head[0] == 0x7f && head[1] == 'E' &&
               head[2] == 'L' && head[3] == 'F';
}

/* The next bowl under /bowls, as its root; false at the end. The named-root
   test is what passes over ., .. and bin. */
static bool bowl_next_root(file_walk address_to walk, p8 address_to root)
{
        struct linux_dirent64 address_to entry;

        while ((entry = file_walk_next(walk)))
                if (path_join(root, BOWL_PATH_LIMIT, BOWL_ROOT_DIRECTORY,
                              entry->d_name) &&
                    bowl_named_root(root))
                        return true;
        return false;
}

/*
        A bare name the PATH did not hold: look under each bowl's usual
        command directories, expose it, and hand back the launcher so the
        next lookup is an ordinary PATH hit.
*/
static bool bowl_fill_command(string_address name, p8 address_to into,
                             positive room)
{
        file_walk walk;
        p8 root[BOWL_PATH_LIMIT];
        p8 rel[256];
        p8 installed[BOWL_PATH_LIMIT];
        p8 fallback[BOWL_PATH_LIMIT];
        bool found = false;
        bool held = false;
        positive name_length;

        if (!name || string_first_of(name, '/') || !bowl_name(name, true))
                return false;

        name_length = string_length(name);
        if (!file_walk_open(address_of walk, AT_FDCWD, BOWL_ROOT_DIRECTORY))
                return false;

        while (!found && bowl_next_root(address_of walk, root))
        {
                for (positive at = 0; bowl_guest_bins[at]; at++)
                {
                        if (string_length(bowl_guest_bins[at]) + 1 +
                                name_length >=
                            sizeof(rel))
                                continue;

                        if (!path_join(rel, sizeof(rel), bowl_guest_bins[at],
                                       name) ||
                            !bowl_root_path(installed, sizeof(installed), root,
                                            rel) ||
                            bowl_executable_in_root(root, rel) < 0)
                                continue;

                        if (!bowl_expose_program(root, rel, name, false) &&
                            path_join(into, room, BOWL_EXPOSE_DIRECTORY,
                                      name))
                        {
                                found = true;
                                break;
                        }

                        /* Another root already owns the name, or the write
                           failed. Keep the guest path so wrap can still run
                           this binary, and keep looking for a root that can
                           own the launcher. */
                        if (!held && string_length(installed) < sizeof(fallback))
                        {
                                memory_copy(fallback, installed,
                                            string_length(installed) + 1);
                                held = true;
                        }
                }
        }

        file_walk_close(address_of walk);
        if (found)
                return true;
        if (!held || string_length(fallback) >= room)
                return false;
        memory_copy(into, fallback, string_length(fallback) + 1);
        return true;
}

static p8 bowl_wrap_root[BOWL_PATH_LIMIT];
static p8 bowl_wrap_program[BOWL_PATH_LIMIT];
static p8 bowl_wrap_absolute[BOWL_PATH_LIMIT];
static string_address bowl_wrap_vector[BOWL_WRAP_ARGV];

static bool bowl_guest_absolute(string_address path, string_address cwd,
                                p8 address_to into, positive room)
{
        if (!path || !path[0])
                return false;

        if (path[0] == '/')
        {
                positive length = string_length(path);

                if (length >= room)
                        return false;

                memory_copy(into, path, length + 1);
                return true;
        }

        if (!cwd || cwd[0] != '/')
                return false;

        return path_join(into, room, cwd, path) != 0;
}

static bool bowl_guest_elf_command(string_address path, string_address cwd)
{
        if (!bowl_guest_absolute(path, cwd, bowl_wrap_absolute,
                                 sizeof(bowl_wrap_absolute)))
                return false;

        return bowl_file_elf(bowl_wrap_absolute) &&
               bowl_split_guest_path(bowl_wrap_absolute, bowl_wrap_root,
                                     sizeof(bowl_wrap_root),
                                     bowl_wrap_program,
                                     sizeof(bowl_wrap_program));
}

/*
        A guest ELF is not a Moonwater program: execve of it looks for
        ld-linux under the host and answers -2. Run it through bowl so the
        fast view can bind the loader, whether the name was typed, hashed, or
        spelled as ./btop from the guest bin directory.
*/
static bool bowl_wrap_command(string_address path, string_address cwd,
                              string_address address_to address_to argv,
                              positive address_to argc)
{
        string_address address_to old;
        positive count;

        if (!argv || !argc || !bowl_guest_elf_command(path, cwd))
                return false;

        old = address_to argv;
        count = address_to argc;
        if (!old || count + 2 >= BOWL_WRAP_ARGV)
                return false;

        bowl_wrap_vector[0] = BOWL_PROGRAM;
        bowl_wrap_vector[1] = bowl_wrap_root;
        bowl_wrap_vector[2] = bowl_wrap_program;
        if (count > 1)
                memory_copy(bowl_wrap_vector + 3, old + 1,
                            (count - 1) * sizeof(*old));
        bowl_wrap_vector[count + 2] = null;
        address_to argv = bowl_wrap_vector;
        address_to argc = count + 2;
        return true;
}

static bool bowl_wrap_words(string_address cwd,
                           string_address address_to words, positive count,
                           positive room)
{
        if (!words || count < 1 || count + 2 >= room ||
            !bowl_guest_elf_command(words[0], cwd))
                return false;

        memory_copy(words + 3, words + 1, count * sizeof(*words));
        words[0] = BOWL_PROGRAM;
        words[1] = bowl_wrap_root;
        words[2] = bowl_wrap_program;
        words[count + 2] = null;
        return true;
}

static bipolar bowl_dev_link(string_address target, string_address name)
{
        bipolar failed = system_symbolic_link_at(target, AT_FDCWD, name);

        return (failed < 0 && failed != -EEXIST) ? failed : 0;
}

/*
        The host kernel's settings, which a fresh proc shows a guest exactly as
        the host sees them.

        A pid namespace gives the guest its own processes and nothing else, so
        /proc/sys is the running kernel's: procps's postinst runs
        sysctl --system inside the bowl and rewrote the host's core_pattern,
        sysrq, fs.protected_* and rp_filter with Debian's defaults. Each is
        bound over itself read-only, which is what a container runtime does
        with the same list; a write answers EROFS and sysctl says it could not
        set the key. /proc/sys is always there; the rest only when the kernel
        was built with what makes them.

        This keeps package scripts off the host. It is not a wall against a
        guest that means to get through: the bowl is root with every
        capability and could remount any of these.
*/
static string_address bowl_kernel_settings[] = {
    "/proc/sys", "/proc/sysrq-trigger", "/proc/irq", "/proc/bus", "/proc/fs",
    null};

static bipolar bowl_kernel_settings_seal(void)
{
        for (positive i = 0; bowl_kernel_settings[i]; i++)
        {
                string_address path = bowl_kernel_settings[i];
                bipolar failed = system_mount(path, path, 0, MS_BIND, 0);

                if (failed == -ENOENT && i)
                        continue;

                if (!failed)
                        failed = system_mount(0, path, 0,
                                              MS_BIND | MS_REMOUNT | MS_RDONLY |
                                                  MS_NOSUID | MS_NOEXEC | MS_NODEV,
                                              0);

                if (failed)
                {
                        bowl_fail(path, failed);
                        return failed;
                }
        }

        return 0;
}

static bipolar bowl_isolated_populate(void)
{
        bipolar failed = 0;

        for (positive i = 0; bowl_isolated_mounts[i].target; i++)
        {
                struct bowl_mount_point address_to point =
                    bowl_isolated_mounts + i;

                failed = bowl_mkdir(point->target);
                if (!failed)
                        failed = system_mount(point->source, point->target,
                                              point->filesystem, point->flags, 0);

                if (failed)
                {
                        bowl_fail(point->target, failed);
                        return failed;
                }
        }

        failed = bowl_kernel_settings_seal();
        if (failed)
                return failed;

        /*
                bash process substitution opens /dev/fd/N. devtmpfs does not
                create those names; pacman-key uses them while filling the
                keyring.
        */
        failed = bowl_dev_link("/proc/self/fd", "/dev/fd");
        if (!failed)
                failed = bowl_dev_link("/proc/self/fd/0", "/dev/stdin");
        if (!failed)
                failed = bowl_dev_link("/proc/self/fd/1", "/dev/stdout");
        if (!failed)
                failed = bowl_dev_link("/proc/self/fd/2", "/dev/stderr");
        if (failed)
                bowl_fail("/dev/fd", failed);

        return failed;
}

/*
        Replace the root for the isolated profile.

        The bind makes the new root a mount point. pivot_root with the same
        path twice stacks the old root there, where it can be detached without
        requiring a writable put_old directory inside the distribution.

        It is not recursive, for bowl_bind_ro's reason: a bowl root is an
        unpacked tree, and a recursive bind brought along whatever an
        operator, or a guest the last time, had mounted below it -- a host
        directory bound in for a copy was then the guest's to write.
*/
static bipolar bowl_isolated_enter(string_address root)
{
        bipolar failed;

        failed = system_mount(root, root, 0, MS_BIND, 0);
        if (failed)
                return failed;

        failed = system_change_directory(root);
        if (failed)
                return failed;

        failed = system_call_2(syscall(pivot_root), (positive)".", (positive)".");
        if (failed)
                return failed;

        failed = system_call_2(syscall(umount2), (positive)".", MNT_DETACH);
        if (failed)
                return failed;

        failed = system_change_directory("/");
        if (failed)
                return failed;

        failed = bowl_isolated_populate();
        if (failed)
                return failed;

        /* Populate puts a fresh tmpfs on /run, so the host copy installed
           before pivot is gone. Write the compiled database into this /run
           or $TERMINFO points at an empty directory and ncurses refuses to
           open the terminal. */
        terminal_terminfo_install();
        return 0;
}

/* Bind only the loader search paths a guest binary still spells in ELF. */
static bipolar bowl_fast_enter(string_address root)
{
        p8 source[BOWL_PATH_LIMIT];
        bipolar failed;

        for (positive i = 0; bowl_fast_layers[i]; i++)
        {
                string_address layer = bowl_fast_layers[i];

                if (!bowl_root_path(source, sizeof(source), root, layer))
                        return -ENAMETOOLONG;

                if (system_access_at(AT_FDCWD, source, 0) < 0)
                        continue;

                failed = bowl_mkdir_parents(layer);
                if (failed < 0)
                        return failed;

                failed = bowl_bind_ro(source, layer);
                if (failed)
                        return failed;
        }

        return 0;
}

static b32 bowl_launch_failed(bipolar native_shell, string_address what,
                              bipolar failed)
{
        if (native_shell >= 0)
                system_close(native_shell);
        return bowl_fail(what, failed);
}

static fn bowl_session_prepare_from(string_address address_to environment);
static fn bowl_session_prepare_at(string_address home, string_address runtime,
                                  bool user_dirs);
static string_address address_to bowl_environment(
    string_address address_to inherited);

static DEAD_END fn bowl_inside(string_address root,
                               string_address program,
                               string_address address_to arguments,
                               string_address address_to environment,
                               bipolar native_shell, bool isolated)
{
        bipolar program_handle = -1;
        bipolar failed;

        failed = system_mount(0, "/", 0, MS_REC | MS_PRIVATE, 0);
        if (!failed)
                failed = isolated ? bowl_isolated_enter(root)
                                  : bowl_fast_enter(root);

        if (failed)
        {
                bowl_fail(root, failed);
                exit(1);
        }

        /* Isolated /run is a fresh tmpfs; fast keeps the host's. Either way
           the guest now sees the directories the session variables name. */
        bowl_session_prepare_from(environment);

        /*
                Fast mode has not pivoted, so executing ROOT + /program by
                pathname lets an absolute symlink inside the bowl jump back to
                the host root. Pin the program with the same in-root resolver
                used by setup, then execute that object. O_PATH deliberately
                stays open across exec: Linux needs the descriptor available
                when an execveat target is a shebang script.
        */
        if (!isolated && native_shell < 0)
        {
                if (!program || program[0] != '/')
                {
                        bowl_fail(program ? program : root, -ERROR_INVALID);
                        exit(1);
                }

                program_handle = bowl_open_in_root(root, program, O_PATH);
                if (program_handle < 0)
                {
                        bowl_fail(program, program_handle);
                        exit(1);
                }
        }

        failed = native_shell >= 0
            ? system_call_5(syscall(execveat), (positive)native_shell,
                             (positive)"", (positive)arguments,
                             (positive)environment, AT_EMPTY_PATH)
            : program_handle >= 0
                ? system_call_5(syscall(execveat), (positive)program_handle,
                                (positive)"", (positive)arguments,
                                (positive)environment, AT_EMPTY_PATH)
                : system_execute(program, arguments, environment);
        if (program_handle >= 0)
                system_close(program_handle);
        bowl_fail(program, failed);
        exit(127);
}

static b32 bowl_env_named(string_address entry, string_address name)
{
        positive length = string_length(name);

        return entry && !string_compare_max(entry, name, length) &&
               entry[length] == '=';
}

#define bowl_env_value(environment, name) \
        ((environment) ? string_get_environment((environment), (name)) : null)

/*
        The session a guest program expects.

        Weston, GTK, Qt, PipeWire and almost every other desktop program
        refuse to start without XDG_RUNTIME_DIR, and they want it to be a
        0700 directory owned by this user. Isolated populate puts a fresh
        tmpfs on /run, so the host copy is gone and this runs after the
        guest can see the path. An inherited environment that already names
        usable values is left alone; one that names none — bowl from a
        kernel console, bind init, or a script that cleared the block —
        gets the set, or Weston fails with "XDG_RUNTIME_DIR not set".

        getenv keeps the first assignment. An empty or relative
        XDG_RUNTIME_DIR= still counts as set, so appending a real one would
        leave Weston reading the blank. Those entries are dropped and the
        default is written instead. chmod 0700 is only for the runtime
        directory: /tmp as XDG_RUNTIME_DIR is a common wrong value, and
        making /tmp 0700 takes it away from everyone else.
*/
#define BOWL_RUNTIME_DIR "/run/user/"
#define BOWL_SESSION_HOME "/root"
#define BOWL_ENV_ROOM 512
#define BOWL_ENV_DEFAULTS 10

static p8 bowl_runtime_path[sizeof(BOWL_RUNTIME_DIR) + 20]
    __attribute__((section(".bss.hot")));
static p8 bowl_runtime_assignment[sizeof("XDG_RUNTIME_DIR=") +
                                 sizeof(bowl_runtime_path)]
    __attribute__((section(".bss.hot")));
static p8 bowl_user_assignment[sizeof("USER=") + 20];
static p8 bowl_logname_assignment[sizeof("LOGNAME=") + 20];

static b32 bowl_path_same(string_address path, string_address want)
{
        positive i = 0;

        if (!path || !want)
                return false;

        while (want[i] && path[i] == want[i])
                i++;

        if (want[i])
                return false;

        return !path[i] || (path[i] == '/' && !path[i + 1]);
}

static b32 bowl_session_host_path(string_address path)
{
        /* Read by every shell start (session preparation), so with the other
           tables a start reads (spark.ld). */
        static string_address const trees[] __attribute__((section(".rodata.hot"))) = {
            "/etc", "/usr", "/bin", "/sbin", "/boot", "/lib", "/lib64",
            "/proc", "/sys", null};
        positive i;

        if (!path)
                return false;

        if (bowl_path_same(path, "/"))
                return true;

        for (i = 0; trees[i]; i++)
        {
                positive n = string_length(trees[i]);

                if (string_compare_max(path, trees[i], n))
                        continue;
                if (!path[n] || path[n] == '/')
                        return true;
        }

        return false;
}

static b32 bowl_runtime_shared(string_address path)
{
        return bowl_path_same(path, "/tmp") || bowl_path_same(path, "/var/tmp") ||
               bowl_path_same(path, "/dev/shm") || bowl_path_same(path, "/run") ||
               bowl_path_same(path, "/dev") || bowl_path_same(path, "/");
}

/*
        The variables a session cannot do without, in the order the defaults
        are appended. A path one must be an absolute path that is not a host
        tree; the rest only have to be non-empty. also is the row that a
        name counts for when it is present: LC_ALL keeps LANG's default away.
*/
enum { BOWL_VAR_TERM, BOWL_VAR_TERMINFO, BOWL_VAR_HOME, BOWL_VAR_PATH,
       BOWL_VAR_LANG, BOWL_VAR_USER, BOWL_VAR_LOGNAME, BOWL_VAR_RUNTIME,
       BOWL_VAR_SHELL, BOWL_VAR_TMPDIR, BOWL_VAR_LC_ALL, BOWL_VARS };

static const struct
{
        string_address name;
        string_address fallback;
        bool path;
        p8 counts_as;
} bowl_variables[BOWL_VARS] = {
    [BOWL_VAR_TERM] = {"TERM", "TERM=" TERM_NAME, false, BOWL_VAR_TERM},
    [BOWL_VAR_TERMINFO] = {"TERMINFO", "TERMINFO=" TERM_INFO_DIRECTORY, false, BOWL_VAR_TERMINFO},
    [BOWL_VAR_HOME] = {"HOME", "HOME=" BOWL_SESSION_HOME, true, BOWL_VAR_HOME},
    [BOWL_VAR_PATH] = {"PATH", "PATH=" BOWL_DEFAULT_PATH, false, BOWL_VAR_PATH},
    [BOWL_VAR_LANG] = {"LANG", "LANG=C.UTF-8", false, BOWL_VAR_LANG},
    [BOWL_VAR_USER] = {"USER", (string_address)bowl_user_assignment, false, BOWL_VAR_USER},
    [BOWL_VAR_LOGNAME] = {"LOGNAME", (string_address)bowl_logname_assignment, false, BOWL_VAR_LOGNAME},
    [BOWL_VAR_RUNTIME] = {"XDG_RUNTIME_DIR", (string_address)bowl_runtime_assignment, true, BOWL_VAR_RUNTIME},
    [BOWL_VAR_SHELL] = {"SHELL", "SHELL=/bin/sh", false, BOWL_VAR_SHELL},
    [BOWL_VAR_TMPDIR] = {"TMPDIR", "TMPDIR=/tmp", true, BOWL_VAR_TMPDIR},
    [BOWL_VAR_LC_ALL] = {"LC_ALL", null, false, BOWL_VAR_LANG},
};

// Which row an environment entry is, or BOWL_VARS.
static positive bowl_variable_of(string_address entry)
{
        for (positive at = 0; at < BOWL_VARS; at++)
                if (bowl_env_named(entry, bowl_variables[at].name))
                        return at;
        return BOWL_VARS;
}

static b32 bowl_session_unusable(string_address entry)
{
        positive which = entry ? bowl_variable_of(entry) : BOWL_VARS;
        string_address value;

        if (which == BOWL_VARS)
                return false;
        value = entry + string_length(bowl_variables[which].name) + 1;
        return bowl_variables[which].path
                   ? bowl_path_steps(value) || bowl_session_host_path(value)
                   : !value[0];
}

static b32 bowl_session_default_missing(string_address name,
                                        string_address value)
{
        if (!name)
                return true;

        if (string_equals(name, "IFS") || string_equals(name, "OPTIND"))
                return !value;

        if (!value || !value[0])
                return true;

        if (string_equals(name, "HOME") ||
            string_equals(name, "XDG_RUNTIME_DIR") ||
            string_equals(name, "TMPDIR"))
                return bowl_path_steps(value) ||
                       bowl_session_host_path(value);

        return false;
}

static fn bowl_session_assign(p8 address_to into, positive room,
                              string_address name, string_address value)
{
        positive n = string_length(name);
        positive v = string_length(value);

        if (!room || n + v + 2 > room)
        {
                if (room)
                        into[0] = end;
                return;
        }

        memory_copy(into, name, n);
        into[n] = '=';
        memory_copy(into + n + 1, value, v + 1);
}

/*  What the last fill was made from is still true until the process has
    changed who it is. A shell start asks four times (the three assignments
    and the preparation) and the answer cannot have changed between them, so
    those ask bowl_session_fill_started, which fills once; everything that
    can be reached after a user namespace or a setuid asks bowl_session_fill
    and gets the uid as it is. */
static bool bowl_fill_valid __attribute__((section(".bss.hot")));

static fn bowl_session_fill(void)
{
        /* Weston stats getuid, not geteuid, against the directory owner. */
        positive uid = (positive)system_call(syscall(getuid));

        bowl_fill_valid = true;
        p8 digits[24];
        string_address user;

        memory_copy(bowl_runtime_path, BOWL_RUNTIME_DIR,
                    sizeof(BOWL_RUNTIME_DIR) - 1);
        bowl_runtime_path[sizeof(BOWL_RUNTIME_DIR) - 1 +
                          positive_into_string(bowl_runtime_path +
                                                   sizeof(BOWL_RUNTIME_DIR) - 1,
                                               uid)] = end;
        bowl_session_assign(bowl_runtime_assignment,
                            sizeof(bowl_runtime_assignment), "XDG_RUNTIME_DIR",
                            bowl_runtime_path);

        if (!uid)
                user = (string_address) "root";
        else
        {
                digits[positive_into_string(digits, uid)] = end;
                user = digits;
        }

        bowl_session_assign(bowl_user_assignment, sizeof(bowl_user_assignment),
                            "USER", user);
        bowl_session_assign(bowl_logname_assignment,
                            sizeof(bowl_logname_assignment), "LOGNAME", user);
}

static fn bowl_session_fill_started(void)
{
        if (!bowl_fill_valid)
                bowl_session_fill();
}

static string_address bowl_session_runtime_assignment(void)
{
        bowl_session_fill_started();
        return bowl_runtime_assignment;
}

static string_address bowl_session_user_assignment(void)
{
        bowl_session_fill_started();
        return bowl_user_assignment;
}

static string_address bowl_session_logname_assignment(void)
{
        bowl_session_fill_started();
        return bowl_logname_assignment;
}

static fn bowl_chmod_directory(string_address path, positive mode)
{
        bipolar handle = bowl_open_directory(path, BOWL_MAKE_NONE, null);

        if (handle < 0)
                return;

        system_call_2(syscall(fchmod), (positive)handle, mode);
        system_close(handle);
}

static fn bowl_session_home_dirs(string_address home)
{
        static string_address names[] = {
            "/.config", "/.cache", "/.local", "/.local/share", "/.local/state",
            null};
        p8 path[BOWL_PATH_LIMIT];
        positive i;

        if (!home || home[0] != '/')
                return;

        /*  The home itself has to be there: these are the directories a
            desktop keeps inside somebody's home, not a reason to invent
            the home. The list is in order, so .local stands before
            .local/share asks for it. */
        for (i = 0; names[i]; i++)
                if (bowl_root_path(path, sizeof(path), home, names[i]))
                        bowl_mkdir_parents_made(path, BOWL_MAKE_LEAF, null);
}

static b32 bowl_write_bytes(string_address path, string_address text,
                            positive length);

/*
        Who root is, and which machine this is. Moonwater's /etc has neither
        (nothing writes a passwd, a group or a machine id), and the fast view
        keeps the host's /etc, so dbus-daemon stops at "Could not get password
        database information for UID of current process" and a desktop never
        starts. A missing file is made once, atomically, and never replaced,
        and a root that cannot write /etc is left as it was. The machine id is
        written last, so one access() says all three are there: a shell start
        runs this too.
*/
static fn bowl_session_identity(void)
{
        static const struct { string_address path; string_address text; } files[] = {
            {"/etc/passwd", "root:x:0:0:root:/root:/bin/sh\n"},
            {"/etc/group", "root:x:0:\nnogroup:x:65534:\n"},
        };
        static const p8 digits[] = "0123456789abcdef";
        p8 raw[16];
        p8 id[33];

        if (system_access_at(AT_FDCWD, "/etc/machine-id", 0) >= 0)
                return;

        bowl_quiet = true;
        for (positive at = 0; at < array_count(files); at++)
                if (system_access_at(AT_FDCWD, files[at].path, 0) < 0)
                        bowl_write_bytes(files[at].path, files[at].text,
                                         string_length(files[at].text));

        if (!system_random_fill(raw, sizeof(raw), 0))
        {
                for (positive at = 0; at < sizeof(raw); at++)
                {
                        id[at * 2] = digits[raw[at] >> 4];
                        id[at * 2 + 1] = digits[raw[at] & 15];
                }
                id[32] = '\n';
                bowl_write_bytes("/etc/machine-id", id, sizeof(id));
        }
        bowl_quiet = false;
}

static fn bowl_session_prepare_at(string_address home, string_address runtime,
                                  bool user_dirs)
{
        bool made = false;
        bool sockets = false;

        /* A shell start (no home directories asked for) is the one that may
           ask for what its own start already filled. */
        if (user_dirs)
                bowl_session_fill();
        else
                bowl_session_fill_started();

        if (!runtime || bowl_path_steps(runtime) ||
            bowl_session_host_path(runtime))
                runtime = bowl_runtime_path;

        /*  0700 is Weston's requirement of the runtime directory it is
            handed, and this makes the one it creates meet it. A directory
            that was already there is not this session's to change: the name
            comes from XDG_RUNTIME_DIR, so chmodding an existing path would
            let any inherited environment take a host tree the string
            denylist does not happen to name -- /home, /var, /opt -- down to
            0700, from every shell start, not only from bowl.

            How much of it may be made apart from that. /run/user/<uid> is
            this file's own name and /run/user is ours to make, so that one
            is made in full. A name the environment chose gets its last
            directory and no more: XDG_RUNTIME_DIR=/tmp/a/b/c quietly made
            three directories at every shell start, and a path nobody has
            prepared is a typo far more often than it is a plan. Tighter
            than safe makes none of it and waits for the directory to be
            there. */
        p8 make = runtime == bowl_runtime_path ? BOWL_MAKE_CHAIN
                : MOONWATER_STRICT >= STRICT_TIGHT ? BOWL_MAKE_NONE
                                                   : BOWL_MAKE_LEAF;

        /*  A shell start on a machine that is already set up asks one
            question of each of these and does nothing else: the answer to
            "is it there" is a faccessat, where the walk that would make it
            costs an open and a close of every component. A directory that
            is there is not this session's to make or change (see made), so
            the walk's only work in that case is the open that finds it
            there, and a symlink in the way is a refusal there and "there"
            here, which come to the same nothing. Anything that is not there
            takes the walk, which is the only one that creates. /tmp is the
            one that is changed when it is there: its mode is put back to
            1777 at every start, and one statx says it already is, so the
            open, the fchmod and the close are for a /tmp that is not. */
        if (system_access_at(AT_FDCWD, runtime, 0) < 0)
        {
                bowl_mkdir_parents_made(runtime, make, address_of made);
                if (made && !bowl_runtime_shared(runtime))
                        bowl_chmod_directory(runtime, 0700);
        }
        {
                p8 held[256];
                bool sticky = false;

                if (system_stat_at(AT_FDCWD, "/tmp", AT_SYMLINK_NOFOLLOW,
                                   0x7ff, held) >= 0)
                {
                        unsigned mode = (unsigned)(held[28] | held[29] << 8);

                        sticky = (mode & 0170000) == 0040000 &&
                                 (mode & 07777) == 01777;
                }
                if (!sticky)
                {
                        bowl_mkdir("/tmp");
                        bowl_chmod_directory("/tmp", 01777);
                }
        }
        /* Where Xwayland, and every X client of any bowl, puts its socket:
           kwin_wayland_wrapper stops at "/tmp/.X11-unix does not exist". */
        if (system_access_at(AT_FDCWD, "/tmp/.X11-unix", 0) < 0)
        {
                bowl_mkdir_parents_made("/tmp/.X11-unix", BOWL_MAKE_LEAF,
                                        address_of sockets);
                if (sockets)
                        bowl_chmod_directory("/tmp/.X11-unix", 01777);
        }
        /*  The rest is the machine's and not a session's: /dev/shm, /run/lock
            and /var, the two links and the three identity files are there or
            not for every shell after the first, until the machine is
            started again. The first to finish them says so, in a file in the
            runtime directory this file names for itself (/run/user/<uid>,
            which is on /run and so is new at every boot, and in a bowl's
            fresh /run, new at every entry), and the shells after it ask for
            that one file where they asked nine questions. A runtime
            directory the environment chose does not carry the mark, since
            it may be on a disk that outlives the boot and say it is done
            for a machine that is not; /tmp and its socket directory, which
            anyone may take away, are asked at every start above. */
        {
                p8 stamp[BOWL_PATH_LIMIT];
                positive length = string_length(runtime);
                bool marked = false;
                bool markable = !string_compare_max(runtime, (string_address)bowl_runtime_path,
                                                    sizeof(bowl_runtime_path)) &&
                                length + sizeof("/.session") < sizeof(stamp);

                if (markable)
                {
                        memory_copy(stamp, runtime, length);
                        memory_copy(stamp + length, "/.session",
                                    sizeof("/.session"));
                        marked = system_access_at(AT_FDCWD, stamp, 0) >= 0;
                }
                if (!marked)
                {
                        if (system_access_at(AT_FDCWD, "/dev/shm", 0) < 0)
                                bowl_mkdir("/dev/shm");
                        if (system_access_at(AT_FDCWD, "/run/lock", 0) < 0)
                                bowl_mkdir("/run/lock");
                        if (system_access_at(AT_FDCWD, "/var", 0) < 0)
                                bowl_mkdir("/var");
                        bowl_dev_link("/run", "/var/run");
                        bowl_dev_link("/run/lock", "/var/lock");
                        bowl_session_identity();
                        if (markable)
                        {
                                bowl_quiet = true;
                                bowl_write_bytes(stamp, "1\n", 2);
                                bowl_quiet = false;
                        }
                }
        }
        if (!user_dirs)
                return;
        if (!home || bowl_path_steps(home) || bowl_session_host_path(home))
                home = (string_address)BOWL_SESSION_HOME;
        bowl_session_home_dirs(home);
}

static fn bowl_session_prepare(string_address home, string_address runtime)
{
        bowl_session_prepare_at(home, runtime, true);
}

static fn bowl_session_prepare_from(string_address address_to environment)
{
        bowl_session_prepare(bowl_env_value(environment, "HOME"),
                             bowl_env_value(environment, "XDG_RUNTIME_DIR"));
}

static string_address address_to bowl_environment(
    string_address address_to inherited)
{
        static string_address mixed[BOWL_ENV_ROOM];
        bool have[BOWL_VARS] = {false};
        positive n = 0;
        b32 skipped = false;

        bowl_session_fill();

        for (positive at = 0; inherited && inherited[at]; at++)
        {
                positive which;

                if (bowl_session_unusable(inherited[at]))
                {
                        skipped = true;
                        continue;
                }

                which = bowl_variable_of(inherited[at]);
                if (which < BOWL_VARS)
                        have[bowl_variables[which].counts_as] = true;

                /*  Out of room. Handing back the block as it
                    arrived is only safe while nothing has been
                    dropped from it: once an entry has been refused,
                    returning the original puts that entry back into
                    the guest, which is the whole of what the refusal
                    was for. What fit is then what is passed. */
                if (n + 1 >= BOWL_ENV_ROOM)
                {
                        if (!skipped)
                                return inherited;
                        mixed[n] = null;
                        return mixed;
                }

                mixed[n++] = inherited[at];
        }

        if (inherited)
        {
                positive missing = 0;

                for (positive at = 0; at < BOWL_VAR_LC_ALL; at++)
                        missing += !have[at];
                if (!skipped && !missing)
                        return inherited;
                if (n + BOWL_ENV_DEFAULTS >= BOWL_ENV_ROOM)
                {
                        if (!skipped)
                                return inherited;
                        mixed[n] = null;
                        return mixed;
                }
        }

        for (positive at = 0; at < BOWL_VAR_LC_ALL; at++)
                if (!have[at])
                        mixed[n++] = (string_address)bowl_variables[at].fallback;
        mixed[n] = null;
        return mixed;
}

/*
        Room on the filesystem that holds a bowl.

        A live session keeps /bowls on its root, a tmpfs cut to half of memory,
        and a bowl that fills it fails part way through a download, an unpack
        or a package manager's run with a message that names a file and not the
        reason. What is left is said, and what it is kept in: a tmpfs is memory
        as well as a size, and runs out of whichever is smaller.
*/
#define BOWL_TMPFS_MAGIC 0x01021994
#define BOWL_RAMFS_MAGIC 0x858458f6
#define BOWL_MEBIBYTE ((p64)1024 * 1024)

typedef struct
{
        p64 free;
        p64 total;
        p64 memory;
        bool in_memory;
} bowl_room;

// One figure from /proc/meminfo in bytes, or none when it is not there.
static p64 bowl_meminfo_bytes(string_address text, string_address name)
{
        positive length = string_length(name);

        for (positive at = 0; text[at]; at++)
        {
                p64 kilobytes = 0;

                if ((at && text[at - 1] != '\n') ||
                    string_compare_max(text + at, name, length))
                        continue;

                for (at += length; text[at] == ' '; at++)
                        ;
                for (; text[at] >= '0' && text[at] <= '9'; at++)
                        kilobytes = kilobytes * 10 + (p64)(text[at] - '0');

                return kilobytes * 1024;
        }

        return 0;
}

/*
        Asked through a descriptor rather than a path. An isolated guest's
        pivot_root moves the root of every process in its mount namespace, and
        bowl forked it inside that namespace, so after the pivot /bowls/NAME
        named nothing from here and the room could not be asked at all.
*/
#define BOWL_ROOM_OPEN (O_PATH | O_DIRECTORY | O_CLOEXEC)

static bool bowl_room_of(bipolar handle, bowl_room address_to room)
{
        file_mount_facts facts;
        p8 text[4096];

        memory_fill(room, 0, sizeof(*room));
        if (system_call_2(syscall(fstatfs), (positive)handle,
                          (positive)address_of facts) < 0)
                return false;

        p64 unit = (p64)(facts.fragment_size ? facts.fragment_size
                                             : facts.block_size);

        room->total = facts.blocks * unit;
        room->free = facts.blocks_available * unit;
        room->memory = (p64)-1;
        room->in_memory = facts.type == BOWL_TMPFS_MAGIC ||
                          facts.type == BOWL_RAMFS_MAGIC;

        // Memory can be given back by swapping, so swap counts as room too.
        if (room->in_memory &&
            file_slurp_once_at(AT_FDCWD, (string_address) "/proc/meminfo",
                               text, sizeof(text)) > 0)
                room->memory = bowl_meminfo_bytes(text, "MemAvailable:") +
                               bowl_meminfo_bytes(text, "SwapFree:");

        // ramfs has no size of its own: memory is all the room it has.
        if (room->in_memory && !facts.blocks)
                room->free = room->total = room->memory;

        return true;
}

static bool bowl_room_at(string_address path, bowl_room address_to room)
{
        bipolar handle = system_open_at(AT_FDCWD, path, BOWL_ROOM_OPEN);
        bool known;

        if (handle < 0)
                return false;

        known = bowl_room_of(handle, room);
        system_close(handle);
        return known;
}

static fn bowl_room_hint(bowl_room address_to room)
{
        if (room->in_memory)
                string_format(log, bowl_label "/bowls is kept in memory, as a live "
                                              "session keeps it; moonwater setup install "
                                              "DISK puts bowls on the disk's data "
                                              "partition\n");
}

/*
        After a step failed: when what is left is a sixteenth of the filesystem
        or less, capped at 256 MiB, space is the likely reason, and how much is
        said. The numbers are the filesystem's own; nothing is guessed but
        whether they are worth a line.
*/
static p64 bowl_room_low(bowl_room address_to room)
{
        return room->total / 16 < 256 * BOWL_MEBIBYTE ? room->total / 16
                                                      : 256 * BOWL_MEBIBYTE;
}

static fn bowl_room_say_low_of(bipolar handle, string_address path)
{
        bowl_room room;

        if (!bowl_room_of(handle, address_of room) ||
            room.free > bowl_room_low(address_of room))
                return;

        string_format(log, bowl_label "%s has %p MiB free of %p MiB\n", path,
                      (positive)(room.free / BOWL_MEBIBYTE),
                      (positive)(room.total / BOWL_MEBIBYTE));
        bowl_room_hint(address_of room);
        log_flush();
}

static fn bowl_room_say_low(string_address path)
{
        bipolar handle = system_open_at(AT_FDCWD, path, BOWL_ROOM_OPEN);

        if (handle < 0)
                return;

        bowl_room_say_low_of(handle, path);
        system_close(handle);
}

/*
        How low the room got while a guest ran.

        A package manager that fails for want of room has tidied up by the time
        it returns: on a 1 GB live root dpkg stopped at "No space left on
        device" and apt exited with 218 MiB free again, so what is free
        afterwards says nothing. The filesystem is looked at while the guest
        runs instead -- a statfs ten times a second, from a poll on the child's
        pidfd so that a quick command is not held up by it. The moment a write
        fails is too short to be seen, but the filling that leads to it takes
        seconds, so the lowest it saw is what is kept. The root is the
        descriptor opened before the guest pivoted.
*/
static p64 bowl_wait_watching(bipolar child, bipolar root)
{
        p64 lowest = (p64)-1;
        bipolar watch = system_call_2(syscall(pidfd_open), (positive)child, 0);

        while (watch >= 0)
        {
                file_mount_facts facts;
                system_poll_descriptor wanted = {(b32)watch, SYSTEM_POLL_READ, 0};
                timespec tenth = {0, 100000000};
                bipolar ready;

                if (system_call_2(syscall(fstatfs), (positive)root,
                                  (positive)address_of facts) >= 0 &&
                    facts.blocks)
                {
                        p64 unit = (p64)(facts.fragment_size ? facts.fragment_size
                                                             : facts.block_size);

                        if (facts.blocks_available * unit < lowest)
                                lowest = facts.blocks_available * unit;
                }

                ready = system_poll_wait(address_of wanted, 1, address_of tenth,
                                         null);
                if (ready != 0 && ready != -EINTR)
                        break;
        }

        if (watch >= 0)
                system_close(watch);
        return lowest;
}

/*
        After a guest failed: how low the room got while it ran, when that was
        low, or how low it is now. Both are the filesystem's own numbers, and a
        run that never came near filling it says nothing.
*/
static fn bowl_room_after(bipolar handle, string_address root, p64 lowest)
{
        bowl_room room;

        if (!bowl_room_of(handle, address_of room))
                return;

        p64 low = bowl_room_low(address_of room);

        if (lowest > low || lowest >= room.free)
        {
                bowl_room_say_low_of(handle, root);
                return;
        }

        string_format(log, bowl_label "%s was down to %p MiB free of %p MiB while "
                                      "this ran, and has %p MiB now\n", root,
                      (positive)(lowest / BOWL_MEBIBYTE),
                      (positive)(room.total / BOWL_MEBIBYTE),
                      (positive)(room.free / BOWL_MEBIBYTE));
        bowl_room_hint(address_of room);
        log_flush();
}

static b32 bowl_launch(string_address root, string_address program,
                       string_address address_to arguments,
                       bool isolated)
{
        string_address native_arguments[] = {BOWL_NATIVE_SHELL, null};
        string_address address_to environment =
            bowl_environment(file_environment_all());
        bipolar native_shell = -1;
        bipolar failed;
        bipolar child;
        positive status = 0;

        if (!root || root[0] != '/')
                return bowl_usage();

        terminal_terminfo_install();

        if (!program)
        {
                native_shell = system_open_at(AT_FDCWD, BOWL_NATIVE_SHELL,
                                              FILE_READ | O_CLOEXEC);
                if (native_shell < 0)
                        return bowl_launch_failed(native_shell,
                                                  BOWL_NATIVE_SHELL,
                                                  native_shell);

                program = BOWL_NATIVE_SHELL;
                arguments = native_arguments;
        }

        failed = system_call_1(syscall(unshare), CLONE_NEWNS |
            (isolated ? CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWPID : 0));
        if (failed)
                return bowl_launch_failed(native_shell,
                    isolated ? "cannot make isolated views"
                             : "cannot make a mount view", failed);

        /* Success never returns: this process becomes the command. */
        if (!isolated)
                bowl_inside(root, program, arguments, environment,
                            native_shell, false);

        // Opened before the guest pivots this namespace's root away from it.
        bipolar room = system_open_at(AT_FDCWD, root, BOWL_ROOM_OPEN);

        /* CLONE_NEWPID places the next child, not this caller, in the view. */
        child = system_fork();
        if (child < 0)
        {
                if (room >= 0)
                        system_close(room);
                return bowl_launch_failed(native_shell, "cannot start", child);
        }

        if (child == 0)
                bowl_inside(root, program, arguments, environment,
                            native_shell, true);

        if (native_shell >= 0)
                system_close(native_shell);

        p64 lowest = room >= 0 ? bowl_wait_watching(child, room) : (p64)-1;

        failed = system_wait4_retry(child, address_of status, 0, null);

        // apt says a file could not be written; this says the root was full.
        b32 code = failed < 0 ? 1 : wait_status_code(status);

        if (failed >= 0 && code && room >= 0)
                bowl_room_after(room, root, lowest);
        if (room >= 0)
                system_close(room);
        return code;
}


/* ---- Landing a bootstrap at /bowls/NAME: extract, flatten, configure. ---- */

/*
        Land a bootstrap at /bowls/NAME.

        Setup downloads the archive; this file does not talk to pacman, apt
        or apk. It probes the compression magic, extracts, hoists a prefix
        directory when the marker is under one child (Arch root.x86_64, a
        Nix version dir), and         writes the files a first isolated install
        actually needs -- resolv.conf without a stub resolver, plus the
        manager conf that exists in that tree. Isolated launches rewrite
        resolv.conf only; they do not clear manager locks.

        gzip and xz applets are compiled in. Extract looks at the archive
        magic and `tar` unpacks gzip, xz, zstd and uncompressed streams
        in-process.
*/



#define BOWL_TEXT 131072
/* For a pacman mirror list with no live server: the machine's own port's. */
#if X64
#define BOWL_GEO_MIRROR \
        "Server = https://geo.mirror.pkgbuild.com/$repo/os/$arch\n"
#elif ARM64
#define BOWL_GEO_MIRROR "Server = http://mirror.archlinuxarm.org/$arch/$repo\n"
#else
#define BOWL_GEO_MIRROR "Server = https://riscv.mirror.pkgbuild.com/repo/$repo\n"
#endif

static bool bowl_has(string_address root, string_address path)
{
        bipolar found;

        if (!bowl_named_root(root) || bowl_path_steps(path))
                return false;

        /*
                In particular, /usr/bin/pacman -> /bin/true means the bowl's
                /bin/true, never the host's. A plain access(ROOT + path) would
                cross that boundary for absolute symlinks.
        */
        found = bowl_open_in_root(root, path, O_PATH | O_CLOEXEC);
        if (found < 0)
                return false;

        system_close(found);
        return true;
}

/*
        bowl_has for a directory one level inside a named root -- Arch's
        root.x86_64, before flattening hoists it -- which is not itself
        /bowls/NAME and so is refused by bowl_has. The same protection holds:
        the child is opened relative to the root without following a link, so
        a member that is a symlink cannot move the question out of the bowl,
        and the marker resolves with the child as its own /.
*/
static bool bowl_has_below(string_address root, string_address child,
                           string_address path)
{
        bipolar root_handle;
        bipolar child_handle;
        bipolar found;

        if (!bowl_named_root(root) || !bowl_name(child, true) ||
            bowl_path_steps(path))
                return false;

        root_handle = bowl_open_directory(root, false, null);
        if (root_handle < 0)
                return false;
        child_handle = system_open_at(root_handle, child,
                                      O_PATH | O_DIRECTORY | O_NOFOLLOW |
                                              O_CLOEXEC);
        system_close(root_handle);
        if (child_handle < 0)
                return false;

        found = system_open_resolved(child_handle, path, O_PATH | O_CLOEXEC,
                                     SYSTEM_RESOLVE_IN_ROOT |
                                         SYSTEM_RESOLVE_NO_MAGICLINKS);
        system_close(child_handle);
        if (found < 0)
                return false;

        system_close(found);
        return true;
}

static bool bowl_root_busy(string_address root)
{
        file_walk walk;
        struct linux_dirent64 address_to entry;
        bool busy = false;

        if (!file_walk_open(address_of walk, AT_FDCWD, root))
                return false;

        while ((entry = file_walk_next(address_of walk)))
        {
                if (!file_is_dot(entry->d_name))
                {
                        busy = true;
                        break;
                }
        }

        file_walk_close(address_of walk);
        return busy;
}

#define BOWL_RESET_DEPTH 48

/* Entries a caller leaves where they are. Null ends the list, and a null
   list keeps nothing. */
static bool bowl_reset_kept(string_address address_to keep, string_address name)
{
        for (positive at = 0; keep && keep[at]; at++)
                if (string_equals(name, keep[at]))
                        return true;

        return false;
}

/*
        Everything under a directory, gone: a subdirectory emptied first and
        then removed, anything else unlinked. An entry already missing is
        somebody else having removed it, not a failure.

        A name the walk itself found is opened with O_NOFOLLOW, so a
        subdirectory swapped for a symlink between the stat and the open
        fails closed instead of walking out of the tree. A path somebody
        named is opened the way naming one means, following: that is how
        moonwater wipe reaches a /root the machine has put something on.

        keep names entries to leave, and only at this level -- the recursion
        passes none, so a kept name further down is an ordinary entry.
*/
static bipolar bowl_reset_walk_at(bipolar directory, string_address name,
                                  positive depth, bool named,
                                  string_address address_to keep)
{
        file_walk walk;
        struct linux_dirent64 address_to entry;
        bipolar failed = 0;

        if (depth >= BOWL_RESET_DEPTH)
                return -ERROR_LOOP;

        if (!(named ? file_walk_open(address_of walk, directory, name)
                    : file_walk_open_found(address_of walk, directory, name)))
                return walk.error == -ERROR_NO_ENTRY ? 0 : walk.error;

        while (!failed && (entry = file_walk_next(address_of walk)))
        {
                if (file_is_dot(entry->d_name) ||
                    bowl_reset_kept(keep, entry->d_name))
                        continue;

                if (file_is_directory(walk.handle, entry->d_name))
                {
                        /* A path somebody named is the tree's own root and
                           not a step into it, so the cap counts from below. */
                        failed = bowl_reset_walk_at(walk.handle, entry->d_name,
                                                    named ? depth : depth + 1,
                                                    false, null);
                        if (!failed)
                                failed = system_remove_at(walk.handle,
                                    entry->d_name, AT_REMOVEDIR);
                }
                else
                        failed = system_remove_at(walk.handle, entry->d_name, 0);

                if (failed == -ERROR_NO_ENTRY)
                        failed = 0;
        }

        if (!failed)
                failed = walk.error;
        file_walk_close(address_of walk);
        return failed;
}

static bipolar bowl_reset_walk(string_address path, positive depth)
{
        p8 leaf[BOWL_PATH_LIMIT];
        bipolar parent = system_open_parent_nofollow(AT_FDCWD, path, false, 0,
                                                      leaf, sizeof(leaf));
        if (parent < 0)
                return parent == -ERROR_NO_ENTRY ? 0 : parent;
        bipolar failed = bowl_reset_walk_at(parent, leaf, depth, false, null);
        system_close(parent);
        return failed;
}

static b32 bowl_reset_root(string_address root)
{
        bipolar failed = bowl_reset_walk(root, 0);

        if (failed < 0)
                return bowl_fail(root, failed);
        return 0;
}

static b32 bowl_forget_path(string_address path)
{
        p8 leaf[BOWL_PATH_LIMIT];
        bipolar parent = system_open_parent_nofollow(AT_FDCWD, path, false, 0,
                                                      leaf, sizeof(leaf));
        bipolar failed;

        if (parent < 0)
                return parent == -ERROR_NO_ENTRY ? 0 : bowl_fail(path, parent);
        if (file_is_directory(parent, leaf))
        {
                failed = bowl_reset_walk_at(parent, leaf, 0, false, null);
                if (!failed)
                        failed = system_remove_at(parent, leaf, AT_REMOVEDIR);
        }
        else
                failed = system_remove_at(parent, leaf, 0);
        system_close(parent);

        if (failed < 0 && failed != -ERROR_NO_ENTRY)
                return bowl_fail(path, failed);
        return 0;
}

static b32 bowl_recover_from(string_address root, string_address marker)
{
        p8 from[BOWL_PATH_LIMIT];

        if (!bowl_root_path(from, sizeof(from), root, ".bowl-from"))
                return bowl_refuse("bowl path is too long\n");

        if (system_access_at(AT_FDCWD, from, 0) < 0)
                return 0;

        if (bowl_has(root, marker) || bowl_root_busy(root))
                return bowl_forget_path(from);

        system_remove_at(AT_FDCWD, root, AT_REMOVEDIR);
        {
                bipolar failed = system_rename_at(AT_FDCWD, from, AT_FDCWD,
                                                  root, 0);

                if (failed < 0)
                        return bowl_fail(from, failed);
        }
        return 0;
}

static b32 bowl_write_bytes(string_address path, string_address text,
                            positive length)
{
        p8 temp[BOWL_PATH_LIMIT];
        p8 leaf[BOWL_PATH_LIMIT];
        bipolar parent = system_open_parent_nofollow(AT_FDCWD, path, false, 0,
                                                      leaf, sizeof(leaf));
        bipolar handle;
        bipolar failed = 0;

        if (parent < 0)
                return bowl_fail(path, parent);

        handle = file_temporary_open_at(parent, leaf, temp, sizeof(temp),
            ".bowl-", 6, (positive)system_call_1(syscall(getpid), 0), 128, 0644);
        if (handle < 0)
        {
                system_close(parent);
                return bowl_fail(path, handle);
        }

        if (system_write_all((positive)handle, text, length) != length)
                failed = -ERROR_INPUT_OUTPUT;
        if (!failed)
                failed = system_call_1(syscall(fsync), (positive)handle);
        system_close(handle);

        if (!failed)
                failed = system_rename_at(parent, temp, parent, leaf, 0);
        if (failed < 0)
                system_remove_at(parent, temp, 0);
        system_close(parent);

        return failed < 0 ? bowl_fail(path, failed) : 0;
}

static string_address bowl_line_word(string_address line,
                                     bool address_to commented)
{
        line += string_span(line, string_set_blanks);
        address_to commented = *line == '#';
        if (address_to commented)
                line += 1 + string_span(line + 1, string_set_blanks);
        return line;
}

static bool bowl_keyword(string_address line, string_address word)
{
        positive length = string_length(word);

        if (string_compare_max(line, word, length))
                return false;

        return !line[length] || line[length] == ' ' || line[length] == '\t' ||
               line[length] == '=' || line[length] == '\n';
}

static bool bowl_section_is(string_address line, string_address name)
{
        positive length = string_length(name);

        if (line[0] != '[')
                return false;
        if (string_compare_max(line + 1, name, length))
                return false;
        return line[1 + length] == ']';
}

static bool bowl_isolation_keyword(string_address line)
{
        return bowl_keyword(line, "DisableSandbox") ||
               bowl_keyword(line, "DisableSandboxFilesystem") ||
               bowl_keyword(line, "DisableSandboxSyscalls");
}

static bool bowl_put_isolation(byte_store address_to out)
{
        string_address text =
            "DisableSandboxFilesystem\n"
            "DisableSandboxSyscalls\n";

        return byte_store_append_exact(out, text, string_length(text));
}

static bool bowl_options_keyword(string_address text, string_address word)
{
        positive at = 0;
        positive length = string_length(text);
        bool in_options = false;

        while (at < length)
        {
                bool commented = false;
                positive stop = at + memory_span_without_byte(text + at, '\n',
                                                              length - at);
                string_address token;

                token = bowl_line_word(text + at, address_of commented);
                if (!commented && token[0] == '[')
                        in_options = bowl_section_is(token, "options");

                if (in_options && !commented && bowl_keyword(token, word))
                        return true;

                at = stop + (stop < length);
        }

        return false;
}

static bool bowl_nameserver_ok(string_address line, positive length)
{
        /* Blanks stop at the line's newline or terminator, so they stay
           inside length. */
        positive at = string_span(line, string_set_blanks);

        if (length - at < 11 || string_compare_max(line + at, "nameserver ", 11))
                return false;

        at += 11;
        at += string_span(line + at, string_set_blanks);

        if (at >= length || line[at] == '\n' || line[at] == '#')
                return false;

        return string_compare_max(line + at, "127.", 4) != 0;
}

static b32 bowl_wait_applet(bipolar child, string_address what)
{
        positive status = 0;
        bipolar failed;

        if (child < 0)
                return bowl_fail(what, child);

        failed = system_wait4_retry(child, address_of status, 0, null);
        if (failed < 0)
                return bowl_fail(what, failed);

        if (wait_status_code(status))
                return bowl_refuse(what);

        return 0;
}

/* A bootstrap is a ustar archive or one packed by a codec tar reads. */
static bool bowl_archive_known(string_address archive)
{
        p8 head[512];
        bipolar got = file_read_once_at(AT_FDCWD, archive, head, sizeof(head));

        if (got < 6)
                return false;

        p8 pack = tar_pack_from_magic(head, (positive)got);
        return pack == TAR_PACK_GZIP || pack == TAR_PACK_XZ ||
               pack == TAR_PACK_ZSTD ||
               (got >= 262 && !string_compare_max(head + 257, "ustar", 5));
}

static b32 bowl_extract(string_address archive, string_address root)
{
        string_address tar_file[] = {
            "tar", "-x", "-f", archive, "-C", root, null};
        bipolar extract;

        log_flush();
        if (bowl_archive_known(archive))
        {
                extract = system_fork();
                if (extract == 0)
                {
                        program_arguments_use(tar_file, 6);
                        exit(file_tar_without_special());
                }

                return bowl_wait_applet(extract,
                                        "tar could not extract the archive\n");
        }

        return bowl_refuse("archive is not a bootstrap\n");
}

/* The children of root holding the marker -- none, one, or 2 for more than
   one -- with the first kept in rel as "/NAME", or negative when root does
   not open as a directory. Landing asks whether a busy root is a prefix
   waiting to be hoisted; flattening hoists only a lone one. */
static bipolar bowl_prefix_find(string_address root, string_address marker,
                                p8 address_to rel)
{
        file_walk walk;
        struct linux_dirent64 address_to entry;
        p8 name[258];
        bipolar found = 0;

        if (!file_walk_open(address_of walk, AT_FDCWD, root))
                return -ERROR_NOT_DIRECTORY;

        while (found < 2 && (entry = file_walk_next(address_of walk)))
        {
                if (file_is_dot(entry->d_name))
                        continue;

                //      A child is not /bowls/NAME, which bowl_has insists on,
                //      so it is asked with bowl_has_below: the check that
                //      refused it here is what broke every Arch landing.
                name[0] = '/';
                string_copy_max_end(name + 1, entry->d_name, sizeof(name) - 2);
                if (bowl_has_below(root, entry->d_name, marker) && !found++)
                        memory_copy(rel, name, sizeof(name));
        }

        file_walk_close(address_of walk);
        return found;
}

static b32 bowl_flatten(string_address root, string_address marker)
{
        p8 sibling[BOWL_PATH_LIMIT];
        p8 from[BOWL_PATH_LIMIT];
        p8 rel[258];
        bipolar failed;

        if (bowl_has(root, marker))
                return 0;

        failed = bowl_prefix_find(root, marker, rel);
        if (failed < 0)
                return bowl_fail(root, failed);
        if (failed != 1)
                return bowl_refuse(failed ? "archive has more than one root\n"
                                          : "archive is not a bowl bootstrap\n");

        if (!bowl_root_path(sibling, sizeof(sibling), root, ".bowl-from"))
                return bowl_refuse("bowl path is too long\n");

        if (system_access_at(AT_FDCWD, sibling, 0) >= 0)
        {
                failed = bowl_forget_path(sibling);
                if (failed)
                        return failed;
        }

        failed = system_rename_at(AT_FDCWD, root, AT_FDCWD, sibling, 0);
        if (failed < 0)
                return bowl_fail(sibling, failed);

        if (!bowl_root_path(from, sizeof(from), sibling, rel))
                return bowl_refuse("bowl path is too long\n");

        failed = system_rename_at(AT_FDCWD, from, AT_FDCWD, root, 0);
        if (failed < 0)
                return bowl_fail(from, failed);

        if (system_remove_at(AT_FDCWD, sibling, AT_REMOVEDIR) < 0)
                bowl_forget_path(sibling);
        return 0;
}

static b32 bowl_write_resolv(string_address root)
{
        p8 path[BOWL_PATH_LIMIT];
        p8 host[4096];
        p8 buffer[4096];
        byte_store out = {buffer, sizeof(buffer), 0};
        bipolar got;
        bipolar failed;
        positive at = 0;
        string_address fallback = "nameserver 1.1.1.1\n";

        if (!bowl_root_path(path, sizeof(path), root, "/etc/resolv.conf"))
                return bowl_refuse("bowl path is too long\n");

        {
                p8 etc[BOWL_PATH_LIMIT];

                if (!bowl_root_path(etc, sizeof(etc), root, "/etc"))
                        return bowl_refuse("bowl path is too long\n");
                failed = bowl_mkdir(etc);
                if (failed < 0)
                        return bowl_fail(etc, failed);
        }

        if (!byte_store_append_exact(address_of out, fallback,
                                     string_length(fallback)))
                return bowl_refuse("resolv.conf is too long\n");

        got = file_slurp("/etc/resolv.conf", host, sizeof(host));
        if (got > 0 && (positive)got >= sizeof(host))
                got = (bipolar)(sizeof(host) - 1);
        if (got > 0)
                host[got] = end;
        while (got > 0 && at < (positive)got)
        {
                positive start = at;
                positive stop = start + memory_span_without_byte(
                                            host + start, '\n', (positive)got - start);

                if (bowl_nameserver_ok(host + start, stop - start) &&
                    string_compare_max(host + start, fallback,
                                       string_length(fallback) - 1))
                {
                        if (!byte_store_append_exact(address_of out, host + start,
                                                     stop - start) ||
                            !byte_store_append_exact(address_of out, "\n", 1))
                                return bowl_refuse("resolv.conf is too long\n");
                }

                at = stop + (stop < (positive)got);
        }

        return bowl_write_bytes(path, out.bytes, out.used);
}

/*
        The machine's zone, as the bowl's /etc/localtime.

        A bowl's programs read their own /etc/localtime, not the file
        moonwater timezone writes, so without this every guest shows the
        zone its distribution shipped with -- usually UTC -- whatever the
        machine is set to. The file is a TZif carrying the POSIX rule
        (clock_zone_tzif), which needs no tzdata in the guest.

        The old /etc/localtime is usually a symlink into the guest's own
        zoneinfo. bowl_write_bytes renames over the link rather than writing
        through it, so the guest's tzdata is never touched. A machine that
        has never set a zone leaves the bowl's alone.
*/
static b32 bowl_write_localtime_at(string_address path)
{
        p8 zone[80];
        p8 file[512];
        positive length;

        if (clock_zone_load(zone, sizeof(zone)) <= 0)
                return 0;

        length = clock_zone_tzif(zone, file, sizeof(file));
        if (!length)
                return bowl_refuse("the machine's timezone does not parse\n");

        //      Every boot re-applies the zone, and a write here is an fsync
        //      per bowl on the way to a prompt. A file that already says the
        //      same thing is left alone. The read does not follow a link --
        //      a symlink into the guest's zoneinfo is exactly what differs.
        {
                p8 held[sizeof(file)];
                bipolar handle = system_open_at(AT_FDCWD, path,
                                                O_RDONLY | O_NOFOLLOW |
                                                        O_CLOEXEC);

                if (handle >= 0)
                {
                        bipolar had = system_read_once(handle, held,
                                                       sizeof(held));

                        system_close(handle);
                        if (had == (bipolar)length &&
                            !memory_compare(held, file, length))
                                return 0;
                }
        }
        return bowl_write_bytes(path, file, length);
}

static b32 bowl_write_localtime(string_address root)
{
        p8 path[BOWL_PATH_LIMIT];

        if (!bowl_has(root, "/etc"))
                return 0;
        if (!bowl_root_path(path, sizeof(path), root, "/etc/localtime"))
                return bowl_refuse("bowl path is too long\n");
        return bowl_write_localtime_at(path);
}

/*
        Moonwater's own /etc/localtime. A bowl program launched in the fast
        view -- which is most of them, btop included -- sees Moonwater's /etc
        and not its bowl's, so this is the file its glibc reads. Without it
        such a program shows UTC however the machine is set, which is exactly
        what a clock two hours behind in btop was.
*/
static b32 bowl_write_localtime_host(void)
{
        bipolar made = bowl_mkdir("/etc");

        if (made < 0)
                return bowl_fail("/etc", made);
        return bowl_write_localtime_at("/etc/localtime");
}

/* Every bowl's /etc/localtime, after the machine's zone changes. Returns how
   many took it and counts the ones that refused; a refusal is skipped, not
   fatal, and bowl_write_bytes has already said which and why. */
static positive bowl_write_localtime_all(positive address_to refused)
{
        file_walk walk;
        p8 root[BOWL_PATH_LIMIT];
        positive written = 0;

        if (!file_walk_open(address_of walk, AT_FDCWD, BOWL_ROOT_DIRECTORY))
                return 0;
        while (bowl_next_root(address_of walk, root))
        {
                if (!bowl_has(root, "/etc"))
                        continue;
                if (!bowl_write_localtime(root))
                        written++;
                else if (refused)
                        address_to refused += 1;
        }
        file_walk_close(address_of walk);
        return written;
}

static b32 bowl_write_mirror(string_address root)
{
        p8 path[BOWL_PATH_LIMIT];
        p8 text[BOWL_TEXT];
        p8 buffer[BOWL_TEXT];
        byte_store out = {buffer, sizeof(buffer), 0};
        bipolar got;
        positive at = 0;
        bool live = false;

        if (!bowl_root_path(path, sizeof(path), root, "/etc/pacman.d/mirrorlist"))
                return bowl_refuse("bowl path is too long\n");

        got = file_slurp(path, text, sizeof(text));
        if (got <= 0)
                return bowl_fail(path, got < 0 ? got : -ERROR_NO_ENTRY);

        if ((positive)got >= sizeof(text) - 1)
                return bowl_refuse("mirrorlist is too long\n");
        text[got] = end;

        while (at < (positive)got)
        {
                bool commented = false;
                positive start = at;

                at += memory_span_without_byte(text + at, '\n', (positive)got - at);

                if (bowl_keyword(bowl_line_word(text + start, address_of commented),
                                 "Server") &&
                    !commented)
                        live = true;

                at = at + (at < (positive)got);
        }

        if ((!live &&
             !byte_store_append_exact(address_of out, BOWL_GEO_MIRROR,
                                      string_length(BOWL_GEO_MIRROR))) ||
            !byte_store_append_exact(address_of out, text, (positive)got))
                return bowl_refuse("mirrorlist is too long\n");

        return bowl_write_bytes(path, out.bytes, out.used);
}

static fn bowl_clear_lock(string_address root, string_address rel)
{
        p8 path[BOWL_PATH_LIMIT];

        if (bowl_root_path(path, sizeof(path), root, rel))
        {
                p8 leaf[BOWL_PATH_LIMIT];
                bipolar parent = system_open_parent_nofollow(AT_FDCWD, path,
                    false, 0, leaf, sizeof(leaf));
                if (parent >= 0)
                {
                        system_remove_at(parent, leaf, 0);
                        system_close(parent);
                }
        }
}

static bool bowl_archive_usable(string_address path, p64 floor)
{
        file_facts facts;

        return file_look_at(path, address_of facts) && facts.size >= floor &&
               bowl_archive_known(path);
}

static b32 bowl_write_pacman(string_address root);
static b32 bowl_write_apk(string_address root);
static b32 bowl_write_apt(string_address root);
static b32 bowl_write_nix(string_address root);

static b32 bowl_configure(string_address root)
{
        b32 failed = bowl_write_resolv(root);

        if (!failed)
                failed = bowl_write_localtime(root);

        if (!failed && bowl_has(root, "/etc/pacman.conf"))
        {
                if (bowl_has(root, "/etc/pacman.d/mirrorlist"))
                        failed = bowl_write_mirror(root);
                if (!failed)
                        failed = bowl_write_pacman(root);
                bowl_clear_lock(root, "/var/lib/pacman/db.lck");
        }

        if (!failed && bowl_has(root, "/etc/apk/repositories"))
                failed = bowl_write_apk(root);
        bowl_clear_lock(root, "/lib/apk/db/lock");

        if (!failed && bowl_has(root, "/etc/apt"))
                failed = bowl_write_apt(root);
        bowl_clear_lock(root, "/var/lib/dpkg/lock");
        bowl_clear_lock(root, "/var/lib/dpkg/lock-frontend");
        bowl_clear_lock(root, "/var/lib/apt/lists/lock");
        bowl_clear_lock(root, "/var/cache/apt/archives/lock");
        bowl_clear_lock(root, "/run/dnf/dnf.conf.lock");
        bowl_clear_lock(root, "/var/lib/rpm/.rpm.lock");

        if (!failed && bowl_has(root, "/nix/.reginfo"))
                failed = bowl_write_nix(root);

        return failed;
}

static b32 bowl_write_apk(string_address root)
{
        p8 path[BOWL_PATH_LIMIT];
        p8 existing[4096];
        bipolar got;
        positive at = 0;
        string_address text =
            "https://dl-cdn.alpinelinux.org/alpine/latest-stable/main\n"
            "https://dl-cdn.alpinelinux.org/alpine/latest-stable/community\n";

        if (!bowl_root_path(path, sizeof(path), root, "/etc/apk/repositories"))
                return bowl_refuse("bowl path is too long\n");

        got = file_slurp(path, existing, sizeof(existing));
        if (got > 0 && (positive)got >= sizeof(existing))
                got = (bipolar)(sizeof(existing) - 1);
        if (got > 0)
                existing[got] = end;
        while (got > 0 && at < (positive)got)
        {
                bool commented = false;
                string_address word =
                    bowl_line_word(existing + at, address_of commented);

                if (!commented && !string_compare_max(word, "https://", 8))
                        return 0;

                at += memory_span_without_byte(existing + at, '\n',
                                               (positive)got - at);
                at = at + (at < (positive)got);
        }

        return bowl_write_bytes(path, text, string_length(text));
}

static b32 bowl_write_apt(string_address root)
{
        p8 dir[BOWL_PATH_LIMIT];
        p8 path[BOWL_PATH_LIMIT];
        bipolar failed;
        string_address text =
            "APT::Sandbox::User \"root\";\n"
            "DPkg::Use-Pty \"false\";\n";

        if (!bowl_root_path(dir, sizeof(dir), root, "/etc/apt/apt.conf.d") ||
            !bowl_root_path(path, sizeof(path), root,
                            "/etc/apt/apt.conf.d/99bowl"))
                return bowl_refuse("bowl path is too long\n");

        failed = bowl_mkdir(dir);
        if (failed < 0)
                return bowl_fail(dir, failed);

        return bowl_write_bytes(path, text, string_length(text));
}

static b32 bowl_write_pacman(string_address root)
{
        p8 path[BOWL_PATH_LIMIT];
        p8 text[BOWL_TEXT];
        p8 buffer[BOWL_TEXT];
        byte_store out = {buffer, sizeof(buffer), 0};
        bipolar got;
        positive at = 0;
        bool seen_options = false;
        bool injected = false;

        if (!bowl_root_path(path, sizeof(path), root, "/etc/pacman.conf"))
                return bowl_refuse("bowl path is too long\n");

        got = file_slurp(path, text, sizeof(text));
        if (got <= 0)
                return bowl_fail(path, got < 0 ? got : -ERROR_NO_ENTRY);

        if ((positive)got >= sizeof(text) - 1)
                return bowl_refuse("pacman.conf is too long\n");
        text[got] = end;

        /*
                Pacman only reads these from [options]. Appending them after
                [extra] is a no-op and prints "directive not recognized".
        */
        if (bowl_options_keyword(text, "DisableSandboxFilesystem") &&
            bowl_options_keyword(text, "DisableSandboxSyscalls"))
                return 0;

        while (at < (positive)got)
        {
                bool commented = false;
                positive start = at;
                positive stop = start + memory_span_without_byte(
                                            text + start, '\n', (positive)got - start);
                string_address word;

                word = bowl_line_word(text + start, address_of commented);

                if (!commented && word[0] == '[')
                {
                        if (bowl_section_is(word, "options"))
                                seen_options = true;
                        else if (!injected)
                        {
                                if ((!seen_options &&
                                     !byte_store_append_exact(address_of out,
                                                              "[options]\n", 10)) ||
                                    !bowl_put_isolation(address_of out))
                                        return bowl_refuse(
                                            "pacman.conf is too long\n");
                                injected = true;
                        }
                }

                if (!commented && bowl_isolation_keyword(word))
                {
                        at = stop + (stop < (positive)got);
                        continue;
                }

                if (bowl_keyword(word, "DownloadUser") ||
                    bowl_keyword(word, "CheckSpace"))
                {
                        if (!commented &&
                            !byte_store_append_exact(address_of out, "#", 1))
                                return bowl_refuse("pacman.conf is too long\n");
                }

                if (!byte_store_append_exact(address_of out, text + start,
                                             stop - start + (stop < (positive)got)))
                        return bowl_refuse("pacman.conf is too long\n");

                at = stop + (stop < (positive)got);
        }

        if (!injected)
        {
                if ((!seen_options &&
                     !byte_store_append_exact(address_of out, "[options]\n", 10)) ||
                    !bowl_put_isolation(address_of out))
                        return bowl_refuse("pacman.conf is too long\n");
        }

        return bowl_write_bytes(path, out.bytes, out.used);
}

/* ---- Digests: the download a setup pins, and every blob an OCI layout names. ---- */

#define BOWL_DIGEST_HEX 64

static bool bowl_hex_digest(string_address text, positive length)
{
        //      Lower case only: a digest spelled any other way is not the
        //      one an OCI layout names.
        static const b8 bowl_set_lower_hex[STRING_SET_BYTES] = {
            ['0' ... '9'] = 1, ['a' ... 'f'] = 1};

        return length == BOWL_DIGEST_HEX &&
               string_span_max(text, length, bowl_set_lower_hex) == length;
}

/* Everything a descriptor reads, written into a digest that is already open
   (the signed archive adds its trailer after it), with how many bytes that
   was when asked. False when a read failed, an interrupted one retried. One
   buffer for both callers. */
static bool bowl_digest_stream(bipolar handle, digest_state address_to digest,
                               p64 address_to size)
{
        static p8 chunk[65536];
        bipolar got;
        p64 total = 0;

        while ((got = system_read_retry((positive)handle, chunk,
                                        sizeof(chunk))) > 0)
        {
                digest_write(digest, chunk, (positive)got);
                total += (p64)got;
        }
        if (got < 0)
                return false;
        if (size)
                address_to size = total;
        return true;
}

/* The SHA-256 of what a descriptor reads, as lower-case hex, with how many
   bytes that was. */
static bool bowl_sha256_of(bipolar handle, p8 address_to hex, p64 address_to size)
{
        digest_state digest;
        p8 sum[32];
        bool whole;

        digest_open(address_of digest, DIGEST_SHA256, 32);
        whole = bowl_digest_stream(handle, address_of digest, size);
        digest_close(address_of digest, sum);
        if (!whole)
                return false;

        memory_into_hex(hex, sum, sizeof(sum));
        hex[BOWL_DIGEST_HEX] = end;
        return true;
}

/* Whether a download is the one the table pins. A row that pins no digest
   is not one this takes: a release that is unpinned is refused, never
   waved through (Arch's, Debian's and Arch Linux RISC-V's rows were once
   taken on the mirror's word alone). */
static bool bowl_archive_digest_ok(string_address path, string_address want)
{
        p8 hex[BOWL_DIGEST_HEX + 1];
        bipolar handle;
        bool same;

        if (!want)
                return false;

        handle = system_open_at(AT_FDCWD, path,
                                FILE_READ | O_NOFOLLOW | O_CLOEXEC);
        if (handle < 0)
                return false;

        same = bowl_sha256_of(handle, hex, null) && string_equals(hex, want);
        system_close(handle);
        return same;
}

/* ---- Signed downloads: an OpenPGP detached signature against a pinned key. ---- */

/*
        Arch Linux ARM publishes one root tarball, always the latest, with no
        dated copy to pin a digest to, and signs it with its build system's
        RSA key. The key is pinned here as its modulus and exponent and as
        the fingerprint those make (checked again on every use, so a table
        that drifts from its fingerprint verifies nothing); the signature is
        fetched from the mirror beside the archive and proves nothing the
        key did not sign. since is the floor on the signature's creation
        time: an older archive, validly signed, is a replay and is refused,
        and the floor is what a newer release raises.

        Read as RFC 4880 section 5.2 has it, version 4 only: a binary
        document signature (0x00), RSA (1), SHA-256 or SHA-512, the creation
        time and the issuer fingerprint in the hashed area (the unhashed area
        is an attacker's and is not read), an unknown critical subpacket a
        refusal, one packet and nothing after it. The key is asked to make
        exactly the PKCS#1 v1.5 block the net code's RSA checks.
*/
struct bowl_key
{
        string_address fingerprint; // 40 upper-case hex digits, the v4 one
        p32 created;                // the key packet's creation time
        p32 exponent;
        string_address modulus;     // big endian, lower-case hex
        p32 since;                  // no signature made before this
};

static bool crypto_rsa_pkcs1(p8 address_to n_bytes, positive n_length,
                             p64 exponent, p8 address_to sig,
                             positive sig_length,
                             const p8 address_to digestinfo,
                             positive digestinfo_length, p8 address_to hash,
                             positive hash_length);

#define BOWL_KEY_BYTES 512
#define BOWL_SIGNATURE_BYTES 2048

static positive bowl_key_modulus(const struct bowl_key address_to key,
                                 p8 address_to into)
{
        static p8 digits[256];
        positive length = string_length(key->modulus);

        if (length > BOWL_KEY_BYTES * 2 || (length & 1))
                return 0;

        //      Lower case only, as the table holds it: anything else is the
        //      sentinel, and the decoder stops before the pair it is in.
        if (!digits[0])
        {
                memory_fill(digits, 255, sizeof(digits));
                for (positive at = 0; at < 10; at++)
                        digits['0' + at] = (p8)at;
                for (positive at = 0; at < 6; at++)
                        digits['a' + at] = (p8)(10 + at);
        }
        return memory_decode_power2(into, (address_any)key->modulus, length / 2,
                                    digits, 4) == length / 2
                   ? length / 2
                   : 0;
}

// The bits in a byte, counted from its highest set one.
static positive bowl_key_width(p8 byte)
{
        return byte ? top_bit_known(byte) + 1 : 0;
}

static positive bowl_key_mpi(p8 address_to into, const p8 address_to bytes,
                             positive length)
{
        positive zeros = memory_span_byte((address_any)bytes, 0, length);
        positive bits;

        bytes += zeros;
        length -= zeros;
        bits = length ? length * 8 - 8 + bowl_key_width(bytes[0]) : 0;
        network_store_16(into, (p16)bits);
        memory_copy(into + 2, bytes, length);
        return 2 + length;
}

/* The v4 fingerprint the key's numbers make, as upper-case hex. */
static bool bowl_key_fingerprint(const struct bowl_key address_to key,
                                 p8 address_to hex)
{
        p8 modulus[BOWL_KEY_BYTES];
        p8 exponent[4];
        p8 packet[3 + 6 + 2 + BOWL_KEY_BYTES + 2 + 4];
        p8 sum[20];
        positive modulus_length = bowl_key_modulus(key, modulus);
        positive at = 3;
        digest_state digest;

        if (!modulus_length)
                return false;

        network_store_32(exponent, key->exponent);
        packet[at++] = 4;
        network_store_32(packet + at, key->created);
        at += 4;
        packet[at++] = 1;
        at += bowl_key_mpi(packet + at, modulus, modulus_length);
        at += bowl_key_mpi(packet + at, exponent, 4);
        packet[0] = 0x99;
        network_store_16(packet + 1, (p16)(at - 3));

        digest_open(address_of digest, DIGEST_SHA1, 20);
        digest_write(address_of digest, packet, at);
        digest_close(address_of digest, sum);
        memory_into_hex_case(hex, sum, sizeof(sum), 1);
        hex[40] = end;
        return true;
}

/* The signature packet's fields, or false for anything this does not read. */
struct bowl_signature
{
        positive hash;           // 8 or 10: the OpenPGP hash algorithm
        p32 created;
        const p8 address_to hashed;     // the hashed subpackets' bytes
        positive hashed_length;
        p8 left[2];
        const p8 address_to mpi;        // the signature integer, no header
        positive mpi_length;
};

static bool bowl_signature_read(const p8 address_to bytes, positive length,
                                const struct bowl_key address_to key,
                                struct bowl_signature address_to into)
{
        byte_reader packet = byte_reader_open(bytes, length);
        p8 tag = byte_reader_u8(&packet);
        positive size;
        byte_reader body;
        byte_reader hashed;
        positive unhashed;
        positive bits;
        bool created = false;
        bool listed = false;
        bool issuer = false;

        if (!(tag & 0x80))
                return false;

        // Old or new packet header; tag 2 is a signature.
        if (tag & 0x40)
        {
                p8 first;

                if ((tag & 0x3f) != 2)
                        return false;
                first = byte_reader_u8(&packet);
                if (first < 192)
                        size = first;
                else if (first < 224)
                        size = (positive)((first - 192) << 8) +
                               byte_reader_u8(&packet) + 192;
                else
                        return false;
        }
        else
        {
                if (((tag >> 2) & 15) != 2)
                        return false;
                if ((tag & 3) == 0)
                        size = byte_reader_u8(&packet);
                else if ((tag & 3) == 1)
                        size = byte_reader_u16(&packet);
                else
                        return false;
        }

        // One packet and nothing after it.
        body = byte_reader_window(&packet, size);
        if (!byte_reader_end(&packet) || size < 12)
                return false;

        if (byte_reader_u8(&body) != 4 || byte_reader_u8(&body) != 0 ||
            byte_reader_u8(&body) != 1)
                return false;
        into->hash = byte_reader_u8(&body);
        if (into->hash != 8 && into->hash != 10)
                return false;
        into->hashed_length = byte_reader_u16(&body);
        hashed = byte_reader_window(&body, into->hashed_length);
        if (!byte_reader_ok(&body))
                return false;
        into->hashed = byte_reader_here(&hashed);

        while (byte_reader_left(&hashed))
        {
                positive span = byte_reader_u8(&hashed);
                byte_reader field;
                const p8 address_to print;
                p8 kind;

                if (span == 255)
                        return false;
                if (span >= 192)
                        span = (positive)((span - 192) << 8) +
                               byte_reader_u8(&hashed) + 192;
                field = byte_reader_window(&hashed, span);
                if (!span || !byte_reader_ok(&hashed))
                        return false;
                kind = byte_reader_u8(&field);
                if (kind == 2 && span == 5 && !created)
                {
                        into->created = byte_reader_u32(&field);
                        created = true;
                }
                else if (kind == 33 && span == 22 && !listed &&
                         byte_reader_u8(&field) == 4 &&
                         (print = byte_reader_take(&field, 20)))
                {
                        p8 hex[41];

                        memory_into_hex_case(hex, (address_any)print, 20, 1);
                        hex[40] = end;
                        listed = true;
                        issuer = string_equals((string_address)hex,
                                               key->fingerprint);
                }
                else if (kind == 2 || kind == 33)
                        return false;
                else if (kind & 0x80)
                        return false;
        }
        if (!byte_reader_ok(&hashed) || !created || !issuer)
                return false;

        // The unhashed area is an attacker's and is not read.
        unhashed = byte_reader_u16(&body);
        (void)byte_reader_skip(&body, unhashed);
        into->left[0] = byte_reader_u8(&body);
        into->left[1] = byte_reader_u8(&body);
        bits = byte_reader_u16(&body);
        into->mpi_length = (bits + 7) / 8;
        into->mpi = byte_reader_take(&body, into->mpi_length);
        // The count is the integer's own width, so one integer has one spelling.
        return byte_reader_end(&body) && bits && into->mpi[0] &&
               bits == into->mpi_length * 8 - 8 + bowl_key_width(into->mpi[0]);
}

/* Whether the signature file is a good signature by key over the archive,
   made no earlier than key->since. */
static bool bowl_signature_ok(string_address archive, string_address signature,
                              const struct bowl_key address_to key)
{
        static const p8 info_256[] = {0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60,
                                      0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02,
                                      0x01, 0x05, 0x00, 0x04, 0x20};
        static const p8 info_512[] = {0x30, 0x51, 0x30, 0x0d, 0x06, 0x09, 0x60,
                                      0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02,
                                      0x03, 0x05, 0x00, 0x04, 0x40};
        p8 modulus[BOWL_KEY_BYTES];
        p8 sig[BOWL_SIGNATURE_BYTES];
        p8 padded[BOWL_KEY_BYTES];
        p8 hex[41];
        p8 sum[64];
        p8 trailer[6];
        struct bowl_signature found;
        digest_state digest;
        positive modulus_length;
        positive sig_length = 0;
        positive sum_length;
        bipolar handle;
        bipolar got;
        bool whole;

        if (!bowl_key_fingerprint(key, hex) ||
            !string_equals((string_address)hex, key->fingerprint))
                return false;
        modulus_length = bowl_key_modulus(key, modulus);

        handle = system_open_at(AT_FDCWD, signature,
                                FILE_READ | O_NOFOLLOW | O_CLOEXEC);
        if (handle < 0)
                return false;
        while ((got = system_read_retry((positive)handle, sig + sig_length,
                                        sizeof(sig) - sig_length)) > 0)
                sig_length += (positive)got;
        system_close(handle);
        if (got < 0 || sig_length == sizeof(sig) ||
            !bowl_signature_read(sig, sig_length, key, address_of found) ||
            found.created < key->since || found.mpi_length > modulus_length)
                return false;

        handle = system_open_at(AT_FDCWD, archive,
                                FILE_READ | O_NOFOLLOW | O_CLOEXEC);
        if (handle < 0)
                return false;
        sum_length = found.hash == 10 ? 64 : 32;
        digest_open(address_of digest, found.hash == 10 ? DIGEST_SHA512 : DIGEST_SHA256,
                    sum_length);
        whole = bowl_digest_stream(handle, address_of digest, null);
        system_close(handle);
        if (!whole)
                return false;
        {
                p8 head[6] = {4, 0, 1, (p8)found.hash};

                network_store_16(head + 4, (p16)found.hashed_length);
                digest_write(address_of digest, head, 6);
                digest_write(address_of digest, found.hashed, found.hashed_length);
                trailer[0] = 4;
                trailer[1] = 0xff;
                network_store_32(trailer + 2, (p32)(6 + found.hashed_length));
                digest_write(address_of digest, trailer, 6);
        }
        digest_close(address_of digest, sum);
        if (sum[0] != found.left[0] || sum[1] != found.left[1])
                return false;

        memory_fill(padded, 0, sizeof(padded));
        memory_copy(padded + modulus_length - found.mpi_length, found.mpi,
                    found.mpi_length);
        return crypto_rsa_pkcs1(modulus, modulus_length, key->exponent, padded,
                                modulus_length,
                                found.hash == 10 ? info_512 : info_256,
                                found.hash == 10 ? sizeof(info_512)
                                                 : sizeof(info_256),
                                sum, sum_length);
}

/* Arch Linux ARM Build System <builder@archlinuxarm.org>, which signs every
   root tarball the mirrors carry. Taken from keyserver.ubuntu.com and from
   the archlinuxarm-keyring package's own archlinuxarm.gpg (GitHub, sha256
   6ce771e8...2332f) on 2026-09-30, where both gave this fingerprint. Its
   v4 fingerprint is what bowl_key_fingerprint makes of the numbers below.
   since is 2026-08-05, the signature on the build setup last measured;
   raise it with each release that is measured. */
static const struct bowl_key bowl_alarm_key = {
        "68B3537F39A313B3E574D06777193F152BDBE6A6", 1390085363, 65537,
        "cd0765c2849429950c1b01c97cc2486c3d93a8cc68715183be055105e6f2a3c5"
        "c373791e7b7238f5bde4b0e1a32612fd373240478bd2b2ca4db02949051ee4f1"
        "e030396719d3b7ed927047b31057261282ec45889d790df9b5859a163a636432"
        "db4786e9d04ba60a2f33bf1c10857975cb899166d0425fccf8a2d7605e4f1d05"
        "448926c1e026c991a1bccdac10c612be799cd6d3f3c07155c1b7f627446ff54e"
        "3aa4b6e2ba20349be4414751c26bbc0466d2588fc868b07459cddc30431809fe"
        "538c4c12f47b12bd43a675613a45689ba67690a7dfc20b63c78cfd509d76f2dd"
        "bd6d0389cd9b72243fc1eac29788ad22f785748628c9f7afc0ff4a297591f535"
        "5399e5974ca4d6116801f54ae00584bdc5c7ab2dc1c0361ad62ca28aa1b198d4"
        "7288aebc11995cdb4b868c6fc6b0d271fbfb40a964ce5bcb0926ba7e6064ffd3"
        "bb87da244f8af8d601eb608a1eb19666db92c90a9b885023eb28cd614e6d0ccf"
        "7bd47594d2df9dc197c71396924bc695b7595576273d229fe9d17e1fe9592e34"
        "af851ce74e5cfe8ccfcebbff5b9b37f492f26de9f388e1a47c65eb1e5c425fc6"
        "903405d6f16ab8cf52b3e3f5754bbbbe5b1573337912045b7ccb77d3a21c09f1"
        "7cb966fd60f0734cd88c923253457f295858c7b2aa98d943e73a60e20221fa7e"
        "773a94993ec2c25b1c1aff9cbcc3c1cb384a59441fcdea8726cc84928aa59497",
        1785933702};

/* ---- Just enough JSON for an OCI layout. ---- */

/*
        An OCI layout says what it holds in three small JSON documents: the
        index names a manifest, the manifest names the layers. What is read is
        a member of an object, the elements of an array and a string without
        anything but ASCII in it. A document is checked whole before anything
        is taken from it, so a walk that stops early has reached the end and
        not a fault; anything else is a refusal, never a guess.
*/
#define BOWL_JSON_DEPTH 32
#define BOWL_JSON_ROOM 65536

static string_address bowl_json_space(string_address at, string_address stop)
{
        while (at < stop &&
               (*at == ' ' || *at == '\t' || *at == '\n' || *at == '\r'))
                at++;
        return at;
}

// Past a string that starts at its quote, or null.
static string_address bowl_json_string_end(string_address at,
                                           string_address stop)
{
        if (at >= stop || *at != '"')
                return null;

        for (at++; at < stop; at++)
        {
                if (*at == '"')
                        return at + 1;
                if (*at < 0x20)
                        return null;
                if (*at != '\\')
                        continue;
                if (++at >= stop || !*at || !string_first_of("\"\\/bfnrtu", *at))
                        return null;
                if (*at == 'u')
                {
                        if (stop - at < 5)
                                return null;
                        for (positive digit = 1; digit <= 4; digit++)
                                if (!byte_is_hexadecimal(at[digit]))
                                        return null;
                        at += 4;
                }
        }

        return null;
}

// Past one value, or null when it is not one.
static string_address bowl_json_skip(string_address at, string_address stop,
                                     positive depth)
{
        at = bowl_json_space(at, stop);
        if (at >= stop || depth > BOWL_JSON_DEPTH)
                return null;

        if (*at == '"')
                return bowl_json_string_end(at, stop);

        if (*at == '{' || *at == '[')
        {
                bool object = *at == '{';
                p8 close = object ? '}' : ']';

                at = bowl_json_space(at + 1, stop);
                if (at < stop && *at == close)
                        return at + 1;

                while (at < stop)
                {
                        if (object)
                        {
                                at = bowl_json_string_end(at, stop);
                                if (!at)
                                        return null;
                                at = bowl_json_space(at, stop);
                                if (at >= stop || *at != ':')
                                        return null;
                                at++;
                        }

                        at = bowl_json_skip(at, stop, depth + 1);
                        if (!at)
                                return null;
                        at = bowl_json_space(at, stop);
                        if (at < stop && *at == close)
                                return at + 1;
                        if (at >= stop || *at != ',')
                                return null;
                        at = bowl_json_space(at + 1, stop);
                }

                return null;
        }

        static string_address words[] = {"true", "false", "null"};

        for (positive word = 0; word < array_count(words); word++)
        {
                positive length = string_length(words[word]);

                if ((positive)(stop - at) >= length &&
                    !memory_compare(at, words[word], length))
                        return at + length;
        }

        string_address from = at;

        while (at < stop && ((*at >= '0' && *at <= '9') || *at == '-' ||
                             *at == '+' || *at == '.' || *at == 'e' ||
                             *at == 'E'))
                at++;

        return at > from ? at : null;
}

// Whether text..stop is one value and nothing else.
static bool bowl_json_whole(string_address text, string_address stop)
{
        string_address after = bowl_json_skip(text, stop, 0);

        return after && bowl_json_space(after, stop) == stop;
}

// The value of a member of the object at at, or null when there is none.
static string_address bowl_json_member(string_address at, string_address stop,
                                       string_address key)
{
        positive length = string_length(key);

        at = bowl_json_space(at, stop);
        if (at >= stop || *at != '{')
                return null;
        at = bowl_json_space(at + 1, stop);

        while (at < stop && *at == '"')
        {
                string_address name_end = bowl_json_string_end(at, stop);
                string_address value;

                if (!name_end)
                        return null;
                value = bowl_json_space(name_end, stop);
                if (value >= stop || *value != ':')
                        return null;
                value = bowl_json_space(value + 1, stop);

                if ((positive)(name_end - at) == length + 2 &&
                    !memory_compare(at + 1, key, length))
                        return value;

                at = bowl_json_skip(value, stop, 1);
                if (!at)
                        return null;
                at = bowl_json_space(at, stop);
                if (at >= stop || *at != ',')
                        return null;
                at = bowl_json_space(at + 1, stop);
        }

        return null;
}

/* The next element of an array. The cursor starts at the array's '[' and
   moves past each element handed back; null is the end. */
static string_address bowl_json_next(string_address address_to cursor,
                                     string_address stop)
{
        string_address at = address_to cursor ? bowl_json_space(address_to cursor, stop)
                                              : stop;
        string_address element;

        if (at >= stop || (*at != '[' && *at != ','))
                return null;
        at = bowl_json_space(at + 1, stop);
        if (at < stop && *at == ']')
        {
                address_to cursor = null;
                return null;
        }

        element = at;
        at = bowl_json_skip(at, stop, 1);
        if (!at)
                return null;
        at = bowl_json_space(at, stop);
        address_to cursor = at < stop && *at == ',' ? at : null;
        return element;
}

/* A string value into out: printable ASCII only, which is all a media type,
   a digest or a platform name is. */
static bool bowl_json_text(string_address at, string_address stop,
                           p8 address_to out, positive room)
{
        positive n = 0;

        if (!at || at >= stop || *at != '"' || !room)
                return false;

        for (at++; at < stop && *at != '"'; at++)
        {
                p8 byte = *at;

                if (byte == '\\')
                {
                        if (++at >= stop)
                                return false;
                        byte = *at;
                        if (byte == 'u')
                        {
                                positive taken = 0;
                                positive value;

                                if (stop - at < 5)
                                        return false;
                                //      Exactly four, and the escape reader
                                //      stops at the first byte that is not a
                                //      digit, so a short count is a refusal.
                                value = string_digits_hexadecimal_escape_max(
                                    at + 1, 4, address_of taken);
                                if (taken != 4)
                                        return false;
                                at += 4;
                                byte = value < 0x80 ? (p8)value : 0;
                        }
                        else if (byte != '"' && byte != '\\' && byte != '/')
                                return false;
                }

                if (byte < 0x20 || byte >= 0x7f || n + 1 >= room)
                        return false;
                out[n++] = byte;
        }

        if (at >= stop)
                return false;
        out[n] = end;
        return true;
}

// A member that is a string, compared whole.
static bool bowl_json_says(string_address object, string_address stop,
                           string_address key, string_address want)
{
        p8 text[128];

        return bowl_json_text(bowl_json_member(object, stop, key), stop, text,
                              sizeof(text)) &&
               string_equals(text, want);
}

// A member that is a whole number, or false.
static bool bowl_json_count(string_address at, string_address stop,
                            p64 address_to value)
{
        p64 total = 0;
        string_address from = at;

        while (at && at < stop && *at >= '0' && *at <= '9' &&
               total < ((p64)1 << 58))
                total = total * 10 + (p64)(*at++ - '0');

        if (!at || at == from || (at < stop && *at >= '0' && *at <= '9'))
                return false;
        address_to value = total;
        return true;
}

/* ---- An OCI image layout, unpacked into a root. ---- */

/*
        Fedora publishes its base as an OCI image layout in a tar.xz, not as a
        root tarball: index.json names a manifest, the manifest names the
        layers, and each is a blob named by its own SHA-256. The layout is
        extracted beside the root (/bowls/NAME.oci), every blob read is checked
        against the digest and size that named it, and the layers go on in
        order, each extracted by itself and then merged down, which is where
        the whiteouts apply: .wh.NAME takes NAME away from the layers below,
        and .wh..wh..opq empties its directory of them first. A whiteout is
        never left in the tree.

        A hard link in a layer can only name a member of the same layer --
        tar refuses one naming anything it did not make -- so a layer that
        links to a file under it fails, and says so, instead of landing
        without the file.
*/
#define BOWL_OCI_DEPTH 64
#define BOWL_OCI_WHITEOUT ".wh."
#define BOWL_OCI_OPAQUE ".wh..wh..opq"

#if X64
#define BOWL_OCI_ARCHITECTURE "amd64"
#elif ARM64
#define BOWL_OCI_ARCHITECTURE "arm64"
#else
#define BOWL_OCI_ARCHITECTURE "riscv64"
#endif

static string_address bowl_oci_manifest_types[] = {
    "application/vnd.oci.image.manifest.v1+json",
    "application/vnd.docker.distribution.manifest.v2+json", null};
static string_address bowl_oci_index_types[] = {
    "application/vnd.oci.image.index.v1+json",
    "application/vnd.docker.distribution.manifest.list.v2+json", null};
static string_address bowl_oci_layer_types[] = {
    "application/vnd.oci.image.layer.v1.tar",
    "application/vnd.oci.image.layer.v1.tar+gzip",
    "application/vnd.oci.image.layer.v1.tar+zstd",
    "application/vnd.docker.image.rootfs.diff.tar.gzip", null};

static bool bowl_oci_type_is(string_address object, string_address stop,
                             string_address address_to types)
{
        p8 text[128];

        if (!bowl_json_text(bowl_json_member(object, stop, "mediaType"), stop,
                            text, sizeof(text)))
                return false;

        for (; *types; types++)
                if (string_equals(text, *types))
                        return true;

        return false;
}

/*
        The blob a descriptor names, checked: its digest must be sha256 and
        match what the file hashes to, and its size what the descriptor says.
        The name is opened beneath the layout without following a link, so a
        layout cannot point a blob at anything outside itself. path gets
        blobs/sha256/HEX under the layout.
*/
static b32 bowl_oci_blob(string_address layout, string_address descriptor,
                         string_address stop, p8 address_to path,
                         positive room)
{
        p8 digest[96];
        p8 hex[BOWL_DIGEST_HEX + 1];
        p8 rel[128];
        p64 want = 0;
        p64 size = 0;
        bipolar directory;
        bipolar handle;
        bool same;

        if (!bowl_json_text(bowl_json_member(descriptor, stop, "digest"), stop,
                            digest, sizeof(digest)) ||
            string_compare_max(digest, "sha256:", 7) ||
            !bowl_hex_digest(digest + 7, string_length(digest + 7)) ||
            !bowl_json_count(bowl_json_member(descriptor, stop, "size"), stop,
                             address_of want))
                return bowl_refuse("OCI layout names a blob by something other "
                                   "than its sha256 and size\n");

        path_join(rel, sizeof(rel), "blobs/sha256", digest + 7);
        if (!path_join(path, room, layout, rel))
                return bowl_refuse("bowl path is too long\n");

        directory = system_open_at(AT_FDCWD, layout,
                                   FILE_READ | O_DIRECTORY | O_NOFOLLOW |
                                       O_CLOEXEC);
        if (directory < 0)
                return bowl_fail(layout, directory);
        handle = system_open_resolved(directory, rel, FILE_READ | O_CLOEXEC,
                                      SYSTEM_RESOLVE_BENEATH |
                                          SYSTEM_RESOLVE_NO_SYMLINKS);
        system_close(directory);
        if (handle < 0)
                return bowl_fail(path, handle);

        same = bowl_sha256_of(handle, hex, address_of size) &&
               string_equals(hex, digest + 7) && size == want;
        system_close(handle);
        if (!same)
                return bowl_refuse("an OCI blob does not match its digest\n");

        return 0;
}

/* A JSON document into text, checked whole; the length is returned. */
static bipolar bowl_oci_json(string_address path, p8 address_to text,
                             positive room)
{
        bipolar handle = system_open_at(AT_FDCWD, path,
                                        FILE_READ | O_NOFOLLOW | O_CLOEXEC);
        bipolar got;

        if (handle < 0)
                return handle;
        got = system_read_retry((positive)handle, text, room);
        system_close(handle);
        if (got < 0)
                return got;
        if ((positive)got >= room ||
            !bowl_json_whole(text, text + got))
                return -ERROR_INVALID;
        return got;
}

// This machine's manifest in an index: one for linux on this architecture,
// or one that names no platform, which a single-architecture image does.
static string_address bowl_oci_pick(string_address index, string_address stop)
{
        string_address cursor = bowl_json_member(index, stop, "manifests");
        string_address entry;

        while ((entry = bowl_json_next(address_of cursor, stop)))
        {
                string_address platform = bowl_json_member(entry, stop, "platform");

                if (!bowl_oci_type_is(entry, stop, bowl_oci_manifest_types) &&
                    !bowl_oci_type_is(entry, stop, bowl_oci_index_types))
                        continue;
                if (!platform ||
                    (bowl_json_says(platform, stop, "os", "linux") &&
                     bowl_json_says(platform, stop, "architecture",
                                    BOWL_OCI_ARCHITECTURE)))
                        return entry;
        }

        return null;
}

static bipolar bowl_oci_remove_at(bipolar directory, string_address name)
{
        bipolar failed;

        if (file_is_directory(directory, name))
        {
                failed = bowl_reset_walk_at(directory, name, 0, false, null);
                if (!failed)
                        failed = system_remove_at(directory, name, AT_REMOVEDIR);
        }
        else
                failed = system_remove_at(directory, name, 0);

        return failed == -ERROR_NO_ENTRY ? 0 : failed;
}

static bool bowl_oci_whiteout(string_address name)
{
        return !string_compare_max(name, BOWL_OCI_WHITEOUT,
                                   sizeof(BOWL_OCI_WHITEOUT) - 1);
}

// Whiteouts out of a tree no lower layer is under.
static bipolar bowl_oci_strip(bipolar directory, positive depth)
{
        file_walk walk;
        struct linux_dirent64 address_to entry;
        bipolar failed = 0;

        if (depth > BOWL_OCI_DEPTH)
                return -ERROR_LOOP;
        if (!file_walk_open(address_of walk, directory, "."))
                return walk.error;

        while (!failed && (entry = file_walk_next(address_of walk)))
        {
                if (file_is_dot(entry->d_name))
                        continue;
                if (bowl_oci_whiteout(entry->d_name))
                        failed = bowl_oci_remove_at(walk.handle, entry->d_name);
                else if (file_is_directory(walk.handle, entry->d_name))
                {
                        bipolar below = system_open_at(walk.handle, entry->d_name,
                                                       FILE_READ | O_DIRECTORY |
                                                           O_NOFOLLOW | O_CLOEXEC);

                        failed = below < 0 ? below : bowl_oci_strip(below, depth + 1);
                        if (below >= 0)
                                system_close(below);
                }
        }

        if (!failed)
                failed = walk.error;
        file_walk_close(address_of walk);
        return failed;
}

/*
        One layer's tree, upper, merged down onto the tree below it. What the
        layer has goes in by rename, replacing what was there; a directory on
        both sides is merged into, taking the upper one's owner and mode.
        Entries leave upper as they are dealt with, so the walk goes round
        until a pass finds nothing left.
*/
static bipolar bowl_oci_merge(bipolar upper, bipolar lower, positive depth)
{
        bipolar failed = 0;
        bool moved = true;

        if (depth > BOWL_OCI_DEPTH)
                return -ERROR_LOOP;

        if (system_access_at(upper, BOWL_OCI_OPAQUE, 0) >= 0)
        {
                failed = bowl_reset_walk_at(lower, ".", 0, true, null);
                if (!failed)
                        failed = system_remove_at(upper, BOWL_OCI_OPAQUE, 0);
        }

        while (!failed && moved)
        {
                file_walk walk;
                struct linux_dirent64 address_to entry;

                moved = false;
                if (!file_walk_open(address_of walk, upper, "."))
                        return walk.error;

                while (!failed && (entry = file_walk_next(address_of walk)))
                {
                        string_address name = entry->d_name;

                        if (file_is_dot(name))
                                continue;
                        moved = true;

                        if (bowl_oci_whiteout(name))
                        {
                                string_address hidden = name + sizeof(BOWL_OCI_WHITEOUT) - 1;

                                // .wh..wh. is the whiteout tools' own bookkeeping.
                                if (string_compare_max(hidden, BOWL_OCI_WHITEOUT,
                                                       sizeof(BOWL_OCI_WHITEOUT) - 1))
                                {
                                        if (!hidden[0] || file_is_dot(hidden))
                                                failed = -ERROR_INVALID;
                                        else
                                                failed = bowl_oci_remove_at(lower, hidden);
                                }
                                if (!failed)
                                        failed = bowl_oci_remove_at(walk.handle, name);
                                continue;
                        }

                        if (file_is_directory(walk.handle, name) &&
                            file_is_directory(lower, name))
                        {
                                file_facts facts;
                                bipolar from = system_open_at(walk.handle, name,
                                    FILE_READ | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
                                bipolar onto = system_open_at(lower, name,
                                    FILE_READ | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);

                                failed = from < 0 ? from : onto < 0 ? onto :
                                    bowl_oci_merge(from, onto, depth + 1);
                                if (!failed &&
                                    !file_look(walk.handle, name,
                                               AT_SYMLINK_NOFOLLOW, address_of facts))
                                        failed = -ERROR_INPUT_OUTPUT;
                                if (!failed)
                                        failed = system_change_owner_at(onto, "",
                                            facts.owner, facts.group, AT_EMPTY_PATH);
                                if (!failed)
                                        failed = system_change_mode_at(lower, name,
                                                                       facts.mode & 07777);
                                if (from >= 0)
                                        system_close(from);
                                if (onto >= 0)
                                        system_close(onto);
                                if (!failed)
                                        failed = system_remove_at(walk.handle, name,
                                                                  AT_REMOVEDIR);
                                continue;
                        }

                        failed = bowl_oci_remove_at(lower, name);
                        if (!failed)
                                failed = system_rename_at(walk.handle, name, lower,
                                                          name, 0);
                        if (!failed && file_is_directory(lower, name))
                        {
                                bipolar below = system_open_at(lower, name,
                                    FILE_READ | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);

                                failed = below < 0 ? below : bowl_oci_strip(below, depth + 1);
                                if (below >= 0)
                                        system_close(below);
                        }
                }

                if (!failed)
                        failed = walk.error;
                file_walk_close(address_of walk);
        }

        return failed;
}

static b32 bowl_oci_layer(string_address blob, string_address layout,
                          string_address root)
{
        p8 upper[BOWL_PATH_LIMIT];
        bipolar from;
        bipolar onto;
        bipolar failed;

        if (!bowl_root_path(upper, sizeof(upper), layout, "/.layer"))
                return bowl_refuse("bowl path is too long\n");

        failed = bowl_forget_path(upper);
        if (!failed)
        {
                failed = bowl_mkdir(upper);
                if (failed < 0)
                        return bowl_fail(upper, failed);
        }
        if (!failed)
                failed = bowl_extract(blob, upper);
        if (failed)
                return failed;

        from = system_open_at(AT_FDCWD, upper,
                              FILE_READ | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        onto = system_open_at(AT_FDCWD, root,
                              FILE_READ | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        failed = from < 0 ? from : onto < 0 ? onto : bowl_oci_merge(from, onto, 0);
        if (from >= 0)
                system_close(from);
        if (onto >= 0)
                system_close(onto);
        if (failed)
                return bowl_fail(root, failed);

        return bowl_forget_path(upper);
}

static b32 bowl_extract_oci(string_address archive, string_address root)
{
        static p8 text[BOWL_JSON_ROOM];
        p8 layout[BOWL_PATH_LIMIT];
        p8 path[BOWL_PATH_LIMIT];
        string_address stop;
        string_address chosen;
        string_address cursor;
        string_address layer;
        positive layers = 0;
        bipolar got;
        b32 failed;

        if (!bowl_root_path(layout, sizeof(layout), root, ".oci") ||
            !path_join(path, sizeof(path), layout, "index.json"))
                return bowl_refuse("bowl path is too long\n");

        failed = bowl_forget_path(layout);
        if (!failed)
        {
                bipolar made = bowl_mkdir(layout);

                if (made < 0)
                        return bowl_fail(layout, made);
        }
        if (!failed)
                failed = bowl_extract(archive, layout);
        if (failed)
        {
                // Beside the root, where a reset of the root does not reach.
                bowl_forget_path(layout);
                return failed;
        }

        /*
                The archive is in the layout now, blob for blob, and every
                blob is checked before it is used; the download takes as much
                room again as the layers and is not needed for any of them.
        */
        system_remove_at(AT_FDCWD, archive, 0);

        got = bowl_oci_json(path, text, sizeof(text));
        if (got < 0)
        {
                bowl_forget_path(layout);
                return got == -ERROR_INVALID
                    ? bowl_refuse("archive is not an OCI image layout\n")
                    : bowl_fail(path, got);
        }
        stop = text + got;
        chosen = bowl_oci_pick(text, stop);

        //      An index may name an index, one per platform; one level is
        //      what registries write.
        for (positive nested = 0; !failed && chosen &&
                                  bowl_oci_type_is(chosen, stop, bowl_oci_index_types);
             nested++)
        {
                failed = nested ? bowl_refuse("OCI indexes nest too deeply\n")
                                : bowl_oci_blob(layout, chosen, stop, path,
                                                sizeof(path));
                if (!failed)
                {
                        got = bowl_oci_json(path, text, sizeof(text));
                        if (got < 0)
                                failed = bowl_fail(path, got);
                        else
                        {
                                stop = text + got;
                                chosen = bowl_oci_pick(text, stop);
                        }
                }
        }

        if (!failed && !chosen)
                failed = bowl_refuse("OCI layout has no image for linux/"
                                     BOWL_OCI_ARCHITECTURE "\n");
        if (!failed)
                failed = bowl_oci_blob(layout, chosen, stop, path, sizeof(path));
        if (!failed)
        {
                got = bowl_oci_json(path, text, sizeof(text));
                if (got < 0)
                        failed = bowl_fail(path, got);
                else
                        stop = text + got;
        }

        cursor = failed ? null : bowl_json_member(text, stop, "layers");
        while (!failed && (layer = bowl_json_next(address_of cursor, stop)))
        {
                if (!bowl_oci_type_is(layer, stop, bowl_oci_layer_types))
                        failed = bowl_refuse("an OCI layer is not a tar\n");
                if (!failed)
                        failed = bowl_oci_blob(layout, layer, stop, path,
                                               sizeof(path));
                if (!failed)
                        failed = bowl_oci_layer(path, layout, root);
                if (!failed)
                {
                        // Taken as it is used, so the room the layers
                        // need is the tree's and one blob's.
                        system_remove_at(AT_FDCWD, path, 0);
                        layers++;
                }
        }

        if (!failed && !layers)
                failed = bowl_refuse("OCI manifest has no layers\n");

        if (!failed)
                failed = bowl_forget_path(layout);
        else
                bowl_forget_path(layout);
        return failed;
}

/* ---- A Nix store as a bowl. ---- */

/*
        Nix is not a distribution root. Its binary release is a store: the
        closure of nix itself -- glibc, curl, busybox for the builder shell,
        CA certificates -- under nix-VERSION-SYSTEM/store, a .reginfo that
        registers those paths in the database, and an install script whose
        first lines name the nix and cacert paths to put in a profile.

        A Nix bowl is that store at /nix/store and nothing under it: every
        program Nix runs names its libraries and loader by /nix/store paths,
        so the root needs no /usr at all. It is single-user as root, the way
        the release's own installer sets up a machine without a daemon --
        Moonwater has no service manager to run one. Landing moves the store
        into place and keeps the two names as /nix/.bowl-seed; the database
        and profile come after, in the isolated view (bowl_prime_nix).
*/
#define BOWL_NIX_SEED "/nix/.bowl-seed"
#define BOWL_NIX_PATH 160

/* nix="/nix/store/HASH-NAME" as the release's install script spells it: a
   store path of 32 base-32 digits and a name, and nothing a shell or a path
   could read more into. */
static bool bowl_nix_store_path(string_address path, positive length)
{
        static const p8 prefix[] = "/nix/store/";
        positive at = sizeof(prefix) - 1;

        if (length >= BOWL_NIX_PATH || length < at + 34 ||
            memory_compare(path, prefix, at))
                return false;

        for (positive digit = 0; digit < 32; digit++, at++)
                if (!((path[at] >= '0' && path[at] <= '9') ||
                      (path[at] >= 'a' && path[at] <= 'z' && path[at] != 'e' &&
                       path[at] != 'o' && path[at] != 'u' && path[at] != 't')))
                        return false;
        if (path[at++] != '-')
                return false;

        for (; at < length; at++)
                if (!byte_is_alnum(path[at]) && path[at] != '-' &&
                    path[at] != '.' && path[at] != '_' && path[at] != '+')
                        return false;

        return true;
}

// The value of NAME="..." at the start of a line, when it is a store path.
static bool bowl_nix_assigned(string_address text, positive length,
                              string_address name, p8 address_to out)
{
        positive name_length = string_length(name);

        for (positive at = 0; at < length;)
        {
                positive stop = at + memory_span_without_byte(text + at, '\n',
                                                              length - at);

                if (stop - at > name_length + 3 &&
                    !memory_compare(text + at, name, name_length) &&
                    text[at + name_length] == '=' &&
                    text[at + name_length + 1] == '"' && text[stop - 1] == '"')
                {
                        positive from = at + name_length + 2;
                        positive size = stop - 1 - from;

                        if (!bowl_nix_store_path(text + from, size))
                                return false;
                        memory_copy(out, text + from, size);
                        out[size] = end;
                        return true;
                }

                at = stop + 1;
        }

        return false;
}

static bool bowl_nix_read_seed(string_address root, p8 address_to nix,
                               p8 address_to cacert)
{
        p8 path[BOWL_PATH_LIMIT];
        p8 text[2 * BOWL_NIX_PATH + 32];
        bipolar got;

        if (!bowl_root_path(path, sizeof(path), root, BOWL_NIX_SEED))
                return false;
        got = file_slurp(path, text, sizeof(text));
        if (got <= 0 || (positive)got >= sizeof(text))
                return false;

        return bowl_nix_assigned(text, (positive)got, "nix", nix) &&
               bowl_nix_assigned(text, (positive)got, "cacert", cacert);
}

static b32 bowl_extract_nix(string_address archive, string_address root)
{
        static p8 script[65536];
        static string_address keep[] = {"nix", null};
        p8 nix[BOWL_NIX_PATH];
        p8 cacert[BOWL_NIX_PATH];
        p8 seed[2 * BOWL_NIX_PATH + 32];
        p8 from[BOWL_PATH_LIMIT];
        p8 to[BOWL_PATH_LIMIT];
        p8 leaf[BOWL_PATH_LIMIT];
        byte_store out = {seed, sizeof(seed), 0};
        bipolar failed;
        bipolar parent;
        bipolar got;

        failed = bowl_extract(archive, root);
        if (!failed)
                failed = bowl_flatten(root, "/.reginfo");
        if (failed)
                return failed;

        //      tar makes a directory the archive does not list private, and
        //      the release lists neither its top directory, which is the
        //      root now, nor store/.
        failed = system_change_mode_at(AT_FDCWD, root, 0755);
        if (failed)
                return bowl_fail(root, failed);

        if (!bowl_root_path(from, sizeof(from), root, "/install"))
                return bowl_refuse("bowl path is too long\n");
        got = file_slurp(from, script, sizeof(script));
        if (got <= 0 || (positive)got >= sizeof(script) ||
            !bowl_nix_assigned(script, (positive)got, "nix", nix) ||
            !bowl_nix_assigned(script, (positive)got, "cacert", cacert))
                return bowl_refuse("archive is not a Nix binary release\n");

        if (!byte_store_append_exact(address_of out, "nix=\"", 5) ||
            !byte_store_append_exact(address_of out, nix, string_length(nix)) ||
            !byte_store_append_exact(address_of out, "\"\ncacert=\"", 10) ||
            !byte_store_append_exact(address_of out, cacert,
                                     string_length(cacert)) ||
            !byte_store_append_exact(address_of out, "\"\n", 2))
                return bowl_refuse("Nix store path is too long\n");

        if (!bowl_root_path(to, sizeof(to), root, "/nix"))
                return bowl_refuse("bowl path is too long\n");
        failed = bowl_mkdir(to);
        if (failed < 0)
                return bowl_fail(to, failed);

        if (!bowl_root_path(from, sizeof(from), root, "/store") ||
            !bowl_root_path(to, sizeof(to), root, "/nix/store"))
                return bowl_refuse("bowl path is too long\n");
        failed = system_rename_at(AT_FDCWD, from, AT_FDCWD, to, 0);
        if (!failed)
                failed = system_change_mode_at(AT_FDCWD, to, 0755);
        if (failed)
                return bowl_fail(to, failed);

        if (!bowl_root_path(to, sizeof(to), root, BOWL_NIX_SEED))
                return bowl_refuse("bowl path is too long\n");
        failed = bowl_write_bytes(to, seed, out.used);
        if (failed)
                return failed;

        // Last: /nix/.reginfo is the marker that the store is in place.
        if (!bowl_root_path(from, sizeof(from), root, "/.reginfo") ||
            !bowl_root_path(to, sizeof(to), root, "/nix/.reginfo"))
                return bowl_refuse("bowl path is too long\n");
        failed = system_rename_at(AT_FDCWD, from, AT_FDCWD, to, 0);
        if (failed)
                return bowl_fail(to, failed);

        // The installer scripts: this is what they would have done.
        parent = system_open_parent_nofollow(AT_FDCWD, root, false, 0, leaf,
                                             sizeof(leaf));
        if (parent < 0)
                return bowl_fail(root, parent);
        failed = bowl_reset_walk_at(parent, leaf, 0, false, keep);
        system_close(parent);
        return failed ? bowl_fail(root, failed) : 0;
}

/*
        What a single-user Nix needs around its store.

        nix.conf: no build users -- there is no nixbld group and the bowl is
        root -- and no sandbox: the isolated view is already a namespace of
        its own, and a build sandbox inside it needs user namespaces the
        Moonwater kernel may not have; substitutes from cache.nixos.org need
        neither. The new command and flakes are on, so `nix run nixpkgs#hello`
        works as the Nix manual writes it. A file that is already there is the
        person's and is left alone, as are passwd and group, which Nix asks
        for its user's name and home.

        The channel is subscribed as the installer does, not fetched: nixpkgs
        unpacks to half a gigabyte in a store a live session keeps in memory,
        and flakes need no channel at all.
*/
static b32 bowl_write_nix(string_address root)
{
        static const struct
        {
                string_address path;
                string_address text;
        } files[] = {
            {"/etc/nix/nix.conf",
             "build-users-group =\n"
             "sandbox = false\n"
             "experimental-features = nix-command flakes\n"},
            {"/etc/passwd", "root:x:0:0:root:/root:/bin/sh\n"
                            "nobody:x:65534:65534:nobody:/var/empty:/bin/false\n"},
            {"/etc/group", "root:x:0:\nnogroup:x:65534:\n"},
            {"/root/.nix-channels",
             "https://channels.nixos.org/nixpkgs-unstable nixpkgs\n"},
        };
        static string_address directories[] = {"/etc/nix", "/root", null};
        p8 path[BOWL_PATH_LIMIT];
        bipolar failed;

        for (positive at = 0; directories[at]; at++)
        {
                if (!bowl_root_path(path, sizeof(path), root, directories[at]))
                        return bowl_refuse("bowl path is too long\n");
                failed = bowl_mkdir(path);
                if (failed < 0)
                        return bowl_fail(path, failed);
        }

        for (positive at = 0; at < array_count(files); at++)
        {
                if (bowl_has(root, files[at].path))
                        continue;
                if (!bowl_root_path(path, sizeof(path), root, files[at].path))
                        return bowl_refuse("bowl path is too long\n");
                failed = bowl_write_bytes(path, files[at].text,
                                          string_length(files[at].text));
                if (failed)
                        return failed;
        }

        return 0;
}

/*
        unpack puts the archive's tree under root: bowl_extract for a root
        tarball, or a distribution's own way of unpacking one that is not.
*/
static b32 bowl_land(string_address archive, string_address root,
                     string_address marker,
                     b32(address_to unpack)(string_address, string_address))
{
        bipolar failed;

        if (!bowl_named_root(root) || !archive || archive[0] != '/' ||
            !marker || marker[0] != '/')
                return bowl_refuse("setup needs an archive path and /bowls/NAME\n");

        failed = system_access_at(AT_FDCWD, archive, 0);
        if (failed < 0)
                return bowl_fail(archive, failed);

        failed = bowl_mkdir(BOWL_ROOT_DIRECTORY);
        if (!failed)
                failed = bowl_mkdir(root);
        if (failed < 0)
                return bowl_fail(root, failed);

        failed = bowl_recover_from(root, marker);
        if (failed)
                return failed;

        if (!bowl_has(root, marker))
        {
                if (bowl_root_busy(root))
                {
                        p8 rel[258];

                        failed = bowl_prefix_find(root, marker, rel) > 0
                                     ? bowl_flatten(root, marker)
                                     : bowl_reset_root(root);
                        if (failed)
                                return failed;
                }

                if (!bowl_has(root, marker))
                {
                        failed = (unpack ? unpack : bowl_extract)(archive,
                                                                  root);
                        if (!failed)
                                failed = bowl_flatten(root, marker);
                        if (failed)
                        {
                                // Asked before the half-landed tree is taken
                                // away, while a full filesystem is still full.
                                bowl_room_say_low(BOWL_ROOT_DIRECTORY);
                                bowl_reset_root(root);
                                return failed;
                        }
                }

                if (!bowl_has(root, marker))
                        return bowl_refuse("archive is not a bowl bootstrap\n");
        }

        return bowl_configure(root);
}

/* ---- The built-in first boots, one pipeline per named distribution. ---- */

/*
        Built-in first boots.

        Every named setup shares one pipeline: become root, install /bowl,
        download if the marker is missing, land (extract by magic, flatten
        by marker), configure whatever tree that archive actually contains,
        then expose the manager on PATH. Pacman, apt and apk stay guest
        binaries. Distros differ by URL, floor, marker and a
        small prime step.
*/

/*
        Where each first boot comes from, per machine: every binary is built
        for one, and a bowl is that machine's own distribution. All five have
        an upstream on all three.

        Arch itself builds x86_64 only. arm64 is Arch Linux ARM, the port
        whose root tarball carries its own keyring and mirror list; its
        download host has no TLS, so the file comes from one of its mirrors
        that speaks TLS 1.3, which is all this wget speaks: de3, the nearest
        to most of Europe, answers only TLS 1.2. riscv64 is Arch Linux RISC-V, which Arch's own
        riscv.mirror.pkgbuild.com serves. Fedora builds riscv64 in its alt/
        tree beside the primary architectures. Nix publishes all three from
        the one release.

        Every download is checked against something this file carries, and
        a row that carries nothing is refused. Five kinds of row, each named
        by where its bytes come from:
          Alpine, Fedora, Nix: the SHA-256 of one release, from that
          release's own word for it (Alpine's .sha256 beside each tarball,
          Fedora's CHECKSUM file, which Fedora signs, and the riscv64 image's
          .sha256, the hash lines of the installer at
          releases.nixos.org/nix/nix-2.35.2/install).
          Arch, x86_64: archive.archlinux.org/iso/2026.09.01, a dated copy
          that never changes, SHA-256 from its sha256sums.txt and checked
          against Pierre Schmitz's signature (3E80CA1A...9A5C, Ed25519) with
          gpg on 2026-09-30.
          Arch Linux RISC-V: its dated archriscv-2026-08-27, SHA-256 taken
          from the download (the mirror publishes none).
          Debian: the file at one commit of docker-debian-artifacts, which
          is a content address (the branch the old URL followed moves daily),
          with the SHA-256 of what that commit holds.
          Arch Linux ARM: only ever a latest, with no dated copy to name, so
          a signature: its build system's RSA key, pinned as numbers and as a
          fingerprint, with a floor on the signature's date (bowl_alarm_key).
        To move to a newer release, change its lines and nothing else: take
        the new URL (a newer date for Arch x86_64 and RISC-V, the new commit
        from `git ls-remote` on the dist-ARCH branch for Debian, a newer
        release for the others), download it with the wget this builds,
        record its SHA-256 and its size (BOWL_*_BYTES, the first figure), and
        check the unpacked tree against the second; for Arch Linux ARM raise
        bowl_alarm_key's since to the new signature's creation time. A
        commit GitHub has stopped serving, or a dated copy a mirror has
        dropped, is a refused download and a reason to move.

        The sizes are what each download and its unpacked tree took on a
        4 KiB-page tmpfs, with nothing added, so a setup that fits is never
        refused. x86_64 on 2026-09-15 and 2026-09-21, arm64 and riscv64 on
        2026-09-22. Fedora's tree counts its one layer, which is on the
        filesystem while it is unpacked and which the download does not
        outlive; Nix's counts its database and profile. Arch Linux ARM's
        root tarball is a bootable system, kernel and firmware included,
        which is why it is so much larger than Arch's bootstrap. A release
        that has grown since is what the check after the download is for,
        and a failure part way still says how much room is left.
*/
#if X64
#define BOWL_ARCH_LABEL "Arch"
#define BOWL_ARCH_URL \
        "https://archive.archlinux.org/iso/2026.09.01/archlinux-bootstrap-2026.09.01-x86_64.tar.zst"
#define BOWL_ARCH_SHA256 \
        "895661bdf6c64e91b7725874165fd05dd30c438d3ffec661671ab5cfb261ca58"
#define BOWL_ARCH_KEY null
#define BOWL_ARCH_STORE "archlinux-bootstrap-x86_64.tar.zst"
#define BOWL_ARCH_BYTES 126491574, 607944704
#define BOWL_ALPINE_URL \
        "https://dl-cdn.alpinelinux.org/alpine/latest-stable/releases/x86_64/alpine-minirootfs-3.24.1-x86_64.tar.gz"
#define BOWL_ALPINE_SHA256 \
        "41f73e3cf5fa919b8aa5ca6b30dc48f0da2720776d7423e2a7748211456fe081"
#define BOWL_ALPINE_STORE "alpine-minirootfs-x86_64.tar.gz"
#define BOWL_ALPINE_BYTES 3698422, 8675328
#define BOWL_DEBIAN_URL \
        "https://github.com/debuerreotype/docker-debian-artifacts/raw/8f962b15d7884a90e17876a9303cbac909d119aa/stable/oci/blobs/rootfs.tar.gz"
#define BOWL_DEBIAN_SHA256 \
        "5b2f4c89fd582d08d15b8ee070cd70b636281bd0f1948889bd090fdfd6887e52"
#define BOWL_DEBIAN_STORE "debian-rootfs-amd64.tar.gz"
#define BOWL_DEBIAN_BYTES 49379699, 130928640
#define BOWL_FEDORA_URL \
        "https://dl.fedoraproject.org/pub/fedora/linux/releases/44/Container/x86_64/images/Fedora-Container-Base-Generic-44-1.7.x86_64.oci.tar.xz"
#define BOWL_FEDORA_SHA256 \
        "75200f5752a74a21a616ca9a75e25beb594e2e117a0195c54f87c0b3e3974d1b"
#define BOWL_FEDORA_STORE "fedora-container-x86_64.oci.tar.xz"
#define BOWL_FEDORA_BYTES 70170200, 261955584
#define BOWL_NIX_URL \
        "https://releases.nixos.org/nix/nix-2.35.2/nix-2.35.2-x86_64-linux.tar.xz"
#define BOWL_NIX_SHA256 \
        "0c3960a9792331a22081c3c7a5d8465db9b17c50b3acdf18587fa4c6f2cb1158"
#define BOWL_NIX_STORE "nix-x86_64-linux.tar.xz"
#define BOWL_NIX_BYTES 27131728, 125870080
#elif ARM64
#define BOWL_ARCH_LABEL "Arch Linux ARM"
#define BOWL_ARCH_URL \
        "https://fl.us.mirror.archlinuxarm.org/os/ArchLinuxARM-aarch64-latest.tar.gz"
#define BOWL_ARCH_SHA256 null
#define BOWL_ARCH_KEY address_of bowl_alarm_key
#define BOWL_ARCH_STORE "archlinuxarm-aarch64.tar.gz"
#define BOWL_ARCH_BYTES 829367415, 2193645568
#define BOWL_ALPINE_URL \
        "https://dl-cdn.alpinelinux.org/alpine/latest-stable/releases/aarch64/alpine-minirootfs-3.24.1-aarch64.tar.gz"
#define BOWL_ALPINE_SHA256 \
        "f55a90f69052c5bd6f92cb09a8f47065970830b194c917a006fb94028e721259"
#define BOWL_ALPINE_STORE "alpine-minirootfs-aarch64.tar.gz"
#define BOWL_ALPINE_BYTES 4023732, 8912896
#define BOWL_DEBIAN_URL \
        "https://github.com/debuerreotype/docker-debian-artifacts/raw/ca011a8b1c3b259e4cbbf83bf6841f1fd5f497c1/stable/oci/blobs/rootfs.tar.gz"
#define BOWL_DEBIAN_SHA256 \
        "6b7b02a048d52062eea469add2cba2aff1c9de92848af7dd77d44d20e3072846"
#define BOWL_DEBIAN_STORE "debian-rootfs-arm64.tar.gz"
#define BOWL_DEBIAN_BYTES 49748834, 153010176
#define BOWL_FEDORA_URL \
        "https://dl.fedoraproject.org/pub/fedora/linux/releases/44/Container/aarch64/images/Fedora-Container-Base-Generic-44-1.7.aarch64.oci.tar.xz"
#define BOWL_FEDORA_SHA256 \
        "eca19542a48a8e39b84e869713a1fa2408cbcc578de26c25ae72e3334ef968c1"
#define BOWL_FEDORA_STORE "fedora-container-aarch64.oci.tar.xz"
#define BOWL_FEDORA_BYTES 66049080, 267513856
#define BOWL_NIX_URL \
        "https://releases.nixos.org/nix/nix-2.35.2/nix-2.35.2-aarch64-linux.tar.xz"
#define BOWL_NIX_SHA256 \
        "4d0302a2910f5eec1c33b8deef634f04899a75737e7001ec49908d003ae5efda"
#define BOWL_NIX_STORE "nix-aarch64-linux.tar.xz"
#define BOWL_NIX_BYTES 25288932, 137592832
#elif RISCV64
#define BOWL_ARCH_LABEL "Arch Linux RISC-V"
#define BOWL_ARCH_URL \
        "https://archriscv.felixc.at/images/archriscv-2026-08-27.tar.zst"
#define BOWL_ARCH_SHA256 \
        "a2045c8b62232db2f60d8e4db610dbb5d9e12856dab0ba08634ad3d7cb7ad498"
#define BOWL_ARCH_KEY null
#define BOWL_ARCH_STORE "archriscv-riscv64.tar.zst"
#define BOWL_ARCH_BYTES 171783137, 745746432
#define BOWL_ALPINE_URL \
        "https://dl-cdn.alpinelinux.org/alpine/latest-stable/releases/riscv64/alpine-minirootfs-3.24.1-riscv64.tar.gz"
#define BOWL_ALPINE_SHA256 \
        "7201513262d851f39105102cf95519410100259bd7996fca13bade517838d7b7"
#define BOWL_ALPINE_STORE "alpine-minirootfs-riscv64.tar.gz"
#define BOWL_ALPINE_BYTES 3442892, 7393280
#define BOWL_DEBIAN_URL \
        "https://github.com/debuerreotype/docker-debian-artifacts/raw/53b98b5d28214ce21bc9801fd9c94101867d4517/stable/oci/blobs/rootfs.tar.gz"
#define BOWL_DEBIAN_SHA256 \
        "b940b8c443bae67bb23b7a64f960907cf55246e9cef2e9298d447a14b5afc091"
#define BOWL_DEBIAN_STORE "debian-rootfs-riscv64.tar.gz"
#define BOWL_DEBIAN_BYTES 47866994, 116793344
#define BOWL_FEDORA_URL \
        "https://dl.fedoraproject.org/pub/alt/risc-v/release/44/Container/riscv64/images/Fedora-Container-Base-Generic-44-20260604.0.riscv64.oci.tar.xz"
#define BOWL_FEDORA_SHA256 \
        "198c75fe6f58fea77e539fd29a3103407c0923833c7192c5e962a008c0595f31"
#define BOWL_FEDORA_STORE "fedora-container-riscv64.oci.tar.xz"
#define BOWL_FEDORA_BYTES 75309204, 269754368
#define BOWL_NIX_URL \
        "https://releases.nixos.org/nix/nix-2.35.2/nix-2.35.2-riscv64-linux.tar.xz"
#define BOWL_NIX_SHA256 \
        "98ee79540d4b9ccfe733655ea049a67af24f15860fbf92103bc1911bbd905a53"
#define BOWL_NIX_STORE "nix-riscv64-linux.tar.xz"
#define BOWL_NIX_BYTES 28914596, 138350592
#else
#error "bowl has no first boots for this architecture"
#endif
#define BOWL_INTERPRETER "/bowl"
#define BOWL_BIN_DIRECTORY "/bin"

#define BOWL_PRIME_NONE 0
#define BOWL_PRIME_ARCH 1
#define BOWL_PRIME_NIX 2

struct bowl_distro
{
        string_address name;
        string_address label;
        string_address root;
        string_address store;
        string_address url;
        string_address marker;
        string_address next;
        p64 floor;
        // What the download and the tree it unpacks to took, measured.
        p64 archive_bytes;
        p64 tree_bytes;
        p8 prime;
        string_address address_to expose;
        // The download's SHA-256 when the URL names one release, or null.
        string_address sha256;
        // How a download that is not a root tarball is unpacked, or null.
        b32(address_to unpack)(string_address archive, string_address root);
        // The key that signs a download no digest can name, or null. Every
        // row has a digest or a key; a row with neither is refused.
        const struct bowl_key address_to key;
        // The manager's own words for adding and dropping packages without
        // asking, which a profile appends its package names to; null when
        // profiles cannot use this distribution.
        string_address address_to install;
        string_address address_to remove;
};

static string_address bowl_sudo_places[] = {
    "/usr/bin/sudo", "/bin/sudo", "/usr/local/bin/sudo", null};
static string_address bowl_ln_places[] = {
    "/usr/bin/ln", "/bin/ln", null};

static string_address bowl_find_executable(string_address address_to places)
{
        for (; *places; places++)
                if (system_access_at(AT_FDCWD, *places, BOWL_ACCESS_EXECUTE) >=
                    0)
                        return *places;

        return null;
}

static bool bowl_is_root(void)
{
        return system_call_1(syscall(geteuid), 0) == 0;
}

static b32 bowl_setup_self(p8 address_to into, positive room);

static b32 bowl_setup_run(string_address path, string_address address_to argv,
                          string_address what)
{
        bipolar child;

        child = system_fork();
        if (child == 0)
        {
                system_execute(path, argv, file_environment_all());
                exit(127);
        }

        return bowl_wait_applet(child, what);
}

/* Whether an archive is what a row pins: its digest, or a signature from its
   key that is no older than the key's floor. A row that has neither is
   refused, and so is an archive whose signature file is missing. */
static bool bowl_archive_trusted(const struct bowl_distro address_to distro,
                                 string_address archive, string_address signature)
{
        if (distro->sha256)
                return bowl_archive_digest_ok(archive, distro->sha256);
        return distro->key && bowl_signature_ok(archive, signature, distro->key);
}

/* A download is checked only once it is whole, so until then the mirror
   decides how much of /bowls it fills, and /bowls is memory on a live
   session. The wget that fetches one is held to ceiling bytes by the kernel
   (RLIMIT_FSIZE) with SIGXFSZ ignored, so a write past it fails with EFBIG,
   wget reports it and abandons its unnamed stage, and the setup refuses the
   download with nothing left behind. */
static b32 bowl_setup_fetch(string_address self, string_address into,
                            string_address url, p64 ceiling)
{
        string_address argv[6];
        bipolar child;

        system_remove_at(AT_FDCWD, into, 0);

        argv[0] = "wget";
        argv[1] = "-q";
        argv[2] = "-O";
        argv[3] = into;
        argv[4] = url;
        argv[5] = null;

        child = system_fork();
        if (child == 0)
        {
                p64 limit[2] = {ceiling, ceiling};

                if (system_call_4(syscall(prlimit64), 0, 1, (positive)limit, 0) < 0 ||
                    !system_signal_install(SIGXFSZ, (positive)SIG_IGN, 0, 0, null))
                        exit(126);
                system_execute(self, argv, file_environment_all());
                exit(127);
        }

        return bowl_wait_applet(child, "download failed\n");
}

/* What a row's download may take: twice what it measured and 64 MiB more,
   so a release that has grown is never refused here (a digest row refuses
   any other bytes anyway) while a mirror that never stops stops there. A
   detached signature is a few hundred bytes. */
#define BOWL_SIGNATURE_CEILING ((p64)65536)

static p64 bowl_download_ceiling(const struct bowl_distro address_to distro)
{
        return 2 * distro->archive_bytes + 64 * BOWL_MEBIBYTE;
}

static b32 bowl_setup_download(const struct bowl_distro address_to distro)
{
        p8 self[BOWL_PATH_LIMIT];
        p8 part[BOWL_PATH_LIMIT];
        p8 sign[BOWL_PATH_LIMIT];
        p8 sign_part[BOWL_PATH_LIMIT];
        p8 sign_url[BOWL_PATH_LIMIT];
        string_address dest = distro->store;

        string_format(log, bowl_label "downloading %s\n", distro->url);
        log_flush();

        if (!distro->sha256 && !distro->key)
                return bowl_refuse("this download names no digest and no key "
                                   "to check it against\n");

        if (!bowl_root_path(part, sizeof(part), dest, ".part") ||
            !bowl_root_path(sign, sizeof(sign), dest, ".sig") ||
            !bowl_root_path(sign_part, sizeof(sign_part), dest, ".sig.part") ||
            !bowl_root_path(sign_url, sizeof(sign_url), distro->url, ".sig"))
                return bowl_refuse("bowl path is too long\n");

        if (bowl_setup_self(self, sizeof(self)))
                return 1;

        if (bowl_setup_fetch(self, part, distro->url,
                             bowl_download_ceiling(distro)))
        {
                // Asked while the part that filled it is still there.
                bowl_room_say_low(BOWL_ROOT_DIRECTORY);
                system_remove_at(AT_FDCWD, part, 0);
                return 1;
        }

        if (distro->key && !distro->sha256 &&
            bowl_setup_fetch(self, sign_part, sign_url, BOWL_SIGNATURE_CEILING))
        {
                system_remove_at(AT_FDCWD, part, 0);
                system_remove_at(AT_FDCWD, sign_part, 0);
                return 1;
        }

        if (!bowl_archive_usable(part, distro->floor))
        {
                system_remove_at(AT_FDCWD, part, 0);
                system_remove_at(AT_FDCWD, sign_part, 0);
                return bowl_refuse("download was not a bootstrap archive\n");
        }

        if (!bowl_archive_trusted(distro, part, sign_part))
        {
                system_remove_at(AT_FDCWD, part, 0);
                system_remove_at(AT_FDCWD, sign_part, 0);
                return bowl_refuse(distro->sha256
                                       ? "download is not the release setup pins: "
                                         "its SHA-256 differs\n"
                                       : "download is not signed by the key setup "
                                         "pins, or by it before the floor\n");
        }

        system_remove_at(AT_FDCWD, dest, 0);
        system_remove_at(AT_FDCWD, sign, 0);
        if (system_rename_at(AT_FDCWD, part, AT_FDCWD, dest, 0) < 0 ||
            (!distro->sha256 &&
             system_rename_at(AT_FDCWD, sign_part, AT_FDCWD, sign, 0) < 0))
        {
                system_remove_at(AT_FDCWD, part, 0);
                system_remove_at(AT_FDCWD, sign_part, 0);
                return bowl_refuse("could not keep the bootstrap archive\n");
        }

        return 0;
}

/* input, when there is one, is a file in the bowl the program reads as its
   standard input. */
static b32 bowl_setup_isolated(string_address root, string_address program,
                               string_address address_to arguments,
                               string_address what, string_address input)
{
        bipolar child;

        log_flush();
        {
                b32 failed = bowl_configure(root);

                if (failed)
                        return failed;
        }
        child = system_fork();
        if (child == 0)
        {
                if (input)
                {
                        bipolar handle = bowl_open_in_root(root, input,
                                                           FILE_READ | O_CLOEXEC);

                        if (handle < 0)
                                exit(bowl_fail(input, handle));
                        if (system_descriptor_install(handle, 0) < 0)
                                exit(bowl_fail(input, -ERROR_INPUT_OUTPUT));
                        if (handle)
                                system_close(handle);
                }
                exit(bowl_launch(root, program, arguments, true));
        }

        return bowl_wait_applet(child, what);
}

static b32 bowl_setup_self(p8 address_to into, positive room)
{
        bipolar got = file_link_text("/proc/self/exe", into, room);

        if (got < 0)
                return bowl_fail("/proc/self/exe", got);

        return 0;
}

static b32 bowl_setup_bind_via_sudo(string_address self)
{
        string_address sudo = bowl_find_executable(bowl_sudo_places);
        string_address ln = bowl_find_executable(bowl_ln_places);
        string_address argv[8];

        if (!sudo || !ln)
                return bowl_refuse("setup needs to write /bowls\n");

        argv[0] = sudo;
        argv[1] = "-n";
        argv[2] = ln;
        argv[3] = "-sfn";
        argv[4] = self;
        argv[5] = BOWL_INTERPRETER;
        argv[6] = null;

        return bowl_setup_run(sudo, argv, "cannot install /bowl\n");
}

static b32 bowl_setup_bind_interpreter(void)
{
        p8 self[BOWL_PATH_LIMIT];
        p8 current[BOWL_PATH_LIMIT];
        bipolar got;
        bipolar failed;

        if (bowl_setup_self(self, sizeof(self)))
                return 1;

        got = file_link_text(BOWL_INTERPRETER, current, sizeof(current));
        if (got >= 0 && string_equals(current, self))
                return 0;

        /*
                A regular file at /bowl already satisfies #!/bowl. Leave it.
                A dangling or differently-aimed symlink is replaced so this
                binary is the one later shebangs invoke.
        */
        if (got < 0 &&
            system_access_at(AT_FDCWD, BOWL_INTERPRETER, BOWL_ACCESS_EXECUTE) >=
                0)
                return 0;

        system_remove_at(AT_FDCWD, BOWL_INTERPRETER, 0);
        failed = system_symbolic_link_at(self, AT_FDCWD, BOWL_INTERPRETER);
        if (failed >= 0 || failed == -EEXIST)
                return 0;

        if (failed != -ERROR_NOT_PERMITTED && failed != -ERROR_ACCESS)
                return bowl_fail(BOWL_INTERPRETER, failed);

        return bowl_setup_bind_via_sudo(self);
}

static DEAD_END fn bowl_setup_reexec_root(string_address verb,
                                          string_address name,
                                          string_address action)
{
        string_address sudo = bowl_find_executable(bowl_sudo_places);
        string_address argv[7];

        if (!sudo)
        {
                bowl_refuse("setup needs to write /bowls\n");
                exit(1);
        }

        argv[0] = sudo;
        argv[1] = "-n";
        argv[2] = BOWL_INTERPRETER;
        argv[3] = verb;
        argv[4] = name;
        argv[5] = action;
        argv[6] = null;

        system_execute(sudo, argv, file_environment_all());
        bowl_refuse("cannot obtain root\n");
        exit(1);
}

static b32 bowl_setup_become_root(string_address verb, string_address name,
                                  string_address action)
{
        if (bowl_setup_bind_interpreter())
                return 1;

        if (bowl_is_root())
                return 0;

        bowl_setup_reexec_root(verb, name, action);
        return 1;
}

static b32 bowl_publish_bin(string_address name)
{
        p8 from[BOWL_PATH_LIMIT];
        p8 to[BOWL_PATH_LIMIT];
        bipolar failed;

        if (sizeof(BOWL_EXPOSE_DIRECTORY) + string_length(name) >=
                sizeof(from) ||
            sizeof(BOWL_BIN_DIRECTORY) + string_length(name) >= sizeof(to))
                return bowl_refuse("exposed path is too long\n");

        path_join(from, sizeof(from), BOWL_EXPOSE_DIRECTORY, name);
        path_join(to, sizeof(to), BOWL_BIN_DIRECTORY, name);

        if (system_access_at(AT_FDCWD, to, 0) >= 0)
                return 0;

        failed = system_symbolic_link_at(from, AT_FDCWD, to);
        if (failed < 0 && failed != -EEXIST)
                return bowl_fail(to, failed);

        return 0;
}

static b32 bowl_prime_arch(string_address root)
{
        string_address init_argv[] = {"/usr/bin/pacman-key", "--init", null};
        // Every keyring the tree ships: archlinux, and archlinuxarm beside
        // it on Arch Linux ARM.
        string_address populate_argv[] = {"/usr/bin/pacman-key", "--populate",
                                          null};
        b32 failed;

        if (bowl_has(root, "/etc/pacman.d/gnupg/pubring.gpg") ||
            bowl_has(root, "/etc/pacman.d/gnupg/pubring.kbx"))
                return 0;

        string_format(log, bowl_label "initialising the keyring\n");
        log_flush();
        failed = bowl_setup_isolated(root, "/usr/bin/pacman-key", init_argv,
                                     "pacman-key --init failed\n", null);
        if (!failed)
                failed = bowl_setup_isolated(root, "/usr/bin/pacman-key",
                                             populate_argv,
                                             "pacman-key --populate failed\n",
                                             null);
        return failed;
}

/*
        The two steps of the release's installer that run Nix: register the
        store's paths in the database from .reginfo, then put nix and the CA
        certificates in the default profile, which is where every exposed
        command and Nix's own TLS look. Each is skipped once it has been done,
        so a setup that stopped part way picks up where it was.
*/
static b32 bowl_prime_nix(string_address root)
{
        p8 nix[BOWL_NIX_PATH];
        p8 cacert[BOWL_NIX_PATH];
        p8 program[BOWL_NIX_PATH + 32];
        b32 failed = 0;

        if (!bowl_nix_read_seed(root, nix, cacert))
                return bowl_refuse("the Nix store has no seed; remove "
                                   BOWL_ROOT_PREFIX "nix and set it up again\n");

        if (!bowl_has(root, "/nix/var/nix/db/db.sqlite"))
        {
                string_address argv[] = {program, "--load-db", null};

                path_join(program, sizeof(program), nix, "bin/nix-store");
                string_format(log, bowl_label "registering the store\n");
                log_flush();
                failed = bowl_setup_isolated(root, program, argv,
                                             "nix-store --load-db failed\n",
                                             "/nix/.reginfo");
        }

        if (!failed && !bowl_has(root, "/nix/var/nix/profiles/default/bin/nix"))
        {
                string_address argv[] = {program, "-i", nix, cacert, null};

                path_join(program, sizeof(program), nix, "bin/nix-env");
                string_format(log, bowl_label "installing Nix in its profile\n");
                log_flush();
                failed = bowl_setup_isolated(root, program, argv,
                                             "nix-env -i failed\n", null);
        }

        return failed;
}

static b32 bowl_setup_publish(string_address root, string_address program)
{
        p8 name[256];
        b32 failed;

        path_tail_copy(name, sizeof(name), program);
        failed = bowl_expose_program(root, program, null, false);
        if (!failed)
                failed = bowl_publish_bin(name);
        return failed;
}

static string_address bowl_arch_expose[] = {
    "/usr/bin/pacman", "/usr/bin/pacman-key", null};
static string_address bowl_alpine_expose[] = {"/sbin/apk", null};
static string_address bowl_debian_expose[] = {
    "/usr/bin/apt-get", "/usr/bin/apt", null};
static string_address bowl_fedora_expose[] = {"/usr/bin/dnf", null};
static string_address bowl_nix_expose[] = {
    "/nix/var/nix/profiles/default/bin/nix",
    "/nix/var/nix/profiles/default/bin/nix-env",
    "/nix/var/nix/profiles/default/bin/nix-channel",
    "/nix/var/nix/profiles/default/bin/nix-shell",
    "/nix/var/nix/profiles/default/bin/nix-build",
    "/nix/var/nix/profiles/default/bin/nix-store", null};

static string_address bowl_arch_install[] = {
    "/usr/bin/pacman", "-Syu", "--needed", "--noconfirm", null};
static string_address bowl_arch_remove[] = {
    "/usr/bin/pacman", "-Rns", "--noconfirm", null};
static string_address bowl_alpine_install[] = {
    "/sbin/apk", "add", "--no-cache", null};
static string_address bowl_alpine_remove[] = {"/sbin/apk", "del", null};

static const struct bowl_distro bowl_distros[] = {
    {"arch", BOWL_ARCH_LABEL, BOWL_ROOT_PREFIX "arch",
     BOWL_ROOT_PREFIX BOWL_ARCH_STORE, BOWL_ARCH_URL,
     "/usr/bin/pacman", "pacman -Syu", (p64)32 * 1024 * 1024,
     BOWL_ARCH_BYTES, BOWL_PRIME_ARCH, bowl_arch_expose, BOWL_ARCH_SHA256, null, BOWL_ARCH_KEY,
     bowl_arch_install, bowl_arch_remove},
    {"alpine", "Alpine", BOWL_ROOT_PREFIX "alpine",
     BOWL_ROOT_PREFIX BOWL_ALPINE_STORE, BOWL_ALPINE_URL, "/sbin/apk",
     "apk update", (p64)1024 * 1024, BOWL_ALPINE_BYTES, BOWL_PRIME_NONE,
     bowl_alpine_expose, BOWL_ALPINE_SHA256, null, null, bowl_alpine_install,
     bowl_alpine_remove},
    {"debian", "Debian", BOWL_ROOT_PREFIX "debian",
     BOWL_ROOT_PREFIX BOWL_DEBIAN_STORE, BOWL_DEBIAN_URL, "/usr/bin/apt-get",
     "apt-get update", (p64)8 * 1024 * 1024, BOWL_DEBIAN_BYTES,
     BOWL_PRIME_NONE, bowl_debian_expose, BOWL_DEBIAN_SHA256, null},
    {"fedora", "Fedora", BOWL_ROOT_PREFIX "fedora",
     BOWL_ROOT_PREFIX BOWL_FEDORA_STORE, BOWL_FEDORA_URL,
     "/usr/bin/dnf", "dnf makecache", (p64)32 * 1024 * 1024,
     BOWL_FEDORA_BYTES, BOWL_PRIME_NONE, bowl_fedora_expose,
     BOWL_FEDORA_SHA256, bowl_extract_oci},
    {"nix", "Nix", BOWL_ROOT_PREFIX "nix",
     BOWL_ROOT_PREFIX BOWL_NIX_STORE, BOWL_NIX_URL, "/nix/.reginfo",
     "nix-channel --update, or nix run nixpkgs#hello",
     (p64)8 * 1024 * 1024, BOWL_NIX_BYTES, BOWL_PRIME_NIX,
     bowl_nix_expose, BOWL_NIX_SHA256, bowl_extract_nix},
};

static const struct bowl_distro address_to bowl_find_distro(string_address name)
{
        positive at = string_table_find(name, bowl_distros,
                                        sizeof(bowl_distros[0]),
                                        array_count(bowl_distros));

        return at < array_count(bowl_distros) ? bowl_distros + at : null;
}

/*
        Refused before anything is written when the room is not there: the
        download and its tree are on the filesystem together until the download
        is removed. Which of the filesystem and memory is short is the one said.
*/
static b32 bowl_room_short(const struct bowl_distro address_to distro, p64 need)
{
        bowl_room room;

        if (!need || !bowl_room_at(BOWL_ROOT_DIRECTORY, address_of room))
                return 0;

        bool memory = room.in_memory && room.memory < room.free;
        p64 have = memory ? room.memory : room.free;

        if (have >= need)
                return 0;

        string_format(log, bowl_label "%s needs %p MiB in " BOWL_ROOT_DIRECTORY
                                      " and %s %p MiB %s\n",
                      distro->label,
                      (positive)((need + BOWL_MEBIBYTE - 1) / BOWL_MEBIBYTE),
                      memory ? "memory has" : "it has",
                      (positive)(have / BOWL_MEBIBYTE),
                      memory ? "available" : "free");
        bowl_room_hint(address_of room);
        log_flush();
        return 1;
}

static fn bowl_store_forget(const struct bowl_distro address_to distro)
{
        p8 sign[BOWL_PATH_LIMIT];

        system_remove_at(AT_FDCWD, distro->store, 0);
        if (bowl_root_path(sign, sizeof(sign), distro->store, ".sig"))
                system_remove_at(AT_FDCWD, sign, 0);
}

static b32 bowl_setup_distro(const struct bowl_distro address_to distro)
{
        b32 failed = 0;
        string_address address_to program;

        failed = bowl_mkdir(BOWL_ROOT_DIRECTORY);
        if (failed < 0)
                return bowl_fail(BOWL_ROOT_DIRECTORY, failed);

        if (!bowl_has(distro->root, distro->marker))
        {
                p8 sign[BOWL_PATH_LIMIT];
                bool kept = bowl_root_path(sign, sizeof(sign), distro->store,
                                           ".sig") &&
                            bowl_archive_usable(distro->store, distro->floor) &&
                            bowl_archive_trusted(distro, distro->store, sign);

                if (bowl_room_short(distro, distro->tree_bytes +
                                                (kept ? 0 : distro->archive_bytes)))
                        return 1;

                if (!kept)
                {
                        bowl_store_forget(distro);
                        failed = bowl_setup_download(distro);
                        if (failed)
                                return failed;

                        // The download is in place now and its tree is not.
                        if (bowl_room_short(distro, distro->tree_bytes))
                        {
                                bowl_store_forget(distro);
                                return 1;
                        }
                }

                string_format(log, bowl_label "landing %s at %s\n",
                              distro->label, distro->root);
                log_flush();
                failed = bowl_land(distro->store, distro->root, distro->marker,
                                   distro->unpack);
                if (failed)
                {
                        if (!bowl_has(distro->root, distro->marker))
                                bowl_store_forget(distro);
                        return failed;
                }

                bowl_store_forget(distro);

                /*
                        A keyring that arrives in the tarball was made where
                        the tarball was, master key and all, and is the same
                        for everyone who downloads it: Arch Linux RISC-V ships
                        one. Pacman trusts what that key signs, so it goes, and
                        the prime step makes this machine's own.
                */
                if (distro->prime == BOWL_PRIME_ARCH)
                {
                        p8 keyring[BOWL_PATH_LIMIT];

                        if (!bowl_root_path(keyring, sizeof(keyring), distro->root,
                                            "/etc/pacman.d/gnupg"))
                                return bowl_refuse("bowl path is too long\n");
                        failed = bowl_forget_path(keyring);
                        if (failed)
                                return failed;
                }
        }

        if (distro->prime == BOWL_PRIME_ARCH)
                failed = bowl_prime_arch(distro->root);
        else if (distro->prime == BOWL_PRIME_NIX)
                failed = bowl_prime_nix(distro->root);
        if (failed)
                return failed;

        program = distro->expose;
        while (program && *program)
        {
                failed = bowl_setup_publish(distro->root, *program);
                if (failed)
                        return failed;
                program++;
        }

        string_format(log, bowl_label "%s is ready. %s\n", distro->label,
                      distro->next);
        log_flush();
        return 0;
}

static b32 bowl_setup(positive count, string_address address_to arguments)
{
        const struct bowl_distro address_to distro;

        if (count != 3)
                return bowl_usage();

        distro = bowl_find_distro(arguments[2]);
        if (!distro)
                return bowl_refuse("known setups: arch alpine debian fedora nix\n");

        if (bowl_setup_become_root("setup", distro->name, null))
                return 1;

        return bowl_setup_distro(distro);
}


/* ---- Profiles: what a machine is for, as components from several bowls. ---- */

/*
        A profile is not a distribution. It is a set of components, and each
        component is the packages one bowl is best placed to supply: KDE from
        Alpine, where it runs on musl and its closure is small, Steam from
        Arch, where it finds the glibc it was built for. Every program runs
        against its own bowl's loader and libraries (the fast view binds them
        per launch), and they talk to each other over the host's /run, /tmp and
        /dev, which every fast view shares: a Wayland client from one bowl
        draws on a compositor from another.

        Install does the glue so a person never has to:
          - lands each component's bowl if it is missing and has its own
            manager add the packages;
          - makes every program the bowl holds reachable by name and by
            absolute path. Moonwater's /usr/bin is /bin, a guest that execs
            dbus-daemon or /usr/bin/kwin_wayland never asks the shell, and the
            bowl's own /usr/bin is not on any path. So each one gets a
            launcher in /bowls/bin and a link in /bin, except a name /bin
            already has, which stays Moonwater's;
          - writes the session script, a launcher named for the profile.
        What it exposed is written under the bowl, so remove undoes exactly
        that: launchers that still name this bowl, the packages the profile
        lists with the dependencies nothing else needs, the script.
*/

#define BOWL_PROFILE_ARGV 96
#define BOWL_PROFILE_STATE "/var/lib/moonwater/profile"

struct bowl_component
{
        string_address distro;
        string_address address_to packages;
        // What the packages and their dependencies took installed, measured on
        // this machine's own build; 0 where nobody has, which is not checked.
        p64 bytes;
};

struct bowl_profile
{
        string_address name;
        string_address label;
        string_address next;
        const struct bowl_component address_to components;
        // Lines of the session script, run by /bin/sh.
        string_address address_to session;
};

/*
        The desktop leaves out what Moonwater already is: no display manager
        (the script is the session), no NetworkManager applet, no Bluetooth
        or print stack, and no kernel, shell or coreutils, which the bowl's
        own would only shadow.
*/
static string_address bowl_desktop_alpine[] = {
    "plasma-desktop", "plasma-workspace", "kwin", "kscreen", "plasma-pa",
    "plasma-systemmonitor", "konsole", "dolphin", "spectacle", "breeze",
    "breeze-icons", "kde-gtk-config", "xdg-desktop-portal-kde",
    "qt6-qtwayland", "xwayland", "dbus", "elogind", "polkit-elogind",
    "pipewire", "pipewire-pulse", "wireplumber", "mesa-dri-gallium",
    "mesa-egl", "mesa-gbm", "font-noto", "font-dejavu", "xdg-user-dirs",
    null};

/* 1963 MiB in 628 packages, x86-64 Alpine 3.24 on 2026-10-01. */
#if X64
#define BOWL_DESKTOP_ALPINE_BYTES ((p64)1963 * 1024 * 1024)
#else
#define BOWL_DESKTOP_ALPINE_BYTES 0
#endif

static const struct bowl_component bowl_desktop_components[] = {
    {"alpine", bowl_desktop_alpine, BOWL_DESKTOP_ALPINE_BYTES},
    {null, null, 0},
};

static string_address bowl_desktop_session[] = {
    "export XDG_SESSION_TYPE=wayland XDG_CURRENT_DESKTOP=KDE",
    "export KDE_FULL_SESSION=true QT_QPA_PLATFORM=wayland",
    "exec dbus-run-session -- startplasma-wayland",
    null};

static const struct bowl_profile bowl_profiles[] = {
    {"desktop", "KDE Plasma", "start it with: desktop", bowl_desktop_components,
     bowl_desktop_session},
};

static const struct bowl_profile address_to bowl_find_profile(string_address name)
{
        positive at = string_table_find(name, bowl_profiles,
                                        sizeof(bowl_profiles[0]),
                                        array_count(bowl_profiles));

        return at < array_count(bowl_profiles) ? bowl_profiles + at : null;
}

/* Where a component's bowl keeps what this profile exposed there. */
static bool bowl_profile_marker(p8 address_to into, positive room,
                                string_address root, string_address name)
{
        p8 rel[BOWL_PATH_LIMIT];

        rel[0] = end;
        return path_join(rel, sizeof(rel), BOWL_PROFILE_STATE, name) &&
               bowl_root_path(into, room, root, rel);
}

static bool bowl_profile_header(p8 address_to into, positive room,
                                string_address name)
{
        static const p8 first[] = "#!/bin/sh\n# bowl profile ";
        positive length = string_length(name);

        if (sizeof(first) - 1 + length + 2 > room)
                return false;

        memory_copy(into, first, sizeof(first) - 1);
        memory_copy(into + sizeof(first) - 1, name, length);
        into[sizeof(first) - 1 + length] = '\n';
        into[sizeof(first) + length] = end;
        return true;
}

/* A name /bin has that is not this bowl system's own link to a launcher. */
static bool __attribute__((__noinline__)) bowl_bin_taken(string_address name)
{
        p8 to[BOWL_PATH_LIMIT];
        p8 from[BOWL_PATH_LIMIT];
        p8 current[BOWL_PATH_LIMIT];

        to[0] = from[0] = end;
        if (!path_join(to, sizeof(to), BOWL_BIN_DIRECTORY, name) ||
            !path_join(from, sizeof(from), BOWL_EXPOSE_DIRECTORY, name))
                return true;

        if (system_access_at(AT_FDCWD, to, 0) < 0)
                return false;

        return file_link_text(to, current, sizeof(current)) < 0 ||
               !string_equals(current, from);
}

/* Opened to add to, never to start over: an install run again finds every
   launcher already there, notes nothing, and would leave a truncated marker
   empty for remove to find. */
static bipolar bowl_profile_marker_open(string_address path)
{
        return system_open_at_mode(AT_FDCWD, path,
                                   FILE_APPEND | O_CLOEXEC | O_NOFOLLOW, 0644);
}

static fn bowl_profile_note(bipolar marker, string_address name)
{
        p8 line[BOWL_SHEBANG_LIMIT + 2];
        positive length = string_length(name);

        if (marker < 0 || length >= BOWL_SHEBANG_LIMIT)
                return;

        memory_copy(line, name, length);
        line[length] = '\n';
        system_write_all((positive)marker, line, length + 1);
}

/*
        Every program one directory of the bowl holds, exposed. A name that is
        already a launcher is left to whoever made it and is not recorded, so
        a program two profiles share is not taken from one by the other's
        remove.
*/
static positive __attribute__((__noinline__))
bowl_profile_expose_dir(string_address root, string_address dir, bipolar marker)
{
        file_walk walk;
        struct linux_dirent64 address_to entry;
        p8 where[BOWL_PATH_LIMIT];
        p8 rel[BOWL_PATH_LIMIT];
        p8 launcher[BOWL_PATH_LIMIT];
        positive made = 0;

        if (!bowl_root_path(where, sizeof(where), root, dir) ||
            !file_walk_open(address_of walk, AT_FDCWD, where))
                return 0;

        while ((entry = file_walk_next(address_of walk)))
        {
                string_address name = entry->d_name;
                bool exists;

                if (file_is_dot(name) || !bowl_name(name, true) ||
                    bowl_bin_taken(name) ||
                    !path_join(rel, sizeof(rel), dir, name) ||
                    !path_join(launcher, sizeof(launcher),
                               BOWL_EXPOSE_DIRECTORY, name) ||
                    bowl_executable_in_root(root, rel) < 0)
                        continue;

                exists = system_access_at(AT_FDCWD, launcher, 0) >= 0;
                if (!exists && bowl_expose_program(root, rel, name, false))
                        continue;

                if (bowl_publish_bin(name) || exists)
                        continue;

                bowl_profile_note(marker, name);
                made++;
        }

        file_walk_close(address_of walk);
        return made;
}

static string_address bowl_profile_bins[] = {"/usr/bin", "/usr/sbin", null};

static b32 bowl_profile_expose(string_address root, string_address name)
{
        p8 marker_path[BOWL_PATH_LIMIT];
        p8 state[BOWL_PATH_LIMIT];
        bipolar marker;
        positive made = 0;
        bipolar failed;

        if (!bowl_profile_marker(marker_path, sizeof(marker_path), root, name) ||
            !bowl_root_path(state, sizeof(state), root, BOWL_PROFILE_STATE))
                return bowl_refuse("bowl path is too long\n");

        failed = bowl_mkdir_parents(state);
        if (failed < 0)
                return bowl_fail(state, failed);

        marker = bowl_profile_marker_open(marker_path);
        if (marker < 0)
                return bowl_fail(marker_path, marker);

        bowl_quiet = true;
        for (positive at = 0; bowl_profile_bins[at]; at++)
                made += bowl_profile_expose_dir(root, bowl_profile_bins[at],
                                                marker);
        bowl_quiet = false;
        system_close(marker);

        string_format(log, bowl_label "%s: %p programs reachable by name and "
                                      "by /usr/bin path\n",
                      root, made);
        log_flush();
        return 0;
}

static b32 bowl_profile_packages(const struct bowl_distro address_to distro,
                                 string_address address_to verb,
                                 string_address address_to packages,
                                 string_address what)
{
        string_address argv[BOWL_PROFILE_ARGV];
        positive used = 0;

        for (; verb[used]; used++)
                argv[used] = verb[used];
        for (positive at = 0; packages[at]; at++)
        {
                if (used + 2 > BOWL_PROFILE_ARGV)
                        return bowl_refuse("too many packages for one command\n");
                argv[used++] = packages[at];
        }
        argv[used] = null;

        return bowl_setup_isolated(distro->root, argv[0], argv, what, null);
}

static b32 bowl_profile_script(const struct bowl_profile address_to profile)
{
        p8 path[BOWL_PATH_LIMIT];
        p8 text[BOWL_SHEBANG_LIMIT * 8];
        positive used;
        bipolar handle;

        path[0] = end;
        if (!path_join(path, sizeof(path), BOWL_EXPOSE_DIRECTORY,
                       profile->name) ||
            !bowl_profile_header(text, sizeof(text), profile->name))
                return bowl_refuse("session path is too long\n");

        used = string_length(text);
        for (positive at = 0; profile->session[at]; at++)
        {
                positive length = string_length(profile->session[at]);

                if (used + length + 2 > sizeof(text))
                        return bowl_refuse("session script is too long\n");
                memory_copy(text + used, profile->session[at], length);
                used += length;
                text[used++] = '\n';
        }

        // Removed first and made O_EXCL, so a link planted at the name is
        // not followed.
        system_remove_at(AT_FDCWD, path, 0);
        handle = system_open_at_mode(AT_FDCWD, path,
                                     FILE_WRITE | FILE_EXCLUSIVE | O_CLOEXEC,
                                     0755);
        if (handle < 0)
                return bowl_fail(path, handle);
        if (system_write_all((positive)handle, text, used) != used)
        {
                system_close(handle);
                system_remove_at(AT_FDCWD, path, 0);
                return bowl_refuse("could not write the session script\n");
        }
        system_close(handle);
        return 0;
}

static b32 bowl_profile_install(const struct bowl_profile address_to profile)
{
        for (const struct bowl_component address_to part =
                 profile->components;
             part->distro; part++)
        {
                const struct bowl_distro address_to distro =
                    bowl_find_distro(part->distro);
                b32 failed;

                if (!distro || !distro->install)
                        return bowl_refuse("a profile cannot use that "
                                           "distribution yet\n");

                failed = bowl_setup_distro(distro);
                if (!failed)
                {
                        // Refused before apk or pacman fills a stick's memory.
                        struct bowl_distro named = {.label = profile->label};

                        if (bowl_room_short(address_of named, part->bytes))
                                return 1;
                        string_format(log, bowl_label "%s: adding %s packages\n",
                                      profile->name, distro->label);
                        log_flush();
                        failed = bowl_profile_packages(
                            distro, distro->install, part->packages,
                            "the package manager could not add them\n");
                }
                if (!failed)
                        failed = bowl_profile_expose(distro->root,
                                                     profile->name);
                if (failed)
                        return failed;
        }

        {
                // Published beside the managers, so it is on every PATH.
                b32 failed = bowl_profile_script(profile);

                if (!failed)
                        failed = bowl_publish_bin(profile->name);
                if (failed)
                        return failed;
        }

        string_format(log, bowl_label "%s is ready. %s\n", profile->label,
                      profile->next);
        log_flush();
        return 0;
}

/* One name from a marker: its launcher goes if it still names this bowl, and
   so does /bin's link to it. Anything else is not this profile's to remove. */
static fn bowl_profile_drop(string_address root, string_address name)
{
        p8 launcher[BOWL_PATH_LIMIT];
        p8 link[BOWL_PATH_LIMIT];
        p8 current[BOWL_PATH_LIMIT];
        p8 line[BOWL_SHEBANG_LIMIT];
        p8 owner[BOWL_PATH_LIMIT];
        string_address program = null;
        bipolar reader;
        bipolar got;

        if (!bowl_name(name, true) ||
            !path_join(launcher, sizeof(launcher), BOWL_EXPOSE_DIRECTORY,
                       name) ||
            !path_join(link, sizeof(link), BOWL_BIN_DIRECTORY, name))
                return;

        reader = system_open_at(AT_FDCWD, launcher,
                                FILE_READ | O_CLOEXEC | O_NOFOLLOW);
        if (reader < 0)
                return;
        got = system_read_retry((positive)reader, line, sizeof(line) - 1);
        system_close(reader);
        if (got <= 0)
                return;
        line[got] = end;

        if (!bowl_shebang_target(line, owner, sizeof(owner),
                                 address_of program) ||
            !string_equals(owner, root))
                return;

        system_remove_at(AT_FDCWD, launcher, 0);
        if (file_link_text(link, current, sizeof(current)) >= 0 &&
            string_equals(current, launcher))
                system_remove_at(AT_FDCWD, link, 0);
}

static b32 bowl_profile_unexpose(string_address root, string_address marker)
{
        p8 chunk[4096];
        p8 name[BOWL_SHEBANG_LIMIT];
        positive have = 0;
        bool whole = true;
        bipolar handle = system_open_at(AT_FDCWD, marker,
                                        FILE_READ | O_CLOEXEC | O_NOFOLLOW);
        bipolar got;

        if (handle < 0)
                return handle == -ERROR_NO_ENTRY ? 0 : bowl_fail(marker, handle);

        while ((got = system_read_retry((positive)handle, chunk,
                                        sizeof(chunk))) > 0)
                for (bipolar at = 0; at < got; at++)
                {
                        if (chunk[at] != '\n')
                        {
                                if (have + 1 < sizeof(name))
                                        name[have++] = chunk[at];
                                else
                                        whole = false;
                                continue;
                        }
                        name[have] = end;
                        if (whole)
                                bowl_profile_drop(root, name);
                        have = 0;
                        whole = true;
                }

        system_close(handle);
        return 0;
}

static b32 bowl_profile_remove(const struct bowl_profile address_to profile)
{
        p8 path[BOWL_PATH_LIMIT];
        p8 want[BOWL_SHEBANG_LIMIT];
        p8 have[BOWL_SHEBANG_LIMIT];
        bipolar reader;
        bipolar got;

        for (const struct bowl_component address_to part =
                 profile->components;
             part->distro; part++)
        {
                const struct bowl_distro address_to distro =
                    bowl_find_distro(part->distro);
                p8 marker[BOWL_PATH_LIMIT];
                b32 failed;

                if (!distro || !distro->remove ||
                    !bowl_profile_marker(marker, sizeof(marker), distro->root,
                                         profile->name))
                        return bowl_refuse("a profile cannot use that "
                                           "distribution yet\n");

                failed = bowl_profile_unexpose(distro->root, marker);
                if (!failed && bowl_has(distro->root, distro->marker))
                        failed = bowl_profile_packages(
                            distro, distro->remove, part->packages,
                            "the package manager could not remove them\n");
                if (failed)
                        return failed;

                // Last, so a remove that stopped part way can be run again.
                system_remove_at(AT_FDCWD, marker, 0);
        }

        // The script goes only if it is the one install wrote.
        if (path_join(path, sizeof(path), BOWL_EXPOSE_DIRECTORY, profile->name) &&
            bowl_profile_header(want, sizeof(want), profile->name))
        {
                reader = system_open_at(AT_FDCWD, path,
                                        FILE_READ | O_CLOEXEC | O_NOFOLLOW);
                got = reader < 0 ? reader
                                 : system_read_retry((positive)reader, have,
                                                     sizeof(have) - 1);
                if (reader >= 0)
                        system_close(reader);
                if (got > 0)
                {
                        have[got] = end;
                        if (!string_compare_max(have, want, string_length(want)))
                        {
                                p8 link[BOWL_PATH_LIMIT];
                                p8 current[BOWL_PATH_LIMIT];

                                system_remove_at(AT_FDCWD, path, 0);
                                if (path_join(link, sizeof(link),
                                              BOWL_BIN_DIRECTORY,
                                              profile->name) &&
                                    file_link_text(link, current,
                                                   sizeof(current)) >= 0 &&
                                    string_equals(current, path))
                                        system_remove_at(AT_FDCWD, link, 0);
                        }
                }
        }

        string_format(log, bowl_label "%s removed\n", profile->label);
        log_flush();
        return 0;
}

static b32 bowl_profile_list(void)
{
        for (positive at = 0; at < array_count(bowl_profiles); at++)
        {
                const struct bowl_profile address_to profile = bowl_profiles + at;
                bool installed = true;

                for (const struct bowl_component address_to part =
                         profile->components;
                     part->distro; part++)
                {
                        const struct bowl_distro address_to distro =
                            bowl_find_distro(part->distro);
                        p8 marker[BOWL_PATH_LIMIT];

                        installed &= distro &&
                                     bowl_profile_marker(marker, sizeof(marker),
                                                         distro->root,
                                                         profile->name) &&
                                     system_access_at(AT_FDCWD, marker, 0) >= 0;
                }

                string_format(log, bowl_label "%s  %s%s\n", profile->name,
                              profile->label, installed ? "  (installed)" : "");
        }

        log_flush();
        return 0;
}

#define BOWL_PROFILE_WORDS_OK 0
#define BOWL_PROFILE_WORDS_USAGE 1
#define BOWL_PROFILE_WORDS_UNKNOWN 2

/* `bowl profile NAME [install|remove]`, and `remove NAME` as readily. */
static positive bowl_profile_words(
    positive count, string_address address_to arguments,
    const struct bowl_profile address_to address_to profile, bool address_to remove)
{
        string_address name;
        string_address action = "install";

        if (count < 3 || count > 4)
                return BOWL_PROFILE_WORDS_USAGE;

        name = arguments[2];
        if (count == 4)
        {
                action = arguments[3];
                if (string_equals(name, "install") ||
                    string_equals(name, "remove"))
                {
                        action = name;
                        name = arguments[3];
                }
        }

        address_to remove = string_equals(action, "remove");
        if (!address_to remove && !string_equals(action, "install"))
                return BOWL_PROFILE_WORDS_USAGE;

        address_to profile = bowl_find_profile(name);
        return address_to profile ? BOWL_PROFILE_WORDS_OK
                                  : BOWL_PROFILE_WORDS_UNKNOWN;
}

/* Out of line: inlined, its frames stack onto bowl_main's and cross the
   stack lane's pin. */
static b32 __attribute__((__noinline__))
bowl_profile_main(positive count, string_address address_to arguments)
{
        const struct bowl_profile address_to profile = null;
        bool remove = false;
        positive words;

        if (count == 2)
                return bowl_profile_list();

        words = bowl_profile_words(count, arguments, address_of profile,
                                   address_of remove);
        if (words == BOWL_PROFILE_WORDS_USAGE)
                return bowl_usage();
        if (words == BOWL_PROFILE_WORDS_UNKNOWN)
                return bowl_refuse("known profiles: desktop\n");

        if (bowl_setup_become_root("profile", profile->name,
                                   remove ? "remove" : "install"))
                return 1;

        return remove ? bowl_profile_remove(profile)
                      : bowl_profile_install(profile);
}

static b32 bowl_main()
{
        string_address address_to arguments = program_argument_list();
        positive count = (positive)program_argument_count();
        positive root_at = 1;
        string_address root;
        string_address program = null;
        string_address address_to command_arguments = null;
        bool isolated = false;
        bool isolated_told = false;
        p8 launcher_root[BOWL_PATH_LIMIT];

        if (!arguments || count < 2)
                return bowl_usage();

        if (string_equals(arguments[1], "setup"))
                return bowl_setup(count, arguments);

        if (string_equals(arguments[1], "expose"))
                return bowl_expose(count, arguments);

        if (string_equals(arguments[1], "profile"))
                return bowl_profile_main(count, arguments);

        if (string_equals(arguments[1], "--isolated"))
        {
                isolated = true;
                isolated_told = true;
                root_at++;
        }
        else if (string_equals(arguments[1], "--fast"))
        {
                isolated_told = true;
                root_at++;
        }
        else if (arguments[1][0] == '-' && arguments[1][1] == '-')
                return bowl_usage();

        if (root_at >= count)
                return bowl_usage();

        root = arguments[root_at];

        if (root[0] == '@')
        {
                /* argv[2] is the launcher filename inserted by binfmt_script. */
                if (root_at != 1 || count < 3 ||
                    !bowl_launcher(root, launcher_root,
                                   sizeof(launcher_root), address_of program))
                        return bowl_refuse("invalid exposed command\n");

                root = launcher_root;
                arguments[2] = program;
                command_arguments = arguments + 2;
        }
        else if (root_at + 1 < count)
        {
                program = arguments[root_at + 1];
                command_arguments = arguments + root_at + 1;
        }

        if (!isolated_told)
                isolated = bowl_needs_isolated(program);

        /* Isolated guests need the host's nameservers. Pacman/apk/apt conf
           and lock files are written when the tree is landed, not on every
           enter: clearing locks here raced a manager already running in
           that root. */
        if (isolated)
        {
                b32 failed = bowl_write_resolv(root);

                if (failed)
                        return failed;
        }

        return bowl_launch(root, program, command_arguments, isolated);
}
