/*
        test/checks/process.c: processes, signals, locks and sockets.

        A run of test/checks.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. A section is built as
        test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib -nostartfiles
        -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its
        freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name>
        test/checks.c).
*/

#ifdef CHECK_process
#include "../../src/lib.util.c"

/*
        Directories, the exec family, sleeping, and the small POSIX names
        beside them.

        What is not here is the exhaustive half, and that is deliberate. The
        parts of this family whose answer glibc defines -- strverscmp,
        basename, dirname, realpath, getopt, the whole of readdir and
        scandir's ordering, remove's errno and execvp's PATH walk -- were
        compared against glibc 2.44 on the build machine by running identical
        sweeps through both and diffing the output, on all three machines:

            8100 strverscmp pairs over 90 strings
            52 paths through basename and dirname
            45 paths through realpath, over a tree of symbolic links,
               loops, dangling links and .. past root
            32 getopt cases, every tuple and both diagnostics
            2402 entries read out of one directory, which is four getdents64
               refills and not one
            two scandir orderings, telldir and seekdir round trips
            17 execvp cases over a directory of deliberately broken stubs

        All of it agreed line for line on x86_64, arm64 and riscv64. That
        harness needs a glibc and a writable file system laid out just so, and
        nothing else in this tree needs either, so it does not live here.

        What is here is everything a differential cannot reach: the contracts
        that are about this implementation rather than about an answer, and
        the failure paths glibc and this agree to have but that a sweep would
        have to break the machine to provoke.
*/

#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

static fn same(string_address name, bipolar got, bipolar want)
{
        checks++;

        if (got == want)
                return;

        failures++;
        string_format(log, "FAIL %s: got %b want %b\n", name, got, want);
}

static fn same_text(string_address name, string_address got,
                    string_address want)
{
        checks++;

        if (!is_null(got) && !is_null(want) && string_compare(got, want) == 0)
                return;

        failures++;
        string_format(log, "FAIL %s: got %s want %s\n", name,
                      is_null(got) ? (string_address) "(null)" : got,
                      is_null(want) ? (string_address) "(null)" : want);
}

static fn true_is(string_address name, p8 got)
{
        same(name, got ? 1 : 0, 1);
}

//      Where everything that needs a file system happens, made once and torn
//      down at the end.
static p8 test_root[PATH_MAX];

static positive test_path(p8 address_to into, string_address tail)
{
        return path_join(into, PATH_MAX, test_root, tail);
}

//      -- sleeping -----------------------------------------------------------

/*
        The three sleeps, and the one thing about them that is worth a test:
        that time actually passed and that a bad request is refused rather
        than rounded.

        The elapsed check is deliberately loose at the top and tight at the
        bottom. A sleep must not return early -- that is the whole contract --
        but it may return arbitrarily late, because the machine may be busy
        and qemu certainly is, so the upper bound is a second rather than
        anything that would make this flaky.
*/
static fn test_sleeping(void)
{
        timespec span;
        timespec left;
        timespec before;
        timespec after;
        positive elapsed;

        span.tv_sec = 0;
        span.tv_nsec = 1000000;
        left.tv_sec = 0;
        left.tv_nsec = 0;
        same("nanosleep short", nanosleep(address_of span, address_of left), 0);

        //      Nothing was interrupted, so nothing was written back.
        same("nanosleep left seconds", (bipolar)left.tv_sec, 0);
        same("nanosleep left nanoseconds", (bipolar)left.tv_nsec, 0);

        span.tv_sec = 0;
        span.tv_nsec = 1000000000;
        errno = 0;
        same("nanosleep bad nanoseconds", nanosleep(address_of span, null), -1);
        same("nanosleep bad errno", errno, EINVAL);

        span.tv_sec = (positive)(bipolar)-1;
        span.tv_nsec = 0;
        errno = 0;
        same("nanosleep negative", nanosleep(address_of span, null), -1);
        same("nanosleep negative errno", errno, EINVAL);

        errno = 0;
        same("nanosleep null", nanosleep(null, null), -1);
        same("nanosleep null errno", errno, EFAULT);

        //      clock_nanosleep reports through its return value and must not
        //      disturb errno, which is the difference that makes it a
        //      separate routine rather than an alias.
        span.tv_sec = 0;
        span.tv_nsec = 1000000;
        errno = 1234;
        same("clock_nanosleep short",
             clock_nanosleep(CLOCK_MONOTONIC, 0, address_of span, null), 0);
        same("clock_nanosleep left errno alone", errno, 1234);

        span.tv_nsec = 1000000000;
        errno = 1234;
        same("clock_nanosleep bad",
             clock_nanosleep(CLOCK_MONOTONIC, 0, address_of span, null),
             EINVAL);
        same("clock_nanosleep still left errno alone", errno, 1234);

        //      An absolute deadline already in the past returns at once and
        //      is not an error.
        before.tv_sec = 0;
        before.tv_nsec = 0;
        same("clock_nanosleep absolute past",
             clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME,
                             address_of before, null),
             0);

        same("usleep", usleep(1000), 0);
        same("usleep past a second", usleep(1100000), 0);

        same("sleep zero seconds", (bipolar)process_sleep_seconds(0), 0);

        //      Time really passed. Twenty milliseconds asked for, and the
        //      monotonic clock must have moved at least that far.
        clock_gettime(CLOCK_MONOTONIC, address_of before);
        span.tv_sec = 0;
        span.tv_nsec = 20000000;
        nanosleep(address_of span, null);
        clock_gettime(CLOCK_MONOTONIC, address_of after);

        elapsed = (after.tv_sec - before.tv_sec) * 1000000000u +
                  after.tv_nsec - before.tv_nsec;

        true_is("nanosleep did not return early", elapsed >= 20000000u);
        true_is("nanosleep returned within a second", elapsed < 1000000000u);

        /*
                The name `sleep` still means lib.c's assembly routine.

                This is the whole reason process_sleep_seconds exists under
                its own name: the POSIX spelling is behind
                STANDARD_SLEEP_IS_POSIX and must stay behind it, because
                CHECK_clock and CHECK_stream_buffering both call the
                assembly one with a pointer and would compile into nonsense
                the day the macro went on by default. Calling it here with a
                timespec is what proves the macro is off.
        */
        span.tv_sec = 0;
        span.tv_nsec = 1000000;
        sleep(address_of span);
        checks++;
}

//      -- directories --------------------------------------------------------

static b32 test_directory_names(DIR address_to folder, string_address wanted)
{
        process_dirent address_to entry;
        b32 found = 0;

        while (!is_null(entry = readdir(folder)))
                if (string_compare(entry->d_name, wanted) == 0)
                        found++;

        return found;
}

static fn test_directories(void)
{
        p8 path[PATH_MAX];
        p8 other[PATH_MAX];
        DIR address_to folder;
        process_dirent address_to entry;
        process_dirent held;
        process_dirent address_to answer;
        b32 handle;
        b32 count;
        b32 files;
        b32 directories;
        b32 links;
        bipolar mark;

        test_path(path, (string_address) "one");
        handle = creat(path, 0644);
        close(handle);

        test_path(path, (string_address) "two");
        handle = creat(path, 0644);
        close(handle);

        test_path(path, (string_address) "inner");
        mkdir(path, 0755);

        test_path(path, (string_address) "pointer");
        symlink((string_address) "one", path);

        folder = opendir(test_root);
        true_is("opendir found the directory", !is_null(folder));

        count = 0;
        files = 0;
        directories = 0;
        links = 0;

        while (!is_null(entry = readdir(folder)))
        {
                count++;

                if (entry->d_type == DT_REG)
                        files++;
                else if (entry->d_type == DT_DIR)
                        directories++;
                else if (entry->d_type == DT_LNK)
                        links++;
        }

        //      Four names, a dot, a dot-dot and the directory made above.
        same("readdir saw every entry", count, 6);
        same("readdir counted the files", files, 2);
        same("readdir counted the directories", directories, 3);
        same("readdir counted the links", links, 1);

        //      The end of a directory is a null answer with errno untouched,
        //      which is the only way a caller tells it from a failure.
        errno = 4321;
        true_is("readdir at the end is null", is_null(readdir(folder)));
        same("readdir at the end left errno alone", errno, 4321);

        rewinddir(folder);
        same("rewinddir went back to the start",
             test_directory_names(folder, (string_address) "one"), 1);

        //      telldir before an entry names that entry.
        rewinddir(folder);
        mark = telldir(folder);
        entry = readdir(folder);
        string_copy(other, entry->d_name);
        readdir(folder);
        seekdir(folder, mark);
        entry = readdir(folder);
        same_text("seekdir came back to the same entry", entry->d_name, other);

        //      readdir_r copies rather than pointing, and says nothing
        //      through errno.
        rewinddir(folder);
        count = 0;

        while (readdir_r(folder, address_of held, address_of answer) == 0 &&
               !is_null(answer))
        {
                count++;
                true_is("readdir_r answered with the caller's structure",
                        answer == address_of held);
        }

        same("readdir_r saw every entry", count, 6);

        same("dirfd is a descriptor", dirfd(folder) >= 0 ? 1 : 0, 1);
        handle = dirfd(folder);
        same("closedir", closedir(folder), 0);

        //      closedir closed the descriptor it was holding.
        errno = 0;
        same("closedir closed the descriptor", close(handle), -1);
        same("closedir errno", errno, EBADF);

        //      fdopendir takes the descriptor it is given, and closedir
        //      closes that one too.
        handle = open(test_root, O_RDONLY | O_DIRECTORY);
        folder = fdopendir(handle);
        true_is("fdopendir took the descriptor", !is_null(folder));
        same("fdopendir kept the same descriptor", dirfd(folder), handle);
        closedir(folder);
        errno = 0;
        same("fdopendir's descriptor was closed", close(handle), -1);

        //      Every refusal.
        test_path(path, (string_address) "one");
        errno = 0;
        true_is("opendir refuses a regular file", is_null(opendir(path)));
        same("opendir on a file is ENOTDIR", errno, ENOTDIR);

        test_path(path, (string_address) "missing");
        errno = 0;
        true_is("opendir refuses a missing path", is_null(opendir(path)));
        same("opendir on nothing is ENOENT", errno, ENOENT);

        errno = 0;
        true_is("opendir refuses a null path", is_null(opendir(null)));
        same("opendir on null is EINVAL", errno, EINVAL);

        test_path(path, (string_address) "one");
        handle = open(path, O_RDONLY);
        errno = 0;
        true_is("fdopendir refuses a regular file",
                is_null(fdopendir(handle)));
        same("fdopendir on a file is ENOTDIR", errno, ENOTDIR);
        close(handle);

        errno = 0;
        true_is("fdopendir refuses a closed descriptor",
                is_null(fdopendir(handle)));
        same("fdopendir on a closed descriptor is EBADF", errno, EBADF);

        errno = 0;
        true_is("fdopendir refuses a negative descriptor",
                is_null(fdopendir(-1)));
        same("fdopendir on a negative descriptor is EBADF", errno, EBADF);
}

//      -- scandir -------------------------------------------------------------

static b32 test_keep_all(const process_dirent address_to entry)
{
        (void)entry;

        return 1;
}

static b32 test_keep_none(const process_dirent address_to entry)
{
        (void)entry;

        return 0;
}

static b32 test_keep_plain(const process_dirent address_to entry)
{
        return entry->d_name[0] != '.';
}

static fn test_scandir_growth(void)
{
        p8 folder[PATH_MAX], path[PATH_MAX];
        test_path(folder, "scan-growth");
        same("scandir growth directory", mkdir(folder, 0700), 0);
        for (b32 at = 0; at < 65; at++)
        {
                snprintf(path, sizeof(path), "%s/%02d", folder, at);
                b32 descriptor = open(path, O_CREAT | O_WRONLY, 0600);
                true_is("scandir growth file", descriptor >= 0);
                close(descriptor);
        }
        process_dirent address_to address_to list = null;
        b32 count = scandir(folder, address_of list, test_keep_plain, alphasort);
        same("scandir crosses three vector growth boundaries", count, 65);
        for (b32 at = 0; at < count; at++)
        {
                p8 name[16];
                snprintf(name, sizeof(name), "%02d", at);
                same_text("scandir growth order", list[at]->d_name, name);
                path_join(path, sizeof(path), folder, list[at]->d_name);
                unlink(path);
                free(list[at]);
        }
        free(list);
        rmdir(folder);
}

static fn test_scandir(void)
{
        test_scandir_growth();
        process_dirent address_to address_to list = null;
        b32 count;
        b32 at;

        count = scandir(test_root, address_of list, test_keep_plain,
                        alphasort);

        same("scandir kept the four names", count, 4);
        same_text("scandir sorted first", list[0]->d_name,
                  (string_address) "inner");
        same_text("scandir sorted second", list[1]->d_name,
                  (string_address) "one");
        same_text("scandir sorted third", list[2]->d_name,
                  (string_address) "pointer");
        same_text("scandir sorted fourth", list[3]->d_name,
                  (string_address) "two");

        //      An entry from scandir is its own allocation, is terminated,
        //      and says how long it is.
        for (at = 0; at < count; at++)
        {
                true_is("scandir sized the entry to the name",
                        list[at]->d_reclen ==
                                PROCESS_DIRENT_HEADER +
                                        string_length(list[at]->d_name) + 1);
                free(list[at]);
        }

        free(list);

        list = null;
        count = scandir(test_root, address_of list, test_keep_none, alphasort);
        same("scandir kept nothing", count, 0);
        true_is("scandir still handed back a vector", !is_null(list));
        free(list);

        list = null;
        count = scandir(test_root, address_of list, test_keep_all, null);
        same("scandir with no ordering kept everything", count, 6);

        for (at = 0; at < count; at++)
                free(list[at]);

        free(list);

        list = null;
        errno = 0;
        same("scandir on a missing directory",
             scandir((string_address) "/tmp/pd-not-here-at-all",
                     address_of list, null, alphasort),
             -1);
        same("scandir on a missing directory is ENOENT", errno, ENOENT);

        errno = 0;
        same("scandir with no list refuses",
             scandir(test_root, null, null, alphasort), -1);
        same("scandir with no list is EINVAL", errno, EINVAL);
}

//      -- version ordering ---------------------------------------------------

static fn test_version_sign(string_address left, string_address right,
                            b32 want)
{
        b32 got = strverscmp(left, right);

        checks++;

        if ((got < 0 && want < 0) || (got > 0 && want > 0) ||
            (got == 0 && want == 0))
                return;

        failures++;
        string_format(log, "FAIL strverscmp(%s, %s): got %b want %b\n", left,
                      right, (bipolar)got, (bipolar)want);
}

static fn test_versions(void)
{
        //      The property that makes this different from strcmp: a run of
        //      digits compares as a number.
        test_version_sign((string_address) "file2", (string_address) "file10",
                          -1);
        test_version_sign((string_address) "file10", (string_address) "file2",
                          1);
        test_version_sign((string_address) "file1", (string_address) "file1",
                          0);

        //      And the property that makes it different from a number
        //      compare: a run that starts with a zero is a fraction.
        test_version_sign((string_address) "1.010", (string_address) "1.09",
                          -1);
        test_version_sign((string_address) "007", (string_address) "07", -1);
        test_version_sign((string_address) "", (string_address) "", 0);
        test_version_sign((string_address) "", (string_address) "a", -1);
        test_version_sign((string_address) "a", (string_address) "", 1);
}

//      -- basename and dirname ------------------------------------------------

static fn test_names(void)
{
        p8 held[PATH_MAX];

        string_copy(held, (string_address) "/usr/lib");
        same_text("basename of a path", basename(held),
                  (string_address) "lib");
        string_copy(held, (string_address) "/usr/lib");
        same_text("dirname of a path", dirname(held), (string_address) "/usr");

        string_copy(held, (string_address) "usr");
        same_text("basename with no directory", basename(held),
                  (string_address) "usr");
        string_copy(held, (string_address) "usr");
        same_text("dirname with no directory", dirname(held),
                  (string_address) ".");

        string_copy(held, (string_address) "/");
        same_text("basename of root", basename(held), (string_address) "/");
        string_copy(held, (string_address) "/");
        same_text("dirname of root", dirname(held), (string_address) "/");

        string_copy(held, (string_address) "//");
        same_text("dirname of exactly two slashes", dirname(held),
                  (string_address) "//");
        string_copy(held, (string_address) "///");
        same_text("dirname of three slashes", dirname(held),
                  (string_address) "/");

        string_copy(held, (string_address) "a/b/");
        same_text("basename ignores a trailing slash", basename(held),
                  (string_address) "b");

        same_text("basename of nothing", basename((string_address) ""),
                  (string_address) ".");
        same_text("dirname of nothing", dirname((string_address) ""),
                  (string_address) ".");
        same_text("basename of null", basename(null), (string_address) ".");
        same_text("dirname of null", dirname(null), (string_address) ".");
}

//      -- realpath ------------------------------------------------------------

//      The name of link number index in the chain: chain-0, chain-1 and so on.
static fn test_chain_name(p8 address_to name, positive index)
{
        p8 digits[24];
        positive count = 0;
        positive at = 6;

        do
        {
                digits[count++] = (p8)('0' + index % 10);
                index /= 10;
        } while (index);

        memory_copy(name, "chain-", 6);
        while (count)
                name[at++] = digits[--count];
        name[at] = end;
}

static fn test_realpath(void)
{
        p8 path[PATH_MAX];
        p8 answer[PATH_MAX];
        p8 wanted[PATH_MAX];
        string_address allocated;
        p8 name[32];

        test_path(path, (string_address) "pointer");
        test_path(wanted, (string_address) "one");

        same_text("realpath followed the link", realpath(path, answer),
                  wanted);

        //      A null buffer allocates, and what comes back is free()able.
        allocated = realpath(path, null);
        same_text("realpath allocated the answer", allocated, wanted);
        free(allocated);

        test_path(path, (string_address) "inner/../one");
        same_text("realpath resolved dot dot", realpath(path, answer), wanted);

        test_path(path, (string_address) "./inner/./");
        test_path(wanted, (string_address) "inner");
        same_text("realpath dropped the dots", realpath(path, answer), wanted);

        //      Every refusal, and the errno that goes with it.
        errno = 0;
        true_is("realpath refuses null", is_null(realpath(null, answer)));
        same("realpath on null is EINVAL", errno, EINVAL);

        errno = 0;
        true_is("realpath refuses an empty path",
                is_null(realpath((string_address) "", answer)));
        same("realpath on nothing is ENOENT", errno, ENOENT);

        test_path(path, (string_address) "missing");
        errno = 0;
        true_is("realpath refuses a missing path",
                is_null(realpath(path, answer)));
        same("realpath on a missing path is ENOENT", errno, ENOENT);

        test_path(path, (string_address) "one/below");
        errno = 0;
        true_is("realpath refuses a file used as a directory",
                is_null(realpath(path, answer)));
        same("realpath through a file is ENOTDIR", errno, ENOTDIR);

        //      The kernel's own answer for /etc/passwd/.. is ENOTDIR, and a
        //      walk that just popped the last component would say /etc.
        test_path(path, (string_address) "one/..");
        errno = 0;
        true_is("realpath refuses dot dot out of a file",
                is_null(realpath(path, answer)));
        same("realpath dot dot through a file is ENOTDIR", errno, ENOTDIR);

        // A separator requires a directory even when no ordinary component
        // follows it. Exercise both caller-owned and allocated output, and
        // the same regular file reached directly or through a symbolic link.
        static const string_address nondirectory[] = {
            "one/", "one//", "one/.", "one/./", "one//.//",
            "pointer/", "pointer//", "pointer/.", "pointer/./",
        };
        for (positive i = 0; i < array_count(nondirectory); i++)
                for (positive owned = 0; owned < 2; owned++)
                {
                        test_path(path, nondirectory[i]);
                        errno = 0;
                        allocated = realpath(path, owned ? answer : null);
                        true_is("realpath requires a directory before slash",
                                is_null(allocated));
                        same("realpath slash through file is ENOTDIR",
                             errno, ENOTDIR);
                        if (!owned)
                                free(allocated);
                }

        //      A loop of links is ELOOP and not a hang.
        test_path(path, (string_address) "round");
        test_path(wanted, (string_address) "trip");
        symlink(wanted, path);
        symlink(path, wanted);
        errno = 0;
        true_is("realpath refuses a loop", is_null(realpath(path, answer)));
        same("realpath on a loop is ELOOP", errno, ELOOP);

        //      A self-link followed with a trailing slash grows by one slash
        //      each time round and is still a loop.
        test_path(path, (string_address) "spin");
        symlink((string_address) "spin/", path);
        errno = 0;
        true_is("realpath refuses a self-link with a slash",
                is_null(realpath(path, answer)));
        same("realpath on a self-link with a slash is ELOOP", errno, ELOOP);

        //      A chain of 2,000 links is a path, not a loop: GNU's realpath
        //      follows a chain of any length, and the kernel's forty lookups
        //      limit open(2), not a name. Each link names the next by its
        //      absolute path, and the last one is pointer.
        {
                positive chain = 2000;

                for (positive i = 0; i <= chain; i++)
                {
                        test_chain_name(name, i);
                        test_path(path, name);
                        if (i == chain)
                                test_path(wanted, (string_address) "pointer");
                        else
                        {
                                test_chain_name(name, i + 1);
                                test_path(wanted, name);
                        }
                        symlink(wanted, path);
                }

                test_chain_name(name, 0);
                test_path(path, name);
                test_path(wanted, (string_address) "one");
                same_text("realpath follows a chain of 2000 links",
                          realpath(path, answer), wanted);
        }

        //      Root is its own parent.
        same_text("realpath of root", realpath((string_address) "/", answer),
                  (string_address) "/");
        same_text("realpath above root",
                  realpath((string_address) "/../..", answer),
                  (string_address) "/");
}

//      -- exec ----------------------------------------------------------------

//      The raw wait status, decoded through the macros this family adds.
static b32 test_run(string_address address_to words, p8 by_path)
{
        b32 status = 0;
        b32 child;

        //      Anything buffered belongs to this process; the child would
        //      write it a second time.
        log_flush();

        child = fork();

        if (child == 0)
        {
                if (by_path)
                        execvp(words[0], words);
                else
                        execv(words[0], words);

                _exit(100 + errno);
        }

        waitpid(child, address_of status, 0);

        if (!WIFEXITED(status))
                return -1;

        return WEXITSTATUS(status);
}

static fn test_exec_paths(void)
{
        static string_address formats[] = {"%s", "%s/", "%s//", ":%s",
                "/pd-no-such-directory:%s", "%s:", "", ":", ".", "./"};
        static string_address names[] = {"exec-link", "exec-script", "exec-denied"};
        p8 path[PATH_MAX], search[PATH_MAX * 2];

        test_path(path, names[0]);
        same("exec PATH fixture link", symlink((string_address)"/bin/sh", path), 0);
        for (positive at = 1; at < 3; at++)
        {
                test_path(path, names[at]);
                b32 handle = creat(path, at == 1 ? 0700 : 0600);
                same("exec PATH fixture script", write(handle, "exit 7\n", 7), 7);
                close(handle);
        }
        for (positive shape = 0; shape <= sizeof(formats) / sizeof(formats[0]); shape++)
        {
                if (shape == sizeof(formats) / sizeof(formats[0]))
                {
                        memory_fill(search, 'x', PATH_MAX);
                        search[PATH_MAX] = ':';
                        string_copy(search + PATH_MAX + 1, test_root);
                }
                else
                        snprintf(search, sizeof search, formats[shape], test_root);
                for (positive kind = 0; kind < 3; kind++)
                {
                        b32 status = 0;
                        log_flush();
                        b32 child = fork();
                        if (child == 0)
                        {
                                string_address words[] = {names[kind], "-c", "exit 7", null};
                                if (chdir(test_root) || setenv((string_address)"PATH", search, 1))
                                        _exit(99);
                                execvp(words[0], words);
                                _exit(100 + errno);
                        }
                        same("exec PATH fixture child", waitpid(child, &status, 0), child);
                        same("exec PATH separators, fallback and denied", WIFEXITED(status)
                                ? WEXITSTATUS(status) : -1, kind == 2 ? 100 + EACCES : 7);
                }
        }
        for (positive at = 0; at < 3; at++)
        {
                test_path(path, names[at]);
                unlink(path);
        }
}

