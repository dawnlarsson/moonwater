/*
        Small coreutils process wrappers.

        These are policy around facilities the combined shell already owns:
        file_take reads the leading options, file_exec_path_try performs the
        PATH/environment handoff, wait_status_code decodes the kernel's wait
        word, and the signal/clock/file fronts reach the kernel.  In
        particular, none of the three carries a private command launcher.
*/

// Common command handoff ------------------------------------------

static b32 process_tool_exec_environment(
    string_address program, string_address address_to words,
    string_address address_to environment, string_address path)
{
        log_flush();

        bipolar answer = file_exec_path_try_in(words[0], words, environment,
                                               path);

        //      nohup names the command with quoteaf and the others with
        //      quote(), which is the difference between 'new'$'\n''line' and
        //      'new\nline' -- and between "q'x" and 'q\'x'.
        if (string_equals(program, "nohup"))
                return string_report(log_error, answer == -ERROR_NO_ENTRY ? 127 : 126,
                                     "%s: failed to run command %w: %s\n", program,
                                     writer_shell_quoted_name, words[0],
                                     file_reason(answer));
        return string_report(log_error, answer == -ERROR_NO_ENTRY ? 127 : 126, "%s: failed to run command '%w': %s\n",
                      program, writer_terminal_quoted_name, words[0], file_reason(answer));
}

static b32 process_tool_exec(string_address program,
                             string_address address_to words)
{
        return process_tool_exec_environment(
            program, words, file_environment_all(),
            file_environment((string_address) "PATH"));
}

// stdbuf ----------------------------------------------------------
/* The PATH query itself is shared with command/type.  Supplying the current
   process PATH explicitly is important here: a farm-linked utility dispatches
   before the resident shell has initialized its variable table. */
static b32 shell_find_in_path_mode(string_address name, p8 address_to into,
                                   positive room, positive access,
                                   bool use_hash, string_address value);

#define STDBUF_ELF_DYNAMIC 1
#define STDBUF_ELF_STATIC 2

static const argument_option process_stdbuf_options[] = {
    {"input", 'i', ARGUMENT_REQUIRED},
    {"output", 'o', ARGUMENT_REQUIRED},
    {"error", 'e', ARGUMENT_REQUIRED},
    {null},
};

static p8 stdbuf_input_assignment[48];
static p8 stdbuf_output_assignment[48];
static p8 stdbuf_error_assignment[48];
static p8 address_to stdbuf_preload_assignment;
static positive stdbuf_preload_room;
static p8 stdbuf_library[FILE_PATH_MAX];

/* Native ELF is little-endian on every supported target. Use the common
   alias-safe load so x86/ARM get one load and baseline RV64 stays alignment
   safe, rather than maintaining another byte-decoding family. */
#define stdbuf_u16(bytes) memory_load_unaligned(p16, (bytes))
#define stdbuf_u32(bytes) memory_load_unaligned(p32, (bytes))
#define stdbuf_u64(bytes) memory_load_unaligned(p64, (bytes))

static PURE positive stdbuf_elf_machine()
{
#if X64
        return 62;
#elif ARM64
        return 183;
#else
        return 243;
#endif
}

static PURE bool stdbuf_elf_native(p8 address_to bytes, positive length,
                                   bool shared)
{
        return length >= 64 && bytes[0] == 0x7f && bytes[1] == 'E' &&
               bytes[2] == 'L' && bytes[3] == 'F' && bytes[4] == 2 &&
               bytes[5] == 1 && stdbuf_u16(bytes + 18) == stdbuf_elf_machine() &&
               (!shared || stdbuf_u16(bytes + 16) == 3);
}

/* A candidate must be a native ELF shared object and must carry the three
   variables libstdbuf consumes.  This rejects a same-architecture executable
   renamed to libstdbuf.so instead of letting the loader complain and continue
   without applying the requested policy. */
static bool stdbuf_library_usable(string_address path)
{
        if (string_first_of(path, ':') || string_first_of(path, ' ') ||
            string_first_of(path, '\t'))
                return false;

        bipolar handle = system_open_at(AT_FDCWD, path,
                                        FILE_READ | O_CLOEXEC);

        if (handle < 0)
                return false;

        positive used = 0;

        while (used < sizeof(file_transfer))
        {
                bipolar got = system_read_retry(
                    (positive)handle, file_transfer + used,
                    sizeof(file_transfer) - used);

                if (got < 0)
                {
                        used = 0;
                        break;
                }
                if (!got)
                        break;

                used += (positive)got;
        }

        system_close(handle);

        if (!stdbuf_elf_native(file_transfer, used, true) ||
            !memory_search(file_transfer, used,
                           (address_any) "_STDBUF_I", 9) ||
            !memory_search(file_transfer, used,
                           (address_any) "_STDBUF_O", 9) ||
            !memory_search(file_transfer, used,
                           (address_any) "_STDBUF_E", 9))
                return false;

        positive length = string_length(path);

        if (length >= sizeof(stdbuf_library))
                return false;

        memory_copy_end(stdbuf_library, path, length);
        return true;
}

/* Search one filesystem root.  Debian installs in libexec, Arch and newer
   coreutils builds commonly use lib/coreutils, and multiarch Debian roots use
   the architecture component. */
static bool stdbuf_library_under(string_address root)
{
        static const string_address common[] = {
            "/usr/libexec/coreutils/libstdbuf.so",
            "/usr/lib/coreutils/libstdbuf.so",
            "/usr/lib64/coreutils/libstdbuf.so",
#if X64
            "/usr/lib/x86_64-linux-gnu/coreutils/libstdbuf.so",
#elif ARM64
            "/usr/lib/aarch64-linux-gnu/coreutils/libstdbuf.so",
#else
            "/usr/lib/riscv64-linux-gnu/coreutils/libstdbuf.so",
#endif
            null,
        };

        for (positive i = 0; common[i]; i++)
        {
                p8 path[FILE_PATH_MAX];
                string_address candidate = common[i];

                if (root)
                {
                        if (!file_path_join(path, root, candidate + 1))
                                continue;
                        candidate = path;
                }

                if (stdbuf_library_usable(candidate))
                        return true;
        }

        return false;
}

static bool stdbuf_bowl_root_from_text(string_address text, positive length,
                                       p8 address_to root)
{
        string_address prefix = (string_address)BOWL_ROOT_PREFIX;
        positive marked = sizeof(BOWL_ROOT_PREFIX) - 1;
        p8 address_to found = (p8 address_to)memory_search(
            (address_any)text, length, (address_any)prefix, marked);

        if (!found)
                return false;

        positive used = marked;
        positive remaining = length - (positive)(found - text);

        while (used < remaining && found[used] && found[used] != '/' &&
               found[used] != '\n' && !byte_is_space(found[used]))
                used++;

        if (used == marked || used >= FILE_PATH_MAX)
                return false;

        memory_copy_end(root, found, used);
        return true;
}

/* An exposed Bowl command is a #!/bowl launcher containing its real root.
   A directly named /bowls/NAME program already carries the same answer in
   its path. */
static bool stdbuf_bowl_root(string_address target, p8 address_to root)
{
        if (stdbuf_bowl_root_from_text(target, string_length(target), root) &&
            string_compare(root, (string_address)BOWL_EXPOSE_DIRECTORY))
                return true;

        bipolar got = file_read_once_at(AT_FDCWD, target, file_transfer, 256);

        return got > 0 && stdbuf_bowl_root_from_text(
                              file_transfer, (positive)got, root) &&
               string_compare(root, (string_address)BOWL_EXPOSE_DIRECTORY);
}

static bool stdbuf_find_library(string_address preferred_root)
{
        string_address forced =
            file_environment((string_address) "MOONWATER_STDBUF_LIBRARY");

        if (forced)
                return stdbuf_library_usable(forced);

        if (preferred_root && stdbuf_library_under(preferred_root))
                return true;

        /* Do not walk other bowls: Alpine's musl libstdbuf is not loadable
           into a Debian glibc process, and the reverse is the same crash. */
        return stdbuf_library_under(null);
}

/* A native ELF without PT_INTERP is statically linked, so LD_PRELOAD cannot
   possibly apply the requested buffering.  Scripts are left to the kernel;
   their interpreter receives the preload in the ordinary way. */
static b32 stdbuf_target_kind(string_address path)
{
        bipolar handle = system_open_at(AT_FDCWD, path,
                                        FILE_READ | O_CLOEXEC);

        if (handle < 0)
                return 0;

        bipolar got = system_read_retry((positive)handle, file_transfer,
                                        FILE_BLOCK);

        if (got < 64 || !stdbuf_elf_native(file_transfer, (positive)got,
                                           false))
        {
                system_close(handle);
                return 0;
        }

        p64 phoff = stdbuf_u64(file_transfer + 32);
        positive entry_size = stdbuf_u16(file_transfer + 54);
        positive entries = stdbuf_u16(file_transfer + 56);

        if (entry_size < 4 || !entries ||
            entries > FILE_BLOCK / entry_size ||
            phoff > positive_max - entry_size * entries)
        {
                system_close(handle);
                return STDBUF_ELF_STATIC;
        }

        positive bytes = entry_size * entries;
        p8 address_to table = file_transfer;

        if (phoff + bytes > (positive)got)
        {
                /* ELF headers beyond the first block use the same bounded,
                   EINTR/short-read-safe positional reader as disk metadata. */
                if (storage_read(handle, file_transfer, bytes, phoff) != bytes)
                {
                        system_close(handle);
                        return STDBUF_ELF_STATIC;
                }
        }
        else
                table += (positive)phoff;

        b32 answer = STDBUF_ELF_STATIC;

        for (positive i = 0; i < entries; i++)
                if (stdbuf_u32(table + i * entry_size) == 3)
                {
                        answer = STDBUF_ELF_DYNAMIC;
                        break;
                }

        system_close(handle);
        return answer;
}

static bool stdbuf_mode(string_address text, bool input,
                        p8 letter, p8 address_to assignment)
{
        memory_copy_apart(assignment, (address_any) "_STDBUF_I=", 10);
        assignment[8] = letter;

        if (string_is(text, 'L') && !string_get(text + 1))
        {
                if (input)
                        return string_report(log_error, false,
                                             "stdbuf: line buffering standard input is meaningless\n"
                                             "Try 'stdbuf --help' for more information.\n");

                assignment[10] = 'L';
                assignment[11] = end;
                return true;
        }

        string_address at = text;

        if (string_is(at, '+'))
                at++;

        positive value = 1;

        // A unit alone is one of it, as xstrtoumax reads -oK: a kibibyte.
        if (!byte_is_digit(string_get(at)) && string_get(at) &&
            (string_get(at) == 'k' || size_suffix_power(string_get(at), false)))
                ;
        else if (!string_digits_checked(address_of at, 10, address_of value))
                goto invalid;

        positive power = 0;
        positive base = 1024;
        p8 suffix = string_get(at);

        if (suffix)
        {
                power = suffix == 'k' ? 1
                                      : size_suffix_power(suffix, false);

                if (!power || (suffix >= 'a' && suffix != 'k'))
                        goto invalid;

                at++;

                if (string_is(at, 'B') && !string_get(at + 1))
                {
                        base = 1000;
                        at++;
                }
                else if (string_is(at, 'i') && string_is(at + 1, 'B') &&
                         !string_get(at + 2))
                        at += 2;
                else if (string_get(at))
                        goto invalid;
        }

        if (string_get(at))
                goto invalid;

        p64 scaled;
        if (!size_scale_power_checked(
                value, base, (p8)power, (p64)positive_max,
                address_of scaled))
                goto invalid;
        value = (positive)scaled;

        positive_into_string(assignment + 10, value);
        return true;

invalid:
        return string_report(log_error, false, "stdbuf: invalid mode '%w'\n", writer_terminal_quoted_name, text);
}

static bool stdbuf_preload(string_address library)
{
        string_address before =
            file_environment((string_address) "LD_PRELOAD");
        positive old_length = before ? string_length(before) : 0;
        positive library_length = string_length(library);
        positive wanted = 11 + old_length + (before ? 1 : 0) +
                          library_length + 1;

        if (!shell_array_room(stdbuf_preload_assignment, stdbuf_preload_room, wanted))
                return false;

        memory_copy_apart(stdbuf_preload_assignment,
                          (address_any) "LD_PRELOAD=", 11);
        positive used = 11;

        if (before)
        {
                memory_copy_apart(stdbuf_preload_assignment + used, before,
                                  old_length);
                used += old_length;
                stdbuf_preload_assignment[used++] = ':';
        }

        memory_copy_end(stdbuf_preload_assignment + used, library,
                        library_length);
        return true;
}

/* coreutils checks each mode as the option is read, so an invalid one is
   refused even when a later option for the same stream would supersede it. */
static bool process_stdbuf_seen(p8 letter, string_address value)
{
        p8 scratch[64];

        if (!value)
                return true;
        if (letter == 'i')
                return stdbuf_mode(value, true, 'I', scratch);
        if (letter == 'o')
                return stdbuf_mode(value, false, 'O', scratch);
        if (letter == 'e')
                return stdbuf_mode(value, false, 'E', scratch);
        return true;
}

