/*
        src/sh/host/disk.c: the disk: the state directory and boot's verdict, the lock, the census of disks, images
        and their builds, the place steps an image is written by (host_place_*), the splash an install lays, a disk
        taken, boot's settle and its question, and setup install's work (host_install_locked); the setup verbs are
        page.c's.

        A run of src/sh/host.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. host_main, at the end
        of src/sh/host.c, names the function each verb runs.
*/

/*
        Boot publishes its verdict by a rename into the state directory, so a
        wait for the verdict is a wait for that rename. inotify on the directory
        says when it happens: the watch is made before the verdict is read, so a
        publish that lands between the read and the wait is already queued and
        the wait returns at once. Each wait ends on the first thing the
        directory hears of (the verdict's own rename, or a temporary file on
        the way to it), or at the boot time it is given. Where no watch can be
        made, the wait is the poll it was.
*/
static fn host_state_ready(void);

#define HOST_VERDICT_EVENTS (0x040 | 0x080 | 0x100 | 0x200)

/* An inotify watch of path for events, or -1 where none can be made. The
   watch is nonblocking and closed on exec, and is read with host_watch_wait. */
static bipolar host_watch_open(string_address path, unsigned int events)
{
        bipolar notify = system_call_1(syscall(inotify_init1), O_CLOEXEC | O_NONBLOCK);

        if (notify < 0)
                return -1;
        if (system_call_3(syscall(inotify_add_watch), (positive)notify,
                          (positive)path, events) < 0)
        {
                system_close((positive)notify);
                return -1;
        }
        return notify;
}

static bipolar host_verdict_watch(void)
{
        host_state_ready();
        return host_watch_open(HOST_STATE, HOST_VERDICT_EVENTS);
}

static fn host_verdict_unwatch(bipolar notify)
{
        if (notify >= 0)
                system_close((positive)notify);
}

/* Until something is queued on the watch, or limit runs out (null: for ever).
   What is queued is read and dropped, so the next wait starts on the next
   event. */
static fn host_watch_wait(bipolar notify, timespec address_to limit)
{
        if (descriptor_wait_readable(notify, limit, null) > 0)
        {
                p8 drained[256];

                while (system_read_once((positive)notify, drained, sizeof(drained)) > 0)
                        ;
        }
}

/* Until the directory is heard of, or the boot clock reads until (0: for
   ever); fallback is the poll used where there is no watch. */
static fn host_verdict_pause(bipolar notify, p64 until, p64 fallback)
{
        p64 now = system_clock_ns(HOST_CLOCK_BOOTTIME);
        timespec span;
        timespec address_to limit = null;

        if (notify < 0)
        {
                host_pause(until && until > now && until - now < fallback
                               ? until - now : fallback);
                return;
        }
        if (until)
        {
                if (until <= now)
                        return;
                span = (timespec){(until - now) / 1000000000,
                                  (until - now) % 1000000000};
                limit = address_of span;
        }
        host_watch_wait(notify, limit);
}

#include "../host_words.h"

/* A small kernel file (sysfs, proc), without its trailing newline: the kernel's,
   not a name anybody plants, and a test's sandbox stands a FIFO at one to hold a
   sleep until its reader is ready, so it is read as it always was. Everything
   this keeps for itself is read by host_read_word. */
static bipolar host_read_kernel(string_address path, p8 address_to into,
                                positive room)
{
        bipolar got = file_slurp_once_at(AT_FDCWD, path, into, room);

        if (got < 0)
        {
                into[0] = end;
                return got;
        }

        return host_word_end(into, got);
}

/*
        A state file this writes as root, under /run/moonwater or /root, is
        opened where it is and never through a link: a name planted there
        first made the write truncate whatever it pointed at, with the
        secret some of these files carry (the wifi passwords, the settings)
        going into it. And its mode is set as well as asked for, since a
        mode given to open reaches only a file it creates: /root/wifi left
        at 0644 by anything before kept the passwords readable by all.
        opening is FILE_WRITE, or FILE_APPEND for a log only added to.
*/
static bipolar host_open_state(bipolar directory, string_address path,
                               positive mode, positive opening)
{
        //      Nonblocking, and a file or nothing: a FIFO at the name held the
        //      writer until a reader came, and a device took the mode.
        bipolar handle = system_open_at_mode(directory, path,
                                             opening | O_CLOEXEC | O_NOFOLLOW |
                                                 O_NONBLOCK,
                                             mode);
        system_path_identity facts;
        bipolar moded;

        if (handle < 0)
                return handle;

        moded = system_path_identity_at(handle, (string_address) "",
                                        SYSTEM_PATH_AT_EMPTY_PATH,
                                        SYSTEM_PATH_STATX_TYPE, address_of facts);
        if (moded >= 0)
                moded = (facts.mode & 0170000) == 0100000
                            ? system_call_2(syscall(fchmod), (positive)handle, mode)
                            : -22;
        if (moded < 0)
        {
                system_close(handle);
                return moded;
        }

        return handle;
}

/* A stage published over a state name. The name is looked at again when the
   stage is renamed onto it: one that was not there is published only if it
   still is not (EEXIST: a creator that came between is not written over unseen),
   and one that was there only if it is still the file the stage was made
   against (EAGAIN: nor a replacement). For a word that is kept the later writer
   is the one that stands, so the one that lost the race writes again, over what
   is there now. Two commands at once that set one word -- name, dns, keyboard,
   ntp server, canvas scale, mouse, link on, link port -- left the later of them
   with "Resource temporarily unavailable", or "File exists" when the word was
   not there yet, and its word unkept, one run in three. A name taken away
   between the look and the rename (dns auto beside dns ADDRESS) is the same
   race, once or twice; a directory that is not there answers the same and is
   not asked sixteen times. Only the writers of a whole word come through here:
   a file that is read, changed and written back is under its lock, and the
   machine's key is made by a publish that refuses to replace. */
static bipolar host_publish_again(string_address path, const address_any bytes, positive length,
                                  positive mode, bool sync, positive behavior)
{
        bipolar result = file_publish_with(path, bytes, length, mode, sync, behavior);

        for (positive attempt = 0; attempt < 16 &&
                                   (result == -EEXIST || result == -EAGAIN ||
                                    (result == -ENOENT && attempt < 2));
             attempt++)
                result = file_publish_with(path, bytes, length, mode, sync, behavior);
        return result;
}

/* Bytes over a state file, made if it is not there. A choice that has to
   survive the power going is synced: written beside itself under a name of
   its own, and renamed over the old one, so a crash or a signal part way (the
   saved wifi list, passwords and all, was truncated first and written second)
   leaves the file as it was or as it is to be, and nothing half of each, and
   two writers at once leave one whole. A /run file that only says what this
   session is doing does not need to be synced, but it is still renamed over
   its name: a file another process reads (the wifi SAE list, the scan and
   avoid lists) was truncated in place before, so a reader could see it empty
   and a signal between the truncate and the write left it empty. */
static bipolar host_write_file(string_address path, const p8 address_to bytes,
                               positive length, positive mode, bool sync)
{
        return host_publish_again(path, bytes, length, mode, sync,
                                  FILE_STAGED_EXACT_MODE);
}

/* Bytes over a /run file that others read, whole or not at all and not synced:
   written beside the name and renamed over it, as the synced ones are, so that
   a reader never finds it empty. The stages a killed writer left in its
   directory are taken away first, as a synced write does, since a settings
   write killed between its two steps left a stage in /run/moonwater for each
   kill, with the commands of the list in it, until the machine stopped. */
static bipolar host_publish(string_address path, address_any bytes, positive length,
                            positive mode)
{
        file_stage_sweep(path);
        return host_publish_again(path, bytes, length, mode, false, 0);
}

static bipolar host_write_text(string_address path, string_address text)
{
        return host_write_file(path, (p8 address_to)text, string_length(text),
                               0644, false);
}

/* One line from standard input, or negative: -1 at its end, and HOST_LINE_LONG
   for one that did not fit, which is no answer to anything -- its first
   characters read as a name were once all that was looked at. */
#define HOST_LINE_LONG (-2)

static bipolar host_read_line(p8 address_to into, positive room)
{
        positive used = 0;
        bool cut = false;

        for (;;)
        {
                p8 byte;
                bipolar got = system_read_once(0, address_of byte, 1);

                if (got == -4)
                        continue;
                if (got <= 0)
                {
                        if (!used)
                                return -1;
                        break;
                }
                if (byte == '\n')
                        break;
                if (used + 1 < room)
                        into[used++] = byte;
                else
                        cut = true;
        }

        //      The blanks a line ends with go as they go off a word.
        used = (positive)host_word_end(into, (bipolar)used);
        return cut ? HOST_LINE_LONG : (bipolar)used;
}

typedef bool (*host_entry_visitor)(string_address directory,
                                   string_address name, address_any context);

static fn host_each_entry(string_address path, host_entry_visitor visit,
                          address_any context)
{
        file_walk walk;
        struct linux_dirent64 address_to entry;

        if (!file_walk_open(address_of walk, AT_FDCWD, path))
                return;

        while ((entry = file_walk_next(address_of walk)))
                if (entry->d_name[0] != '.' &&
                    !visit(path, entry->d_name, context))
                        break;

        file_walk_close(address_of walk);
}

/* A directory made, or left as it is when it is there already: the failure is
   the kernel's and a name that exists is none. With exact the mode is the one
   asked for whatever the umask. */
static bipolar host_make_directory(string_address path, positive mode, bool exact)
{
        bipolar made = exact ? system_make_directory_exact_at(AT_FDCWD, path, mode)
                             : system_make_directory_at(AT_FDCWD, path, mode);

        return made < 0 && made != -EEXIST ? made : 0;
}

/*      A directory this keeps state in, made if it is not there -- and made
        again if what is there is a link, which every write under it would
        otherwise have followed, since a mkdir that fails because the name
        exists says nothing about what the name is. Its mode is the one asked
        for and not the caller's umask taken from it: /run/moonwater made by a
        root shell with umask 077 was 0700, and the security file in it, which
        every TLS client reads, could not be opened by anyone else. */