static fn test_exec(void)
{
        test_exec_paths();
        string_address words[5];
        b32 status = 0;
        b32 child;

        words[0] = (string_address) "/bin/sh";
        words[1] = (string_address) "-c";
        words[2] = (string_address) "exit 7";
        words[3] = null;
        same("execv ran the shell", test_run(words, false), 7);

        //      A script with no #! line, run with no arguments after argv[0].
        //      The kernel refuses it with ENOEXEC and execvp hands it to
        //      /bin/sh with the script as the operand; the operand was lost
        //      when there were no arguments, and sh read its standard input.
        //      execv does not retry (glibc's does not either); the PATH walk of
        //      execvp does, so this runs execvpe with PATH naming the cwd and an
        //      empty argument vector, which is the case that lost the operand.
        {
                static const char script[] = "exit 7\n";
                bipolar made = system_call_4(syscall(openat), (positive)AT_FDCWD,
                                             (positive)"pd-no-shebang",
                                             O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0755);
                string_address script_words[1] = {null};
                string_address script_env[2] = {(string_address) "PATH=.", null};

                if (made >= 0)
                {
                        system_call_3(syscall(write), (positive)made,
                                      (positive)script, sizeof(script) - 1);
                        system_close(made);
                }
                log_flush();
                b32 script_child = fork();
                if (script_child == 0)
                {
                        setenv((string_address) "PATH", (string_address) ".", 1);
                        execvpe((string_address) "pd-no-shebang", script_words, script_env);
                        _exit(100 + errno);
                }
                b32 script_status = 0;
                waitpid(script_child, address_of script_status, 0);
                same("execvp's PATH walk runs a script with no #! line and no arguments",
                     WIFEXITED(script_status) ? WEXITSTATUS(script_status) : -1, 7);
                system_remove_at(AT_FDCWD, (string_address) "pd-no-shebang", 0);
        }

        words[0] = (string_address) "sh";
        same("execvp found the shell on PATH", test_run(words, true), 7);

        words[0] = (string_address) "pd-no-such-program-anywhere";
        words[1] = null;
        same("execvp did not find a missing name", test_run(words, true),
             100 + ENOENT);

        words[0] = (string_address) "/pd/no/such/path";
        same("execv did not find a missing path", test_run(words, false),
             100 + ENOENT);

        errno = 0;
        same("execvp refuses an empty name",
             execvp((string_address) "", words), -1);
        same("execvp on an empty name is ENOENT", errno, ENOENT);

        errno = 0;
        same("execvp refuses a null name", execvp(null, words), -1);
        same("execvp on a null name is ENOENT", errno, ENOENT);

        //      The variadic spellings, each in its own child.
        log_flush();
        child = fork();

        if (child == 0)
        {
                execl((string_address) "/bin/sh", (string_address) "sh",
                      (string_address) "-c", (string_address) "exit 11",
                      (string_address)null);
                _exit(100 + errno);
        }

        waitpid(child, address_of status, 0);
        same("execl ran the shell", WEXITSTATUS(status), 11);

        log_flush();
        child = fork();

        if (child == 0)
        {
                execlp((string_address) "sh", (string_address) "sh",
                       (string_address) "-c", (string_address) "exit 12",
                       (string_address)null);
                _exit(100 + errno);
        }

        waitpid(child, address_of status, 0);
        same("execlp ran the shell", WEXITSTATUS(status), 12);

        log_flush();
        child = fork();

        if (child == 0)
        {
                string_address environment[2];

                environment[0] = (string_address) "PD_MARKER=13";
                environment[1] = null;

                execle((string_address) "/bin/sh", (string_address) "sh",
                       (string_address) "-c",
                       (string_address) "exit $PD_MARKER",
                       (string_address)null, environment);
                _exit(100 + errno);
        }

        waitpid(child, address_of status, 0);
        same("execle used the environment it was given",
             WEXITSTATUS(status), 13);
}

//      -- descriptors a child lets go of -----------------------------------------

/*
        descriptors_close_except in a child of its own, over sixteen
        descriptors that are all open to begin with: the ones from `from` up
        are closed except the two kept, whatever their order, and the ones
        below `from` are not touched. The child answers with the number of
        descriptors that are not what that says, and 100 when the call
        itself said false. A third kept one goes through
        descriptors_close_outside, which the two-kept form is.
*/
static fn test_close_three_case(string_address name, p32 from, p32 first,
                                p32 second, p32 third)
{
        b32 status = 0;
        b32 child;

        log_flush();
        child = fork();

        if (child == 0)
        {
                positive wrong = 0;
                bipolar null_device = system_open_at(AT_FDCWD, "/dev/null", FILE_READ);
                p32 keep[3] = {first, second, third};

                for (positive at = 0; at < 16; at++)
                        system_call_3(syscall(dup3), (positive)null_device, at, 0);
                if (null_device >= 16)
                        system_close(null_device);

                if (third == descriptors_none
                        ? !descriptors_close_except(from, first, second)
                        : !descriptors_close_outside(from, keep, 3))
                        _exit(100);

                for (positive at = 0; at < 16; at++)
                {
                        p8 open = system_call_3(syscall(fcntl), at, 1, 0) >= 0;
                        p8 wanted = at < from || at == first || at == second ||
                                      at == third;

                        wrong += open != wanted;
                }
                _exit((int)wrong);
        }

        waitpid(child, address_of status, 0);
        same(name, WEXITSTATUS(status), 0);
}

static fn test_close_except_case(string_address name, p32 from, p32 first,
                                 p32 second)
{
        test_close_three_case(name, from, first, second, descriptors_none);
}

static fn test_close_except(void)
{
        test_close_three_case("close_outside: three kept, out of order", 3, 11, 5, 8);
        test_close_three_case("close_outside: three adjacent", 3, 6, 4, 5);
        test_close_three_case("close_outside: one below from, two above", 3, 9, 1, 4);
        test_close_three_case("close_outside: one twice and one missing", 3, 7,
                              descriptors_none, 7);
        test_close_three_case("close_outside: the last of the sixteen and two below it",
                              0, 15, 0, 14);
        test_close_except_case("close_except: all from 3 up", 3, descriptors_none,
                               descriptors_none);
        test_close_except_case("close_except: one kept", 3, 5, descriptors_none);
        test_close_except_case("close_except: one kept, in the second place", 3,
                               descriptors_none, 5);
        test_close_except_case("close_except: two kept", 3, 5, 9);
        test_close_except_case("close_except: two kept, reversed", 3, 9, 5);
        test_close_except_case("close_except: the same one twice", 3, 5, 5);
        test_close_except_case("close_except: two adjacent", 3, 7, 8);
        test_close_except_case("close_except: one at from, one beside it", 3, 3, 4);
        test_close_except_case("close_except: the last of the sixteen kept", 3, 15,
                               descriptors_none);
        test_close_except_case("close_except: kept below from are not the range's", 3,
                               1, 2);
        test_close_except_case("close_except: one below from and one above it", 3, 2, 9);
        test_close_except_case("close_except: one above from and one below it", 3, 9, 1);
        test_close_except_case("close_except: from 0 closes standard input too", 0,
                               4, 8);
        test_close_except_case("close_except: one past every open one", 3, 100,
                               descriptors_none);
        test_close_except_case("close_except: the highest number there is", 3,
                               0xfffffffe, descriptors_none);
}

//      -- the kernel log's record -------------------------------------------------

/*
        kmsg_write against a pipe in place of /dev/kmsg: the bytes that go
        in are the level and the text in one write, and one that would not
        fit behind the level is cut to what does -- the kernel refuses the
        record whole past 1,024 bytes.
*/
static fn test_kmsg_write(void)
{
        b32 ends[2];
        p8 text[1100];
        p8 got[1100];
        bipolar read_length;

        same("kmsg: a pipe to stand for the log", system_pipe(ends, 0), 0);
        memory_fill(text, 'x', sizeof text);

        true_is("kmsg: a short record is written whole",
                kmsg_write(ends[1], "hello", 5));
        read_length = system_read_retry((positive)ends[0], got, sizeof got);
        same("kmsg: it is the level and the text", read_length, 8);
        same("kmsg: and in that order", memory_compare(got, "<6>hello", 8), 0);

        true_is("kmsg: an empty record is the level alone",
                kmsg_write(ends[1], "", 0));
        same("kmsg: which is three bytes", system_read_retry((positive)ends[0], got, sizeof got), 3);

        true_is("kmsg: the longest text that fits is written whole",
                kmsg_write(ends[1], text, 1021));
        same("kmsg: as 1,024 bytes", system_read_retry((positive)ends[0], got, sizeof got), 1024);

        true_is("kmsg: more than fits is cut to it, not refused",
                kmsg_write(ends[1], text, sizeof text));
        read_length = system_read_retry((positive)ends[0], got, sizeof got);
        same("kmsg: 1,024 bytes again", read_length, 1024);
        same("kmsg: of the level and the text's first 1,021",
             memory_compare(got, "<6>", 3) == 0 && got[1023] == 'x', 1);

        system_close(ends[0]);
        system_close(ends[1]);
}

//      -- temporary names ------------------------------------------------------

static fn test_temporary(void)
{
        p8 pattern[PATH_MAX];
        p8 second[PATH_MAX];
        p8 built[PATH_MAX];
        struct stat facts;
        FILE address_to handle;
        b32 one;
        b32 two;
        string_address name;
        p8 read_back[16];

        test_path(built, (string_address) "tmpXXXXXX");

        string_copy(pattern, built);
        one = mkstemp(pattern);
        true_is("mkstemp opened something", one >= 0);
        true_is("mkstemp rewrote the template",
                string_compare(pattern, built) != 0);
        same("mkstemp made the file", stat(pattern, address_of facts), 0);
        same("mkstemp made it private", (bipolar)(facts.st_mode & 0777),
             0600);
        true_is("mkstemp made a regular file", S_ISREG(facts.st_mode) != 0);

        string_copy(second, built);
        two = mkstemp(second);
        true_is("a second mkstemp opened something", two >= 0);
        true_is("two mkstemp names differ",
                string_compare(pattern, second) != 0);

        close(one);
        close(two);
        unlink(pattern);
        unlink(second);

        //      A template that does not end in six X is refused rather than
        //      quietly given a name of its own.
        string_copy(pattern, (string_address) "/tmp/pd-no-x-here");
        errno = 0;
        same("mkstemp refuses a template with no X", mkstemp(pattern), -1);
        same("mkstemp with no X is EINVAL", errno, EINVAL);

        string_copy(pattern, (string_address) "XXXXX");
        errno = 0;
        same("mkstemp refuses five X", mkstemp(pattern), -1);
        same("mkstemp with five X is EINVAL", errno, EINVAL);

        errno = 0;
        same("mkstemp refuses null", mkstemp(null), -1);
        same("mkstemp on null is EINVAL", errno, EINVAL);

        //      mkstemps keeps a suffix.
        test_path(built, (string_address) "tmpXXXXXX.log");
        string_copy(pattern, built);
        one = mkstemps(pattern, 4);
        true_is("mkstemps opened something", one >= 0);
        same_text("mkstemps kept the suffix",
                  pattern + string_length(pattern) - 4,
                  (string_address) ".log");
        close(one);
        unlink(pattern);

        //      mkdtemp makes a directory and nothing else can enter it.
        test_path(built, (string_address) "dirXXXXXX");
        string_copy(pattern, built);
        name = mkdtemp(pattern);
        true_is("mkdtemp answered with its template", name == pattern);
        same("mkdtemp made the directory", stat(pattern, address_of facts), 0);
        true_is("mkdtemp made a directory", S_ISDIR(facts.st_mode) != 0);
        same("mkdtemp made it private", (bipolar)(facts.st_mode & 0777),
             0700);
        rmdir(pattern);

        string_copy(pattern, (string_address) "/tmp/pd-no-x");
        errno = 0;
        true_is("mkdtemp refuses a bad template", is_null(mkdtemp(pattern)));
        same("mkdtemp with no X is EINVAL", errno, EINVAL);

        //      tmpnam hands back a name in /tmp that is not there yet.
        name = tmpnam(null);
        true_is("tmpnam answered", !is_null(name));
        true_is("tmpnam answered in /tmp",
                memory_compare(name, "/tmp/", 5) == 0);
        errno = 0;
        same("tmpnam's name was free", access(name, F_OK), -1);
        same("tmpnam's name was free, and that is why", errno, ENOENT);

        //      tmpfile is a stream with no name that still reads back what
        //      was written.
        handle = tmpfile();
        true_is("tmpfile opened a stream", !is_null(handle));

        if (!is_null(handle))
        {
                same("tmpfile took the bytes",
                     (bipolar)fwrite("abcdefg", 1, 7, handle), 7);
                same("tmpfile rewound", fseek(handle, 0, SEEK_SET), 0);
                same("tmpfile gave the bytes back",
                     (bipolar)fread(read_back, 1, 7, handle), 7);
                same("tmpfile kept them intact",
                     memory_compare(read_back, "abcdefg", 7), 0);
                fclose(handle);
        }
        else
        {
                checks += 4;
                failures += 4;
        }
}

//      -- remove ---------------------------------------------------------------

static fn test_remove(void)
{
        p8 path[PATH_MAX];
        p8 inside[PATH_MAX];
        b32 handle;

        test_path(path, (string_address) "removable");
        handle = creat(path, 0644);
        close(handle);
        same("remove took the file away", remove(path), 0);
        errno = 0;
        same("the file is gone", access(path, F_OK), -1);

        test_path(path, (string_address) "removable_directory");
        mkdir(path, 0755);
        same("remove took the directory away", remove(path), 0);

        test_path(path, (string_address) "full_directory");
        mkdir(path, 0755);
        path_join(inside, PATH_MAX, path, (string_address) "occupant");
        handle = creat(inside, 0644);
        close(handle);
        errno = 0;
        same("remove refuses a directory with something in it", remove(path),
             -1);
        same("remove on a full directory is ENOTEMPTY", errno, ENOTEMPTY);
        unlink(inside);
        rmdir(path);

        test_path(path, (string_address) "never_existed");
        errno = 0;
        same("remove refuses a missing name", remove(path), -1);
        same("remove on a missing name is ENOENT", errno, ENOENT);
}

//      -- getopt ---------------------------------------------------------------

static fn test_getopt(void)
{
        string_address words[6];
        b32 answer;

        words[0] = (string_address) "prog";
        words[1] = (string_address) "-a";
        words[2] = (string_address) "-bvalue";
        words[3] = (string_address) "rest";
        words[4] = null;

        optind = 0;
        opterr = 0;

        answer = getopt(4, words, (string_address) "ab:");
        same("getopt took the first option", answer, 'a');
        true_is("getopt left no argument", is_null(optarg));

        answer = getopt(4, words, (string_address) "ab:");
        same("getopt took the second option", answer, 'b');
        same_text("getopt took the attached argument", optarg,
                  (string_address) "value");

        answer = getopt(4, words, (string_address) "ab:");
        same("getopt stopped at the operand", answer, -1);
        same("getopt left optind on the operand", optind, 3);
        same_text("and that operand is where it was", words[optind],
                  (string_address) "rest");

        //      A missing argument, both ways of hearing about it.
        words[1] = (string_address) "-b";
        words[2] = null;
        optind = 0;
        answer = getopt(2, words, (string_address) "ab:");
        same("getopt reported a missing argument", answer, '?');
        same("getopt said which option", optopt, 'b');

        optind = 0;
        answer = getopt(2, words, (string_address) ":ab:");
        same("getopt in silent mode reports a colon", answer, ':');
        same("getopt in silent mode still says which", optopt, 'b');

        //      A double dash ends the options and is consumed.
        words[1] = (string_address) "--";
        words[2] = (string_address) "-a";
        words[3] = null;
        optind = 0;
        same("getopt stopped at the double dash",
             getopt(3, words, (string_address) "ab:"), -1);
        same("getopt consumed the double dash", optind, 2);

        opterr = 1;
}

//      -- assert -----------------------------------------------------------------

/*
        assert has to be checked in a child, because a passing assert is
        invisible and a failing one takes the process with it.

        The child sends its diagnostic to a file rather than to the terminal,
        so that the parent can read the sentence back and check it is the one
        glibc writes; and the parent checks that the child died of SIGABRT
        rather than exiting, because an assert that returned would be worse
        than one that printed nothing.
*/
static fn test_assert(void)
{
        p8 path[PATH_MAX];
        p8 line[512];
        b32 status = 0;
        b32 child;
        b32 handle;
        bipolar length;

        //      A true assertion is not allowed to do anything at all.
        assert(1 == 1);
        assert(test_root[0] == '/');
        checks++;

        test_path(path, (string_address) "assert.out");

        log_flush();
        child = fork();

        if (child == 0)
        {
                handle = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                dup2(handle, 2);
                close(handle);

                assert(2 + 2 == 5);

                _exit(0);
        }

        waitpid(child, address_of status, 0);

        true_is("assert did not let the child exit", !WIFEXITED(status));
        true_is("assert killed the child", WIFSIGNALED(status) != 0);
        same("assert killed it with SIGABRT", WTERMSIG(status), SIGABRT);

        handle = open(path, O_RDONLY);
        length = read(handle, line, sizeof(line) - 1);
        close(handle);

        if (length < 0)
                length = 0;

        line[length] = end;

        true_is("assert wrote the expression it was given",
                !is_null(string_search(line,
                                       (string_address) "Assertion `2 + 2 == 5' failed.")));
        true_is("assert named the file",
                !is_null(string_search(line, (string_address) __FILE__)));
        true_is("assert named the function",
                !is_null(string_search(line, (string_address) "test_assert")));

        unlink(path);
}

//      -- the tear down -----------------------------------------------------------

static fn test_sweep_away(void)
{
        process_dirent address_to address_to list = null;
        p8 path[PATH_MAX];
        b32 count;
        b32 at;

        count = scandir(test_root, address_of list, test_keep_plain, null);

        for (at = 0; at < count; at++)
        {
                path_join(path, PATH_MAX, test_root, list[at]->d_name);

                if (unlink(path) < 0)
                        rmdir(path);

                free(list[at]);
        }

        free(list);
        rmdir(test_root);
}

b32 main(void)
{
        string_copy(test_root, (string_address) "/tmp/pd-test-XXXXXX");

        if (is_null(mkdtemp(test_root)))
        {
                string_format(log, "FAIL could not make a working directory\n");
                string_format(log, "1 checks, 1 failures\n");
                log_flush();
                return 1;
        }

        test_sleeping();
        test_directories();
        test_scandir();
        test_versions();
        test_names();
        test_realpath();
        test_exec();
        test_close_except();
        test_kmsg_write();
        test_temporary();
        test_remove();
        test_getopt();
        test_assert();
        test_sweep_away();

        return test_report(null);
}
#endif /* CHECK_process */

#ifdef CHECK_signal
#include "../../src/lib.util.c"

/*
        <signal.h>, and the half of setjmp that was missing, on three machines.

        The signal section of src/lib.util.c was included directly when the
        umbrella had not already pulled it in, so this test builds and runs
        before the merge that adds that line to src/lib.util.c and stays correct after
        it: the file guards itself, and the guard is what this asks about.

        Every case is in the table CHECK_signal shares with
        CHECK_signal_reference, which builds it against the host's glibc. The two
        are supposed to print the same verdict, and a difference is a real
        disagreement about behaviour rather than a difference of opinion about
        what to test.

        The three things this lane is really for, and none of them are visible
        in a test that only checks that a handler ran:

          a handler RETURNS. On x86_64 nothing returns from a handler unless
          sa_restorer points at a two instruction trampoline that calls
          rt_sigreturn, and the failure without one is SIGSEGV rather than a
          wrong answer. Building this file with that flag removed dies at 139
          on the first raise.

          sa_mask ARRIVES. riscv64's kernel struct sigaction has no restorer
          slot, so its mask is eight bytes earlier than on the other two.
          Building this file with the x86_64 layout forced on all three fails
          exactly one check on exactly riscv64, and every other check still
          passes.

          the MASK survives the jump. A handler that leaves by longjmp never
          returns, so the signal it was handling stays blocked forever unless
          the mark wrote the mask down. sigsetjmp with a one is what writes it
          down and siglongjmp is what puts it back.
*/
#ifndef STANDARD_MODERN_C_STANDARD_SIGNAL
#include "../../src/lib.util.c"
#endif

#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

/*
        What the cases are written against.

        One name per routine and per type, so the case list mentions neither
        this library's spelling nor glibc's and can be compiled against
        either.
*/
typedef signal_set sig_set;
typedef signal_action sig_action;
typedef signal_handler sig_handler;
typedef jump_state sig_jump_state;

#define sig_errno errno
#define sig_zero(place, size) memory_zero((place), (size))

#define sig_set_empty(set) signal_set_empty(set)
#define sig_set_fill(set) signal_set_fill(set)
#define sig_set_add(set, number) signal_set_add((set), (number))
#define sig_set_remove(set, number) signal_set_remove((set), (number))
#define sig_set_has(set, number) signal_set_has((set), (number))

#define sig_action_change(number, wanted, previous)                           \
        signal_action_change((number), (wanted), (previous))
#define sig_handle(number, handler) signal_handle((number), (handler))
#define sig_raise(number) signal_raise(number)
#define sig_mask_change(how, wanted, previous)                                \
        signal_mask_change((how), (wanted), (previous))
#define sig_mask_suspend(mask) signal_mask_suspend(mask)
#define sig_mask_pending(into) signal_mask_pending(into)
#define sig_alarm(seconds) signal_alarm(seconds)
#define sig_wait() signal_wait()

#define sig_handler_of(action) ((action).handler)
#define sig_mask_of(action) ((action).mask)
#define sig_flags_of(action) ((action).flags)

//      Only the bits a caller is allowed to set. SA_RESTORER is not one of
//      them, and glibc hands it back on x86_64 where this library does not.
#define SIG_CALLER_FLAGS                                                      \
        (SA_NOCLDSTOP | SA_NOCLDWAIT | SA_SIGINFO | SA_ONSTACK | SA_RESTART | \
         SA_NODEFER | SA_RESETHAND)

#define sig_flags_asked(action) (sig_flags_of(action) & SIG_CALLER_FLAGS)

#define sig_jump_mark(state, save) signal_jump_mark((state), (save))
#define sig_jump_to_mark(state, value) signal_jump_to_mark((state), (value))

#define SHARED_signal_cases
#include "../checks.c"
#undef SHARED_signal_cases