static b32 process_stdbuf()
{
        file_taking taking = {
            .program = (string_address) "stdbuf",
            .options = process_stdbuf_options,
            .seen = process_stdbuf_seen,
        };

        if (!file_take(address_of taking))
                return 125;

        positive modes = taking.flags &
                         (FILE_FLAG('i') | FILE_FLAG('o') | FILE_FLAG('e'));
        positive count = (positive)program_argument_count();

        if (taking.first >= count)
                return string_report(log_error, 125, "stdbuf: missing operand\n" TOOLS_TRY("stdbuf"));
        if (!modes)
                return string_report(log_error, 125,
                                     "stdbuf: you must specify a buffering mode option\n"
                                     "Try 'stdbuf --help' for more information.\n");

        bool input = (modes & FILE_FLAG('i')) == 0 ||
                     stdbuf_mode(file_option_value(address_of taking, 'i'),
                                 true, 'I', stdbuf_input_assignment);
        bool output = (modes & FILE_FLAG('o')) == 0 ||
                      stdbuf_mode(file_option_value(address_of taking, 'o'),
                                  false, 'O', stdbuf_output_assignment);
        bool error = (modes & FILE_FLAG('e')) == 0 ||
                     stdbuf_mode(file_option_value(address_of taking, 'e'),
                                 false, 'E', stdbuf_error_assignment);

        if (!input || !output || !error)
                return 125;

        string_address address_to words =
            program_argument_list() + taking.first;
        p8 target[FILE_PATH_MAX];
        p8 bowl_root[FILE_PATH_MAX];
        string_address path = file_environment((string_address) "PATH");

        if (!path)
                path = (string_address)BOWL_DEFAULT_PATH;

        bool target_found = shell_find_in_path_mode(
            words[0], target, sizeof(target), 1, false, path);
        bool has_bowl_root = target_found &&
                             stdbuf_bowl_root(target, bowl_root);
        string_address forced = file_environment(
            (string_address) "MOONWATER_STDBUF_LIBRARY");

        /*
                A statically linked target -- this shell's own programs among
                them -- gets the _STDBUF_ variables and no preload: GNU's
                stdbuf runs it all the same, and ours read _STDBUF_O
                themselves (stdbuf_prompt). Only a dynamic one needs the
                library.
        */
        bool preload = !(target_found && stdbuf_target_kind(target) == STDBUF_ELF_STATIC);

        if (preload && !stdbuf_find_library(has_bowl_root ? bowl_root : null))
                return string_report(log_error, 125, forced
                    ? "stdbuf: '%s' is not a compatible libstdbuf.so\n"
                    : "stdbuf: no compatible libstdbuf.so found in the native or Bowl roots\n",
                    forced);

        if (preload && !stdbuf_preload(stdbuf_library))
                return string_report(log_error, 125, "stdbuf: environment is too large\n");

        env_have = 0;
        string_address address_to inherited = file_environment_all();

        for (positive i = 0; inherited && inherited[i]; i++)
                if (!env_put(inherited[i]))
                        return 125;

        env_drop((string_address) "MOONWATER_STDBUF_LIBRARY");

        if (((modes & FILE_FLAG('i')) &&
             !env_put(stdbuf_input_assignment)) ||
            ((modes & FILE_FLAG('o')) &&
             !env_put(stdbuf_output_assignment)) ||
            ((modes & FILE_FLAG('e')) &&
             !env_put(stdbuf_error_assignment)) ||
            (preload && !env_put(stdbuf_preload_assignment)))
                return 125;

        env_list[env_have] = null;
        path = string_get_environment(env_list, (string_address) "PATH");

        return process_tool_exec_environment((string_address) "stdbuf", words,
                                             env_list, path);
}

// chroot ----------------------------------------------------------

static const argument_option process_chroot_options[] = {
    {"skip-chdir", 'k', ARGUMENT_LONG_ONLY},
    {"groups", 'G', ARGUMENT_REQUIRED | ARGUMENT_LONG_ONLY},
    {"userspec", 'U', ARGUMENT_REQUIRED | ARGUMENT_LONG_ONLY},
    {null},
};

/*
        --userspec=USER[:GROUP] and --groups=G1,G2,...: the credentials the
        command runs with, said in the new root's own /etc/passwd and
        /etc/group, so they are read after the root is changed. A name is
        looked up and a number is taken as it is; the groups are set first,
        then the group, then the user, as GNU sets them.
*/
static b32 process_chroot_credentials(string_address userspec, string_address groups)
{
        bipolar uid = -1;
        bipolar gid = -1;
        positive list[64];
        positive listed = 0;

        if (userspec && userspec[0])
        {
                p8 text[128];
                string_address split = string_first_of(userspec, ':');
                positive user_length = split ? (positive)(split - userspec) : string_length(userspec);
                string_address group = split ? split + 1 : null;

                if (user_length >= sizeof(text))
                        return string_report(log_error, 125, "chroot: invalid user\n");
                memory_copy(text, userspec, user_length);
                text[user_length] = end;
                if (user_length)
                {
                        uid = file_identity_of((string_address)text, false);
                        if (uid < 0)
                                return string_report(log_error, 125, "chroot: invalid user\n");
                }
                if (group && group[0])
                {
                        gid = file_identity_of(group, true);
                        if (gid < 0)
                                return string_report(log_error, 125, "chroot: invalid group\n");
                }
        }
        if (groups && groups[0])
        {
                for (string_address at = groups; *at;)
                {
                        p8 text[64];
                        string_address comma = string_first_of(at, ',');
                        positive length = comma ? (positive)(comma - at) : string_length(at);

                        if (length && length < sizeof(text) && listed < array_count(list))
                        {
                                memory_copy(text, at, length);
                                text[length] = end;
                                bipolar id = file_identity_of((string_address)text, true);

                                if (id < 0)
                                        return string_report(log_error, 125, "chroot: invalid group '%s'\n",
                                                             (string_address)text);
                                list[listed++] = (positive)id;
                        }
                        at += length + (comma != null);
                }
                bipolar grouped = system_call_2(syscall(setgroups), listed, (positive)list);

                if (grouped < 0)
                        return string_report(log_error, 125, "chroot: failed to set supplemental groups: %s\n",
                                             file_reason(grouped));
        }
        bipolar set = 0;

        if (gid >= 0 && (set = system_call_1(syscall(setgid), (positive)gid)) < 0)
                return string_report(log_error, 125, "chroot: failed to set group-ID: %s\n", file_reason(set));
        if (uid >= 0 && (set = system_call_1(syscall(setuid), (positive)uid)) < 0)
                return string_report(log_error, 125, "chroot: failed to set user-ID: %s\n", file_reason(set));
        return 0;
}

static b32 process_chroot()
{
        file_taking taking = {
            .program = (string_address) "chroot",
            .options = process_chroot_options,
        };
        positive count = (positive)program_argument_count();

        if (!file_take(address_of taking))
                return 125;
        if (taking.first >= count)
                return string_report(log_error, 125, "chroot: missing operand\n" TOOLS_TRY("chroot"));

        string_address root = program_argument((b32)taking.first++);

        /* --skip-chdir is safe only when the requested root is the root we
           already have.  Compare inode/device identity instead of accepting
           one spelling of "/" while rejecting another. */
        if (taking.flags & FILE_FLAG('k'))
        {
                file_facts requested;
                file_facts current;

                if (!file_look_at(root, address_of requested) ||
                    !file_look_at((string_address) "/", address_of current) ||
                    !file_same_identity(address_of requested,
                                        address_of current))
                {
                        //      coreutils sends the reader on to --help here,
                        //      as it does for every usage complaint.
                        log_error("chroot: option --skip-chdir only permitted if NEWROOT is old '/'\n", 0);
                        return string_report(log_error, 125, TOOLS_TRY("chroot"));
                }
        }

        bipolar changed = system_call_1(syscall(chroot), (positive)root);

        if (changed < 0)
                return string_report(log_error, 125, "chroot: cannot change root directory to '%s': %s\n",
                              root, file_reason(changed));

        if (!(taking.flags & FILE_FLAG('k')) &&
            (changed = system_change_directory((string_address) "/")) < 0)
                return string_report(log_error, 125, "chroot: cannot chdir to root directory: %s\n",
                              file_reason(changed));

        {
                b32 credentials = process_chroot_credentials(
                    (taking.flags & FILE_FLAG('U')) ? file_option_value(address_of taking, 'U') : null,
                    (taking.flags & FILE_FLAG('G')) ? file_option_value(address_of taking, 'G') : null);

                if (credentials)
                        return credentials;
        }

        if (taking.first < count)
                return process_tool_exec((string_address) "chroot",
                    program_argument_list() + taking.first);

        string_address shell = file_environment((string_address) "SHELL");
        string_address words[3];

        if (!shell || !string_get(shell))
                shell = (string_address) "/bin/sh";

        words[0] = shell;
        words[1] = (string_address) "-i";
        words[2] = null;
        return process_tool_exec((string_address) "chroot", words);
}

// nohup -----------------------------------------------------------

static bool process_nohup_duplicate(bipolar from, b32 to,
                                    string_address what)
{
        bipolar answer = system_descriptor_install((b32)from, to);

        if (answer < 0)
                return string_report(log_error, false, "nohup: cannot redirect %s: %s\n",
                              what, file_reason(answer));

        return true;
}

/* Pin and authenticate one possible output name. */
static bipolar process_nohup_output_open(string_address path)
{
        /* nohup is commonly launched from a terminal as root.  Opening the
           public name with O_APPEND alone followed a planted symlink, and a
           FIFO could hold the privileged process in open forever.  Pin the
           object without following, without blocking, and accept only one
           ordinary inode: a second hard link is the remaining way an
           existing sensitive file can be presented under this name. */
#define NOHUP_OUTPUT_FLAGS (FILE_APPEND | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK)
        bipolar answer = system_open_at_mode(
            AT_FDCWD, path, NOHUP_OUTPUT_FLAGS, 0600);

        if (answer >= 0)
        {
                file_facts facts;

                if (!file_look(answer, (string_address)"", AT_EMPTY_PATH,
                               address_of facts) ||
                    (facts.mode & MODE_FORMAT) != MODE_FILE ||
                    facts.hard_links != 1 ||
                    facts.owner != (p32)system_call_1(syscall(geteuid), 0) ||
                    /* O_NONBLOCK was only for the open; the command's output
                       blocks, as GNU nohup's does. */
                    system_call_3(syscall(fcntl), (positive)answer,
                                  FILE_F_SETFL, FILE_APPEND) < 0)
                {
                        system_close((positive)answer);
                        answer = -ERROR_ACCESS;
                }
        }

        return answer;
#undef NOHUP_OUTPUT_FLAGS
}

static bipolar process_nohup_output(p8 address_to path)
{
        bipolar answer = process_nohup_output_open(
            (string_address)"nohup.out");

        if (answer >= 0)
        {
                string_copy(path, (string_address) "nohup.out");
                return answer;
        }

        string_address home = file_environment((string_address) "HOME");
        positive home_length = home ? string_length(home) : 0;

        if (!home_length || home_length >= FILE_PATH_MAX - 11)
                return answer;

        path_join(path, FILE_PATH_MAX, home, (string_address) "nohup.out");
        return process_nohup_output_open(path);
}

static b32 process_nohup()
{
        file_taking taking = {
            .program = (string_address) "nohup",
            .options = (const argument_option[]){
    {null},
        }};
        positive count = (positive)program_argument_count();
        /* POSIX has nohup fail with 127 where coreutils otherwise uses 125. */
        b32 failure = file_environment((string_address) "POSIXLY_CORRECT") ? 127 : 125;

        if (!file_take(address_of taking))
                return failure;
        if (taking.first >= count)
                return string_report(log_error, failure, "nohup: missing operand\n" TOOLS_TRY("nohup"));

        bool input_terminal = stream_is_terminal(0);
        bool output_terminal = stream_is_terminal(1);
        bool error_terminal = stream_is_terminal(2);
        bipolar null_input = -1;
        bipolar output = -1;
        p8 output_path[FILE_PATH_MAX];

        if (input_terminal)
        {
                null_input = system_open_at(AT_FDCWD,
                    (string_address) "/dev/null", FILE_READ | O_CLOEXEC);

                if (null_input < 0)
                        return string_report(log_error, failure, "nohup: failed to open '/dev/null': %s\n",
                                      file_reason(null_input));
        }

        if (output_terminal)
        {
                output = process_nohup_output(output_path);

                if (output < 0)
                {
                        if (null_input >= 0)
                                system_close(null_input);
                        return string_report(log_error, failure, "nohup: failed to open 'nohup.out': %s\n",
                                      file_reason(output));
                }
        }

        if (input_terminal && output_terminal)
                string_format(log_error,
                    "nohup: ignoring input and appending output to '%s'\n",
                    output_path);
        else if (input_terminal)
                log_error("nohup: ignoring input\n", 0);
        else if (output_terminal)
                string_format(log_error, "nohup: appending output to '%s'\n",
                              output_path);

        /* Flush the explanation to the original error stream before that
           descriptor is made to follow the command's output. */
        log_flush();

        if (null_input >= 0)
        {
                if (!process_nohup_duplicate(null_input, 0,
                                              (string_address) "standard input"))
                {
                        system_close(null_input);
                        if (output >= 0)
                                system_close(output);
                        return failure;
                }
                if (null_input != 0)
                        system_close(null_input);
        }

        if (output >= 0)
        {
                if (!process_nohup_duplicate(output, 1,
                                              (string_address) "standard output"))
                {
                        system_close(output);
                        return failure;
                }
                if (output != 1)
                        system_close(output);
        }

        if (error_terminal &&
            !process_nohup_duplicate(1, 2, (string_address) "standard error"))
                return failure;

        /* Ignored dispositions survive exec; that is precisely nohup's one
           signal operation. */
        if (!system_signal_install(SIGHUP, SIGNAL_IGNORE, 0, 0, null))
                return string_report(log_error, failure, "nohup: cannot ignore hangup signal\n");

        return process_tool_exec((string_address) "nohup",
            program_argument_list() + taking.first);
}

// pipesz ----------------------------------------------------------
/* Linux exposes pipe capacity through fcntl and the unread byte count through
   FIONREAD.  Keeping this wrapper in the process-policy unit lets its optional
   command use the same PATH/environment handoff as stdbuf, env and nohup. */
#define PIPESZ_SET 1031
#define PIPESZ_GET 1032
#define PIPESZ_UNREAD 0x541b

static const argument_option process_pipesz_options[] = {
    {"get", 'g'},
    {"set", 's', ARGUMENT_REQUIRED},
    {"file", 'f', ARGUMENT_REQUIRED},
    {"fd", 'n', ARGUMENT_REQUIRED},
    {"stdin", 'i'},
    {"stdout", 'o'},
    {"stderr", 'e'},
    {"check", 'c'},
    {"quiet", 'q'},
    {"verbose", 'v'},
    {"help", 'h'},
    {"version", 'V'},
    {null},
};