static fn host_state_directory(string_address path, positive mode)
{
        bipolar opened;

        system_make_directory_exact_at(AT_FDCWD, path, mode);
        opened = system_open_at(AT_FDCWD, path,
                                FILE_READ | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (opened >= 0)
        {
                system_close(opened);
                return;
        }

        if (system_remove_at(AT_FDCWD, path, 0) >= 0)
                system_make_directory_exact_at(AT_FDCWD, path, mode);
}

/* The state directory is made, and looked at, once for a command: it asked four
   system calls at each of 26 places, a verb that took the settings lock and
   saved them three times over. The machine and the watchers that outlive one
   ask again whenever they take a lock, which is when a directory removed under
   them would matter, so only a command's own process holds the answer. */
static bool host_state_made;
static bool host_state_once;

static fn host_state_ready(void)
{
        if (host_state_made)
                return;
        system_make_directory_at(AT_FDCWD, "/run", 0755);
        host_state_directory(HOST_STATE, 0755);
        host_state_made = host_state_once;
}

/* The verdict a terminal waits on; one that could not be written is said, since
   nothing else will tell the terminal why it never came. */
static b32 host_verdict_set(string_address kind, string_address disk)
{
        p8 line[HOST_NAME_ROOM + 16];
        p8 text[HOST_NAME_ROOM + 16];
        bipolar failed;

        if (!host_join(line, sizeof(line), kind, disk) ||
            !host_join(text, sizeof(text), line, "\n"))
                return 0;

        failed = host_publish(HOST_VERDICT, text, string_length(text), 0644);
        return failed < 0 ? host_fail(HOST_VERDICT, failed) : 0;
}

/* An empty file a terminal looks for, made or emptied; one that could not be
   written is said, since nothing else will tell the terminal why it is not
   there. */
static fn host_mark_set(string_address path)
{
        bipolar failed = host_write_text(path, "");

        if (failed < 0)
                host_fail(path, failed);
}

/* A session that settled live: its verdict. The desktop opens the installer
   on it (moonwater install's window, ui/installer.c); a terminal says nothing. */
static fn host_settle_live(void)
{
        host_verdict_set("live", "");
}

/*
        A session that is live because the install boot chose could not be
        kept (host_take has said why): its verdict is "live DISK", which names
        the disk kept away from, so that moonwater setup says what happened to it and send nobody to an install
        over it. It was a plain live verdict with no hint, and setup then said
        moonwater setup install DISK keeps it, which erases the disk whose data
        partition would not mount.
*/
static fn host_settle_kept_away(string_address disk)
{
        host_verdict_set("live ", disk);
        host_say(log_error, host_label "this session is live, and %s is not used: its Moonwater "
                                       "data is not kept this session. moonwater setup use %s "
                                       "tries it again\n",
                 disk, disk);
}

typedef struct
{
        p8 disk[HOST_NAME_ROOM];
        bool found;
} host_data_look;

static bool host_parent(string_address name, p8 address_to into, positive room);

static bool host_data_visit(string_address directory, string_address name, address_any opaque)
{
        host_data_look address_to look = (host_data_look address_to)opaque;
        p8 path[HOST_PATH_ROOM];
        p8 text[1024];
        p8 address_to line;
        bipolar got;

        if (!host_join(path, sizeof(path), directory, "/") ||
            !host_join(path, sizeof(path), path, name) ||
            !host_join(path, sizeof(path), path, "/uevent"))
                return true;
        got = file_slurp_once_at(AT_FDCWD, path, text, sizeof(text));
        for (line = got > 0 ? text : null; line;)
        {
                p8 address_to next = (p8 address_to)string_first_of(line, '\n');

                if (next)
                        *next = end;
                if (string_equals(line, "PARTNAME=" HOST_DATA_NAME) &&
                    host_parent(name, look->disk, sizeof(look->disk)) &&
                    !string_equals(look->disk, name))
                {
                        look->found = true;
                        return false;
                }
                line = next ? next + 1 : null;
        }
        return true;
}

/*
        A disk with a Moonwater data partition on it, as sysfs names the
        partitions (no device is opened): what a live session's words say
        an install over would erase, where they suggested one to anybody.
*/
static bool host_data_disk(p8 address_to into, positive room)
{
        host_data_look look;

        memory_zero(address_of look, sizeof(look));
        host_each_entry("/sys/class/block", host_data_visit, address_of look);
        return look.found && string_length(look.disk) < room && (string_copy(into, look.disk), true);
}

#define RADIO_LOCK_EX 2
#define RADIO_LOCK_NB 4
#define RADIO_LOCK_UN 8

/*
        A lock file opened as one: nonblocking, and a regular file or nothing.
        A FIFO at a lock's name held the open of whoever asked whose lock it
        was (host_lock_owner opens it to read, which waits for a writer that
        never comes), so `moonwater link`, `link off`, `device lock` and
        `device unlock` sat for ever where one was planted (the cli and state
        fuzzes' hangs past the deadline); a device or a directory is no lock
        file either. The handle, or the error; -EINVAL for what is no file.
*/
static bipolar host_lock_handle(bipolar handle)
{
        system_path_identity facts;
        bipolar looked;

        if (handle < 0)
                return handle;
        looked = system_path_identity_at(handle, (string_address) "", SYSTEM_PATH_AT_EMPTY_PATH,
                                         SYSTEM_PATH_STATX_TYPE, address_of facts);
        if (looked >= 0 && (facts.mode & 0170000) == 0100000)
                return handle;
        system_close(handle);
        return looked < 0 ? looked : -22;
}

/* A lock file, made if it is not there. Not through a link: the lock is made
   where it is named. */
static bipolar host_lock_open(string_address path)
{
        host_state_ready();
        return host_lock_handle(system_open_at_mode(AT_FDCWD, path,
                                                    FILE_READ_WRITE | FILE_CREATE | O_NONBLOCK |
                                                        O_NOFOLLOW | O_CLOEXEC,
                                                    0600));
}

/*
        A lock file held, waited for asleep in the kernel's flock, which the
        holder's letting go wakes, and ended at a time by an alarm. The machine's
        lock was waited for on an inotify watch of its file, and a close is heard
        before the kernel drops the lock it held (__fput says so to the watch,
        then takes the lock away): a waiter woken by the close of a holder that
        went without unlocking (a command killed, or ended with the lock held)
        tried while the lock was still there, and slept until its thirty seconds
        ran out for a release it had already been told of -- 17 waits of 1,500
        on the box. The radio and clock locks were polled every two milliseconds
        for half a second. The alarm rings again every 50 ms after its time, so a
        ring that lands before the flock is entered is not the only one.

        0 once the lock is held; -EAGAIN when nanoseconds went by first (0 waits
        for ever); another error as flock gives it. A timer of this process's own
        (flock -w arms one) is left alone, and the wait is then a poll.
*/
#define HOST_TIMER_REAL 0
#define HOST_LOCK_RING_AGAIN_US 50000

static volatile bool host_lock_rang;

static fn host_lock_ring(b32 number)
{
        (void)number;
        host_lock_rang = true;
}

static bipolar host_lock_take(bipolar handle, p64 nanoseconds)
{
        signal_interval span = {0};
        signal_interval before = {0};
        positive previous[4];
        positive mask = 0;
        positive unblock = (positive)1 << (SIGALRM - 1);
        p64 started;
        bipolar locked;

        if (!nanoseconds)
        {
                do
                        locked = system_call_2(syscall(flock), (positive)handle,
                                               RADIO_LOCK_EX);
                while (locked == -EINTR);
                return locked;
        }

        if (system_call_2(syscall(getitimer), HOST_TIMER_REAL,
                          (positive)address_of before) < 0 ||
            before.first_seconds || before.first_microseconds ||
            !system_signal_install(SIGALRM, (positive)host_lock_ring,
                                   SIGNAL_CATCH_FLAGS & ~SIGNAL_RESTART,
                                   SIGNAL_CATCH_RESTORER, previous))
        {
                started = system_clock_ns(HOST_CLOCK_BOOTTIME);
                while ((locked = system_call_2(syscall(flock), (positive)handle,
                                               RADIO_LOCK_EX | RADIO_LOCK_NB)) == -EAGAIN &&
                       system_clock_ns(HOST_CLOCK_BOOTTIME) - started < nanoseconds)
                        host_pause(2000000);
                return locked;
        }

        host_lock_rang = false;
        span.first_seconds = (bipolar)(nanoseconds / 1000000000);
        span.first_microseconds = (bipolar)((nanoseconds % 1000000000 + 999) / 1000);
        span.repeat_microseconds = HOST_LOCK_RING_AGAIN_US;
        system_signal_mask(SIG_UNBLOCK, address_of unblock, address_of mask,
                           SIGNAL_KERNEL_SET_BYTES);
        system_call_3(syscall(setitimer), HOST_TIMER_REAL, (positive)address_of span, 0);
        do
                locked = system_call_2(syscall(flock), (positive)handle, RADIO_LOCK_EX);
        while (locked == -EINTR && !host_lock_rang);
        memory_zero(address_of span, sizeof(span));
        system_call_3(syscall(setitimer), HOST_TIMER_REAL, (positive)address_of span, 0);
        system_signal_mask(SIG_SETMASK, address_of mask, null, SIGNAL_KERNEL_SET_BYTES);
        system_signal_action(SIGALRM, previous, null, SIGNAL_KERNEL_SET_BYTES);
        return locked == -EINTR ? -EAGAIN : locked;
}

static bipolar host_lock(string_address path, bool wait)
{
        bipolar handle = host_lock_open(path);
        bipolar locked;

        if (handle < 0)
                return handle;

        locked = system_call_2(syscall(flock), (positive)handle,
                               RADIO_LOCK_EX | RADIO_LOCK_NB);
        //      A holder that has it for a join (the radio lock is held through
        //      every network tried, which is a minute for a few) was waited for
        //      with nothing said: a command that takes the lock says what it is
        //      waiting for once, and then waits as it did.
        //      A holder that lets go within the half second it takes to notice a wait is
        //      not worth a line: the output of a verb that met another for a moment is the
        //      output of one that did not.
        if (locked == -EAGAIN && wait)
                locked = host_lock_take(handle, 500000000);
        if (locked == -EAGAIN && wait)
        {
                host_say(log, host_label "waiting for another moonwater command to let go of %s\n",
                         path);
                log_flush();
                locked = host_lock_take(handle, 0);
        }
        if (locked < 0)
        {
                system_close(handle);
                return locked;
        }

        return handle;
}

/*
        A process that holds a file, named by the file: a write lock over all
        of one in /run/moonwater, root's own, that the kernel drops when its
        holder goes, and F_GETLK says whose it is. The holder of the keeper's
        and the listener's is a process of their own for as long as it lives,
        which is what these ask of it: who it is (0 for nobody), the lock
        itself, a signal for it that cannot land on a process that is not it,
        and a wait for it to be gone.
*/
#define HOST_FILE_GETLK 5
#define HOST_FILE_SETLK 6
#define HOST_FILE_SETLKW 7
#define HOST_FILE_WRLCK 1
#define HOST_FILE_UNLCK 2

typedef struct
{
        b16 type;
        b16 whence;
        b32 padding;
        bipolar start;
        bipolar length;
        b32 pid;
        b32 padding2;
} host_record_lock;

/* The file's handle, or the error, after command (F_GETLK, F_SETLK or
   F_SETLKW) over all of it; with owner, whose lock it is. The lock is held
   for as long as the handle is. */
static bipolar host_lock_file(string_address path, positive command,
                              b32 address_to owner)
{
        host_record_lock lock = {.type = HOST_FILE_WRLCK};
        //      Asking whose lock it is makes none: a lock file never made is
        //      nobody's, so a status or a query never creates one.
        bipolar handle = command == HOST_FILE_GETLK
                             ? host_lock_handle(system_open_at(AT_FDCWD, path,
                                                               O_RDONLY | O_NONBLOCK | O_CLOEXEC |
                                                                   O_NOFOLLOW))
                             : host_lock_open(path);
        bipolar asked;

        if (handle < 0)
                return handle;
        do
                asked = system_call_3(syscall(fcntl), (positive)handle, command,
                                      (positive)address_of lock);
        while (asked == -4);
        if (asked < 0)
        {
                system_close(handle);
                return asked;
        }
        if (owner)
                address_to owner = lock.type == HOST_FILE_UNLCK ? 0 : lock.pid;
        return handle;
}

static b32 host_lock_owner(string_address path)
{
        b32 owner = 0;
        bipolar handle = host_lock_file(path, HOST_FILE_GETLK, address_of owner);

        if (handle >= 0)
                system_close(handle);
        return owner;
}

// A signal for whoever holds the lock, and never for a pid reused since.
static bool host_lock_signal(string_address path, b32 signal)
{
        b32 owner = host_lock_owner(path);
        bipolar handle;
        bool sent;

        if (!owner)
                return false;
        handle = system_call_2(syscall(pidfd_open), (positive)owner, 0);
        if (handle < 0)
                return false;
        sent = host_lock_owner(path) == owner &&
               system_call_4(syscall(pidfd_send_signal), (positive)handle,
                             (positive)signal, 0, 0) >= 0;
        system_close(handle);
        return sent;
}

// Held until nobody holds it, and let go again at once.
static fn host_lock_wait(string_address path)
{
        bipolar handle = host_lock_file(path, HOST_FILE_SETLKW, null);

        if (handle >= 0)
                system_close(handle);
}

/*
        The child of a fork made a daemon of its own: forked once more so
        that init reaps it and not whatever started it, a session of its
        own, /dev/null for its three descriptors and nothing else open but
        the two named (descriptors_none for a missing one), which are what
        its caller made it for. True in the one that is left; false in the
        caller, which has waited for the first child, and when nothing could
        be forked.
*/
static bool host_detach(p32 keep, p32 keep_too)
{
        positive status = 0;
        bipolar quiet;
        bipolar child = system_fork();

        if (child)
        {
                if (child > 0)
                        system_call_4(syscall(wait4), (positive)child,
                                      (positive)address_of status, 0, 0);
                return false;
        }
        //      The second fork: a negative answer is a failure, not the parent's turn to go (it was taken as one).
        {
                bipolar grandchild = system_fork();

                if (grandchild != 0)
                        system_call_1(syscall(exit_group), grandchild < 0 ? 1 : 0);
        }

        system_call(syscall(setsid));
        quiet = system_open_at(AT_FDCWD, "/dev/null", FILE_READ_WRITE | O_CLOEXEC);
        for (positive at = 0; quiet >= 0 && at < 3; at++)
                if ((positive)quiet != at)
                        system_call_3(syscall(dup3), (positive)quiet, at, 0);
        (void)descriptors_close_except(3, keep, keep_too);
        return true;
}

/*
        Standard input is /dev/null. The machine process is started with init's
        descriptors, whose input is the console, and the script it runs has
        nobody at that: a `read` or a verb that asks would hold every event
        behind it until a key was struck, and a keystroke meant for the login
        prompt would be taken by whichever got it first.
*/
static fn host_input_none(void)
{
        bipolar quiet = system_open_at(AT_FDCWD, "/dev/null", FILE_READ | O_CLOEXEC);

        if (quiet > 0)
        {
                system_call_3(syscall(dup3), (positive)quiet, 0, 0);
                system_close(quiet);
        }
}

/*
        One command at a time changes what a machine keeps.

        Settings are read, changed and written back whole, so two commands at
        once -- six `moonwater init add` in a loop -- each read the same
        copy and the last write kept one entry of the six. The lock covers a
        command from its read to its save, and an install, an update or a take
        from its first look to its last write. The process that holds it takes
        it again without waiting on itself, and lets go of it before it
        forks anything that does not exec: a child would carry it on past its
        parent. A command that cannot have it in time says so and does
        nothing. A lock that cannot be made at all (no /run, not root) has
        nobody to exclude, and is no reason to stop.
*/
static bipolar host_held = -1;
static positive host_held_count;

/*
        What a command that was killed left mounted, taken down by the next
        one to have the lock. The places a command mounts a partition at to
        look at it or write it (the medium, the system partition, a stick,
        a look) are its own for as long as it holds the lock, and nothing is
        mounted at one between commands: so whatever is found there when the
        lock is taken is a leftover, of a command ended by a signal between
        its mount and its unmount. It stayed for the rest of the session, and
        a partition mounted read-only there could not be mounted to be
        written, so an update, a rollback and every save of a setting to the
        disk failed with "busy", the settings going on in the session alone.
        A mount in use is not taken down (no MNT_DETACH), and the place the
        data partition is kept at for the session is not one of these.
*/
static fn host_leftovers_sweep(void)
{
        static const string_address places[] = {HOST_MEDIUM, HOST_SYSTEM, HOST_STICK, HOST_LOOK};

        for (positive at = 0; at < array_count(places); at++)
                for (positive deep = 0; deep < 8; deep++)
                        if (system_call_2(syscall(umount2), (positive)places[at], 0) < 0)
                                break;
}

/*
        A command that cannot have the lock sleeps in the kernel's flock until
        the holder lets go, for HOST_LOCK_WAIT_NS at most (host_lock_take).
*/
static bool host_acquire(void)
{
        bipolar handle;
        bipolar locked;

        if (host_held_count)
        {
                host_held_count++;
                return true;
        }

        // A lock that cannot be opened at all has nobody to exclude, as it never had.
        handle = host_lock_open(HOST_LOCK);
        if (handle < 0)
        {
                host_held = handle;
                host_held_count = 1;
                return true;
        }

        locked = system_call_2(syscall(flock), (positive)handle,
                               RADIO_LOCK_EX | RADIO_LOCK_NB);
        if (locked == -EAGAIN)
                locked = host_lock_take(handle, HOST_LOCK_WAIT_NS);
        if (locked == -EAGAIN)
        {
                system_close((positive)handle);
                host_say(log_error, host_label "another moonwater command is changing "
                                               "this machine's settings or disks; "
                                               "try again in a moment\n");
                return false;
        }
        if (locked < 0)
        {
                // Not held and not refused: as host_lock's error, the lock is
                // taken as nobody's to exclude.
                system_close((positive)handle);
                handle = locked;
        }
        host_held = handle;
        host_held_count = 1;
        if (handle >= 0)
                host_leftovers_sweep();
        return true;
}

static fn host_release(void)
{
        if (!host_held_count || --host_held_count)
                return;

        if (host_held >= 0)
        {
                system_call_2(syscall(flock), (positive)host_held, RADIO_LOCK_UN);
                system_close(host_held);
        }
        host_held = -1;
}

static fn host_release_all(void)
{
        while (host_held_count)
                host_release();
}

// Disks ---------------------------------------------------------

/* The disk whose data this session already keeps, when it keeps one. */
static bool host_session_disk(p8 address_to into, positive room)
{
        p8 verdict[HOST_NAME_ROOM + 16];

        return host_read_word(HOST_VERDICT, verdict, sizeof(verdict)) >= 0 &&
               string_has_prefix(verdict, "disk ") && string_length(verdict + 5) < room &&
               (string_copy(into, verdict + 5), true);
}

static bool host_name_valid(string_address name)
{
        positive length = string_length(name);

        return length && length < HOST_NAME_ROOM &&
               string_span_of_set(name, "abcdefghijklmnopqrstuvwxyz"
                                        "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") == length;
}

/* The name a DISK argument gives, with a /dev/ in front of it taken off. */
static string_address host_disk_word(string_address word)
{
        return string_has_prefix(word, "/dev/") ? word + 5 : word;
}

/* Whether a block device's sysfs attribute (/ro, /removable) reads 1. */
static bool host_block_flag(string_address sysfs, string_address attribute)
{
        p8 path[HOST_PATH_ROOM];
        p8 text[8];

        return host_join(path, sizeof(path), sysfs, attribute) &&
               host_read_kernel(path, text, sizeof(text)) > 0 && string_equals(text, "1");
}

/* The whole disk a block device belongs to: itself, unless it is a partition. */
static bool host_parent(string_address name, p8 address_to into, positive room)
{
        p8 sysfs[HOST_PATH_ROOM];
        p8 partition[HOST_PATH_ROOM];
        p8 link[HOST_PATH_ROOM * 2];
        positive last;
        positive before;

        if (!host_name_valid(name) ||
            !host_join(sysfs, sizeof(sysfs), "/sys/class/block/", name) ||
            !host_join(partition, sizeof(partition), sysfs, "/partition"))
                return false;

        if (system_access_at(AT_FDCWD, partition, 0) < 0)
                return string_length(name) < room &&
                       (string_copy(into, name), true);

        if (file_link_text(sysfs, link, sizeof(link)) <= 0)
                return false;

        p8 address_to slash = memory_last_of(link, '/', string_length(link));

        if (!slash)
                return false;

        last = (positive)(slash - link);
        slash = memory_last_of(link, '/', last);
        before = slash ? (positive)(slash - link) + 1 : 0;

        if (last - before >= room || last == before)
                return false;

        memory_copy(into, link + before, last - before);
        into[last - before] = end;
        return true;
}

/* The install a census holds for disk, if it holds one. */
static host_install address_to host_census_find(host_census address_to census,
                                                string_address disk)
{
        for (positive at = 0; at < census->count; at++)
                if (string_equals(census->found[at].disk, disk))
                        return census->found + at;

        return null;
}

static bool host_census_visit(storage_identity address_to identity,
                              address_any opaque)
{
        host_census address_to census = (host_census address_to)opaque;
        bool system = string_equals(identity->partlabel, HOST_SYSTEM_NAME);
        bool data = string_equals(identity->partlabel, HOST_DATA_NAME);
        string_address name = identity->path + sizeof("/dev/") - 1;
        p8 parent[HOST_NAME_ROOM];
        host_install address_to install = null;

        /* Moonwater installs are GPT, so the partition UUID is the stable
           identity carried across every later mount. A label without one is
           not one of ours strongly enough to act on. */
        if ((!system && !data) || !identity->partuuid_length ||
            !host_parent(name, parent, sizeof(parent)))
                return true;

        install = host_census_find(census, parent);
        if (!install)
        {
                if (census->count == HOST_INSTALLS)
                {
                        census->dropped++;
                        return true;
                }

                install = census->found + census->count++;
                memory_zero(install, sizeof(address_to install));
                string_copy(install->disk, parent);
        }

        string_copy(system ? install->system : install->data, name);
        string_copy(system ? install->system_partuuid : install->data_partuuid,
                    identity->partuuid);
        return true;
}

/* Every disk with both of Moonwater's partitions on it, by their names and
   stable GPT identities. */
static fn host_census_take(host_census address_to census)
{
        positive kept = 0;

        memory_zero(census, sizeof(address_to census));
        storage_each_device(host_census_visit, census);

        for (positive at = 0; at < census->count; at++)
                if (census->found[at].system[0] && census->found[at].data[0])
                        census->found[kept++] = census->found[at];

        census->count = kept;

        for (positive at = 0; at < kept; at++)
                census->found[at].resolved = system_clock_ns(HOST_CLOCK_BOOTTIME);
}

/* The census could not keep every disk Moonwater has partitions on. */
static fn host_census_said(host_census address_to census)
{
        if (census->dropped)
                host_say(log_error, host_label "more than %p disks have Moonwater partitions; "
                                               "the rest are not looked at\n",
                         (positive)HOST_INSTALLS);
}

typedef struct
{
        string_address prefix;
        p8 name[HOST_NAME_ROOM];
        bool seen;
} host_prefix_look;

static bool host_prefix_visit(string_address directory, string_address name,
                              address_any opaque)
{
        host_prefix_look address_to look = (host_prefix_look address_to)opaque;

        if (!host_starts(name, look->prefix) || string_length(name) >= HOST_NAME_ROOM)
                return true;

        string_copy(look->name, name);
        look->seen = true;
        return false;
}

static bool host_has_entry(string_address directory, string_address prefix,
                           p8 address_to name)
{
        host_prefix_look look;

        memory_zero(address_of look, sizeof(look));
        look.prefix = prefix;
        host_each_entry(directory, host_prefix_visit, address_of look);

        if (name && look.seen)
                string_copy(name, look.name);

        return look.seen;
}

/* A controller not live yet, or live and not yet showing a namespace. */
static bool host_nvme_visit(string_address directory, string_address name,
                            address_any opaque)
{
        p8 path[HOST_PATH_ROOM];
        p8 state[32];
        p8 prefix[HOST_NAME_ROOM + 2];

        if (!host_join(path, sizeof(path), directory, "/") ||
            !host_join(path, sizeof(path), path, name) ||
            !host_join(prefix, sizeof(prefix), name, "n"))
                return true;

        if (!host_has_entry(path, prefix, null))
        {
                address_to (bool address_to)opaque = true;
                return false;
        }

        if (!host_join(path, sizeof(path), path, "/state") ||
            host_read_kernel(path, state, sizeof(state)) < 0 ||
            !string_equals(state, "live"))
        {
                address_to (bool address_to)opaque = true;
                return false;
        }

        return true;
}

/* A mass-storage interface whose SCSI host has no target yet. */
static bool host_usb_visit(string_address directory, string_address name,
                           address_any opaque)
{
        p8 path[HOST_PATH_ROOM];
        p8 host[HOST_NAME_ROOM];

        if (!string_first_of(name, ':') ||
            !host_join(path, sizeof(path), directory, "/") ||
            !host_join(path, sizeof(path), path, name))
                return true;

        if (!host_has_entry(path, "host", host) ||
            !host_join(path, sizeof(path), path, "/") ||
            !host_join(path, sizeof(path), path, host) ||
            !host_has_entry(path, "target", null))
        {
                address_to (bool address_to)opaque = true;
                return false;
        }

        return true;
}

static bool host_storage_arriving(void)
{
        bool arriving = false;

        host_each_entry("/sys/class/nvme", host_nvme_visit, address_of arriving);
        if (!arriving)
                host_each_entry("/sys/bus/usb/drivers/usb-storage", host_usb_visit,
                                address_of arriving);

        return arriving;
}

// Builds --------------------------------------------------------

/* This kernel's version string, spelled the way the boot header spells it. */
static bool host_running_build(p8 address_to into, positive room)
{
        static p8 head[] = "Linux version ";
        file_machine machine;
        p8 banner[1024];
        positive release;
        positive version;
        positive prefix;
        bipolar got;
        string_address who;
        positive who_length = 0;

        into[0] = end;
        if (!file_machine_read(address_of machine))
                return false;

        got = host_read_kernel("/proc/version", banner, sizeof(banner));
        if (got <= 0)
                return false;

        release = string_length(machine.release);
        version = string_length(machine.version);
        prefix = sizeof(head) - 1 + release + 2;

        /*  The fixed compares below are exact byte spans, not string ones,
            and the builder scan after them has only the terminator to stop
            it. One short read from procfs would put both past what was read
            and into whatever the frame held, so the record has to be at
            least as long as the part being matched. */
        if ((positive)got < prefix ||
            memory_compare(banner, head, sizeof(head) - 1) ||
            memory_compare(banner + sizeof(head) - 1, machine.release, release) ||
            memory_compare(banner + sizeof(head) - 1 + release, " (", 2))
                return false;

        who = banner + prefix;
        who_length = (positive)(string_first_of_or_end(who, ')') - who);

        if (!who[who_length] || release + who_length + version + 5 > room)
                return false;

        memory_copy(into, machine.release, release);
        memory_copy(into + release, " (", 2);
        memory_copy(into + release + 2, who, who_length);
        memory_copy(into + release + 2 + who_length, ") ", 2);
        memory_copy(into + release + 4 + who_length, machine.version, version + 1);
        return true;
}

/*
        Bytes a disk chose, before a terminal reads them.

        Two strings here are the medium's rather than this machine's: the
        version an image carries in its setup header, and the model a device
        answers an INQUIRY with under /sys. Both are written to a terminal --
        one by moonwater status beside the disk it came from, the other in
        the line asking whether to erase a disk -- and neither is filtered by
        anything between the medium and here, so somebody who plugs in a disk
        chooses what those lines do. A real banner and a real model are ASCII
        throughout, so nothing legitimate changes and an image still compares
        equal to the running build.
*/
static fn host_plain_line(p8 address_to text)
{
        for (positive at = 0; text[at]; at++)
                if (text[at] < 0x20 || text[at] > 0x7e)
                        text[at] = '?';
}

/*
        The two places an image says its build: x86's setup header, and the
        kernel's own banner in an arm64 or riscv64 image. Both are here on
        every machine, for build.sh, which reads the image it wrote whatever
        machine it is for; host_image_version is this machine's one.
*/

/* The version string in an x86 boot image's setup header, if it has one. */
static bool host_setup_version(bipolar handle, p8 address_to into, positive room)
{
        p8 header[0x210];

        if (file_transfer_exact(syscall(pread64), handle, header, sizeof(header),
                                0) == (bipolar)sizeof(header) &&
            header[0] == 'M' && header[1] == 'Z' &&
            !memory_compare(header + 0x202, "HdrS", 4) &&
            storage_le16(header + 0x206) >= 0x0200 &&
            storage_le16(header + 0x20e))
        {
                bipolar got = system_call_4(syscall(pread64), (positive)handle,
                                            (positive)into, room - 1,
                                            0x200 + (positive)storage_le16(header + 0x20e));

                if (got > 0)
                {
                        into[got] = end;
                        return into[0] && string_length(into) < (positive)got;
                }
        }

        return false;
}

#define HOST_BANNER_HEAD "Linux version "
#define HOST_BANNER_ROOM 512
#define HOST_BANNER_CHUNK ((positive)1 << 20)

/*
        One banner, "Linux version R (who) (compiler) #N ...\n" as /proc/version
        prints it, spelled "R (who) #N ..." the way x86's setup header spells
        the same build. The compiler is in brackets that nest -- "(gcc (GCC)
        16.1.0, GNU ld (GNU Binutils) 2.47)" -- and a build number follows it.
        A copy with no number is the one the kernel links before it is
        numbered, and is not this build's.
*/
static bool host_banner_parse(p8 address_to at, positive length,
                              p8 address_to into, positive room)
{
        positive head = sizeof(HOST_BANNER_HEAD) - 1;
        positive release = head;
        positive who;
        positive compiler;
        positive version;
        positive depth = 1;
        positive stop;
        positive used;

        while (release < length && at[release] > ' ' && at[release] < 0x7f &&
               at[release] != '(')
                release++;
        if (release == head || release + 2 >= length ||
            memory_compare(at + release, " (", 2))
                return false;

        who = release + 2;
        while (who < length && at[who] >= ' ' && at[who] < 0x7f &&
               at[who] != ')' && at[who] != '(')
                who++;
        if (who + 3 >= length || memory_compare(at + who, ") (", 3))
                return false;

        compiler = who + 3;
        while (compiler < length && depth && at[compiler] >= ' ' &&
               at[compiler] < 0x7f)
        {
                depth += at[compiler] == '(';
                depth -= at[compiler] == ')';
                compiler++;
        }
        if (depth || compiler + 3 >= length || at[compiler] != ' ' ||
            at[compiler + 1] != '#' || at[compiler + 2] < '0' ||
            at[compiler + 2] > '9')
                return false;

        version = compiler + 1;
        stop = version;
        if (stop < length)
                stop += string_span_max(at + stop, length - stop,
                                        string_set_printable);
        if (stop >= length || at[stop] != '\n')
                return false;

        used = (release - head) + 2 + (who - release - 2) + 2 + (stop - version);
        if (used + 1 > room)
                return false;

        //      "R (who) " then the version, the build number onward.
        memory_copy(into, at + head, who + 1 - head);
        into[who + 1 - head] = ' ';
        memory_copy(into + who + 2 - head, at + version, stop - version);
        into[used] = end;
        return true;
}

/*
        The kernel's own banner, from an image that is the kernel itself.

        An arm64 or riscv64 image is the kernel uncompressed behind its PE
        header, and has no setup header to carry a version; its banner is in
        its read-only data where it is linked, some megabytes in. The file is
        read a megabyte at a time until one parses, the tail of each chunk
        carried into the next so a banner across the seam is still whole.
*/
static bool host_banner_version(bipolar handle, p8 address_to into, positive room)
{
        static p8 chunk[HOST_BANNER_CHUNK + HOST_BANNER_ROOM];
        positive head = sizeof(HOST_BANNER_HEAD) - 1;
        positive kept = 0;
        p64 offset = 0;

        for (;;)
        {
                bipolar got = system_call_4(syscall(pread64), (positive)handle,
                                            (positive)(chunk + kept),
                                            HOST_BANNER_CHUNK, (positive)offset);
                positive have;
                positive at = 0;

                if (got <= 0)
                        return false;
                offset += (p64)got;
                have = kept + (positive)got;

                while (at + head <= have)
                {
                        p8 address_to found = (p8 address_to)memory_search(
                            chunk + at, have - at, HOST_BANNER_HEAD, head);
                        positive left;

                        if (!found)
                                break;
                        at = (positive)(found - chunk);
                        left = have - at;
                        //      Too near the end to judge: the next chunk
                        //      starts with it.
                        if (left < HOST_BANNER_ROOM &&
                            (positive)got == HOST_BANNER_CHUNK)
                                break;
                        if (host_banner_parse(found, left < HOST_BANNER_ROOM
                                                         ? left
                                                         : HOST_BANNER_ROOM,
                                              into, room))
                                return true;
                        at++;
                }

                if ((positive)got < HOST_BANNER_CHUNK)
                        return false;
                kept = have < HOST_BANNER_ROOM ? have : HOST_BANNER_ROOM;
                memory_copy(chunk, chunk + have - kept, kept);
        }
}

// The build this machine's image says, where its images say it.
static bool host_image_version(bipolar handle, p8 address_to into, positive room)
{
#if X64
        return host_setup_version(handle, into, room);
#else
        return host_banner_version(handle, into, room);
#endif
}

/* The build an image carries, as host_running_build spells this one. */
static bool host_image_build(string_address path, p8 address_to into,
                             positive room)
{
        bipolar handle = system_open_at(AT_FDCWD, path, FILE_READ | O_CLOEXEC);
        bool found = false;

        into[0] = end;
        if (handle < 0)
                return false;

        found = host_image_version(handle, into, room);
        system_close(handle);
        if (found)
                host_plain_line(into);
        else
                into[0] = end;

        return found;
}

/*
        Which of two builds is the later one, from what each says of itself.

        A build's words end with the moment its kernel was linked, as date(1)
        spells it -- "#4 SMP PREEMPT Tue Sep 22 07:21:00 CEST 2026" -- and
        start that part with the count of links (#4) the tree had made. The
        moment is the builder's clock in the builder's zone: the zones of the
        table are taken to UTC, and any other zone is read as UTC, which
        orders two builds of one zone rightly whatever it is. Two builds of
        the same second are ordered by their count. 1 when a is the later, -1
        when b is, 0 when they are the same build, and HOST_BUILD_UNORDERED
        when they differ and their words do not say which came first.
*/
#define HOST_BUILD_UNORDERED 2
#define HOST_BUILD_WORDS 32

static const struct
{
        string_address name;
        b32 minutes;
} host_build_zones[] = {
    {"UTC", 0},     {"GMT", 0},     {"WET", 0},     {"WEST", 60},   {"BST", 60},
    {"CET", 60},    {"CEST", 120},  {"EET", 120},   {"EEST", 180},  {"MSK", 180},
    {"EST", -300},  {"EDT", -240},  {"CST", -360},  {"CDT", -300},  {"MST", -420},
    {"MDT", -360},  {"PST", -480},  {"PDT", -420},
};

typedef struct
{
        b64 seconds; // when it was linked, in seconds from 1970 in UTC
        positive count;
        bool dated;
        bool counted;
} host_build_moment;

static fn host_build_when(string_address build, host_build_moment address_to when)
{
        static const p8 months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
        p8 copy[HOST_BUILD_ROOM];
        string_address words[HOST_BUILD_WORDS];
        positive count = 0;
        positive length = string_length(build);
        positive day, hour, minute, second, year, month = 12;
        b64 offset = 0;
        string_address at;

        memory_zero(when, sizeof(address_to when));
        if (length >= sizeof(copy))
                return;
        memory_copy(copy, build, length + 1);

        for (positive i = 0; copy[i] && count < HOST_BUILD_WORDS;)
        {
                while (copy[i] == ' ')
                        copy[i++] = end;
                if (!copy[i])
                        break;
                words[count++] = (string_address)copy + i;
                while (copy[i] && copy[i] != ' ')
                        i++;
        }

        for (positive i = 0; i < count; i++)
        {
                at = words[i] + 1;
                if (words[i][0] == '#' &&
                    string_digits_checked_exact(at, 10, address_of when->count))
                {
                        when->counted = true;
                        break;
                }
        }

        //      Wday Mon DD HH:MM:SS ZONE YYYY, the last six words.
        if (count < 6 || string_length(words[count - 5]) != 3)
                return;
        for (positive i = 0; i < 12; i++)
                if (!memory_compare(months + 3 * i, words[count - 5], 3))
                        month = i;
        at = words[count - 3];
        if (month == 12 || !string_digits_checked_exact(words[count - 4], 10, address_of day) ||
            !string_digits_checked_exact(words[count - 1], 10, address_of year) ||
            !string_digits_checked(address_of at, 10, address_of hour) || *at++ != ':' ||
            !string_digits_checked(address_of at, 10, address_of minute) || *at++ != ':' ||
            !string_digits_checked_exact(at, 10, address_of second) || !day || day > 31 ||
            hour > 23 || minute > 59 || second > 60 || year < 1970 || year > 9999)
                return;

        for (positive i = 0; i < array_count(host_build_zones); i++)
                if (string_equals(words[count - 2], host_build_zones[i].name))
                        offset = (b64)host_build_zones[i].minutes * 60;

        when->seconds = (b64)clock_days_from_civil((bipolar)year, (bipolar)month + 1,
                                                   (bipolar)day) * 86400 +
                        (b64)(hour * 3600 + minute * 60 + second) - offset;
        when->dated = true;
}

static b32 host_build_order(string_address a, string_address b)
{
        host_build_moment first;
        host_build_moment second;

        if (string_equals(a, b))
                return 0;

        host_build_when(a, address_of first);
        host_build_when(b, address_of second);
        if (!first.dated || !second.dated)
                return HOST_BUILD_UNORDERED;
        if (first.seconds != second.seconds)
                return first.seconds > second.seconds ? 1 : -1;
        if (first.counted && second.counted && first.count != second.count)
                return first.count > second.count ? 1 : -1;
        return HOST_BUILD_UNORDERED;
}

static bipolar host_mount(string_address name, string_address target,
                          string_address type, positive flags)
{
        p8 device[HOST_NAME_ROOM + 8];
        bipolar made;

        if (!host_join(device, sizeof(device), "/dev/", name))
                return -ERROR_INVALID;

        host_state_ready();
        made = host_make_directory(target, 0700, false);
        if (made < 0)
                return made;

        /*      A FAT written to goes on when it finds a chain that is wrong, and
                does not turn read-only. What a write stages beside the image and
                power cut off part way is wrong in just that way (the directory
                entry reached the disk and the table did not), and removing it is
                the next write's first step, which an ro partition refuses. */
        return system_mount(device, target, type, flags,
                            !(flags & MS_RDONLY) && string_equals(type, "vfat")
                                ? (string_address) "errors=continue"
                                : !(flags & MS_RDONLY) && string_equals(type, "ext4")
                                    ? (string_address) "commit=30"
                                    : null);
}

/* Resolve the identity again immediately before a later use. Device names are
   presentation, not authority: a terminal can sit at the install/update prompt
   long enough for hotplug to reuse sda or an nvme namespace number. */
static bool host_partition_uuid_resolve(string_address uuid,
                                        p8 address_to name)
{
        p8 query[sizeof("PARTUUID=") - 1 + STORAGE_PARTUUID_ROOM];
        p8 path[HOST_PATH_ROOM];

        if (!uuid[0] ||
            !host_join(query, sizeof(query), "PARTUUID=", uuid) ||
            !storage_resolve_tag(query, path, sizeof(path)) ||
            !string_has_prefix(path, "/dev/") ||
            !host_name_valid(path + sizeof("/dev/") - 1))
                return false;

        string_copy(name, path + sizeof("/dev/") - 1);
        return true;
}

/* Both partitions must still exist and still belong to one physical disk.
   Refresh all display names together so the rest of an operation cannot mix
   one old name with one newly resolved identity. Names resolved a moment ago
   are not resolved again: each look is a scan of every disk there is, and boot
   made seven of them in a row, where the point of looking again is the
   minutes a prompt can wait. */
static bool host_install_refresh(host_install address_to install)
{
        p8 system[HOST_NAME_ROOM];
        p8 data[HOST_NAME_ROOM];
        p8 system_disk[HOST_NAME_ROOM];
        p8 data_disk[HOST_NAME_ROOM];

        if (install->resolved &&
            system_clock_ns(HOST_CLOCK_BOOTTIME) - install->resolved < HOST_FRESH_NS)
                return true;

        if (!host_partition_uuid_resolve(install->system_partuuid, system) ||
            !host_partition_uuid_resolve(install->data_partuuid, data) ||
            !host_parent(system, system_disk, sizeof(system_disk)) ||
            !host_parent(data, data_disk, sizeof(data_disk)) ||
            !string_equals(system_disk, data_disk))
                return false;

        /*      The disk the census found, and no other: a second disk that
                carries the same partition identities (copied from this one,
                which anybody who has read its table can do) answers the tag
                first some boots, and would be mounted in its place. */
        if (install->disk[0] && !string_equals(install->disk, system_disk))
                return false;

        string_copy(install->disk, system_disk);
        string_copy(install->system, system);
        string_copy(install->data, data);
        install->resolved = system_clock_ns(HOST_CLOCK_BOOTTIME);
        return true;
}

static bool host_unmount(string_address target)
{
        return system_call_2(syscall(umount2), (positive)target, 0) >= 0;
}

/*
        Whether the file system mounted at target is the partition whose
        identity is uuid, on disk: the device the mount holds, by its numbers,
        and what sysfs says of those numbers now, the disk its directory is in
        and the PARTUUID in its uevent.

        A name resolved before a question is only a name: a stick or disk
        swapped while the answer waits is given the same sdb1, and that was
        the partition mounted and written. Nor does a descriptor opened on the
        node pin it, since mount(2) looks a block device up by its numbers,
        which the next disk can be given. What a mount holds is the device it
        opened, so it is that device, once mounted, that is asked about; one
        taken away after this leaves the mount on it, and its writes fail.
*/
static bool host_mounted_is(string_address target, string_address uuid, string_address disk)
{
        file_facts facts;
        p8 sysfs[64];
        p8 path[80];
        p8 link[HOST_PATH_ROOM * 2];
        p8 text[2048];
        p8 want[sizeof("PARTUUID=") + STORAGE_PARTUUID_ROOM];
        p8 address_to slash;
        p8 address_to line;
        positive used = sizeof("/sys/dev/block/") - 1;
        bipolar got;

        if (!uuid[0] || !file_look(AT_FDCWD, target, 0, address_of facts))
                return false;

        memory_copy(sysfs, "/sys/dev/block/", used);
        used += positive_into_string(sysfs + used, facts.device_major);
        sysfs[used++] = ':';
        used += positive_into_string(sysfs + used, facts.device_minor);
        sysfs[used] = end;

        //      .../block/DISK/PARTITION: the disk is the one asked about.
        if (file_link_text(sysfs, link, sizeof(link)) <= 0 ||
            !(slash = memory_last_of(link, '/', string_length(link))))
                return false;
        *slash = end;
        slash = memory_last_of(link, '/', string_length(link));
        if (!string_equals(slash ? slash + 1 : link, disk))
                return false;

        //      And its uevent names the identity asked about, on a line of its own.
        if (!host_join(path, sizeof(path), sysfs, "/uevent") ||
            !host_join(want, sizeof(want), "PARTUUID=", uuid))
                return false;
        got = file_slurp_once_at(AT_FDCWD, path, text, sizeof(text));
        for (line = got > 0 ? text : null; line;)
        {
                p8 address_to next = (p8 address_to)string_first_of(line, '\n');

                if (next)
                        *next = end;
                if (string_equals(line, want))
                        return true;
                line = next ? next + 1 : null;
        }
        return false;
}

/*
        An install's system partition mounted at HOST_SYSTEM to be written,
        refused unless it is the partition of the disk asked about
        (host_mounted_is). With again, after a question: the name is resolved
        afresh from the identity the question was about, and a disk that is
        not there by it, or is there under another disk's name, is not
        written. setup update usb mounted the name it resolved before asking,
        so a stick or disk swapped while the answer waited was written.
*/
static b32 host_system_mount(host_install address_to install, bool again)
{
        bipolar mounted;

        if (again)
                install->resolved = 0;
        if (!host_install_refresh(install))
                return host_refuse("%s is no longer here as the disk it was asked about, so "
                                   "nothing was written\n", install->disk);

        mounted = host_mount(install->system, HOST_SYSTEM, "vfat", HOST_WRITABLE);
        return_if(mounted < 0, host_fail(install->system, mounted));
        if (host_mounted_is(HOST_SYSTEM, install->system_partuuid, install->disk))
                return 0;

        host_unmount(HOST_SYSTEM);
        return host_refuse("%s is not the partition of %s it was asked about, so nothing was "
                           "written\n", install->system, install->disk);
}

typedef bool (*host_image_visitor)(string_address path, address_any opaque);

/*
        The image a FAT partition keeps, looked at where it lies: the
        partition is mounted read-only at where, look is given the image's
        path, and the mount is let go of. False when the partition would not
        mount; what look finds it hands back through opaque. The search that
        keeps the one it found mounted, and the check that unmounts the
        partition to read it from the disk, are not this.
*/
static bool host_with_image(string_address name, string_address where,
                            host_image_visitor look, address_any opaque)
{
        p8 path[HOST_PATH_ROOM];

        if (host_mount(name, where, "vfat", HOST_READ_ONLY) < 0)
                return false;

        if (host_join(path, sizeof(path), where, HOST_IMAGE))
                look(path, opaque);

        host_unmount(where);
        return true;
}

static bool host_install_build_visit(string_address path, address_any opaque)
{
        host_install address_to install = (host_install address_to)opaque;

        install->readable = host_image_build(path, install->build,
                                             sizeof(install->build));
        return install->readable;
}

static fn host_install_read(host_install address_to install)
{
        install->readable = false;
        install->build[0] = end;

        if (host_install_refresh(install))
                host_with_image(install->system, HOST_LOOK, host_install_build_visit,
                                install);
}

typedef struct
{
        string_address build;
        string_address skip;
        p8 name[HOST_NAME_ROOM];
        p8 disk[HOST_NAME_ROOM];
        bool found;
} host_medium_search;

/* A FAT partition whose image is this build. The one found stays mounted. */
static bool host_medium_visit(storage_identity address_to identity,
                              address_any opaque)
{
        host_medium_search address_to search = (host_medium_search address_to)opaque;
        string_address name = identity->path + sizeof("/dev/") - 1;
        p8 parent[HOST_NAME_ROOM];
        p8 path[HOST_PATH_ROOM];
        p8 build[HOST_BUILD_ROOM];

        if (!string_equals(identity->type, "vfat") ||
            !host_parent(name, parent, sizeof(parent)) ||
            (search->skip && string_equals(parent, search->skip)))
                return true;

        if (host_mount(name, HOST_MEDIUM, "vfat", HOST_READ_ONLY) < 0)
                return true;

        if (host_join(path, sizeof(path), HOST_MEDIUM, HOST_IMAGE) &&
            host_image_build(path, build, sizeof(build)) &&
            string_equals(build, search->build))
        {
                string_copy(search->name, name);
                string_copy(search->disk, parent);
                search->found = true;
                return false;
        }

        host_unmount(HOST_MEDIUM);
        return true;
}

/*
        A stick can still be on its way when this is asked. Boot stops looking
        the moment it finds an install, so on a machine with one the shell --
        and the question -- can be ready before usb-storage has scanned the
        stick the answer needs. A search that finds nothing keeps looking
        while the machine is young or storage is still arriving.
*/
static bool host_medium_find(host_medium_search address_to search,
                             string_address build, string_address skip)
{
        for (;;)
        {
                p64 uptime;

                memory_zero(search, sizeof(address_to search));
                search->build = build;
                search->skip = skip;
                storage_each_device(host_medium_visit, search);

                uptime = system_clock_ns(HOST_CLOCK_BOOTTIME);
                if (search->found || uptime >= HOST_SETTLE_MOST_NS ||
                    (uptime >= HOST_MEDIUM_FLOOR_NS && !host_storage_arriving()))
                        return search->found;

                host_pause(HOST_POLL_NS);
        }
}

/*
        Whether the image on the medium found is all there, said when it is
        not. Asked before a disk is erased or an image on it is kept as the
        previous one: a copy that is cut short reads back as itself.
*/
static bool host_medium_whole(void)
{
        p8 source[HOST_PATH_ROOM];

        if (host_join(source, sizeof(source), HOST_MEDIUM, HOST_IMAGE) &&
            host_image_whole(source))
                return true;

        host_say(log_error, host_label "the image to write is cut short: it ends before "
                                       "its own sections do, so nothing was written\n");
        return false;
}

// Placing an image ----------------------------------------------

static b32 host_copy_file(string_address from, string_address to)
{
        bipolar source = system_open_at(AT_FDCWD, from, FILE_READ | O_CLOEXEC);
        bipolar target;
        bool copied;
        bipolar synced = 0;

        return_if(source < 0, host_fail(from, source));

        target = system_open_at_mode(AT_FDCWD, to, FILE_WRITE | O_NOFOLLOW | O_CLOEXEC, 0644);
        if (target < 0)
        {
                system_close(source);
                return host_fail(to, target);
        }

        copied = file_copy_handles(source, target, null);
        if (copied)
                synced = system_call_1(syscall(fsync), (positive)target);

        system_close(source);
        system_close(target);
        if (!copied)
                return host_refuse("%s could not be copied whole\n", from);

        return synced < 0 ? host_fail(to, synced) : 0;
}

/*
        Two files alike byte for byte but for a window of the first, which is
        where an image is stamped with the settings it is to keep.
*/
static bool host_files_alike(string_address left, string_address right, p64 hole,
                             p64 hole_end)
{
        p8 one[16384];
        p8 two[16384];
        file_facts first_facts;
        file_facts second_facts;
        bipolar first = system_open_at(AT_FDCWD, left, FILE_READ | O_CLOEXEC);
        bipolar second = system_open_at(AT_FDCWD, right, FILE_READ | O_CLOEXEC);
        bool alike = first >= 0 && second >= 0 &&
                     file_look_code(first, (string_address) "", AT_EMPTY_PATH, address_of first_facts) >= 0 &&
                     file_look_code(second, (string_address) "", AT_EMPTY_PATH, address_of second_facts) >= 0 &&
                     first_facts.size == second_facts.size;
        p64 offset = 0;

        //      A read that comes up short before the size is an unreadable
        //      file, which is not one that matches.
        while (alike && offset < first_facts.size)
        {
                positive want = first_facts.size - offset < sizeof(one)
                                    ? (positive)(first_facts.size - offset)
                                    : sizeof(one);

                if (storage_read(first, one, want, offset) != want ||
                    storage_read(second, two, want, offset) != want)
                        alike = false;

                for (positive at = 0; alike && at < want; at++)
                        if ((offset + at < hole || offset + at >= hole_end) &&
                            one[at] != two[at])
                                alike = false;

                offset += want;
        }

        if (first >= 0)
                system_close(first);
        if (second >= 0)
                system_close(second);

        return alike;
}

/* Whether both of an image's settings slots are carry. */
static bool host_image_slots_are(string_address path, host_settings address_to carry)
{
        host_settings slots[2];
        p64 offset = 0;
        bipolar handle = system_open_at(AT_FDCWD, path, FILE_READ | O_CLOEXEC);
        bool alike;

        if (handle < 0)
                return false;

        alike = host_settings_slots(handle, slots, address_of offset) &&
                !memory_compare(slots, carry, SPARK_SETTINGS_SLOT) &&
                !memory_compare(slots + 1, carry, SPARK_SETTINGS_SLOT);
        system_close(handle);
        return alike;
}

/* Whether path is the image at source with the slots it is to be stamped with. */
static bool host_image_is(string_address path, string_address source,
                          host_settings address_to carry)
{
        bipolar handle = system_open_at(AT_FDCWD, source, FILE_READ | O_CLOEXEC);
        p64 offset = 0;
        bool alike;

        if (handle >= 0)
        {
                offset = carry ? host_settings_section(handle) : 0;
                system_close(handle);
        }

        alike = host_files_alike(source, path, offset,
                                 offset ? offset + 2 * SPARK_SETTINGS_SLOT : 0);
        if (!alike || !offset)
                return alike;

        return host_image_slots_are(path, carry);
}

/*
        The SHA-256 of a file, and with masked the bytes of its settings
        slots read as nought, so that an image sums the same whatever
        settings are stamped in it. False when it cannot be read whole.
*/
static bool host_image_sum(string_address path, bool masked, p8 address_to sum)
{
        static p8 block[16384];
        digest_state digest;
        file_facts facts;
        bipolar handle = system_open_at(AT_FDCWD, path, FILE_READ | O_CLOEXEC);
        p64 hole;
        p64 offset = 0;
        bool read;

        if (handle < 0)
                return false;

        hole = masked ? host_settings_section(handle) : 0;
        read = file_look_code(AT_FDCWD, path, 0, address_of facts) >= 0;
        digest_open(address_of digest, DIGEST_SHA256, 32);
        while (read && offset < facts.size)
        {
                positive want = facts.size - offset < sizeof(block)
                                    ? (positive)(facts.size - offset)
                                    : sizeof(block);
                p64 low = hole > offset ? hole : offset;
                p64 high = hole + 2 * SPARK_SETTINGS_SLOT < offset + want
                               ? hole + 2 * SPARK_SETTINGS_SLOT
                               : offset + want;

                if (storage_read(handle, block, want, offset) != want)
                {
                        read = false;
                        break;
                }
                if (hole && low < high)
                        memory_zero(block + (low - offset), (positive)(high - low));
                digest_write(address_of digest, block, want);
                offset += want;
        }
        digest_close(address_of digest, sum);
        system_close(handle);
        return read;
}

/* What a place that did not finish leaves is not left: the copies it was
   making beside the images. */
static fn host_image_discard(string_address system)
{
        p8 path[HOST_PATH_ROOM];

        if (host_join(path, sizeof(path), system, HOST_IMAGE_NEXT))
                system_remove_at(AT_FDCWD, path, 0);
        if (host_join(path, sizeof(path), system, HOST_KEPT_IMAGE_NEXT))
                system_remove_at(AT_FDCWD, path, 0);
        if (host_join(path, sizeof(path), system, SPLASH_FILE_NEXT))
                system_remove_at(AT_FDCWD, path, 0);
}

/*
        The images the partition keeps, as the disk has them: the partition
        is let go of, its buffers dropped, and it is mounted again with the
        pages of both images dropped, so what look is given (the mount, at
        system) is what a firmware would read and not memory. The partition
        is left unmounted.
*/
static bool host_on_disk(string_address name, string_address system,
                         host_image_visitor look, address_any opaque)
{
        static const string_address kept[] = {HOST_IMAGE, HOST_KEPT_IMAGE};
        p8 device[HOST_NAME_ROOM + 8];
        p8 path[HOST_PATH_ROOM];
        bipolar handle;
        bool alike;

        if (!host_unmount(system) || !host_join(device, sizeof(device), "/dev/", name))
                return false;

        handle = system_open_at(AT_FDCWD, device, FILE_READ | O_CLOEXEC);
        if (handle >= 0)
        {
                system_control(handle, HOST_BLKFLSBUF, 0);
                system_close(handle);
        }

        if (host_mount(name, system, "vfat", HOST_READ_ONLY) < 0)
                return false;

        for (positive at = 0; at < array_count(kept); at++)
        {
                if (!host_join(path, sizeof(path), system, kept[at]))
                        continue;
                handle = system_open_at(AT_FDCWD, path, FILE_READ | O_CLOEXEC);
                if (handle >= 0)
                {
                        system_call_4(syscall(fadvise64), (positive)handle, 0, 0,
                                      HOST_FADVISE_DONTNEED);
                        system_close(handle);
                }
        }

        alike = look(system, opaque);
        host_unmount(system);
        return alike;
}

/*
        An image put where firmware starts it, on a system partition that
        is mounted: what is put there and the build it is to read back as,
        the settings stamped into it, and the names on the partition.

        The loader is only ever replaced by a rename, from a file beside it
        that has been written, synced and read back, and the build it
        replaces is kept the same way, as previous.efi: so a machine that
        loses power at any step finds a loader that is whole, the one it had
        or the new one, and previous.efi the build it kept or the one the
        loader was. What a cut left beside them is removed before the next
        write and never opened, since a file a cut write left on FAT can
        share its clusters with another (test/run's power cuts hold this on
        a disk killed at every few milliseconds of the write).
*/
typedef struct
{
        string_address system;
        string_address source;
        string_address build;
        host_settings address_to carry;
        bool carried;    // the slots of what was put in place are carry
        bool had_loader; // a rollback found a loader to keep
        bool switched;   // the loader is the image put in place: what firmware starts now
        bool unkept;     // the loader could not be read, so it is not kept as previous.efi
        bool stranded;   // stopped after the switch: the build the loader was is only in previous.new
        p8 image[HOST_PATH_ROOM];
        p8 next[HOST_PATH_ROOM];
        p8 kept[HOST_PATH_ROOM];
        p8 kept_next[HOST_PATH_ROOM];
        p8 kept_build[HOST_BUILD_ROOM]; // a rollback's: the build previous.efi reads as
        p8 sums[2][32];  // a rollback's: the new loader, its slots aside, and the one it replaces
} host_place;

/*
        An update, a step at a time, each one atomic on the partition or
        leaving only a file beside the images. A cut between any two leaves
        the loader it had before SWITCH and the new one after it. Nothing on
        the partition is touched for an image JUDGE refuses.
*/
enum
{
        HOST_PLACE_JUDGE,     // the image is one this machine starts, of the build it is to be
        HOST_PLACE_CLEAR,     // what a cut write left beside the images is removed
        HOST_PLACE_KEEP_COPY, // the loader copied to previous.new and synced
        HOST_PLACE_KEEP,      // and renamed over previous.efi once it is the loader byte for byte
        HOST_PLACE_COPY,      // the image copied to BOOTX64.NEW and synced
        HOST_PLACE_STAMP,     // both its slots say what the disk is to keep
        HOST_PLACE_CHECK,     // read back: the image but the slots, one this machine starts, the build
        HOST_PLACE_SWITCH,    // renamed over the loader, and the partition synced
        HOST_PLACE_STEPS
};

/*
        A rollback: previous.efi made the loader, and the loader kept as
        previous.efi. A cut before SWITCH leaves the machine as it was; one
        between SWITCH and KEEP leaves it starting the build asked for, with
        that build in previous.efi as well and the one it replaced only in
        the copy beside it, which the next write removes.
*/
enum
{
        HOST_BACK_JUDGE,     // previous.efi is one this machine starts, of the build it is to be
        HOST_BACK_CLEAR,     // what a cut write left beside the images is removed
        HOST_BACK_COPY,      // previous.efi copied to BOOTX64.NEW and synced
        HOST_BACK_STAMP,     // both its slots say what the loader keeps now
        HOST_BACK_CHECK,     // read back: previous.efi but the slots, one this machine starts, its build
        HOST_BACK_KEEP_COPY, // the loader copied to previous.new, synced and read back
        HOST_BACK_SWITCH,    // BOOTX64.NEW renamed over the loader
        HOST_BACK_KEEP,      // previous.new renamed over previous.efi, and the partition synced
        HOST_BACK_STEPS
};

static bool host_place_open(host_place address_to place, string_address system,
                            string_address source, string_address build,
                            host_settings address_to carry)
{
        static const string_address directories[] = {"/EFI", "/EFI/BOOT", HOST_KEPT_DIRECTORY};
        p8 directory[HOST_PATH_ROOM];

        memory_zero(place, sizeof(address_to place));
        place->system = system;
        place->source = source;
        place->build = build;
        place->carry = carry;

        for (positive at = 0; at < array_count(directories); at++)
        {
                if (!host_join(directory, sizeof(directory), system, directories[at]))
                        return false;
                system_make_directory_at(AT_FDCWD, directory, 0755);
        }

        return host_join(place->image, sizeof(place->image), system, HOST_IMAGE) &&
               host_join(place->next, sizeof(place->next), system, HOST_IMAGE_NEXT) &&
               host_join(place->kept, sizeof(place->kept), system, HOST_KEPT_IMAGE) &&
               host_join(place->kept_next, sizeof(place->kept_next), system,
                         HOST_KEPT_IMAGE_NEXT);
}

static fn host_place_sync(string_address system)
{
        bipolar directory = system_open_at(AT_FDCWD, system,
                                           FILE_READ | O_DIRECTORY | O_CLOEXEC);

        if (directory >= 0)
        {
                system_call_1(syscall(syncfs), (positive)directory);
                system_close(directory);
        }
}

/* Both slots of the image beside the loader as carry, before a rename makes
   it the one that starts: an image with no section for them is put in place
   as it is, and its caller says it starts with the defaults. */
static b32 host_place_stamp(host_place address_to place)
{
        bipolar failed = place->carry ? host_settings_stamp(place->next, place->carry) : 0;

        return_if(failed < 0, host_fail(place->next, failed));
        place->carried = place->carry && !failed;
        return 0;
}

/*
        What is to be written is an image this machine's firmware starts, and
        the build it is to be, as its bytes say and not as anything said it
        was; asked before anything on the partition is touched.
*/
static b32 host_place_judge(host_place address_to place)
{
        p8 build[HOST_BUILD_ROOM];
        p16 machine;
        b32 fit = host_image_fit(place->source, build, sizeof(build), address_of machine);

        if (fit)
                return host_refuse("%s is not an image this machine starts, so nothing was "
                                   "written: %s\n", place->source, host_fit_said(fit, machine));
        if (!string_equals(build, place->build))
                return host_refuse("%s is %s, not %s, so nothing was written\n", place->source,
                                   build, place->build);
        return 0;
}

/*
        The file that is to be renamed over the loader is the one that was
        copied, in every byte but the slots, and the slots are the ones
        stamped; it is an image this machine's firmware starts; and it is the
        build it is to be, read again from the copy.
*/
static b32 host_place_check(host_place address_to place)
{
        p8 build[HOST_BUILD_ROOM];

        if (!host_image_is(place->next, place->source, place->carried ? place->carry : null))
                return host_refuse("%s is not the image it was copied from\n", place->next);

        if (host_image_fit(place->next, build, sizeof(build), null) ||
            !string_equals(build, place->build))
                return host_refuse("%s did not read back as %s\n", place->next, place->build);

        return 0;
}

/*
        The loader copied to previous.new to be kept, which an update and a
        rollback do before they replace it. A loader that cannot be read to
        its end is not kept, and does not stop the write that replaces it: an
        image that cannot be read is what a write over it is for, and the
        copy that failed stopped the update or rollback before anything was
        written, so a loader that had gone bad could not be replaced at all.
        What was copied of it is removed, previous.efi is left as it is, that
        is said, and unkept tells the steps after.
*/
static b32 host_place_keep_copy(host_place address_to place)
{
        p8 sum[32];

        if (!host_copy_file(place->image, place->kept_next))
                return 0;
        //      Which side stopped the copy is asked of the loader, read again to
        //      its end, and only when it did stop: a whole copy reads it once.
        if (host_image_sum(place->image, false, sum))
                return 1;
        place->unkept = true;
        system_remove_at(AT_FDCWD, place->kept_next, 0);
        host_say(log_error, host_label "%s cannot be read, so it is not kept to go back to: "
                                       "%s is left as it was\n",
                 place->image, place->kept);
        return 0;
}

static b32 host_place_step(host_place address_to place, positive step)
{
        bipolar failed;
        bool loader = system_access_at(AT_FDCWD, place->image, 0) >= 0;

        switch (step)
        {
        case HOST_PLACE_JUDGE:
                return host_place_judge(place);
        case HOST_PLACE_CLEAR:
                host_image_discard(place->system);
                return 0;
        case HOST_PLACE_KEEP_COPY:
                return loader ? host_place_keep_copy(place) : 0;
        case HOST_PLACE_KEEP:
                if (!loader || place->unkept)
                        return 0;
                if (!host_files_alike(place->image, place->kept_next, 0, 0))
                        return host_refuse("%s could not be kept whole\n", place->image);
                failed = system_rename_at(AT_FDCWD, place->kept_next, AT_FDCWD, place->kept, 0);
                return failed < 0 ? host_fail(place->kept, failed) : 0;
        case HOST_PLACE_COPY:
                return host_copy_file(place->source, place->next);
        case HOST_PLACE_STAMP:
                return host_place_stamp(place);
        case HOST_PLACE_CHECK:
                return host_place_check(place);
        case HOST_PLACE_SWITCH:
                failed = system_rename_at(AT_FDCWD, place->next, AT_FDCWD, place->image, 0);
                return_if(failed < 0, host_fail(place->image, failed));
                place->switched = true;
                host_place_sync(place->system);
                return 0;
        }
        return 0;
}

static b32 host_back_step(host_place address_to place, positive step)
{
        bipolar failed;

        switch (step)
        {
        case HOST_BACK_JUDGE:
                return host_place_judge(place);
        case HOST_BACK_CLEAR:
                host_image_discard(place->system);
                return 0;
        case HOST_BACK_COPY:
                return host_copy_file(place->kept, place->next);
        case HOST_BACK_STAMP:
                return host_place_stamp(place);
        case HOST_BACK_CHECK:
                if (host_place_check(place))
                        return 1;
                return host_image_sum(place->next, true, place->sums[0])
                           ? 0
                           : host_refuse("%s could not be read back\n", place->next);
        case HOST_BACK_KEEP_COPY:
                place->had_loader = system_access_at(AT_FDCWD, place->image, 0) >= 0;
                if (!place->had_loader)
                        return 0;
                if (host_place_keep_copy(place))
                        return 1;
                //      Not kept, so there is nothing for KEEP to rename or for
                //      the read back to find.
                if (place->unkept)
                {
                        place->had_loader = false;
                        return 0;
                }
                if (!host_files_alike(place->image, place->kept_next, 0, 0) ||
                    !host_image_sum(place->kept_next, false, place->sums[1]))
                        return host_refuse("%s could not be kept whole\n", place->image);
                return 0;
        case HOST_BACK_SWITCH:
                failed = system_rename_at(AT_FDCWD, place->next, AT_FDCWD, place->image, 0);
                return_if(failed < 0, host_fail(place->image, failed));
                place->switched = true;
                return 0;
        case HOST_BACK_KEEP:
                failed = place->had_loader ? system_rename_at(AT_FDCWD, place->kept_next,
                                                              AT_FDCWD, place->kept, 0)
                                           : 0;
                return_if(failed < 0, host_fail(place->kept, failed));
                host_place_sync(place->system);
                return 0;
        }
        return 0;
}

/*
        What a write that stopped at step leaves: what it was making beside
        the images is removed, and a refusal of the judge is before anything
        and leaves the partition as it was.

        Once the loader has been switched nothing beside it is removed: the
        only step after a switch is a rollback's KEEP, and when its rename
        fails, previous.efi is the build asked for (the loader now) and the
        build the loader was is in previous.new and nowhere else, written,
        synced and read back by KEEP_COPY. It stays, the partition is synced
        with the loader the switch put there, and the caller says what the
        machine starts (host_rollback_locked). The cleanup removed it, so the
        only copy of the build a rollback replaced went with a failed rename.
*/
static fn host_place_stopped(host_place address_to place, positive step)
{
        place->stranded = place->switched;
        if (place->switched)
                host_place_sync(place->system);
        else if (step != HOST_PLACE_JUDGE)
                host_image_discard(place->system);
}

/* Every step of an update, or of a rollback, in order: the first that fails
   stops it, and host_place_stopped says what is left. */
static b32 host_place_written(host_place address_to place, bool back)
{
        positive steps = back ? HOST_BACK_STEPS : HOST_PLACE_STEPS;

        for (positive step = 0; step < steps; step++)
                if (back ? host_back_step(place, step) : host_place_step(place, step))
                {
                        host_place_stopped(place, step);
                        return 1;
                }
        return 0;
}

// What an update put in place, as the disk has it: the image it was copied from.
static bool host_place_landed(string_address system, address_any opaque)
{
        host_place address_to place = (host_place address_to)opaque;

        (void)system;
        return host_image_is(place->image, place->source,
                             place->carried ? place->carry : null);
}

// What a rollback put in place, as the disk has it: the two sums it took on the way.
static bool host_back_landed(string_address system, address_any opaque)
{
        host_place address_to place = (host_place address_to)opaque;
        p8 sum[32];

        (void)system;
        return host_image_sum(place->image, true, sum) &&
               !memory_compare(sum, place->sums[0], sizeof(sum)) &&
               (!place->carried || host_image_slots_are(place->image, place->carry)) &&
               (!place->had_loader ||
                (host_image_sum(place->kept, false, sum) &&
                 !memory_compare(sum, place->sums[1], sizeof(sum))));
}

/*
        An image onto a mounted system partition, which is the last step of
        an install and the whole of an update: source, which is to read back
        as build, with the settings carry stamped into it. The image there
        before is kept as previous.efi, for `setup rollback`, the firmware's
        shell or a stick to start when a build turns out bad, unless it
        cannot be read, which unkept (when it is not null) then says. What
        it did is read back from the disk itself, the partition unmounted,
        and the partition is left unmounted.
*/
static b32 host_place_image(string_address name, string_address system,
                            string_address source, string_address build,
                            host_settings address_to carry, bool address_to carried,
                            bool address_to unkept)
{
        host_place place;

        *carried = false;
        if (unkept)
                *unkept = false;
        if (!host_place_open(address_of place, system, source, build, carry))
                return host_refuse("%s: path too long\n", system);

        if (host_place_written(address_of place, false))
                return 1;

        *carried = place.carried;
        if (unkept)
                *unkept = place.unkept;
        if (!host_on_disk(name, system, host_place_landed, address_of place))
                return host_refuse("%s did not read back from the disk as the image "
                                   "that was written\n", name);

        return 0;
}

// The boot splash -----------------------------------------------

/*
        The picture Moonwater's loader draws before the kernel starts (src/moonwater/splash.c says what a
        file of one is, kernel/patch/splash-stub.c draws it): a file beside the images on the system
        partition, SPLASH_FILE, written here and nowhere else.

        It is written as the loader is: to a file beside it, synced and read back, and renamed over it, so
        a machine that loses power at any step finds the picture it had or the new one, whole. What a cut
        left beside it is removed before the next write, of a picture or of an image, and never opened.
        And it cannot stop a machine starting whatever becomes of it: the loader draws a file that checks
        and is silent about one that does not, and nothing else reads it.

        What is written is never a person's bytes as they came: the header is splash_header's and the
        pixels are the ones splash_parse checked, so the file the firmware's file system hands the loader
        is one this wrote.

        An update and a rollback replace the loader and the image kept behind it and nothing else on the
        partition, so the picture a disk had is the picture it has. An install writes one to the partition
        it has just made: the picture of the session it is made from, which is how one set on a live stick
        is still set on the disk it installed, or the built-in one (splash_mark.c) when that session has
        none.

        The firmware's own logo, the one a machine shows before any loader runs, is not this and is not
        written: host_splash_firmware says why for the firmware it finds.
*/
static p8 host_splash_bytes[SPLASH_FILE_MOST + 1]; // a picture as its file: the header and the pixels
static p8 host_splash_seen[SPLASH_FILE_MOST + 1];  // one read back, to hold against it

// The file at path read whole into into: its count, or the negative reason, -2 when it is larger than the most.
static bipolar host_splash_read(string_address path, p8 address_to into)
{
        bipolar handle = system_open_at(AT_FDCWD, path, FILE_READ | O_CLOEXEC | O_NONBLOCK);
        positive length = 0;

        if (handle < 0)
                return handle;
        while (length <= SPLASH_FILE_MOST)
        {
                bipolar got = system_read_once(handle, into + length, SPLASH_FILE_MOST + 1 - length);

                if (got < 0)
                {
                        system_close(handle);
                        return got;
                }
                if (!got)
                        break;
                length += (positive)got;
        }
        system_close(handle);
        return length > SPLASH_FILE_MOST ? -2 : (bipolar)length;
}

/*
        The picture at path as the file this writes, in host_splash_bytes: its length, with image the
        picture in it, or 0 with why it is not a usable one in reason. The header a file came with is not
        kept: splash_header's is never longer than one that parsed, so the pixels only move down.
*/
static positive host_splash_take(string_address path, struct splash_image address_to image,
                                 string_address address_to reason)
{
        p8 header[SPLASH_HEADER_MOST];
        bipolar length = host_splash_read(path, host_splash_bytes);
        positive pixels;
        positive head;

        address_to reason = length == -2  ? (string_address) "it is larger than 1 MiB"
                            : length < 0 ? file_reason(length)
                                         : (string_address)splash_parse(host_splash_bytes, (unsigned long)length, image);
        if (address_to reason)
                return 0;

        pixels = (positive)image->width * image->height * 3;
        head = splash_header(header, image->width, image->height);
        memory_copy(host_splash_bytes + head, (p8 address_to)image->rgb, pixels);
        memory_copy(host_splash_bytes, header, head);
        image->rgb = host_splash_bytes + head;
        return head + pixels;
}

// The built-in picture as the file this writes, in host_splash_bytes: its length (black wordmark on pure white).
static positive host_splash_default(void)
{
        positive head = splash_header(host_splash_bytes, SPLASH_MARK_W, SPLASH_MARK_H);

        for (positive at = 0; at < SPLASH_MARK_W * SPLASH_MARK_H; at++)
        {
                p8 grey = (p8)(255u - ((splash_mark[at >> 2] >> (6 - 2 * (at & 3))) & 3u) * 85u);

                host_splash_bytes[head + at * 3] = grey;
                host_splash_bytes[head + at * 3 + 1] = grey;
                host_splash_bytes[head + at * 3 + 2] = grey;
        }
        return head + SPLASH_MARK_W * SPLASH_MARK_H * 3;
}

// Whether bytes are the previous dark-background built-in splash (white wordmark on black).
static bool host_splash_is_legacy_dark(const p8 address_to bytes, positive length)
{
        struct splash_image image;

        if (splash_parse(bytes, (unsigned long)length, address_of image) ||
            image.width != SPLASH_MARK_W || image.height != SPLASH_MARK_H)
                return false;
        for (positive at = 0; at < SPLASH_MARK_W * SPLASH_MARK_H; at++)
        {
                p8 grey = (p8)(((splash_mark[at >> 2] >> (6 - 2 * (at & 3))) & 3u) * 85u);

                if (image.rgb[at * 3] != grey || image.rgb[at * 3 + 1] != grey || image.rgb[at * 3 + 2] != grey)
                        return false;
        }
        return true;
}

// Whether the file at path is these bytes and no more, as the disk has them: its pages are dropped before it is read.
static bool host_splash_file_is(string_address path, const p8 address_to bytes, positive length)
{
        bipolar handle = system_open_at(AT_FDCWD, path, FILE_READ | O_CLOEXEC);
        bipolar seen;

        if (handle < 0)
                return false;
        (void)system_call_4(syscall(fadvise64), (positive)handle, 0, 0, HOST_FADVISE_DONTNEED);
        system_close(handle);

        seen = host_splash_read(path, host_splash_seen);
        return seen == (bipolar)length && !memory_compare(host_splash_seen, bytes, length);
}

/*
        A picture onto a system partition mounted at system, a step at a time: what a cut write left is
        removed, the picture is written beside its name and synced, read back, and renamed over the name,
        and the partition is synced. 0, or the step that failed said, with nothing left beside the name.
*/
static b32 host_splash_place(string_address system, const p8 address_to bytes, positive length)
{
        static const string_address directories[] = {"/EFI", SPLASH_DIRECTORY};
        p8 path[HOST_PATH_ROOM];
        p8 next[HOST_PATH_ROOM];
        bipolar handle;
        bipolar failed;

        if (!length || length > SPLASH_FILE_MOST || !host_join(path, sizeof(path), system, SPLASH_FILE) ||
            !host_join(next, sizeof(next), system, SPLASH_FILE_NEXT))
                return host_refuse("splash: %s: path too long\n", system);

        for (positive at = 0; at < array_count(directories); at++)
        {
                p8 directory[HOST_PATH_ROOM];

                if (host_join(directory, sizeof(directory), system, directories[at]))
                        system_make_directory_at(AT_FDCWD, directory, 0755);
        }

        //      Made new and never through a name already there: the system partition is FAT, which has no links, but a
        //      system that is a directory (the checks', a session's) can have one planted at the name between the remove
        //      and the open, and the picture would be written wherever it pointed.
        system_remove_at(AT_FDCWD, next, 0);
        handle = system_open_at_mode(AT_FDCWD, next, FILE_WRITE | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
        return_if(handle < 0, host_fail(next, handle));

        failed = storage_write(handle, (p8 address_to)bytes, length, 0) == (bipolar)length
                     ? system_call_1(syscall(fsync), (positive)handle)
                     : -ERROR_INPUT_OUTPUT;
        system_close(handle);
        if (failed >= 0 && !host_splash_file_is(next, bytes, length))
                failed = -ERROR_INPUT_OUTPUT;
        if (failed >= 0)
                failed = system_rename_at(AT_FDCWD, next, AT_FDCWD, path, 0);
        if (failed < 0)
        {
                system_remove_at(AT_FDCWD, next, 0);
                return host_fail(path, failed);
        }

        host_place_sync(system);
        return 0;
}

// No picture on a system partition mounted at system: the file and what a cut write left beside it removed, and the partition synced.
static b32 host_splash_clear(string_address system)
{
        p8 path[HOST_PATH_ROOM];
        bipolar gone = 0;

        if (host_join(path, sizeof(path), system, SPLASH_FILE_NEXT))
                system_remove_at(AT_FDCWD, path, 0);
        if (host_join(path, sizeof(path), system, SPLASH_FILE))
                gone = system_remove_at(AT_FDCWD, path, 0);
        return_if(gone < 0 && gone != -ERROR_NO_ENTRY, host_fail(path, gone));

        host_place_sync(system);
        return 0;
}

typedef struct
{
        const p8 address_to bytes; // the picture that was written; null when it was taken away
        positive length;
} host_splash_want;

// What a write of the picture left, as the disk has it (host_on_disk): the bytes written, or no file at all.
static bool host_splash_landed(string_address system, address_any opaque)
{
        host_splash_want address_to want = (host_splash_want address_to)opaque;
        p8 path[HOST_PATH_ROOM];

        if (!host_join(path, sizeof(path), system, SPLASH_FILE))
                return false;
        if (want->bytes)
                return host_splash_file_is(path, want->bytes, want->length);
        return system_access_at(AT_FDCWD, path, 0) == -ERROR_NO_ENTRY;
}

/*
        The picture an install gives the partition it has made, mounted at system: the one the session's
        medium keeps (mounted at medium) when that checks, else the built-in one. It is in host_splash_bytes
        afterwards, its length in length for the check of the disk that follows (host_splash_installed), and 0
        there when it could not be written. An install is never failed for its picture: what went wrong is
        said, and the disk starts without one.
*/
static fn host_splash_install(string_address medium, string_address system, string_address disk,
                              positive address_to length)
{
        struct splash_image image;
        string_address reason = null;
        p8 path[HOST_PATH_ROOM];
        positive own = host_join(path, sizeof(path), medium, SPLASH_FILE)
                           ? host_splash_take(path, address_of image, address_of reason)
                           : 0;

        address_to length = own ? own : host_splash_default();
        if (host_splash_place(system, host_splash_bytes, address_to length))
        {
                address_to length = 0;
                host_say(log_error, host_label "%s starts with no boot splash\n", disk);
                return;
        }
        if (own)
                host_say(log, host_label "%s shows this session's boot splash, %p x %p, before the kernel starts\n",
                         disk, (positive)image.width, (positive)image.height);
        else
                host_say(log, host_label "%s shows Moonwater's boot splash before the kernel starts\n", disk);
}

/*
        Ensure a system partition being updated has a valid, up-to-date boot splash:
        if the live medium carries a valid splash, install it; otherwise keep any valid
        non-legacy splash already on the disk, and write the built-in white/black splash
        whenever the disk has none, a damaged file, or the old dark default.
*/
static fn host_splash_ensure(string_address medium, string_address system, string_address disk,
                             positive address_to length)
{
        struct splash_image image;
        string_address reason = null;
        p8 path[HOST_PATH_ROOM];
        positive own = medium && host_join(path, sizeof(path), medium, SPLASH_FILE)
                           ? host_splash_take(path, address_of image, address_of reason)
                           : 0;

        if (!own && host_join(path, sizeof(path), system, SPLASH_FILE))
        {
                bipolar seen = host_splash_read(path, host_splash_seen);

                if (seen > 0 && !splash_parse(host_splash_seen, (unsigned long)seen, address_of image) &&
                    !host_splash_is_legacy_dark(host_splash_seen, (positive)seen))
                {
                        address_to length = 0;
                        return;
                }
        }

        address_to length = own ? own : host_splash_default();
        if (host_splash_place(system, host_splash_bytes, address_to length))
        {
                address_to length = 0;
                host_say(log_error, host_label "%s starts with no boot splash\n", disk);
                return;
        }
        if (own)
                host_say(log, host_label "%s shows this session's boot splash, %p x %p, before the kernel starts\n",
                         disk, (positive)image.width, (positive)image.height);
        else
                host_say(log, host_label "%s shows Moonwater's boot splash before the kernel starts\n", disk);
}

// Whether the picture an install wrote is on the disk as written, the partition name read again from a mount of its own.
static fn host_splash_installed(string_address name, positive length)
{
        host_splash_want want = {host_splash_bytes, length};
        bool landed;

        if (!length)
                return;
        if (host_mount(name, HOST_SYSTEM, "vfat", HOST_READ_ONLY) < 0)
        {
                host_say(log_error, host_label "%s could not be mounted again to read its boot splash back\n", name);
                return;
        }
        landed = host_splash_landed(HOST_SYSTEM, address_of want);
        host_unmount(HOST_SYSTEM);
        if (!landed)
                host_say(log_error, host_label "the boot splash on %s did not read back as written; the loader "
                                               "draws it only if it checks\n", name);
}

// Keeping data --------------------------------------------------

static bool host_publish_visit(string_address directory, string_address name,
                               address_any unused)
{
        bowl_publish_bin(name);
        return true;
}

/*
        The data partition, mounted, over the directories it keeps.

        Binds, not a new root: the Moonwater running is the one that booted,
        whichever disk its data comes from. Bowl's launchers are published on
        /bin again, since /bin is the image's and forgot them at power off.
*/
static b32 host_attach(host_install address_to install)
{
        bipolar failed;
        positive at;

        if (!host_install_refresh(install))
                return host_refuse("%s is no longer here\n", install->disk);

        failed = host_mount(install->data, HOST_DATA, "ext4", HOST_DATA_MOUNT);

        return_if(failed < 0, host_refuse("%s is the Moonwater data partition of %s, and it could "
                                          "not be mounted: %s\n",
                                          install->data, install->disk, file_reason(failed)));

        for (at = 0; at < array_count(host_kept); at++)
        {
                p8 on_disk[HOST_PATH_ROOM];
                p8 through[HOST_PATH_ROOM];
                p8 number[24];
                bipolar pinned;

                host_join(on_disk, sizeof(on_disk), HOST_DATA, host_kept[at].path);
                failed = host_make_directory(on_disk, host_kept[at].mode, false);
                if (failed < 0)
                        break;

                /*      What the data partition calls this directory is its
                        say, and a link there (home -> /etc) would have this
                        bind the live /etc over /home. The name is opened as
                        a directory and never through a link, and that very
                        directory is what is bound, whatever the name becomes
                        after. */
                pinned = system_open_at(AT_FDCWD, on_disk,
                                        O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
                if (pinned < 0)
                {
                        failed = pinned == -ELOOP ? -ENOTDIR : pinned;
                        break;
                }

                failed = host_make_directory(host_kept[at].path, host_kept[at].mode,
                                             false);
                if (failed < 0)
                {
                        system_close(pinned);
                        break;
                }

                positive_into_string(number, (positive)pinned);
                failed = host_join(through, sizeof(through), "/proc/self/fd/", number)
                             ? system_mount(through, host_kept[at].path, 0, MS_BIND, 0)
                             : -ERROR_INVALID;
                system_close(pinned);
                if (failed < 0)
                        break;
        }

        if (at < array_count(host_kept))
        {
                b32 said = host_fail(host_kept[at].path, failed);

                while (at-- > 0)
                        system_call_2(syscall(umount2), (positive)host_kept[at].path,
                                      MNT_DETACH);
                system_call_2(syscall(umount2), (positive)HOST_DATA, MNT_DETACH);
                return said;
        }

        bowl_mkdir(BOWL_EXPOSE_DIRECTORY);
        //      A profile's launcher from before the session moved into this binary is written again, so that a desktop
        //      typed by hand is the one the verb starts.
        for (positive profile = 0; profile < array_count(bowl_profiles); profile++)
                (void)bowl_profile_refresh(bowl_profiles + profile);
        host_each_entry(BOWL_EXPOSE_DIRECTORY, host_publish_visit, null);
        return 0;
}

/*
        A medium identity that is still all nought has never been drawn; the
        first writer to find it so draws it.
*/
static PURE bool host_medium_blank(const p8 address_to medium, positive size)
{
        return memory_span_byte((address_any)medium, 0, size) == size;
}

static fn host_medium_fresh(p8 address_to medium, positive size)
{
        if (host_medium_blank(medium, size))
                system_random_fill(medium, size, 0);
}

/*
        An update keeps the disk's settings: they are that machine's, and the
        stick is only carrying a build. An image from before there were
        settings has none, which is the defaults. What the loader on the
        system partition mounted at HOST_SYSTEM keeps, as the next generation
        of it, into disk.

        A loader that is there and cannot be read says nothing of the
        settings in it, and the write goes on with the defaults, said before
        anything is written: it was refused, so a loader gone bad could not
        be replaced by the update that would mend it. A loader that reads and
        has no settings is told from one that does not read by reading it to
        its end, which only those two need. lost is whether the loader's
        settings were there and could not be read, either way, so that what
        follows does not say the disk keeps them.
*/
static b32 host_settings_carried(host_install address_to install, host_settings address_to disk,
                                 bool address_to lost)
{
        p8 path[HOST_PATH_ROOM];
        p8 sum[32];
        b32 read = -1;

        host_settings_empty(disk);
        *lost = false;
        if (host_join(path, sizeof(path), HOST_SYSTEM, HOST_IMAGE))
        {
                bipolar opened = system_open_at(AT_FDCWD, path, FILE_READ | O_CLOEXEC);

                if (opened >= 0)
                {
                        system_close(opened);
                        read = host_settings_image(path, disk);
                }
                *lost = opened != -ERROR_NO_ENTRY && read < 0 &&
                        !host_image_sum(path, false, sum);
                if (*lost)
                        host_say(log_error, host_label "the image on %s cannot be read, so the "
                                                       "settings in it cannot be kept: %s starts "
                                                       "from the defaults\n",
                                 install->system, install->disk);
        }

        if (read == 0)
                host_say(log, host_label "both settings slots on %s are damaged: "
                                         "it starts from the defaults\n",
                         install->disk);
        *lost |= read == 0;

        disk->generation++;
        host_medium_fresh(disk->medium, sizeof(disk->medium));
        return 0;
}

/* What became of the settings an image was given, said: whose names the image,
   and lost is host_settings_carried's, whose line then said what the disk has. */
static fn host_settings_landed(host_install address_to install, host_settings address_to disk,
                               bool carried, bool lost, string_address whose)
{
        host_settings session;

        if (!carried)
        {
                host_say(log, host_label "%s has no place for settings, so %s starts "
                                         "with the defaults\n", whose, install->disk);
                return;
        }
        if (lost)
                return;

        //      A session started from this disk is still its copy.
        if (host_settings_kept(address_of session) &&
            !memory_compare(session.medium, disk->medium, sizeof(disk->medium)))
        {
                session.generation = disk->generation;
                host_settings_keep(address_of session);
        }

        host_say(log, host_label "%s keeps its own settings\n", install->disk);
}

static b32 host_update_locked(host_install address_to install)
{
        p8 running[HOST_BUILD_ROOM];
        p8 source[HOST_PATH_ROOM];
        host_medium_search search;
        host_settings disk;
        positive splash = 0;
        bool carried;
        bool lost;
        b32 failed;

        if (!host_running_build(running, sizeof(running)))
                return host_refuse("moonwater cannot read its own build\n");

        if (!host_install_refresh(install))
                return host_refuse("%s is no longer here\n", install->disk);

        if (!host_medium_find(address_of search, running, install->disk))
                return host_refuse("this session's image is on no disk but %s, "
                                   "so there is nothing to update from\n",
                                   install->disk);

        //      The search can wait seconds for a stick, in which time a
        //      disk comes and goes and its name goes to another.
        if (!host_install_refresh(install))
        {
                host_unmount(HOST_MEDIUM);
                return host_refuse("%s is no longer here\n", install->disk);
        }

        if (!host_medium_whole())
        {
                host_unmount(HOST_MEDIUM);
                return 1;
        }

        if (host_system_mount(install, false))
        {
                host_unmount(HOST_MEDIUM);
                return 1;
        }

        host_say(log, host_label "writing this build to %s, from %s\n",
                 install->system, search.name);

        failed = host_settings_carried(install, address_of disk, address_of lost);
        if (!failed && !host_join(source, sizeof(source), HOST_MEDIUM, HOST_IMAGE))
                failed = host_refuse("%s: path too long\n", HOST_MEDIUM);
        if (!failed)
                host_splash_ensure(HOST_MEDIUM, HOST_SYSTEM, install->disk, address_of splash);
        if (!failed)
                failed = host_place_image(install->system, HOST_SYSTEM, source, running,
                                          address_of disk, address_of carried, null);
        if (!failed)
                host_settings_landed(install, address_of disk, carried, lost,
                                     "this build's image");

        host_unmount(HOST_SYSTEM);
        host_unmount(HOST_MEDIUM);
        if (!failed)
                host_splash_installed(install->system, splash);
        return failed;
}

static b32 host_update(host_install address_to install)
{
        b32 failed;

        if (!host_acquire())
                return 1;

        failed = host_update_locked(install);
        host_release();
        return failed;
}

/* A program to the end, its failure said; the install copies trees with cp. */
static b32 host_run(string_address address_to argv)
{
        positive status = 0;
        bipolar child = system_fork();
        bipolar reaped;

        return_if(child < 0, host_fail(argv[0], child));

        if (child == 0)
        {
                //      An ignored signal is ignored by what is exec'd as well.
                (void)system_signal_install(13, 0, 0, 0, null);
                // Through the launch decision every other exec here takes.
                (void)shell_exec_file(argv[0], argv, pointer_vector_count(argv),
                                      file_environment_all());
                system_call_1(syscall(exit), 127);
        }

        reaped = system_wait4_retry(child, address_of status, 0, null);

        return_if(reaped < 0, host_fail(argv[0], reaped));

        if (wait_status_code(status))
                return host_refuse("%s did not finish\n", argv[0]);

        return 0;
}

/*
        What a /root keeps, put back once it is the one to keep it.

        host_take runs it when a disk's /root has just been bound, and the
        machine process runs it at every start, whatever the verdict. An
        install's restores are so done a second time (each finds its work
        done or tried, as radio_wifi_restored says), and a session with no
        disk, or one that is only being asked, has no other place for them.

        radio is false for the machine process under an ask, as radio_recover
        has it: the radios wait for the answer, and the take that follows it
        restores them. name is the caller's: a take settles the name of the
        /root it bound, and the machine process leaves it to host_booted,
        which settles it in every branch of boot. The order is the one both
        callers always had, the name between usb and the locale. The groups an
        image carries are joined last, from the settings the session holds: a
        stick written from a group is in it before the first command (the
        link's listener starts after these), and an image that carries none
        changes nothing.
*/
static fn host_restore_session(bool radio, bool name)
{
        if (radio)
                radio_restore();
        usb_restore();
        if (name)
                name_restore();
        locale_restore();
        dns_restore();
        tune_restore();
        link_group_restore();
}

/*
        Use an install's data, updating it first when asked, and say so.

        A session keeps one disk's data: a second set of binds over the first
        hides it and is no better kept. The restores that follow start
        programs that outlive this one, so the lock is let go of before them.
*/
static b32 host_take(host_install address_to install, bool update)
{
        p8 kept[HOST_NAME_ROOM];

        if (!host_acquire())
                return 1;

        system_remove_at(AT_FDCWD, HOST_QUESTION, 0);

        if (host_session_disk(kept, sizeof(kept)))
        {
                host_release();
                return host_refuse("this session already keeps %s\n", kept);
        }

        if ((update && host_update(install)) || host_attach(install))
        {
                host_release();
                return 1;
        }

        host_verdict_set("disk ", install->disk);
        host_say(log, host_label "%s, /root and /home are kept on %s\n",
                 BOWL_ROOT_DIRECTORY, install->disk);
        host_release_all();
        host_restore_session(true, true);
        return 0;
}

// Boot ----------------------------------------------------------

/*
        The end of boot, whichever way it went.

        The machine's name is settled first, in every branch: a live stick
        rolls one the first time it boots, an install brings its own with its
        /root, and either way the prompt of the first shell and everything
        that asks the kernel what this machine is called has the answer.
*/
static fn host_booted(host_settings address_to settings)
{
        //      What follows starts commands that outlive this one.
        host_release_all();
        name_restore();
        //      Said, and boot goes on: TLS reads the file and not the settings, so
        //      a list that is kept and not published is not in force (or the last
        //      one is, where the removal failed), and nothing else would tell.
        (void)security_publish_answer(settings);
        host_events_boot(settings);
}

/*
        Whether a disk's place in sysfs says something can be plugged into
        it: a USB or Thunderbolt bus in the path. The link is what
        /sys/class/block/NAME points at.
*/
static PURE bool host_link_external(string_address link)
{
        return string_find(link, "/usb") || string_find(link, "/thunderbolt");
}

/*
        Whether a disk is on a bus something can be plugged into. The path
        says USB or Thunderbolt, the block device says removable, and a device
        above it may say so itself: a PCI device behind an external-facing
        port (a USB4 or Thunderbolt NVMe enclosure looks like any NVMe disk)
        reads "removable" in its own `removable` file, as does a USB device
        that is not soldered in. A disk that cannot be told is taken to be
        external.
*/
static bool host_disk_external(string_address disk)
{
        p8 sysfs[HOST_PATH_ROOM];
        p8 link[HOST_PATH_ROOM * 2];
        p8 path[HOST_PATH_ROOM * 2];
        p8 flag[16];
        string_address devices;

        if (!host_name_valid(disk) ||
            !host_join(sysfs, sizeof(sysfs), "/sys/class/block/", disk) ||
            file_link_text(sysfs, link, sizeof(link)) <= 0 ||
            host_link_external(link))
                return true;

        if (!host_join(sysfs, sizeof(sysfs), "/sys/block/", disk) ||
            !host_join(path, sizeof(path), sysfs, "/removable") ||
            host_read_kernel(path, flag, sizeof(flag)) < 0 || flag[0] != '0')
                return true;

        //      Every directory above the block device, up to /sys/devices.
        devices = string_find(link, "/devices/");
        if (!devices || !host_join(path, sizeof(path), "/sys", devices))
                return true;

        for (;;)
        {
                p8 address_to slash = memory_last_of(path, '/', string_length(path));

                if (!slash || slash <= path + sizeof("/sys/devices") - 1)
                        break;
                *slash = end;
                if (host_join(sysfs, sizeof(sysfs), path, "/removable") &&
                    host_read_kernel(sysfs, flag, sizeof(flag)) >= 0 &&
                    (string_equals(flag, "removable") || string_equals(flag, "1")))
                        return true;
        }

        return false;
}

/*
        Whether an install is the one the session started from: the settings
        an image boots with carry the medium they were written for, and the
        same number is in the image on the disk they came from. Only an
        install of the running build can be.
*/
static bool host_install_booted(host_install address_to install,
                                string_address running,
                                host_settings address_to booted)
{
        host_settings theirs;

        return booted && !host_medium_blank(booted->medium, sizeof(booted->medium)) &&
               install->readable && string_equals(install->build, running) &&
               host_settings_install(install, address_of theirs) &&
               !memory_compare(theirs.medium, booted->medium, sizeof(theirs.medium));
}

/*
        Whether boot takes, without asking, the only disk with this build when
        it is not known to be the one the session started from. On a bus
        nothing is plugged into it is; on one something is, a USB or
        Thunderbolt stick or a removable disk, it is asked about, and the
        tight tier asks about it wherever it is. Whether a lone stick of this
        build is taken on a machine that has no install of its own is the one
        decision here: to take it, return !tight.
*/
static PURE bool host_lone_taken(bool external, bool tight)
{
        return !external && !tight;
}

/*
        The one install of this build that the session started from, if it
        is one.
*/
static host_install address_to host_census_booted(host_census address_to census,
                                                  string_address running,
                                                  host_settings address_to booted)
{
        host_install address_to found = null;
        positive count = 0;

        for (positive at = 0; at < census->count; at++)
                if (host_install_booted(census->found + at, running, booted))
                {
                        found = census->found + at;
                        count++;
                }

        return count == 1 ? found : null;
}

static b32 host_boot(void)
{
        p8 running[HOST_BUILD_ROOM];
        p8 verdict[HOST_NAME_ROOM + 16];
        host_census census;
        host_install address_to chosen = null;
        host_install address_to refused = null;
        host_settings settings;
        positive same = 0;
        bool known;

        host_state_ready();
        if (!host_acquire())
                return 1;

        /*      Boot settles a session once, and what it wrote is what the
                terminals and the machine script have been waiting for. Run
                by hand it would settle again, and with every command bound
                to init running a second time. A second boot that was started
                while the first was settling waited for it on the lock above,
                and finds the verdict here. */
        if (host_read_word(HOST_VERDICT, verdict, sizeof(verdict)) >= 0)
        {
                host_release();
                return host_refuse("boot is init's, and has settled this session; "
                                   "moonwater setup shows what it found\n");
        }

        host_running_build(running, sizeof(running));

        /*      The settings the image booted with. A kernel started without
                the EFI stub has none, and takes an install's of this build
                once the disks are found. */
        known = host_settings_booted(address_of settings);
        if (known)
        {
                host_settings_keep(address_of settings);
                //      Before the disks are waited for: TLS can start sooner. Said
                //      when it cannot be, as host_booted says it for the rest.
                (void)security_publish_answer(address_of settings);
        }

        if (known && settings.flags & SPARK_SETTINGS_MOUNT_OFF)
        {
                host_settle_live();
                host_say(log, host_label "init mount is off: nothing on this machine's "
                                         "disks is mounted this session\n");
                host_booted(address_of settings);
                return 0;
        }

        for (;;)
        {
                p64 uptime = system_clock_ns(HOST_CLOCK_BOOTTIME);
                p64 wait = HOST_POLL_NS;

                host_census_take(address_of census);
                if (census.count || uptime >= HOST_SETTLE_MOST_NS ||
                    (uptime >= HOST_SETTLE_FLOOR_NS && !host_storage_arriving()))
                        break;

                /*      To the floor itself, not the next tenth of a second
                        past it: the first terminal's prompt is waiting on
                        this, and a live boot with nothing arriving answers
                        the moment the floor is reached. */
                if (uptime < HOST_SETTLE_FLOOR_NS &&
                    HOST_SETTLE_FLOOR_NS - uptime < wait)
                        wait = HOST_SETTLE_FLOOR_NS - uptime;

                host_pause(wait);
        }

        if (!census.count)
        {
                host_settle_live();
                host_booted(known ? address_of settings : null);
                return 0;
        }

        host_census_said(address_of census);

        for (positive at = 0; at < census.count; at++)
        {
                host_install_read(census.found + at);
                if (running[0] && census.found[at].readable &&
                    string_equals(census.found[at].build, running))
                {
                        if (!chosen)
                                chosen = census.found + at;
                        same++;
                }
        }

        /*      Two disks with this very build are no reason to take the first
                one the kernel listed: it is the disk this session started from,
                which carries the settings it started with, or it is asked. */
        if (same > 1)
        {
                chosen = known ? host_census_booted(address_of census, running,
                                                    address_of settings)
                               : null;
                if (!chosen)
                {
                        chosen = census.found;
                        host_mark_set(HOST_QUESTION);
                        host_verdict_set("ask ", chosen->disk);
                        host_say(log, host_label "more than one disk has Moonwater from "
                                      "this build, and %s is not known to be the one "
                                      "this started from.\n"
                                      host_label "moonwater setup use DISK names the one "
                                      "to keep.\n", chosen->disk);
                        host_booted(known ? address_of settings : null);
                        return 0;
                }
        }

        /*      One disk with this build is no better a reason than two when
                it is not known to be the one this session started from: a
                public release's build is what every copy says, and a stick
                somebody pushed in says it as well as the machine's own disk,
                its /root/rules then running as root. */
        if (same == 1 && chosen &&
            !host_install_booted(chosen, running, known ? address_of settings : null) &&
            !host_lone_taken(host_disk_external(chosen->disk),
                             MOONWATER_STRICT_HOST >= STRICT_TIGHT))
        {
                refused = chosen;
                chosen = null;
        }

        if (chosen)
        {
                if (!known && host_settings_install(chosen, address_of settings))
                {
                        known = true;
                        host_settings_keep(address_of settings);
                }

                if (known && settings.flags & SPARK_SETTINGS_MOUNT_OFF)
                {
                        host_settle_live();
                        host_say(log, host_label "init mount is off: %s is not mounted "
                                                 "this session\n",
                                 chosen->disk);
                        host_booted(address_of settings);
                        return 0;
                }

                if (!host_take(chosen, false))
                {
                        host_booted(known ? address_of settings : null);
                        return 0;
                }

                host_settle_kept_away(chosen->disk);
                host_booted(known ? address_of settings : null);
                return 1;
        }

        chosen = refused ? refused : census.found;
        host_mark_set(HOST_QUESTION);
        host_verdict_set("ask ", chosen->disk);
        host_say(log, refused
                          ? host_label "%s has Moonwater installed from this build, "
                                       "on a disk that is not known to be the one "
                                       "this started from.\n"
                      : chosen->readable
                          ? host_label "%s has Moonwater installed from another build.\n"
                          : host_label "%s has Moonwater's partitions and no image on "
                                       "them: an install that did not finish.\n",
                 chosen->disk);
        host_say(log, host_label "The installer asks what to do with it, or "
                      "moonwater setup use, update or live answers from a shell.\n");
        host_booted(known ? address_of settings : null);
        return 0;
}

/*
        The question, where somebody can see it.

        The first terminal to take the question file asks; it goes back if
        this one is closed without an answer, so the next terminal asks again.
*/
static fn host_question(string_address disk)
{
        host_census census;
        host_install address_to install;
        p8 running[HOST_BUILD_ROOM];
        p8 answer[16];

        host_census_take(address_of census);
        install = host_census_find(address_of census, disk);
        if (!install)
        {
                //      The disk went between boot's question and this terminal's
                //      look: the question goes back for the next one to see, and
                //      says why this one did not ask.
                host_say(log, host_label "%s is no longer here, so there is nothing to ask "
                                         "about\n", disk);
                //      Put back if it can be; the file is in /run, and one that will not go back is asked by moonwater setup.
                (void)system_rename_at(AT_FDCWD, HOST_QUESTION_TAKEN, AT_FDCWD, HOST_QUESTION, 0);
                return;
        }

        host_install_read(install);
        if (!host_running_build(running, sizeof(running)))
                string_copy(running, "a build whose version cannot be read");

        string_format(log, "\n" host_label "%s already has Moonwater on it:\n\n    %s\n\n"
                           host_label "and this is\n\n    %s\n\n",
                      disk,
                      install->readable ? (string_address)install->build
                                        : (string_address)"an image whose build cannot be read",
                      running);
        string_format(log, "  1  use %s's %s, /root and /home with this Moonwater\n",
                      disk, BOWL_ROOT_DIRECTORY);
        string_format(log, "  2  update %s to this Moonwater, then use them\n", disk);
        string_format(log, "  3  neither: a live session, and %s is left alone\n\n",
                      disk);

        for (;;)
        {
                bipolar got;

                host_say(log, host_label "1, 2 or 3? [1] ");

                got = host_read_line(answer, sizeof(answer));
                if (got == -1)
                        break;
                if (got == HOST_LINE_LONG)
                        continue;

                if (!answer[0] || string_equals(answer, "1") ||
                    string_equals(answer, "2"))
                {
                        if (!host_take(install, answer[0] == '2'))
                                return;
                        break;
                }

                if (string_equals(answer, "3"))
                {
                        host_verdict_set("live", "");
                        host_say(log, host_label "%s is left alone, and nothing "
                                                 "is kept this session\n", disk);
                        return;
                }
        }

        (void)system_rename_at(AT_FDCWD, HOST_QUESTION_TAKEN, AT_FDCWD, HOST_QUESTION, 0);
}

/*
        What a terminal does before it starts its shell, in the pty's child,
        so what this prints and reads is the window.

        Boot's verdict can be a few seconds away when the compositor starts the
        first terminal, and a shell that started first would have read /root
        before the disk's /root was there. Terminals opened later find it
        written and pass straight through.
*/
fn host_terminal_opening(void)
{
        p8 verdict[HOST_NAME_ROOM + 16];
        bool said = false;
        bipolar notify = host_verdict_watch();

        while (host_read_word(HOST_VERDICT, verdict, sizeof(verdict)) < 0)
        {
                p64 uptime = system_clock_ns(HOST_CLOCK_BOOTTIME);

                if (uptime >= HOST_VERDICT_WAIT_NS)
                {
                        host_verdict_unwatch(notify);
                        return;
                }

                if (!said && uptime >= HOST_LOOKING_NS)
                {
                        host_say(log, host_label "looking for Moonwater on "
                                                 "this machine's disks\n");
                        said = true;
                }

                host_verdict_pause(notify, said ? HOST_VERDICT_WAIT_NS
                                                : HOST_LOOKING_NS,
                                   HOST_VERDICT_POLL_NS);
        }
        host_verdict_unwatch(notify);
        //      A boot that asks is answered in the installer's window (the desktop opens it on a verdict of live or
        //      ask), or by moonwater ask, setup use, update or live from a shell: a terminal asks nothing.
}

// Install -------------------------------------------------------

/* A GPT GUID is stored with its first three fields little-endian.  Turn the
   exact sixteen bytes written into the PARTUUID spelling the kernel publishes. */
static fn host_partuuid(p8 address_to into, p8 address_to guid)
{
        p8 uuid[16];

        network_store_32(uuid, storage_le32(guid));
        network_store_16(uuid + 4, storage_le16(guid + 4));
        network_store_16(uuid + 6, storage_le16(guid + 6));
        memory_copy(uuid + 8, guid + 8, 8);
        storage_uuid_bytes(into, uuid);
}

/* Find the exact partitions just written, not whatever later happens to own
   the disk's old /dev name.  PARTUUID is generated before the GPT write and
   carried in the table itself, so hot-unplug/name reuse cannot redirect the
   remainder of an install onto another disk. */
static bool host_partition_resolve(p8 address_to guid,
                                   p8 address_to into)
{
        p8 uuid[37];

        host_partuuid(uuid, guid);
        return host_partition_uuid_resolve(uuid, into);
}

static bool host_partitions_wait(host_install address_to install,
                                 storage_format_partition address_to parts)
{
        for (positive tries = 0; tries < 50; tries++)
        {
                install->system[0] = install->data[0] = end;

                /*      The identities too, which every later step resolves
                        the names from again: without them the install's own
                        attach found no partition and said the disk had gone. */
                if (host_partition_resolve(parts[0].unique, install->system) &&
                    host_partition_resolve(parts[1].unique, install->data))
                {
                        host_partuuid(install->system_partuuid, parts[0].unique);
                        host_partuuid(install->data_partuuid, parts[1].unique);
                        install->resolved = system_clock_ns(HOST_CLOCK_BOOTTIME);
                        return true;
                }

                host_pause(HOST_POLL_NS);
        }

        return false;
}

/* The kernel's rescan of a disk's partitions, which waits while anything else
   still holds the disk. */
static bipolar host_rescan(bipolar handle)
{
        bipolar failed = 0;

        for (positive tries = 0; tries < 20; tries++)
        {
                failed = system_control(handle, HOST_BLKRRPART, 0);
                if (failed != -ERROR_BUSY)
                        break;

                host_pause(HOST_POLL_NS);
        }

        return failed;
}

typedef struct
{
        p64 blocks;
        p64 inodes;
        bool deep;
        bool unread;
} host_size;

/*
        What copying a tree will take of an ext4: its blocks and its inodes,
        stopping at the filesystem it starts on, as a bowl's /proc is not data.
        A file costs the blocks it has been given, rounded up to the ext4's
        and shared out among its links, a directory at least one -- a tmpfs
        gives it none -- and a link a block once it is too long to live in
        its inode.
*/
static fn host_measure(bipolar directory, string_address name, positive depth,
                       p64 device, host_size address_to size)
{
        file_facts facts;
        file_walk walk;
        struct linux_dirent64 address_to entry;
        positive format;
        p64 blocks;

        if (file_look_code(directory, name, AT_SYMLINK_NOFOLLOW, address_of facts) < 0)
                return;

        if (!depth)
                device = file_device_key(facts.device_major, facts.device_minor);
        else if (file_device_key(facts.device_major, facts.device_minor) != device)
                return;

        format = facts.mode & MODE_FORMAT;
        blocks = (facts.blocks * 512 + STORAGE_EXT4_BLOCK - 1) / STORAGE_EXT4_BLOCK;
        size->inodes++;

        if (format == MODE_LINK)
                size->blocks += facts.size >= 60;
        else if (format != MODE_DIRECTORY)
                size->blocks += facts.hard_links > 1
                                    ? (blocks + facts.hard_links - 1) / facts.hard_links
                                    : blocks;
        else
        {
                size->blocks += blocks ? blocks : 1;

                if (depth >= HOST_MEASURE_DEPTH)
                        size->deep = true;
                else if (!file_walk_open_found(address_of walk, directory, name))
                        size->unread = true;
                else
                {
                        while ((entry = file_walk_next(address_of walk)))
                                if (!file_is_dot(entry->d_name))
                                        host_measure(walk.handle, entry->d_name, depth + 1,
                                                     device, size);

                        file_walk_close(address_of walk);
                }
        }
}

/*
        Whether what the install copies fits the data partition it is about to
        make, said before anything is asked or written. A disk that is too
        small used to be erased, made bootable and given a data partition
        that took what fitted of a copy that stopped, which the next boot
        attached as the machine's own. A thirty-second part of both is held
        back for the blocks an ext4 spends on the way, in extents and
        directories.
*/
static bool host_data_fits(p64 bytes, string_address name)
{
        storage_ext4_plan plan;
        host_size size;

        memory_zero(address_of size, sizeof(size));
        for (positive at = 0; at < array_count(host_kept); at++)
                host_measure(AT_FDCWD, host_kept[at].path, 0, 0, address_of size);

        if (size.deep)
        {
                host_say(log_error, host_label "%s, /root and /home go deeper than %p "
                                               "levels, more than an install adds up; "
                                               "nothing written\n",
                         BOWL_ROOT_DIRECTORY, (positive)HOST_MEASURE_DEPTH);
                return false;
        }

        if (size.unread)
        {
                host_say(log_error, host_label "a directory under %s, /root or /home "
                                               "could not be read, so what an install "
                                               "copies is not known; nothing written\n",
                         BOWL_ROOT_DIRECTORY);
                return false;
        }

        if (!storage_ext4_layout(address_of plan, bytes))
        {
                host_say(log_error, host_label "the data partition on %s has no ext4 "
                                               "layout; nothing written\n", name);
                return false;
        }

        if (size.blocks + size.blocks / 32 + 64 > storage_ext4_free_blocks(address_of plan) ||
            size.inodes + size.inodes / 32 + 16 > storage_ext4_free_inodes(address_of plan))
        {
                host_say(log_error, host_label "%s, /root and /home hold %p MiB in %p files, "
                                               "and the data partition on %s would hold %p "
                                               "MiB in %p files; nothing written\n",
                         BOWL_ROOT_DIRECTORY,
                         size.blocks * STORAGE_EXT4_BLOCK >> 20, size.inodes, name,
                         storage_ext4_free_blocks(address_of plan) * STORAGE_EXT4_BLOCK >> 20,
                         storage_ext4_free_inodes(address_of plan));
                return false;
        }

        return true;
}

/*
        An install that did not finish is not left looking like one. Both of
        Moonwater's partitions on a disk make it an install to the next boot,
        which attaches whatever data reached it as the machine's own, so the
        table goes, at both ends of the disk since the kernel falls back to the
        backup.
*/
static bool host_install_clear(bipolar handle)
{
        p64 bytes = 0;
        bipolar failed = system_control(handle, HOST_BLKGETSIZE64, address_of bytes);

        if (failed >= 0 && bytes >= 2 * HOST_ALIGN_BYTES)
        {
                failed = storage_format_zero(handle, 0, HOST_ALIGN_BYTES);
                if (!failed)
                        failed = storage_format_zero(handle, bytes - HOST_ALIGN_BYTES,
                                                     HOST_ALIGN_BYTES);
                if (!failed)
                        failed = system_call_1(syscall(fsync), (positive)handle);
        }
        else
                failed = failed < 0 ? failed : -ERROR_INVALID;

        host_rescan(handle);
        return failed >= 0;
}

/* The same, once the partitions are mounted and the disk has been let go of. */
static fn host_install_abandon(host_install address_to target)
{
        p8 device[HOST_NAME_ROOM + 8];
        bipolar handle = -ERROR_INVALID;
        bool cleared = false;

        host_unmount(HOST_DATA);
        host_unmount(HOST_SYSTEM);
        host_unmount(HOST_MEDIUM);

        if (host_install_refresh(target) &&
            host_join(device, sizeof(device), "/dev/", target->disk))
                handle = system_open_at(AT_FDCWD, device,
                                        FILE_READ_WRITE | FILE_EXCLUSIVE | O_CLOEXEC);

        if (handle >= 0)
        {
                cleared = host_install_clear(handle);
                system_close(handle);
        }

        host_say(log_error, cleared ? host_label "%s is left without a partition table: "
                                                 "the install did not finish\n"
                                    : host_label "%s could not be cleared: the install did "
                                                 "not finish, and its data partition is "
                                                 "not to be trusted\n",
                 target->disk);
}

/*
        Where an install puts its partitions on a disk of so many bytes with
        sectors of so many: a 512 MiB system partition at 1 MiB, then the data
        partition to the last whole MiB the GPT leaves usable, and no more than
        an ext4 can address (16 TiB less a block). False for a disk under
        2 GiB, one whose sectors the GPT has no layout for, or one too small
        for both of its tables. Only the places are written into parts, which
        are two in a row.
*/
static bool host_install_place(p64 bytes, b32 sector, storage_format_partition address_to parts)
{
        p64 first;
        p64 last;
        p64 align;
        p64 most;

        if (bytes < HOST_SMALLEST || sector < 512 ||
            !storage_gpt_span(bytes / (p64)sector, (p32)sector, address_of first,
                              address_of last))
                return false;

        align = HOST_ALIGN_BYTES / (p64)sector;
        most = STORAGE_EXT4_MOST * STORAGE_EXT4_BLOCK / (p64)sector;
        parts[0].first = align;
        parts[0].last = align + HOST_SYSTEM_BYTES / (p64)sector - 1;
        parts[1].first = parts[0].last + 1;
        parts[1].last = (last + 1) / align * align - 1;
        if (parts[1].last - parts[1].first + 1 > most)
                parts[1].last = parts[1].first + most - 1;

        return true;
}

/*
        The layout an install writes, on an open disk (or a file the size of one, which is how the installer's check writes it):
        the table zeroed at both ends, a GPT with the system and data partitions where host_install_place put them, named
        HOST_SYSTEM_NAME and HOST_DATA_NAME (what the census and the installer find an install by), a FAT32 on the first and an
        ext4 on the second, from eighty random bytes, and the whole synced. 0, or the first write's failure.
*/
static bipolar host_install_lay(bipolar handle, p64 bytes, b32 sector, storage_format_partition address_to parts, p8 address_to random)
{
        storage_format_identity identity;
        bipolar failed;

        memory_copy(identity.uuid, random, 16);
        memory_copy(identity.hash_seed, random + 16, 16);
        memory_copy(parts[0].unique, random + 48, 16);
        memory_copy(parts[1].unique, random + 64, 16);

        //      Version 4, as the filesystems write theirs (byte 6) and as a
        //      GPT GUID does (byte 7).
        {
                const struct
                {
                        p8 address_to bytes;
                        positive version_at;
                } random_ids[] = {{identity.uuid, 6},   {identity.hash_seed, 6},
                                  {random + 32, 7},     {parts[0].unique, 7},
                                  {parts[1].unique, 7}};

                for (positive at = 0; at < array_count(random_ids); at++)
                        tools_uuid_version(random_ids[at].bytes, random_ids[at].version_at, 4);
        }
        identity.time = (p32)(system_clock_ns(HOST_CLOCK_REALTIME) / 1000000000);
        identity.label = "moonwater";

        memory_copy(parts[0].type, storage_gpt_system_type, 16);
        memory_copy(parts[1].type, storage_gpt_linux_type, 16);
        parts[0].name = HOST_SYSTEM_NAME;
        parts[1].name = HOST_DATA_NAME;

        failed = storage_format_zero(handle, 0, HOST_ALIGN_BYTES);
        if (!failed)
                failed = storage_format_zero(handle, bytes - HOST_ALIGN_BYTES,
                                             HOST_ALIGN_BYTES);
        if (!failed)
                failed = storage_format_gpt(handle, bytes / (p64)sector, (p32)sector,
                                            random + 32,
                                            parts, 2);
        if (!failed)
                failed = storage_format_fat32(
                    handle, parts[0].first * (p64)sector,
                    (parts[0].last - parts[0].first + 1) * (p64)sector, (p32)sector,
                    parts[0].first, address_of identity);
        if (!failed)
                failed = storage_format_zero(handle, parts[1].first * (p64)sector,
                                             HOST_ALIGN_BYTES);
        if (!failed)
                failed = storage_format_ext4(
                    handle, parts[1].first * (p64)sector,
                    (parts[1].last - parts[1].first + 1) * (p64)sector,
                    address_of identity);
        if (!failed)
                failed = system_call_1(syscall(fsync), (positive)handle);
        return failed;
}

/*
        Refusals first, then one question, then the writes.

        The boot image has to be on some other disk -- it is what gets
        copied, and a disk holding the only copy is the one thing that must
        not be erased. A disk with anything mounted is refused by the kernel
        itself: the exclusive open fails. Removable media is refused unless
        asked for, because the stick being installed from is removable too.
*/
/* An install made from a session that keeps another disk's data (a stick installed with setup install): the drive is written and
   the session goes on keeping the stick's, where it was refused. moonwater install sets it. */
static bool host_install_beside;

static b32 host_install_locked(string_address asked, bool removable, bool said_yes)
{
        string_address name = host_disk_word(asked);
        p8 sysfs[HOST_PATH_ROOM];
        p8 path[HOST_PATH_ROOM];
        p8 text[128];
        p8 device[HOST_NAME_ROOM + 8];
        p8 running[HOST_BUILD_ROOM];
        p8 answer[HOST_NAME_ROOM];
        p8 random[80];
        p8 kept[HOST_NAME_ROOM];
        host_medium_search search;
        host_install target;
        storage_format_partition parts[2];
        p64 bytes = 0;
        b32 sector = 0;
        bipolar handle;
        bipolar failed;

        if (!host_name_valid(name) ||
            !host_join(sysfs, sizeof(sysfs), "/sys/class/block/", name) ||
            system_access_at(AT_FDCWD, sysfs, 0) < 0)
                return host_refuse("there is no disk called %s\n", name);

        if (host_join(path, sizeof(path), sysfs, "/partition") &&
            system_access_at(AT_FDCWD, path, 0) >= 0)
                return host_refuse("%s is a partition; name the whole disk\n", name);

        if (host_block_flag(sysfs, "/ro"))
                return host_refuse("%s is read-only\n", name);

        if (!removable && host_block_flag(sysfs, "/removable"))
                return host_refuse("%s is removable media; moonwater setup "
                                   "install DISK removable takes it\n", name);

        /*      Keeping a second disk's data over the first's is what host_take
                refuses, and it is asked now and not after the erase. */
        if (!host_install_beside && host_session_disk(kept, sizeof(kept)))
                return host_refuse("this session already keeps %s; installing on "
                                   "another disk starts from a live session\n", kept);

        if (!host_running_build(running, sizeof(running)))
                return host_refuse("moonwater cannot read its own build\n");

        /*      Read before anything is mounted to look for the image: an
                install takes this session's settings, which is how what was
                set on a live stick is still set on the disk it installed. */
        host_settings carry;
        bool carried;
        positive splash = 0;

        host_settings_session(address_of carry);
        carry.generation++;
        system_random_fill(carry.medium, sizeof(carry.medium), 0);

        if (!host_medium_find(address_of search, running, name))
        {
                if (host_medium_find(address_of search, running, null))
                {
                        host_unmount(HOST_MEDIUM);
                        return host_refuse("the only copy of this session's image is "
                                           "on %s itself\n", name);
                }

                return host_refuse("this session's image is on no disk here, so "
                                   "there is nothing to install on %s\n", name);
        }

        if (!host_medium_whole())
        {
                host_unmount(HOST_MEDIUM);
                return 1;
        }

        host_join(device, sizeof(device), "/dev/", name);

        /*
                Pin the destructive target before describing it or asking for
                confirmation, and keep that exact open description through the
                final write.  Closing a read-only probe here and reopening
                /dev/<name> after the prompt leaves a hot-unplug/name-reuse
                window in which the operator confirms one disk and a different
                disk receives the partition table.
        */
        handle = system_open_at(AT_FDCWD, device,
                                FILE_READ_WRITE | FILE_EXCLUSIVE | O_CLOEXEC);
        if (handle < 0)
        {
                host_unmount(HOST_MEDIUM);
                return handle == -ERROR_BUSY
                           ? host_refuse("%s is in use: something on it is mounted\n",
                                         name)
                           : host_fail(device, handle);
        }

        failed = system_control(handle, HOST_BLKGETSIZE64, address_of bytes);
        if (failed >= 0)
                failed = system_control(handle, HOST_BLKSSZGET,
                                        address_of sector);

        //      Placed now, before the rescan and the prompt, so a disk that
        //      cannot be given both partitions is turned away on the open handle.
        memory_zero(parts, sizeof(parts));
        if (failed < 0 || !host_install_place(bytes, sector, parts))
        {
                system_close(handle);
                host_unmount(HOST_MEDIUM);
                //      Too small is one refusal; a size and sector the GPT has no
                //      layout for is another, and is not said to be too small.
                return failed < 0 ? host_fail(device, failed)
                       : bytes < HOST_SMALLEST
                           ? host_refuse("%s is smaller than the 2 GiB an install needs\n", name)
                           : host_refuse("%s has no layout an install can be given\n", name);
        }

        /*      The partition table is read back from the kernel once it is
                written, which a disk that cannot hold partitions (a loop device
                without them, a device-mapper or RAID one) never does: found
                out now, on the open disk, and not after the erase. */
        failed = host_rescan(handle);
        if (failed < 0)
        {
                system_close(handle);
                host_unmount(HOST_MEDIUM);
                return failed == -ERROR_BUSY
                           ? host_refuse("%s is in use: something on it is mounted\n", name)
                       : failed == -EINVAL || failed == -ENOTTY
                           ? host_refuse("%s cannot hold partitions\n", name)
                           : host_fail(device, failed);
        }

        if (!host_data_fits((parts[1].last - parts[1].first + 1) * (p64)sector, name))
        {
                system_close(handle);
                host_unmount(HOST_MEDIUM);
                return 1;
        }

        if (!host_join(path, sizeof(path), sysfs, "/device/model") ||
            host_read_kernel(path, text, sizeof(text)) <= 0)
                string_copy(text, "a disk");
        host_plain_line(text);

        host_say(log, host_label "Installing erases everything on %s: %s, %p GiB.\n",
                 name, text, bytes >> 30);

        //      Said yes to, in the words of the command: nothing is asked.
        //      Otherwise the name is typed, which is how a person at a
        //      terminal is sure of which disk this is.
        //      No partition is mounted while the answer is waited for: a
        //      command ended at the question (Ctrl-C) left the medium mounted
        //      for the rest of the session. The image is found again after
        //      the answer, and looked at again, as it was the first time.
        if (!said_yes)
        {
                host_unmount(HOST_MEDIUM);
                host_say(log, host_label "Type %s to go on: ", name);

                if (host_read_line(answer, sizeof(answer)) < 0 ||
                    !string_equals(answer, name))
                {
                        system_close(handle);
                        host_say(log, host_label "nothing written\n");
                        return 1;
                }
                if (!host_medium_find(address_of search, running, name))
                {
                        system_close(handle);
                        return host_refuse("this session's image is no longer on a disk "
                                           "here, so nothing was written to %s\n", name);
                }
                if (!host_medium_whole())
                {
                        system_close(handle);
                        host_unmount(HOST_MEDIUM);
                        return 1;
                }
        }

        failed = system_random_fill(random, sizeof(random), 0);
        if (failed)
        {
                system_close(handle);
                host_unmount(HOST_MEDIUM);
                return host_fail(device, failed);
        }

        if (!failed)
        {
                host_say(log, host_label "partitioning and formatting %s\n", name);
                failed = host_install_lay(handle, bytes, sector, parts, random);
        }

        //      Written part way, the disk is not left holding half of an install,
        //      and what became of it is said as well as the write's own
        //      failure, in host_install_abandon's words: the table taken off,
        //      or that it could not be. Only the second used to be said, so a
        //      disk the install had erased and cleared again was reported as
        //      an I/O error and nothing about the disk.
        if (failed)
        {
                if (!host_install_clear(handle))
                        host_say(log_error, host_label "%s could not be cleared: the install "
                                                       "did not finish, and its partitions are "
                                                       "not to be trusted\n", name);
                else
                        host_say(log_error, host_label "%s is left without a partition table: "
                                                       "the install did not finish\n", name);
        }
        else
                failed = host_rescan(handle);

        system_close(handle);

        memory_zero(address_of target, sizeof(target));
        string_copy(target.disk, name);

        if (failed || !host_partitions_wait(address_of target, parts))
        {
                host_unmount(HOST_MEDIUM);
                return failed ? host_fail(device, failed)
                              : host_refuse("the new partitions on %s did not appear\n",
                                            name);
        }

        /*
                The data goes first and the image last. The image is what makes
                a disk an install of this build, which boot attaches without
                asking: with it placed before the copy, power lost during the
                copy left a disk that boots as complete and keeps what part of
                /root had arrived. A disk cut before the image has partitions
                and none, and is asked about.
        */
        failed = host_mount(target.data, HOST_DATA, "ext4", HOST_DATA_MOUNT);
        if (failed < 0)
        {
                b32 said = host_fail(target.data, failed);

                host_install_abandon(address_of target);
                return said;
        }

        /*
                What this session already has goes along, bowls and all. Each
                directory is copied under its own name into the empty data
                partition, which host_attach then binds: this cp copies nothing
                for `cp -a dir/. existing`, and exits 1 without a word.
        */
        for (positive at = 0; at < array_count(host_kept); at++)
        {
                string_address argv[] = {"/bin/cp", "-a", host_kept[at].path,
                                         HOST_DATA "/", null};

                if (system_access_at(AT_FDCWD, host_kept[at].path, 0) < 0)
                        continue;

                host_say(log, host_label "copying %s\n", host_kept[at].path);

                if (host_run(argv))
                {
                        host_install_abandon(address_of target);
                        return 1;
                }
        }

        if (!host_unmount(HOST_DATA))
        {
                b32 said = host_refuse("the data partition on %s could not be let go "
                                       "of\n", name);

                host_install_abandon(address_of target);
                return said;
        }

        failed = host_mount(target.system, HOST_SYSTEM, "vfat", HOST_WRITABLE);
        if (failed < 0)
        {
                b32 said = host_fail(target.system, failed);

                host_install_abandon(address_of target);
                return said;
        }

        host_say(log, host_label "writing this build to %s, from %s\n",
                 target.system, search.name);

        //      The picture first, while both partitions are mounted: the image is the last thing an install writes.
        host_splash_install(HOST_MEDIUM, HOST_SYSTEM, name, address_of splash);
        failed = host_join(path, sizeof(path), HOST_MEDIUM, HOST_IMAGE)
                     ? host_place_image(target.system, HOST_SYSTEM, path, running,
                                        address_of carry, address_of carried, null)
                     : host_refuse("%s: path too long\n", HOST_MEDIUM);
        host_unmount(HOST_SYSTEM);
        host_unmount(HOST_MEDIUM);
        if (failed)
        {
                host_install_abandon(address_of target);
                return 1;
        }
        host_splash_installed(target.system, splash);

        host_say(log, carried ? host_label "%s starts with this session's settings\n"
                              : host_label "this build's image has no place for "
                                           "settings, so %s starts with the defaults\n",
                 name);

        if (!host_install_beside && host_take(address_of target, false))
                return 1;

        host_say(log, host_label "Moonwater is installed on %s. Power off and take "
                                 "the stick out, and the machine starts from %s.\n",
                 name, name);
        return 0;
}

/*
        Before the machine stops, every filesystem on a disk remounted
        read-only.

        sync puts the data on the disk, but an ext4 left mounted keeps a
        journal the next mount has to replay, and until one does, e2fsck calls
        the filesystem unclean. Remounting read-only writes the journal out
        and marks it clean, which an installed machine wants every time it
        powers off. Best effort, latest mount first: a filesystem something
        still holds open for writing refuses, and keeps what sync gave it.
*/
fn host_quiesce(void)
{
        storage_mount_table table;
        string_address done[32];
        positive done_count = 0;

        if (!storage_mount_table_load(address_of table, null))
                return;

        //      A device bound in several places is one superblock: once is all.
        for (positive at = table.count; at-- > 0;)
        {
                storage_mount address_to mount = table.entry + at;
                bool seen = false;

                if (!string_has_prefix(mount->source, "/dev/"))
                        continue;

                for (positive look = 0; look < done_count; look++)
                        if (string_equals(done[look], mount->device))
                                seen = true;
                if (seen)
                        continue;

                if (done_count < array_count(done))
                        done[done_count++] = mount->device;

                system_mount(0, mount->target, 0, MS_REMOUNT | MS_RDONLY, 0);
        }

        storage_mount_table_release(address_of table);
}

static b32 host_install_disk(string_address asked, bool removable, bool said_yes)
{
        b32 failed;

        if (!host_acquire())
                return 1;

        failed = host_install_locked(asked, removable, said_yes);
        host_release();
        return failed;
}