b32 main(void)
{
        sig_cases_all();

        /*
                The two questions glibc answers differently on purpose, asked
                here in the shape this library answers them.

                A full set is full. glibc leaves thirty two and thirty three
                out because NPTL cancels threads and changes credentials with
                them and a program that blocked them would break its own
                runtime. There are no threads here and nothing reserved, so
                filling a set fills it.
        */
        {
                signal_set set;

                signal_set_fill(address_of set);

                check("a full set holds the first reserved signal",
                      signal_set_has(address_of set, 32) == 1);
                check("and the second",
                      signal_set_has(address_of set, 33) == 1);
                check("and the last signal there is",
                      signal_set_has(address_of set, 64) == 1);
        }

        /*
                A disposition read back holds only what the caller put in it.

                x86_64 needs a restorer and this library supplies one, so the
                flags the kernel holds have SA_RESTORER in them on that
                machine and not on the other two. Handing that back would make
                a program that saves a disposition and compares it against
                what it asked for disagree with itself on one architecture out
                of three. glibc does hand it back -- a read back there answers
                0x14000000 on x86_64 and 0x10000000 on arm64 and riscv64 --
                and this is the deliberate divergence.
        */
        {
                signal_action wanted;
                signal_action previous;

                memory_zero(address_of wanted, sizeof wanted);
                memory_zero(address_of previous, sizeof previous);

                wanted.handler = SIG_IGN;
                wanted.flags = SA_RESTART;

                signal_action_change(SIGUSR1, address_of wanted, null);
                signal_action_change(SIGUSR1, null, address_of previous);

                check("the flags read back are exactly the flags asked for",
                      previous.flags == SA_RESTART);

                signal_action_change(SIGUSR1, null, null);
                signal_handle(SIGUSR1, SIG_DFL);
        }

        /*
                The queued signals, which this library hands out from thirty
                two and glibc hands out from thirty four.

                glibc keeps thirty two and thirty three for NPTL and its
                SIGRTMIN is therefore two above the kernel's. There are no
                threads here and nothing reserved, so SIGRTMIN is the kernel's
                own, and the claim that makes that safe rather than merely
                different is the one checked below: both are ordinary signals
                that take a handler, deliver, and return.

                It is checked rather than only documented because a constant
                that disagrees with glibc and has no test behind it is a trap
                for whoever ports the next program in.
        */
        {
                check("the queued range starts where the kernel starts it",
                      SIGRTMIN == 32);
                check("and ends at the last signal there is", SIGRTMAX == 64);

                sig_first_ran = 0;
                check("the first reserved signal takes a handler",
                      signal_handle(SIGRTMIN, sig_note_first) != SIG_ERR);
                check("and delivers", signal_raise(SIGRTMIN) == 0 &&
                                              sig_first_ran == SIGRTMIN);

                sig_first_ran = 0;
                check("so does the second",
                      signal_handle(SIGRTMIN + 1, sig_note_first) != SIG_ERR);
                check("and delivers", signal_raise(SIGRTMIN + 1) == 0 &&
                                              sig_first_ran == SIGRTMIN + 1);

                signal_handle(SIGRTMIN, SIG_DFL);
                signal_handle(SIGRTMIN + 1, SIG_DFL);
        }

#ifndef STANDARD_SIGNAL_NO_DECLARATIONS
        /*
                And the thing this family can do that no other family in this
                library can yet: call the POSIX names as names.

                Everywhere else a standard spelling is a symbol with no
                prototype -- nm shows memcpy, and &memcpy does not compile.
                The signal section declares its sixteen and lets the .set
                supply the bodies, so the lines below are ordinary C calling
                sigemptyset and sigprocmask by those names.

                Under the same guard the declarations themselves carry, so
                that turning the block off in the library -- which is the
                remedy if a later family arrives with its own prototype for
                any of the sixteen -- turns this off with it and leaves a lane
                that still builds and still checks everything else. The rest
                of this file goes through the prose names and does not care.
        */
        {
                signal_set set;
                signal_set had;

                check("sigemptyset by name", sigemptyset(address_of set) == 0);
                check("sigaddset by name",
                      sigaddset(address_of set, SIGUSR2) == 0);
                check("sigismember by name",
                      sigismember(address_of set, SIGUSR2) == 1);
                check("sigprocmask by name",
                      sigprocmask(SIG_BLOCK, address_of set, address_of had) ==
                              0);
                check("raise by name on a blocked signal",
                      raise(SIGUSR2) == 0);
                check("sigpending by name",
                      sigpending(address_of had) == 0 &&
                              sigismember(address_of had, SIGUSR2) == 1);
                check("signal by name", signal(SIGUSR2, SIG_IGN) == SIG_DFL);
                check("sigprocmask by name again",
                      sigprocmask(SIG_UNBLOCK, address_of set, null) == 0);
                check("alarm by name", alarm(0) == 0);
                signal(SIGUSR2, SIG_DFL);
        }
#endif // STANDARD_SIGNAL_NO_DECLARATIONS

        return test_report(null);
}
#endif /* CHECK_signal */

#ifdef CHECK_signal_reference
#define _GNU_SOURCE

#include <errno.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/*
        The same cases, against the host's glibc.

        This is not built by the distribution and it links nothing the
        distribution ships. It exists so that every expectation in
        CHECK_signal is checked against the implementation those
        routines are specified by, on the machine the tests run on -- an
        expectation that only this library's signal section satisfies is an
        expectation that agrees with a bug.

        Build it beside the freestanding one and compare the verdicts:

            gcc -O2 -no-pie -w -DCHECK_signal_reference \
                -o /tmp/signal.reference test/checks.c

        -no-pie for the same reason the string reference wants it: the case
        file takes the addresses of handlers and compares them against what a
        disposition read back, and a position independent executable would
        route some of those through the PLT so that two names for one function
        stop comparing equal.

        This waits a real second in the alarm case, exactly as the
        freestanding build does.
*/

static unsigned long long checks;
static unsigned long long failures;

#define check(name, condition)                                          \
        do {                                                            \
                checks++;                                               \
                if (!(condition)) {                                     \
                        failures++;                                     \
                        printf("  FAIL " name "\n");                    \
                }                                                       \
        } while (0)

typedef sigset_t sig_set;
typedef struct sigaction sig_action;
typedef void (*sig_handler)(int);
typedef sigjmp_buf sig_jump_state;

#define sig_errno errno
#define sig_zero(place, size) memset((place), 0, (size))

#define sig_set_empty(set) sigemptyset(set)
#define sig_set_fill(set) sigfillset(set)
#define sig_set_add(set, number) sigaddset((set), (number))
#define sig_set_remove(set, number) sigdelset((set), (number))
#define sig_set_has(set, number) sigismember((set), (number))

#define sig_action_change(number, wanted, previous)                           \
        sigaction((number), (wanted), (previous))
#define sig_handle(number, handler) signal((number), (handler))
#define sig_raise(number) raise(number)
#define sig_mask_change(how, wanted, previous)                                \
        sigprocmask((how), (wanted), (previous))
#define sig_mask_suspend(mask) sigsuspend(mask)
#define sig_mask_pending(into) sigpending(into)
#define sig_alarm(seconds) alarm(seconds)
#define sig_wait() pause()

#define sig_handler_of(action) ((action).sa_handler)
#define sig_mask_of(action) ((action).sa_mask)
#define sig_flags_of(action) ((action).sa_flags)

//      Only the bits a caller is allowed to set. glibc hands SA_RESTORER back
//      on the architectures that have one, and that bit is not a caller's.
#define SIG_CALLER_FLAGS                                                      \
        (SA_NOCLDSTOP | SA_NOCLDWAIT | SA_SIGINFO | SA_ONSTACK | SA_RESTART | \
         SA_NODEFER | SA_RESETHAND)

#define sig_flags_asked(action) (sig_flags_of(action) & SIG_CALLER_FLAGS)

#define sig_jump_mark(state, save) sigsetjmp((state), (save))
#define sig_jump_to_mark(state, value) siglongjmp((state), (value))

#define SHARED_signal_cases
#include "../checks.c"
#undef SHARED_signal_cases

int main(void)
{
        sig_cases_all();

        /*
                The two questions this library answers differently on purpose,
                asked here in the shape glibc answers them, so that the
                divergence is written down as a passing check on both sides
                rather than as a count that does not match.

                glibc's sigfillset leaves thirty two and thirty three out.
                They are SIGCANCEL and SIGSETXID, NPTL's own, and a program
                that blocked them would break the runtime under it.
                The signal section has no threads to protect and fills all
                sixty four.
        */
        {
                sigset_t set;

                sigfillset(&set);

                check("glibc keeps the first reserved signal out of a full set",
                      sigismember(&set, 32) == 0);
                check("and the second", sigismember(&set, 33) == 0);
                check("and the last signal there is still goes in",
                      sigismember(&set, 64) == 1);
        }

        /*
                A disposition read back carries glibc's own restorer bit on
                the architectures that have one.

                Not spelled SA_RESTORER, because riscv64's headers do not
                define it -- the bit is asked about by value.

                x86_64 is the only one of the three where it comes back. That
                was measured rather than assumed, and the first version of
                this file assumed arm64 as well and was corrected by its own
                failure: arm64 defines SA_RESTORER and would honour one, but
                glibc does not supply one there and lets the kernel point x30
                at the vDSO trampoline instead -- which is the same choice
                the signal section makes. riscv64 has no such field at all.

                So glibc answers 0x14000000 on x86_64 and 0x10000000 on the
                other two. The signal section clears the bit on all three,
                so that what a caller reads back is what a caller asked for
                and is the same number on every machine.
        */
        {
                struct sigaction wanted;
                struct sigaction previous;
                int leaked;

                memset(&wanted, 0, sizeof wanted);
                memset(&previous, 0, sizeof previous);

                wanted.sa_handler = SIG_IGN;
                wanted.sa_flags = SA_RESTART;
                sigemptyset(&wanted.sa_mask);

                sigaction(SIGUSR1, &wanted, NULL);
                sigaction(SIGUSR1, NULL, &previous);

                leaked = (previous.sa_flags & 0x04000000) != 0;

                check("the flags a caller may set came back",
                      (previous.sa_flags & SIG_CALLER_FLAGS) == SA_RESTART);
#if defined(__x86_64__)
                check("and glibc's own restorer bit came back with them",
                      leaked == 1);
#else
                check("and glibc supplied no restorer on this machine",
                      leaked == 0);
#endif

                signal(SIGUSR1, SIG_DFL);
        }

        printf("%llu checks, %llu failures\n", checks, failures);

        return failures ? 1 : 0;
}
#endif /* CHECK_signal_reference */

#ifdef CHECK_lock
/*
        Experimental C standard library

        threads and the lock: that threads start, keep their own errno and
        block, block signals and give their stacks back; that the lock
        excludes and sleeps; that the allocator survives churn across threads;
        and what the lock and the allocator cost with and without company

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater

        www.dawning.dev
*/

/*
        This lane began as the answer to one question -- what would it cost
        to make the allocator, errno and the streams safe for a second thread
        -- and carried its own clone trampoline to ask it, because nothing in
        the tree made threads. thread_start in lib.c does now, with the
        thread block, CHILD_CLEARTID and a guard-paged stack, so the lane
        uses it and proves it instead.

        What it proves, in order:

        The thread runtime. Every thread's register points at its own block;
        errno is the block's and one thread's failures never show through in
        another; a started thread has every asynchronous signal blocked while
        the first thread's mask is untouched; threads_live counts exactly the
        threads started and not joined; and a storm of starts and joins gives
        every stack back, which is read off /proc/self/maps.

        The lock. Eight threads incrementing one counter lose nothing under
        it and, as a printed control, lose increments without it. A waiter
        really waits. The elided single-thread path moves the word through
        the same states. The shared spelling works across two processes over
        a MAP_SHARED page.

        The allocator. Eight threads allocate, fill, verify and free across
        the whole range of classes and mappings, and pass blocks between them
        so that one thread frees what another took. The first thread
        allocates cleanly afterwards from what the joined threads left.

        The process. A forked child of a threaded parent starts at zero
        threads, and a return from main with a thread still running ends the
        whole process, because exit is exit_group.

        The tree. parallel_tree's bytes equal a serial reference at every
        width under 256 descriptors, over thousands of siblings and a chain
        twelve hundred deep, with leaves, stops, nesting and removal.

        The pool. parallel_for runs every index once at every width;
        parallel_ordered's bytes do not move with the width; stops, a refusing
        sink, nesting, the beside job and a forked child behave as the sheet
        in lib.util.c says.

        The timing is BENCH_lock's: sh test/run bench lock.
*/
#include "../../src/lib.util.c"

#if defined(KERNEL_MODE) || defined(STANDARD_NO_PLATFORM)
#error "the lock is a Linux userspace primitive"
#endif

#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

#define LOCK_THREADS 8

//      -- small helpers ----------------------------------------------------

static fn lock_nap(positive nanoseconds)
{
        positive duration[2];

        duration[0] = nanoseconds / 1000000000ULL;
        duration[1] = nanoseconds % 1000000000ULL;

        system_call_2(syscall(nanosleep), (positive)address_of duration, 0);
}

#define LOCK_CLOCK_MONOTONIC 1

static positive lock_now(void)
{
        positive when[2] = {0, 0};

        system_call_2(syscall(clock_gettime), LOCK_CLOCK_MONOTONIC,
                      (positive)address_of when);

        return when[0] * 1000000000ULL + when[1];
}

//      The number of lines in /proc/self/maps: one per mapping, near enough
//      for a leak of eight megabyte stacks to show.
static positive lock_mappings(void)
{
        static p8 text[1 << 16];
        bipolar got = file_slurp((string_address)"/proc/self/maps", text,
                                 sizeof(text));
        positive lines = 0;
        positive at;

        if (got <= 0)
                return 0;

        for (at = 0; at < (positive)got; at++)
                lines += text[at] == '\n';

        return lines;
}

//      A gate so that every thread is running before any of them counts.
static volatile b32 lock_go = 0;

static fn lock_wait_for_go(void)
{
        while (atomic_load(address_of lock_go) == 0)
                thread_wait(address_of lock_go, 0);
}

static fn lock_open_gate(void)
{
        atomic_exchange(address_of lock_go, 1);
        thread_wake(address_of lock_go, 1 << 30);
}

//      -- the runtime -------------------------------------------------------

typedef struct
{
        positive index;
        thread address_to handle_seen;
        positive self_right;
        b32 address_to errno_address;
        b32 errno_kept;
        positive mask;
        positive ran;
} lock_identity;

static fn lock_identity_thread(address_any argument)
{
        lock_identity address_to it = argument;
        positive turn;
        positive mask[1] = {0};

        it->handle_seen = thread_self();
        it->self_right = thread_self()->self == thread_self() &&
                         thread_self() != address_of thread_main;
        it->errno_address = address_of errno;
        errno = (b32)(1000 + it->index);

        lock_wait_for_go();

        it->errno_kept = 1;

        for (turn = 0; turn < 20000; turn++)
        {
                if (errno != (b32)(1000 + it->index))
                        it->errno_kept = 0;

                errno = (b32)(1000 + it->index);

                if ((turn & 1023) == 0)
                        system_call(syscall(sched_yield));
        }

        system_call_4(syscall(rt_sigprocmask), 0, 0, (positive)address_of mask, 8);
        it->mask = mask[0];
        it->ran = 1;
}

#define LOCK_SIGNAL_BIT(number) (1ull << ((number) - 1))

static fn lock_runtime(void)
{
        lock_identity identity[LOCK_THREADS];
        thread address_to handles[LOCK_THREADS];
        positive main_mask_before[1] = {0};
        positive main_mask_after[1] = {0};
        positive started = 0;
        positive i;
        p8 distinct = true;
        p8 own = true;
        p8 kept = true;
        p8 blocked = true;

        check("a process that started no thread counts none", threads_live == 0);
        check("the first thread's register is thread_main",
              thread_self() == address_of thread_main);
        check("thread_main points at itself", thread_main.self == address_of thread_main);

        system_call_4(syscall(rt_sigprocmask), 0, 0,
                      (positive)address_of main_mask_before, 8);

        errno = 7;
        atomic_exchange(address_of lock_go, 0);

        for (i = 0; i < LOCK_THREADS; i++)
        {
                identity[i] = (lock_identity){.index = i};
                handles[i] = thread_start(lock_identity_thread, address_of identity[i]);
                started += handles[i] != null;
        }

        check("eight threads started", started == LOCK_THREADS);
        check("threads_live counts the eight", threads_live == started);

        system_call_4(syscall(rt_sigprocmask), 0, 0,
                      (positive)address_of main_mask_after, 8);
        check("starting threads left the first thread's signal mask alone",
              main_mask_before[0] == main_mask_after[0]);

        lock_open_gate();

        for (i = 0; i < LOCK_THREADS; i++)
                if (handles[i])
                        thread_join(handles[i]);

        check("every joined thread is out of the count", threads_live == 0);

        for (i = 0; i < LOCK_THREADS; i++)
        {
                positive j;

                //      Read from what the thread recorded: a joined block is
                //      unmapped, so the handle is only an address now.
                own = own && handles[i] && identity[i].handle_seen == handles[i] &&
                      identity[i].self_right;
                kept = kept && identity[i].ran && identity[i].errno_kept;
                blocked = blocked &&
                          (identity[i].mask & LOCK_SIGNAL_BIT(2)) &&
                          (identity[i].mask & LOCK_SIGNAL_BIT(10)) &&
                          (identity[i].mask & LOCK_SIGNAL_BIT(15)) &&
                          (identity[i].mask & LOCK_SIGNAL_BIT(17));

                for (j = 0; j < i; j++)
                        distinct = distinct && identity[i].errno_address !=
                                                       identity[j].errno_address;

                distinct = distinct && identity[i].errno_address != address_of errno;
        }

        check("each thread's register is its own block", own);
        check("each thread's errno is at its own address", distinct);
        check("no thread saw another's errno", kept);
        check("the first thread's errno survived eight others", errno == 7);
        check("a started thread blocks SIGINT, SIGUSR1, SIGTERM and SIGCHLD", blocked);
}

//      Starts and joins in rounds, and the mapping count after is the one
//      before: every stack went back.
static volatile positive lock_storm_ran = 0;

static fn lock_storm_thread(address_any argument)
{
        (void)argument;
        atomic_add(address_of lock_storm_ran, 1);
}

static fn lock_storm(void)
{
        thread address_to handles[LOCK_THREADS];
        positive before;
        positive after;
        positive round;
        positive i;
        positive wanted = 0;
        p8 all = true;

        //      Warm: the first round maps allocator chunks the rest reuse.
        for (i = 0; i < LOCK_THREADS; i++)
                handles[i] = thread_start(lock_storm_thread, null);
        for (i = 0; i < LOCK_THREADS; i++)
                if (handles[i])
                        thread_join(handles[i]), wanted++;

        before = lock_mappings();

        for (round = 0; round < 40; round++)
        {
                for (i = 0; i < LOCK_THREADS; i++)
                {
                        handles[i] = thread_start(lock_storm_thread, null);
                        all = all && handles[i];
                }

                for (i = 0; i < LOCK_THREADS; i++)
                        if (handles[i])
                                thread_join(handles[i]), wanted++;
        }

        after = lock_mappings();

        check("forty rounds of eight starts all started", all);
        check("every started thread ran its entry", lock_storm_ran == wanted);
        check("the storm left no thread counted", threads_live == 0);
        check("the storm gave every stack back", before && after <= before);

        string_format(log, "  storm: %p threads, mappings %p before and %p after\n",
                      wanted, before, after);
}

//      -- the lock ----------------------------------------------------------

static lock lock_guard = lock_start;
static volatile positive lock_counter = 0;

#define LOCK_TURNS 40000

static fn lock_racing_thread(address_any argument)
{
        positive turn = 0;

        (void)argument;
        lock_wait_for_go();

        while (turn < LOCK_TURNS)
        {
                lock_counter++;
                turn++;
        }
}

static fn lock_counting_thread(address_any argument)
{
        positive turn = 0;

        (void)argument;
        lock_wait_for_go();

        while (turn < LOCK_TURNS)
        {
                lock_take(address_of lock_guard);
                lock_counter++;
                lock_release(address_of lock_guard);
                turn++;
        }
}

static positive lock_count_with(fn(address_to entry)(address_any))
{
        thread address_to handles[LOCK_THREADS];
        positive i;
        positive started = 0;

        lock_counter = 0;
        atomic_exchange(address_of lock_go, 0);

        for (i = 0; i < LOCK_THREADS; i++)
        {
                handles[i] = thread_start(entry, null);
                started += handles[i] != null;
        }

        lock_open_gate();

        for (i = 0; i < LOCK_THREADS; i++)
                if (handles[i])
                        thread_join(handles[i]);

        return started == LOCK_THREADS ? lock_counter : 0;
}

static volatile positive lock_holder_inside = 0;
static volatile positive lock_waiter_saw = 0;

static fn lock_holding_thread(address_any argument)
{
        (void)argument;

        lock_take(address_of lock_guard);
        lock_holder_inside = 1;
        lock_nap(50000000);
        lock_holder_inside = 2;
        lock_release(address_of lock_guard);
}

static fn lock_waiting_thread(address_any argument)
{
        (void)argument;

        while (lock_holder_inside == 0)
                system_call(syscall(sched_yield));

        lock_take(address_of lock_guard);
        lock_waiter_saw = lock_holder_inside;
        lock_release(address_of lock_guard);
}

//      A thread that sleeps on a word until told to go, so that a test can
//      have another thread alive for as long as it needs one.
static volatile b32 lock_parked_release = 0;

static fn lock_parked_thread(address_any argument)
{
        (void)argument;

        while (atomic_load(address_of lock_parked_release) == 0)
                thread_wait(address_of lock_parked_release, 0);
}

static thread address_to lock_park(void)
{
        atomic_exchange(address_of lock_parked_release, 0);
        return thread_start(lock_parked_thread, null);
}

static fn lock_unpark(thread address_to parked)
{
        atomic_exchange(address_of lock_parked_release, 1);
        thread_wake(address_of lock_parked_release, 1 << 30);

        if (parked)
                thread_join(parked);
}

//      check() wants a literal name, so the label is pasted rather than
//      chosen at run time.
#define lock_states(label)                                                    \
        do                                                                    \
        {                                                                     \
                lock quiet = lock_start;                                      \
                                                                              \
                check(label ": a fresh lock is free",                         \
                      lock_state(address_of quiet) == LOCK_FREE);             \
                check(label ": try on a free lock succeeds",                  \
                      lock_try(address_of quiet));                            \
                check(label ": a tried lock reads held",                      \
                      lock_state(address_of quiet) == LOCK_HELD);             \
                check(label ": try on a held lock fails",                     \
                      !lock_try(address_of quiet));                           \
                lock_release(address_of quiet);                               \
                check(label ": release frees it",                             \
                      lock_state(address_of quiet) == LOCK_FREE);             \
                lock_take(address_of quiet);                                  \
                check(label ": take holds it",                                \
                      lock_state(address_of quiet) == LOCK_HELD);             \
                lock_release(address_of quiet);                               \
                check(label ": release after take frees it",                  \
                      lock_state(address_of quiet) == LOCK_FREE);             \
                /* A word a waiter left at 2 and then left is released. */    \
                quiet.word = LOCK_WAITED;                                     \
                lock_release(address_of quiet);                               \
                check(label ": a waited word releases to free",               \
                      lock_state(address_of quiet) == LOCK_FREE);             \
        } while (0)