typedef struct
{
        bipolar descriptor;
        string_address label;
        bool close;
} process_pipe_target;

static b32 process_pipesz()
{
        file_taking taking = {
            .program = (string_address)"pipesz",
            .options = process_pipesz_options,
        };

        if (!file_take(address_of taking))
                return 1;

        if (file_meta(address_of taking, "[options] [--] [command]\n"
                      "  -g, --get       examine pipe buffers\n"
                      "  -s, --set SIZE  set pipe buffer size\n"
                      "  -f FILE  -n FD  -i stdin  -o stdout  -e stderr\n"
                      "  -c check  -q quiet  -v verbose", log))
                return 0;

        bool getting = (taking.flags & FILE_FLAG('g')) != 0;
        if (getting && (taking.flags & FILE_FLAG('s')))
                return string_report(log_error, 1, "pipesz: options --get and --set cannot be combined\n");
        positive count = (positive)program_argument_count();

        if (getting && taking.first < count)
                return string_report(log_error, 1, "pipesz: cannot specify a command with --get\n");

        positive requested = 1024 * 1024;
        if (taking.flags & FILE_FLAG('s'))
        {
                b64 parsed;
                p8 relation;
                string_address value = file_option_value(address_of taking, 's');

                if (!truncate_size(value, address_of parsed,
                                   address_of relation) ||
                    relation != TRUNCATE_ABSOLUTE || parsed < 0 ||
                    (p64)parsed > positive_max)
                        return string_report(log_error, 1, "pipesz: invalid size: '%s'\n", value);
                requested = (positive)parsed;
        }

        process_pipe_target targets[5];
        positive used = 0;
        bool chosen = (taking.flags &
                       (FILE_FLAG('f') | FILE_FLAG('n') | FILE_FLAG('i') |
                        FILE_FLAG('o') | FILE_FLAG('e'))) != 0;

        /* util-linux parses every option before it opens a file, refuses a
           descriptor that is not an integer whatever --quiet says, and lets
           a negative one through to fcntl, which refuses it in turn. */
        p8 fd_label[32];
        bipolar named_descriptor = 0;
        if (taking.flags & FILE_FLAG('n'))
        {
                string_address value = file_option_value(address_of taking, 'n');

                if (!file_signed_decimal(value, address_of named_descriptor) ||
                    named_descriptor > b32_max || named_descriptor < -(bipolar)b32_max)
                        return string_report(log_error, 1, "pipesz: invalid fd argument: '%s'\n",
                                             value);

                positive at = (positive)(memory_copy_end(
                    fd_label, (address_any)"fd ", 3) - fd_label);
                if (named_descriptor < 0)
                        fd_label[at++] = '-';
                positive_into_string(fd_label + at,
                                     (positive)(named_descriptor < 0
                                                    ? -named_descriptor
                                                    : named_descriptor));
        }

        if (getting && (taking.flags & FILE_FLAG('v')))
                log("pipe\tsize\tunread\n", 17);

        if (taking.flags & FILE_FLAG('f'))
        {
                string_address path = file_option_value(address_of taking, 'f');
                bipolar descriptor = system_open_at(
                    AT_FDCWD, path, FILE_READ | O_NONBLOCK | O_CLOEXEC);

                if (descriptor < 0)
                {
                        if (!(taking.flags & FILE_FLAG('q')) ||
                            (taking.flags & FILE_FLAG('c')))
                                string_format(log_error,
                                              "pipesz: cannot open %s: %s\n",
                                              path, file_reason(descriptor));
                        if (taking.flags & FILE_FLAG('c'))
                                return 1;
                }
                else
                        targets[used++] = (process_pipe_target){
                            descriptor, path, true};
        }

        if (taking.flags & FILE_FLAG('n'))
                targets[used++] = (process_pipe_target){
                    named_descriptor, (string_address)fd_label, false};
        if (taking.flags & FILE_FLAG('i'))
                targets[used++] = (process_pipe_target){0, (string_address)"fd 0", false};
        if (taking.flags & FILE_FLAG('o'))
                targets[used++] = (process_pipe_target){1, (string_address)"fd 1", false};
        if (taking.flags & FILE_FLAG('e'))
                targets[used++] = (process_pipe_target){2, (string_address)"fd 2", false};
        if (!chosen)
                targets[used++] = (process_pipe_target){
                    getting ? 0 : 1,
                    getting ? (string_address)"fd 0" : (string_address)"fd 1",
                    false};

        bool failed = false;

        for (positive i = 0; i < used; i++)
        {
                process_pipe_target address_to target = targets + i;
                bipolar answer = system_call_3(
                    syscall(fcntl), (positive)target->descriptor,
                    getting ? PIPESZ_GET : PIPESZ_SET,
                    getting ? 0 : requested);

                if (answer < 0)
                {
                        failed = true;
                        if (!(taking.flags & FILE_FLAG('q')) ||
                            (taking.flags & FILE_FLAG('c')))
                                string_format(
                                    log_error,
                                    "pipesz: cannot %s pipe buffer size of %s: %s\n",
                                    getting ? (string_address)"get"
                                            : (string_address)"set",
                                    target->label, file_reason(answer));
                }
                else if (getting)
                {
                        b32 unread = 0;
                        bipolar counted = system_control(
                            target->descriptor, PIPESZ_UNREAD, address_of unread);

                        string_format(log, "%s\t%p\t%p\n", target->label,
                                      (positive)answer,
                                      counted < 0 ? 0 : (positive)unread);
                }
                else if (taking.flags & FILE_FLAG('v'))
                        string_format(log_error,
                                      "pipesz: %s pipe buffer size set to %p\n",
                                      target->label, (positive)answer);

                if (target->close)
                        system_close(target->descriptor);
                if (failed && (taking.flags & FILE_FLAG('c')))
                        break;
        }

        log_flush();
        if (failed && (taking.flags & FILE_FLAG('c')))
                return 1;
        if (taking.first < count)
                return process_tool_exec(
                    (string_address)"pipesz",
                    program_argument_list() + taking.first);
        return 0;
}

// coresched -------------------------------------------------------
/* Core scheduling is a kernel cookie, not a userspace scheduler.  The whole
   implementation is one prctl command path plus the process handoff already
   shared by the wrappers above. */
#define PROCESS_SCHED_CORE 62
#define PROCESS_SCHED_CORE_GET 0
#define PROCESS_SCHED_CORE_CREATE 1
#define PROCESS_SCHED_CORE_SHARE_TO 2
#define PROCESS_SCHED_CORE_SHARE_FROM 3
#define PROCESS_SCHED_CORE_THREAD 0
#define PROCESS_SCHED_CORE_THREAD_GROUP 1
#define PROCESS_SCHED_CORE_PROCESS_GROUP 2

static const argument_option process_coresched_options[] = {
    {"source", 's', ARGUMENT_REQUIRED},
    {"dest", 'd', ARGUMENT_REQUIRED},
    {"dest-type", 't', ARGUMENT_REQUIRED},
    {"verbose", 'v'},
    {"help", 'h'},
    {"version", 'V'},
    {null},
};

static bipolar process_coresched_call(positive operation, positive process,
                                      positive scope,
                                      p64 address_to cookie)
{
        return system_call_5(syscall(prctl), PROCESS_SCHED_CORE, operation,
                             process, scope, (positive)cookie);
}

static bool process_coresched_pid(string_address text,
                                  positive address_to process)
{
        return string_digits_checked_exact(text, 10, process) && address_to process &&
               address_to process <= b32_max;
}

static bool process_coresched_scope(string_address text,
                                    positive address_to scope)
{
        if (!text || string_equals(text, (string_address)"tgid"))
                address_to scope = PROCESS_SCHED_CORE_THREAD_GROUP;
        else if (string_equals(text, (string_address)"pid"))
                address_to scope = PROCESS_SCHED_CORE_THREAD;
        else if (string_equals(text, (string_address)"pgid"))
                address_to scope = PROCESS_SCHED_CORE_PROCESS_GROUP;
        else
                return false;
        return true;
}

static fn process_coresched_cookie(writer write, p64 cookie)
{
        p8 text[24] = "0x";
        positive length = 2 + positive_into_base(text + 2, cookie, 16, false);
        write(text, length);
}

static b32 process_coresched()
{
        enum { CORE_GET, CORE_NEW, CORE_COPY } operation = CORE_GET;

        /* util-linux reads the options wherever they stand; the first bare
           word may name the function and the rest is the command. */
        file_operands_begin();
        file_taking taking = {
            .program = (string_address)"coresched",
            .options = process_coresched_options,
            .operand = file_operand,
        };
        if (!file_take(address_of taking) || file_operand_failed)
                return 1;

        positive first_word = 0;
        if (file_operand_count)
        {
                string_address word = file_operand_at(0);
                if (string_equals(word, (string_address)"get"))
                        first_word = 1;
                else if (string_equals(word, (string_address)"new"))
                {
                        operation = CORE_NEW;
                        first_word = 1;
                }
                else if (string_equals(word, (string_address)"copy"))
                {
                        operation = CORE_COPY;
                        first_word = 1;
                }
        }

        string_address words[256];
        positive word_count = file_operand_count - first_word;
        if (word_count >= array_count(words))
                return string_report(log_error, 1, "coresched: too many arguments\n");
        for (positive at = 0; at < word_count; at++)
                words[at] = file_operand_at(first_word + at);
        words[word_count] = null;

        if (file_meta(address_of taking, "[get] [--source PID]\n"
                      "       coresched new [-t TYPE] --dest PID|-- COMMAND\n"
                      "       coresched copy [--source PID] [-t TYPE] --dest PID|-- COMMAND", log))
                return 0;

        positive self = (positive)system_call(syscall(getpid));
        positive source = self;
        positive destination = 0;
        positive scope;

        if ((taking.flags & FILE_FLAG('s')) &&
            !process_coresched_pid(file_option_value(address_of taking, 's'),
                                   address_of source))
                return string_report(log_error, 1, "coresched: invalid source PID: '%s'\n",
                              file_option_value(address_of taking, 's'));
        if ((taking.flags & FILE_FLAG('d')) &&
            !process_coresched_pid(file_option_value(address_of taking, 'd'),
                                   address_of destination))
                return string_report(log_error, 1, "coresched: invalid destination PID: '%s'\n",
                              file_option_value(address_of taking, 'd'));
        if (!process_coresched_scope(
                file_option_value(address_of taking, 't'), address_of scope))
                return string_report(log_error, 1, "coresched: invalid destination type: '%s'\n",
                              file_option_value(address_of taking, 't'));

        bool command = word_count > 0;
        if (operation == CORE_GET)
        {
                // A destination type is accepted and ignored here, as in
                // util-linux.
                if (command || destination)
                        return string_report(log_error, 1, "coresched: bad usage of the get function\n");

                p64 cookie = 0;
                bipolar answer = process_coresched_call(
                    PROCESS_SCHED_CORE_GET, source, PROCESS_SCHED_CORE_THREAD,
                    address_of cookie);
                if (answer < 0)
                        return string_report(log_error, 1, "coresched: cannot get cookie of PID %p: %s\n",
                                      source, file_reason(answer));
                string_format(log, "cookie of PID %p is ", source);
                process_coresched_cookie(log, cookie);
                log("\n", 1);
                log_flush();
                return 0;
        }

        if ((operation == CORE_NEW &&
             (taking.flags & FILE_FLAG('s'))) ||
            (destination && command) || (!destination && !command))
                return string_report(log_error, 1, operation == CORE_NEW
                              ? (string_address)"coresched: new requires either a destination or a command\n"
                              : (string_address)"coresched: copy requires either a destination or a command\n");

        bipolar changed;
        if (operation == CORE_NEW)
                changed = process_coresched_call(
                    PROCESS_SCHED_CORE_CREATE, destination, scope, null);
        else
        {
                changed = 0;
                if (source != self)
                        changed = process_coresched_call(
                            PROCESS_SCHED_CORE_SHARE_FROM, source,
                            PROCESS_SCHED_CORE_THREAD, null);
                if (changed >= 0 && destination)
                        changed = process_coresched_call(
                            PROCESS_SCHED_CORE_SHARE_TO, destination, scope,
                            null);
        }

        if (changed < 0)
                return string_report(log_error, 1, "coresched: cannot change cookie: %s\n",
                              file_reason(changed));

        if (taking.flags & FILE_FLAG('v'))
        {
                positive shown = operation == CORE_COPY
                                     ? source
                                     : (destination ? destination : self);
                p64 cookie = 0;

                if (process_coresched_call(
                        PROCESS_SCHED_CORE_GET, shown,
                        PROCESS_SCHED_CORE_THREAD, address_of cookie) >= 0)
                {
                        if (operation == CORE_COPY && destination)
                        {
                                log_error("coresched: copied cookie ", 0);
                                process_coresched_cookie(log_error, cookie);
                                string_format(log_error,
                                              " from PID %p to PID %p\n",
                                              source, destination);
                        }
                        else
                        {
                                string_format(log_error,
                                              "coresched: set cookie of PID %p to ",
                                              shown);
                                process_coresched_cookie(log_error, cookie);
                                log_error("\n", 1);
                        }
                }
        }

        if (command)
                return process_tool_exec((string_address)"coresched", words);
        return 0;
}

// timeout ---------------------------------------------------------

static const argument_option process_timeout_options[] = {
    {"foreground", 'f'},
    {"kill-after", 'k', ARGUMENT_REQUIRED},
    {"preserve-status", 'p'},
    {"signal", 's', ARGUMENT_REQUIRED},
    {"verbose", 'v'},
    {null},
};

/*
        What timeout stands between the command and: coreutils' term_sig --
        the terminal's and the kill command's signals, and every other one
        whose default is to end a process, the real-time ones included --
        less any this process was started with ignored, which a shell hands a
        background job and which it means to stay unheard; ALRM and the
        signal asked for are taken whatever. Left to their defaults they
        ended timeout itself (kill -ALRM answered 142, a closed pipe 141) and
        orphaned the command it was minding. SIGCHLD shares the signalfd only
        so an old kernel without pidfd support still has an event to wake an
        unlimited wait. One mask and one descriptor replace a handler per
        signal.
*/
#define PROCESS_SIGNAL_ALARM 14
/* script's relay set: HUP, INT, QUIT and TERM, and SIGCHLD to wake it. */
#define PROCESS_TIMEOUT_SIGNALS                                             \
        (((positive)1 << (SIGHUP - 1)) |                                    \
         ((positive)1 << (SIGINT - 1)) |                                    \
         ((positive)1 << (SIGQUIT - 1)) |                                   \
         ((positive)1 << (SIGTERM - 1)) |                                   \
         ((positive)1 << (SIGCHLD - 1)))

static bool process_timeout_ignored(b32 signal)
{
        positive action[4] = {0, 0, 0, 0};

        return system_call_4(syscall(rt_sigaction), (positive)signal, 0,
                             (positive)action, 8) >= 0 &&
               action[0] == 1;
}

static positive process_timeout_relayed(b32 term)
{
        static const p8 ending[] = {
            PROCESS_SIGNAL_ALARM, SIGINT, SIGQUIT, SIGHUP, SIGTERM, 13, 10, 12, 4, 5, 6, 7, 8, 11,
            24, 25, 31, 26, 27, 29, 30, 16};
        positive mask = (positive)1 << (SIGCHLD - 1);

        for (positive at = 0; at < array_count(ending) + 31; at++)
        {
                b32 signal = at < array_count(ending) ? ending[at]
                                                      : (b32)(34 + at - array_count(ending));

                if (signal == PROCESS_SIGNAL_ALARM || signal == term || !process_timeout_ignored(signal))
                        mask |= (positive)1 << (signal - 1);
        }
        // -s 0 is a signal that is never sent, and has no bit.
        return term ? mask | (positive)1 << (term - 1) : mask;
}

/* Wait for one child until a monotonic deadline. pidfd+ppoll is the native
   steady-state path: no handler, alarm signal, tick loop, or PID reuse race.
   The WNOHANG loop exists only for kernels predating pidfd_open. */
static bipolar process_timeout_wait(b32 child, bipolar pidfd, bipolar signal_fd,
                                    positive deadline,
                                    positive address_to status,
                                    b32 address_to forwarded)
{
        if (pidfd >= 0 || signal_fd >= 0)
        {
                system_poll_descriptor waited[2];
                positive descriptors = 0;
                positive pid_index = positive_max;
                positive signal_index = positive_max;

                if (pidfd >= 0)
                {
                        pid_index = descriptors;
                        waited[descriptors++] =
                            (system_poll_descriptor){
                                (b32)pidfd, SYSTEM_POLL_READ, 0};
                }
                if (signal_fd >= 0)
                {
                        signal_index = descriptors;
                        waited[descriptors++] =
                            (system_poll_descriptor){
                                (b32)signal_fd, SYSTEM_POLL_READ, 0};
                }

                for (;;)
                {
                        timespec span;
                        timespec address_to limit = null;

                        if (deadline)
                        {
                                positive now = clock_monotonic_nanoseconds();
                                /*      Overdue is not the same as still
                                        running. A child that ended while this
                                        was off the processor -- which a busy
                                        machine can hold it for longer than
                                        the whole grace period -- has its
                                        descriptor ready this moment, and
                                        asking costs a poll that waits for
                                        nothing. coreutils reaps without
                                        waiting before it suspends, for the
                                        same reason, and so never kills a
                                        command that had already gone. */
                                positive left = now >= deadline
                                                    ? 0 : deadline - now;
                                span = (timespec){left / 1000000000,
                                                  left % 1000000000};
                                limit = address_of span;
                        }

                        for (positive i = 0; i < descriptors; i++)
                                waited[i].returned = 0;
                        bipolar ready = system_poll_wait(
                            waited, descriptors, limit, null);

                        if (!ready)
                                return 0;
                        if (ready < 0)
                        {
                                if (ready == UL_ERROR_INTERRUPTED)
                                        continue;
                                return -1;
                        }

                        if (pid_index != positive_max &&
                            waited[pid_index].returned)
                                return system_wait4_retry(child, status, 0,
                                                          null) < 0 ? -1 : 1;

                        if (signal_index != positive_max &&
                            waited[signal_index].returned)
                        {
                                positive information[16];
                                bipolar got = system_read_retry(
                                    (positive)signal_fd, information,
                                    sizeof(information));

                                if (got < (bipolar)sizeof(p32))
                                        return got < 0 ? -1 : 0;

                                b32 number = (b32)(p32)information[0];

                                if (number != SIGCHLD)
                                {
                                        address_to forwarded = number;
                                        return 2;
                                }

                                /* pidfd and SIGCHLD can become ready in
                                   either order. After consuming SIGCHLD, the
                                   next poll observes the pidfd; without one,
                                   the WNOHANG check below reaps it. */
                                if (pidfd >= 0)
                                        continue;

                                bipolar reaped = system_wait4_retry(
                                    child, status, 1, null);

                                if (reaped == child)
                                        return 1;
                                if (reaped < 0)
                                        return -1;
                        }
                }
        }

        if (!deadline)
                return system_wait4_retry(child, status, 0, null) < 0 ? -1 : 1;

        for (;;)
        {
                bipolar waited = system_wait4_retry(child, status, 1, null);

                if (waited == child)
                        return 1;
                if (waited < 0)
                        return -1;

                positive now = clock_monotonic_nanoseconds();

                if (now >= deadline)
                        return 0;

                positive left = deadline - now;
                process_nap_ns(left < 10000000 ? left : 10000000);
        }
}

static fn process_timeout_cleanup(bipolar pidfd, bipolar signal_fd,
                                  positive previous_mask)
{
        if (pidfd >= 0)
                system_close(pidfd);
        if (signal_fd >= 0)
                system_close(signal_fd);

        /* A signal that came with nothing left to send it to -- the PIPE of
           a -v line written to a reader already gone -- is spent here, as
           coreutils' handler spends it, rather than ending timeout the
           moment the mask comes off and turning 124 into 141. */
        for (;;)
        {
                positive pending = 0;
                timespec none = {0, 0};

                if (system_call_2(syscall(rt_sigpending), (positive)address_of pending, 8) < 0)
                        break;
                pending &= ~previous_mask;
                if (!pending ||
                    system_call_4(syscall(rt_sigtimedwait), (positive)address_of pending, 0,
                                  (positive)address_of none, 8) < 0)
                        break;
        }

        system_signal_mask(UL_SIGNAL_SET_MASK, address_of previous_mask, null,
                           8);
}

static fn process_timeout_signal(b32 child, bipolar pidfd, b32 signal,
                                 bool foreground, bool verbose,
                                 string_address command)
{
        if (verbose)
        {
                p8 name[16];

                kill_name((positive)signal, name);
                string_format(log_error,
                              "timeout: sending signal %s to command '%w'\n",
                              name, writer_terminal_quoted_name, command);
        }

        if (foreground && pidfd >= 0 &&
            system_call_4(syscall(pidfd_send_signal), (positive)pidfd,
                          (positive)signal, 0, 0) >= 0)
                return;

        system_call_2(syscall(kill),
                      (positive)(foreground ? child : -child),
                      (positive)signal);
}

/* GNU's duration: a float with an optional s/m/h/d, where strtod also
   takes inf (no deadline at all) and a 0x hexadecimal. */
static bool process_timeout_duration(string_address text,
                                     positive address_to duration)
{
        // strtod's hexadecimal floats, 0x1p-3 among them, as sleep reads them.
        if (string_is(text, '0') && (text[1] == 'x' || text[1] == 'X') &&
            sleep_read_other(text, duration))
        {
                if (address_to duration == positive_max)
                        address_to duration = 0;
                return true;
        }
        if (string_equals(text, "inf") || string_equals(text, "INF") ||
            string_equals(text, "infinity") || string_equals(text, "+inf"))
        {
                address_to duration = 0;
                return true;
        }
        if (string_is(text, '0') && (text[1] == 'x' || text[1] == 'X'))
        {
                string_address at = text + 2;
                positive value;
                positive unit = 1;

                if (!string_digits_checked(address_of at, 16, address_of value))
                        return false;
                if (string_get(at) == 'm')
                        unit = 60;
                else if (string_get(at) == 'h')
                        unit = 3600;
                else if (string_get(at) == 'd')
                        unit = 86400;
                else if (string_get(at) && string_get(at) != 's')
                        return false;
                if (string_get(at) && string_get(at + 1))
                        return false;
                if (value > positive_max / (unit * 1000000000u))
                        return false;
                address_to duration = value * unit * 1000000000u;
                return true;
        }
        /* A number past what nanoseconds can count is forever, as the
           double it is to coreutils saturates rather than refuses. */
        if (file_duration_read(text, true, duration))
                return true;
        if (!file_duration_overflowed)
                return false;
        address_to duration = 0;
        return true;
}

/* coreutils validates each --signal and --kill-after as it is read. */
static bool process_timeout_seen(p8 letter, string_address value)
{
        if (letter == 's' && value)
        {
                bipolar named = ul_signal_number(value);

                if (named < 0 || named > 64)
                {
                        return string_report(log_error, false, "timeout: '%w': invalid signal\n"
                                                 "Try 'timeout --help' for more information.\n",
                                      writer_terminal_quoted_name, value);
                }
        }
        if (letter == 'k' && value)
        {
                positive after;

                if (!process_timeout_duration(value, address_of after))
                {
                        return string_report(log_error, false, "timeout: invalid time interval '%w'\n"
                                                 "Try 'timeout --help' for more information.\n",
                                      writer_terminal_quoted_name, value);
                }
        }
        return true;
}

static b32 process_timeout()
{
        file_taking taking = {
            .program = (string_address) "timeout",
            .options = process_timeout_options,
            .seen = process_timeout_seen,
        };
        positive count = (positive)program_argument_count();

        if (!file_take(address_of taking))
                return 125;
        /* coreutils answers a missing duration or command with the usage
           hint alone, and names the offending value in the other three. */
        if (taking.first >= count)
                return string_report(log_error, 125, "Try 'timeout --help' for more information.\n");

        positive duration;
        string_address interval = program_argument((b32)taking.first++);

        if (!process_timeout_duration(interval, address_of duration))
                return string_report(log_error, 125, "timeout: invalid time interval '%w'\n"
                                                     "Try 'timeout --help' for more information.\n",
                                     writer_terminal_quoted_name, interval);
        if (taking.first >= count)
                return string_report(log_error, 125, "Try 'timeout --help' for more information.\n");

        b32 signal = SIGTERM;
        string_address signal_text = file_option_value(address_of taking, 's');

        if (signal_text)
                signal = (b32)ul_signal_number(signal_text);

        bool escalate = (taking.flags & FILE_FLAG('k')) != 0;
        positive kill_after = 0;

        if (escalate)
                process_timeout_duration(file_option_value(address_of taking, 'k'),
                                         address_of kill_after);
        // Zero disables the second signal, as it disables the first.
        escalate = escalate && kill_after;

        bool foreground = (taking.flags & FILE_FLAG('f')) != 0;
        bool preserve = (taking.flags & FILE_FLAG('p')) != 0;
        bool verbose = (taking.flags & FILE_FLAG('v')) != 0;
        positive status = 0;
        positive blocked = process_timeout_relayed(signal);
        positive previous_mask = 0;

        /* A command's end is heard through SIGCHLD, and one inherited as
           ignored has the kernel reap it unasked, which left nothing to
           wait for and answered 125 every time. */
        shell_default(SIGCHLD);

        if (system_signal_mask(UL_SIGNAL_BLOCK, address_of blocked,
                               address_of previous_mask, 8) < 0)
                return string_report(log_error, 125, "timeout: cannot block relay signals\n");

        log_flush();
        bipolar parent = (bipolar)system_call(syscall(getpid));
        bipolar child = system_fork();

        if (child < 0)
        {
                system_signal_mask(UL_SIGNAL_SET_MASK,
                                   address_of previous_mask, null, 8);
                return string_report(log_error, 125, "timeout: cannot fork: %s\n",
                              file_reason(child));
        }

        if (child == 0)
        {
                /*
                        An ignored signal stays ignored through exec, and a
                        shell that starts something in the background hands it
                        an ignored interrupt. coreutils puts its own handler on
                        each of the signals it relays before it forks, and exec
                        turns a handler back into the default, so the command it
                        starts always answers them however it was started.
                        Blocking them and unblocking them again, as this does,
                        leaves an inherited ignore in place -- and a command
                        that ignores the signal outlives the very signal this
                        program exists to send it, to be killed a grace period
                        later for no reason. So hand the command the default
                        back, for the signals relayed and for the one asked for.
                */
                shell_default(SIGHUP);
                shell_default(SIGINT);
                shell_default(SIGQUIT);
                shell_default(SIGTERM);
                shell_default(PROCESS_SIGNAL_ALARM);
                shell_default(signal);

                system_signal_mask(UL_SIGNAL_SET_MASK,
                                   address_of previous_mask, null, 8);

                if (!foreground)
                        system_call_2(syscall(setpgid), 0, 0);

                /*
                        A timeout killed outright (SIGKILL) can send nothing,
                        so the kernel is asked to send the command the signal
                        for it when timeout dies (PR_SET_PDEATHSIG), as GNU's
                        does; one already gone by then is sent it at once --
                        to this process alone, since under --foreground its
                        group is the caller's whole job.
                */
                system_call_2(syscall(prctl), 1, (positive)signal);
                if ((bipolar)system_call(syscall(getppid)) != parent)
                        system_call_2(syscall(kill),
                                      (positive)system_call(syscall(getpid)),
                                      (positive)signal);

                b32 answer = process_tool_exec((string_address) "timeout",
                    program_argument_list() + taking.first);
                exit(answer);
        }

        if (!foreground)
                system_call_2(syscall(setpgid), (positive)child,
                              (positive)child);

        bipolar pidfd = system_call_2(syscall(pidfd_open), (positive)child, 0);
        bipolar signal_fd = system_call_4(
            syscall(signalfd4), (positive)(bipolar)-1,
            (positive)address_of blocked, 8, O_CLOEXEC);

        /* A kernel old enough to lack signalfd must not leave the signals
           blocked. pidfd still gives it the fast child wait; externally
           delivered signals then retain their inherited disposition. */
        if (signal_fd < 0)
                system_signal_mask(UL_SIGNAL_SET_MASK,
                                   address_of previous_mask, null, 8);

        positive deadline = 0;

        if (duration)
        {
                positive now = clock_monotonic_nanoseconds();
                deadline = duration > positive_max - now
                               ? positive_max : now + duration;
        }

        /*
                coreutils' cleanup(): the deadline passing and an ALRM from
                outside are both the timeout, and send the signal asked for;
                any other signal is sent on as it came. Whichever is first
                starts --kill-after's grace, after which the signal is KILL.
        */
        string_address command = program_argument((b32)taking.first);
        b32 sending = signal;
        bool timed_out = false;
        bipolar waited;

        for (;;)
        {
                b32 forwarded = 0;

                waited = process_timeout_wait((b32)child, pidfd, signal_fd,
                                               deadline, address_of status,
                                               address_of forwarded);
                if (waited < 0)
                {
                        process_timeout_cleanup(pidfd, signal_fd, previous_mask);
                        return string_report(log_error, 125, "timeout: failure while waiting for command\n");
                }
                if (waited == 1)
                        break;

                b32 sent = forwarded;

                if (!waited || forwarded == PROCESS_SIGNAL_ALARM)
                {
                        timed_out = true;
                        sent = sending;
                }
                process_timeout_signal((b32)child, pidfd, sent, foreground,
                                       verbose, command);

                if (escalate)
                {
                        positive now = clock_monotonic_nanoseconds();

                        sending = SIGKILL;
                        deadline = kill_after > positive_max - now
                                       ? positive_max : now + kill_after;
                        escalate = false;
                }
                else if (!waited)
                        deadline = 0;
        }

        process_timeout_cleanup(pidfd, signal_fd, previous_mask);

        if (timed_out && (status & 0x7f) == SIGKILL)
                return 128 + SIGKILL;
        if (timed_out && !preserve)
                return 124;
        return wait_status_code(status);
}