static fn lock_exclusion(void)
{
        thread address_to parked;
        thread address_to holder;
        thread address_to waiter;
        positive counted;

        lock_states("alone");

        parked = lock_park();
        check("a parked thread started", parked != null);
        lock_states("with company");
        lock_unpark(parked);

        counted = lock_count_with(lock_counting_thread);
        check("eight threads lost no increments under the lock",
              counted == LOCK_TURNS * LOCK_THREADS);
        check("the lock came back free", lock_state(address_of lock_guard) == LOCK_FREE);
        string_format(log, "  counted %p of %p under the lock\n", counted,
                      (positive)(LOCK_TURNS * LOCK_THREADS));

        counted = lock_count_with(lock_racing_thread);
        string_format(log, "  counted %p of %p with no lock at all\n", counted,
                      (positive)(LOCK_TURNS * LOCK_THREADS));

        lock_holder_inside = 0;
        lock_waiter_saw = 0;
        holder = thread_start(lock_holding_thread, null);
        waiter = thread_start(lock_waiting_thread, null);

        if (holder)
                thread_join(holder);
        if (waiter)
                thread_join(waiter);

        check("the waiter got in only after the holder left",
              holder && waiter && lock_waiter_saw == 2);
}

//      -- the allocator -----------------------------------------------------

#define LOCK_CHURN_TURNS 30000
#define LOCK_CHURN_LIVE 48
#define LOCK_HANDOFF 64

static lock lock_handoff_guard = lock_start;
static address_any lock_handoff[LOCK_HANDOFF];
static positive lock_handoff_size[LOCK_HANDOFF];
static positive lock_handoff_count;
static volatile positive lock_churn_bad = 0;
static volatile positive lock_churn_failed = 0;

static p8 lock_churn_byte(positive size)
{
        return (p8)(size * 31 + (size >> 7));
}

static p8 lock_churn_intact(address_any block, positive size)
{
        p8 address_to bytes = block;
        p8 want = lock_churn_byte(size);

        return bytes[0] == want && bytes[size / 2] == want && bytes[size - 1] == want;
}

static fn lock_churn_thread(address_any argument)
{
        positive seed = 0x9e3779b97f4a7c15ull * ((positive)argument + 1);
        address_any live[LOCK_CHURN_LIVE] = {0};
        positive sizes[LOCK_CHURN_LIVE] = {0};
        positive turn;

        lock_wait_for_go();

        for (turn = 0; turn < LOCK_CHURN_TURNS; turn++)
        {
                positive slot;
                positive size;

                XORSHIFT64(seed);

                slot = seed % LOCK_CHURN_LIVE;
                size = (seed >> 8) % 4000 + 1;

                if ((seed >> 20) % 512 == 0)
                        size = 300000 + (seed >> 30) % 100000;

                if (live[slot])
                {
                        if (!lock_churn_intact(live[slot], sizes[slot]))
                                atomic_add(address_of lock_churn_bad, 1);

                        //      Every eighth block is handed to whichever
                        //      thread comes next, which frees what it takes.
                        if ((seed >> 40) % 8 == 0)
                        {
                                address_any taken = null;
                                positive taken_size = 0;

                                lock_take(address_of lock_handoff_guard);

                                if (lock_handoff_count)
                                {
                                        lock_handoff_count--;
                                        taken = lock_handoff[lock_handoff_count];
                                        taken_size = lock_handoff_size[lock_handoff_count];
                                }

                                if (lock_handoff_count < LOCK_HANDOFF)
                                {
                                        lock_handoff[lock_handoff_count] = live[slot];
                                        lock_handoff_size[lock_handoff_count] = sizes[slot];
                                        lock_handoff_count++;
                                        live[slot] = null;
                                }

                                lock_release(address_of lock_handoff_guard);

                                if (taken)
                                {
                                        if (!lock_churn_intact(taken, taken_size))
                                                atomic_add(address_of lock_churn_bad, 1);
                                        free(taken);
                                }
                        }

                        if (live[slot])
                                free(live[slot]);
                }

                live[slot] = malloc(size);
                sizes[slot] = size;

                if (!live[slot])
                {
                        atomic_add(address_of lock_churn_failed, 1);
                        continue;
                }

                memory_fill(live[slot], lock_churn_byte(size), size);
        }

        for (turn = 0; turn < LOCK_CHURN_LIVE; turn++)
                if (live[turn])
                {
                        if (!lock_churn_intact(live[turn], sizes[turn]))
                                atomic_add(address_of lock_churn_bad, 1);
                        free(live[turn]);
                }
}

static fn lock_allocator(void)
{
        thread address_to handles[LOCK_THREADS];
        address_any again[256];
        positive i;
        positive started = 0;
        p8 fine = true;

        atomic_exchange(address_of lock_go, 0);

        for (i = 0; i < LOCK_THREADS; i++)
        {
                handles[i] = thread_start(lock_churn_thread, (address_any)i);
                started += handles[i] != null;
        }

        lock_open_gate();

        for (i = 0; i < LOCK_THREADS; i++)
                if (handles[i])
                        thread_join(handles[i]);

        while (lock_handoff_count)
        {
                lock_handoff_count--;
                if (!lock_churn_intact(lock_handoff[lock_handoff_count],
                                       lock_handoff_size[lock_handoff_count]))
                        lock_churn_bad++;
                free(lock_handoff[lock_handoff_count]);
        }

        check("eight churning threads started", started == LOCK_THREADS);
        check("no block was overwritten by another thread", lock_churn_bad == 0);
        check("no allocation failed under churn", lock_churn_failed == 0);

        //      The first thread, alone again, takes from what they left.
        for (i = 0; i < 256; i++)
        {
                positive size = 16 + i * 13;

                again[i] = malloc(size);
                fine = fine && again[i];
                if (again[i])
                        memory_fill(again[i], lock_churn_byte(size), size);
        }

        for (i = 0; i < 256; i++)
                if (again[i])
                {
                        fine = fine && lock_churn_intact(again[i], 16 + i * 13);
                        free(again[i]);
                }

        check("the first thread allocates cleanly after the joins", fine);
}

/*
        A depot deeper than a fixed array ever held.

        Thirty two threads each allocate and free a run of blocks of one
        class, so each leaves a chain of it on its shelf, and all of them are
        joined. Every join stacks its chain on the class's depot through the
        chain's first block's tag; the depot these replaced held eight chains
        and walked a ninth to its tail under the lock. The checks read the
        stack itself -- one chain per join, every freed block on one of them
        -- and then take everything back through malloc on the first thread:
        every block the threads freed must come out again and none twice, and
        the depot must be as it was before.
*/
#define LOCK_DEPOT_THREADS 32
#define LOCK_DEPOT_MOST 40
#define LOCK_DEPOT_SIZE 5000
#define LOCK_DEPOT_NODES 4096

static address_any lock_depot_blocks[LOCK_DEPOT_THREADS][LOCK_DEPOT_MOST];
static volatile positive lock_depot_ready = 0;
static address_any lock_depot_taken[LOCK_DEPOT_NODES];

static fn lock_depot_thread(address_any argument)
{
        positive who = (positive)argument;
        positive count = 1 + who % LOCK_DEPOT_MOST;
        positive at;

        lock_wait_for_go();

        for (at = 0; at < count; at++)
                lock_depot_blocks[who][at] = malloc(LOCK_DEPOT_SIZE);

        for (at = 0; at < count; at++)
                free(lock_depot_blocks[who][at]);

        atomic_add(address_of lock_depot_ready, 1);
}

//      The chain below one on the depot, as allocator_take_shared reads it.
static address_any lock_depot_below(address_any chain)
{
        positive tag = address_to allocator_tag(chain);

        return tag >= ALLOCATOR_FREED + ALLOCATOR_CLASSES ? (address_any)tag : null;
}

static positive lock_depot_depth(b32 class)
{
        positive depth = 0;
        address_any chain;

        for (chain = allocator_depot[class]; chain; chain = lock_depot_below(chain))
                depth++;

        return depth;
}

static fn lock_depot(void)
{
        thread address_to handles[LOCK_DEPOT_THREADS];
        b32 class = allocator_class_of(LOCK_DEPOT_SIZE + ALLOCATOR_HEADER);
        positive before = lock_depot_depth(class);
        positive started = 0;
        positive nodes = 0;
        positive taken = 0;
        positive who, at, i, j;
        address_any chain;
        p8 on_chains = true;
        p8 came_back = true;
        p8 once = true;

        //      Whatever the first thread already holds of the class goes
        //      aside first, so that what malloc hands back below can only
        //      have come off the depot or been cut fresh.
        positive aside = 0;
        address_any held[64];

        while (allocator_free_list[class] && aside < 64)
                held[aside++] = malloc(LOCK_DEPOT_SIZE);

        atomic_exchange(address_of lock_go, 0);

        for (who = 0; who < LOCK_DEPOT_THREADS; who++)
        {
                handles[who] = thread_start(lock_depot_thread, (address_any)who);
                started += handles[who] != null;
        }

        lock_open_gate();

        while (atomic_load(address_of lock_depot_ready) < started)
                ;

        for (who = 0; who < LOCK_DEPOT_THREADS; who++)
                if (handles[who])
                        thread_join(handles[who]);

        check("thirty two chain-leaving threads started", started == LOCK_DEPOT_THREADS);
        check("every join stacked one chain on the depot",
              lock_depot_depth(class) == before + started);

        for (chain = allocator_depot[class]; chain; chain = lock_depot_below(chain))
                for (address_any block = chain; block; block = address_to allocator_link(block))
                        nodes++;

        for (who = 0; who < started; who++)
                for (at = 0; at < 1 + who % LOCK_DEPOT_MOST; at++)
                {
                        p8 found = false;

                        for (chain = allocator_depot[class]; chain && !found;
                             chain = lock_depot_below(chain))
                                for (address_any block = chain; block && !found;
                                     block = address_to allocator_link(block))
                                        found = block == lock_depot_blocks[who][at];

                        on_chains = on_chains && found;
                }

        check("every freed block is on a stacked chain", on_chains);

        while (taken < nodes && taken < LOCK_DEPOT_NODES)
        {
                lock_depot_taken[taken] = malloc(LOCK_DEPOT_SIZE);
                if (!lock_depot_taken[taken])
                        break;
                taken++;
        }

        for (who = 0; who < started; who++)
                for (at = 0; at < 1 + who % LOCK_DEPOT_MOST; at++)
                {
                        p8 found = false;

                        for (i = 0; i < taken && !found; i++)
                                found = lock_depot_taken[i] == lock_depot_blocks[who][at];

                        came_back = came_back && found;
                }

        for (i = 0; i < taken; i++)
                for (j = i + 1; j < taken; j++)
                        once = once && lock_depot_taken[i] != lock_depot_taken[j];

        check("every stacked block comes back out through malloc", came_back && taken == nodes);
        check("no stacked block comes back twice", once);
        //      Everything counted was every chain on the depot, any that were
        //      there before the threads included, so taking them all empties
        //      it.
        check("the depot is empty once every stacked block is taken",
              lock_depot_depth(class) == 0);

        for (i = 0; i < taken; i++)
                free(lock_depot_taken[i]);

        while (aside)
                free(held[--aside]);
}

//      -- the process -------------------------------------------------------

static fn lock_process(void)
{
        thread address_to parked = lock_park();
        b32 child;
        b32 raw = 0;
        positive started;

        check("a thread is alive across the fork", parked && threads_live == 1);

        log_flush();
        child = fork();

        if (child == 0)
                _exit(threads_live == 0 ? 0 : 1);

        if (child > 0)
        {
                system_wait4_retry(child, address_of raw, 0, null);
                check("a forked child of a threaded parent counts no threads",
                      (raw & 0x7f) == 0 && ((raw >> 8) & 0xff) == 0);
        }
        else
                check("the fork happened", false);

        lock_unpark(parked);

        //      exit is exit_group: a child whose thread is still asleep for ten
        //      seconds returns from main and is gone at once.
        log_flush();
        started = lock_now();
        child = fork();

        if (child == 0)
        {
                atomic_exchange(address_of lock_parked_release, 0);
                if (!thread_start(lock_parked_thread, null))
                        _exit(9);
                lock_nap(1000000);
                exit(5);
        }

        //      Waited for with the row's own five seconds as the deadline, and
        //      killed past it: under qemu-aarch64 8.2 this child never ended,
        //      and an unbounded wait held the lane for as long as nobody came.
        raw = 0;
        if (child > 0)
        {
                bipolar reaped = 0;

                while ((reaped = system_wait4_retry(child, address_of raw, 1, null)) == 0 &&
                       lock_now() - started < 5000000000ull)
                        lock_nap(10000000);

                if (reaped == 0)
                {
                        system_call_2(syscall(kill), (positive)child, 9);
                        system_wait4_retry(child, address_of raw, 0, null);
                        log_direct(str("  the exit child had not ended after five seconds, and was killed\n"));
                }
        }

        check("exit ends a process whose other thread is still asleep",
              child > 0 && (raw & 0x7f) == 0 && ((raw >> 8) & 0xff) == 5 &&
              lock_now() - started < 5000000000ull);
}

//      -- the pool ----------------------------------------------------------

/*
        The pool proves four things. Every index of a parallel_for runs
        exactly once at every width, and below the threshold it runs on the
        caller alone. parallel_ordered's bytes -- lengths from 0 to 3000 per
        job, half written through parallel_reserve and half through
        parallel_write -- hash the same at widths 1, 2, 3 and 8 as inline,
        every sink call comes on the first thread in index order, and no more
        than two outputs a thread are ever finished and waiting. A stop from
        a job or a false from the sink ends the run with no sink call after
        it. And the beside job, nesting, fork and exit behave as the API
        says.
*/
#define POOL_JOBS 4096

static positive pool_ran[POOL_JOBS];
static volatile positive pool_highest_slot = 0;
static volatile positive pool_nested_bad = 0;
static volatile positive pool_errno_bad = 0;
static volatile positive pool_outstanding = 0;
static volatile positive pool_outstanding_most = 0;
static volatile positive pool_jobs_run = 0;
static volatile positive pool_stop_index = positive_max;
static volatile b32 pool_stopped_word = 0;

static volatile positive pool_slot_out_of_range = 0;

static fn pool_note_slot(void)
{
        positive slot = parallel_slot();
        positive seen = atomic_load(address_of pool_highest_slot);

        //      Per-slot scratch is sized by parallel_slots(); a slot at or past
        //      it would index past every utility's array.
        if (slot >= parallel_slots())
                atomic_add(address_of pool_slot_out_of_range, 1);

        while (slot > seen &&
               !atomic_compare_exchange(address_of pool_highest_slot, seen, slot))
                seen = atomic_load(address_of pool_highest_slot);
}

static fn pool_nested_job(address_any context, positive index)
{
        atomic_add((positive address_to)context, index + 1);
}

static fn pool_count_job(address_any context, positive index)
{
        (void)context;
        atomic_add(address_of pool_ran[index], 1);
        atomic_add(address_of pool_jobs_run, 1);
        pool_note_slot();

        if (index == atomic_load(address_of pool_stop_index))
        {
                parallel_stop();
                atomic_exchange(address_of pool_stopped_word, 1);
                thread_wake(address_of pool_stopped_word, 1 << 30);
        }
        //      A job past the stop waits for it, so how many run is not a race
        //      with whether the stopping thread was descheduled (under qemu the
        //      others once ran all 4096 before it got back).
        else if (index > atomic_load(address_of pool_stop_index))
        {
                while (!atomic_load(address_of pool_stopped_word))
                        thread_wait(address_of pool_stopped_word, 0);
        }
}

static positive pool_length(positive index)
{
        positive mixed = index * 0x9e3779b97f4a7c15ull;

        mixed ^= mixed >> 29;
        return mixed % 3001;
}

static p8 pool_byte(positive index, positive at)
{
        return (p8)(index * 13 + at * 7);
}

static fn pool_emit_job(address_any context, positive index,
                        parallel_output address_to output)
{
        positive length = pool_length(index);
        positive at;
        b32 mine = (b32)(index * 3 + 1);
        positive most;
        positive now;

        (void)context;

        errno = mine;
        atomic_add(address_of pool_jobs_run, 1);
        pool_note_slot();

        //      A nested call runs inline, on this thread, and still answers.
        if (index % 97 == 0)
        {
                positive sum = 0;
                positive slot = parallel_slot();

                if (!parallel_for(pool_nested_job, address_of sum, 10, PARALLEL_SPREAD) ||
                    sum != 55 || parallel_slot() != slot)
                        atomic_add(address_of pool_nested_bad, 1);
        }

        if (index & 1)
        {
                p8 address_to span = parallel_reserve(output, length);

                if (span)
                        for (at = 0; at < length; at++)
                                span[at] = pool_byte(index, at);
        }
        else
        {
                //      Through the allocator, on whatever thread this is.
                p8 address_to scratch = malloc(length + 1);

                if (!scratch)
                        atomic_add(address_of pool_errno_bad, 1000);
                else
                {
                        for (at = 0; at < length; at++)
                                scratch[at] = pool_byte(index, at);

                        for (at = 0; at < length; at += 64)
                                parallel_write(output, scratch + at,
                                               length - at < 64 ? length - at : 64);

                        free(scratch);
                }
        }

        if (errno != mine)
                atomic_add(address_of pool_errno_bad, 1);

        now = atomic_add(address_of pool_outstanding, 1) + 1;
        most = atomic_load(address_of pool_outstanding_most);

        while (now > most &&
               !atomic_compare_exchange(address_of pool_outstanding_most, most, now))
                most = atomic_load(address_of pool_outstanding_most);

        if (index == atomic_load(address_of pool_stop_index))
                parallel_stop();
}

typedef struct
{
        positive next;
        positive calls;
        positive bytes;
        positive hash;
        positive wrong;
        positive refuse_at;
} pool_stream;

static p8 pool_sink(address_any context, positive index, address_any data,
                      positive length)
{
        pool_stream address_to stream = context;
        p8 address_to bytes = data;
        positive at;

        if (thread_self() != address_of thread_main || index != stream->next ||
            length != pool_length(index))
                stream->wrong++;

        for (at = 0; at < length; at++)
        {
                if (bytes[at] != pool_byte(index, at))
                {
                        stream->wrong++;
                        break;
                }

                stream->hash = (stream->hash ^ bytes[at]) * 0x100000001b3ull;
        }

        atomic_sub(address_of pool_outstanding, 1);
        stream->next = index + 1;
        stream->calls++;
        stream->bytes += length;

        return index != stream->refuse_at;
}

static fn pool_fresh_stream(pool_stream address_to stream)
{
        address_to stream = (pool_stream){0};
        stream->hash = 0xcbf29ce484222325ull;
        stream->refuse_at = positive_max;
        pool_outstanding = 0;
        pool_outstanding_most = 0;
        pool_jobs_run = 0;
        pool_highest_slot = 0;
}

//      The beside job: a producer filling a small ring the caller drains.
#define POOL_BESIDE_ITEMS 200000
#define POOL_BESIDE_RING 64

static lock pool_ring_guard = lock_start;
static positive pool_ring[POOL_BESIDE_RING];
static positive pool_ring_head = 0;
static positive pool_ring_tail = 0;
static volatile b32 pool_ring_count = 0;
static volatile positive pool_beside_slot = 0;

static fn pool_beside_producer(address_any context, positive index)
{
        positive item;

        (void)context;
        (void)index;
        pool_beside_slot = parallel_slot();

        for (item = 1; item <= POOL_BESIDE_ITEMS; item++)
        {
                for (;;)
                {
                        b32 count = atomic_load(address_of pool_ring_count);

                        if (count < POOL_BESIDE_RING)
                                break;

                        thread_wait(address_of pool_ring_count, count);
                }

                lock_take(address_of pool_ring_guard);
                pool_ring[pool_ring_tail] = item;
                pool_ring_tail = (pool_ring_tail + 1) % POOL_BESIDE_RING;
                lock_release(address_of pool_ring_guard);

                atomic_add(address_of pool_ring_count, 1);
                thread_wake(address_of pool_ring_count, 1);
        }
}

static fn pool_beside_stopper(address_any context, positive index)
{
        (void)context;
        (void)index;
        parallel_stop();
}

static volatile positive pool_beside_refused_inside = 0;

static fn pool_beside_inside_job(address_any context, positive index)
{
        (void)context;

        if (index == 0 && !parallel_beside(pool_beside_stopper, null))
                atomic_add(address_of pool_beside_refused_inside, 1);
}

static fn lock_pool(p8 emulated)
{
        static const positive widths[] = {1, 2, 3, 8};
        pool_stream reference;
        pool_stream stream;
        positive round;
        positive at;
        p8 every_once = true;
        p8 same = true;
        p8 bounded = true;
        p8 ordered = true;
        b32 child;
        b32 raw = 0;

        parallel_reset(0);
        check("the pool's width is at least one", parallel_width() >= 1);

        //      Below the threshold: the caller alone, in order.
        for (at = 0; at < POOL_JOBS; at++)
                pool_ran[at] = 0;
        pool_highest_slot = 0;
        check("a small parallel_for completes",
              parallel_for(pool_count_job, null, POOL_JOBS, PARALLEL_MINIMUM_BYTES - 1));
        check("a small parallel_for ran only on the caller", pool_highest_slot == 0);

        //      The reference stream, inline.
        pool_fresh_stream(address_of reference);
        check("an inline ordered run completes",
              parallel_ordered(pool_emit_job, pool_sink, address_of reference,
                               POOL_JOBS, 0));
        check("the inline run called the sink once per job in order",
              reference.calls == POOL_JOBS && reference.wrong == 0);

        for (round = 0; round < sizeof(widths) / sizeof(widths[0]); round++)
        {
                positive width = widths[round];

                parallel_reset(width);

                for (at = 0; at < POOL_JOBS; at++)
                        pool_ran[at] = 0;

                pool_stop_index = positive_max;
                pool_jobs_run = 0;

                if (!parallel_for(pool_count_job, null, POOL_JOBS, PARALLEL_SPREAD))
                        every_once = false;

                for (at = 0; at < POOL_JOBS; at++)
                        every_once = every_once && pool_ran[at] == 1;

                pool_fresh_stream(address_of stream);

                if (!parallel_ordered(pool_emit_job, pool_sink, address_of stream,
                                      POOL_JOBS, PARALLEL_SPREAD))
                        same = false;

                same = same && stream.hash == reference.hash &&
                       stream.bytes == reference.bytes && stream.calls == reference.calls;
                ordered = ordered && stream.wrong == 0;
                bounded = bounded && pool_outstanding_most <= 2 * width;

                string_format(log, "  pool: width %p, %p jobs, %p bytes, most waiting %p\n",
                              width, stream.calls, stream.bytes, pool_outstanding_most);
        }

        check("every index ran exactly once at widths 1, 2, 3 and 8", every_once);
        check("ordered bytes are the same at every width as inline", same);
        check("every sink call was on the first thread, in index order, with its job's bytes",
              ordered);
        check("no more than two outputs a thread ever waited", bounded);
        check("nested calls ran inline and answered", pool_nested_bad == 0);
        check("every job kept its own errno", pool_errno_bad == 0);
        check("every job's slot was below parallel_slots()", pool_slot_out_of_range == 0);

        //      Under an address-space limit the pool starts only the workers
        //      whose eight-megabyte stacks fit in a quarter of it, whatever
        //      width was asked for: 200 MB is six workers. Width 63 is forced so
        //      the answer does not depend on how many processors this has. A
        //      user-mode emulator keeps no address-space limit of the guest's
        //      (it reads back infinity after a successful setrlimit), so there
        //      the limit is not in force and the count cannot be checked.
        {
                positive saved[2] = {0, 0};
                positive lowered[2] = {0, 0};
                positive read_back[2] = {0, 0};

                system_call_4(syscall(prlimit64), 0, 9, 0, (positive)saved);
                lowered[0] = 200 * 1024 * 1024;
                lowered[1] = saved[1];
                system_call_4(syscall(prlimit64), 0, 9, (positive)lowered, 0);
                system_call_4(syscall(prlimit64), 0, 9, 0, (positive)read_back);

                parallel_reset(63);
                check("a parallel_for under a 200 MB address space completes",
                      parallel_for(pool_count_job, null, POOL_JOBS, PARALLEL_SPREAD));

                if (read_back[0] == lowered[0])
                        check("the pool starts only the workers the address space holds",
                              parallel_pool.workers <= 6);
                else
                        log_direct(str("  pool: the address-space limit is not kept under emulation, NOT RUN\n"));

                system_call_4(syscall(prlimit64), 0, 9, (positive)saved, 0);
                parallel_reset(0);
        }

        //      Back to back runs of every count the callers use. Each run wakes
        //      only as many workers as its count can use, so a count is a
        //      different wake every time: every index must run once per
        //      parallel_for, and every ordered run must call the sink once per
        //      job, in order.
        {
                static const positive stress_counts[] = {2, 3, 4, 5, 8, 9, 16, 33, 64};
                positive stress_round;
                p8 stress_exact = true;

                parallel_reset(8);

                for (stress_round = 0; stress_round < 4000; stress_round++)
                {
                        positive count = stress_counts[stress_round % (sizeof(stress_counts) / sizeof(stress_counts[0]))];
                        positive each;

                        pool_stop_index = positive_max;

                        for (each = 0; each < count; each++)
                                pool_ran[each] = 0;

                        if (!parallel_for(pool_count_job, null, count, PARALLEL_SPREAD))
                                stress_exact = false;

                        for (each = 0; each < count; each++)
                                stress_exact = stress_exact && pool_ran[each] == 1;

                        if (stress_round % 16 == 0)
                        {
                                pool_fresh_stream(address_of stream);

                                if (!parallel_ordered(pool_emit_job, pool_sink, address_of stream,
                                                      count, PARALLEL_SPREAD))
                                        stress_exact = false;

                                stress_exact = stress_exact && stream.calls == count && stream.wrong == 0;
                        }
                }

                check("back to back runs of 2 to 64 jobs run every index once, and ordered runs answer every sink call in order",
                      stress_exact);
        }

        //      Stopping, at width 8.
        parallel_reset(8);
        pool_stop_index = 300;
        pool_stopped_word = 0;
        pool_jobs_run = 0;
        check("a job's stop makes parallel_for answer false",
              !parallel_for(pool_count_job, null, POOL_JOBS, PARALLEL_SPREAD));
        check("a stopped parallel_for started at most one job a thread past the stop",
              pool_jobs_run <= 301 + 8);

        pool_fresh_stream(address_of stream);
        pool_stop_index = 300;
        check("a job's stop makes parallel_ordered answer false",
              !parallel_ordered(pool_emit_job, pool_sink, address_of stream,
                                POOL_JOBS, PARALLEL_SPREAD));
        check("a stopped ordered run started no job past its window",
              pool_jobs_run <= 300 + 2 * 8 + 1 && stream.next <= 300 && stream.wrong == 0);

        pool_fresh_stream(address_of stream);
        pool_stop_index = positive_max;
        stream.refuse_at = 500;
        check("a refusing sink makes parallel_ordered answer false",
              !parallel_ordered(pool_emit_job, pool_sink, address_of stream,
                                POOL_JOBS, PARALLEL_SPREAD));
        check("nothing reached the sink after it refused",
              stream.calls == 501 && stream.wrong == 0);
        check("after a stop the next call runs normally",
              parallel_for(pool_count_job, null, POOL_JOBS, PARALLEL_SPREAD));

        //      Beside.
        {
                positive taken = 0;
                positive sum = 0;

                pool_ring_head = pool_ring_tail = 0;
                pool_ring_count = 0;

                check("a beside job starts", parallel_beside(pool_beside_producer, null));
                check("a second beside job is refused while one runs",
                      !parallel_beside(pool_beside_stopper, null));

                while (taken < POOL_BESIDE_ITEMS)
                {
                        b32 count = atomic_load(address_of pool_ring_count);

                        if (!count)
                        {
                                thread_wait(address_of pool_ring_count, 0);
                                continue;
                        }

                        lock_take(address_of pool_ring_guard);
                        sum += pool_ring[pool_ring_head];
                        pool_ring_head = (pool_ring_head + 1) % POOL_BESIDE_RING;
                        lock_release(address_of pool_ring_guard);

                        atomic_sub(address_of pool_ring_count, 1);
                        thread_wake(address_of pool_ring_count, 1);
                        taken++;
                }

                check("the beside job joined cleanly", parallel_beside_wait());
                check("every item the beside job made arrived once",
                      sum == (positive)POOL_BESIDE_ITEMS * (POOL_BESIDE_ITEMS + 1) / 2);
                check("the beside thread is slot width", pool_beside_slot == 8);
                check("the beside thread's slot is below parallel_slots()",
                      pool_beside_slot < parallel_slots());

                check("a stopping beside job starts", parallel_beside(pool_beside_stopper, null));
                check("a stopped beside job joins false", !parallel_beside_wait());

                pool_beside_refused_inside = 0;
                parallel_for(pool_beside_inside_job, null, 4, PARALLEL_SPREAD);
                check("a job cannot start a beside job", pool_beside_refused_inside == 1);
        }

        //      Fork: the child starts its own workers. qemu-user 11.1 aborts
        //      in plugins/core.c qemu_plugin_vcpu_init__async when a forked
        //      child of a threaded process makes a thread, so under emulation
        //      the child proves the parent's pool forgotten and runs inline.
        log_flush();
        child = fork();

        if (child == 0)
        {
                if (emulated)
                        parallel_reset(1);

                for (at = 0; at < POOL_JOBS; at++)
                        pool_ran[at] = 0;

                if (!parallel_for(pool_count_job, null, POOL_JOBS, PARALLEL_SPREAD))
                        exit(2);

                for (at = 0; at < POOL_JOBS; at++)
                        if (pool_ran[at] != 1)
                                exit(3);

                exit(0);
        }

        if (child > 0)
                system_wait4_retry(child, address_of raw, 0, null);

        if (emulated)
        {
                log_direct(str("  pool: a forked child starting its own workers NOT RUN under emulation\n"));
                check("a forked child of a pooled parent forgets its pool",
                      child > 0 && (raw & 0x7f) == 0 && ((raw >> 8) & 0xff) == 0);
        }
        else
                check("a forked child of a pooled parent runs its own pool",
                      child > 0 && (raw & 0x7f) == 0 && ((raw >> 8) & 0xff) == 0);

        if (!(child > 0 && (raw & 0x7f) == 0 && ((raw >> 8) & 0xff) == 0))
                string_format(log, "  pool: forked child answered raw status %p\n",
                              (positive)(p32)raw);

        parallel_reset(0);
        check("a reset pool leaves no thread counted", threads_live == 0);
}

//      -- the tree ----------------------------------------------------------

/*
        parallel_tree over a tree built for the purpose, under a soft
        descriptor limit of 256: three thousand sibling directories with a
        file and a subdirectory each, a chain twelve hundred deep with leaf
        directories beside every level, a directory nobody may read, and a
        symbolic link that must not be entered. The probe writes each entry
        as it reads it and a subtree count in its leave, and hashes the
        stream in the sink. The same bytes must come at widths 1, 2, 3 and 8,
        and must equal a serial recursive reference that shares no code with
        the pool. A leave's handle must be the directory its enter saw, which
        is what proves the reopen after an eviction found the right one.
*/
#define TREE_WIDE 3000
#define TREE_DEEP 1200
#define TREE_CHAIN 300

typedef struct tree_probe
{
        struct tree_probe address_to parent;
        positive own;
        positive total;
        positive depth;
        positive device;
        positive inode;
        b32 opened;
        b32 spare;
} tree_probe;

static volatile positive tree_probe_live = 0;
static volatile positive tree_bad = 0;
static volatile positive tree_stop_at_depth = positive_max;
static volatile positive tree_leaves_run = 0;
static p8 tree_use_leaves = false;

static fn tree_number(p8 address_to into, positive address_to at, positive value)
{
        p8 digits[24];
        positive count = 0;

        do
        {
                digits[count++] = (p8)('0' + value % 10);
                value /= 10;
        } while (value);

        while (count)
                into[(address_to at)++] = digits[--count];
}

static fn tree_line(parallel_output address_to output, p8 mark, positive value)
{
        p8 line[32];
        positive at = 0;

        line[at++] = mark;
        tree_number(line, address_of at, value);
        line[at++] = '\n';
        parallel_write(output, line, at);
}

static tree_probe address_to tree_probe_new(tree_probe address_to parent)
{
        tree_probe address_to probe = malloc(sizeof(tree_probe));

        if (probe)
        {
                memory_fill(probe, 0, sizeof(tree_probe));
                probe->parent = parent;
                probe->depth = parent ? parent->depth + 1 : 0;
                atomic_add(address_of tree_probe_live, 1);
        }

        return probe;
}

static fn tree_facts(bipolar handle, positive address_to device, positive address_to inode)
{
        positive facts[24] = {0};

        system_call_2(syscall(fstat), (positive)handle, (positive)facts);
        address_to device = facts[0];
        address_to inode = facts[1];
}

//      A leaf: checks that the directory it was handed is its parent's, and
//      writes the parent's inode's low digits so its bytes are its own.
static fn tree_leaf_job(address_any context, address_any node, bipolar directory,
                        parallel_output address_to output)
{
        tree_probe address_to probe = node;
        positive device;
        positive inode;

        (void)context;
        atomic_add(address_of tree_leaves_run, 1);
        tree_facts(directory, address_of device, address_of inode);

        if (directory < 0 || inode != probe->parent->inode)
                atomic_add(address_of tree_bad, 1);

        tree_line(output, 'F', probe->own);
}

static fn tree_probe_enter(address_any context, address_any node, bipolar directory,
                           parallel_output address_to output)
{
        tree_probe address_to probe = node;
        p8 records[8192];

        (void)context;

        if (directory < 0)
        {
                tree_line(output, 'E', (positive)-directory);
                return;
        }

        probe->opened = 1;
        tree_facts(directory, address_of probe->device, address_of probe->inode);

        if (probe->depth == atomic_load(address_of tree_stop_at_depth))
                parallel_stop();

        for (;;)
        {
                bipolar got = system_read_directory(directory, records, sizeof(records));
                bipolar at = 0;

                if (got <= 0)
                {
                        if (got < 0)
                                tree_line(output, 'R', (positive)-got);
                        break;
                }

                while (at < got)
                {
                        struct linux_dirent64 address_to record =
                                (struct linux_dirent64 address_to)(records + at);
                        positive length;

                        at += record->d_reclen;

                        if (record->d_name[0] == '.' &&
                            (!record->d_name[1] ||
                             (record->d_name[1] == '.' && !record->d_name[2])))
                                continue;

                        length = string_length(record->d_name);
                        parallel_write(output, record->d_name, length);
                        tree_line(output, '/', record->d_type);
                        probe->own++;

                        if (record->d_type == DT_DIR)
                        {
                                tree_probe address_to child = tree_probe_new(probe);

                                if (!child || !parallel_child(output, record->d_name, child))
                                {
                                        if (child)
                                        {
                                                free(child);
                                                atomic_sub(address_of tree_probe_live, 1);
                                        }

                                        atomic_add(address_of tree_bad, 1);
                                }
                        }
                        else if (tree_use_leaves && record->d_type == DT_REG)
                        {
                                tree_probe address_to leaf = tree_probe_new(probe);

                                if (leaf)
                                        leaf->own = probe->own;

                                if (!leaf || !parallel_leaf(output, tree_leaf_job, leaf))
                                        atomic_add(address_of tree_bad, 1);
                        }
                }
        }
}

static fn tree_probe_leave(address_any context, address_any node, bipolar directory,
                           parallel_output address_to output)
{
        tree_probe address_to probe = node;
        positive total = atomic_load(address_of probe->total) + probe->own;

        (void)context;

        if (probe->opened)
        {
                positive device;
                positive inode;

                tree_facts(directory, address_of device, address_of inode);

                if (directory < 0 || device != probe->device || inode != probe->inode)
                        atomic_add(address_of tree_bad, 1);
        }

        tree_line(output, 'L', total);

        if (probe->parent)
                atomic_add(address_of probe->parent->total, total);
}

typedef struct
{
        positive hash;
        positive bytes;
        positive nodes;
        positive wrong;
        positive after_stop;
} tree_stream;

static fn tree_hash(tree_stream address_to stream, p8 address_to bytes, positive length)
{
        positive at;

        for (at = 0; at < length; at++)
                stream->hash = (stream->hash ^ bytes[at]) * 0x100000001b3ull;

        stream->bytes += length;
}

static p8 tree_probe_sink(address_any context, address_any node, address_any data,
                            positive length, p8 finished)
{
        tree_stream address_to stream = context;

        if (thread_self() != address_of thread_main)
                stream->wrong++;

        if (finished)
        {
                stream->nodes++;
                free(node);
                atomic_sub(address_of tree_probe_live, 1);
                return true;
        }

        //      The thread's run is an address_any; a tree's is its base, a parallel_run.
        if (thread_self()->run &&
            atomic_load(address_of ((parallel_run address_to)thread_self()->run)->stop) != 0)
                stream->after_stop++;

        tree_hash(stream, data, length);
        return true;
}

//      The reference: the same lines from a plain recursion, holding a
//      handle per level, run with the descriptor limit raised.
static fn tree_reference_write(tree_stream address_to stream, p8 address_to line,
                               positive length)
{
        tree_hash(stream, line, length);
}

static positive tree_reference(tree_stream address_to stream, bipolar directory,
                               p8 leaves)
{
        p8 address_to records = malloc(8192);
        positive own = 0;
        positive total = 0;
        p8 line[48];
        positive at;

        if (!records)
                return 0;

        for (;;)
        {
                bipolar got = system_read_directory(directory, records, 8192);
                bipolar offset = 0;

                if (got <= 0)
                {
                        if (got < 0)
                        {
                                at = 0;
                                line[at++] = 'R';
                                tree_number(line, address_of at, (positive)-got);
                                line[at++] = '\n';
                                tree_reference_write(stream, line, at);
                        }
                        break;
                }

                while (offset < got)
                {
                        struct linux_dirent64 address_to record =
                                (struct linux_dirent64 address_to)(records + offset);

                        offset += record->d_reclen;

                        if (record->d_name[0] == '.' &&
                            (!record->d_name[1] ||
                             (record->d_name[1] == '.' && !record->d_name[2])))
                                continue;

                        tree_reference_write(stream, record->d_name,
                                             string_length(record->d_name));
                        at = 0;
                        line[at++] = '/';
                        tree_number(line, address_of at, record->d_type);
                        line[at++] = '\n';
                        tree_reference_write(stream, line, at);
                        own++;

                        if (record->d_type == DT_DIR)
                        {
                                bipolar child = system_open_at(
                                        directory, record->d_name,
                                        FILE_READ | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);

                                if (child < 0)
                                {
                                        at = 0;
                                        line[at++] = 'E';
                                        tree_number(line, address_of at, (positive)-child);
                                        line[at++] = '\n';
                                        tree_reference_write(stream, line, at);
                                        tree_reference_write(stream, (p8 address_to)"L0\n", 3);
                                }
                                else
                                {
                                        total += tree_reference(stream, child, leaves);
                                        system_close(child);
                                }
                        }
                        else if (leaves && record->d_type == DT_REG)
                        {
                                at = 0;
                                line[at++] = 'F';
                                tree_number(line, address_of at, own);
                                line[at++] = '\n';
                                tree_reference_write(stream, line, at);
                        }
                }
        }

        free(records);

        at = 0;
        line[at++] = 'L';
        tree_number(line, address_of at, own + total);
        line[at++] = '\n';
        tree_reference_write(stream, line, at);

        return own + total;
}

static fn tree_limit(positive soft)
{
        positive now[2] = {0, 0};
        positive want[2];

        system_call_4(syscall(prlimit64), 0, 7, 0, (positive)address_of now);
        want[0] = soft < now[1] ? soft : now[1];
        want[1] = now[1];
        system_call_4(syscall(prlimit64), 0, 7, (positive)address_of want, 0);
}

static positive tree_open_count(void)
{
        bipolar handle = system_open_at(AT_FDCWD, (string_address)"/proc/self/fd",
                                        FILE_READ | O_DIRECTORY | O_CLOEXEC);
        p8 records[8192];
        positive count = 0;

        if (handle < 0)
                return 0;

        for (;;)
        {
                bipolar got = system_read_directory(handle, records, sizeof(records));
                bipolar at = 0;

                if (got <= 0)
                        break;

                while (at < got)
                {
                        struct linux_dirent64 address_to record =
                                (struct linux_dirent64 address_to)(records + at);

                        at += record->d_reclen;
                        count += record->d_name[0] != '.';
                }
        }

        system_close(handle);
        return count;
}

//      Building the tree, through handles all the way down.
static fn tree_make_file(bipolar directory, string_address name)
{
        bipolar handle = system_open_at_mode(directory, name,
                                             O_WRONLY | O_CREAT | O_CLOEXEC, 0644);

        if (handle >= 0)
                system_close(handle);
}

static bipolar tree_make_directory(bipolar directory, string_address name)
{
        system_call_3(syscall(mkdirat), (positive)directory, (positive)name, 0755);
        return system_open_at(directory, name, FILE_READ | O_DIRECTORY | O_CLOEXEC);
}

static fn tree_build(bipolar base)
{
        bipolar wide = tree_make_directory(base, (string_address)"wide");
        bipolar deep = tree_make_directory(base, (string_address)"deep");
        p8 name[16];
        positive i;

        for (i = 0; i < TREE_WIDE && wide >= 0; i++)
        {
                positive at = 0;
                bipolar member;

                name[at++] = 'w';
                tree_number(name, address_of at, i);
                name[at] = 0;
                member = tree_make_directory(wide, name);

                if (member < 0)
                        continue;

                tree_make_file(member, (string_address)"f");
                system_close(tree_make_directory(member, (string_address)"s"));

                if (i == 17)
                        system_call_4(syscall(fchmodat), (positive)member,
                                      (positive)"s", 0, 0);

                system_close(member);
        }

        system_call_4(syscall(symlinkat), (positive)".", (positive)base,
                      (positive)"loop", 0);

        for (i = 0; i < TREE_DEEP && deep >= 0; i++)
        {
                bipolar next;

                //      The leaf directories first, so that on file systems
                //      that list newest first the chain comes before them and
                //      every level stays pinned while the chain is walked.
                system_close(tree_make_directory(deep, (string_address)"e1"));
                system_close(tree_make_directory(deep, (string_address)"e2"));
                tree_make_file(deep, (string_address)"f");
                next = tree_make_directory(deep, (string_address)"d");
                system_close(deep);
                deep = next;
        }

        if (deep >= 0)
                system_close(deep);

        if (wide >= 0)
                system_close(wide);

        //      Nothing in it but the next one down.
        {
                bipolar chain = tree_make_directory(base, (string_address)"chain");

                for (i = 0; i < TREE_CHAIN && chain >= 0; i++)
                {
                        bipolar next = tree_make_directory(chain, (string_address)"c");

                        system_close(chain);
                        chain = next;
                }

                if (chain >= 0)
                        system_close(chain);
        }
}

//      And removing it with the pool: files in enter, emptied directories in
//      the parent's leave, which is the rm -r shape.
static fn tree_remove_enter(address_any context, address_any node, bipolar directory,
                            parallel_output address_to output)
{
        p8 records[8192];

        (void)context;
        (void)node;

        if (directory < 0)
                return;

        for (;;)
        {
                bipolar got = system_read_directory(directory, records, sizeof(records));
                bipolar at = 0;

                if (got <= 0)
                        break;

                while (at < got)
                {
                        struct linux_dirent64 address_to record =
                                (struct linux_dirent64 address_to)(records + at);

                        at += record->d_reclen;

                        if (record->d_name[0] == '.' &&
                            (!record->d_name[1] ||
                             (record->d_name[1] == '.' && !record->d_name[2])))
                                continue;

                        if (record->d_type == DT_DIR)
                        {
                                system_call_4(syscall(fchmodat), (positive)directory,
                                              (positive)record->d_name, 0700, 0);
                                parallel_child(output, record->d_name, null);
                        }
                        else
                                system_call_3(syscall(unlinkat), (positive)directory,
                                              (positive)record->d_name, 0);
                }
        }
}

static fn tree_remove_leave(address_any context, address_any node, bipolar directory,
                            parallel_output address_to output)
{
        p8 records[8192];

        (void)context;
        (void)node;
        (void)output;

        if (directory < 0)
                return;

        system_call_3(syscall(lseek), (positive)directory, 0, 0);

        for (;;)
        {
                bipolar got = system_read_directory(directory, records, sizeof(records));
                bipolar at = 0;

                if (got <= 0)
                        break;

                while (at < got)
                {
                        struct linux_dirent64 address_to record =
                                (struct linux_dirent64 address_to)(records + at);

                        at += record->d_reclen;

                        if (record->d_type == DT_DIR && record->d_name[0] != '.')
                                system_call_3(syscall(unlinkat), (positive)directory,
                                              (positive)record->d_name, 0x200);
                }
        }
}

static p8 tree_quiet_sink(address_any context, address_any node, address_any data,
                            positive length, p8 finished)
{
        (void)context;
        (void)node;
        (void)data;
        (void)length;
        (void)finished;
        return true;
}

typedef struct
{
        bipolar base;
        positive hash;
} tree_nested_context;

static fn tree_nested_job(address_any context, positive index)
{
        tree_nested_context address_to nested = context;
        tree_stream stream = {0xcbf29ce484222325ull, 0, 0, 0, 0};
        bipolar wide = system_open_at(nested->base, (string_address)"wide/w5",
                                      FILE_READ | O_DIRECTORY | O_CLOEXEC);
        tree_probe address_to root = tree_probe_new(null);

        (void)index;

        if (wide >= 0 && root)
                parallel_tree(tree_probe_enter, tree_probe_leave, tree_probe_sink,
                              address_of stream, wide, root, O_NOFOLLOW);

        if (wide >= 0)
                system_close(wide);

        nested->hash = stream.hash;
}