// script ----------------------------------------------------------
/* The terminal recorder and graphical terminal share pty.c's open floor. */
#define PROCESS_TERMINAL_ECHO 0x0008u
// This machine's: riscv64 has x86_64's value, and only arm64 moves it.
#define PROCESS_O_NOFOLLOW O_NOFOLLOW


typedef struct
{
        bipolar handle;
        string_address path;
        p64 bytes;
        bool owned;
        bool failed;
} process_script_log;

typedef struct
{
        process_script_log logs[3]; // Output (also combined), input, timing.
        process_script_log address_to out;
        process_script_log address_to in;
        bool append;
        bool force;
        bool flush;
        bool quiet;
        bool child_status;
        bool advanced;
        bool failed;
        p8 echo;
        positive last_event;
        positive began;
        p64 output_bytes;
} process_script_state;

static bool process_script_log_write(process_script_log address_to log_file,
                                     address_any bytes, positive length)
{
        if (!log_file || !length)
                return true;
        if (log_file->failed)
                return false;
        if (system_write_all((positive)log_file->handle, bytes, length) !=
            length)
        {
                log_file->failed = true;
                return false;
        }
        log_file->bytes += length;
        return true;
}

static bool process_script_log_text(process_script_log address_to log_file,
                                    string_address text)
{
        return process_script_log_write(log_file, text, string_length(text));
}

/* Open without truncation, prove the descriptor is not an existing hard
   link, and only then truncate it.  O_NOFOLLOW closes the pathname race for
   symlinks; --force deliberately requests util-linux's less restrictive
   policy. */
static bipolar process_script_log_open(process_script_log address_to log_file,
                                       string_address path, bool append,
                                       bool force)
{
        positive flags = (append ? FILE_APPEND
                                 : FILE_READ_WRITE | FILE_CREATE) |
                         O_CLOEXEC | O_NONBLOCK;
        if (!force)
                flags |= PROCESS_O_NOFOLLOW;

        bipolar handle = system_open_at_mode(AT_FDCWD, path, flags, 0666);
        if (handle < 0)
                return handle;

        file_facts facts;
        if (!file_look(handle, (string_address)"", AT_EMPTY_PATH,
                       address_of facts) ||
            ((facts.mode & MODE_FORMAT) != MODE_FILE &&
             (facts.mode & MODE_FORMAT) != MODE_CHARACTER) ||
            (!force && (facts.mode & MODE_FORMAT) == MODE_FILE &&
             facts.hard_links > 1))
        {
                system_close((positive)handle);
                return -ERROR_ACCESS;
        }
        if (!append && (facts.mode & MODE_FORMAT) == MODE_FILE &&
            system_call_2(syscall(ftruncate), (positive)handle, 0) < 0)
        {
                system_close((positive)handle);
                return -ERROR_INPUT_OUTPUT;
        }

        log_file->handle = handle;
        log_file->path = path;
        log_file->bytes = 0;
        log_file->owned = true;
        log_file->failed = false;
        return 0;
}

static fn process_script_log_close(process_script_log address_to log_file,
                                   bool flush)
{
        if (!log_file || !log_file->owned)
                return;
        if (flush)
                system_call_1(syscall(fsync), (positive)log_file->handle);
        system_close((positive)log_file->handle);
        log_file->owned = false;
}

/* Different path spellings can still name one inode.  Separate timing/input/
   output streams must not interleave through two descriptors to that inode. */
static bool process_script_log_same(process_script_log address_to one,
                                    process_script_log address_to two)
{
        file_facts first, second;

        if (!one || !two || one == two || !one->owned || !two->owned)
                return false;
        if (!file_look(one->handle, (string_address)"", AT_EMPTY_PATH,
                       address_of first) ||
            !file_look(two->handle, (string_address)"", AT_EMPTY_PATH,
                       address_of second))
                return true; /* The open descriptors were statable moments ago. */
        return file_same_identity(address_of first, address_of second);
}

static positive process_script_stamp(p8 address_to into, positive room)
{
        positive made = login_iso_time(into, room, file_now(), false, 0);
        if (made > 10)
                into[10] = ' ';
        return made;
}

static bool process_script_header_one(process_script_log address_to log_file,
                                      string_address stamp,
                                      string_address command,
                                      bool terminal)
{
        if (!log_file)
                return true;
        return process_script_log_text(log_file, "Script started on ") &&
               process_script_log_text(log_file, stamp) &&
               process_script_log_text(log_file, " [COMMAND=\"") &&
               process_script_log_text(log_file, command) &&
               process_script_log_text(
                   log_file, terminal
                                 ? (string_address)"\"]\n"
                                 : (string_address)"\" <not executed on terminal>]\n");
}

static bool process_script_footer_one(process_script_log address_to log_file,
                                      string_address stamp, b32 status)
{
        if (!log_file)
                return true;
        p8 number[24];
        positive length = positive_into(number, (positive)status);
        return process_script_log_text(log_file, "\nScript done on ") &&
               process_script_log_text(log_file, stamp) &&
               process_script_log_text(log_file,
                                       " [COMMAND_EXIT_CODE=\"") &&
               process_script_log_write(log_file, number, length) &&
               process_script_log_text(log_file, "\"]\n");
}

static bool process_script_timing_line(process_script_state address_to state,
                                       p8 stream, positive length)
{
        process_script_log address_to timing = state->logs + 2;
        if (timing->handle < 0)
                return true;
        if (!state->advanced && stream != 'O')
                return true;

        positive now = clock_monotonic_nanoseconds();
        positive elapsed = now >= state->last_event
                               ? now - state->last_event : 0;
        state->last_event = now;
        p8 line[96];
        positive used = 0;
        if (state->advanced)
        {
                line[used++] = stream;
                line[used++] = ' ';
        }
        fixed_decimal seconds = fixed_decimal_prepare(elapsed / 1000, 6, false, 0, 6, 0);
        used += fixed_decimal_into(line + used, sizeof(line) - used, address_of seconds);
        line[used++] = ' ';
        used += positive_into(line + used, length);
        line[used++] = '\n';
        return process_script_log_write(timing, line, used);
}

static bool process_script_timing_header(process_script_state address_to state,
                                         string_address key,
                                         string_address value)
{
        process_script_log address_to timing = state->logs + 2;
        if (timing->handle < 0 || !state->advanced)
                return true;
        return process_script_log_text(timing, "H 0.000000 ") &&
               process_script_log_text(timing, key) &&
               process_script_log_text(timing, " ") &&
               process_script_log_text(timing, value) &&
               process_script_log_text(timing, "\n");
}

static bool process_script_command_text(p8 address_to into, positive room,
                                        string_address command,
                                        positive command_first)
{
        positive used = 0;
        if (command)
        {
                positive length = string_length(command);
                if (length >= room)
                        return false;
                memory_copy_end(into, command, length);
                return true;
        }

        positive count = (positive)program_argument_count();
        for (positive at = command_first; at < count; at++)
        {
                string_address word = program_argument((b32)at);
                positive length = string_length(word);
                /*      The separator is part of what this word costs, so it
                        is counted before the room is judged: measured after
                        it, a join landing exactly on `room` passed the test
                        and the terminator below went one byte past `into`. */
                positive needed = length + (used ? 1 : 0);
                if (needed >= room - used)
                        return false;
                if (used)
                        into[used++] = ' ';
                memory_copy(into + used, word, length);
                used += length;
        }
        if (!used)
        {
                string_address shell = file_environment(
                    (string_address)"SHELL");
                if (!shell || !*shell)
                        shell = (string_address)"/bin/sh";
                positive length = string_length(shell);
                if (length >= room)
                        return false;
                memory_copy(into, shell, length);
                used = length;
        }
        into[used] = end;
        return true;
}

/* Whether a log names a regular file, the only kind two logs may not
   share. */
static bool process_script_log_regular(process_script_log address_to log_file)
{
        file_facts facts;

        return log_file && log_file->handle >= 0 &&
               file_look(log_file->handle, (string_address)"", AT_EMPTY_PATH,
                         address_of facts) &&
               (facts.mode & MODE_FORMAT) == MODE_FILE;
}

static b32 process_script_child(string_address command,
                                positive command_first, b32 master, b32 slave,
                                bipolar signal_fd, bipolar pidfd,
                                positive previous_mask)
{
        system_signal_mask(UL_SIGNAL_SET_MASK, address_of previous_mask, null,
                           8);
        bipolar prepared = process_pty_child_setup(master, slave, signal_fd,
                                                   pidfd);
        if (prepared < 0)
                return string_report(log_error, 1, "script: cannot establish pseudo-terminal: %s\n",
                              file_reason(prepared));

        // The shell is named by its last component, as util-linux names
        // it, which is how bash spells itself in its diagnostics.
        string_address shell = file_environment((string_address)"SHELL");
        if (!shell || !*shell)
                shell = (string_address)"/bin/sh";
        string_address shell_name = file_last_component(shell);
        if (command)
        {
                string_address words[] = {shell_name, (string_address)"-c",
                                          command, null};
                return process_tool_exec_environment(
                    (string_address)"script", words, file_environment_all(),
                    file_environment((string_address)"PATH"));
        }
        if (command_first < (positive)program_argument_count())
                return process_tool_exec_environment(
                    (string_address)"script",
                    program_argument_list() + command_first,
                    file_environment_all(),
                    file_environment((string_address)"PATH"));

        string_address words[] = {shell_name, (string_address)"-i", null};
        return process_tool_exec_environment(
            (string_address)"script", words, file_environment_all(),
            file_environment((string_address)"PATH"));
}

static bool process_script_payload(process_script_state address_to state,
                                   p8 stream, p8 address_to bytes,
                                   positive length)
{
        process_script_log address_to destination = stream == 'O'
                                                        ? state->out
                                                        : state->in;
        if (stream == 'O')
        {
                if (system_write_all(1, bytes, length) != length)
                        return false;
                state->output_bytes += length;
        }
        if (!process_script_log_write(destination, bytes, length))
                return false;
        return process_script_timing_line(state, stream, length);
}