static fn lock_tree(void)
{
        static const positive widths[] = {1, 2, 3, 8};
        p8 path[256];
        positive at = 0;
        string_address place = (string_address)getenv("TMPDIR");
        bipolar base;
        tree_stream reference = {0xcbf29ce484222325ull, 0, 0, 0, 0};
        tree_stream leaf_reference = {0xcbf29ce484222325ull, 0, 0, 0, 0};
        positive round;
        p8 same = true;
        p8 leaves_same = true;
        p8 within = true;
        p8 evicted = true;
        p8 alone = true;
        positive handles_before;
        positive limits[2] = {0, 0};

        if (!place || !string_get(place) || string_length(place) > 180)
                place = (string_address)"/tmp";

        at = string_length(place);
        memory_copy(path, place, at);
        memory_copy(path + at, "/mt-tree-check-", 15);
        at += 15;
        tree_number(path, address_of at, (positive)system_call(syscall(getpid)));
        path[at] = 0;

        system_call_3(syscall(mkdirat), (positive)AT_FDCWD, (positive)path, 0700);
        base = system_open_at(AT_FDCWD, path, FILE_READ | O_DIRECTORY | O_CLOEXEC);

        if (base < 0)
        {
                check("the tree's scratch directory could be made", false);
                return;
        }

        tree_build(base);

        //      The reference, with room for twelve hundred handles.
        system_call_4(syscall(prlimit64), 0, 7, 0, (positive)address_of limits);
        tree_limit(4096);
        {
                bipolar again = system_open_at(base, (string_address)".",
                                               FILE_READ | O_DIRECTORY | O_CLOEXEC);
                bipolar twice = system_open_at(base, (string_address)".",
                                               FILE_READ | O_DIRECTORY | O_CLOEXEC);

                tree_reference(address_of reference, again, false);
                tree_reference(address_of leaf_reference, twice, true);
                system_close(again);
                system_close(twice);
        }

        tree_limit(256);
        handles_before = tree_open_count();

        for (round = 0; round < sizeof(widths) / sizeof(widths[0]); round++)
        {
                tree_stream stream = {0xcbf29ce484222325ull, 0, 0, 0, 0};
                tree_probe address_to root;
                bipolar top;
                p8 answered;

                parallel_reset(widths[round]);
                top = system_open_at(base, (string_address)".",
                                     FILE_READ | O_DIRECTORY | O_CLOEXEC);
                root = tree_probe_new(null);
                tree_use_leaves = false;

                answered = parallel_tree(tree_probe_enter, tree_probe_leave, tree_probe_sink,
                                         address_of stream, top, root, O_NOFOLLOW);
                system_close(top);

                same = same && answered && stream.hash == reference.hash &&
                       stream.bytes == reference.bytes;
                alone = alone && stream.wrong == 0;
                within = within && parallel_tree_last.open_most <= parallel_tree_last.cap;
                evicted = evicted && parallel_tree_last.evictions > 0 &&
                          parallel_tree_last.reopens > 0;

                string_format(log, "  tree: width %p, %p nodes, %p bytes, handles at most %p of %p, %p evictions, %p reopens\n",
                              widths[round], stream.nodes, stream.bytes,
                              parallel_tree_last.open_most, parallel_tree_last.cap,
                              parallel_tree_last.evictions, parallel_tree_last.reopens);

                //      Leaves: every regular file becomes its own job.
                stream = (tree_stream){0xcbf29ce484222325ull, 0, 0, 0, 0};
                top = system_open_at(base, (string_address)".",
                                     FILE_READ | O_DIRECTORY | O_CLOEXEC);
                root = tree_probe_new(null);
                tree_use_leaves = true;
                tree_leaves_run = 0;

                answered = parallel_tree(tree_probe_enter, tree_probe_leave, tree_probe_sink,
                                         address_of stream, top, root, O_NOFOLLOW);
                system_close(top);
                tree_use_leaves = false;

                leaves_same = leaves_same && answered &&
                              stream.hash == leaf_reference.hash &&
                              stream.bytes == leaf_reference.bytes &&
                              tree_leaves_run == TREE_WIDE + TREE_DEEP;
                alone = alone && stream.wrong == 0;
        }

        check("tree bytes are the reference's at widths 1, 2, 3 and 8 under 256 descriptors", same);
        check("tree bytes with a leaf per file are the reference's at every width", leaves_same);
        check("every tree sink call was on the first thread", alone);
        check("no tree job saw a wrong directory, reopened or pinned", tree_bad == 0);
        check("the tree never held more handles than its cap", within);
        check("a 1200-deep walk under 256 descriptors evicted and reopened handles", evicted);
        check("every tree node was finished to the sink", tree_probe_live == 0);
        check("a finished tree walk left no descriptor open",
              tree_open_count() == handles_before);

        //      A chain of directories has one node waiting at any time, and
        //      the walk takes it itself: handing the walk to the pool woke
        //      every thread for each level, 4,640 futex calls for 500 levels
        //      and six times the time of one thread walking them.
        {
                tree_stream serial = {0xcbf29ce484222325ull, 0, 0, 0, 0};
                tree_stream chained = {0xcbf29ce484222325ull, 0, 0, 0, 0};
                bipolar top = system_open_at(base, (string_address)"chain",
                                             FILE_READ | O_DIRECTORY | O_CLOEXEC);
                tree_probe address_to root = tree_probe_new(null);
                p8 answered;

                parallel_reset(1);
                answered = parallel_tree(tree_probe_enter, tree_probe_leave, tree_probe_sink,
                                         address_of serial, top, root, O_NOFOLLOW);
                system_close(top);

                top = system_open_at(base, (string_address)"chain",
                                     FILE_READ | O_DIRECTORY | O_CLOEXEC);
                root = tree_probe_new(null);
                parallel_reset(8);
                answered = answered && parallel_tree(tree_probe_enter, tree_probe_leave,
                                                     tree_probe_sink, address_of chained, top, root,
                                                     O_NOFOLLOW);
                system_close(top);

                check("a walk down a chain of directories gives what one thread's gives",
                      answered && chained.hash == serial.hash && chained.bytes == serial.bytes &&
                          chained.nodes == TREE_CHAIN + 1);
                check("a walk down a chain of directories gives none of it to the pool",
                      parallel_tree_last.threads == 1);
        }

        //      A stop deep in the chain.
        {
                tree_stream stream = {0xcbf29ce484222325ull, 0, 0, 0, 0};
                bipolar top = system_open_at(base, (string_address)".",
                                             FILE_READ | O_DIRECTORY | O_CLOEXEC);
                tree_probe address_to root = tree_probe_new(null);

                parallel_reset(8);
                tree_stop_at_depth = 600;
                check("a stopped tree walk answers false",
                      !parallel_tree(tree_probe_enter, tree_probe_leave, tree_probe_sink,
                                     address_of stream, top, root, O_NOFOLLOW));
                tree_stop_at_depth = positive_max;
                system_close(top);

                //      The emitter reads the stop before every span it hands over;
                //      a worker's stop landing between that read and the call
                //      is one span already on its way, and nothing comes after.
                check("at most one tree span reached the sink after the stop",
                      stream.after_stop <= 1);
                check("a stopped tree walk finished every node it made", tree_probe_live == 0);
                check("a stopped tree walk left no descriptor open",
                      tree_open_count() == handles_before);
        }

        //      Nested inside a job: runs on that thread, same bytes.
        {
                tree_nested_context nested = {base, 0};
                tree_stream alone_stream = {0xcbf29ce484222325ull, 0, 0, 0, 0};
                bipolar wide = system_open_at(base, (string_address)"wide/w5",
                                              FILE_READ | O_DIRECTORY | O_CLOEXEC);
                tree_probe address_to root = tree_probe_new(null);

                parallel_tree(tree_probe_enter, tree_probe_leave, tree_probe_sink,
                              address_of alone_stream, wide, root, O_NOFOLLOW);
                system_close(wide);

                parallel_for(tree_nested_job, address_of nested, 1, PARALLEL_SPREAD);
                check("a tree walk nested in a job gives the same bytes",
                      nested.hash == alone_stream.hash && tree_probe_live == 0);
        }

        //      Remove it all with the pool.
        {
                parallel_reset(8);
                parallel_tree(tree_remove_enter, tree_remove_leave, tree_quiet_sink, null,
                              base, null, O_NOFOLLOW);
                system_close(base);
                check("the pool removed the tree it walked",
                      system_call_3(syscall(unlinkat), (positive)AT_FDCWD,
                                    (positive)path, 0x200) == 0);
        }

        tree_limit(limits[0]);
        parallel_reset(0);
}

//      -- across processes --------------------------------------------------

#define LOCK_MAP_SHARED 1
#define LOCK_MAP_ANONYMOUS 0x20
#define LOCK_PROTECT_READ_WRITE 3
#define LOCK_SHARED_TURNS 20000

typedef struct
{
        lock guard;
        positive counter;
} lock_shared_page;

static fn lock_across_processes(void)
{
        lock_shared_page address_to page;
        b32 child;
        b32 raw = 0;
        positive turn;

        page = (lock_shared_page address_to)mmap(
                null, 4096, LOCK_PROTECT_READ_WRITE,
                LOCK_MAP_SHARED | LOCK_MAP_ANONYMOUS, -1, 0);

        if (page == MAP_FAILED)
        {
                check("a shared page could be mapped", false);
                return;
        }

        page->guard.word = LOCK_FREE;
        page->counter = 0;

        log_flush();
        child = fork();

        if (child < 0)
        {
                check("the second process started", false);
                munmap((address_any)page, 4096);
                return;
        }

        if (child == 0)
        {
                for (turn = 0; turn < LOCK_SHARED_TURNS; turn++)
                {
                        lock_take_shared(address_of page->guard);
                        page->counter++;
                        lock_release_shared(address_of page->guard);
                }

                _exit(0);
        }

        for (turn = 0; turn < LOCK_SHARED_TURNS; turn++)
        {
                lock_take_shared(address_of page->guard);
                page->counter++;
                lock_release_shared(address_of page->guard);
        }

        system_wait4_retry(child, address_of raw, 0, null);

        check("two processes lost no increments",
              page->counter == LOCK_SHARED_TURNS * 2);
        check("the shared lock came back free", page->guard.word == LOCK_FREE);

        munmap((address_any)page, 4096);
}

/*
        parallel_lines: a file's lines, in pieces of whole lines cut by the
        jobs that read them. The content is made here and the pieces are held
        to the bytes: they must be contiguous, each beginning where a line
        does, complete and in order at every width, whatever the chunk, from
        a start in the middle of a line, with a last line that has no end;
        and where a piece is refused, or a line is too long to cut, what was
        taken must end on a line's edge for a serial loop to go on from.
*/
#define LINES_MAX 3200000
#define LINES_PIECES 4096

static p8 lines_data[LINES_MAX];
static positive lines_piece_bytes[LINES_PIECES];
static positive lines_piece_begin[LINES_PIECES];
static positive lines_wrong;
static positive lines_refuse_at;
static positive lines_sink_hash;
static positive lines_sink_bytes;
static positive lines_sink_wrong;
static positive lines_sink_next;
static positive lines_start;
static positive lines_expected_offset;

static positive lines_fnv(positive hash, const p8 address_to bytes, positive length)
{
        for (positive at = 0; at < length; at++)
                hash = (hash ^ bytes[at]) * 0x100000001b3ull;

        return hash;
}

//      Lines of every length up to a few hundred, empty ones among them, and
//      where long is set, one of more than the bound of a piece and one of
//      a few kilobytes; ended is whether the last of them ends.
static positive lines_make(p8 long_line, p8 ended)
{
        positive at = 0;
        positive seed = 0x9e3779b97f4a7c15ull;
        positive number = 0;

        while (at < 3000000)
        {
                positive length;

                seed = seed * 6364136223846793005ull + 1442695040888963407ull;
                length = (seed >> 33) % 300;

                if (!(number % 50))
                        length = 0;

                if (long_line && number == 4000)
                        length = 900000;
                else if (number % 700 == 3)
                        length = 3000 + (seed >> 40) % 3000;

                for (positive c = 0; c < length; c++)
                        lines_data[at + c] = (p8)(0x20 + (c * 7 + number) % 95);

                at += length;
                lines_data[at++] = '\n';
                number++;
        }

        if (!ended)
                at--;

        return at;
}

static p8 lines_kernel(address_any context, positive index, p8 address_to lines,
                         positive length, positive offset)
{
        (void)context;

        if (index == lines_refuse_at)
                return false;

        //      A piece with no line beginning in it is empty and stands anywhere.
        if (length && (memory_compare(lines, lines_data + offset, length) ||
                       (offset > lines_start && lines_data[offset - 1] != '\n') ||
                       (offset + length < lines_expected_offset && lines[length - 1] != '\n')))
                atomic_add(address_of lines_wrong, 1);

        lines_piece_bytes[index] = length;
        lines_piece_begin[index] = offset;
        return true;
}

static p8 lines_emit(address_any context, positive index, p8 address_to lines,
                       positive length, positive offset,
                       parallel_output address_to output)
{
        if (!lines_kernel(context, index, lines, length, offset))
                return false;

        return parallel_write(output, lines, length);
}

static p8 lines_sink(address_any context, positive index, address_any data,
                       positive length)
{
        (void)context;

        if (index != lines_sink_next++)
                lines_sink_wrong++;

        lines_sink_hash = lines_fnv(lines_sink_hash, data, length);
        lines_sink_bytes += length;
        return true;
}

static fn lock_lines_one(string_address name, positive size, positive start, positive chunk,
                         p8 address_to whole, p8 address_to prefix)
{
        static const positive widths[] = {2, 3, 8};
        bipolar handle = system_open_at(AT_FDCWD, name, FILE_READ | O_CLOEXEC);
        positive round;
        positive reference = lines_fnv(0xcbf29ce484222325ull, lines_data + start, size - start);

        lines_start = start;

        for (round = 0; round < sizeof(widths) / sizeof(widths[0]); round++)
        {
                parallel_lines run;
                positive at;
                positive sum = 0;
                positive count;

                parallel_reset(widths[round]);

                //      Every piece, counted.
                if (!parallel_lines_open(address_of run, (positive)handle, start, size, '\n', chunk))
                {
                        address_to whole = false;
                        break;
                }

                count = run.count;
                lines_wrong = 0;
                lines_expected_offset = size;
                lines_refuse_at = positive_max;
                parallel_lines_for(address_of run, lines_kernel, null);

                for (at = 0; at < run.taken; at++)
                        sum += lines_piece_bytes[at];

                if (run.taken != count || run.resume != size || sum != size - start ||
                    lines_wrong)
                {
                        address_to whole = false;
                        string_format(log, "  lines: counted, start %p chunk %p width %p: "
                                      "taken %p of %p, resume %p of %p, bytes %p of %p, wrong %p\n",
                                      start, chunk, widths[round], run.taken, count, run.resume,
                                      size, sum, size - start, lines_wrong);
                }

                //      In order, the bytes the file has.
                parallel_lines_again(address_of run);
                lines_sink_hash = 0xcbf29ce484222325ull;
                lines_sink_bytes = 0;
                lines_sink_next = 0;
                lines_sink_wrong = 0;
                lines_wrong = 0;
                parallel_lines_ordered(address_of run, lines_emit, lines_sink, null);

                if (run.taken != count || run.resume != size || lines_wrong || lines_sink_wrong ||
                    lines_sink_hash != reference || lines_sink_bytes != size - start)
                {
                        address_to whole = false;
                        string_format(log, "  lines: ordered, start %p chunk %p width %p: "
                                      "taken %p of %p, resume %p of %p, bytes %p of %p, wrong %p, out of order %p\n",
                                      start, chunk, widths[round], run.taken, count, run.resume,
                                      size, lines_sink_bytes, size - start, lines_wrong,
                                      lines_sink_wrong);
                }

                //      A piece refused: what came before it, and where that ends.
                parallel_lines_again(address_of run);
                lines_refuse_at = count / 2;
                lines_wrong = 0;
                parallel_lines_for(address_of run, lines_kernel, null);

                if (count > 2 && lines_refuse_at)
                {
                        positive edge = run.resume;

                        if (run.taken != lines_refuse_at || edge <= start ||
                            lines_data[edge - 1] != '\n' || lines_wrong ||
                            (run.taken && edge != lines_piece_begin[run.taken - 1] +
                                                  lines_piece_bytes[run.taken - 1]))
                                address_to prefix = false;
                }

                parallel_lines_again(address_of run);
                lines_sink_hash = 0xcbf29ce484222325ull;
                lines_sink_bytes = 0;
                lines_sink_next = 0;
                lines_sink_wrong = 0;
                lines_refuse_at = count / 2;
                parallel_lines_ordered(address_of run, lines_emit, lines_sink, null);

                if (count > 2 && lines_refuse_at &&
                    (run.taken != lines_refuse_at || run.resume <= start || lines_sink_wrong ||
                     lines_sink_hash != lines_fnv(0xcbf29ce484222325ull, lines_data + start,
                                                  run.resume - start)))
                        address_to prefix = false;

                lines_refuse_at = positive_max;
                parallel_lines_close(address_of run);
        }

        system_close((positive)handle);
}

static fn lock_lines(void)
{
        p8 path[256];
        positive at = 0;
        string_address place = (string_address)getenv("TMPDIR");
        p8 whole = true;
        p8 prefix = true;
        p8 long_stops = true;
        bipolar handle;
        positive size;
        positive variant;

        if (!place || !string_get(place) || string_length(place) > 180)
                place = (string_address)"/tmp";

        at = string_length(place);
        memory_copy(path, place, at);
        memory_copy(path + at, "/mt-lines-check-", 16);
        at += 16;
        tree_number(path, address_of at, (positive)system_call(syscall(getpid)));
        path[at] = 0;

        for (variant = 0; variant < 4; variant++)
        {
                //      0 plain, 1 with no last newline, 2 with a line past the bound,
                //      3 as 0 from the middle of a line.
                size = lines_make(variant == 2, variant != 1);
                handle = system_open_at_mode(AT_FDCWD, (string_address)path,
                                             O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);

                if (handle < 0)
                {
                        check("the lines file could be made", false);
                        return;
                }

                {
                        positive done = 0;

                        while (done < size)
                        {
                                bipolar wrote = system_call_3(syscall(write), (positive)handle,
                                                              (positive)(lines_data + done),
                                                              size - done);

                                if (wrote <= 0)
                                        break;

                                done += (positive)wrote;
                        }
                }

                system_close((positive)handle);

                if (variant == 2)
                {
                        //      The long line ends every run at or before the piece that
                        //      holds it; nothing after it is taken.
                        static const positive chunks[] = {4096, 30000};
                        positive round;

                        for (round = 0; round < 2; round++)
                        {
                                parallel_lines run;
                                positive long_at = 0;
                                positive number = 0;

                                handle = system_open_at(AT_FDCWD, (string_address)path,
                                                        FILE_READ | O_CLOEXEC);
                                parallel_reset(3);

                                for (long_at = 0; number < 4000; long_at++)
                                        number += lines_data[long_at] == '\n';

                                lines_refuse_at = positive_max;
                                lines_wrong = 0;
                                lines_start = 0;
                                lines_expected_offset = size;

                                if (!parallel_lines_open(address_of run, (positive)handle, 0, size,
                                                         '\n', chunks[round]))
                                {
                                        long_stops = false;
                                        system_close((positive)handle);
                                        continue;
                                }

                                parallel_lines_for(address_of run, lines_kernel, null);
                                long_stops = long_stops && run.taken < run.count &&
                                             run.resume <= long_at && run.resume > 0 &&
                                             lines_data[run.resume - 1] == '\n' &&
                                             lines_wrong == 0;
                                parallel_lines_close(address_of run);
                                system_close((positive)handle);
                        }
                }
                else
                {
                        lock_lines_one((string_address)path, size, variant == 3 ? 1234 : 0,
                                       4096, address_of whole, address_of prefix);
                        lock_lines_one((string_address)path, size, variant == 3 ? 777 : 0,
                                       30000, address_of whole, address_of prefix);
                }
        }

        system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)path, 0);
        parallel_reset(0);
        check("pieces are contiguous, whole, in order and complete at widths 2, 3 and 8, "
              "from any start and with or without a last newline",
              whole);
        check("a refused piece ends what was taken on the edge of a line", prefix);
        check("a line past the bound stops the pieces at or before it", long_stops);
}

//      -- the lane ----------------------------------------------------------

/*
        A directory made with an exact mode while another thread creates
        files. The mode is set after the umask has taken its bits; a version
        that moved the process umask to zero for the length of the call would
        hand a file the other thread creates in that window its full 0666
        instead of 0600. The maker thread makes exact 0700 directories under
        a umask of 0077 while this thread creates files: each file must be
        0600 when its mode is read back.
*/
#define LOCK_UMASK_ROUNDS 4000

static positive lock_umask_name(char address_to into, char tag, positive value)
{
        char digits[24];
        positive count = 0, at;

        do
        {
                digits[count++] = (char)('0' + value % 10);
                value /= 10;
        } while (value);
        into[0] = tag;
        for (at = 0; at < count; at++)
                into[1 + at] = digits[count - 1 - at];
        into[1 + count] = 0;
        return count + 1;
}

static fn lock_umask_maker(address_any argument)
{
        char name[32];

        (void)argument;
        for (positive i = 0; i < LOCK_UMASK_ROUNDS; i++)
        {
                lock_umask_name(name, 'd', i);
                system_make_directory_exact_at(AT_FDCWD, (string_address)name, 0700);
        }
}

static fn lock_umask(void)
{
        positive saved = system_call_1(syscall(umask), 0077);
        positive made = 0, wrong = 0;
        thread address_to handle = thread_start(lock_umask_maker, null);
        char name[32];

        for (positive i = 0; i < LOCK_UMASK_ROUNDS; i++)
        {
                system_path_identity found;
                lock_umask_name(name, 'f', i);
                bipolar fd = system_call_4(syscall(openat), (positive)AT_FDCWD,
                                           (positive)name,
                                           O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0666);
                if (fd < 0)
                        continue;
                system_close(fd);
                made++;
                if (system_path_identity_at(AT_FDCWD, (string_address)name, 0,
                                            SYSTEM_PATH_STATX_IDENTITY, address_of found) < 0 ||
                    (found.mode & 07777) != 0600)
                        wrong++;
        }

        if (handle)
                thread_join(handle);
        check("a file created beside exact directories keeps the umask's mode", made > 0 && wrong == 0);

        for (positive i = 0; i < LOCK_UMASK_ROUNDS; i++)
        {
                lock_umask_name(name, 'f', i);
                system_remove_at(AT_FDCWD, (string_address)name, 0);
                lock_umask_name(name, 'd', i);
                system_remove_at(AT_FDCWD, (string_address)name, AT_REMOVEDIR);
        }
        system_call_1(syscall(umask), saved);
}

/*
        A walk's identity for a handle the kernel refuses. The fstat fails, and
        the answer must say so and leave the identity the caller held alone;
        a version that ignored the failed fstat stored device 0 and inode 0,
        which a later pin then compared against.
*/
static fn lock_facts(void)
{
        positive device = 0x1111, inode = 0x2222;
        (void)parallel_tree_facts(-1, address_of device, address_of inode);

        check("a failed fstat leaves the identity the caller held",
              device == 0x1111 && inode == 0x2222);
}

b32 main(void)
{
        lock_runtime();
        lock_facts();
        lock_umask();
        lock_storm();
        lock_exclusion();
        lock_allocator();
        lock_depot();
        lock_process();
        lock_pool(program_argument_count() > 1 &&
                  !string_compare(program_argument(1), (string_address)"--emulated"));
        lock_tree();
        lock_lines();
        lock_across_processes();

        check("nothing is left counted at the end", threads_live == 0);

        return test_report((string_address) "\n");
}
#endif /* CHECK_lock */

#ifdef CHECK_socket
#include "../../src/lib.util.c"
#include "../../src/canvas/window.c"
/*
        Byte order, and the socket calls that need it.

        The reversal is checked against a reference that pulls the bytes out
        one at a time and rebuilds them in the other order, which shares no
        code with any of the three implementations. Sixteen bits is small
        enough to check completely. Thirty two is walked over every single bit
        and its complement, the values that have caught this kind of routine
        before, and a stride that does not divide any power of two -- the
        interesting failures are sign extension on riscv64, where the shifts
        are the only thing keeping bit 31 from spreading upward, so the values
        with that bit set are the ones written out by hand.

        The socket half opens a real listening socket on loopback, lets the
        kernel choose the port, connects to it and moves bytes through. That
        is the part a compile cannot check: whether socket_address_internet is
        the shape the kernel expects to be handed. getsockname is asked for
        the address back, and both the family and the host are compared
        against what was put in, so a struct with a field in the wrong place
        fails here rather than the first time a route is added.
*/

#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

//      Independent of every reversal path: index the bytes, rebuild reversed.
static p32 reference_32(p32 value)
{
        p8 b0 = value & 0xff, b1 = (value >> 8) & 0xff;
        p8 b2 = (value >> 16) & 0xff, b3 = (value >> 24) & 0xff;
        return ((p32)b0 << 24) | ((p32)b1 << 16) | ((p32)b2 << 8) | (p32)b3;
}

static p16 reference_16(p16 value)
{
        p8 b0 = value & 0xff, b1 = (value >> 8) & 0xff;
        return (p16)(((p16)b0 << 8) | (p16)b1);
}

static fn byte_order(void)
{
        static const p32 awkward[] = {
                0, 1, 0x80000000u, 0xffffffffu, 0x7fffffffu, 0xdeadbeefu,
                0x00ff00ffu, 0xff00ff00u, 0x12345678u, 0x000000ffu, 0xff000000u,
        };

        for (p32 value = 0; value <= 0xffffu; value++)
                check("bytes_reverse_16", bytes_reverse_16((p16)value) ==
                                                  reference_16((p16)value));

        for (positive i = 0; i < sizeof awkward / sizeof *awkward; i++)
                check("bytes_reverse_32 awkward",
                      bytes_reverse_32(awkward[i]) == reference_32(awkward[i]));

        for (b32 bit = 0; bit < 32; bit++) {
                p32 value = 1u << bit;
                check("bytes_reverse_32 single bit",
                      bytes_reverse_32(value) == reference_32(value));
                check("bytes_reverse_32 single bit clear",
                      bytes_reverse_32(~value) == reference_32(~value));
        }

        for (p32 value = 0; value < 0x04000000u; value += 9973u)
                check("bytes_reverse_32 stride",
                      bytes_reverse_32(value) == reference_32(value));

        //      Reversing twice is the identity, which the wire relies on.
        for (p32 value = 0; value < 0x00400000u; value += 7919u)
                check("bytes_reverse_32 is its own inverse",
                      bytes_reverse_32(bytes_reverse_32(value)) == value);

        // Every offset is deliberate: packet fields routinely start unaligned.
        for (positive offset = 0; offset < 8; offset++)
        {
                p8 wire[16];

                memory_fill(wire, 0xa5, sizeof wire);
                network_store_16(wire + offset, 0x81fe);
                check("network_store_16 first byte", wire[offset] == 0x81);
                check("network_store_16 second byte", wire[offset + 1] == 0xfe);
                check("network_load_16", network_load_16(wire + offset) == 0x81fe);

                memory_fill(wire, 0xa5, sizeof wire);
                network_store_32(wire + offset, 0x81fe23dcu);
                check("network_store_32 first byte", wire[offset] == 0x81);
                check("network_store_32 second byte", wire[offset + 1] == 0xfe);
                check("network_store_32 third byte", wire[offset + 2] == 0x23);
                check("network_store_32 fourth byte", wire[offset + 3] == 0xdc);
                check("network_load_32", network_load_32(wire + offset) == 0x81fe23dcu);
        }

        /* Every first byte, sign bit included, at every start residue that
           matters to the byte-wise assembly: the value is the four bytes in
           order, whichever of them has the top bit. */
        for (positive first = 0; first < 256; first++)
                for (positive offset = 0; offset < 4; offset++)
                {
                        p8 wire[12];
                        unsigned int want = ((unsigned int)first << 24) | 0x003ca96eu;

                        memory_fill(wire, 0xa5, sizeof wire);
                        wire[offset] = (p8)first;
                        wire[offset + 1] = 0x3c;
                        wire[offset + 2] = 0xa9;
                        wire[offset + 3] = 0x6e;
                        check("network_load_32 every first byte",
                              network_load_32(wire + offset) == want);
                }
}

/*
        The dotted quad, both ways.

        host_into does not terminate what it writes -- it returns a length, the
        way positive_into does -- so the round trip has to put the zero on
        itself before handing the buffer back. That is the interesting case
        anyway: every value written must read back as itself, which catches a
        formatter that drops a leading digit and a parser that accepts one.
*/
static const struct { const char *text; b64 expect; } address_cases[] = {
        {"0.0.0.0",            0x00000000},
        {"1.2.3.4",            0x01020304},
        {"127.0.0.1",          0x7f000001},
        {"192.168.1.10",       0xc0a8010a},
        {"255.255.255.255",    0xffffffff},
        {"10.0.0.255",         0x0a0000ff},
        {"9.9.9.9",            0x09090909},
        {"099.9.9.9",          -1},  // a leading zero, which iproute2 would read as octal
        {"010.1.1.1",          -1},
        {"01.2.3.4",           -1},
        {"1.2.3.04",           -1},
        {"00.0.0.0",           -1},
        {"0.0.0.0",            0x00000000},  // a single zero is still a zero
        {"",                   -1},
        {"1",                  -1},
        {"1.2",                -1},
        {"1.2.3",              -1},
        {"1.2.3.4.5",          -1},
        {"1.2.3.",             -1},
        {".1.2.3",             -1},
        {"1..2.3",             -1},
        {"256.1.1.1",          -1},
        {"1.256.1.1",          -1},
        {"1.1.1.256",          -1},
        {"1234.1.1.1",         -1},  // a fourth digit
        {"1.2.3.4 ",           -1},
        {" 1.2.3.4",           -1},
        {"1.2.3.4x",           -1},
        {"-1.2.3.4",           -1},
        {"1.2.3.-4",           -1},
        {"...",                -1},
        {"999.999.999.999",    -1},
};

static fn addresses(void)
{
        p8 scratch[32];

        for (positive i = 0; i < sizeof address_cases / sizeof *address_cases; i++)
                check("string_to_host",
                      string_to_host((string_address)address_cases[i].text) ==
                              address_cases[i].expect);

        //      What it writes, exactly, for the shapes that differ in width.
        check("host_into 0.0.0.0", host_into(scratch, 0x00000000) == 7);
        check("host_into 0.0.0.0 bytes", memory_compare(scratch, "0.0.0.0", 7) == 0);
        check("host_into 127.0.0.1", host_into(scratch, 0x7f000001) == 9);
        check("host_into 127.0.0.1 bytes", memory_compare(scratch, "127.0.0.1", 9) == 0);
        check("host_into 255.255.255.255", host_into(scratch, 0xffffffffu) == 15);
        check("host_into 255.255.255.255 bytes",
              memory_compare(scratch, "255.255.255.255", 15) == 0);
        check("host_into 8.8.8.8", host_into(scratch, 0x08080808) == 7);
        check("host_into 10.1.100.9", host_into(scratch, 0x0a016409) == 10);
        check("host_into 10.1.100.9 bytes",
              memory_compare(scratch, "10.1.100.9", 10) == 0);

        //      Every value written must read back as itself.
        for (p32 value = 0; value < 0x04000000u; value += 7919u) {
                positive width = host_into(scratch, value);
                scratch[width] = 0;
                check("host_into then string_to_host", string_to_host(scratch) == (b64)value);
                check("host_into width is sane", width >= 7 && width <= 15);
        }

        //      The boundaries of each octet's digit count, walked exactly.
        for (p32 octet = 0; octet < 256; octet++) {
                p32 value = (octet << 24) | (octet << 16) | (octet << 8) | octet;
                positive width = host_into(scratch, value);
                scratch[width] = 0;
                check("repeated octet round trip", string_to_host(scratch) == (b64)value);
        }
}