static b32 process_script_record(process_script_state address_to state,
                                 string_address command,
                                 positive command_first,
                                 string_address command_text)
{
        b32 master = -1, slave = -1;
        bipolar opened = process_pty_open(address_of master, address_of slave,
                                          true);
        if (opened < 0)
                return string_report(log_error, 1, "script: cannot open pseudo-terminal: %s\n",
                              file_reason(opened));

        winsize size;
        if (system_control(0, TIOCGWINSZ, address_of size) >= 0)
                system_control(slave, TIOCSWINSZ, address_of size);

        if (state->echo)
        {
                terminal_modes modes;
                if (system_control(slave, PTY_TCGETS, address_of modes) >= 0)
                {
                        if (state->echo == 1)
                                modes.behaviour |= PROCESS_TERMINAL_ECHO;
                        else
                                modes.behaviour &= ~PROCESS_TERMINAL_ECHO;
                        system_control(slave, PTY_TCSETS, address_of modes);
                }
        }

        positive blocked = PROCESS_TIMEOUT_SIGNALS;
        positive previous_mask = 0;
        if (system_signal_mask(UL_SIGNAL_BLOCK, address_of blocked,
                               address_of previous_mask, 8) < 0)
        {
                system_close((positive)slave);
                system_close((positive)master);
                return string_report(log_error, 1, "script: cannot block relay signals\n");
        }

        bipolar signal_fd = system_call_4(
            syscall(signalfd4), (positive)(bipolar)-1,
            (positive)address_of blocked, 8, O_CLOEXEC | O_NONBLOCK);
        if (signal_fd < 0)
        {
                system_signal_mask(UL_SIGNAL_SET_MASK,
                                   address_of previous_mask, null, 8);
                system_close((positive)slave);
                system_close((positive)master);
                return string_report(log_error, 1, "script: cannot create signal descriptor: %s\n",
                              file_reason(signal_fd));
        }
        log_flush();
        bipolar child = system_fork();
        if (child < 0)
        {
                process_timeout_cleanup(-1, signal_fd, previous_mask);
                system_close((positive)slave);
                system_close((positive)master);
                return string_report(log_error, 1, "script: cannot fork: %s\n",
                              file_reason(child));
        }
        if (!child)
                exit(process_script_child(command, command_first, master,
                                          slave, signal_fd, -1,
                                          previous_mask));

        system_close((positive)slave);
        bipolar pidfd = system_call_2(syscall(pidfd_open), (positive)child, 0);

        state->began = state->last_event = clock_monotonic_nanoseconds();
        static p8 input[4096], output[16384];
        positive input_at = 0, input_length = 0;
        bool input_end = false, eot = false, master_end = false;
        bool child_done = false, failed = false;
        /*      An end of file on our own input becomes the terminal's
                end-of-file character, which only means that to a command
                that has already taken the terminal and is waiting on it. So
                it is not written the moment our input runs out but only
                after the session has fallen quiet, and again each time it
                falls quiet after that, a few times over: a shell that reads
                its line through readline takes the terminal back when it
                accepts the line and throws away whatever was typed ahead, so
                one written while the line was still being read is gone.
                Waiting for the quiet rather than for the first word out is
                also what keeps the transcript the same from run to run --
                offering it into the middle of the session's own writing puts
                the byte in a different place each time. */
        positive offered = 0;
        positive status = 0;

        /*
                The first of our input goes into the terminal before the
                command has finished starting. A terminal echoes what it is
                given as it is given it, so input handed over after the
                command's first word is recorded after that word, and which of
                the two comes first is otherwise left to how the two processes
                are scheduled -- the same session recorded twice then differs.
                The read does not wait, so a terminal on the other side of our
                own input does not hold the recorder here.
        */
        {
                system_poll_descriptor first[2] = {
                    {0, SYSTEM_POLL_READ, 0},
                    {master, SYSTEM_POLL_WRITE, 0}};
                timespec none = {0, 0};

                if (system_poll_wait(first, 2, address_of none, null) > 0 &&
                    first[0].returned)
                {
                        bipolar got = system_read_once(0, input, sizeof(input));

                        if (got > 0)
                        {
                                input_length = (positive)got;

                                /* Only where the terminal has room for it: a
                                   command that never reads its input leaves a
                                   full one, and a write that waits here waits
                                   before the loop that empties the other side.
                                   Held back, it goes out on the first pass. */
                                bipolar wrote = first[1].returned
                                                    ? system_write_once(
                                                          (positive)master, input,
                                                          input_length)
                                                    : 0;

                                if (wrote > 0)
                                {
                                        input_at = (positive)wrote;

                                        if (!process_script_payload(
                                                state, 'I', input,
                                                (positive)wrote))
                                        {
                                                failed = true;
                                                master_end = true;
                                        }
                                }
                        }
                        else if (got != -UL_ERROR_AGAIN &&
                                 got != UL_ERROR_INTERRUPTED)
                        {
                                input_end = true;
                                eot = true;
                        }
                }
        }

        while (!master_end)
        {
                system_poll_descriptor waited[4];
                positive count = 0;
                positive master_index = count;
                bool sending = input_at < input_length;
                waited[count++] = (system_poll_descriptor){
                    master, (b16)(SYSTEM_POLL_READ |
                                  (sending ? SYSTEM_POLL_WRITE : 0)), 0};
                positive input_index = positive_max;
                if (!input_end && input_at == input_length)
                {
                        input_index = count;
                        waited[count++] = (system_poll_descriptor){
                            0, SYSTEM_POLL_READ, 0};
                }
                positive signal_index = positive_max;
                if (signal_fd >= 0)
                {
                        signal_index = count;
                        waited[count++] = (system_poll_descriptor){
                            (b32)signal_fd, SYSTEM_POLL_READ, 0};
                }
                positive pid_index = positive_max;
                if (pidfd >= 0 && !child_done)
                {
                        pid_index = count;
                        waited[count++] = (system_poll_descriptor){
                            (b32)pidfd, SYSTEM_POLL_READ, 0};
                }

                timespec drain = {1, 0};
                timespec quiet = {0, 250000000};
                bool offering = input_end && !child_done && offered < 4;
                timespec address_to timeout =
                    child_done ? address_of drain
                               : (offering ? address_of quiet : null);
                bipolar ready = system_poll_wait(
                    waited, count, timeout, null);
                if (ready < 0)
                {
                        if (ready == UL_ERROR_INTERRUPTED)
                                continue;
                        failed = true;
                        break;
                }
                if (!ready)
                {
                        if (offering)
                        {
                                //      Quiet, and our own input is spent:
                                //      offer the end-of-file.
                                input[0] = 4;
                                input_at = 0;
                                input_length = 1;
                                offered++;
                                continue;
                        }
                        /* A descendant retaining the slave must not hold the
                           recorder forever after the command is reaped. */
                        process_timeout_signal((b32)child, pidfd, SIGHUP, false,
                                               false, command_text);
                        master_end = true;
                        break;
                }

                if (input_index != positive_max &&
                    waited[input_index].returned)
                {
                        bipolar got = system_read_once(0, input, sizeof(input));
                        if (got > 0)
                        {
                                input_at = 0;
                                input_length = (positive)got;
                        }
                        else if (got != -UL_ERROR_AGAIN &&
                                 got != UL_ERROR_INTERRUPTED)
                        {
                                input_end = true;
                                eot = true;
                        }
                }

                if (waited[master_index].returned & SYSTEM_POLL_WRITE)
                {
                        if (sending)
                        {
                                bipolar wrote = system_write_once(
                                    (positive)master, input + input_at,
                                    input_length - input_at);
                                if (wrote > 0)
                                {
                                        if (!eot && !process_script_payload(
                                                state, 'I', input + input_at,
                                                (positive)wrote))
                                        {
                                                failed = true;
                                                break;
                                        }
                                        input_at += (positive)wrote;
                                }
                                else if (wrote && wrote != -UL_ERROR_AGAIN &&
                                         wrote != UL_ERROR_INTERRUPTED)
                                        //      A write that placed nothing
                                        //      is a write to try again, not
                                        //      a session that has ended:
                                        //      closing the terminal here
                                        //      hangs up on a command that is
                                        //      still running and turns its
                                        //      status into a signal.
                                        master_end = true;
                        }
                }

                if (waited[master_index].returned &
                    (SYSTEM_POLL_READ | SYSTEM_POLL_HANGUP |
                     SYSTEM_POLL_ERROR))
                {
                        for (;;)
                        {
                                bipolar got = system_read_once(
                                    (positive)master, output, sizeof(output));
                                if (got > 0)
                                {
                                        if (!process_script_payload(
                                                state, 'O', output,
                                                (positive)got))
                                        {
                                                failed = true;
                                                break;
                                        }
                                        continue;
                                }
                                if (!got || (got != -UL_ERROR_AGAIN &&
                                             got != UL_ERROR_INTERRUPTED))
                                        master_end = true;
                                break;
                        }
                        if (failed)
                                break;
                }

                if (signal_index != positive_max &&
                    waited[signal_index].returned)
                {
                        positive information[16];
                        bipolar got = system_read_retry(
                            (positive)signal_fd, information,
                            sizeof(information));
                        if (got >= (bipolar)sizeof(p32))
                        {
                                b32 number = (b32)(p32)information[0];
                                if (number != SIGCHLD)
                                        process_timeout_signal((b32)child, pidfd,
                                                               number, false,
                                                               false,
                                                               command_text);
                        }
                }

                if (!child_done &&
                    ((pid_index != positive_max && waited[pid_index].returned) ||
                     (signal_index != positive_max &&
                      waited[signal_index].returned)))
                {
                        bipolar reaped = system_wait4_retry(
                            (b32)child, address_of status, 1, null);
                        if (reaped == child)
                                child_done = true;
                }
        }

        if (failed && !child_done)
                process_timeout_signal((b32)child, pidfd, SIGKILL, false, false,
                                       command_text);
        if (!child_done)
        {
                positive deadline = clock_monotonic_nanoseconds() +
                                    1000000000;
                while (!failed)
                {
                        b32 forwarded = 0;
                        bipolar waited = process_timeout_wait(
                            (b32)child, pidfd, signal_fd, deadline,
                            address_of status, address_of forwarded);
                        if (waited != 2)
                        {
                                child_done = waited == 1;
                                break;
                        }
                        process_timeout_signal((b32)child, pidfd, forwarded, false,
                                               false, command_text);
                }
                if (!child_done)
                {
                        process_timeout_signal((b32)child, pidfd, SIGKILL, false,
                                               false, command_text);
                        if (system_wait4_retry((b32)child, address_of status,
                                               0, null) < 0)
                                failed = true;
                        else
                                child_done = true;
                }
        }
        /*      The terminal is let go only once the command has been waited
                for. Closing the master hangs up on whatever is still in the
                pty's foreground group, and the read that ended the loop can
                race the command's own last breath: closing first turned a
                command that exited zero into one that died of the hangup,
                about once in forty runs. */
        system_close((positive)master);
        process_timeout_cleanup(pidfd, signal_fd, previous_mask);

        if (state->flush)
        {
                if (state->out)
                        system_call_1(syscall(fsync),
                                      (positive)state->out->handle);
                if (state->in && state->in != state->out)
                        system_call_1(syscall(fsync),
                                      (positive)state->in->handle);
                if (state->logs[2].handle >= 0)
                        system_call_1(syscall(fsync),
                                      (positive)state->logs[2].handle);
        }
        state->failed = failed;
        return failed ? 1 : wait_status_code(status);
}

static const argument_option process_script_options[] = {
    {"log-in", 'I', ARGUMENT_REQUIRED},
    {"log-out", 'O', ARGUMENT_REQUIRED},
    {"log-io", 'B', ARGUMENT_REQUIRED},
    {"log-timing", 'T', ARGUMENT_REQUIRED},
    {"timing", 't', ARGUMENT_OPTIONAL | ARGUMENT_LONG_OPTIONAL | ARGUMENT_STICKY},
    {"logging-format", 'm', ARGUMENT_REQUIRED},
    {"append", 'a'},
    {"command", 'c', ARGUMENT_REQUIRED},
    {"return", 'e'},
    {"flush", 'f'},
    {"force", 'X'},
    {"echo", 'E', ARGUMENT_REQUIRED},
    {"output-limit", 'o', ARGUMENT_REQUIRED},
    {"quiet", 'q'},
    {"help", 'h'},
    {"version", 'V'},
    {null},
};