#ifdef LINUX
static fn sockets(void)
{
        socket_address_internet where, target, peer;
        p8 note[] = "hardware floor";
        p8 back[32];
        p32 size = sizeof where;
        b32 one = 1;

        bipolar server = socket_new(AF_INET, SOCK_STREAM, 0);
        check("socket_new stream", server >= 0);
        if (server < 0)
                return;

        check("socket_option_set", socket_option_set((b32)server, SOL_SOCKET,
                                                     SO_REUSEADDR, &one,
                                                     sizeof one) == 0);

        //      Both pointer arguments reach the kernel after the option name.
        //      This is also the call the original test never exercised at all.
        b32 option = 0;
        p32 option_size = sizeof option;
        check("socket_option_get", socket_option_get((b32)server, SOL_SOCKET,
                                                     SO_REUSEADDR, &option,
                                                     &option_size) == 0);
        check("socket_option_get size", option_size == sizeof option);
        check("socket_option_get value", option == one);

        memory_fill(&where, 0, sizeof where);
        where.family = AF_INET;
        where.port = network_order_16(0);
        where.host = network_order_32(HOST_LOOPBACK);
        check("socket_bind", socket_bind((b32)server, &where, sizeof where) == 0);
        check("socket_listen", socket_listen((b32)server, 4) == 0);

        //      The kernel writing the address back is what pins the struct.
        memory_fill(&where, 0, sizeof where);
        check("socket_name", socket_name((b32)server, &where, &size) == 0);
        check("socket_name filled the whole address", size == sizeof where);
        check("family came back", where.family == AF_INET);
        check("host came back in wire order",
              where.host == network_order_32(HOST_LOOPBACK));
        check("a port was chosen", network_order_16(where.port) != 0);

        bipolar client = socket_new(AF_INET, SOCK_STREAM, 0);
        check("socket_new client", client >= 0);

        memory_fill(&target, 0, sizeof target);
        target.family = AF_INET;
        target.port = where.port;
        target.host = network_order_32(HOST_LOOPBACK);
        check("socket_connect",
              socket_connect((b32)client, &target, sizeof target) == 0);

        p32 peer_size = sizeof peer;
        memory_fill(&peer, 0, sizeof peer);
        bipolar accepted = socket_accept((b32)server, &peer, &peer_size,
                                         SOCK_CLOEXEC);
        check("socket_accept", accepted >= 0);
        check("socket_accept address size", accepted >= 0 &&
                                            peer_size == sizeof peer);
        check("socket_accept address family", accepted >= 0 &&
                                              peer.family == AF_INET);

        //      F_GETFD is one and FD_CLOEXEC is bit zero on Linux. A successful
        //      check proves accept4 saw its nonzero fourth argument, including
        //      x86_64's rcx-to-r10 move.
        check("socket_accept flags", accepted >= 0 &&
              (system_call_3(syscall(fcntl), (positive)accepted, 1, 0) & 1));

        check("socket_send",
              socket_send((b32)client, note, sizeof note - 1, 0, 0, 0) ==
                    sizeof note - 1);
        memory_fill(back, 0, sizeof back);
        check("socket_receive",
              socket_receive((b32)accepted, back, sizeof back, 0, 0, 0) ==
                    sizeof note - 1);
        check("the bytes arrived unchanged",
              memory_compare(back, note, sizeof note - 1) == 0);

        check("socket_shutdown",
              socket_shutdown((b32)client, SHUT_BOTH) == 0);
        check("socket_close", socket_close((b32)client) == 0);
        socket_close((b32)accepted);
        socket_close((b32)server);

        /*
                A datagram is deliberately not connected. sendto therefore
                needs arguments five and six, and recvfrom writes through
                arguments five and six; MSG_DONTWAIT makes argument four
                nonzero on both. The stream round trip above could succeed if
                every one of those arguments were dropped.
        */
        socket_address_internet datagram, from;
        p8 packet[] = "all six arguments";
        p32 datagram_size = sizeof datagram;
        p32 from_size = sizeof from;
        bipolar receiver = socket_new(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        bipolar sender = socket_new(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

        check("socket_new datagram receiver", receiver >= 0);
        check("socket_new datagram sender", sender >= 0);

        memory_fill(&datagram, 0, sizeof datagram);
        datagram.family = AF_INET;
        datagram.host = network_order_32(HOST_LOOPBACK);
        check("socket_bind datagram", receiver >= 0 &&
              socket_bind((b32)receiver, &datagram, sizeof datagram) == 0);

        memory_fill(&datagram, 0, sizeof datagram);
        check("socket_name datagram", receiver >= 0 &&
              socket_name((b32)receiver, &datagram, &datagram_size) == 0);
        check("socket_name datagram size", datagram_size == sizeof datagram);
        check("socket_name datagram port", network_order_16(datagram.port) != 0);

        check("socket_send all arguments", sender >= 0 &&
              socket_send((b32)sender, packet, sizeof packet - 1, MSG_DONTWAIT,
                          &datagram, sizeof datagram) == sizeof packet - 1);

        memory_fill(back, 0, sizeof back);
        memory_fill(&from, 0, sizeof from);
        check("socket_receive all arguments", receiver >= 0 &&
              socket_receive((b32)receiver, back, sizeof back, MSG_DONTWAIT,
                             &from, &from_size) == sizeof packet - 1);
        check("datagram bytes arrived unchanged",
              memory_compare(back, packet, sizeof packet - 1) == 0);
        check("socket_receive address size", from_size == sizeof from);
        check("socket_receive address family", from.family == AF_INET);

        if (sender >= 0)
                socket_close((b32)sender);
        if (receiver >= 0)
                socket_close((b32)receiver);

        //      What address configuration will actually be talking to.
        socket_address_netlink self;
        bipolar route = socket_new(AF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE);
        check("socket_new netlink", route >= 0);
        memory_fill(&self, 0, sizeof self);
        self.family = AF_NETLINK;
        check("socket_bind netlink",
              socket_bind((b32)route, &self, sizeof self) == 0);
        socket_close((b32)route);

        //      A failure has to arrive as a negative errno, not as a trap.
        check("a bad handle is -EBADF", socket_listen(-1, 1) == -9);
}
#endif

/*
        The client of the compositor's window table (src/canvas/window.c), on
        the three machines' real syscalls: a row's events read out of its
        columns in order, a picture counted and knocked for, whether the
        compositor has taken it, the doors' names, and the wake -- a pipe
        here -- drained by windows_wait and read as the compositor gone once
        its writing end has closed. read, write and ppoll are different
        numbers on x86-64 and on arm64 and riscv64, and a wrong one would be a
        different call, not a failure to compile.
*/
#ifdef LINUX
static fn windows_client(void)
{
        static struct windows *table_scratch;
        struct windows *const table_at = windows_scratch_table(&table_scratch);
        struct window_row own = {table_at, 3, -1, -1, 0};
        struct window_event event;
        b32 wake[2] = {-1, -1}, door[2] = {-1, -1};
        char name[48], byte;
        unsigned int read_back = 0, in_order = 1;

        windows_door_name(name, 3, 0);
        check("row 3's door", string_compare((string_address)name, (string_address) "/run/moonwater/window.3") == 0);
        windows_door_name(name, 17, 1);
        check("row 17's wake",
              string_compare((string_address)name, (string_address) "/run/moonwater/window.17.wake") == 0);
        windows_door_name(name, 100, 0);
        check("row 100's door, its every digit", string_compare((string_address)name, (string_address) "/run/moonwater/window.100") == 0);
        windows_door_name(name, 2550, 1);
        check("row 2550's wake (once '/0', a path through no directory)",
              string_compare((string_address)name, (string_address) "/run/moonwater/window.2550.wake") == 0);
        windows_door_name(name, WINDOWS - 1, 1);
        check("the layout's last row's wake", string_compare((string_address)name, (string_address) "/run/moonwater/window.1048575.wake") == 0);

        //      The compositor runs as root and frees a row when its door's last
        //      writer closes, so a door the whole machine may write is one it
        //      may hold open against the window it names: the mode the
        //      compositor makes a door and its wake is root's, with no group or
        //      other access. This is the constant compositor_table uses.
        check("a window door is root-only",
              WINDOWS_DOOR_MODE == 0600 && (WINDOWS_DOOR_MODE & 0077) == 0);

        check("an empty ring has no event", !windows_event(&own, &event));
        for (unsigned int at = 0; at < WINDOWS_EVENTS; at++)
        {
                table_at->event_type[3][at] = WINDOW_EVENT_KEY;
                table_at->event_flags[3][at] = WINDOW_KEY_DOWN;
                table_at->event_a[3][at] = (int)at;
                table_at->event_b[3][at] = -(int)at;
        }
        table_at->event_tail[3] = 0xfffffff0u;
        table_at->event_head[3] = 0xfffffff0u + WINDOWS_EVENTS;
        while (windows_event(&own, &event))
        {
                unsigned int at = (0xfffffff0u + read_back) % WINDOWS_EVENTS;

                in_order &= event.type == WINDOW_EVENT_KEY && event.flags == WINDOW_KEY_DOWN &&
                            event.a == (int)at && event.b == -(int)at;
                read_back++;
        }
        check("a full ring is read whole, in order, across the counter's wrap",
              read_back == WINDOWS_EVENTS && in_order && table_at->event_tail[3] == table_at->event_head[3]);

        check("a pipe for the wake", system_call_2(syscall(pipe2), (positive)wake, O_NONBLOCK) == 0);
        check("a pipe for the door", system_call_2(syscall(pipe2), (positive)door, O_NONBLOCK) == 0);
        own.wake = wake[0];
        own.door = door[1];

        table_at->frame[3] = table_at->shown[3] = 7;
        check("a picture taken leaves the other buffer free", windows_ready(&own));
        windows_show(&own, 1);
        check("a picture is counted and named", table_at->frame[3] == 8 && table_at->buffer_index[3] == 1);
        check("and knocked for", system_read_once(door[0], &byte, 1) == 1);
        check("not free until the compositor has taken it", !windows_ready(&own));
        table_at->shown[3] = 8;
        check("free once it has", windows_ready(&own));

        check("no knock: the wait times out", windows_wait(&own, -1, 0) == 0);
        byte = 0;
        system_write_once(wake[1], &byte, 1);
        system_write_once(wake[1], &byte, 1);
        check("a knock wakes it", windows_wait(&own, -1, 1000000) == 1);
        check("and is drained", system_read_once(wake[0], &byte, 1) == -11);
        system_close(wake[1]);
        check("the compositor gone is -1", windows_wait(&own, -1, 1000000) == -1);

        system_close(wake[0]);
        system_close(door[0]);
        system_close(door[1]);
}
#endif

/*
        The window lists: a table with a list for every row after its pictures, each row's list at its own place inside the mapping, a
        table whose lists do not fit or do not follow the pictures refused by windows_take, a reset row with no list state, and a
        list's header read as window_list_valid reads it (ui/ui.c's format): valid when it is one the compositor may render, and
        refused for each word that makes it another list (a publish in progress, a count past the room, a size that is not the row's,
        damage past the picture, a lost frame, a half that is not one, a cover offset that is not the layout's, a pitch narrower than
        the picture, a magic or version that is not the kit's, and no list at all).
*/
static unsigned int wl_list[WINDOW_LIST_BYTES / 4];

static void wl_header(unsigned int width, unsigned int height)
{
        unsigned int address_to head = wl_list;

        memory_zero(wl_list, sizeof(wl_list));
        head[UI_DRAW_LIST_MAGIC_AT] = UI_DRAW_LIST_MAGIC, head[UI_DRAW_LIST_VERSION_AT] = UI_DRAW_LIST_VERSION;
        head[UI_DRAW_LIST_GEN] = 2, head[UI_DRAW_LIST_HALF] = 0, head[UI_DRAW_LIST_COUNT] = 0, head[UI_DRAW_LIST_ROOM] = WINDOW_LIST_ROOM;
        head[UI_DRAW_LIST_WIDTH] = width, head[UI_DRAW_LIST_HEIGHT] = height, head[UI_DRAW_LIST_PITCH] = width;
        head[UI_DRAW_LIST_COVER] = (unsigned int)(UI_DRAW_LIST_HEADER + WINDOW_LIST_ROOM * UI_DRAW_LIST_OP), head[UI_DRAW_LIST_COVER_USED] = 0;
        head[UI_DRAW_LIST_FLAGS] = 0;
}

static fn windows_lists(void)
{
        static struct windows *table_scratch;
        struct windows *const table_at = windows_scratch_table(&table_scratch);
        // A table with room for 40 rows, of the layout's WINDOWS: its lists follow the pictures of the rows it has room for.
        const unsigned int room = 40;
        unsigned long buffer = 64 * 32 * 4, size = WINDOWS_HEADER + 2 * room * buffer + room * WINDOW_LIST_BYTES, at;
        unsigned char address_to list = (unsigned char address_to)wl_list;
        unsigned int address_to head = wl_list;
        unsigned int lists_at;
        int every = 1;

        table_at->capacity = room;
        table_at->lists = WINDOWS_LISTS_AT(buffer, room), table_at->list_bytes = WINDOW_LIST_BYTES;
        check("the lists come after the pictures and end at the mapping", table_at->lists + room * WINDOW_LIST_BYTES == size);
        for (int row = 0; row < (int)room; row++)
        {
                unsigned char address_to got = window_list_of(table_at, row);

                at = (unsigned long)((char address_to)got - (char address_to)table_at);
                every = every && got && at == table_at->lists + (unsigned long)row * WINDOW_LIST_BYTES && at + WINDOW_LIST_BYTES <= size;
        }
        check("every row's list is its own, inside the mapping, with none shared", every);
        table_at->lists = 0;
        check("a table with no lists has none", !window_list_of(table_at, 0));
        table_at->lists = WINDOWS_LISTS_AT(buffer, room);
        table_at->list_bytes = WINDOW_LIST_BYTES / 2;
        check("a list of another size is none", !window_list_of(table_at, 3));
        table_at->list_bytes = WINDOW_LIST_BYTES;
        check("a row out of range is none", !window_list_of(table_at, -1) && !window_list_of(table_at, WINDOWS));
        check("a row past the table's room has no list, whatever the layout's rows", !window_list_of(table_at, (int)room));
        table_at->capacity = room + 1;
        check("lists laid for another room do not fit", !windows_lists_fit(table_at, size, buffer));
        table_at->capacity = room;

        check("the lists fit the mapping", windows_lists_fit(table_at, size, buffer));
        check("a mapping a page short of the lists does not", !windows_lists_fit(table_at, size - 4096, buffer));
        lists_at = table_at->lists;
        table_at->lists += 4096;
        check("lists that do not follow the pictures do not", !windows_lists_fit(table_at, size, buffer));
        table_at->lists = lists_at;
        table_at->lists = 0;
        check("a table with no lists is one windows_take takes", windows_lists_fit(table_at, size, buffer));
        table_at->lists = lists_at;

        // The reset zeroes the row's list header, which this table (the struct alone, no pictures or lists behind it) does not have.
        table_at->lists = 0;
        table_at->list_state[5] = 2, table_at->list_seen[5] = 6;
        windows_reset_row(table_at, 5);
        table_at->lists = lists_at;
        check("a reset row has no list state and no generation seen", !table_at->list_state[5] && !table_at->list_seen[5]);

        // A header the kit wrote: valid for the picture it is for, and refused for each word that makes it another list.
        wl_header(240, 160);
        check("a published list is valid at its picture's size", window_list_valid(list, 240, 160));
        check("a list of another size is not, whatever the picture", !window_list_valid(list, 240, 159) && !window_list_valid(list, 239, 160));
        check("no list at all is not", !window_list_valid(null, 240, 160) && !window_list_valid(list, 0, 160));
        head[UI_DRAW_LIST_GEN] = 3;
        check("a publish in progress (an odd generation) is not", !window_list_valid(list, 240, 160));
        head[UI_DRAW_LIST_GEN] = 2;
        head[UI_DRAW_LIST_COUNT] = WINDOW_LIST_ROOM + 1;
        check("a count past the room is not", !window_list_valid(list, 240, 160));
        head[UI_DRAW_LIST_COUNT] = 0;
        head[UI_DRAW_LIST_DX1] = 241, head[UI_DRAW_LIST_DY1] = 10;
        check("damage past the picture is not", !window_list_valid(list, 240, 160));
        head[UI_DRAW_LIST_DX1] = 0, head[UI_DRAW_LIST_DY1] = 0;
        check("empty damage is valid", window_list_valid(list, 240, 160));
        head[UI_DRAW_LIST_FLAGS] = 1;
        check("a lost frame is not", !window_list_valid(list, 240, 160));
        head[UI_DRAW_LIST_FLAGS] = 0;
        head[UI_DRAW_LIST_HALF] = 2;
        check("a half that is not one is not", !window_list_valid(list, 240, 160));
        head[UI_DRAW_LIST_HALF] = 0;
        head[UI_DRAW_LIST_COVER] = 100;
        check("a cover offset that is not the layout's is not", !window_list_valid(list, 240, 160));
        head[UI_DRAW_LIST_COVER] = (unsigned int)(UI_DRAW_LIST_HEADER + WINDOW_LIST_ROOM * UI_DRAW_LIST_OP);
        head[UI_DRAW_LIST_PITCH] = 100;
        check("a pitch narrower than the picture is not", !window_list_valid(list, 240, 160));
        head[UI_DRAW_LIST_PITCH] = 240;
        head[UI_DRAW_LIST_MAGIC_AT] = 0;
        check("a list without the kit's magic is not", !window_list_valid(list, 240, 160));
        head[UI_DRAW_LIST_MAGIC_AT] = UI_DRAW_LIST_MAGIC;
        head[UI_DRAW_LIST_ROOM] = 64;
        head[UI_DRAW_LIST_COVER] = (unsigned int)(UI_DRAW_LIST_HEADER + 64 * UI_DRAW_LIST_OP);
        check("a room other than the list's is not", !window_list_valid(list, 240, 160));
        head[UI_DRAW_LIST_ROOM] = WINDOW_LIST_ROOM;
        head[UI_DRAW_LIST_COVER] = (unsigned int)(UI_DRAW_LIST_HEADER + WINDOW_LIST_ROOM * UI_DRAW_LIST_OP);
        check("and the list is valid again once every word is the kit's", window_list_valid(list, 240, 160));

        // Which test a list fails, as the compositor counts it (window_list_check). A torn read (a publish that begins between the open and
        // the reader's still) takes a second writer, which these rows do not have.
        {
                struct ui_draw_view view;

                check("list check: a list that may be rendered fails none", window_list_check(list, 240, 160, &view) == WINDOW_LIST_VALID);
                check("list check: no list, or no picture, is none to render",
                      window_list_check(null, 240, 160, &view) == WINDOW_LIST_NONE && window_list_check(list, 0, 160, &view) == WINDOW_LIST_NONE);
                head[UI_DRAW_LIST_GEN] = 3;
                check("list check: an odd generation is a publish writing it", window_list_check(list, 240, 160, &view) == WINDOW_LIST_PUBLISHING);
                head[UI_DRAW_LIST_GEN] = 2;
                head[UI_DRAW_LIST_MAGIC_AT] = 0;
                check("list check: a list without the kit's magic is the header's", window_list_check(list, 240, 160, &view) == WINDOW_LIST_HEADER);
                head[UI_DRAW_LIST_MAGIC_AT] = UI_DRAW_LIST_MAGIC;
                head[UI_DRAW_LIST_FLAGS] = 1;
                check("list check: a lost frame is the header's", window_list_check(list, 240, 160, &view) == WINDOW_LIST_HEADER);
                head[UI_DRAW_LIST_FLAGS] = 0;
                head[UI_DRAW_LIST_COUNT] = WINDOW_LIST_ROOM + 1;
                check("list check: a count past the room is the header's", window_list_check(list, 240, 160, &view) == WINDOW_LIST_HEADER);
                head[UI_DRAW_LIST_COUNT] = 0;
                head[UI_DRAW_LIST_ROOM] = 64;
                head[UI_DRAW_LIST_COVER] = (unsigned int)(UI_DRAW_LIST_HEADER + 64 * UI_DRAW_LIST_OP);
                check("list check: a room other than the list's is the room's", window_list_check(list, 240, 160, &view) == WINDOW_LIST_ROOM_OTHER);
                head[UI_DRAW_LIST_ROOM] = WINDOW_LIST_ROOM;
                head[UI_DRAW_LIST_COVER] = (unsigned int)(UI_DRAW_LIST_HEADER + WINDOW_LIST_ROOM * UI_DRAW_LIST_OP);
                check("list check: a list of another size is the size's, and its view has the list's own size",
                      window_list_check(list, 239, 160, &view) == WINDOW_LIST_SIZE && view.width == 240 && view.height == 160 &&
                          window_list_check(list, 240, 161, &view) == WINDOW_LIST_SIZE);
                head[UI_DRAW_LIST_PITCH] = 100;
                check("list check: a pitch narrower than the picture is the pitch's", window_list_check(list, 240, 160, &view) == WINDOW_LIST_PITCH);
                head[UI_DRAW_LIST_PITCH] = 240;
                head[UI_DRAW_LIST_DX1] = 241, head[UI_DRAW_LIST_DY1] = 10;
                check("list check: damage past the picture is the damage's", window_list_check(list, 240, 160, &view) == WINDOW_LIST_DAMAGE);
                head[UI_DRAW_LIST_DX1] = 0, head[UI_DRAW_LIST_DY1] = 0;
                check("list check: and the list fails none again", window_list_check(list, 240, 160, &view) == WINDOW_LIST_VALID);
        }
}

/*
        What windows_take checks in the header a peer may write, and what a row's reset clears. A header the
        compositor could have written is taken; one that is not (pixels off the layout, a buffer that is not whole
        pages or runs past the mapping, a stride narrower than the output, a buffer shorter than the rows used) is
        refused. A reset row has no hover, glass, lift, spoiled rows or wanted size left; its pinned is kept,
        because the card may still read the buffers it names.
*/
static fn windows_header(void)
{
        static struct windows *table_scratch;
        struct windows *const table_at = windows_scratch_table(&table_scratch);
        const unsigned int room = 48;
        unsigned long buffer = 64 * 32 * 4, size = WINDOWS_HEADER + 2 * room * buffer;

        check("a header the compositor writes fits the mapping",
              windows_fits(size, WINDOWS_HEADER, buffer, 64, 64, 32, room));
        check("pixels off the layout do not", !windows_fits(size, WINDOWS_HEADER + 4096, buffer, 64, 64, 32, room));
        check("a buffer past the mapping does not", !windows_fits(size, WINDOWS_HEADER, buffer + 4096, 64, 64, 32, room));
        check("a buffer that is not whole pages does not", !windows_fits(size, WINDOWS_HEADER, buffer + 4, 64, 64, 32, room));
        check("a stride narrower than the output does not", !windows_fits(size, WINDOWS_HEADER, buffer, 32, 64, 32, room));
        check("a buffer shorter than the rows used does not", !windows_fits(size, WINDOWS_HEADER, 4096, 64, 64, 32, room));
        check("a room of rows past the mapping does not, nor none, nor one past the layout's",
              !windows_fits(size, WINDOWS_HEADER, buffer, 64, 64, 32, room + 1) && !windows_fits(size, WINDOWS_HEADER, buffer, 64, 64, 32, 0) &&
                  !windows_fits(size, WINDOWS_HEADER, buffer, 64, 64, 32, WINDOWS + 1));
        check("a table of this layout's version is taken, and one of another is refused before any field is read (2 and 3, the 32 rows', among them)",
              windows_abi_ok(WINDOWS_ABI) && WINDOWS_ABI == 8 && !windows_abi_ok(0) && !windows_abi_ok(2) && !windows_abi_ok(3) && !windows_abi_ok(WINDOWS_ABI + 1));
        check("the layout has rows for more than a million windows, and the struct's per-row fields with them",
              WINDOWS >= (1u << 20) && sizeof(table_at->pid) / sizeof(table_at->pid[0]) == WINDOWS);
        table_at->capacity = room;
        table_at->rows = room + 5;
        check("the rows a table says are in use are held to its room (a peer's count past it reads no row that is not there)",
              windows_rows(table_at) == room && windows_room(table_at) == room);
        table_at->capacity = WINDOWS + 7;
        check("and its room to the layout's", windows_room(table_at) == WINDOWS);
        table_at->capacity = room;
        table_at->rows = 3;
        check("and are what the table says below it", windows_rows(table_at) == 3);
        table_at->rows = 0;
                check("a zero buffer, width or height does not",
              !windows_fits(size, WINDOWS_HEADER, 0, 64, 64, 32, room) && !windows_fits(size, WINDOWS_HEADER, buffer, 64, 0, 32, room) &&
              !windows_fits(size, WINDOWS_HEADER, buffer, 64, 64, 0, room));

        table_at->hover_off[3] = 1;
        table_at->glass[3] = WINDOW_GLASS;
        table_at->lift[3] = 5;
        table_at->spoiled_height[3][1] = 7;
        table_at->damage_height[3] = 4;
        table_at->want_width[3] = 9;
        table_at->pinned[3] = 1;
        windows_reset_row(table_at, 3);
        check("a reset row has no hover, glass, lift, spoiled rows or wanted size",
              !table_at->hover_off[3] && !table_at->glass[3] && !table_at->lift[3] && !table_at->spoiled_height[3][1] &&
              !table_at->damage_height[3] && !table_at->want_width[3]);
        check("and keeps pinned, which the card may still read", table_at->pinned[3] == 1);

        /*
                The table, its memfd and the doors are made owner-only (0600):
                the mapping is every window's picture and input ring, so a
                program that is not root must not open it and inject into the
                root shell's window. The compositor's fchmod/mknod use this
                one constant; a flip back to a group- or world-readable mode
                fails here, and the guest canvas lane proves an unprivileged
                opener is refused at the file and at WINDOWS_IOCTL_TABLE_GET.
        */
        check("the window table and its doors are private to the owner", WINDOWS_MODE == 0600);
        check("no group or other bits reach the table or its doors", (WINDOWS_MODE & 0077) == 0);

        table_at->place[3] = WINDOW_PLACE_AT | WINDOW_PLACE_SIZE;
        table_at->place_x[3] = 5;
        table_at->place_screen[3] = 2;
        table_at->name[3][0] = 't';
        table_at->name[3][14] = 'x';
        table_at->moved[3] = 4;
        windows_reset_row(table_at, 3);
        check("a reset row has no placement, no name to its last byte and no move left",
              !table_at->place[3] && !table_at->place_x[3] && !table_at->place_screen[3] && !table_at->name[3][0] && !table_at->name[3][14] &&
                  !table_at->moved[3]);

        // A menu had the row: the window that takes it next is no popup of the menu's window, at the menu's anchor.
        table_at->popup_parent[3] = 2;
        table_at->popup_x[3] = 273, table_at->popup_y[3] = 248, table_at->popup_w[3] = 9, table_at->popup_h[3] = 9;
        table_at->popup_flags[3] = WINDOW_POPUP_POINT;
        table_at->clip_watch[3] = 1;
        windows_reset_row(table_at, 3);
        check("a reset row is no popup: no parent, anchor or flags left from the menu that had it, and no clipboard watch",
              !table_at->popup_parent[3] && !table_at->popup_x[3] && !table_at->popup_y[3] && !table_at->popup_w[3] && !table_at->popup_h[3] &&
                  !table_at->popup_flags[3] && !table_at->clip_watch[3]);
}

/*
        A placement: the words `moonwater window` takes and MOONWATER_PLACE holds, read strictly, said back the way
        they are read, and taken into a row once, the environment's copy emptied so what the program starts next
        is not placed too. Every word, every bound of every number and every word the reader must refuse.
*/
static b32 place_words(const char *text, struct window_place address_to place)
{
        return window_place_from_text(place, text);
}

static fn windows_placement(void)
{
        static const char *const good[] = {
            "center", "maximized", "fullscreen", "at 0,0", "at -16384,16384", "at 16384,-16384", "screen 1", "screen 8",
            "size 1x1", "size 16384x16384", "name terminal", "name a", "name 0-a", "name abcdefghijklmno",
            "center screen 2 size 800x600 name notes", "at -10,20 screen 3", "size 640x480", "maximized screen 2 name klog",
            "name x center", "screen 4 at 7,9"};
        static const char *const bad[] = {
            "", " ", "center ", " center", "center  screen 1", "centre", "Center", "center center", "center maximized",
            "at 1,2 center", "maximized fullscreen", "at", "at 1", "at 1,", "at ,1", "at 1;2", "at 1,2,3", "at +1,2",
            "at 01,2", "at -0,2", "at 16385,0", "at 0,-16385", "at 999999,0", "at 1x2", "screen", "screen 0", "screen 9",
            "screen -1", "screen 1x", "screen 01", "size", "size 0x5", "size 5x0", "size 16385x1", "size 5", "size 5x",
            "size x5", "size -5x5", "size 5X5", "name", "name -a", "name A", "name a_b", "name abcdefghijklmnop",
            "name a.b", "name ../x", "maximized size 800x600", "fullscreen size 1x1", "screen 1 screen 2", "size 1x1 size 2x2",
            "name a name b", "at 1,2 at 3,4", "center\tscreen 1", "a b c d e f g h i j k l m n o"};
        struct window_place place;
        char said[WINDOW_PLACE_TEXT];
        unsigned int wrong = 0, round = 0;

        for (positive at = 0; at < array_count(good); at++)
        {
                struct window_place again;

                if (!place_words(good[at], &place))
                {
                        wrong++;
                        continue;
                }
                window_place_say(&place, said, sizeof(said));
                round += !place_words(said, &again) || memory_compare(&place, &again, sizeof(place)) != 0;
        }
        check("every placement word and every bound of its numbers is read", !wrong);
        check("and said back as words that read as the same placement", !round);
        wrong = 0;
        for (positive at = 0; at < array_count(bad); at++)
                wrong += place_words(bad[at], &place);
        check("a placement with a word that is wrong, twice, empty, out of its range or in a second mode is none", !wrong);

        check("center screen 2 size 800x600 name notes is the middle of the second screen, at that size, remembered as notes",
              place_words("center screen 2 size 800x600 name notes", &place) &&
                  place.how == (WINDOW_PLACE_CENTER | WINDOW_PLACE_SCREEN | WINDOW_PLACE_SIZE) && place.screen == 1 &&
                  place.width == 800 && place.height == 600 && string_equals((string_address)place.name, "notes"));
        check("at -10,20 is a top left past the screen's left, which the compositor clamps and the reader does not",
              place_words("at -10,20", &place) && place.how == WINDOW_PLACE_AT && place.x == -10 && place.y == 20);
        window_place_say(&place, said, sizeof(said));
        check("a placement is said in the words it is read from", string_equals((string_address)said, "at -10,20"));
        place_words("maximized screen 2 name klog", &place);
        window_place_say(&place, said, 10);
        check("and cut to the room it is given, ended all the same", string_length((string_address)said) == 9);

        {
                const char *argv[] = {"at", "5,6", "/surface", "-x", "1"};
                const char *refused[] = {"at", "5,x", "/surface"};
                const char *only[] = {"/surface", "center"};

                check("the words of the verb end at the first that is no placement word: the command",
                      window_place_parse(&place, argv, 5) == 2 && place.x == 5 && place.y == 6);
                check("a wrong value is a refusal, not the command", window_place_parse(&place, refused, 3) == -1);
                check("and a command first is no placement at all", window_place_parse(&place, only, 2) == 0 && !place.how);
        }

        check("a name is lower-case letters, digits and hyphens, a letter or a digit first, under sixteen",
              window_name_valid("terminal") && window_name_valid("9-x") && !window_name_valid("") &&
                  !window_name_valid("-x") && !window_name_valid("Settings") && !window_name_valid("a b") &&
                  !window_name_valid("abcdefghijklmnop") && window_name_valid("abcdefghijklmno"));

        /*
                The environment the program was started with: the entry is read into the row the program takes, and
                emptied where it is, the vector's length and its null left as they were (the auxiliary vector is
                found past that null). A second window of the program, and whatever it starts, finds nothing.
        */
        {
                static struct windows *table_scratch;
                struct windows *const table_at = windows_scratch_table(&table_scratch);
                string_address address_to environment = program_environment_list();
                static char first[] = "MOONWATER_PLACE=at 40,50 size 300x200 name probe";
                static char second[] = "MOONWATER_PLACE=center";
                string_address kept[3];
                positive count = 0;

                while (environment && environment[count])
                        count++;
                check("the environment has room for the two entries the next checks put in it", environment && count >= 2);
                if (environment && count >= 2)
                {
                        kept[0] = environment[0];
                        kept[1] = environment[1];
                        environment[0] = (string_address)first;
                        environment[1] = (string_address)second;
                        windows_place_take(table_at, 6);
                        check("MOONWATER_PLACE is the row's placement and name",
                              table_at->place[6] == (WINDOW_PLACE_AT | WINDOW_PLACE_SIZE) && table_at->place_x[6] == 40 &&
                                  table_at->place_y[6] == 50 && table_at->place_width[6] == 300 && table_at->place_height[6] == 200 &&
                                  string_equals((string_address)table_at->name[6], "probe"));
                        check("and every copy of it is emptied in place, the vector as long as it was",
                              string_equals(environment[0], "MOONWATER_PLACE=") &&
                                  string_equals(environment[1], "MOONWATER_PLACE=") && environment[count] == null &&
                                  environment[count - 1] != null);
                        windows_reset_row(table_at, 7);
                        windows_place_take(table_at, 7);
                        check("so a second window of the program is where it asks, unplaced", !table_at->place[7] && !table_at->name[7][0]);
                        environment[0] = kept[0];
                        environment[1] = kept[1];
                }

                {
                        struct window_row own = {table_at, 6, -1, -1, 0, 0, 0, 0, 0};

                        windows_name(&own, "terminal");
                        check("a placement's name stands over the name the program gives", string_equals((string_address)table_at->name[6], "probe"));
                        windows_reset_row(table_at, 6);
                        windows_name(&own, "terminal");
                        check("which is the name with none from the placement", string_equals((string_address)table_at->name[6], "terminal"));
                        windows_reset_row(table_at, 6);
                        windows_name(&own, "Not A Name");
                        check("and a name that is none is no name", !table_at->name[6][0]);
                }
        }
}

/*
        Where windows were last put, as the compositor keeps it and `canvas windows` reads it: a line a window,
        every field checked, the newest first and a name once, said back as it is read, and the oldest left off
        when all PLACES_MOST lines are used.
*/
static fn windows_places(void)
{
        static const char text[] =
            "terminal Virtual-1 0 0 100 80 800 600\n"
            "settings HDMI-A-1 1920 0 -30 40 640 480\n"
            "terminal Virtual-1 0 0 5 5 50 50\n"
            "Bad Virtual-1 0 0 1 1 1 1\n"
            "klog Virtual-1 0 0 1 1 0 1\n"
            "klog Virtual-1 0 0 16385 1 1 1\n"
            "klog Virtual-1 0 0 1 1 1\n"
            "klog Virtual-1 0 0 1 1 1 1 1\n"
            "klog  Virtual-1 0 0 1 1 1 1\n"
            " klog Virtual-1 0 0 1 1 1 1\n"
            "klog Virtual-1 0 0 1 1 1 1 \n"
            "klog Virt_1 0 0 1 1 1 1\n"
            "klog Virtual-1 0 0 01 1 1 1\n"
            "klog Virtual-1 0 0 1x 1 1 1\n"
            "\n"
            "klog Virtual-1 0 0 1 1 1 1 xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n"
            "notes eDP-1 -1280 100 0 0 300 200\n"
            "cut Virtual-1 0 0 1 1 1 1";
        static const char said_wanted[] = "terminal Virtual-1 0 0 100 80 800 600\n"
                                          "settings HDMI-A-1 1920 0 -30 40 640 480\n"
                                          "notes eDP-1 -1280 100 0 0 300 200\n";
        static struct places places, again;
        static char said[PLACES_BYTES];
        struct place_kept kept;
        unsigned long length;
        unsigned int full = 1;

        places_parse(&places, text, sizeof(text) - 1);
        check("places: a line is a name, a screen and six numbers in their bounds, and nothing else is one",
              places.count == 3 && string_equals((string_address)places.kept[0].name, "terminal") &&
                  places.kept[0].x == 100 && places.kept[0].height == 600 &&
                  string_equals((string_address)places.kept[1].screen, "HDMI-A-1") && places.kept[1].origin_x == 1920 &&
                  places.kept[1].x == -30 && string_equals((string_address)places.kept[2].name, "notes") &&
                  places.kept[2].origin_x == -1280);
        check("places: a second line of a name is left out, the first kept",
              places_find(&places, "terminal") == 0 && places.kept[0].width == 800 && places_find(&places, "klog") < 0);
        check("places: and so is a last line with no line end, which was cut short", places_find(&places, "cut") < 0);
        length = places_say(&places, said, sizeof(said));
        said[length] = 0;
        check("places: they are said back in the words they are read from, the newest first",
              string_equals((string_address)said, said_wanted));
        places_parse(&again, said, length);
        check("places: and read again they are the same", again.count == 3 && !memory_compare(&again, &places, sizeof(places)));

        memory_zero(&kept, sizeof(kept));
        memory_copy(kept.name, "klog", 5);
        memory_copy(kept.screen, "Virtual-1", 10);
        kept.x = 7, kept.y = 9, kept.width = 300, kept.height = 200;
        places_note(&places, &kept);
        check("places: a place noted is the newest line", places.count == 4 && places_find(&places, "klog") == 0 &&
                                                              places_find(&places, "terminal") == 1);
        kept.x = 70;
        memory_copy(kept.name, "settings", 9);
        places_note(&places, &kept);
        check("places: one noted again for a name is moved to the front, its old line gone",
              places.count == 4 && places_find(&places, "settings") == 0 && places.kept[0].x == 70 &&
                  places_find(&places, "klog") == 1 && places_find(&places, "notes") == 3);
        for (unsigned int at = 0; at < PLACES_MOST; at++)
        {
                char name[8] = {'w', (char)('a' + at / 10), (char)('0' + at % 10), 0};

                memory_zero(kept.name, sizeof(kept.name));
                memory_copy(kept.name, name, 4);
                places_note(&places, &kept);
                full &= places.count <= PLACES_MOST;
        }
        check("places: all of them used, the oldest is left off and the newest kept",
              full && places.count == PLACES_MOST && places_find(&places, "notes") < 0 && places_find(&places, "settings") < 0 &&
                  places_find(&places, "wa0") == PLACES_MOST - 1 && places_find(&places, "wd1") == 0);
        length = places_say(&places, said, sizeof(said));
        check("places: and the whole of them fit the room the compositor reads", length < PLACES_BYTES);
        check("places: a screen is named by letters, digits and hyphens, one to fifteen",
              place_screen_name_valid("HDMI-A-1") && place_screen_name_valid("eDP-1") && !place_screen_name_valid("") &&
                  !place_screen_name_valid("a b") && !place_screen_name_valid("Virt_1") &&
                  !place_screen_name_valid("ABCDEFGHIJKLMNOP"));
}

b32 main(void)
{
        byte_order();
        addresses();
#ifdef LINUX
        sockets();
        windows_client();
#endif
        windows_header();
        windows_lists();
        windows_placement();
        windows_places();
        return test_report(null);
}
#endif /* CHECK_socket */