static b32 process_script()
{
        file_taking taking = {
            .program = (string_address)"script",
            .options = process_script_options,
        };
        positive count = (positive)program_argument_count();
        b32 answer;
        if (ul_options_done(address_of taking,
                "[options] [file] [-- command [argument...]]", address_of answer))
                return answer;
        tools_refuse(taking.flags & FILE_FLAG('o'), "script", "output limits are not supported");

        positive separator = count;
        for (positive at = 1; at < count; at++)
                if (string_equals(program_argument((b32)at),
                                  (string_address)"--"))
                {
                        separator = at;
                        break;
                }

        /*      The log operand is whatever stands before the separator; a
                bare -- at the end names no command at all, which is why the
                reference takes --command beside one. */
        positive tail = separator < count ? separator : count;
        positive operands = tail > taking.first ? tail - taking.first : 0;
        string_address command = file_option_value(address_of taking, 'c');
        positive command_first = count;
        string_address positional = operands ? program_argument((b32)taking.first)
                                             : null;
        if (command)
        {
                tools_refuse(separator + 1 < count || operands > 1, "script", "--command cannot be combined with -- command");
        }
        else if (separator < count)
        {
                // A bare -- with nothing after it means the shell itself.
                tools_refuse(operands > 1, "script", "invalid command operands");
                command_first = separator + 1;
        }
        else if (operands > 1)
                return string_report(log_error, 1, "%s: %s\n", "script", "extra operand");

        bool has_io = (taking.flags & (FILE_FLAG('I') | FILE_FLAG('O') |
                                       FILE_FLAG('B'))) != 0;
        tools_refuse(positional && has_io, "script", "positional log conflicts with explicit log");
        //      --log-timing and --timing name the same file by two spellings,
        //      and the reference refuses the pair rather than choosing.
        tools_refuse((taking.flags & FILE_FLAG('T')) && (taking.flags & FILE_FLAG('t')), "script", "options --log-timing and --timing cannot be combined");

        process_script_state state;
        memory_fill(address_of state, 0, sizeof(state));
        for (positive i = 0; i < array_count(state.logs); i++)
                state.logs[i].handle = -1;
        state.append = (taking.flags & FILE_FLAG('a')) != 0;
        state.force = (taking.flags & FILE_FLAG('X')) != 0;
        state.flush = (taking.flags & FILE_FLAG('f')) != 0;
        state.quiet = (taking.flags & FILE_FLAG('q')) != 0;
        state.child_status = (taking.flags & FILE_FLAG('e')) != 0;

        string_address format = file_option_value(address_of taking, 'm');
        state.advanced = (taking.flags & (FILE_FLAG('I') | FILE_FLAG('B'))) != 0;
        bool classic_asked = false;
        if (format)
        {
                if (string_equals(format, (string_address)"advanced"))
                        state.advanced = true;
                else if (string_equals(format, (string_address)"classic"))
                {
                        classic_asked = true;
                        state.advanced = false;
                }
                else
                        return string_report(log_error, 1, "%s: %s\n", "script", "unknown logging format");
        }

        string_address echo = file_option_value(address_of taking, 'E');
        if (echo)
        {
                if (string_equals(echo, (string_address)"always"))
                        state.echo = 1;
                else if (string_equals(echo, (string_address)"never"))
                        state.echo = 2;
                else if (!string_equals(echo, (string_address)"auto"))
                        return string_report(log_error, 1, "%s: %s\n", "script", "unknown echo mode");
        }

        string_address output_path = null;
        string_address input_path = null;
        string_address combined_path = file_option_value(address_of taking, 'B');
        if (combined_path)
        {
                // --log-io names both streams; a separate log for one of
                // them, wherever it stands, takes that stream over.
                output_path = input_path = combined_path;
                if (file_option_value(address_of taking, 'O'))
                {
                        output_path = file_option_value(address_of taking, 'O');
                        combined_path = null;
                }
                if (file_option_value(address_of taking, 'I'))
                {
                        input_path = file_option_value(address_of taking, 'I');
                        combined_path = null;
                }
        }
        else
        {
                output_path = file_option_value(address_of taking, 'O');
                input_path = file_option_value(address_of taking, 'I');
                if (!has_io)
                        output_path = positional ? positional
                                                 : (string_address)"typescript";
        }
        string_address timing_path = file_option_value(address_of taking, 'T');
        string_address old_timing = file_option_value(address_of taking, 't');
        if (!timing_path && old_timing)
                timing_path = old_timing;
        /*      util-linux takes the same path for a log and the timing, and
                for both logs -- the typical case being /dev/null everywhere.
                What it minds is one regular file written from two places,
                which the check below the opens answers with the objects
                themselves rather than with the spelling of their names. */

        /*      A classic timing file has one column of byte counts and no
                column saying which stream they came from, so it cannot time
                two streams at once. Logging both without asking for a timing
                file is no conflict, and the reference allows it. */
        if (classic_asked && output_path && input_path &&
            (timing_path || (taking.flags & FILE_FLAG('t'))))
                return string_report(log_error, 1, "%s: %s\n", "script",
                                     "log multiple streams is mutually exclusive with 'classic' format");

        string_address paths[] = {
            output_path, combined_path ? null : input_path, timing_path};
        answer = 1;
        bool close_flush = false;
        for (positive i = 0; i < array_count(paths); i++)
        {
                if (paths[i])
                {
                        bipolar opened = process_script_log_open(state.logs + i,
                            paths[i], state.append, state.force);
                        if (opened < 0)
                        {
                                string_format(log_error, "script: %s: %s\n",
                                              paths[i], file_reason(opened));
                                goto close_logs;
                        }
                }
        }
        state.out = output_path ? state.logs : null;
        state.in = combined_path ? state.out : input_path ? state.logs + 1 : null;
        process_script_log address_to timing = state.logs + 2;

        if (!timing_path && (taking.flags & FILE_FLAG('t')) &&
            (taking.bare & FILE_FLAG('t')))
        {
                timing->handle = 2;
                timing->path = (string_address)"/dev/stderr";
        }

        // Two names for one device (the usual /dev/null twice) are no
        // conflict; util-linux minds only a shared regular file.
        if (process_script_log_regular(state.out) &&
            (process_script_log_same(state.out, state.in) ||
             process_script_log_same(state.out, timing)))
        {
                string_report(log_error, 1, "%s: %s\n", "script", "log files must name distinct objects");
                goto close_logs;
        }

        p8 display[4096];
        if (!process_script_command_text(display, sizeof(display), command,
                                         command_first))
        {
                string_report(log_error, 1, "%s: %s\n", "script", "command is too long");
                goto close_logs;
        }
        // util-linux hands a -- command to the shell as one line, the
        // words joined by spaces, exactly as --command does.
        if (!command && command_first < count)
                command = display;

        p8 stamp[64];
        process_script_stamp(stamp, sizeof(stamp));
        bool terminal = system_control(0, TIOCGWINSZ,
                                       address_of (winsize){0}) >= 0;
        bool good = process_script_header_one(state.out, stamp, display,
                                              terminal);
        if (state.in != state.out)
                good &= process_script_header_one(state.in, stamp, display,
                                                  terminal);
        good &= process_script_timing_header(address_of state,
                                             (string_address)"START_TIME",
                                             stamp);
        string_address shell = file_environment((string_address)"SHELL");
        if (!shell || !*shell)
                shell = (string_address)"/bin/sh";
        good &= process_script_timing_header(address_of state,
                                             (string_address)"SHELL", shell);
        good &= process_script_timing_header(address_of state,
                                             (string_address)"COMMAND", display);
        if (timing->path)
                good &= process_script_timing_header(
                    address_of state, (string_address)"TIMING_LOG",
                    timing->path);
        if (output_path)
                good &= process_script_timing_header(
                    address_of state, (string_address)"OUTPUT_LOG", output_path);
        if (input_path)
                good &= process_script_timing_header(
                    address_of state, (string_address)"INPUT_LOG", input_path);

        if (!state.quiet)
        {
                string_format(log, "Script started");
                if (output_path)
                        string_format(log, ", output log file is '%s'",
                                      output_path);
                if (input_path)
                        string_format(log, ", input log file is '%s'",
                                      input_path);
                if (timing->path)
                        string_format(log, ", timing file is '%s'",
                                      timing->path);
                string_format(log, ".\n");
                log_flush();
        }

        b32 status = good ? process_script_record(address_of state, command,
                                                   command_first, display) : 1;
        process_script_stamp(stamp, sizeof(stamp));
        good &= process_script_footer_one(state.out, stamp, status);
        if (state.in != state.out)
                good &= process_script_footer_one(state.in, stamp, status);

        if (timing->handle >= 0 && state.advanced)
        {
                positive now = clock_monotonic_nanoseconds();
                positive elapsed = now >= state.began ? now - state.began : 0;
                p8 value[48];
                fixed_decimal seconds = fixed_decimal_prepare(elapsed / 1000, 6, false, 0, 6, 0);
                value[fixed_decimal_into(value, sizeof(value) - 1, address_of seconds)] = end;
                good &= process_script_timing_header(
                    address_of state, (string_address)"DURATION", value);
                positive_into_string(value, (positive)status);
                good &= process_script_timing_header(
                    address_of state, (string_address)"EXIT_CODE", value);
        }

        if (!state.quiet)
        {
                string_format(log, "Script done.\n");
                log_flush();
        }
        if (good && !state.failed)
                answer = state.child_status ? status : 0;
        close_flush = state.flush;
close_logs:
        for (positive i = 0; i < array_count(state.logs); i++)
        {
                if (state.logs[i].failed)
                        answer = 1;
                process_script_log_close(state.logs + i, close_flush);
        }
        return answer;
}

// scriptreplay ----------------------------------------------------
/* Two small buffered readers keep timing and payload progress independent.
   A timing line has a hard ceiling; payload lengths remain streaming and are
   never allocated from an untrusted count. */
#define PROCESS_REPLAY_BLOCK 4096
#define PROCESS_REPLAY_LINE 1024
#define PROCESS_REPLAY_MAX_DELAY (3600ul * 1000000000ul)

typedef struct
{
        byte_input from;
        bool failed;
        p8 bytes[PROCESS_REPLAY_BLOCK];
} process_replay_reader;

static bipolar process_replay_open(process_replay_reader address_to reader,
                                   string_address path)
{
        reader->failed = false;
        byte_input_open_fd(address_of reader->from,
                           system_open_at(AT_FDCWD, path, FILE_READ | O_CLOEXEC),
                           reader->bytes, sizeof(reader->bytes));
        return reader->from.fd;
}

static bipolar process_replay_fill(process_replay_reader address_to reader)
{
        bipolar got = byte_input_need(address_of reader->from, 1);
        reader->failed |= got < 0;
        return got;
}

static bipolar process_replay_line(process_replay_reader address_to reader,
                                   p8 address_to line, positive room)
{
        positive used = 0;
        for (;;)
        {
                bipolar got = process_replay_fill(reader);
                if (got <= 0)
                {
                        if (!got && !used)
                                return 0;
                        if (got < 0)
                                return -1;
                        break;
                }
                p8 address_to start = reader->from.buf + reader->from.at;
                p8 address_to newline = memory_first_of(start, '\n', (positive)got);
                positive take = newline ? (positive)(newline - start) : (positive)got;
                if (take >= room - used)
                {
                        reader->from.at += room - used;
                        reader->failed = true;
                        return -1;
                }
                memory_copy(line + used, start, take);
                used += take;
                reader->from.at += take + (newline != null);
                if (newline)
                        break;
        }
        if (used && line[used - 1] == '\r')
                used--;
        line[used] = end;
        return (bipolar)used + 1;
}

static bool process_replay_skip_header(
    process_replay_reader address_to reader)
{
        /*      util-linux drops the transcript's opening line whatever it
                holds -- the "Script started on" banner is only its usual
                tenant -- and a file with no newline in it is that one line
                entire.  Matching the banner instead replayed every log whose
                first line is payload one entry out of step. */
        positive used = 0;
        while (used < (positive)1 << 20)
        {
                bipolar got = process_replay_fill(reader);
                if (got <= 0)
                        return got == 0;
                p8 address_to start = reader->from.buf + reader->from.at;
                positive room = (positive)got;
                positive take = memory_span_without_byte(start, '\n', room);
                reader->from.at += take;
                used += take;
                if (take < room)
                {
                        reader->from.at++;
                        return true;
                }
        }
        reader->failed = true;
        return false;
}

/*      What one timing line said, answered the way the reference's
        `sscanf("%lf %zu")` answers: nothing converted means this is not a
        timing file at all, one field converted means it is one and this line
        is broken. The first tells the opening line from a file of some other
        shape, which util-linux replays as empty and calls a success; the
        second is the "timing file error" it refuses on. */
#define PROCESS_REPLAY_NOT_TIMING 0
#define PROCESS_REPLAY_LINE_READ 1
#define PROCESS_REPLAY_LINE_BROKEN 2

static b32 process_replay_timing(string_address line, bool address_to advanced,
                                 p8 address_to stream,
                                 positive address_to delay,
                                 positive address_to count,
                                 string_address address_to text)
{
        string_address at = line;
        p8 kind = 0;
        address_to text = null;
        if (string_get(at + 1) == ' ' &&
            (string_get(at) == 'I' || string_get(at) == 'O' ||
             string_get(at) == 'S' || string_get(at) == 'H'))
        {
                address_to advanced = true;
                kind = string_get(at);
                at += 2;
        }

        at += string_span_of_set(at, " ");
        string_address gap = string_first_of(at, ' ');
        if (!gap)
                return PROCESS_REPLAY_NOT_TIMING;
        address_to gap = end;
        positive waited;
        bool okay = file_duration_read(at, false, address_of waited);
        address_to gap = ' ';
        if (!okay)
                return PROCESS_REPLAY_NOT_TIMING;
        at = gap + 1;

        /*      Signal and header rows carry a name and its data rather than a
                byte count: they consume no payload, and the stream that asked
                for them prints the pair. */
        positive bytes = 0;
        if (kind == 'S' || kind == 'H')
                address_to text = at;
        else
        {
                at += string_span_of_set(at, " ");
                if (!file_unsigned_decimal(at, address_of bytes))
                        return PROCESS_REPLAY_LINE_BROKEN;
        }
        address_to stream = kind;
        address_to delay = waited;
        address_to count = bytes;
        return PROCESS_REPLAY_LINE_READ;
}

static bool process_replay_sleep(positive delay, positive divisor,
                                 bool limited, positive maximum)
{
        //      A divisor of zero is taken, as the reference takes it, and
        //      scales nothing: there is no dividing by it.
        if (divisor && divisor != 1000000000)
        {
                positive whole = delay / divisor;
                positive remainder = delay % divisor;
                if (remainder > positive_max / 1000000000)
                        return false;
                positive fraction = remainder * 1000000000 / divisor;
                if (whole > (positive_max - fraction) / 1000000000)
                        return false;
                delay = whole * 1000000000 + fraction;
        }
        if (limited && delay > maximum)
                delay = maximum;
        if (delay > PROCESS_REPLAY_MAX_DELAY)
                return false;
        timespec span = {delay / 1000000000, delay % 1000000000};
        while (span.tv_sec || span.tv_nsec)
        {
                timespec left = {0, 0};
                bipolar answer = system_call_2(
                    syscall(nanosleep), (positive)address_of span,
                    (positive)address_of left);
                if (answer >= 0)
                        break;
                if (answer != UL_ERROR_INTERRUPTED)
                        return false;
                span = left;
        }
        return true;
}

static bool process_replay_payload(process_replay_reader address_to reader,
                                   positive length, bool emit, bool carriage)
{
        while (length)
        {
                if (process_replay_fill(reader) <= 0)
                {
                        reader->failed = true;
                        return false;
                }
                positive chunk = min(length, reader->from.have - reader->from.at);
                if (emit)
                {
                        p8 address_to past = reader->from.buf + reader->from.at + chunk;

                        for (p8 address_to cr = carriage ? memory_first_of(past - chunk, '\r', chunk)
                                                         : null;
                             cr; cr = memory_first_of(cr + 1, '\r', (positive)(past - cr - 1)))
                                *cr = '\n';
                        if (system_write_all(1, reader->from.buf + reader->from.at,
                                             chunk) != chunk)
                                return false;
                }
                reader->from.at += chunk;
                length -= chunk;
        }
        return true;
}

/*      The signal and info streams print the pair a row carries rather than
        bytes out of a log: `NAME DATA` for a signal, the name in a
        ten-column field and then `: DATA` for a header. DATA still holds the
        space that separated it from the name, which is where the reference's
        second space in `SIGWINCH  ROWS=24 COLS=80` comes from. */
static bool process_replay_named(string_address name, positive length,
                                 string_address data, bool padded)
{
        p8 room[PROCESS_REPLAY_LINE + 32];
        positive used = 0;
        if (padded)
        {
                used = difference_or_zero(10, length);
                memory_fill(room, ' ', used);
        }
        memory_copy(room + used, name, length);
        used += length;
        if (padded)
                room[used++] = ':';
        room[used++] = ' ';
        positive rest = string_length(data);
        if (used + rest >= sizeof(room))
                rest = sizeof(room) - used - 1;
        memory_copy(room + used, data, rest);
        used += rest;
        room[used++] = '\n';
        return system_write_all(1, room, used) == (bipolar)used;
}

static bool process_replay_say(string_address text, bool padded)
{
        string_address gap = string_first_of(text, ' ');
        positive length = gap ? (positive)(gap - text) : string_length(text);
        return process_replay_named(text, length, gap ? gap : text + length,
                                    padded);
}

/*      Every value scriptreplay reads is read where it is written, the way
        the reference reads it inside its own option loop: a later good value
        does not rescue an earlier bad one, so the check cannot wait until
        the last value of each letter is known. */
static bool process_replay_number(p8 letter, string_address value)
{
        positive scratch;
        if (!value)
                return true;
        if (letter == 'd' || letter == 'm')
        {
                if (file_duration_read(value, false, address_of scratch))
                        return true;
                return string_report(log_error, false, letter == 'd'
                                  ? "scriptreplay: failed to parse number: '%s'\n"
                                  : "scriptreplay: failed to parse maximal delay argument: '%s'\n",
                              value);
        }
        if (letter == 'x')
        {
                if (string_equals(value, (string_address)"out") ||
                    string_equals(value, (string_address)"in") ||
                    string_equals(value, (string_address)"signal") ||
                    string_equals(value, (string_address)"info"))
                        return true;
                return string_report(log_error, false, "scriptreplay: unsupported stream name: '%s'\n", value);
        }
        if (letter == 'c')
        {
                if (string_equals(value, (string_address)"auto") ||
                    string_equals(value, (string_address)"never") ||
                    string_equals(value, (string_address)"always"))
                        return true;
                return string_report(log_error, false, "scriptreplay: unsupported mode name: '%s'\n", value);
        }
        return true;
}

static const argument_option process_scriptreplay_options[] = {
    {"timing", 't', ARGUMENT_REQUIRED, 1},
    {"log-timing", 'T', ARGUMENT_REQUIRED, 1},
    {"log-in", 'I', ARGUMENT_REQUIRED},
    {"log-out", 'O', ARGUMENT_REQUIRED},
    {"log-io", 'B', ARGUMENT_REQUIRED},
    {"typescript", 's', ARGUMENT_REQUIRED},
    {"summary", 'S'},
    {"divisor", 'd', ARGUMENT_REQUIRED},
    {"maxdelay", 'm', ARGUMENT_REQUIRED},
    {"stream", 'x', ARGUMENT_REQUIRED},
    {"cr-mode", 'c', ARGUMENT_REQUIRED},
    {"help", 'h'},
    {"version", 'V'},
    {null},
};

static b32 process_scriptreplay()
{
        //      -t and -T name the same file, so the last of them written wins.
        p8 timing_letter = 0;
        file_taking taking = {
            .program = (string_address)"scriptreplay",
            .options = process_scriptreplay_options,
            .selection = address_of timing_letter,
            .seen = process_replay_number,
        };
        positive argument_count = (positive)program_argument_count();
        b32 answer;
        if (ul_options_done(address_of taking,
                "[options] timingfile [typescript [divisor]]", address_of answer))
                return answer;
        tools_refuse(taking.flags & FILE_FLAG('S'), "scriptreplay", "summary mode is not supported");

        string_address timing_path = timing_letter
            ? file_option_value(address_of taking, timing_letter)
            : null;
        positive operand = taking.first;
        if (!timing_path)
        {
                tools_refuse(operand >= argument_count, "scriptreplay", "missing timing file");
                timing_path = program_argument((b32)operand++);
        }

        /*      --log-io is the fallback for both streams and not a claim on
                either: --log-in and --log-out name their own log whichever
                side of it they were written, which is what the reference's
                per-stream association does. Naming any log at all also fills
                the transcript's operand slot, so the next operand there is
                the divisor. */
        string_address out_option = file_option_value(address_of taking, 'O');
        if (out_option && file_option_value(address_of taking, 's'))
        {
                return string_report(log_error, 1,
                                     "scriptreplay: options --log-out and --typescript cannot be combined\n");
        }
        if (!out_option)
                out_option = file_option_value(address_of taking, 's');
        string_address in_option = file_option_value(address_of taking, 'I');
        string_address both_path = file_option_value(address_of taking, 'B');
        bool log_named = (taking.flags & (FILE_FLAG('I') | FILE_FLAG('O') |
                                          FILE_FLAG('s') | FILE_FLAG('B'))) != 0;
        string_address operand_log = null;
        if (!log_named && operand < argument_count)
                operand_log = program_argument((b32)operand++);

        string_address out_path = out_option ? out_option
                                             : (operand_log ? operand_log : both_path);
        string_address in_path = in_option ? in_option : both_path;
        if (!out_path && !in_path)
                out_path = (string_address)"typescript";
        //      Both streams out of one --log-io read one file position, the
        //      way the reference's single associated log does.
        bool shared = both_path && out_path == both_path && in_path == both_path;

        // Operands past the divisor are ignored, as util-linux ignores them.
        string_address divisor_text = file_option_value(address_of taking, 'd');
        if (!divisor_text && operand < argument_count)
                divisor_text = program_argument((b32)operand++);
        positive divisor = 1000000000;
        if (divisor_text && !file_duration_read(divisor_text, false, address_of divisor))
                return string_report(log_error, 1, "scriptreplay: failed to parse number: '%s'\n",
                                     divisor_text);

        bool limited = false;
        positive maximum = 0;
        string_address maximum_text = file_option_value(address_of taking, 'm');
        if (maximum_text)
        {
                file_duration_read(maximum_text, false, address_of maximum);
                limited = true;
        }

        p8 selected = 'O';
        string_address stream = file_option_value(address_of taking, 'x');
        if (stream)
        {
                if (string_equals(stream, (string_address)"out"))
                        selected = 'O';
                else if (string_equals(stream, (string_address)"in"))
                        selected = 'I';
                else if (string_equals(stream, (string_address)"signal"))
                        selected = 'S';
                else if (string_equals(stream, (string_address)"info"))
                        selected = 'H';
                else
                        return string_report(log_error, 1, "scriptreplay: unsupported stream name: '%s'\n", stream);
        }
        else if (!out_path && in_path)
                selected = 'I';
        //      A stream with no log behind it plays nothing and is not an
        //      error: the reference writes its closing newline and leaves.
        p8 classic = selected == 'I' ? 'I' : 'O';

        p8 cr_mode = 0;
        string_address cr = file_option_value(address_of taking, 'c');
        if (cr)
        {
                if (string_equals(cr, (string_address)"never"))
                        cr_mode = 1;
                else if (string_equals(cr, (string_address)"always"))
                        cr_mode = 2;
                else if (!string_equals(cr, (string_address)"auto"))
                        return string_report(log_error, 1, "%s: %s\n", "scriptreplay", "invalid CR mode");
        }

        process_replay_reader timing, output, input;
        bipolar opened = process_replay_open(address_of timing, timing_path);
        if (opened < 0)
                return string_report(log_error, 1, "scriptreplay: %s: %s\n",
                              timing_path, file_reason(opened));
        output.from.fd = input.from.fd = -1;
        if (out_path)
        {
                opened = process_replay_open(address_of output, out_path);
                if (opened < 0 || !process_replay_skip_header(address_of output))
                        goto replay_open_failed;
        }
        if (in_path && !shared)
        {
                opened = process_replay_open(address_of input, in_path);
                if (opened < 0 || !process_replay_skip_header(address_of input))
                        goto replay_open_failed;
        }

        bool advanced = false;
        bool failed = false;
        bool first = true;
        p8 line[PROCESS_REPLAY_LINE];
        bipolar got;
        while ((got = process_replay_line(address_of timing, line,
                                          sizeof(line))) > 0)
        {
                p8 kind;
                positive delay, length;
                string_address text;
                b32 read = process_replay_timing(line, address_of advanced,
                                                 address_of kind, address_of delay,
                                                 address_of length, address_of text);
                if (read == PROCESS_REPLAY_LINE_BROKEN)
                {
                        failed = true;
                        break;
                }
                if (read == PROCESS_REPLAY_NOT_TIMING)
                {
                        /*      Nothing converted on the opening line means
                                this file is not a timing file, which the
                                reference replays as empty rather than
                                refusing; after that the format is settled and
                                a line it cannot read is an error. */
                        if (!first)
                                failed = true;
                        break;
                }
                first = false;
                if (kind == 'H')
                {
                        if (selected == 'H' && !process_replay_say(text, true))
                                failed = true;
                        if (failed)
                                break;
                        continue;
                }
                if (!process_replay_sleep(delay, divisor, limited, maximum))
                {
                        failed = true;
                        break;
                }
                if (kind == 'S')
                {
                        if (selected == 'S' && !process_replay_say(text, false))
                                failed = true;
                        if (failed)
                                break;
                        continue;
                }

                //      A classic timing file has no stream letters, so its
                //      entries belong to whichever stream was asked for.
                p8 which = kind ? kind : classic;
                process_replay_reader address_to source =
                    shared || which == 'O' ? address_of output
                                           : address_of input;
                if (source->from.fd < 0)
                        continue;
                //      A log with less left in it than the row asks for ends
                //      the replay where it runs out, and that is a success:
                //      the reference stops there and says nothing.
                if (!process_replay_payload(source, length, which == selected,
                                            cr_mode == 2 ||
                                            (!cr_mode && which == 'I')))
                        break;
        }
        if (got < 0 || timing.failed)
                failed = true;
        system_close((positive)timing.from.fd);
        if (output.from.fd >= 0)
                system_close((positive)output.from.fd);
        if (input.from.fd >= 0 && input.from.fd != output.from.fd)
                system_close((positive)input.from.fd);
        if (failed)
                return string_report(log_error, 1, "scriptreplay: malformed or truncated timing/log file\n");
        system_write_all(1, "\n", 1);
        return 0;

replay_open_failed:
        string_format(log_error, "scriptreplay: cannot read transcript: %s\n",
                      opened < 0 ? file_reason(opened)
                                 : (string_address)"invalid header");
        system_close((positive)timing.from.fd);
        if (output.from.fd >= 0)
                system_close((positive)output.from.fd);
        if (input.from.fd >= 0 && input.from.fd != output.from.fd)
                system_close((positive)input.from.fd);
        return 1;
}

// pivot_root ------------------------------------------------------

/* Keep this applet as the syscall-shaped primitive it is.  Bowl setup can
   construct the mount tree with the existing mount/unshare tools and then
   cross the root boundary without launching a second utility runtime. */
static const argument_option process_pivot_root_options[] = {
    {"help", 'h'},
    {"version", 'V'},
    {null},
};

static b32 process_pivot_root()
{
        file_taking taking = {
            .program = (string_address)"pivot_root",
            .options = process_pivot_root_options,
        };
        positive count = (positive)program_argument_count();

        b32 answer;
        if (ul_options_done(address_of taking, "[options] new_root put_old",
                            address_of answer))
                return answer;
        if (taking.first + 2 != count)
                return string_report(log_error, 1, "%s: %s\n", (string_address)"pivot_root", (string_address)"expected new_root and put_old");

        string_address new_root = program_argument((b32)taking.first);
        string_address put_old = program_argument((b32)taking.first + 1);
        bipolar changed = system_call_2(syscall(pivot_root),
                                        (positive)new_root,
                                        (positive)put_old);

        if (changed < 0)
                return string_report(log_error, 1, "pivot_root: failed to change root from %s to %s: %s\n",
                              new_root, put_old, file_reason(changed));
        return 0;
}

// ctrlaltdel ------------------------------------------------------

#define PROCESS_REBOOT_MAGIC 0xfee1dead
#define PROCESS_REBOOT_MAGIC_SECOND 672274793
#define PROCESS_REBOOT_CAD_OFF 0
#define PROCESS_REBOOT_CAD_ON 0x89abcdef

static const argument_option process_ctrlaltdel_options[] = {
    {"help", 'h'},
    {"version", 'V'},
    {null},
};

static b32 process_ctrlaltdel()
{
        file_taking taking = {
            .program = (string_address)"ctrlaltdel",
            .options = process_ctrlaltdel_options,
        };
        positive count = (positive)program_argument_count();

        b32 answer;
        if (ul_options_done(address_of taking, "hard|soft", address_of answer))
                return answer;

        /* With nothing after the program name util-linux reports the current
           setting, which the kernel publishes as 0 (soft) or 1 (hard). Its
           operand is the second word of the vector and not the first word
           the option reader left standing, so a lone -- is an argument to it
           and not the end of the options. */
        if (count < 2)
        {
                string_address knob = (string_address)"/proc/sys/kernel/ctrl-alt-del";
                p8 setting[16];
                bipolar handle = system_open_at(AT_FDCWD, knob,
                                                FILE_READ | O_CLOEXEC);
                bipolar got = handle < 0
                                  ? handle
                                  : system_read_retry((positive)handle, setting,
                                                      sizeof(setting) - 1);

                if (handle >= 0)
                        system_close(handle);
                if (got < 0)
                {
                        return string_report(log_error, 1, "ctrlaltdel: cannot read %s: %s\n",
                                      knob, file_reason(got));
                }
                log(got > 0 && setting[0] == '1' ? "hard\n" : "soft\n", 5);
                log_flush();
                return 0;
        }
        string_address mode = program_argument(1);
        positive command;

        if (string_equals(mode, (string_address)"hard"))
                command = PROCESS_REBOOT_CAD_ON;
        else if (string_equals(mode, (string_address)"soft"))
                command = PROCESS_REBOOT_CAD_OFF;
        else
                return string_report(log_error, 1, "ctrlaltdel: unknown argument: %s\n", mode);

        bipolar changed = system_call_4(syscall(reboot), PROCESS_REBOOT_MAGIC,
                                        PROCESS_REBOOT_MAGIC_SECOND, command,
                                        0);
        if (changed < 0)
                return string_report(log_error, 1, "ctrlaltdel: cannot set %s mode: %s\n",
                              mode, file_reason(changed));
        return 0;
}
