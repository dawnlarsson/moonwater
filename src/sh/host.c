/*
        Moonwater on a disk.

        The system is one EFI image -- the kernel with the userspace built into
        it -- so installing Moonwater copies no root filesystem. It lays a GPT
        on a disk with two partitions: a FAT32 system partition holding that
        image where firmware looks for one, and an ext4 holding what a live
        session loses at power off, the bowl roots, /root and /home. Updating
        is copying a newer image over the old one; the data stays where it is.

        Which image is which comes from the image. It carries the kernel's
        version string -- release, builder, and the count and date of its
        link, the words uname and /proc/version give back -- in the x86 boot
        header, and in the kernel's own banner on arm64 and riscv64, whose
        image is the kernel uncompressed. So a running system finds its own
        image on the stick it started from by reading images, and tells an
        installed disk carrying this very build from one carrying another.

        `moonwater boot` is a service of init's, and settles which of three a
        machine is before any shell starts:

          live  nothing installed: nothing is kept, as before
          disk  an install carrying this build: its data is mounted over the
                bowl roots, /root and /home
          ask   an install carrying another build, which is what a stick with
                a newer Moonwater looks like to an installed machine

        PID 1 cannot ask anything. The canvas console only prints, and the
        keyboard belongs to the terminal the compositor starts, so the verdict
        is written under /run/moonwater and that terminal asks before its
        shell does: use the disk's data with this build, update the disk to
        this build first, or leave the disk alone. `moonwater setup use`,
        `update` and `live` answer the same question from any shell.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater

        www.dawning.dev
*/

#define host_label TERM_BOLD "[Moonwater]" TERM_RESET " "

#define HOST_STATE "/run/moonwater"
#define HOST_VERDICT HOST_STATE "/verdict"
#define HOST_QUESTION HOST_STATE "/question"
#define HOST_QUESTION_TAKEN HOST_STATE "/question.taken"
#define HOST_HINT HOST_STATE "/hint"
#define HOST_HINT_TAKEN HOST_STATE "/hint.taken"
#define HOST_DATA HOST_STATE "/data"
#define HOST_SYSTEM HOST_STATE "/system"
#define HOST_MEDIUM HOST_STATE "/medium"
#define HOST_LOOK HOST_STATE "/look"
#define HOST_MACHINE_SCRIPT "/root/main.moonwater.sh"
#define HOST_MACHINE_BUILTIN "builtin"
#define HOST_MACHINE_RUNTIME HOST_STATE "/machine.sh"
#define HOST_MACHINE_DIRTY HOST_STATE "/machine.dirty"
#define HOST_MACHINE_SOURCED HOST_STATE "/machine.sourced"

#include "../moonwater/moonwater.c"

//      What this file says as log and log_error is said through plain_out and
//      plain_err (file.c): colour only to a terminal, names shown and not obeyed.
#define log plain_out
#define log_error plain_err

// The start of a line: the label, or the two blanks a page indents by.
static fn host_prefix(bool label)
{
        string_format(log, label ? host_label : "  ");
}

#define HOST_SYSTEM_NAME "moonwater-boot"
#define HOST_DATA_NAME "moonwater-data"
/* The removable-media name the UEFI specification gives each machine's loader:
   firmware on a disk with no boot entry of its own starts this file. */
#if X64
#define HOST_IMAGE "/EFI/BOOT/BOOTX64.EFI"
#define HOST_IMAGE_NEXT "/EFI/BOOT/BOOTX64.NEW"
#elif ARM64
#define HOST_IMAGE "/EFI/BOOT/BOOTAA64.EFI"
#define HOST_IMAGE_NEXT "/EFI/BOOT/BOOTAA64.NEW"
#elif RISCV64
#define HOST_IMAGE "/EFI/BOOT/BOOTRISCV64.EFI"
#define HOST_IMAGE_NEXT "/EFI/BOOT/BOOTRISCV64.NEW"
#else
#error "no removable-media loader name for this architecture"
#endif
#define HOST_KEPT_DIRECTORY "/EFI/moonwater"
#define HOST_KEPT_IMAGE HOST_KEPT_DIRECTORY "/previous.efi"
#define HOST_KEPT_IMAGE_NEXT HOST_KEPT_DIRECTORY "/previous.new"

#define HOST_NAME_ROOM 64
#define HOST_PATH_ROOM 256
#define HOST_BUILD_ROOM 256
#define HOST_INSTALLS 8
#define HOST_SYSTEM_BYTES ((p64)512 << 20)
#define HOST_ALIGN_BYTES ((p64)1 << 20)
#define HOST_SMALLEST ((p64)2 << 30)
#define HOST_READ_ONLY (MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOEXEC)
#define HOST_WRITABLE (MS_NOSUID | MS_NODEV | MS_NOEXEC)
/* The data partition runs programs, and a bowl's sudo and su need their set-user
   bits, so it is mounted without device files only. The locked tier has no
   bowl and no one to run su, and keeps the set-user bits out as well. */
#if MOONWATER_STRICT_HOST >= STRICT_LOCKED
#define HOST_DATA_MOUNT (MS_NODEV | MS_NOSUID)
#else
#define HOST_DATA_MOUNT MS_NODEV
#endif
#define HOST_LOCK HOST_STATE "/lock"
#define HOST_LOCK_WAIT_NS ((p64)30000000000)
#define HOST_LOCK_POLL_NS ((p64)10000000)
/* How deep the install measures what it will copy before it refuses to guess. */
#define HOST_MEASURE_DEPTH 128

/*
        How long boot looks for disks. NVMe namespaces and USB disks arrive
        after init starts -- usb-storage waits a second before it scans -- so
        a first look finds nothing on a machine that has everything. Boot
        stops as soon as an install turns up, and otherwise once a second has
        passed and nothing is still on its way, and never later than ten.
*/
#define HOST_SETTLE_FLOOR_NS ((p64)1000000000)
#define HOST_SETTLE_MOST_NS ((p64)10000000000)
#define HOST_POLL_NS ((p64)100000000)
#define HOST_VERDICT_WAIT_NS ((p64)15000000000)
#define HOST_LOOKING_NS ((p64)700000000)
/* How long names just resolved are still taken to be the disks' own. */
#define HOST_FRESH_NS ((p64)2000000000)
/*      How often a terminal looks for boot's verdict where no watch on the
        state directory can be made; with one, it waits for the publish itself. */
#define HOST_VERDICT_POLL_NS ((p64)5000000)
#define HOST_MEDIUM_FLOOR_NS ((p64)5000000000)

#define HOST_CLOCK_REALTIME 0
#define HOST_CLOCK_BOOTTIME 7
#define HOST_BLKRRPART 0x125f
#define HOST_BLKFLSBUF 0x1261
#define HOST_FADVISE_DONTNEED 4
#define HOST_BLKSSZGET 0x1268
#define HOST_BLKGETSIZE64 0x80081272u

/* What an install keeps, and each directory's mode where it is made new. */
static const struct
{
        string_address path;
        positive mode;
} host_kept[] = {
    {BOWL_ROOT_DIRECTORY, 0755},
    {"/root", 0700},
    {"/home", 0755},
};

typedef struct
{
        p8 disk[HOST_NAME_ROOM];
        p8 system[HOST_NAME_ROOM];
        p8 data[HOST_NAME_ROOM];
        p8 system_partuuid[STORAGE_PARTUUID_ROOM];
        p8 data_partuuid[STORAGE_PARTUUID_ROOM];
        p8 build[HOST_BUILD_ROOM];
        bool readable;
        //      When the names above were last resolved, on the boot clock.
        p64 resolved;
} host_install;

typedef struct
{
        host_install found[HOST_INSTALLS];
        positive count;
        positive dropped;
} host_census;

// Settings, defined further down with the commands that change them.
typedef struct spark_settings host_settings;

static fn host_settings_empty(host_settings address_to settings);
static b32 host_settings_image(string_address path, host_settings address_to into);
static bipolar host_settings_stamp(string_address path, host_settings address_to settings);
static p64 host_settings_section(bipolar handle);
static bool host_image_whole(string_address path);
static bool host_settings_slots(bipolar handle, host_settings address_to slots,
                                p64 address_to offset);
static bool host_settings_booted(host_settings address_to into);
static bool host_settings_kept(host_settings address_to into);
static bool host_settings_session(host_settings address_to settings);
static bool host_settings_keep(host_settings address_to settings);
static bool host_settings_install(host_install address_to install,
                                  host_settings address_to into);
static fn host_events_boot(host_settings address_to settings);
static fn host_bind_apply(host_settings address_to settings);
static b32 host_bind(string_address address_to arguments, positive count);
static b32 host_usage(void);
static b32 host_setting_group(string_address address_to words, positive count);
static positive radio_display(p8 address_to into, positive room, p8 address_to name,
                              positive length);
static p16 host_machine_hook_line(p8 hook);
static p16 host_machine_event_line(unsigned int event);
static string_address host_machine_where(void);
static fn host_machine_refused(string_address name, p16 line);
static bool host_machine_stop(void);
static bool host_init_list_runs(void);

//      builtin.c's, which stops the machine the way reboot does; it is
//      included after this file. The number is the one it defines.
fn shell_stop(writer write, positive command);
#ifndef REBOOT_RESTART
#define REBOOT_RESTART 0x01234567
#endif
static b32 host_machine_run(void);
static fn tune_restore(void);
static fn radio_restore(void);
static fn radio_recover(void);
static fn usb_restore(void);
static fn usb_recover(void);
static fn locale_restore(void);
static fn dns_restore(void);
static fn security_publish(host_settings address_to settings);
//      Set by the machine process alone, in moonwater.c: only it asks the time.
static bool host_machine_self;
static fn name_restore(void);
static fn locale_recover(void);
static unsigned int locale_wake_ms(unsigned int most);

// A line to the writer, then the buffer out: the two calls every report ends on.
#define host_say(...) \
        do \
        { \
                string_format(__VA_ARGS__); \
                log_flush(); \
        } while (0)

// The word the command was given, which its refusals and its usage name.
static string_address host_verb;

/*
        What a command that cannot be taken back is said yes to. At a terminal
        a person is asked; anywhere else nobody can be, and a script that means
        it says yes as the last word. One that has neither is refused where it
        stands, with the line that would have been right, and never reads: an
        input that is /dev/null, a pipe nobody writes to or the console a
        machine script was started on is no answer.
*/
#define HOST_YES "yes"
#define host_asks() stream_is_terminal(0)

// The root check the commands that change the machine open with.
#define host_need_root() \
        return_if(!bowl_is_root(), host_refuse("moonwater %s needs root\n", host_verb))

// A refusal, in the voice of the rest: the label, the words and what they name.
#define host_refuse(...) \
        ({ \
                string_format(log_error, host_label); \
                host_say(log_error, __VA_ARGS__); \
                (b32)1; \
        })

static b32 host_fail(string_address what, bipolar error)
{
        host_say(log_error, host_label "%s: %s\n", what, file_reason(error));
        return 1;
}

/* Both halves must fit, or into is truncated and the answer says so. */
#define host_join string_join

static bool host_starts(string_address text, string_address prefix)
{
        return !string_compare_max(text, prefix, string_length(prefix));
}

/* The words a switch is set by: 1 for on, 0 for off, -1 for anything else. */
static bipolar host_onoff(string_address word)
{
        return string_equals(word, "on") ? 1 : string_equals(word, "off") ? 0 : -1;
}

static fn host_pause(p64 nanoseconds)
{
        process_nap_ns(nanoseconds);
}

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

static bipolar host_verdict_watch(void)
{
        bipolar notify;

        host_state_ready();
        notify = system_call_1(syscall(inotify_init1), O_CLOEXEC | O_NONBLOCK);
        if (notify < 0)
                return -1;
        if (system_call_3(syscall(inotify_add_watch), (positive)notify,
                          (positive)HOST_STATE, HOST_VERDICT_EVENTS) < 0)
        {
                system_close((positive)notify);
                return -1;
        }
        return notify;
}

static fn host_verdict_unwatch(bipolar notify)
{
        if (notify >= 0)
                system_close((positive)notify);
}

/* Until the directory is heard of, or the boot clock reads until (0: for
   ever); fallback is the poll used where there is no watch. What is queued
   is read and dropped, so the next wait starts on the next event. */
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
        if (descriptor_wait_readable(notify, limit, null) > 0)
        {
                p8 drained[256];

                while (system_read_once((positive)notify, drained, sizeof(drained)) > 0)
                        ;
        }
}

/* The bytes of a word a file held, its newline and the blanks after it taken off
   and the end made: how many are left. */
static bipolar host_word_end(p8 address_to into, bipolar got)
{
        while (got > 0 && (into[got - 1] == '\n' || into[got - 1] == '\r' ||
                           into[got - 1] == ' '))
                got--;

        into[got] = end;
        return got;
}

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

/* Persistent state is a regular file owned by this program. Opening it
   nonblocking first means a FIFO planted at a state name cannot stop a root
   command before it has a chance to reject the object; O_NOFOLLOW gives a
   planted final symlink the same refusal as the writers. */
static bipolar host_read_state(string_address path, p8 address_to into,
                               positive capacity)
{
        return file_slurp_regular_at(AT_FDCWD, path, into, capacity, O_NOFOLLOW);
}

/* A state word as host_read_state reads it, without its newline or the blanks
   after it; negative, and empty, when the file is not there or not a plain one.
   A FIFO or a link planted at the name on the data partition is no word, and
   cannot stop the command. */
static bipolar host_read_word(string_address path, p8 address_to into, positive room)
{
        bipolar got = host_read_state(path, into, room);

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

/* Bytes over a state file, made if it is not there. A choice that has to
   survive the power going is synced: written beside itself under a name of
   its own, and renamed over the old one, so a crash or a signal part way (the
   saved wifi list, passwords and all, was truncated first and written second)
   leaves the file as it was or as it is to be, and nothing half of each, and
   two writers at once leave one whole. A /run file that only says what this
   session is doing does not need to be. */
static bipolar host_write_file(string_address path, p8 address_to bytes,
                               positive length, positive mode, bool sync)
{
        bipolar handle;
        bipolar failed;

        if (sync)
                return file_publish_with(path, bytes, length, mode, true,
                                         FILE_STAGED_EXACT_MODE);

        handle = host_open_state(AT_FDCWD, path, mode, FILE_WRITE);
        if (handle < 0)
                return handle;
        failed = storage_format_write(handle, bytes, length, 0);
        system_close(handle);
        return failed;
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

        while (used && (into[used - 1] == '\r' || into[used - 1] == ' '))
                used--;

        into[used] = end;
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
        exists says nothing about what the name is. */
static fn host_state_directory(string_address path, positive mode)
{
        bipolar opened;

        system_make_directory_at(AT_FDCWD, path, mode);
        opened = system_open_at(AT_FDCWD, path,
                                FILE_READ | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (opened >= 0)
        {
                system_close(opened);
                return;
        }

        if (system_remove_at(AT_FDCWD, path, 0) >= 0)
                system_make_directory_at(AT_FDCWD, path, mode);
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
static fn host_verdict_set(string_address kind, string_address disk)
{
        p8 line[HOST_NAME_ROOM + 16];
        p8 text[HOST_NAME_ROOM + 16];
        bipolar failed;

        if (!host_join(line, sizeof(line), kind, disk) ||
            !host_join(text, sizeof(text), line, "\n"))
                return;

        failed = file_publish_bytes(HOST_VERDICT, text, string_length(text), 0644,
                                    false);
        if (failed < 0)
                host_fail(HOST_VERDICT, failed);
}

#define RADIO_LOCK_EX 2
#define RADIO_LOCK_NB 4
#define RADIO_LOCK_UN 8

static bipolar host_lock(string_address path, bool wait)
{
        bipolar handle;
        bipolar locked;

        host_state_ready();
        // Not through a link: the lock is made where it is named.
        handle = system_open_at_mode(AT_FDCWD, path,
                                     FILE_READ_WRITE | FILE_CREATE |
                                         O_NOFOLLOW | O_CLOEXEC,
                                     0600);
        if (handle < 0)
                return handle;

        locked = system_call_2(syscall(flock), (positive)handle,
                               wait ? RADIO_LOCK_EX
                                    : (RADIO_LOCK_EX | RADIO_LOCK_NB));
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
        bipolar handle;
        bipolar asked;

        host_state_ready();
        // Not through a link: the lock is made where it is named.
        handle = system_open_at_mode(AT_FDCWD, path,
                                     FILE_READ_WRITE | FILE_CREATE | O_NOFOLLOW |
                                         O_CLOEXEC,
                                     0600);
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
        if (system_fork())
                system_call_1(syscall(exit_group), 0);

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
        once -- six `moonwater bind init add` in a loop -- each read the same
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
        A holder lets go by closing the lock file (after the flock that frees
        it, and the kernel closes it when the holder exits), and a close after
        a write or a read is an event on a watch of the file. So a command that
        cannot have the lock waits on that watch for the time it has left, and
        not on a timer that wakes every 10 ms to ask again. The watch stands
        before each try, so a release that lands between a try and the wait is
        already queued and the wait returns at once. Where no watch can be
        made, the wait is the poll it was.
*/
#define HOST_LOCK_EVENTS (0x010 | 0x008 | 0x02000000)

static bipolar host_lock_watch(void)
{
        bipolar notify = system_call_1(syscall(inotify_init1), O_CLOEXEC | O_NONBLOCK);

        if (notify < 0)
                return -1;
        if (system_call_3(syscall(inotify_add_watch), (positive)notify,
                          (positive)HOST_LOCK, HOST_LOCK_EVENTS) < 0)
        {
                system_close((positive)notify);
                return -1;
        }
        return notify;
}

static bool host_acquire(void)
{
        bipolar handle;
        bipolar notify = -1;
        bipolar locked;
        p64 started;

        if (host_held_count)
        {
                host_held_count++;
                return true;
        }

        host_state_ready();
        // Not through a link: the lock is made where it is named. A lock that
        // cannot be opened at all has nobody to exclude, as it never had.
        handle = system_open_at_mode(AT_FDCWD, HOST_LOCK,
                                     FILE_READ_WRITE | FILE_CREATE |
                                         O_NOFOLLOW | O_CLOEXEC,
                                     0600);
        if (handle < 0)
        {
                host_held = handle;
                host_held_count = 1;
                return true;
        }

        // The one descriptor is kept for the whole wait: closing it would be
        // an event on the watch, and the wait would hear its own try.
        started = system_clock_ns(HOST_CLOCK_BOOTTIME);
        for (;;)
        {
                locked = system_call_2(syscall(flock), (positive)handle,
                                       RADIO_LOCK_EX | RADIO_LOCK_NB);
                if (locked != -EAGAIN)
                        break;

                if (notify < 0 && (notify = host_lock_watch()) >= 0)
                        continue;

                p64 waited = system_clock_ns(HOST_CLOCK_BOOTTIME) - started;

                if (waited >= HOST_LOCK_WAIT_NS)
                {
                        if (notify >= 0)
                                system_close((positive)notify);
                        system_close((positive)handle);
                        host_say(log_error, host_label "another moonwater command is changing "
                                                       "this machine's settings or disks; "
                                                       "try again in a moment\n");
                        return false;
                }

                if (notify < 0)
                {
                        host_pause(HOST_LOCK_POLL_NS);
                        continue;
                }

                p64 left = HOST_LOCK_WAIT_NS - waited;
                timespec span = {left / 1000000000, left % 1000000000};

                if (descriptor_wait_readable(notify, address_of span, null) > 0)
                {
                        p8 drained[256];

                        while (system_read_once((positive)notify, drained, sizeof(drained)) > 0)
                                ;
                }
        }

        if (notify >= 0)
                system_close((positive)notify);
        if (locked < 0)
        {
                // Not held and not refused: as host_lock's error, the lock is
                // taken as nobody's to exclude.
                system_close((positive)handle);
                handle = locked;
        }
        host_held = handle;
        host_held_count = 1;
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

        for (positive at = 0; at < census->count; at++)
                if (string_equals(census->found[at].disk, parent))
                        install = census->found + at;

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

static host_install address_to host_census_find(host_census address_to census,
                                                string_address disk)
{
        for (positive at = 0; at < census->count; at++)
                if (string_equals(census->found[at].disk, disk))
                        return census->found + at;

        return null;
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

#if X64
/* The version string in an x86 boot image's setup header, if it has one. */
static bool host_image_version(bipolar handle, p8 address_to into,
                               positive room)
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
#else
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
static bool host_image_version(bipolar handle, p8 address_to into,
                               positive room)
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
#endif

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

        target = system_open_at_mode(AT_FDCWD, to, FILE_WRITE | O_CLOEXEC, 0644);
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
                     file_look_code(AT_FDCWD, left, 0, address_of first_facts) >= 0 &&
                     file_look_code(AT_FDCWD, right, 0, address_of second_facts) >= 0 &&
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

/* Whether path is the image at source with the slots it is to be stamped with. */
static bool host_image_is(string_address path, string_address source,
                          host_settings address_to carry)
{
        host_settings slots[2];
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

        handle = system_open_at(AT_FDCWD, path, FILE_READ | O_CLOEXEC);
        if (handle < 0)
                return false;

        alike = host_settings_slots(handle, slots, address_of offset) &&
                !memory_compare(slots, carry, SPARK_SETTINGS_SLOT) &&
                !memory_compare(slots + 1, carry, SPARK_SETTINGS_SLOT);
        system_close(handle);
        return alike;
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
}

/*
        The image the partition keeps, as the disk has it: the partition is
        let go of, its buffers dropped, and it is mounted again with the file's
        pages dropped, so what is compared is what a firmware would read and
        not memory. The partition is left unmounted.
*/
static bool host_image_on_disk(string_address name, string_address system,
                               string_address source, host_settings address_to carry)
{
        p8 device[HOST_NAME_ROOM + 8];
        p8 path[HOST_PATH_ROOM];
        bipolar handle;
        bool alike = false;

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

        if (host_join(path, sizeof(path), system, HOST_IMAGE))
        {
                handle = system_open_at(AT_FDCWD, path, FILE_READ | O_CLOEXEC);
                if (handle >= 0)
                {
                        system_call_4(syscall(fadvise64), (positive)handle, 0, 0,
                                      HOST_FADVISE_DONTNEED);
                        system_close(handle);
                }

                alike = host_image_is(path, source, carry);
        }

        host_unmount(system);
        return alike;
}

static b32 host_place_written(string_address system, string_address running,
                              host_settings address_to carry, bool address_to carried)
{
        p8 image[HOST_PATH_ROOM];
        p8 next[HOST_PATH_ROOM];
        p8 kept[HOST_PATH_ROOM];
        p8 source[HOST_PATH_ROOM];
        p8 build[HOST_BUILD_ROOM];
        bipolar failed;

        if (!host_join(image, sizeof(image), system, "/EFI") ||
            (system_make_directory_at(AT_FDCWD, image, 0755),
             !host_join(image, sizeof(image), system, "/EFI/BOOT")) ||
            (system_make_directory_at(AT_FDCWD, image, 0755),
             !host_join(image, sizeof(image), system, HOST_KEPT_DIRECTORY)) ||
            (system_make_directory_at(AT_FDCWD, image, 0755),
             !host_join(image, sizeof(image), system, HOST_IMAGE)) ||
            !host_join(source, sizeof(source), HOST_MEDIUM, HOST_IMAGE))
                return host_refuse("%s: path too long\n", system);

        //      What an earlier write that was cut off left beside the images is
        //      not trusted to be a file, and is not opened: it is removed.
        host_image_discard(system);

        if (system_access_at(AT_FDCWD, image, 0) >= 0 &&
            host_join(next, sizeof(next), system, HOST_KEPT_IMAGE_NEXT) &&
            host_join(kept, sizeof(kept), system, HOST_KEPT_IMAGE))
        {
                if (host_copy_file(image, next))
                        return 1;

                if (!host_files_alike(image, next, 0, 0))
                        return host_refuse("%s could not be kept whole\n", image);

                failed = system_rename_at(AT_FDCWD, next, AT_FDCWD, kept, 0);
                return_if(failed < 0, host_fail(kept, failed));
        }

        if (!host_join(next, sizeof(next), system, HOST_IMAGE_NEXT) ||
            host_copy_file(source, next))
                return 1;

        //      Both slots of the new image say what the disk is to keep, before
        //      the rename makes it the one that starts.
        failed = carry ? host_settings_stamp(next, carry) : 0;
        return_if(failed < 0, host_fail(next, failed));

        *carried = carry && !failed;
        if (carry && failed)
                carry = null;

        //      And the file that is renamed is the one that was copied, in every
        //      byte but the slots, and the slots are the ones stamped.
        if (!host_image_is(next, source, carry))
                return host_refuse("%s is not the image it was copied from\n", next);

        if (!host_image_build(next, build, sizeof(build)) ||
            !string_equals(build, running))
                return host_refuse("%s did not read back as this build\n", next);

        failed = system_rename_at(AT_FDCWD, next, AT_FDCWD, image, 0);
        return_if(failed < 0, host_fail(image, failed));

        {
                bipolar directory = system_open_at(AT_FDCWD, system,
                                                   FILE_READ | O_DIRECTORY | O_CLOEXEC);

                if (directory >= 0)
                {
                        system_call_1(syscall(syncfs), (positive)directory);
                        system_close(directory);
                }
        }

        return 0;
}

/*
        The running image onto a mounted system partition, which is the whole
        of an update. The image there before is kept as previous.efi, for the
        firmware's shell or a stick to start when a build turns out bad; the
        new one is written beside the old and renamed over it, so a machine
        that loses power part way finds the image it had or this one, whole
        (test/run's power cuts hold that on a disk killed at every few
        milliseconds of the write). Nothing is renamed that is not what was
        copied, nothing it was copying is left behind when it stops, what a cut
        left is removed before the next write, and what it did is read back
        from the disk itself, the partition unmounted. The partition is left
        unmounted.
*/
static b32 host_place_image(string_address name, string_address system,
                            string_address running, host_settings address_to carry,
                            bool address_to carried)
{
        p8 source[HOST_PATH_ROOM];

        *carried = false;
        if (host_place_written(system, running, carry, carried))
        {
                host_image_discard(system);
                return 1;
        }

        if (!host_join(source, sizeof(source), HOST_MEDIUM, HOST_IMAGE) ||
            !host_image_on_disk(name, system, source, *carried ? carry : null))
                return host_refuse("%s did not read back from the disk as the image "
                                   "that was written\n", name);

        return 0;
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

        return_if(failed < 0, host_fail(install->data, failed));

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

static b32 host_update_locked(host_install address_to install)
{
        p8 running[HOST_BUILD_ROOM];
        host_medium_search search;
        bipolar mounted;
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

        mounted = host_mount(install->system, HOST_SYSTEM, "vfat", HOST_WRITABLE);
        if (mounted < 0)
        {
                host_unmount(HOST_MEDIUM);
                return host_fail(install->system, mounted);
        }

        host_say(log, host_label "writing this build to %s, from %s\n",
                 install->system, search.name);

        /*      An update keeps the disk's settings: they are that machine's,
                and the stick is only carrying a build. An image from before
                there were settings has none, which is the defaults. */
        {
                host_settings disk;
                host_settings session;
                p8 path[HOST_PATH_ROOM];
                bool carried;
                b32 read = -1;

                host_settings_empty(address_of disk);
                failed = 0;
                if (host_join(path, sizeof(path), HOST_SYSTEM, HOST_IMAGE))
                {
                        bipolar opened = system_open_at(AT_FDCWD, path,
                                                        FILE_READ | O_CLOEXEC);

                        //      An image that is there and cannot be read says
                        //      nothing of the settings in it, and is not
                        //      replaced on a guess that it has none.
                        if (opened >= 0)
                        {
                                system_close(opened);
                                read = host_settings_image(path, address_of disk);
                        }
                        else if (opened != -ERROR_NO_ENTRY)
                                failed = host_refuse("%s cannot be read, so its settings "
                                                     "cannot be kept\n", install->system);
                }

                if (read == 0)
                        host_say(log, host_label "both settings slots on %s are damaged: "
                                                 "it starts from the defaults\n",
                                 install->disk);

                disk.generation++;
                host_medium_fresh(disk.medium, sizeof(disk.medium));

                if (!failed)
                        failed = host_place_image(install->system, HOST_SYSTEM, running,
                                                  address_of disk, address_of carried);

                if (!failed && !carried)
                        host_say(log, host_label "this build's image has no place for "
                                                 "settings, so %s starts with the "
                                                 "defaults\n", install->disk);
                else if (!failed)
                {
                        //      A session started from this disk is still its copy.
                        if (host_settings_kept(address_of session) &&
                            !memory_compare(session.medium, disk.medium,
                                            sizeof(disk.medium)))
                        {
                                session.generation = disk.generation;
                                host_settings_keep(address_of session);
                        }

                        host_say(log, host_label "%s keeps its own settings\n",
                                 install->disk);
                }
        }

        host_unmount(HOST_SYSTEM);
        host_unmount(HOST_MEDIUM);
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
        radio_restore();
        usb_restore();
        name_restore();
        locale_restore();
        dns_restore();
        tune_restore();
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
        security_publish(settings);
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
                //      Before the disks are waited for: TLS can start sooner.
                security_publish(address_of settings);
                host_bind_apply(address_of settings);
        }

        if (known && settings.flags & SPARK_SETTINGS_MOUNT_OFF)
        {
                host_write_text(HOST_HINT, "");
                host_verdict_set("live", "");
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
                host_write_text(HOST_HINT, "");
                host_verdict_set("live", "");
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
                        host_write_text(HOST_QUESTION, "");
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
                its /root/main.moonwater.sh then running as root. */
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
                        host_bind_apply(address_of settings);
                }

                if (known && settings.flags & SPARK_SETTINGS_MOUNT_OFF)
                {
                        host_write_text(HOST_HINT, "");
                        host_verdict_set("live", "");
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

                host_verdict_set("live", "");
                host_booted(known ? address_of settings : null);
                return 1;
        }

        chosen = refused ? refused : census.found;
        host_write_text(HOST_QUESTION, "");
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
        host_say(log, host_label "A terminal will ask what to do with it, or "
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
                return;

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

        system_rename_at(AT_FDCWD, HOST_QUESTION_TAKEN, AT_FDCWD, HOST_QUESTION, 0);
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

        if (string_has_prefix(verdict, "ask ") &&
            system_rename_at(AT_FDCWD, HOST_QUESTION, AT_FDCWD, HOST_QUESTION_TAKEN,
                             0) >= 0)
                host_question(verdict + 4);
        else if (string_equals(verdict, "live") &&
                 system_rename_at(AT_FDCWD, HOST_HINT, AT_FDCWD, HOST_HINT_TAKEN,
                                  0) >= 0)
        {
                host_say(log, host_label "This is a live session: nothing is kept "
                                         "after power off.\n"
                              host_label "moonwater setup install DISK puts "
                                         "Moonwater on a disk.\n");
        }
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
        Refusals first, then one question, then the writes.

        The boot image has to be on some other disk -- it is what gets
        copied, and a disk holding the only copy is the one thing that must
        not be erased. A disk with anything mounted is refused by the kernel
        itself: the exclusive open fails. Removable media is refused unless
        asked for, because the stick being installed from is removable too.
*/
static b32 host_install_locked(string_address asked, bool removable, bool said_yes)
{
        string_address name = string_has_prefix(asked, "/dev/") ? asked + 5 : asked;
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
        storage_format_identity identity;
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
        if (host_session_disk(kept, sizeof(kept)))
                return host_refuse("this session already keeps %s; installing on "
                                   "another disk starts from a live session\n", kept);

        if (!host_running_build(running, sizeof(running)))
                return host_refuse("moonwater cannot read its own build\n");

        /*      Read before anything is mounted to look for the image: an
                install takes this session's settings, which is how what was
                set on a live stick is still set on the disk it installed. */
        host_settings carry;
        bool carried;

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
                return failed < 0 ? host_fail(device, failed)
                                  : host_refuse("%s is smaller than the 2 GiB an "
                                                "install needs\n", name);
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
        if (!said_yes)
        {
                host_say(log, host_label "Type %s to go on: ", name);

                if (host_read_line(answer, sizeof(answer)) < 0 ||
                    !string_equals(answer, name))
                {
                        system_close(handle);
                        host_unmount(HOST_MEDIUM);
                        host_say(log, host_label "nothing written\n");
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

        if (!failed)
        {
                host_say(log, host_label "partitioning and formatting %s\n", name);

                failed = storage_format_zero(handle, 0, HOST_ALIGN_BYTES);
        }
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

        //      Written part way, the disk is not left holding half of an install.
        if (failed)
                host_install_clear(handle);
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

        failed = host_place_image(target.system, HOST_SYSTEM, running, address_of carry,
                                  address_of carried);
        host_unmount(HOST_SYSTEM);
        host_unmount(HOST_MEDIUM);
        if (failed)
        {
                host_install_abandon(address_of target);
                return 1;
        }

        host_say(log, carried ? host_label "%s starts with this session's settings\n"
                              : host_label "this build's image has no place for "
                                           "settings, so %s starts with the defaults\n",
                 name);

        if (host_take(address_of target, false))
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

// Bindings ------------------------------------------------------

/* One ioctl of /dev/spark, the device opened for it and let go of. */
static bipolar host_spark_once(unsigned int command, void *request, unsigned int flags)
{
        bipolar device = system_open_at(AT_FDCWD, SPARK_DEVICE, flags | O_CLOEXEC);
        bipolar failed;

        if (device < 0)
                return device;

        failed = system_control(device, command, request);
        system_close(device);
        return failed;
}

static fn host_bind_fill(unsigned int op, unsigned int event, string_address command,
                         struct bind_control address_to control)
{
        positive length;

        memory_zero(control, sizeof(address_to control));
        control->op = op;
        control->event = event;

        if (command)
        {
                length = string_length(command);
                memory_copy(control->command, command,
                            length < SPARK_BIND_COMMAND_MAX ? length + 1
                                                            : SPARK_BIND_COMMAND_MAX);
        }
}

//      What the kernel answers is ended here, whatever it left at the end.
static bipolar host_bind_finish(bipolar failed, struct bind_control address_to control)
{
        control->command[SPARK_BIND_COMMAND_MAX - 1] = end;
        control->name[SPARK_BIND_NAME_MAX - 1] = end;
        return failed < 0 ? failed : 0;
}

static bipolar host_bind_ioctl(bipolar device, unsigned int op, unsigned int event,
                               string_address command,
                               struct bind_control address_to control)
{
        host_bind_fill(op, event, command, control);
        return host_bind_finish(system_control(device, SPARK_IOCTL_BIND, control),
                                control);
}

/*
        One request to a bound event, the device opened for it alone. SET when
        `command` is not null, then the kernel copies the row back. A command
        too long is handed over whole, cut at the size without a terminator, so
        the kernel's own refusal answers. The listing and boot apply keep the
        file open (host_bind_ioctl); opening once per event was twenty-two trips
        through /dev/spark for `moonwater bind` with no arguments.
*/
static bipolar host_bind_request(unsigned int op, unsigned int event,
                                 string_address command,
                                 struct bind_control address_to control)
{
        host_bind_fill(op, event, command, control);
        return host_bind_finish(host_spark_once(SPARK_IOCTL_BIND, control, FILE_READ),
                                control);
}

// Settings ------------------------------------------------------

/*
        What this machine does at boot and when it stops, kept in the boot
        image itself.

        The block is spark.c's: two slots in the image's .mwset section. A
        change is made to this session's copy, /run/moonwater/settings, and
        then written over the older slot of the image this session started
        from. That image is found by its build and by the medium id and
        generation the session's copy carries, so of a stick and a disk with
        the same build only the one that booted is written.

        The write goes to the partition underneath the file, at the blocks the
        filesystem names, and only once those blocks have read back as the
        file's own bytes. The file keeps its size, and the FAT, the directory
        entry and its times are never touched, so power lost part way tears
        one slot and leaves the other to boot from. No image is copied or
        renamed to change a setting.

        What cannot be written -- a read-only stick, an image found on no
        disk, one built without the section -- stays this session's, says
        why, and still goes along with moonwater setup install.
*/
#define HOST_SETTINGS HOST_STATE "/settings"
#define HOST_SETTINGS_SECTION ".mwset\0\0"
#define HOST_SETTINGS_PAGE 4096
#define HOST_SETTINGS_BLOCKS (SPARK_SETTINGS_SLOT / 512)
#define HOST_FIBMAP 1
#define HOST_FIGETBSZ 2

#define HOST_SETTINGS_CHANGED 0
#define HOST_SETTINGS_SHOWN 1
#define HOST_SETTINGS_USAGE 2
#define HOST_SETTINGS_REFUSED 3

typedef struct
{
        struct spark_settings_entry entry;
        p8 address_to text;
        positive at;
} host_setting;

static const struct
{
        string_address verb;
        p8 list;
        string_address empty;
        p8 hook;
} host_lists[] = {
    {"init", SPARK_SETTINGS_INIT, "nothing runs at boot", MOONWATER_HOOK_INIT},
    {"exit", SPARK_SETTINGS_EXIT, "nothing runs when the machine stops",
     MOONWATER_HOOK_END},
};

/*
        Which command each switch is spelled under. Moving one is a line here.

        The block also holds the startup list, which nothing reads yet: no
        command takes it until the machine does, so none is a setting that is
        accepted and then does nothing. Canvas at boot is read by the kernel
        before it probes for a card.
*/
static const struct
{
        string_address verb;
        string_address word;
        p32 flag;
} host_switches[] = {
    {"init", "mount", SPARK_SETTINGS_MOUNT_OFF},
    {"init", "canvas", SPARK_SETTINGS_CANVAS_OFF},
};

static fn host_settings_empty(host_settings address_to settings)
{
        memory_zero(settings, sizeof(address_to settings));
        settings->magic = SPARK_SETTINGS_MAGIC;
        settings->version = SPARK_SETTINGS_VERSION;
        settings->header = SPARK_SETTINGS_HEADER;
        settings->slot = SPARK_SETTINGS_SLOT;
}

static fn host_settings_seal(host_settings address_to settings)
{
        settings->sum = 0;
        settings->sum = spark_settings_sum(settings);
}

/* The entry at at, stepping at past it; false once there are no more. */
static bool host_settings_next(host_settings address_to settings,
                               positive address_to at,
                               host_setting address_to into)
{
        if (address_to at + SPARK_SETTINGS_ENTRY > settings->length)
                return false;

        into->at = address_to at;
        memory_copy_apart(address_of into->entry, settings->payload + into->at,
                          SPARK_SETTINGS_ENTRY);
        //      Every caller copies the text into a buffer of the most an
        //      entry may hold, so an entry that says more is the end of the
        //      list, whoever checked the block before it got here.
        if (into->entry.length > SPARK_SETTINGS_TEXT_MOST ||
            into->entry.length > settings->length - (address_to at + SPARK_SETTINGS_ENTRY))
                return false;
        into->text = settings->payload + into->at + SPARK_SETTINGS_ENTRY;
        address_to at += SPARK_SETTINGS_ENTRY +
                         spark_settings_padded(into->entry.length);
        return true;
}

/* An entry's words, the way the command line spells them. */
static fn host_settings_text(p8 address_to into, host_setting address_to setting)
{
        if (setting->entry.kind == SPARK_SETTINGS_SHELL)
                string_copy(into, "shell");
        else if (setting->entry.kind == SPARK_SETTINGS_KERNEL_SHELL)
                string_copy(into, "kernel_shell");
        else
        {
                memory_copy_apart(into, setting->text, setting->entry.length);
                into[setting->entry.length] = end;
        }
}

static positive host_settings_count(host_settings address_to settings, p8 list)
{
        host_setting setting;
        positive at = 0;
        positive count = 0;

        while (host_settings_next(settings, address_of at, address_of setting))
                count += setting.entry.list == list;

        return count;
}

/*
        An entry at the end of its list, with the next id that list hands out:
        ids are never given twice, so a remove by number cannot reach an entry
        added after the number was read. Null once it is in, else why not.
*/
static string_address host_settings_add(host_settings address_to settings,
                                        p8 list, p8 kind, string_address text,
                                        positive length, p16 address_to id)
{
        positive room = spark_settings_padded(length);
        bool security = list == SPARK_SETTINGS_SECURITY;
        struct spark_settings_entry entry;

        if (length > (list == SPARK_SETTINGS_BIND ? SPARK_SETTINGS_BIND_TEXT_MOST
                      : security                  ? SPARK_SETTINGS_TRUST_MOST
                                                  : SPARK_SETTINGS_TEXT_MOST))
                return list == SPARK_SETTINGS_BIND ? "a bound command is 255 bytes at most"
                       : security                  ? "an anchor is 552 bytes at most"
                                                   : "an entry is 4096 bytes at most";

        if (host_settings_count(settings, list) >=
            (list == SPARK_SETTINGS_BIND ? SPARK_SETTINGS_BIND_MOST
             : security                  ? SPARK_SETTINGS_SECURITY_MOST
                                         : SPARK_SETTINGS_LIST_MOST))
                return list == SPARK_SETTINGS_BIND ? "the bind table holds 48 events at most"
                       : security                  ? "the security list holds 96 entries at most"
                                                   : "a list holds 16 entries at most";

        if (SPARK_SETTINGS_PAYLOAD - settings->length < SPARK_SETTINGS_ENTRY + room)
                return "the settings block is full";

        memory_zero(address_of entry, sizeof(entry));
        entry.list = list;
        entry.kind = kind;
        entry.length = (p16)length;

        //      A bound command's id is its event, and a security entry's says
        //      which switch it is or who added it: the caller names both.
        if (list == SPARK_SETTINGS_BIND || security)
                entry.id = id ? address_to id : 0;
        else
        {
                p16 address_to next = settings->next + list - 1;

                entry.id = address_to next ? address_to next : 1;
                if (entry.id == 0xffff)
                        return "this list has handed out every id it has";

                address_to next = entry.id + 1;
        }

        memory_copy_apart(settings->payload + settings->length, address_of entry,
                          SPARK_SETTINGS_ENTRY);
        memory_copy_apart(settings->payload + settings->length + SPARK_SETTINGS_ENTRY,
                          text, length);
        memory_zero(settings->payload + settings->length + SPARK_SETTINGS_ENTRY +
                        length,
                    room - length);
        settings->length += SPARK_SETTINGS_ENTRY + room;

        if (id)
                address_to id = entry.id;

        return null;
}

static fn host_settings_drop(host_settings address_to settings,
                             host_setting address_to setting)
{
        positive size = SPARK_SETTINGS_ENTRY +
                        spark_settings_padded(setting->entry.length);

        memory_copy(settings->payload + setting->at,
                    settings->payload + setting->at + size,
                    settings->length - setting->at - size);
        settings->length -= size;
        memory_zero(settings->payload + settings->length, size);
}

/* An entry of a list by its exact words. */
static bool host_settings_find_words(host_settings address_to settings, p8 list,
                                     string_address wanted, host_setting address_to into)
{
        p8 text[SPARK_SETTINGS_TEXT_MOST + 1];
        positive at = 0;

        while (host_settings_next(settings, address_of at, into))
                if (into->entry.list == list &&
                    (host_settings_text(text, into), string_equals(text, wanted)))
                        return true;

        return false;
}

/* An entry named by its id, all digits, or by its exact words. */
static bool host_settings_find(host_settings address_to settings, p8 list,
                               string_address wanted, host_setting address_to into)
{
        positive id = 0;
        positive at = 0;
        string_address stop = wanted;
        b32 range = 0;
        bool by_id = byte_is_digit(*wanted);

        if (by_id)
                id = string_to_number_unsigned_checked(wanted, address_of stop, 10,
                                                       address_of range);
        by_id = by_id && !*stop && !range && id <= 0xffff;

        if (!by_id)
                return host_settings_find_words(settings, list, wanted, into);

        while (host_settings_next(settings, address_of at, into))
                if (into->entry.list == list && into->entry.id == id)
                        return true;

        return false;
}

//      Where host_words_write puts what it is handed, and whether it ran out.
static p8 address_to host_words_into;
static positive host_words_room;
static bool host_words_full;

static fn host_words_write(address_any data, positive length)
{
        positive used = string_length(host_words_into);

        if (!length)
                length = string_length(data);
        if (used + length >= host_words_room)
        {
                host_words_full = true;
                return;
        }
        memory_copy_apart(host_words_into + used, data, length);
        host_words_into[used + length] = end;
}

/* Words given apart, joined by one space as a shell would have them: one
   word is the command as it was typed, and several are quoted where they
   need it, so `bind init add sh -c 'echo hi there'` keeps the quotes that
   made `echo hi there` one word instead of running `sh -c echo hi there`. */
static bool host_words_join(p8 address_to into, positive room,
                            string_address address_to words, positive count,
                            positive address_to length)
{
        into[0] = end;
        host_words_into = into;
        host_words_room = room;
        host_words_full = false;

        for (positive at = 0; at < count && !host_words_full; at++)
        {
                if (at)
                        host_words_write(" ", 1);
                if (count == 1)
                        host_words_write((address_any)words[at], 0);
                else
                        writer_shell_name(host_words_write, words[at]);
        }

        address_to length = string_length(into);
        return !host_words_full;
}

/* Which slot a write goes over: a damaged one, else the older. */
static positive host_settings_older(host_settings address_to slots)
{
        if (spark_settings_check(slots) < 0)
                return 0;
        if (spark_settings_check(slots + 1) < 0)
                return 1;

        return slots[1].generation < slots[0].generation ? 1 : 0;
}

/* One past the newest generation either slot believably carries. */
static p64 host_settings_generation(host_settings address_to slots)
{
        p64 most = 0;

        for (positive at = 0; at < 2; at++)
                if (spark_settings_check(slots + at) >= 0 && slots[at].generation > most)
                        most = slots[at].generation;

        return most + 1;
}

/*
        The first page of a PE image read into head, and where its section
        table is in it and how many sections it names: false for a file that
        is not one, or one whose section table does not end inside the page.
*/
static bool host_pe_table(bipolar handle, p8 address_to head, positive address_to table,
                          positive address_to count)
{
        positive pe;

        if (file_transfer_exact(syscall(pread64), handle, head, HOST_SETTINGS_PAGE, 0) !=
                (bipolar)HOST_SETTINGS_PAGE ||
            head[0] != 'M' || head[1] != 'Z')
                return false;

        pe = storage_le32(head + 0x3c);
        if (pe > HOST_SETTINGS_PAGE - 24 || memory_compare(head + pe, "PE\0\0", 4))
                return false;

        address_to count = storage_le16(head + pe + 6);
        address_to table = pe + 24 + storage_le16(head + pe + 20);
        return address_to table + address_to count * 40 <= HOST_SETTINGS_PAGE;
}

/*
        Whether a file runs as far as its own section table says it does. An
        image cut short, by a copy that stopped or a stick pulled, still names
        its build in its first page, and everything past the cut is missing
        from what would be copied and read back as itself.
*/
static bool host_image_whole(string_address path)
{
        p8 head[HOST_SETTINGS_PAGE];
        file_facts facts;
        bipolar handle = system_open_at(AT_FDCWD, path, FILE_READ | O_CLOEXEC);
        positive table;
        positive count;
        bool whole = false;

        if (handle < 0)
                return false;

        if (host_pe_table(handle, head, address_of table, address_of count) && count &&
            file_look_code(AT_FDCWD, path, 0, address_of facts) >= 0)
        {
                whole = true;
                for (positive at = 0; at < count; at++)
                {
                        p8 address_to section = head + table + at * 40;

                        if ((p64)storage_le32(section + 20) + storage_le32(section + 16) >
                            facts.size)
                                whole = false;
                }
        }

        system_close(handle);
        return whole;
}

/*
        Where an image keeps its settings: the file offset of its first slot,
        or 0 for an image without them. The section table says where, and both
        slots must carry the magic there before anything believes it.
*/
static p64 host_settings_section(bipolar handle)
{
        p8 head[HOST_SETTINGS_PAGE];
        p64 magic[2];
        positive table;
        positive count;

        if (!host_pe_table(handle, head, address_of table, address_of count))
                return 0;

        for (positive at = 0; at < count; at++)
        {
                p8 address_to section = head + table + at * 40;
                p64 raw = storage_le32(section + 20);

                if (memory_compare(section, HOST_SETTINGS_SECTION, 8))
                        continue;

                if (!raw || raw % HOST_SETTINGS_PAGE ||
                    storage_le32(section + 16) < 2 * SPARK_SETTINGS_SLOT ||
                    file_transfer_exact(syscall(pread64), handle, (p8 address_to)magic, 8,
                                        raw) != 8 ||
                    file_transfer_exact(syscall(pread64), handle,
                                        (p8 address_to)(magic + 1), 8,
                                        raw + SPARK_SETTINGS_SLOT) != 8 ||
                    magic[0] != SPARK_SETTINGS_MAGIC || magic[1] != SPARK_SETTINGS_MAGIC)
                        return 0;

                return raw;
        }

        return 0;
}

static bool host_settings_slots(bipolar handle, host_settings address_to slots,
                                p64 address_to offset)
{
        address_to offset = host_settings_section(handle);

        return address_to offset &&
               file_transfer_exact(syscall(pread64), handle, (p8 address_to)slots,
                                   2 * SPARK_SETTINGS_SLOT, address_to offset) ==
                   (bipolar)(2 * SPARK_SETTINGS_SLOT);
}

/*
        The settings an image boots with: 1 from a slot that checks, 0 for the
        defaults because both are damaged, -1 for an image without settings.
*/
static b32 host_settings_image(string_address path, host_settings address_to into)
{
        host_settings slots[2];
        bipolar handle = system_open_at(AT_FDCWD, path, FILE_READ | O_CLOEXEC);
        p64 offset;
        bool read;
        b32 newest;

        host_settings_empty(into);
        if (handle < 0)
                return -1;

        read = host_settings_slots(handle, slots, address_of offset);
        system_close(handle);
        if (!read)
                return -1;

        newest = spark_settings_newest(slots);
        if (newest < 0)
                return 0;

        memory_copy_apart(into, slots + newest, SPARK_SETTINGS_SLOT);
        return 1;
}

static bool host_settings_kept(host_settings address_to into)
{
        bipolar handle = system_open_at(AT_FDCWD, HOST_SETTINGS,
                                        FILE_READ | O_NONBLOCK | O_CLOEXEC);
        bipolar got;

        if (handle < 0)
                return false;

        got = file_transfer_exact(syscall(pread64), handle, (p8 address_to)into,
                                  SPARK_SETTINGS_SLOT, 0);
        system_close(handle);
        return got == (bipolar)SPARK_SETTINGS_SLOT && spark_settings_check(into) >= 0;
}

/*
        The kernel's copy: what the image booted with, or what was set since.
        A kernel with no /dev/spark, or started without the stub, has none.
*/
static bool host_settings_booted(host_settings address_to into)
{
        struct spark_settings_request request = {(unsigned long)into, 0};

        return host_spark_once(SPARK_IOCTL_SETTINGS_GET, address_of request,
                               FILE_READ_WRITE) >= 0 &&
               spark_settings_check(into) >= 0;
}

/*
        This session's copy, root's alone because a command can carry a secret,
        and the kernel's. True once both are as the caller has them: a kernel
        with no /dev/spark has no copy to be out of step, and is the only
        refusal that is not one. A copy that could not be written is not left
        beside the old one.
*/
static bool host_settings_keep(host_settings address_to settings)
{
        struct spark_settings_request request = {(unsigned long)settings, 0};
        bipolar set;
        bool written;

        host_settings_seal(settings);
        host_state_ready();
        set = host_spark_once(SPARK_IOCTL_SETTINGS_SET, address_of request,
                              FILE_READ_WRITE);

        written = file_publish_bytes(HOST_SETTINGS, settings, SPARK_SETTINGS_SLOT, 0600,
                                     false) >= 0;

        return written && (set >= 0 || set == -ENOENT || set == -ENODEV);
}

typedef struct
{
        string_address build;
        host_settings address_to session;
        p8 name[HOST_NAME_ROOM];
        p8 partuuid[STORAGE_PARTUUID_ROOM];
        p8 other[HOST_NAME_ROOM];
        positive count;
        host_settings found;
} host_settings_search;

typedef struct
{
        host_settings_search address_to search;
        string_address name;
        string_address partuuid;
} host_settings_look;

static bool host_settings_image_visit(string_address path, address_any opaque)
{
        host_settings_look address_to look = (host_settings_look address_to)opaque;
        host_settings_search address_to search = look->search;
        p8 build[HOST_BUILD_ROOM];
        host_settings newest;

        if (!host_image_build(path, build, sizeof(build)) ||
            !string_equals(build, search->build) ||
            host_settings_image(path, address_of newest) < 0 ||
            (search->session &&
             (newest.generation != search->session->generation ||
              memory_compare(newest.medium, search->session->medium,
                             sizeof(newest.medium)))))
                return false;

        if (!search->count)
        {
                string_copy(search->name, look->name);
                string_copy(search->partuuid, look->partuuid);
                memory_copy_apart(address_of search->found, address_of newest,
                                  SPARK_SETTINGS_SLOT);
        }
        else if (search->count == 1)
                string_copy(search->other, look->name);

        search->count++;
        return true;
}

/* A FAT partition whose image is this build and, when asked, this session's copy. */
static bool host_settings_visit(storage_identity address_to identity,
                                address_any opaque)
{
        host_settings_look look;

        look.search = (host_settings_search address_to)opaque;
        look.name = identity->path + sizeof("/dev/") - 1;
        look.partuuid = identity->partuuid_length ? (string_address)identity->partuuid
                                                  : (string_address) "";

        if (string_equals(identity->type, "vfat") && host_name_valid(look.name))
                host_with_image(look.name, HOST_MEDIUM, host_settings_image_visit,
                                address_of look);

        return true;
}

/*
        What a disk nobody has authenticated is allowed to say.

        The only thing tying a settings image on some disk to this build is
        the version string in that image's own setup header, and that string
        is bytes in a file: anybody who can read /proc/version can write it
        into an image of their own. Believing it about the two switches is a
        nuisance at worst. Believing it about the lists hands whoever pushed
        the disk in what this machine runs -- every entry in the payload is a
        command, at boot, at stop, or on a bound event, and these settings do
        not stay in memory: an install stamps them into the image it writes,
        and moonwater bind saves them as this session's. So the switches are
        taken and the payload is not, and the startup flag goes with it
        because it says a list was written.
*/
static fn host_settings_untrusted(host_settings address_to settings)
{
        memory_zero(settings->payload, settings->length);
        settings->length = 0;
        settings->flags &= ~SPARK_SETTINGS_STARTUP_SET;
        memory_zero(settings->next, sizeof(settings->next));
        host_settings_seal(settings);
}

/*
        This session's settings: its own copy, else the one image of this
        build a disk here has, else the defaults. False when what came back
        is not known to be anything: the copies are root's, so for anybody
        else the defaults are an empty block and not a fact about this
        machine.
*/
static bool host_settings_session(host_settings address_to settings)
{
        host_settings_search search;
        p8 running[HOST_BUILD_ROOM];

        if (host_settings_kept(settings) || host_settings_booted(settings))
                return true;

        host_settings_empty(settings);

        if (!bowl_is_root())
                return false;

        /*  Tighter than safe hears nothing from a disk at all: the switches
            are only a nuisance, but a machine that wants no word from media
            somebody pushed in gets none. */
        if (MOONWATER_STRICT_HOST >= STRICT_TIGHT)
                return true;

        memory_zero(address_of search, sizeof(search));

        if (!host_running_build(running, sizeof(running)))
                return true;

        search.build = running;
        storage_each_device(host_settings_visit, address_of search);

        if (search.count != 1)
                return true;

        memory_copy_apart(settings, address_of search.found, SPARK_SETTINGS_SLOT);
        host_settings_untrusted(settings);
        return true;
}

typedef struct
{
        host_settings slots[2];
        p64 offset;
        b32 blocks[HOST_SETTINGS_BLOCKS];
        positive size;
        positive target;
        p64 seen;
        string_address failed;
} host_settings_place;

/* The partition blocks under the slot a write goes over, as its filesystem names them. */
static bool host_settings_map_visit(string_address path, address_any opaque)
{
        host_settings_place address_to place = (host_settings_place address_to)opaque;
        string_address failed = null;
        bipolar handle = system_open_at(AT_FDCWD, path, FILE_READ | O_CLOEXEC);
        b32 size = 0;

        if (handle < 0)
                failed = "has no image";
        else if (!host_settings_slots(handle, place->slots, address_of place->offset))
                failed = "has an image built without settings";
        else if (system_control(handle, HOST_FIGETBSZ, address_of size) < 0 ||
                 size < 512 || size > HOST_SETTINGS_PAGE || HOST_SETTINGS_PAGE % size)
                failed = "keeps its image in blocks this cannot map";

        place->size = failed ? 512 : (positive)size;
        place->target = host_settings_older(place->slots);

        for (positive at = 0; !failed && at < SPARK_SETTINGS_SLOT / place->size; at++)
        {
                b32 block = (b32)((place->offset + place->target * SPARK_SETTINGS_SLOT) /
                                      place->size +
                                  at);

                if (system_control(handle, HOST_FIBMAP, address_of block) < 0 || block <= 0)
                        failed = "keeps its image in blocks this cannot map";

                place->blocks[at] = block;
        }

        if (handle >= 0)
                system_close(handle);

        place->failed = failed;
        return !failed;
}

static string_address host_settings_map(string_address name,
                                        host_settings_place address_to place)
{
        place->failed = "could not be mounted";
        host_with_image(name, HOST_MEDIUM, host_settings_map_visit, place);
        return place->failed;
}

/* The slots as the disk has them, the image's pages dropped first, and where in
   the image the section was (0 for an image with none). */
static bool host_settings_read_back(string_address path, address_any opaque)
{
        host_settings_place address_to place = (host_settings_place address_to)opaque;
        bipolar handle = system_open_at(AT_FDCWD, path, FILE_READ | O_CLOEXEC);

        place->seen = 0;
        if (handle >= 0)
        {
                system_call_4(syscall(fadvise64), (positive)handle, 0, 0,
                              HOST_FADVISE_DONTNEED);
                if (!host_settings_slots(handle, place->slots, address_of place->seen))
                        place->seen = 0;
                system_close(handle);
        }

        return place->seen;
}

/*
        This session's settings over the older slot of the image on name.

        Nothing else may have the partition mounted, and the blocks must hold
        the file's bytes before one is written. Afterwards the partition's
        cache is flushed and the file read again from a fresh mount with its
        pages dropped, so what is checked is the disk and not memory. Null
        once written and read back, else how it went wrong, said after the
        partition's name.
*/
static string_address host_settings_write(string_address name, string_address partuuid,
                                          host_settings address_to settings)
{
        host_settings_place place;
        host_settings slot;
        storage_mount_table table;
        p8 device[HOST_NAME_ROOM + 8];
        p8 path[HOST_PATH_ROOM];
        p8 block[HOST_SETTINGS_PAGE];
        string_address failed = null;
        bipolar handle;
        b32 newest;

        if (!host_join(device, sizeof(device), "/dev/", name) ||
            !host_join(path, sizeof(path), "/sys/class/block/", name))
                return "has a name too long to write";

        if (host_block_flag(path, "/ro"))
                return "is read-only";

        //      The name was found some time ago, and a disk comes and goes.
        if (partuuid[0])
        {
                p8 current[HOST_NAME_ROOM];

                if (!host_partition_uuid_resolve(partuuid, current) ||
                    !string_equals(current, name))
                        return "is no longer the partition it was found as";
        }

        if (storage_mount_table_load(address_of table, null))
        {
                bool mounted = false;

                for (positive at = 0; at < table.count; at++)
                        mounted |= string_equals(table.entry[at].source, device);

                storage_mount_table_release(address_of table);
                if (mounted)
                        return "is mounted; unmount it and try again";
        }

        failed = host_settings_map(name, address_of place);
        if (failed)
                return failed;

        memory_copy_apart(address_of slot, settings, SPARK_SETTINGS_SLOT);
        slot.generation = host_settings_generation(place.slots);

        if (host_medium_blank(slot.medium, sizeof(slot.medium)))
        {
                newest = spark_settings_newest(place.slots);
                if (newest >= 0)
                        memory_copy_apart(slot.medium, place.slots[newest].medium,
                                          sizeof(slot.medium));

                host_medium_fresh(slot.medium, sizeof(slot.medium));
        }

        host_settings_seal(address_of slot);

        handle = system_open_at(AT_FDCWD, device,
                                FILE_READ_WRITE | FILE_EXCLUSIVE | O_CLOEXEC);
        if (handle < 0)
                return handle == -ERROR_BUSY ? "is in use; unmount it and try again"
                       : handle == -ERROR_READ_ONLY || handle == -ERROR_ACCESS
                           ? "is read-only"
                           : "cannot be opened for writing";

        for (positive at = 0; !failed && at < SPARK_SETTINGS_SLOT / place.size; at++)
                if (file_transfer_exact(syscall(pread64), handle, block, place.size,
                                        (p64)place.blocks[at] * place.size) !=
                        (bipolar)place.size ||
                    memory_compare(block,
                                   (p8 address_to)(place.slots + place.target) +
                                       at * place.size,
                                   place.size))
                        failed = "does not keep its image where its filesystem says, "
                                 "so nothing was written";

        for (positive at = 0; !failed && at < SPARK_SETTINGS_SLOT / place.size; at++)
                if (storage_write(handle, (p8 address_to)address_of slot + at * place.size,
                                  place.size, (p64)place.blocks[at] * place.size) !=
                    (bipolar)place.size)
                        failed = "could not be written; its other slot still has the "
                                 "settings from before";

        if (!failed && system_call_1(syscall(fsync), (positive)handle) < 0)
                failed = "could not be synced; its other slot still has the settings "
                         "from before";

        system_control(handle, HOST_BLKFLSBUF, 0);
        system_close(handle);
        if (failed)
                return failed;

        if (!host_with_image(name, HOST_MEDIUM, host_settings_read_back, address_of place))
                return "was written, and could not be mounted again to check it";

        if (place.seen != place.offset ||
            memory_compare(place.slots + place.target, address_of slot,
                           SPARK_SETTINGS_SLOT) ||
            spark_settings_newest(place.slots) != (b32)place.target)
                return "was written and did not read back as written";

        settings->generation = slot.generation;
        memory_copy_apart(settings->medium, slot.medium, sizeof(slot.medium));
        return null;
}

/*
        Writes this session's settings to its image where it can, ending the
        line with where they went. False when the session could not keep its
        own copy, which the line then says: the change is in the image or
        nowhere, and the next command would read the old one.
*/
static bool host_settings_save(host_settings address_to settings)
{
        host_settings_search search;
        p8 running[HOST_BUILD_ROOM];
        string_address failed = null;

        memory_zero(address_of search, sizeof(search));

        if (host_running_build(running, sizeof(running)))
        {
                search.build = running;
                search.session = settings;
                storage_each_device(host_settings_visit, address_of search);
        }

        if (search.count == 1)
                failed = host_settings_write(search.name, search.partuuid, settings);

        if (search.count == 1 && !failed)
                string_format(log, "saved in the image on %s\n", search.name);
        else if (search.count == 1)
                string_format(log, "this session only: %s %s\n", search.name, failed);
        else if (search.count)
                string_format(log, "this session only: %s and %s both have this "
                                   "session's image\n",
                              search.name, search.other);
        else
                string_format(log, "this session only: no disk here has the image "
                                   "this session started from\n");

        log_flush();
        if (host_settings_keep(settings))
                return true;

        host_say(log_error, host_label "this session could not keep its copy of "
                                       "the settings; the next command reads the "
                                       "ones from before\n");
        return false;
}

/*
        A saved command as it is shown: a script keeps its lines and tabs, and
        every other control byte, and bytes that are not valid text, are
        written as \xNN, so what an image carries in its settings cannot put
        a control sequence in front of the terminal's parser.
*/
static string_address host_plain(string_address text)
{
        static p8 shown[SPARK_SETTINGS_TEXT_MOST * 4 + 8];
        positive used = 0;
        positive at = 0;

        while (text[at] && used + 8 < sizeof(shown))
        {
                positive start = at;

                while (text[at] && text[at] != '\n' && text[at] != '\t')
                        at++;
                radio_display(shown + used, sizeof(shown) - used, (p8 address_to)text + start,
                              at - start);
                used += string_length((string_address)shown + used);
                if (text[at] && used + 8 < sizeof(shown))
                        shown[used++] = (p8)text[at++];
        }
        shown[used] = end;
        return (string_address)shown;
}

/*
        One list said twice.

        `moonwater bind init` writes it as log lines of its own, and
        `moonwater status` sets the same three answers -- the hook that took
        the list away, the entries, or the sentence for an empty one -- under
        a heading on the session page. Only the frame around them differs,
        and a page is one block of output, so it does not flush per list.
*/
static fn host_settings_lines(host_settings address_to settings, positive which,
                              bool page)
{
        p8 text[SPARK_SETTINGS_TEXT_MOST + 1];
        host_setting setting;
        positive at = 0;
        positive shown = 0;
        p16 line = host_machine_hook_line(host_lists[which].hook);

        if (line)
        {
                string_format(log, page ? "  %s: %s:%p\n"
                                        : host_label "%s is %s:%p\n",
                              host_lists[which].verb, host_machine_where(),
                              (positive)line);
                if (!page)
                        log_flush();
                return;
        }

        while (host_settings_next(settings, address_of at, address_of setting))
        {
                if (setting.entry.list != host_lists[which].list)
                        continue;

                if (page && !shown)
                        string_format(log, "  %s:\n", host_lists[which].verb);

                host_settings_text(text, address_of setting);
                string_format(log, page ? "    %p  %s\n" : "%p  %s\n",
                              (positive)setting.entry.id, host_plain((string_address)text));
                shown++;
        }

        if (!shown)
                string_format(log, page ? "  %s: %s\n" : host_label "%s: %s\n",
                              host_lists[which].verb, host_lists[which].empty);

        if (!page)
                log_flush();
}

static b32 host_settings_refused(string_address verb, string_address why,
                                 host_settings address_to settings)
{
        string_format(log_error, host_label "%s: %s", verb, why);
        if (string_equals(why, "the settings block is full"))
                string_format(log_error, " (%p of %p bytes); remove an entry first",
                              (positive)settings->length,
                              (positive)SPARK_SETTINGS_PAYLOAD);
        host_say(log_error, "\n");
        return HOST_SETTINGS_REFUSED;
}

/*
        Whether the words are a settings command at all: a list alone, a
        switch alone or with on or off, add or remove with words. Said before
        anything is read to answer them, so a mistyped line costs its usage
        and not the search for a copy of the settings.
*/
static bool host_settings_shaped(string_address address_to arguments, positive count)
{
        if (string_table_find(arguments[1], host_lists, sizeof(host_lists[0]),
                              array_count(host_lists)) == array_count(host_lists))
                return false;

        if (count == 2)
                return true;

        for (positive at = 0; at < array_count(host_switches); at++)
                if (string_equals(arguments[1], host_switches[at].verb) &&
                    string_equals(arguments[2], host_switches[at].word))
                        return count == 3 ||
                               (count == 4 && host_onoff(arguments[3]) >= 0);

        return count >= 4 && (string_equals(arguments[2], "add") ||
                              string_equals(arguments[2], "remove"));
}

/*
        One settings command against a copy in memory, nothing read or written
        but that copy, once host_settings_shaped has said it is one: CHANGED
        once it printed what changed and wants the line finished by saving,
        SHOWN once it printed what was asked, USAGE for words that name
        nothing to add or remove, or REFUSED once it said why.
*/
static b32 host_settings_apply(host_settings address_to settings,
                               string_address address_to arguments, positive count)
{
        string_address verb = arguments[1];
        p8 text[SPARK_SETTINGS_TEXT_MOST + 1];
        host_setting setting;
        string_address failed;
        positive length = 0;
        positive which = string_table_find(verb, host_lists,
                                           sizeof(host_lists[0]),
                                           array_count(host_lists));
        p16 id = 0;
        p8 list;

        list = host_lists[which].list;

        if (count == 2)
        {
                host_settings_lines(settings, which, false);
                return HOST_SETTINGS_SHOWN;
        }

        {
                p16 overlay = host_machine_hook_line(host_lists[which].hook);

                if (overlay)
                {
                        if (!string_equals(arguments[2], "mount") &&
                            !string_equals(arguments[2], "canvas"))
                        {
                                host_machine_refused(verb, overlay);
                                return HOST_SETTINGS_REFUSED;
                        }
                }
        }

        for (positive at = 0; at < array_count(host_switches); at++)
        {
                p32 flag = host_switches[at].flag;

                if (!string_equals(verb, host_switches[at].verb) ||
                    !string_equals(arguments[2], host_switches[at].word))
                        continue;

                if (count == 3)
                {
                        host_say(log, host_label "%s %s %s\n", verb,
                                 host_switches[at].word,
                                 settings->flags & flag ? "off" : "on");
                        return HOST_SETTINGS_SHOWN;
                }

                //      A switch already as asked is not written again.
                if (!!(settings->flags & flag) == !host_onoff(arguments[3]))
                {
                        host_say(log, host_label "%s %s %s\n", verb,
                                 host_switches[at].word, arguments[3]);
                        return HOST_SETTINGS_SHOWN;
                }

                if (!host_onoff(arguments[3]))
                        settings->flags |= flag;
                else
                        settings->flags &= ~flag;

                string_format(log, host_label "%s %s %s; ", verb,
                              host_switches[at].word, arguments[3]);
                return HOST_SETTINGS_CHANGED;
        }

        {
                bool adding = string_equals(arguments[2], "add");

                if (!host_words_join(text, sizeof(text), arguments + 3, count - 3,
                                         address_of length))
                        return host_settings_refused(verb, "an entry is 4096 bytes at most",
                                                     settings);

                if (!length)
                        return HOST_SETTINGS_USAGE;

                if (adding)
                {
                        //      The same line again, as a script says it at every
                        //      boot, is the entry it already has and not another
                        //      that runs beside it.
                        if (host_settings_find_words(settings, list, text, address_of setting))
                        {
                                host_say(log, host_label "%s %p is already: %s\n", verb,
                                         (positive)setting.entry.id, text);
                                return HOST_SETTINGS_SHOWN;
                        }
                        failed = host_settings_add(settings, list, SPARK_SETTINGS_COMMAND,
                                                   text, length, address_of id);
                        return_if(failed, host_settings_refused(verb, failed, settings));

                        string_format(log, host_label "%s %p added: %s; ", verb,
                                      (positive)id, text);
                        return HOST_SETTINGS_CHANGED;
                }

                if (!host_settings_find(settings, list, text, address_of setting))
                {
                        host_say(log_error, host_label "%s has no entry %s\n", verb,
                                 text);
                        return HOST_SETTINGS_REFUSED;
                }

                id = setting.entry.id;
                host_settings_text(text, address_of setting);
                host_settings_drop(settings, address_of setting);
                string_format(log, host_label "%s %p removed: %s; ", verb, (positive)id,
                              text);
                return HOST_SETTINGS_CHANGED;
        }
}

/* Where the image's own programs are: they are the image, and every boot has them. */
static const string_address host_image_programs[] = {
    "/bin/", "/sbin/", "/usr/bin/", "/usr/sbin/", "/lib/", "/usr/lib/", "/lib64/"};

/*
        A command naming a file the machine will not have when the entry runs:
        a live session's files, and /root, /home and the bowls once boot stops
        mounting them, are gone at power off. The image's own programs are not.
*/
static fn host_settings_note(host_settings address_to settings,
                             string_address verb, string_address text)
{
        p8 path[HOST_PATH_ROOM];
        p8 verdict[HOST_NAME_ROOM + 16];
        positive length = 0;
        bool kept = false;

        if (text[0] != '/')
                return;

        for (positive at = 0; at < array_count(host_image_programs); at++)
                if (host_starts(text, host_image_programs[at]))
                        return;

        while (text[length] && text[length] != ' ' && length + 1 < sizeof(path))
        {
                path[length] = text[length];
                length++;
        }
        path[length] = end;

        if (system_access_at(AT_FDCWD, path, 0) < 0)
                return;

        host_read_word(HOST_VERDICT, verdict, sizeof(verdict));

        for (positive at = 0; at < array_count(host_kept); at++)
        {
                positive prefix = string_length(host_kept[at].path);

                kept |= host_starts(path, host_kept[at].path) && path[prefix] == '/';
        }

        if (kept && string_has_prefix(verdict, "disk ") &&
            !(settings->flags & SPARK_SETTINGS_MOUNT_OFF))
                return;

        host_say(log, host_label "%s is read when the entry runs and is not kept "
                                 "after power off; moonwater bind %s add \"$(cat %s)\" "
                                 "keeps the script itself\n",
                 path, verb, path);
}

static b32 host_settings_locked(string_address address_to arguments, positive count)
{
        host_settings settings;
        p8 text[SPARK_SETTINGS_TEXT_MOST + 1];
        positive length = 0;
        b32 outcome;

        host_state_ready();
        host_settings_session(address_of settings);

        outcome = host_settings_apply(address_of settings, arguments, count);
        return_if(outcome == HOST_SETTINGS_USAGE, host_usage());
        if (outcome != HOST_SETTINGS_CHANGED)
                return outcome == HOST_SETTINGS_SHOWN ? 0 : 1;

        if (!host_settings_save(address_of settings))
                return 1;

        if (count > 3 && string_equals(arguments[2], "add") &&
            host_words_join(text, sizeof(text), arguments + 3, count - 3,
                                address_of length))
                host_settings_note(address_of settings, arguments[1], text);

        return 0;
}

static b32 host_settings_command(string_address address_to arguments, positive count)
{
        b32 outcome;

        host_need_root();

        return_if(!host_settings_shaped(arguments, count), host_usage());

        //      Showing a list or a switch changes nothing and waits for nobody.
        if (count < 4)
                return host_settings_locked(arguments, count);

        if (!host_acquire())
                return 1;

        outcome = host_settings_locked(arguments, count);
        host_release();
        return outcome;
}

/*
        Both slots of an image not yet in place, as these settings: 0 once they
        are, 1 for an image with no section to put them in -- one built before
        there were settings, which starts with the defaults and has said so --
        and negative when the image could not be written.
*/
static bipolar host_settings_stamp(string_address path, host_settings address_to settings)
{
        bipolar handle = system_open_at(AT_FDCWD, path, FILE_READ_WRITE | O_CLOEXEC);
        bipolar failed = 0;
        p64 offset;

        if (handle < 0)
                return handle;

        offset = host_settings_section(handle);
        if (!offset)
                failed = 1;
        else
        {
                host_settings_seal(settings);
                failed = storage_format_write(handle, (p8 address_to)settings,
                                              SPARK_SETTINGS_SLOT, offset);
                if (!failed)
                        failed = storage_format_write(handle, (p8 address_to)settings,
                                                      SPARK_SETTINGS_SLOT,
                                                      offset + SPARK_SETTINGS_SLOT);
                if (!failed)
                        failed = system_call_1(syscall(fsync), (positive)handle);
        }

        system_close(handle);
        return failed;
}

typedef struct
{
        host_settings address_to into;
        b32 found;
} host_settings_found;

static bool host_settings_found_visit(string_address path, address_any opaque)
{
        host_settings_found address_to look = (host_settings_found address_to)opaque;

        look->found = host_settings_image(path, look->into);
        return look->found >= 0;
}

/* The settings an install's own image starts with. */
static bool host_settings_install(host_install address_to install,
                                  host_settings address_to into)
{
        host_settings_found look = {into, -1};

        return host_install_refresh(install) &&
               host_with_image(install->system, HOST_LOOK, host_settings_found_visit,
                               address_of look) &&
               look.found >= 0;
}

// Events --------------------------------------------------------

/*
        init and exit, run the way a terminal runs what is typed into it:
        /shell -c, from /root, with the path a terminal gives its shell, so a
        bowl's published launchers -- pacman once bowl setup arch has run on
        a machine that keeps /bowls -- are found by name.

        init runs once per boot, as root, in the background and in the order
        written, once boot has settled the disks and somebody has answered
        its question when it asked one, so a command reads the /root and the
        bowls it will be using. Each entry's output goes to
        /run/moonwater/init/ID.log and how it ended to ID.status, the kernel
        log -- the kernel log window -- says when each starts and ends, by
        its number and never its text, and neither the prompt nor the
        desktop waits for any of it. The text is root's alone, because a
        command can carry a secret (`link group NS SECRET`), and anybody can
        read the kernel log on this image: its first eighty characters went
        there with every boot.

        exit runs when the machine stops, before anything is remounted
        read-only, with its output on the terminal that stopped it. Each
        entry gets ten seconds and all of them thirty; one still running then
        is killed with its process group, so a stuck command cannot keep a
        machine from powering off. A stop that is not the shell's -- the
        kernel's own orderly power off -- runs none of it.
*/
#define HOST_EVENTS_INIT HOST_STATE "/init"
#define HOST_EVENT_SHELL "/shell"
#define HOST_EXIT_EACH_NS ((p64)10000000000)
#define HOST_EXIT_ALL_NS ((p64)30000000000)
#define HOST_EVENT_POLL_NS ((p64)20000000)
//      The machine process takes the verdict's poll and the script's sourcing
//      to say it has the script; a boot that has not heard by then does not.
#define HOST_MACHINE_SOURCED_WAIT_NS ((p64)5000000000)

static string_address address_to host_event_environment(void)
{
        static string_address seed[] = {
            SPARK_COMMAND_ENVIRONMENT(SPARK_ENVIRONMENT_ENTRY) null};

        bowl_session_prepare("/root", null);
        return bowl_environment(seed);
}

static fn host_event_ending(p8 address_to into, positive room, positive status)
{
        p8 number[24];
        positive code = (positive)wait_status_code_base((p32)status, 256);

        positive_into_string(number, code >= 256 ? code & 0xff : code);
        string_copy_bounded(into, code >= 256 ? "killed by signal " : "exited ", room);
        string_append_bounded(into, number, room);
}

/* One line in the kernel log, which the kernel log window shows. */
static fn host_kmsg(string_address address_to parts)
{
        p8 line[320];
        bipolar handle;

        string_copy_bounded(line, "[moonwater] ", sizeof(line));
        for (; *parts; parts++)
                string_append_bounded(line, *parts, sizeof(line));

        handle = system_open_at(AT_FDCWD, "/dev/kmsg", 01 | O_CLOEXEC);
        if (handle < 0)
                return;

        kmsg_write(handle, line, string_length(line));
        system_close(handle);
}

/*
        /shell in a session of its own, from /root, with the environment init's
        entries get. Its input is /dev/null and its output stays what the
        caller has unless output is a descriptor to put there. Given a
        terminal, all three are that terminal, opened here, after the session
        is made, so that it is the shell's own and its controlling one.
*/
static bipolar host_shell_start(string_address address_to argv, positive count,
                                string_address terminal, bipolar output)
{
        bipolar child = system_fork();
        bipolar handle;

        if (child)
                return child;

        system_call(syscall(setsid));
        handle = system_open_at(AT_FDCWD, terminal ? terminal : (string_address) "/dev/null",
                                FILE_READ_WRITE | O_CLOEXEC | O_NOFOLLOW);
        if (terminal && handle < 3)
                system_call_1(syscall(exit), 126);

        if (handle > 0)
                for (positive at = 0; at < (terminal ? 3 : 1); at++)
                        system_call_3(syscall(dup3), (positive)handle, at, 0);
        if (!terminal && output > 2)
        {
                system_call_3(syscall(dup3), (positive)output, 1, 0);
                system_call_3(syscall(dup3), (positive)output, 2, 0);
        }

        system_signal_install(1, 0, 0, 0, null);
        system_call_1(syscall(chdir), (positive)(string_address) "/root");
        (void)shell_exec_file(HOST_EVENT_SHELL, argv, count, host_event_environment());
        system_call_1(syscall(exit), 127);
        return -1;
}

/* /shell -c text, its output where asked or inherited. */
static bipolar host_event_start(string_address text, bipolar output)
{
        string_address argv[] = {HOST_EVENT_SHELL, "-c", text, null};

        return host_shell_start(argv, 3, null, output);
}

/*
        A child to its end, or past limit killed with its group and then
        waited for: its wait status, and whether it had to be stopped. A limit
        of zero waits as long as it takes.
*/
static positive host_event_wait(bipolar child, p64 limit, bool address_to stopped)
{
        p64 started = system_clock_ns(HOST_CLOCK_BOOTTIME);
        positive status = 0;
        bipolar pidfd = limit ? system_call_2(syscall(pidfd_open), (positive)child, 0)
                              : -1;

        address_to stopped = false;

        /*      With a limit, the child's pidfd is polled for as long as is
                left of it, so the end is heard when it comes; the 20 ms look
                that used to sit here was late by up to that much on every
                event. */
        for (;;)
        {
                bipolar reaped = system_call_4(syscall(wait4), (positive)child,
                                               (positive)address_of status,
                                               limit ? 1 : 0, 0);

                if (reaped == child)
                {
                        if (pidfd >= 0)
                                system_close(pidfd);
                        return status;
                }
                if (reaped == -4)
                        continue;
                if (reaped < 0)
                {
                        if (pidfd >= 0)
                                system_close(pidfd);
                        return 0;
                }

                p64 elapsed = system_clock_ns(HOST_CLOCK_BOOTTIME) - started;

                if (elapsed >= limit)
                {
                        system_call_2(syscall(kill), (positive)(-child), SIGKILL);
                        system_call_2(syscall(kill), (positive)child, SIGKILL);
                        address_to stopped = true;
                        limit = 0;
                        continue;
                }

                if (pidfd < 0)
                {
                        host_pause(HOST_EVENT_POLL_NS);
                        continue;
                }

                p64 left = limit - elapsed;
                timespec span = {left / 1000000000, left % 1000000000};
                system_poll_descriptor waited = {(b32)pidfd, SYSTEM_POLL_READ, 0};

                system_poll_wait(address_of waited, 1, address_of span, null);
        }
}

/*
        Until boot has said what this session is: a verdict that is not a
        question, or the time a boot is given to say one. What runs at boot
        and does not need to be there before the disks are is held here.
*/
static fn host_verdict_settled(void)
{
        bipolar notify = host_verdict_watch();

        for (;;)
        {
                p8 verdict[HOST_NAME_ROOM + 16];
                bool found = host_read_word(HOST_VERDICT, verdict, sizeof(verdict)) >= 0;

                if (found ? !string_has_prefix(verdict, "ask ")
                          : system_clock_ns(HOST_CLOCK_BOOTTIME) >= HOST_VERDICT_WAIT_NS)
                        break;

                //      A question stands until it is answered, for as long as
                //      that takes; with no verdict at all, boot has until its time.
                host_verdict_pause(notify, found ? 0 : HOST_VERDICT_WAIT_NS,
                                   HOST_POLL_NS * 2);
        }
        host_verdict_unwatch(notify);
}

static fn host_events_boot(host_settings address_to settings)
{
        host_setting setting;
        positive at = 0;

        if (!settings || !host_settings_count(settings, SPARK_SETTINGS_INIT) ||
            system_fork())
                return;

        //      The runner, from here on: its own session, outliving boot.
        system_call(syscall(setsid));
        host_state_ready();
        host_state_directory(HOST_EVENTS_INIT, 0700);
        host_verdict_settled();

        //      After the verdict, which is when the machine process goes on to
        //      its script: a script that names moonwater_init has the list's
        //      place only if that process got that far.
        if (!host_init_list_runs())
                system_call_1(syscall(exit), 0);

        while (host_settings_next(settings, address_of at, address_of setting))
        {
                p8 text[SPARK_SETTINGS_TEXT_MOST + 1];
                p8 id[8];
                p8 path[HOST_PATH_ROOM];
                p8 status_path[HOST_PATH_ROOM];
                p8 ending[48];
                bipolar output;
                bipolar child;
                positive status;
                bool stopped = false;

                if (setting.entry.list != SPARK_SETTINGS_INIT)
                        continue;

                host_settings_text(text, address_of setting);
                positive_into_string(id, setting.entry.id);

                if (!host_join(path, sizeof(path), HOST_EVENTS_INIT "/", id) ||
                    !host_join(status_path, sizeof(status_path), path, ".status") ||
                    !host_join(path, sizeof(path), path, ".log"))
                        continue;

                {
                        string_address line[] = {"init ", id, " started", null};

                        host_kmsg(line);
                }

                host_write_text(status_path, "running\n");
                output = host_open_state(AT_FDCWD, path, 0600, FILE_WRITE);
                child = host_event_start(text, output);
                if (output >= 0)
                        system_close(output);

                status = child < 0 ? (positive)127 << 8
                                   : host_event_wait(child, 0, address_of stopped);
                host_event_ending(ending, sizeof(ending), status);
                string_append_bounded(ending, "\n", sizeof(ending));
                host_write_text(status_path, ending);
                ending[string_length(ending) - 1] = end;

                {
                        string_address line[] = {"init ", id, " ", ending, "; its output is in ",
                                                 path, null};

                        host_kmsg(line);
                }
        }

        system_call_1(syscall(exit), 0);
}

fn host_exit_run(void)
{
        host_settings settings;
        host_setting setting;
        positive at = 0;
        p64 started = system_clock_ns(HOST_CLOCK_BOOTTIME);

        bool machine;

        //      Before the machine process is asked anything: END is root's, a
        //      refusal left the stop polling ten seconds for a detach that
        //      was never coming, and the list is root's too.
        if (!bowl_is_root())
                return;

        machine = host_machine_stop();
        if (!host_settings_kept(address_of settings) &&
            !host_settings_booted(address_of settings))
                return;

        if (machine && host_machine_hook_line(MOONWATER_HOOK_END))
                return;

        while (host_settings_next(address_of settings, address_of at, address_of setting))
        {
                p8 text[SPARK_SETTINGS_TEXT_MOST + 1];
                p8 ending[48];
                p64 spent = system_clock_ns(HOST_CLOCK_BOOTTIME) - started;
                p64 limit = HOST_EXIT_ALL_NS - spent;
                bool stopped = false;
                positive status;
                bipolar child;

                if (setting.entry.list != SPARK_SETTINGS_EXIT)
                        continue;

                if (spent >= HOST_EXIT_ALL_NS)
                {
                        host_say(log_error, host_label "exit: thirty seconds are spent, "
                                                       "and the rest do not run\n");
                        break;
                }

                host_settings_text(text, address_of setting);
                host_say(log, host_label "exit %p: %s\n", (positive)setting.entry.id, text);

                //      The environment init's entries get, not the stopping
                //      shell's: a bound poweroff has almost none.
                child = host_event_start(text, -1);
                if (child < 0)
                        continue;

                status = host_event_wait(child, limit < HOST_EXIT_EACH_NS ? limit
                                                                          : HOST_EXIT_EACH_NS,
                                         address_of stopped);
                host_event_ending(ending, sizeof(ending), status);

                if (stopped)
                        string_format(log_error, host_label "exit %p did not finish in time "
                                                            "and was killed\n",
                                      (positive)setting.entry.id);
                else if (status)
                        string_format(log_error, host_label "exit %p %s\n",
                                      (positive)setting.entry.id, ending);
                log_flush();
        }
}

/*
        Canvas, off and on.

        Reading needs nothing; the kernel decides who may turn it off or on.
        Off is usually typed into a terminal of the desktop, which closes under it, so
        the way back is said first and the hangup that closing sends is
        ignored. Where the kernel console is not a screen -- console=ttyS0,
        with init's shell on the serial line -- the text console off leaves
        would have no shell, so one is started on tty1.
*/
static bipolar host_canvas_request(positive request,
                                   struct canvas_control address_to control)
{
        memory_zero(control, sizeof(address_to control));
        control->request = (unsigned int)request;
        return host_spark_once(SPARK_IOCTL_CANVAS, control, FILE_READ);
}

static fn host_canvas_write(bool label,
                            struct canvas_control address_to control)
{
        host_prefix(label);
        if (!control->running)
        {
                string_format(log, "Canvas is off; moonwater desktop on starts it\n");
                return;
        }

        string_format(log, "Canvas is on: %s\n", (string_address)control->driver);

        for (positive at = 0; at < control->output_count && at < SPARK_CANVAS_OUTPUTS; at++)
        {
                host_prefix(label);
                string_format(log, "  %s: %p by %p, %p Hz\n",
                              (string_address)control->output[at].connector,
                              (positive)control->output[at].width,
                              (positive)control->output[at].height,
                              (positive)control->output[at].refresh);
        }

        if (control->suspended)
        {
                host_prefix(label);
                string_format(log, "the display is held by the desktop or another "
                                   "program; Control-Alt-Backspace ends it\n");
        }

        host_prefix(label);
        string_format(log, "the desktop is drawn at %p times the size, modes %s\n",
                      (positive)control->scale,
                      control->modes == SPARK_CANVAS_MODES_PREFERRED
                          ? "preferred" : "largest");
}

static fn host_canvas_say(struct canvas_control address_to control)
{
        host_canvas_write(true, control);
        log_flush();
}

/* Whether the kernel console is a screen, where init's shell is on tty1. */
static bool host_console_is_screen(void)
{
        p8 active[128];

        if (host_read_kernel("/sys/class/tty/console/active", active, sizeof(active)) <= 0)
                return true;

        for (string_address at = (string_address)active; *at;)
        {
                if (string_has_prefix(at, "tty") && byte_is_digit(at[3]))
                        return true;

                at = string_first_of_or_end(at, ' ');
                at += string_span_of_set(at, " ");
        }

        return false;
}

/* Whether a process that is not gone is on tty1: its controlling terminal is 4:1. */
static bool host_tty1_held(void)
{
        static system_snapshot sample;
        bool held = false;

        if (!system_snapshot_take(address_of sample, SPARK_SNAPSHOT_PROCESS, false))
                return false;

        for (positive at = 0; at < sample.header.process_count; at++)
                held |= sample.processes[at].tty == (4 << 8 | 1) &&
                        sample.processes[at].state != 'Z';

        return held;
}

/* A shell on tty1, in a session of its own, for a console that has none. */
static fn host_tty1_shell(void)
{
        string_address argv[] = {HOST_EVENT_SHELL, null};

        //      One is enough: each desktop off over a console that is not a
        //      screen forked another, and none ended, so after a few round
        //      trips tty1 had as many shells splitting its keys.
        if (!host_tty1_held())
                host_shell_start(argv, 1, "/dev/tty1", -1);
}

static b32 locale_canvas_setting(string_address address_to arguments, positive count);
static b32 locale_canvas_cursor(string_address address_to arguments, positive count);

/*
        Canvas off: every window closes and the console is the text one. What `moonwater desktop off` does after it has
        ended the session of a profile; the machine script's own `desktop_stop` runs from the event this fires.
*/
static b32 host_canvas_off(void)
{
        struct canvas_control control;
        bipolar failed;

        //      The banner is for a desktop about to close: said over a
        //      text console, without the right to close one, or with no
        //      Canvas in the kernel, it made the refusal after it a lie.
        if (!bowl_is_root())
                return host_refuse("turning Canvas off needs root (CAP_SYS_ADMIN)\n");
        failed = host_canvas_request(SPARK_CANVAS_STATUS, address_of control);
        return_if(failed < 0, host_fail(SPARK_DEVICE, failed));
        if (!control.running)
                return host_refuse("Canvas is already off\n");

        //      A pipe closed on the banner (`desktop off | true`) must not
        //      end this before the desktop is; the signal is put back at once,
        //      since the console shell started below would inherit it ignored.
        positive piped[4];
        bool quiet = system_signal_install(13, 1, 0, 0, piped);

        host_say(log, host_label "Canvas off: every window closes, this one too. "
                                 "On the text console, moonwater desktop on "
                                 "brings the desktop back.\n");
        if (quiet)
                system_signal_action(13, piped, null, 8);

        system_signal_install(1, 1, 0, 0, null);

        failed = host_canvas_request(SPARK_CANVAS_OFF, address_of control);
        if (failed < 0)
                system_signal_install(1, 0, 0, 0, null);
        if (failed == -EPERM)
                return host_refuse("turning Canvas off needs root (CAP_SYS_ADMIN)\n");
        if (failed == -EALREADY)
                return host_refuse("Canvas is already off\n");
        return_if(failed < 0, host_fail("desktop off", failed));

        //      The hangup stays ignored until the console shell has
        //      forked: the terminal this runs on may be the one that
        //      closed. The shell resets it itself, before it execs.
        if (!host_console_is_screen())
                host_tty1_shell();
        system_signal_install(1, 0, 0, 0, null);

        return 0;
}

// Canvas on: the desktop's machine script starts the compositor from the event this fires.
static b32 host_canvas_on(void)
{
        struct canvas_control control;
        bipolar failed;

        failed = host_canvas_request(SPARK_CANVAS_ON, address_of control);
        if (failed == -EPERM)
                return host_refuse("turning Canvas on needs root (CAP_SYS_ADMIN)\n");
        if (failed == -EALREADY)
                return host_refuse("Canvas is already on\n");
        if (failed == -EBUSY)
        {
                if (control.master_command[0] && !control.master_pid)
                        string_format(log_error, host_label "%s, outside this process namespace, "
                                                            "holds the display; "
                                                            "Canvas stays off until it lets go\n",
                                      (string_address)control.master_command);
                else if (control.master_command[0])
                        string_format(log_error, host_label "%s (pid %p) holds the display; "
                                                            "Canvas stays off until it lets go\n",
                                      (string_address)control.master_command,
                                      (positive)control.master_pid);
                else
                        string_format(log_error, host_label "another program holds the display; "
                                                            "Canvas stays off until it lets go\n");
                log_flush();
                return 1;
        }
        if (failed == -ENODEV)
                return host_refuse("there is no display for Canvas to start on\n");
        return_if(failed < 0, host_fail("desktop on", failed));

        host_canvas_say(address_of control);
        return 0;

}

/* moonwater canvas [scale [N|auto]|modes [largest|preferred]|
   background [COLOUR|default]|cursor [size [N|auto]|look [light|dark|auto]]] */
static b32 host_canvas(string_address address_to arguments, positive count)
{
        struct canvas_control control;
        bipolar failed;

        //      The two settings are the machine's, kept with the others in
        //      /root: they are asked and set in the locale's section.
        if (count >= 3 && (string_equals(arguments[2], "scale") ||
                           string_equals(arguments[2], "modes")))
                return locale_canvas_setting(arguments, count);
        if (count >= 3 && string_equals(arguments[2], "cursor"))
                return locale_canvas_cursor(arguments, count);

        return_if(count > 3, host_usage());

        if (count < 3)
        {
                failed = host_canvas_request(SPARK_CANVAS_STATUS, address_of control);
                return_if(failed < 0, host_fail(SPARK_DEVICE, failed));

                host_canvas_say(address_of control);
                return 0;
        }

        // Turning Canvas on and off is the desktop's: moonwater desktop on|off.
        return host_usage();
}

/*
        moonwater latency [reset]: where a report from a mouse or a keyboard
        spends its time on this machine, as far as the machine can see it.

        The two input handlers Moonwater attaches to every device are counted
        by the kernel and one report in sixteen is timed, so what each costs a
        report is a figure here and not a guess: Canvas's, which hears the
        modifiers and Backspace, the chords that end a desktop, and returns at
        once for every other report whoever has the display, and the machine's
        key watch, which fires the bound events. The stages
        after the kernel (the compositor, the screen) are not the machine's
        to see. reset
        zeroes the counts, so a minute of moving a mouse is a reset, the
        minute and a bare read.
*/
static fn latency_cost_say(string_address name, const struct latency_cost address_to cost,
                           positive elapsed_ms)
{
        string_format(log, host_label "  %s: %p reports", name, (positive)cost->events);
        if (elapsed_ms)
                string_format(log, " (%p a second)", (positive)(cost->events * 1000 / elapsed_ms));
        if (cost->samples)
                string_format(log, ", %p ns each and %p at worst, from %p timed\n",
                              (positive)(cost->total_ns / cost->samples),
                              (positive)cost->worst_ns, (positive)cost->samples);
        else
                string_format(log, ", none timed yet\n");
}

static b32 host_latency(string_address address_to arguments, positive count)
{
        struct latency_stats stats;
        bipolar failed;
        positive elapsed_ms;
        bool reset = count == 3 && string_equals(arguments[2], "reset");

        return_if(count > 3 || (count == 3 && !reset), host_usage());
        host_need_root();

        memory_zero(address_of stats, sizeof(stats));
        stats.request = reset ? SPARK_LATENCY_RESET : SPARK_LATENCY_READ;
        failed = host_spark_once(SPARK_IOCTL_LATENCY, address_of stats, FILE_READ);
        return_if(failed == -EPERM, host_refuse("moonwater latency needs root (CAP_SYS_ADMIN)\n"));
        return_if(failed < 0, host_fail(SPARK_DEVICE, failed));

        if (reset)
        {
                host_say(log, host_label "counts zeroed; moonwater latency reads them\n");
                return 0;
        }

        elapsed_ms = (positive)(stats.elapsed_ns / 1000000);
        string_format(log, host_label "input reports, over %p s since the counts began:\n",
                      (positive)(elapsed_ms / 1000));
        latency_cost_say("Canvas's handler", address_of stats.canvas, elapsed_ms);
        latency_cost_say("the key watch", address_of stats.bind, elapsed_ms);

        string_format(log, host_label "Canvas's handler returned at once for %p of its reports "
                                      "(every one that is no modifier or Backspace), with no lock "
                                      "taken and nothing woken%s\n",
                      (positive)stats.quiet,
                      stats.yielded ? "; another program has the display" : "");

        log_flush();
        return 0;
}

/*
        moonwater bios: restart into the firmware's own setup screen.

        UEFI firmware asks for it through one bit. OsIndicationsSupported
        says whether the firmware has a setup screen it can start at the next
        boot (bit 0), and OsIndications is where the operating system sets
        that bit; the firmware acts on it and clears it. Both are EFI
        variables, files of the efivarfs filesystem: four bytes of attributes,
        then the value. A machine that did not start from UEFI has neither, and
        nothing here could take it to a setup screen, so that is said instead
        of a reboot that lands where it always did.

        The variable is written last, and the machine is stopped through the
        same path poweroff and reboot take, so the disks are synced and what
        `moonwater bind exit` runs has run. If the machine does not stop, the
        bit is left set and the next restart of any kind goes to setup.
*/
#define HOST_EFI_DIRECTORY "/sys/firmware/efi/efivars"
#define HOST_EFI_SUPPORTED HOST_EFI_DIRECTORY "/OsIndicationsSupported-8be4df61-93ca-11d2-aa0d-00e098032b8c"
#define HOST_EFI_INDICATIONS HOST_EFI_DIRECTORY "/OsIndications-8be4df61-93ca-11d2-aa0d-00e098032b8c"
#define HOST_EFI_BOOT_TO_SETUP 1u
//      Non-volatile, boot services and runtime: what the firmware's own
//      OsIndications carries, and what it refuses a write without.
#define HOST_EFI_ATTRIBUTES 7u
#define HOST_FS_GETFLAGS 0x80086601u
#define HOST_FS_SETFLAGS 0x40086602u
#define HOST_FS_IMMUTABLE 0x10u

static bipolar host_efi_get(string_address path, p64 address_to value)
{
        p8 bytes[16];
        bipolar got = file_slurp_once_at(AT_FDCWD, path, bytes, sizeof(bytes));

        if (got < 0)
                return got;
        if (got != 12)
                return -ERROR_INVALID;
        memory_copy(value, bytes + 4, 8);
        return 0;
}

/* Whether the firmware offers its setup at the next boot: 1 yes, 0 not, and
   negative when this is not UEFI or the variables cannot be read. */
static bipolar host_efi_setup_offered(void)
{
        p64 value = 0;
        bipolar handle = system_open_at(AT_FDCWD, "/sys/firmware/efi", FILE_READ | O_CLOEXEC);
        bipolar failed;

        if (handle < 0)
                return -ENODEV;
        system_close((positive)handle);

        failed = host_efi_get(HOST_EFI_SUPPORTED, address_of value);
        if (failed < 0)
        {
                //      A machine that has not mounted efivarfs yet.
                system_mount("efivarfs", HOST_EFI_DIRECTORY, "efivarfs", 0, 0);
                failed = host_efi_get(HOST_EFI_SUPPORTED, address_of value);
        }
        if (failed < 0)
                return failed;
        return (value & HOST_EFI_BOOT_TO_SETUP) != 0;
}

static b32 host_bios_set(void)
{
        p64 indications = 0;
        p8 image[12];
        p32 attributes = HOST_EFI_ATTRIBUTES;
        bipolar handle;
        positive flags = 0;

        //      The bits the firmware already had set stay set.
        (void)host_efi_get(HOST_EFI_INDICATIONS, address_of indications);
        indications |= HOST_EFI_BOOT_TO_SETUP;
        memory_copy(image, address_of attributes, 4);
        memory_copy(image + 4, address_of indications, 8);

        //      efivarfs marks a variable immutable, and a write to one is
        //      refused until the mark is cleared; a variable that does not
        //      exist yet has nothing to clear.
        handle = system_open_at(AT_FDCWD, HOST_EFI_INDICATIONS, FILE_READ | O_CLOEXEC);
        if (handle >= 0)
        {
                if (system_call_3(syscall(ioctl), (positive)handle, HOST_FS_GETFLAGS,
                                  (positive)address_of flags) == 0 &&
                    (flags & HOST_FS_IMMUTABLE))
                {
                        flags &= ~(positive)HOST_FS_IMMUTABLE;
                        system_call_3(syscall(ioctl), (positive)handle, HOST_FS_SETFLAGS,
                                      (positive)address_of flags);
                }
                system_close((positive)handle);
        }

        handle = system_open_at_mode(AT_FDCWD, HOST_EFI_INDICATIONS,
                                     O_WRONLY | FILE_CREATE | O_CLOEXEC | O_NOFOLLOW, 0644);
        return_if(handle < 0, host_fail("firmware setup", handle));
        if (system_write_all((positive)handle, image, sizeof(image)) != sizeof(image))
        {
                system_close((positive)handle);
                return host_fail("firmware setup", -EIO);
        }
        system_close((positive)handle);
        return 0;
}

/* moonwater bios [reboot] */
static b32 host_bios(string_address address_to arguments, positive count)
{
        bipolar offered;

        if (count > 3 || (count == 3 && !string_equals(arguments[2], "reboot")))
                return host_usage();

        offered = host_efi_setup_offered();

        if (count == 2)
        {
                if (offered > 0)
                        string_format(log, host_label "UEFI firmware with a setup screen; "
                                                     "moonwater bios reboot restarts into it\n");
                else if (offered == 0)
                        string_format(log, host_label "UEFI firmware, but it offers no setup "
                                                     "screen at the next boot\n");
                else if (offered == -ENODEV)
                        string_format(log, host_label "this machine did not start from UEFI "
                                                     "firmware, so there is no setup to restart into\n");
                else
                        string_format(log, host_label "UEFI firmware, but its variables cannot "
                                                     "be read here (error %p)\n",
                                      (positive)-offered);
                log_flush();
                return 0;
        }

        host_need_root();
        if (offered == -ENODEV)
                return host_refuse("this machine did not start from UEFI firmware, so "
                                   "there is no setup to restart into\n");
        return_if(offered < 0, host_fail("firmware setup", offered));
        if (!offered)
                return host_refuse("this firmware offers no setup screen at the next boot\n");

        if (host_bios_set())
                return 1;

        host_say(log, host_label "restarting into the firmware's setup\n");
        shell_stop(log, REBOOT_RESTART);

        //      Only reached when the machine did not stop.
        return 1;
}

/* ---- radio: wifi and bluetooth, and the nl80211 they ask the kernel through. ---- */

/*
        Wireless and bluetooth, as moonwater verbs.

        Secrets stay on /root so an image update does not take the password
        with it. /ip watch still owns the address: this only joins the radio
        and says which link to prefer when both have carrier.
*/

/* ---- nl80211: how the wifi above talks to the kernel. ---- */

/* ---- nl80211: join a station, leave it ---- */

#ifndef STANDARD_MODERN_C_NET_NL80211
#define STANDARD_MODERN_C_NET_NL80211

#define GENL_ID_CTRL 16
#define GENL_HEADER 4
#define CTRL_CMD_GETFAMILY 3
#define CTRL_ATTR_FAMILY_ID 1
#define CTRL_ATTR_FAMILY_NAME 2
#define CTRL_ATTR_MCAST_GROUPS 7
#define CTRL_ATTR_MCAST_GRP_NAME 1
#define CTRL_ATTR_MCAST_GRP_ID 2

#define NL80211_CMD_GET_WIPHY 1
#define NL80211_CMD_NEW_WIPHY 3
#define NL80211_CMD_GET_INTERFACE 5
#define NL80211_CMD_NEW_INTERFACE 7
#define NL80211_CMD_NEW_KEY 11
#define NL80211_CMD_GET_STATION 17
#define NL80211_CMD_SET_STATION 18
#define NL80211_CMD_NEW_STATION 19
#define NL80211_CMD_CONNECT 46
#define NL80211_CMD_AUTHENTICATE 37
#define NL80211_CMD_ASSOCIATE 38
#define NL80211_CMD_DEAUTHENTICATE 39
#define NL80211_CMD_DISASSOCIATE 40
#define NL80211_CMD_DISCONNECT 48
#define NL80211_CMD_SET_REKEY_OFFLOAD 79

#define NL80211_ATTR_WIPHY 1
#define NL80211_ATTR_IFINDEX 3
#define NL80211_ATTR_IFNAME 4
#define NL80211_ATTR_IFTYPE 5
#define NL80211_ATTR_MAC 6
#define NL80211_ATTR_KEY_DATA 7
#define NL80211_ATTR_KEY_IDX 8
#define NL80211_ATTR_KEY_CIPHER 9
#define NL80211_ATTR_KEY_SEQ 10
#define NL80211_ATTR_KEY_DEFAULT 11
#define NL80211_ATTR_WIPHY_FREQ 38
#define NL80211_ATTR_IE 42
#define NL80211_ATTR_SCAN_FREQUENCIES 44
#define NL80211_ATTR_SCAN_SSIDS 45
#define NL80211_ATTR_FRAME 51
#define NL80211_ATTR_SSID 52
#define NL80211_ATTR_AUTH_TYPE 53
#define NL80211_ATTR_REASON_CODE 54
#define NL80211_ATTR_KEY_TYPE 55
#define NL80211_ATTR_TIMED_OUT 65
#define NL80211_ATTR_USE_MFP 66
#define NL80211_ATTR_STA_FLAGS2 67
#define NL80211_ATTR_CONTROL_PORT 68
#define NL80211_ATTR_PRIVACY 70
#define NL80211_ATTR_STATUS_CODE 72
#define NL80211_ATTR_CIPHER_SUITES_PAIRWISE 73
#define NL80211_ATTR_CIPHER_SUITE_GROUP 74
#define NL80211_ATTR_WPA_VERSIONS 75
#define NL80211_ATTR_AKM_SUITES 76
#define NL80211_ATTR_REKEY_DATA 122
#define NL80211_REKEY_DATA_KEK 1
#define NL80211_REKEY_DATA_KCK 2
#define NL80211_REKEY_DATA_REPLAY_CTR 3
#define NL80211_ATTR_AUTH_DATA 156
#define NL80211_ATTR_SCAN_FLAGS 158
#define NL80211_ATTR_SPLIT_WIPHY_DUMP 174
#define NL80211_ATTR_EXT_FEATURES 217
#define NL80211_ATTR_PMK 254

#define NL80211_IFTYPE_ADHOC 1
#define NL80211_IFTYPE_STATION 2
#define NL80211_IFTYPE_P2P_CLIENT 8
#define NL80211_SCAN_FLAG_FLUSH 2
#define NL80211_AUTHTYPE_OPEN 0
#define NL80211_AUTHTYPE_SAE 4
#define NL80211_MFP_REQUIRED 1
#define NL80211_WPA_VERSION_2 2
#define NL80211_KEYTYPE_GROUP 0
#define NL80211_KEYTYPE_PAIRWISE 1
#define NL80211_STA_FLAG_AUTHORIZED 1
#define NL80211_EXT_FEATURE_4WAY_HANDSHAKE_STA_PSK 15
#define WLAN_CIPHER_CCMP 0x000fac04u
#define WLAN_CIPHER_BIP_CMAC_128 0x000fac06u
#define WLAN_SUITE_IEEE 0x000fac00u
#define NL80211_CONNECT_SECONDS 20
#define WIFI_EAPOL_SECONDS 8
#define WIFI_EAPOL_HDR 99
#define PACKET_HOST 0

typedef struct
{
        b32 handle;
        p16 family;
        p32 mlme;
        p32 scan;
} nl80211;

/* One station interface: what a join asks of the radio it is on. */
typedef struct
{
        p32 index;
        p32 wiphy;
        bool has_mac;
        p8 mac[6];
        p8 name[IFNAME_SIZE];
} nl80211_card;

#define NL80211_CARDS 4

/* The wireless interface the machine joins by: the first station the kernel
   lists, or, with more than one, the one that has a station of its own, which
   is the one that is joined. */
typedef struct
{
        p32 index;
        p32 wiphy;
        bool found;
        bool has_mac;
        p8 mac[6];
        p8 name[IFNAME_SIZE];
        p8 cards;
        nl80211_card card[NL80211_CARDS];
        p8 others;
        p32 other[8];
} nl80211_iface;

typedef struct
{
        p32 wiphy;
        p32 seen;
        bool offload;
} nl80211_wiphy_query;

static COLD bipolar nl80211_disconnect(nl80211 address_to session, p32 index);

typedef struct
{
        p16 family;
        p32 mlme;
        p32 scan;
} nl80211_family_info;

static COLD bool nl80211_begin(netlink_buffer address_to buffer, p16 family, p8 command,
                          p16 flags, p32 sequence)
{
        p8 address_to body;

        if (!netlink_begin(buffer, family, flags, sequence, GENL_HEADER))
                return false;

        body = (p8 address_to)netlink_body(buffer);
        body[0] = command;
        body[1] = 1;
        body[2] = 0;
        body[3] = 0;
        return true;
}

static COLD bool nl80211_attribute_u32(netlink_buffer address_to buffer, p16 type,
                                  p32 value)
{
        return netlink_attribute_add(buffer, type, address_of value, 4);
}

static COLD p32 nl80211_find_u32(netlink_header address_to header, p16 type, p32 missing)
{
        positive size = 0;
        p8 address_to at = (p8 address_to)netlink_find(header, GENL_HEADER, type,
                                                       address_of size);

        if (!at || size < 4)
                return missing;
        return memory_load_unaligned(p32, at);
}

static COLD p16 nl80211_find_u16(netlink_header address_to header, p16 type,
                            p16 missing)
{
        positive size = 0;
        p8 address_to at = (p8 address_to)netlink_find(header, GENL_HEADER, type,
                                                       address_of size);

        if (!at || size < 2)
                return missing;
        return (p16)memory_load_unaligned(p16, at);
}

static COLD bool nl80211_attr(netlink_header address_to header, p16 type)
{
        return netlink_find(header, GENL_HEADER, type, null) != null;
}

/* The command of a generic netlink message, 0 for one too short to hold the
   header; no message of the family's has command 0. */
static COLD p8 nl80211_command(netlink_header address_to header)
{
        return header->length < NETLINK_HEADER + GENL_HEADER
                   ? 0
                   : ((p8 address_to)header)[NETLINK_HEADER];
}

/* Whether a hardware address is one: all zeros is what none is. */
static COLD bool wifi_mac_set(p8 address_to mac)
{
        return memory_span_byte(mac, 0, 6) != 6;
}

/* The command of a message of nl80211's about this link, and 0 for any
   other: the news on the mlme and scan sockets is every interface's, and
   only this one's counts. */
static COLD p8 nl80211_news(nl80211 address_to session,
                            netlink_header address_to header, p32 index)
{
        return header->type == session->family &&
                       nl80211_find_u32(header, NL80211_ATTR_IFINDEX, 0) == index
                   ? nl80211_command(header)
                   : 0;
}

/* One multicast group of the family: its name and the id to join it by. */
static COLD bool nl80211_group_seen(p16 type, p8 address_to value, positive size,
                                    address_any context)
{
        nl80211_family_info address_to info = (nl80211_family_info address_to)context;
        positive name_length = 0;
        positive id_length = 0;
        p8 address_to name = (p8 address_to)netlink_find_span(
            value, size, CTRL_ATTR_MCAST_GRP_NAME, address_of name_length);
        p8 address_to id = (p8 address_to)netlink_find_span(
            value, size, CTRL_ATTR_MCAST_GRP_ID, address_of id_length);

        (void)type;
        if (!name || name_length < 5 || !id || id_length < 4)
                return true;
        if (!memory_compare(name, "mlme", 5))
                info->mlme = memory_load_unaligned(p32, id);
        else if (!memory_compare(name, "scan", 5))
                info->scan = memory_load_unaligned(p32, id);
        return true;
}

static COLD bool nl80211_family_seen(netlink_header address_to header,
                                address_any context)
{
        nl80211_family_info address_to info = (nl80211_family_info address_to)context;
        positive size = 0;
        positive groups_length = 0;
        p8 address_to at = (p8 address_to)netlink_find(header, GENL_HEADER,
                                                       CTRL_ATTR_FAMILY_ID,
                                                       address_of size);
        p8 address_to groups;

        if (at && size >= 2)
                info->family = memory_load_unaligned(p16, at);

        groups = (p8 address_to)netlink_find(header, GENL_HEADER,
                                             CTRL_ATTR_MCAST_GROUPS,
                                             address_of groups_length);
        if (groups)
                netlink_attributes(groups, groups_length, nl80211_group_seen, info);

        return false;
}

static COLD bipolar nl80211_open(nl80211 address_to session)
{
        netlink_buffer request = {0};
        p32 sequence;
        p8 name[] = "nl80211";
        bipolar handle;
        nl80211_family_info info = {0};

        memory_fill(session, 0, sizeof(*session));
        session->handle = -1;

        handle = netlink_open_protocol(NETLINK_GENERIC, 0);
        if (handle < 0)
                return handle;

        bool kept = false;
        scope_exit(if (!kept) socket_close((b32)handle));
        sequence = netlink_sequence_take();
        if (!nl80211_begin(address_of request, GENL_ID_CTRL, CTRL_CMD_GETFAMILY,
                           NLM_REQUEST | NLM_ACK, sequence))
                return -1;

        netlink_attribute_add(address_of request, CTRL_ATTR_FAMILY_NAME, name,
                              sizeof(name));

        if (netlink_transact((b32)handle, address_of request, sequence,
                             nl80211_family_seen, address_of info) < 0 ||
            !info.family)
                return -19;

        if (info.mlme &&
            socket_option_set((b32)handle, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP,
                              address_of info.mlme, sizeof(info.mlme)) >= 0)
                session->mlme = info.mlme;

        kept = true;
        session->handle = (b32)handle;
        session->family = info.family;
        session->scan = info.scan;
        return 0;
}

static COLD fn nl80211_close(nl80211 address_to session)
{
        if (session->handle >= 0)
                socket_close(session->handle);
        session->handle = -1;
}

static COLD bool nl80211_iface_seen(netlink_header address_to header,
                               address_any context)
{
        nl80211_iface address_to found = (nl80211_iface address_to)context;
        p8 command = nl80211_command(header);
        nl80211_card address_to card;
        p32 type;
        p32 index;
        positive length = 0;
        positive mac_length = 0;
        string_address name;
        p8 address_to mac;

        if (command != NL80211_CMD_NEW_INTERFACE &&
            command != NL80211_CMD_GET_INTERFACE)
                return true;

        /* A station, and only a station: an access point, a monitor or a
           P2P device on the same machine is not one to join from, and the
           first of those in the dump used to be taken when no station
           followed it. The rest that no lease is for are counted too. */
        type = nl80211_find_u32(header, NL80211_ATTR_IFTYPE, 0);
        index = nl80211_find_u32(header, NL80211_ATTR_IFINDEX, 0);
        if (index && type != NL80211_IFTYPE_STATION && type != NL80211_IFTYPE_ADHOC &&
            type != NL80211_IFTYPE_P2P_CLIENT && found->others < 8)
                found->other[found->others++] = index;
        if (type != NL80211_IFTYPE_STATION || !index || found->cards == NL80211_CARDS)
                return true;

        card = found->card + found->cards++;
        card->index = index;
        card->wiphy = nl80211_find_u32(header, NL80211_ATTR_WIPHY, 0);
        name = (string_address)netlink_find(header, GENL_HEADER, NL80211_ATTR_IFNAME,
                                            address_of length);
        if (name && length)
        {
                if (length >= IFNAME_SIZE)
                        length = IFNAME_SIZE - 1;
                memory_copy(card->name, name, length);
                card->name[length] = end;
        }
        mac = (p8 address_to)netlink_find(header, GENL_HEADER, NL80211_ATTR_MAC,
                                          address_of mac_length);
        if (mac && mac_length >= 6)
        {
                memory_copy(card->mac, mac, 6);
                card->has_mac = true;
        }

        return true;
}

static COLD bipolar nl80211_station(nl80211 address_to session, p32 index,
                                    p8 address_to mac);

static COLD bipolar nl80211_interface(nl80211 address_to session,
                                 nl80211_iface address_to found)
{
        netlink_buffer request = {0};
        p32 sequence = netlink_sequence_take();
        positive chosen = 0;
        p8 mac[6];

        memory_fill(found, 0, sizeof(*found));
        if (!nl80211_begin(address_of request, session->family,
                           NL80211_CMD_GET_INTERFACE,
                           NLM_REQUEST | NLM_DUMP, sequence))
                return -1;

        if (netlink_transact(session->handle, address_of request, sequence,
                             nl80211_iface_seen, found) < 0)
                return -1;
        if (!found->cards)
                return -19;

        //      With several radios the one that is joined is the one the
        //      machine is on, whichever the kernel listed first: a pass that
        //      looked at the first alone joined a second time over a link
        //      that stood on the other.
        for (positive at = 0; at < found->cards && found->cards > 1; at++)
                if (nl80211_station(session, found->card[at].index, mac) > 0)
                {
                        chosen = at;
                        break;
                }
        found->found = true;
        found->index = found->card[chosen].index;
        found->wiphy = found->card[chosen].wiphy;
        found->has_mac = found->card[chosen].has_mac;
        memory_copy(found->mac, found->card[chosen].mac, sizeof(found->mac));
        memory_copy(found->name, found->card[chosen].name, sizeof(found->name));
        return 0;
}

/* The wifi links the watcher takes no lease on: access points, monitors,
   anything that is not a station. */
static COLD fn radio_links_unleased(netlink_search address_to search)
{
        nl80211 session;
        nl80211_iface iface;

        if (nl80211_open(address_of session) < 0)
                return;
        nl80211_interface(address_of session, address_of iface);
        memory_copy(search->skip, iface.other, sizeof(search->skip));
        search->skip_count = iface.others;
        nl80211_close(address_of session);
}

/* Whether a password is one WPA takes: eight to sixty-three characters, or
   sixty-four hexadecimal digits, which are the key itself. */
static COLD bool wifi_pass_fits(p8 address_to pass, positive length)
{
        if (length != 64)
                return length >= 8 && length <= 63;
        return !array_any(at, length, digit_known(pass[at], 16) >= 16);
}

static COLD bool wifi_psk(p8 address_to ssid, positive ssid_length,
                     p8 address_to pass, positive pass_length, p8 address_to pmk)
{
        if (!wifi_pass_fits(pass, pass_length))
                return false;
        if (pass_length == 64)
                return memory_from_hex(pmk, (string_address)pass, 32);

        if (!ssid_length || ssid_length > 32)
                return false;

        //      IEEE 802.11i: PBKDF2-HMAC-SHA1 of the passphrase, the SSID as
        //      its salt, 4096 rounds, 256 bits.
        crypto_pbkdf2(DIGEST_SHA1, 20, pass, pass_length, ssid, ssid_length,
                      4096, pmk, 32);
        return true;
}

/*
        The PTK of a handshake: the PMK over the two addresses and the two
        nonces, each pair smaller first. PSK (suite 2) takes 802.11i's
        PRF-512 of HMAC-SHA1, all 64 bytes of it; PSK-SHA256 (6) and SAE (8)
        take the KDF of 802.11-2016 12.7.1.7.2, HMAC-SHA-256 of a counter
        from 1, the label, the data and the length in bits, 384 bits of it.
        Either way the KCK, the KEK and the TK are its first three sixteen
        bytes.
*/
static COLD fn wifi_ptk(p8 address_to pmk, p8 address_to ap, p8 address_to sta,
                   p8 address_to anonce, p8 address_to snonce, p8 address_to ptk,
                   p8 akm)
{
        p8 label[] = "Pairwise key expansion";
        p8 data[6 + 6 + 32 + 32];
        p8 input[22 + 1 + 76 + 1];
        p8 hash[20];
        p8 address_to min_mac;
        p8 address_to max_mac;
        p8 address_to min_nonce;
        p8 address_to max_nonce;
        positive used = 0;
        positive which;

        if (memory_compare(ap, sta, 6) < 0)
        {
                min_mac = ap;
                max_mac = sta;
        }
        else
        {
                min_mac = sta;
                max_mac = ap;
        }
        if (memory_compare(anonce, snonce, 32) < 0)
        {
                min_nonce = anonce;
                max_nonce = snonce;
        }
        else
        {
                min_nonce = snonce;
                max_nonce = anonce;
        }

        memory_copy(data, min_mac, 6);
        memory_copy(data + 6, max_mac, 6);
        memory_copy(data + 12, min_nonce, 32);
        memory_copy(data + 44, max_nonce, 32);
        if (akm != 2)
        {
                p8 block[2 + 22 + 76 + 2];
                p8 sum[32];

                memory_fill(ptk, 0, 64);
                for (which = 1; used < 48; which++)
                {
                        block[0] = (p8)which;
                        block[1] = 0;
                        memory_copy(block + 2, label, 22);
                        memory_copy(block + 24, data, 76);
                        //      384, little endian.
                        block[100] = 0x80;
                        block[101] = 0x01;
                        crypto_hmac_sha256(pmk, 32, block, sizeof(block), sum);
                        memory_copy(ptk + used, sum, used + 32 > 48 ? 48 - used : 32);
                        used += 32;
                }
                crypto_forget(block, sizeof(block));
                crypto_forget(sum, sizeof(sum));
                crypto_forget(data, sizeof(data));
                return;
        }
        memory_copy(input, label, 22);
        input[22] = 0;
        memory_copy(input + 23, data, 76);
        for (which = 0; used < 64; which++)
        {
                input[99] = (p8)which;
                crypto_hmac(DIGEST_SHA1, 20, pmk, 32, input, 100, hash);
                memory_copy(ptk + used, hash, used + 20 > 64 ? 64 - used : 20);
                used += 20;
        }
        crypto_forget(data, sizeof(data));
        crypto_forget(input, sizeof(input));
        crypto_forget(hash, sizeof(hash));
}

/* The key descriptor version an access point's key frames carry for a key
   management suite: 2 (HMAC-SHA1-128) for PSK, 3 (AES-128-CMAC) for
   PSK-SHA256, and 0, "as the suite defines", for SAE, whose MIC is the CMAC
   all the same. */
static COLD p8 wifi_key_version(p8 akm)
{
        return akm == 2 ? 2 : akm == 8 ? 0 : 3;
}

/*
        What an access point's RSN element offers, as far as a join cares:
        its key management suites, management frame protection, the group
        cipher and the pairwise set, each a mask, and whether its RSN
        extension element takes SAE's hash-to-element. The kernel keeps two
        frames of an access point, its latest (a probe response anybody can
        send in its name) and its last beacon, and a join takes what either
        offers: a forged frame that takes an offer away -- protection, WPA3,
        hash-to-element -- would have the join ask for less, while one that
        adds an offer costs a join that a forger can stop anyway. Message 3
        of the handshake is then held to the elements read here
        (wifi_eapol_step).
*/
#define WIFI_AKM_PSK 1
#define WIFI_AKM_PSK_SHA256 2
#define WIFI_AKM_SAE 4
#define WIFI_AKM_OTHER 8
#define WIFI_PMF_CAPABLE 1
#define WIFI_PMF_REQUIRED 2
#define WIFI_CIPHER_CCMP 1
#define WIFI_CIPHER_TKIP 2
#define WIFI_CIPHER_OTHER 4

typedef struct
{
        p8 akms;
        p8 pmf;
        p8 group;
        p8 pairwise;
        p8 h2e;
} wifi_rsn_caps;

static COLD fn wifi_caps_merge(wifi_rsn_caps address_to into, const wifi_rsn_caps address_to from)
{
        into->akms |= from->akms;
        into->pmf |= from->pmf;
        into->group |= from->group;
        into->pairwise |= from->pairwise;
        into->h2e |= from->h2e;
}

/*
        What a saved network has taught the join, or its owner has said of
        it (radio_rules): an access point by its name offered management
        frame protection; it was joined with WPA3; its owner lets the tight
        tier join it with WPA2.
*/
#define WIFI_RULE_PMF 1
#define WIFI_RULE_SAE 2
#define WIFI_RULE_WPA2 4

/* A join's refusals of its own, which it makes having chosen an access
   point and before it sends anything, and the one message 3 can bring:
   below every errno and every -1000-R reason a join reports. */
#define WIFI_REFUSE_TKIP (-70001)
#define WIFI_REFUSE_CIPHER (-70002)
#define WIFI_REFUSE_PMF_GONE (-70003)
#define WIFI_REFUSE_SAE_GONE (-70004)
#define WIFI_REFUSE_NO_PMF (-70005)
#define WIFI_REFUSE_WPA2 (-70006)
#define WIFI_REFUSE_KEY_ONLY (-70007)
#define WIFI_REFUSE_SUITE (-70008)
#define WIFI_REFUSE_SAE_BUDGET (-70009)
#define WIFI_REFUSE_ELEMENTS (-70010)
#define WIFI_REFUSE_FIRST WIFI_REFUSE_ELEMENTS
#define WIFI_REFUSE_LAST WIFI_REFUSE_TKIP
//      The machine's doing, not the network's: no station could be confined
//      here, so nothing was joined; or the station ended without a status.
#define WIFI_REFUSE_CONFINE (-70101)
#define WIFI_REFUSE_APART (-70102)

/* What a join asks of an access point: the key management suite (2, 6 or
   8), the protection it asks for, and the PSK suite it may try when WPA3
   came to nothing for a reason that is not the password, or 0. */
typedef struct
{
        p8 akm;
        p8 pmf;
        p8 fallback;
} wifi_plan;

/*
        The join's choice for an access point that offers caps, at a tier,
        for a saved network with rules, whose secret is a passphrase or a
        key of 64 hex digits (key_only, which is a PMK and no SAE password).
        0 with the plan, or the refusal.

          ciphers   CCMP for the group and among the pairwise ones, or
                    nothing: TKIP (WPA1, and the group cipher of a mixed
                    WPA/WPA2 network) is broken
          memory    an access point of a name that once offered protection
                    is never joined without it, and one joined with WPA3
                    is never joined with WPA2: a twin that offers less is
                    what a forged deauthentication, or a password guessed
                    from a captured handshake, needs
          WPA3      whenever it is offered and there is a password for it,
                    with protection required, and WPA2 after it only when
                    that is offered too, the memory allows it and the tier
                    does
          WPA2      PSK-SHA256 when it is offered with protection, else
                    PSK; protection asked for whenever it is offered, as
                    required, since the kernel takes no optional form from a
                    driver with no connect of its own
          tight     protection always, and WPA2 only for a network whose
                    owner said so (WIFI_RULE_WPA2)
*/
static COLD bipolar wifi_join_plan(positive tier, const wifi_rsn_caps address_to caps,
                                   p8 rules, bool key_only, wifi_plan address_to plan)
{
        bool tight = tier >= STRICT_TIGHT;
        bool pmf = caps->pmf != 0;
        bool sae = (caps->akms & WIFI_AKM_SAE) && !key_only;
        p8 psk = (caps->akms & WIFI_AKM_PSK_SHA256) && (pmf || !(caps->akms & WIFI_AKM_PSK))
                     ? 6
                 : caps->akms & WIFI_AKM_PSK ? 2
                                             : 0;

        memory_fill(plan, 0, sizeof(*plan));
        if (!(caps->group & WIFI_CIPHER_CCMP) || (caps->group & ~WIFI_CIPHER_CCMP) ||
            !(caps->pairwise & WIFI_CIPHER_CCMP))
                return (caps->group | caps->pairwise) & WIFI_CIPHER_TKIP ? WIFI_REFUSE_TKIP
                                                                         : WIFI_REFUSE_CIPHER;
        if (!sae && !psk)
                return caps->akms & WIFI_AKM_SAE ? WIFI_REFUSE_KEY_ONLY : WIFI_REFUSE_SUITE;
        if ((rules & WIFI_RULE_PMF) && !pmf)
                return WIFI_REFUSE_PMF_GONE;
        if ((rules & WIFI_RULE_SAE) && !sae)
                return caps->akms & WIFI_AKM_SAE ? WIFI_REFUSE_KEY_ONLY : WIFI_REFUSE_SAE_GONE;
        if (sae)
        {
                plan->akm = 8;
                plan->pmf = WIFI_PMF_CAPABLE | WIFI_PMF_REQUIRED;
                if (psk && !(rules & WIFI_RULE_SAE) &&
                    (!tight || (pmf && (rules & WIFI_RULE_WPA2))))
                        plan->fallback = psk;
                return 0;
        }
        if (tight && !pmf)
                return WIFI_REFUSE_NO_PMF;
        if (tight && !(rules & WIFI_RULE_WPA2))
                return WIFI_REFUSE_WPA2;
        plan->akm = psk;
        plan->pmf = pmf ? WIFI_PMF_CAPABLE | WIFI_PMF_REQUIRED : 0;
        return 0;
}

/* The RSN element a station offers, in the association request and again
   in message 2, which the access point compares with it: CCMP for both
   ciphers and the one key management suite asked for; with protection,
   the capable and required bits, the PMKID when there is one (SAE's) and
   BIP-CMAC-128 as the group management cipher. With no protection it is
   the element the join always sent. */
static COLD positive wifi_rsn_build(p8 address_to out, p8 akm, p8 pmf, const p8 address_to pmkid)
{
        static const p8 head[] = {0x30, 0x14, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04,
                                  0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00,
                                  0x00, 0x0f, 0xac};
        positive at = sizeof(head);

        memory_copy(out, head, sizeof(head));
        out[at++] = akm;
        out[at++] = (p8)((pmf & WIFI_PMF_CAPABLE ? 0x80 : 0) | (pmf & WIFI_PMF_REQUIRED ? 0x40 : 0));
        out[at++] = 0;
        if (!(pmf & WIFI_PMF_CAPABLE))
                return at;
        out[at++] = pmkid ? 1 : 0;
        out[at++] = 0;
        if (pmkid)
        {
                memory_copy(out + at, pmkid, 16);
                at += 16;
        }
        out[at++] = 0x00;
        out[at++] = 0x0f;
        out[at++] = 0xac;
        out[at++] = 0x06;
        out[1] = (p8)(at - 2);
        return at;
}

/*
        The access point's RSN element and RSN extension element as the
        frames a join chose it from showed them, whole, element id and
        length first: [0] its latest frame's, [1] its last beacon's where
        the kernel kept that apart. A handshake's message 3 carries the
        access point's own, under the MIC of a key only it and this station
        have: one that matches neither is not what the join was decided
        from, which a forged beacon or probe response that took an offer
        away makes, and it is refused (802.11-2020 12.7.6.4, as
        wpa_supplicant refuses it).
*/
#define WIFI_ELEMENT_ROOM (2 + 255)

typedef struct
{
        p8 rsn[2][WIFI_ELEMENT_ROOM];
        p8 rsnx[2][WIFI_ELEMENT_ROOM];
        p16 rsn_length[2];
        p16 rsnx_length[2];
} wifi_seen;

/* Whether an element of message 3 is one of the two seen (none, when
   neither frame had one). */
static COLD bool wifi_seen_one(p8 (address_to seen)[WIFI_ELEMENT_ROOM],
                               const p16 address_to lengths, const p8 address_to got,
                               positive length)
{
        if (!lengths[0] && !lengths[1])
                return !length;
        for (positive at = 0; at < 2; at++)
                if (lengths[at] && lengths[at] == length && !memory_compare(seen[at], got, length))
                        return true;
        return false;
}

/* Whether message 3's RSN element is one the access point showed, and its
   RSN extension element too, or none where neither frame had one: an
   extension element stripped from what the station saw would have it hunt
   for the password element where the access point takes hash-to-element.
   A join that saw no RSN element at all (no scan could be had) has nothing
   to hold it to. */
static COLD bool wifi_seen_matches(wifi_seen address_to seen, const p8 address_to rsn,
                                   positive rsn_length, const p8 address_to rsnx,
                                   positive rsnx_length)
{
        if (!seen->rsn_length[0] && !seen->rsn_length[1])
                return true;
        return wifi_seen_one(seen->rsn, seen->rsn_length, rsn, rsn_length) &&
               wifi_seen_one(seen->rsnx, seen->rsnx_length, rsnx, rsnx_length);
}

static COLD bool nl80211_ext_bit(p8 address_to bits, positive length, positive which)
{
        return which / 8 < length && (bits[which / 8] & (1u << (which % 8)));
}

static COLD bool nl80211_wiphy_seen(netlink_header address_to header, address_any context)
{
        nl80211_wiphy_query address_to query = (nl80211_wiphy_query address_to)context;
        p8 command = nl80211_command(header);
        positive size = 0;
        p8 address_to bits;
        p32 wiphy;

        if (command != NL80211_CMD_NEW_WIPHY && command != NL80211_CMD_GET_WIPHY)
                return true;

        wiphy = nl80211_find_u32(header, NL80211_ATTR_WIPHY, query->seen);
        if (nl80211_attr(header, NL80211_ATTR_WIPHY))
                query->seen = wiphy;
        if (wiphy != query->wiphy)
                return true;

        bits = (p8 address_to)netlink_find(
            header, GENL_HEADER, NL80211_ATTR_EXT_FEATURES, address_of size);
        if (bits && nl80211_ext_bit(bits, size,
                                    NL80211_EXT_FEATURE_4WAY_HANDSHAKE_STA_PSK))
                query->offload = true;
        return true;
}

static COLD bool nl80211_psk_offload(nl80211 address_to session, p32 wiphy)
{
        netlink_buffer request = {0};
        p32 sequence;
        nl80211_wiphy_query query = {.wiphy = wiphy, .seen = ~0u};

        sequence = netlink_sequence_take();
        if (!nl80211_begin(address_of request, session->family,
                           NL80211_CMD_GET_WIPHY, NLM_REQUEST | NLM_DUMP,
                           sequence))
                return false;
        nl80211_attribute_u32(address_of request, NL80211_ATTR_WIPHY, wiphy);
        //      A dump that is not split is the wiphy as the first nl80211 told
        //      it, and has none of its extended features: iw asks for split
        //      too, and a driver's offload was never heard of without it.
        netlink_attribute_add(address_of request, NL80211_ATTR_SPLIT_WIPHY_DUMP, null, 0);
        if (netlink_transact(session->handle, address_of request, sequence,
                             nl80211_wiphy_seen, address_of query) < 0)
                return false;
        return query.offload;
}

static COLD bool nl80211_station_seen(netlink_header address_to header,
                                 address_any context)
{
        p8 command = nl80211_command(header);
        p8 address_to into = (p8 address_to)context;
        positive length = 0;
        p8 address_to mac;

        if (command != NL80211_CMD_NEW_STATION &&
            command != NL80211_CMD_GET_STATION)
                return true;
        mac = (p8 address_to)netlink_find(header, GENL_HEADER, NL80211_ATTR_MAC,
                                          address_of length);
        if (mac && length >= 6)
        {
                memory_copy(into, mac, 6);
                return false;
        }
        return true;
}

/* Whether the link has a station, and its address in mac: 1 when it has one,
   0 when the kernel was asked and has none, -1 when the asking failed, which
   says nothing of the link. */
static COLD bipolar nl80211_station(nl80211 address_to session, p32 index,
                                    p8 address_to mac)
{
        netlink_buffer request = {0};
        p32 sequence = netlink_sequence_take();

        memory_fill(mac, 0, 6);
        if (!nl80211_begin(address_of request, session->family,
                           NL80211_CMD_GET_STATION, NLM_REQUEST | NLM_DUMP,
                           sequence))
                return -1;
        nl80211_attribute_u32(address_of request, NL80211_ATTR_IFINDEX, index);
        if (netlink_transact(session->handle, address_of request, sequence,
                             nl80211_station_seen, mac) < 0)
                return -1;
        return wifi_mac_set(mac) ? 1 : 0;
}

static COLD bipolar nl80211_new_key(nl80211 address_to session, p32 index, p8 idx,
                               p32 type, p8 address_to mac, p8 address_to key,
                               positive key_length, p8 address_to seq,
                               positive seq_length, bool group_default, p32 cipher)
{
        netlink_buffer request = {0};
        p32 sequence = netlink_sequence_take();

        if (!nl80211_begin(address_of request, session->family, NL80211_CMD_NEW_KEY,
                           NLM_REQUEST | NLM_ACK, sequence))
                return -1;
        nl80211_attribute_u32(address_of request, NL80211_ATTR_IFINDEX, index);
        netlink_attribute_add(address_of request, NL80211_ATTR_KEY_DATA, key,
                              key_length);
        netlink_attribute_add(address_of request, NL80211_ATTR_KEY_IDX, address_of idx,
                              1);
        nl80211_attribute_u32(address_of request, NL80211_ATTR_KEY_CIPHER, cipher);
        nl80211_attribute_u32(address_of request, NL80211_ATTR_KEY_TYPE, type);
        if (mac)
                netlink_attribute_add(address_of request, NL80211_ATTR_MAC, mac, 6);
        if (seq && seq_length)
                netlink_attribute_add(address_of request, NL80211_ATTR_KEY_SEQ, seq,
                                      seq_length);
        if (group_default)
                netlink_attribute_add(address_of request, NL80211_ATTR_KEY_DEFAULT,
                                      null, 0);
        return netlink_transact(session->handle, address_of request, sequence,
                                null, null);
}

static COLD bipolar nl80211_authorize(nl80211 address_to session, p32 index,
                                 p8 address_to mac)
{
        netlink_buffer request = {0};
        p32 sequence = netlink_sequence_take();
        p32 flags[2];

        flags[0] = (p32)1 << NL80211_STA_FLAG_AUTHORIZED;
        flags[1] = flags[0];
        if (!nl80211_begin(address_of request, session->family,
                           NL80211_CMD_SET_STATION, NLM_REQUEST | NLM_ACK,
                           sequence))
                return -1;
        nl80211_attribute_u32(address_of request, NL80211_ATTR_IFINDEX, index);
        netlink_attribute_add(address_of request, NL80211_ATTR_MAC, mac, 6);
        netlink_attribute_add(address_of request, NL80211_ATTR_STA_FLAGS2, flags,
                              sizeof(flags));
        return netlink_transact(session->handle, address_of request, sequence,
                                null, null);
}

/*
        The joined link's keys, and one EAPOL-Key state machine that serves
        both the join's four-way handshake and every rekey the access point
        starts after it: a new four-way handshake for the pairwise key, or
        the two-message group key handshake. Nothing answered those once the
        join returned, so the access point gave up and sent the machine away
        with reason 15 or 16 -- on one network, forty minutes in.
*/
#define WIFI_KEY_PAIRWISE 0x0008
#define WIFI_KEY_INSTALL 0x0040
#define WIFI_KEY_ACK 0x0080
#define WIFI_KEY_MIC 0x0100
#define WIFI_KEY_SECURE 0x0200
#define WIFI_KEY_REQUEST 0x0800
#define WIFI_KEY_ENCRYPTED 0x1000
#define WIFI_KEEP_CHECK_SECONDS 30

typedef struct
{
        nl80211 session;
        p32 index;
        b32 eapol;
        p8 sta[6];
        p8 bssid[6];
        p8 pmk[32];
        p8 snonce[32];
        p8 anonce[32];
        p8 ptk[64];
        p8 pending[64];
        p8 gtk[4][16];
        p8 gtk_installed; // one bit per driver slot; zero bytes are a valid key
        p8 igtk[2][16];
        p8 igtk_installed; // bit 0 key id 4, bit 1 key id 5, as gtk_installed
        p8 replay[8];
        p8 answered;
        bool replay_set;
        bool installed;
        bool pending_set;
        bool nonce_used;
        //      What was asked of the access point: the suite (2, 6 or 8), the
        //      key descriptor version its frames carry, protection, and the
        //      RSN element (with the RSN extension element after it for
        //      hash-to-element) that the association request and message 2
        //      carry.
        p8 akm;
        p8 version;
        p8 pmf;
        p8 rsn[48];
        p8 rsn_length;
        wifi_seen seen;
        //      The access point's Transition Disable bits from an
        //      authenticated message 3 (WIFI_TRANSITION_*).
        p8 transition_disable;
} wifi_link;

static COLD fn wifi_link_close(wifi_link address_to link)
{
        if (link->eapol >= 0)
                socket_close(link->eapol);
        nl80211_close(address_of link->session);
        crypto_forget(link, sizeof(*link));
        link->eapol = -1;
        link->session.handle = -1;
}

/* A well-formed RSN EAPOL-Key frame's length, taken from its own body
   length and never past what arrived; 0 for anything else. */
static COLD positive wifi_eapol_length(p8 address_to frame, positive got)
{
        positive length;

        if (got < WIFI_EAPOL_HDR || frame[1] != 3 || frame[4] != 2)
                return 0;
        length = 4 + (positive)network_load_16(frame + 2);
        if (length < WIFI_EAPOL_HDR || length > got ||
            network_load_16(frame + 97) > length - WIFI_EAPOL_HDR)
                return 0;
        return length;
}

/* The frame's MIC with its MIC field zero, HMAC-SHA1-128 for PSK and
   AES-128-CMAC for PSK-SHA256 and SAE: written into the frame, or compared
   in constant time with the MIC it carries, which is left in place so that
   another key can be tried. */
static COLD bool wifi_eapol_mic(const wifi_link address_to link, p8 address_to kck,
                                p8 address_to frame, positive length, bool check)
{
        p8 hash[20];
        p8 carried[16];
        bool same;

        memory_copy(carried, frame + 81, 16);
        memory_fill(frame + 81, 0, 16);
        if (link->akm == 2)
                crypto_hmac(DIGEST_SHA1, 20, kck, 16, frame, length, hash);
        else
                crypto_aes_cmac(kck, frame, length, hash);
        same = crypto_same(carried, hash, 16);
        memory_copy(frame + 81, check ? carried : hash, 16);
        crypto_forget(hash, sizeof(hash));
        return same;
}

/* A key frame to the access point: its key information with the link's
   descriptor version, the replay counter it answers, the SNonce and RSN
   element for message 2, and its MIC under kck. The key length is 0, as
   wpa_supplicant sends for RSN. */
static COLD bipolar wifi_eapol_reply(wifi_link address_to link, p8 address_to kck,
                                     p16 info, p8 address_to replay, bool nonce)
{
        p8 frame[WIFI_EAPOL_HDR + sizeof(link->rsn)];
        positive length = WIFI_EAPOL_HDR + (nonce ? link->rsn_length : 0);
        socket_address_packet to = {.family = AF_PACKET,
                                    .protocol = network_order_16(ETH_P_PAE),
                                    .index = link->index,
                                    .halen = 6};
        bipolar sent;

        memory_fill(frame, 0, sizeof(frame));
        frame[0] = 1;
        frame[1] = 3;
        network_store_16(frame + 2, (p16)(length - 4));
        frame[4] = 2;
        network_store_16(frame + 5, (p16)((info & ~7) | link->version));
        memory_copy(frame + 9, replay, 8);
        if (nonce)
        {
                memory_copy(frame + 17, link->snonce, 32);
                network_store_16(frame + 97, link->rsn_length);
                memory_copy(frame + 99, link->rsn, link->rsn_length);
        }
        wifi_eapol_mic(link, kck, frame, length, false);
        memory_copy(to.addr, link->bssid, 6);
        sent = socket_send(link->eapol, frame, length, 0, address_of to, sizeof(to));
        crypto_forget(frame, sizeof(frame));
        return sent == (bipolar)length ? 0 : -1;
}

/* What key data holds that a station takes: the first GTK and its id, the
   first IGTK (key id 4 or 5, its packet number then the key), the access
   point's own RSN element and RSN extension element, whole, and the
   Transition Disable bitmap (IEEE 802.11-2020 12.7.2: KDE 00-0F-AC:32;
   WPA3 specification 13.5), whose first bit says this network is never to
   be joined with WPA2 again. */
#define WIFI_TRANSITION_WPA3_PERSONAL 1

typedef struct
{
        p8 gtk[16];
        p8 gtk_id;
        p8 igtk[6 + 16];
        p8 igtk_id;
        p8 transition_disable;
        p8 rsn[WIFI_ELEMENT_ROOM];
        p8 rsnx[WIFI_ELEMENT_ROOM];
        p16 rsn_length;
        p16 rsnx_length;
} wifi_key_data;

/* Key data wrapped under the KEK, into what: 1 with a GTK, 0 when the data
   unwraps and holds none, -1 when it does not unwrap. Each kind is the
   first of it; a GTK or IGTK element of any other length is no key. */
static COLD bipolar wifi_gtk_take(p8 address_to kek, p8 address_to data, positive length,
                                  wifi_key_data address_to what)
{
        p8 plain[CRYPTO_WRAP_MOST];
        positive size = 0;
        bipolar found = 0;
        byte_reader reader;

        memory_fill(what, 0, sizeof(*what));
        if (!crypto_aes_key_unwrap(kek, data, length, plain, address_of size))
                return -1;
        reader = byte_reader_open(plain, size);
        while (byte_reader_left(&reader))
        {
                byte_reader whole = reader;
                p8 kind = byte_reader_u8(&reader);
                byte_reader element = byte_reader_vector8(&reader);
                positive span = byte_reader_left(&element);
                p32 type;

                if (!byte_reader_ok(&reader))
                        break;
                //      The access point's elements, as they are, to be held
                //      to what its beacon said.
                if ((kind == 0x30 && !what->rsn_length) ||
                    (kind == 0xf4 && !what->rsnx_length))
                {
                        p8 address_to into = kind == 0x30 ? what->rsn : what->rsnx;

                        memory_copy(into, byte_reader_take(&whole, 2 + span), 2 + span);
                        if (kind == 0x30)
                                what->rsn_length = (p16)(2 + span);
                        else
                                what->rsnx_length = (p16)(2 + span);
                        continue;
                }
                if (kind != 0xdd || span < 4)
                        continue;
                //      The OUI and data type, read once: each test below is of
                //      the same four bytes.
                type = byte_reader_u32(&element);
                //      The GTK KDE: 00-0F-AC:1, the key id, a reserved
                //      byte, then a CCMP key of sixteen bytes.
                if (span == 4 + 2 + 16 && type == 0x000fac01 && !found)
                {
                        p8 key_id = byte_reader_u8(&element);

                        (void)byte_reader_u8(&element);
                        what->gtk_id = (p8)(key_id & 3);
                        memory_copy(what->gtk, byte_reader_take(&element, 16), 16);
                        found = 1;
                }
                //      Transition Disable: 00-0F-AC:32 and its bitmap.
                else if (span >= 4 + 1 && type == 0x000fac20)
                        what->transition_disable |= byte_reader_u8(&element);
                //      The IGTK KDE: 00-0F-AC:9, the key id (4 or 5, two
                //      bytes little endian), the six byte packet number the
                //      key starts at, and a BIP-CMAC-128 key of sixteen bytes.
                else if (span == 4 + 2 + 6 + 16 && type == 0x000fac09 && !what->igtk_id)
                {
                        p16 key_id = byte_reader_u16le(&element);

                        if (key_id == 4 || key_id == 5)
                        {
                                memory_copy(what->igtk, byte_reader_take(&element, 22), 22);
                                what->igtk_id = (p8)key_id;
                        }
                }
        }
        crypto_forget(plain, sizeof(plain));
        return found;
}

/* The replay counter and keys for a driver that answers group rekeys by
   itself while the machine sleeps. Most drivers do not, and nothing here
   depends on it: the keeper answers every rekey it is awake for. */
static COLD fn wifi_rekey_offload(wifi_link address_to link)
{
        netlink_buffer request = {0};
        p32 sequence = netlink_sequence_take();
        positive nest;

        if (!nl80211_begin(address_of request, link->session.family,
                           NL80211_CMD_SET_REKEY_OFFLOAD, NLM_REQUEST | NLM_ACK, sequence))
                return;
        nl80211_attribute_u32(address_of request, NL80211_ATTR_IFINDEX, link->index);
        nest = netlink_nested_begin(address_of request, NL80211_ATTR_REKEY_DATA);
        netlink_attribute_add(address_of request, NL80211_REKEY_DATA_KEK, link->ptk + 16, 16);
        netlink_attribute_add(address_of request, NL80211_REKEY_DATA_KCK, link->ptk, 16);
        netlink_attribute_add(address_of request, NL80211_REKEY_DATA_REPLAY_CTR,
                              link->replay, 8);
        netlink_nested_end(address_of request, nest);
        netlink_transact(link->session.handle, address_of request, sequence, null, null);
}

/*
        One EAPOL-Key frame from the access point: 1 when a four-way
        handshake completes, 2 when a group key handshake does, 0 for a
        frame answered or ignored, negative when a key would not go in.

        These frames arrive unencrypted from anyone in range, even with the
        keys in. So nothing is taken from a frame whose replay counter is not
        past the last one whose MIC checked. Message 1 has no MIC and only
        makes a pending PTK, which replaces the installed one once a message
        3 carrying the same ANonce checks under it. A key already in is not
        put in again when a message is resent: that reinstallation is the
        one KRACK used to reset the nonces. The group management key of a
        link with protection is held the same way. A fresh message 3 whose
        RSN elements are not what the access point showed when it was
        chosen is a downgrade, and is neither answered nor keyed:
        WIFI_REFUSE_ELEMENTS.
*/
static COLD bipolar wifi_eapol_step(wifi_link address_to link, p8 address_to frame,
                                    positive got)
{
        positive length = wifi_eapol_length(frame, got);
        p16 info = length ? network_load_16(frame + 5) : 0;
        bool pairwise = (info & WIFI_KEY_PAIRWISE) != 0;
        bool fresh = false;
        p8 address_to kck = null;
        wifi_key_data what;
        p8 idx;
        bipolar found;
        bipolar failed = 0;

        if (!length || (info & 7) != link->version || !(info & WIFI_KEY_ACK) ||
            (info & WIFI_KEY_REQUEST) ||
            (link->replay_set && memory_compare(frame + 9, link->replay, 8) <= 0))
                return 0;

        if (!(info & WIFI_KEY_MIC))
        {
                if (!pairwise)
                        return 0;
                if (!link->nonce_used &&
                    system_random_fill(link->snonce, sizeof(link->snonce), 0) < 0)
                        return -1;
                link->nonce_used = true;
                memory_copy(link->anonce, frame + 17, 32);
                wifi_ptk(link->pmk, link->bssid, link->sta, link->anonce, link->snonce,
                         link->pending, link->akm);
                link->pending_set = true;
                if (link->answered < 255)
                        link->answered++;
                wifi_eapol_reply(link, link->pending, 0x010a, frame + 9, true);
                return 0;
        }

        if (!(info & WIFI_KEY_ENCRYPTED))
                return 0;
        if (!pairwise)
        {
                if (link->installed && (info & WIFI_KEY_SECURE) &&
                    wifi_eapol_mic(link, link->ptk, frame, length, true))
                        kck = link->ptk;
        }
        else if (info & WIFI_KEY_INSTALL)
        {
                //      The pending key for a new handshake's message 3; the
                //      installed one for a message 3 resent after it.
                fresh = link->pending_set && !memory_compare(frame + 17, link->anonce, 32) &&
                        wifi_eapol_mic(link, link->pending, frame, length, true);
                if (fresh)
                        kck = link->pending;
                else if (link->installed && wifi_eapol_mic(link, link->ptk, frame, length, true))
                        kck = link->ptk;
        }
        if (!kck)
                return 0;

        found = wifi_gtk_take(kck + 16, frame + 99, network_load_16(frame + 97),
                              address_of what);
        idx = what.gtk_id;
        if (found < 0 || (!pairwise && !found))
        {
                crypto_forget(address_of what, sizeof(what));
                return 0;
        }
        if (fresh && !wifi_seen_matches(address_of link->seen, what.rsn, what.rsn_length,
                                        what.rsnx, what.rsnx_length))
        {
                crypto_forget(address_of what, sizeof(what));
                return WIFI_REFUSE_ELEMENTS;
        }
        //      Only from a message 3 whose MIC checked: what the access point
        //      said under the key, not what anyone in range may say.
        link->transition_disable |= what.transition_disable;
        /* A failed group-key install is retried under the same replay
           counter. Do not acknowledge or consume it until the driver has
           the key. The joining handshake fails as a whole on an error, so
           its existing early reply remains part of that transaction. */
        if (pairwise)
        {
                memory_copy(link->replay, frame + 9, 8);
                link->replay_set = true;
                failed = wifi_eapol_reply(link, kck, 0x030a, link->replay, false);
        }
        if (!failed && fresh && (!link->installed || memory_compare(link->ptk + 32,
                                                                    kck + 32, 16)))
                failed = nl80211_new_key(address_of link->session, link->index, 0,
                                         NL80211_KEYTYPE_PAIRWISE, link->bssid, kck + 32,
                                         16, null, 0, false, WLAN_CIPHER_CCMP);
        if (!failed && fresh)
        {
                memory_copy(link->ptk, link->pending, sizeof(link->ptk));
                link->pending_set = false;
                link->nonce_used = false;
                crypto_forget(link->pending, sizeof(link->pending));
        }
        //      A message 3 resent for a handshake already done is answered
        //      and nothing more: held back and let through after a group
        //      rekey, it carries a group key the access point has moved on
        //      from, and putting that in again would reset its replay
        //      counter. Each key id also keeps its own last key.
        if (!failed && found && (fresh || !pairwise) &&
            (!(link->gtk_installed & (1u << idx)) ||
             memory_compare(what.gtk, link->gtk[idx], 16)))
        {
                failed = nl80211_new_key(address_of link->session, link->index, idx,
                                         NL80211_KEYTYPE_GROUP, null, what.gtk, 16, frame + 65,
                                         6, true, WLAN_CIPHER_CCMP);
                if (!failed)
                {
                        memory_copy(link->gtk[idx], what.gtk, 16);
                        link->gtk_installed |= (p8)(1u << idx);
                }
        }
        //      The group management key, for the broadcast deauthentication
        //      and disassociation frames of an access point that protects
        //      them: only on a link that asked for protection, once per key
        //      id and key, and before the group handshake is answered, as
        //      the group key is.
        if (!failed && link->pmf && what.igtk_id && (fresh || !pairwise) &&
            (!(link->igtk_installed & (1u << (what.igtk_id - 4))) ||
             memory_compare(what.igtk + 6, link->igtk[what.igtk_id - 4], 16)))
        {
                //      The IGTK is a group key of its own cipher, its packet
                //      number the six bytes at what.igtk.
                failed = nl80211_new_key(address_of link->session, link->index, what.igtk_id,
                                         NL80211_KEYTYPE_GROUP, null, what.igtk + 6, 16,
                                         what.igtk, 6, false, WLAN_CIPHER_BIP_CMAC_128);
                if (!failed)
                {
                        memory_copy(link->igtk[what.igtk_id - 4], what.igtk + 6, 16);
                        link->igtk_installed |= (p8)(1u << (what.igtk_id - 4));
                }
        }
        crypto_forget(address_of what, sizeof(what));
        if (!failed && !pairwise)
        {
                failed = wifi_eapol_reply(link, kck, 0x0302, frame + 9, false);
                if (!failed)
                {
                        memory_copy(link->replay, frame + 9, 8);
                        link->replay_set = true;
                }
        }
        if (!failed && pairwise && !link->installed)
                failed = nl80211_authorize(address_of link->session, link->index,
                                           link->bssid);
        if (failed)
                return failed;
        link->installed = true;
        wifi_rekey_offload(link);
        return pairwise ? 1 : 2;
}

typedef struct
{
        nl80211 address_to session;
        p32 index;
        b32 reason;
} nl80211_news_turn;

/* One message of the news: the 802.11 reason a link went away for, 1 where
   it names none or a zero, which is still a link gone. */
static COLD bool nl80211_link_news_take(netlink_header address_to header,
                                        address_any context)
{
        nl80211_news_turn address_to turn = (nl80211_news_turn address_to)context;
        p8 command = nl80211_news(turn->session, header, turn->index);
        positive size = 0;
        p8 address_to frame;

        if (command == NL80211_CMD_DISCONNECT)
                turn->reason = nl80211_find_u16(header, NL80211_ATTR_REASON_CODE, 1);
        else if (command == NL80211_CMD_DEAUTHENTICATE ||
                 command == NL80211_CMD_DISASSOCIATE)
        {
                //      The frame itself: its reason follows the 24-byte
                //      management header.
                frame = (p8 address_to)netlink_find(header, GENL_HEADER,
                                                    NL80211_ATTR_FRAME,
                                                    address_of size);
                turn->reason = frame && size >= 26 ? frame[24] | frame[25] << 8 : 1;
        }
        else
                return true;
        turn->reason = turn->reason ? turn->reason : 1;
        return false;
}

/* The link's news on the mlme socket: the 802.11 reason it went, 0 while
   it stands, -1 when the socket lost events and the link has to be asked
   after. Every interface's news comes here; only this one's counts. */
static COLD b32 nl80211_link_news(nl80211 address_to session, p32 index)
{
        netlink_buffer reply = {0};
        p32 local_port = 0;
        nl80211_news_turn turn = {.session = session, .index = index};
        bipolar got = netlink_receive(session->handle, address_of reply,
                                      address_of local_port);

        scope_exit(netlink_forget(address_of reply));
        if (got < 0)
                return got == NETWORK_INTERRUPTED || got == NETWORK_TRY_AGAIN
                           ? 0 : -1;
        netlink_each(address_of reply, nl80211_link_news_take, address_of turn);
        return turn.reason;
}

/* The link's EAPOL socket and its mlme news, waited on together until the
   deadline: bit 1 when a frame is waiting, bit 2 news, 0 at the deadline. */
static COLD bipolar wifi_wait(wifi_link address_to link,
                              const network_deadline address_to deadline)
{
        system_poll_descriptor waited[2] = {
            {link->eapol, SYSTEM_POLL_READ, 0},
            {link->session.handle, SYSTEM_POLL_READ, 0}};
        bipolar ready = network_wait_set(waited, 2, deadline);

        if (ready <= 0)
                return ready;
        return (waited[0].returned ? 1 : 0) | (waited[1].returned ? 2 : 0);
}

/* Whether a frame on the EAPOL socket came from the access point the link
   is with: its hardware address is the access point's and no other's.
   Anything else is another station's frame the access point relayed, or a
   forgery, and is not the access point talking (wpa_supplicant drops it
   too): before this, a message 1 from any source address was answered, and
   its ANonce replaced the pending key a real message 3 is checked under. */
static COLD bool wifi_eapol_from(wifi_link address_to link, socket_address_packet address_to from,
                                 p32 size)
{
        //      The kernel gives back the address only as far as the hardware
        //      address goes, which is 18 bytes of the 20 here.
        return size >= (p32)((p8 address_to)from->addr - (p8 address_to)from) + 6 &&
               from->pkttype == PACKET_HOST && from->halen == 6 &&
               !memory_compare(from->addr, link->bssid, 6);
}

/* One frame off the EAPOL socket, through the state machine. */
static COLD bipolar wifi_eapol_take(wifi_link address_to link)
{
        p8 frame[512];
        socket_address_packet from;
        p32 size = sizeof(from);
        bipolar got;
        bipolar step;

        memory_fill(address_of from, 0, sizeof(from));
        got = socket_receive(link->eapol, frame, sizeof(frame), MSG_DONTWAIT, address_of from,
                             address_of size);
        step = got > 0 && wifi_eapol_from(link, address_of from, size)
                   ? wifi_eapol_step(link, frame, (positive)got)
                   : 0;

        crypto_forget(frame, sizeof(frame));
        return step;
}

/*
        The join's four-way handshake, and what it says when it does not
        finish. It used to wait eight seconds on the EAPOL socket alone and
        call every failure a wrong password; now the access point sending
        the machine away ends it at once, and how far it got says why:

          no message 1 at all          -62  did not begin the handshake
          message 1 answered, then
            the access point asked
            again                      -13  did not accept the password
            sent the machine away    -1000-R ended the handshake (reason R)
            nothing                   -121  stopped answering in the handshake

        A message 1 resent after message 2 is the access point finding no
        MIC it could make in it: a password it does not have.
*/
static COLD bipolar wifi_handshake(wifi_link address_to link)
{
        network_deadline deadline;
        b32 reason = 0;
        bipolar step = 0;

        if (!network_deadline_begin(address_of deadline, WIFI_EAPOL_SECONDS, 0))
                return -1;
        while (step != 1 && step >= 0 && reason <= 0)
        {
                bipolar ready = wifi_wait(link, address_of deadline);

                if (ready <= 0)
                        break;
                if (ready & 2)
                        reason = nl80211_link_news(address_of link->session, link->index);
                if (ready & 1)
                        step = wifi_eapol_take(link);
        }
        if (step == 1)
                return 0;
        if (step < 0)
                return step;
        if (link->answered >= 2)
                return -13;
        if (reason > 0)
                return -(1000 + (bipolar)reason);
        return link->answered ? -121 : -62;
}

/*
        The station, apart, and its keeper.

        Everything the join and the keeper read comes from the air: scan
        elements of any access point in range, SAE commits and confirms,
        EAPOL-Key frames. So all of it -- the scan, the choice, WPA3's
        exchange, the association, the four-way handshake and, for as long
        as the link stands, its rekeys -- runs in the station, a process that
        is nobody with CAP_NET_ADMIN and CAP_NET_RAW and nothing else (no
        capability can come back through exec, which it may not make), not
        dumpable, killed with its broker, and under a filter that kills any
        call but its netlink and EAPOL sockets' (radio_filter_build). It opens
        no file. The little it needs of the machine's state -- what a saved
        network has taught the join, the access points to try last, when the
        air was last scanned for a name, the WPA3 budget -- and the little it
        has to leave there it asks of its broker over a channel of fixed-size
        questions (radio_ask): the broker is root, does the one thing each
        question names to the one file it names, and reads nothing else from
        the station but the join's status.

        The broker is the keeper the rest of the machine knows: it holds a
        write lock on wifi.keeper for as long as its station lives, which
        names it to whatever joins or leaves next (host_lock_owner), which
        ends it and waits until it is gone (and the station with it); what a
        pass finds of it is what says the link is there, without asking the
        kernel. wifi.kept says a station joined and has not ended of its own:
        a link with no keeper and that file is one whose keeper died. The
        joining process waits for the join's status and nothing else, holding
        the radio lock, so no join can start between the two.

        It fails closed: a kernel that will not take the filter joins nothing
        (WIFI_REFUSE_CONFINE), and a station that ends without a status is
        WIFI_REFUSE_APART.
*/
#define RADIO_KEEPER_PATH NET_STATE_DIR "/wifi.keeper"
#define RADIO_KEPT_PATH NET_STATE_DIR "/wifi.kept"

//      A socket of whole messages for the broker's channel: a question is
//      read whole or not at all.
#define RADIO_SOCK_SEQPACKET 5

#define RADIO_ASK_RULES_GET 1
#define RADIO_ASK_RULES_PUT 2
#define RADIO_ASK_AVOID_LOAD 3
#define RADIO_ASK_AVOID_ADD 4
#define RADIO_ASK_SCAN_AGE 5
#define RADIO_ASK_SCAN_NOTE 6
#define RADIO_ASK_SAE_BUDGET 7
#define RADIO_ASK_SAID 8
#define RADIO_ASK_JOINED 9
//      What the station may have logged, by number: the broker logs its
//      own literal for it, so nothing the station holds reaches the log.
#define RADIO_SAID_NO_CLOCK 1
#define RADIO_SAID_SOCKETS 2
//      How long the station waits for its broker to answer, which it does
//      at once: one that does not is gone.
#define RADIO_ASK_SECONDS 10
//      RADIO_AVOID_MOST of radio_avoid, sixteen bytes each.
#define RADIO_ASK_AVOID_ROOM 1024

//      One question, every field of it a fixed size the broker checks.
typedef struct
{
        p32 what;
        p8 ssid_length;
        p8 mask;
        p8 rules;
        p8 named;
        p8 ssid[32];
        p8 bssid[6];
        p8 unused[2];
        bipolar status;
} radio_ask;

typedef struct
{
        bipolar answer;
        p64 count;
        p8 avoid[RADIO_ASK_AVOID_ROOM];
} radio_answer;

//      The station's end of its broker's channel; -1 everywhere else.
static b32 radio_station = -1;

/* A question to the broker and its answer, which every question has, so a
   write is made before the station goes on. A broker that does not answer
   is gone, and its station with it. */
static COLD fn radio_station_ask(radio_ask address_to ask, radio_answer address_to answer)
{
        network_deadline deadline;

        memory_fill(answer, 0, sizeof(*answer));
        if (socket_send(radio_station, ask, sizeof(*ask), MSG_NOSIGNAL, null, 0) !=
                (bipolar)sizeof(*ask) ||
            !network_deadline_begin(address_of deadline, RADIO_ASK_SECONDS, 0) ||
            network_wait_readable_until(radio_station, address_of deadline) <= 0 ||
            socket_receive(radio_station, answer, sizeof(*answer), MSG_DONTWAIT, null, null) !=
                (bipolar)sizeof(*answer))
                system_call_1(syscall(exit_group), 1);
}

//      A question about a name or an access point, its answer's number.
static COLD bipolar radio_station_number(p32 what, const p8 address_to ssid, positive ssid_length,
                                     const p8 address_to bssid, p8 mask, p8 rules)
{
        radio_ask ask;
        radio_answer answer;

        memory_fill(address_of ask, 0, sizeof(ask));
        ask.what = what;
        if (ssid && ssid_length <= sizeof(ask.ssid))
        {
                memory_copy(ask.ssid, ssid, ssid_length);
                ask.ssid_length = (p8)ssid_length;
                ask.named = 1;
        }
        if (bssid)
                memory_copy(ask.bssid, bssid, 6);
        ask.mask = mask;
        ask.rules = rules;
        radio_station_ask(address_of ask, address_of answer);
        return answer.answer;
}

/*
        Where passwords are in this process when a join forks: the saved
        networks a pass loaded, a password a person typed. The broker, which
        never needs one, and the station, once its join is over, wipe every
        region held here, as the keeper used to be started only after the
        joining process had forgotten them.
*/
#define RADIO_SECRETS 4

static struct
{
        address_any at;
        positive length;
} radio_secrets[RADIO_SECRETS];

static fn radio_secret_hold(address_any at, positive length)
{
        for (positive slot = 0; slot < RADIO_SECRETS; slot++)
                if (!radio_secrets[slot].at)
                {
                        radio_secrets[slot].at = at;
                        radio_secrets[slot].length = length;
                        return;
                }
}

static fn radio_secret_let_go(address_any at)
{
        for (positive slot = 0; slot < RADIO_SECRETS; slot++)
                if (radio_secrets[slot].at == at)
                        radio_secrets[slot].at = null;
}

static fn radio_secrets_forget(void)
{
        for (positive slot = 0; slot < RADIO_SECRETS; slot++)
                if (radio_secrets[slot].at)
                        crypto_forget(radio_secrets[slot].at, radio_secrets[slot].length);
}

/* What the keeper or whoever starts it has to say, in the kernel log: its
   output is /dev/null. The reason is a literal at every call. */
static COLD fn radio_keeper_said(string_address reason)
{
        string_address line[] = {"wifi keeper: ", reason, null};

        host_kmsg(line);
}

//      The station's line for the log, said by its broker (RADIO_SAID_*).
static COLD fn radio_station_said(positive which)
{
        radio_ask ask;
        radio_answer answer;

        memory_fill(address_of ask, 0, sizeof(ask));
        ask.what = RADIO_ASK_SAID;
        ask.status = (bipolar)which;
        radio_station_ask(address_of ask, address_of answer);
}

static COLD fn radio_keeper_stop(void)
{
        if (host_lock_owner(RADIO_KEEPER_PATH))
        {
                host_lock_signal(RADIO_KEEPER_PATH, SIGKILL);
                host_lock_wait(RADIO_KEEPER_PATH);
        }
        system_remove_at(AT_FDCWD, RADIO_KEPT_PATH, 0);
}

static COLD fn radio_avoid_add(p8 address_to bssid);

/* The link roamed: the station is another access point's, with a handshake
   of its own to come. What was held of the last one is no key for it, and
   what the last one showed of itself is not what this one shows: a roam the
   driver made on its own has no elements to hold the next message 3 to. */
static COLD fn wifi_roam(wifi_link address_to link, p8 address_to to)
{
        memory_copy(link->bssid, to, 6);
        crypto_forget(link->ptk, sizeof(link->ptk));
        crypto_forget(link->pending, sizeof(link->pending));
        crypto_forget(link->gtk, sizeof(link->gtk));
        link->gtk_installed = 0;
        crypto_forget(link->igtk, sizeof(link->igtk));
        link->igtk_installed = 0;
        memory_fill(address_of link->seen, 0, sizeof(link->seen));
        memory_fill(link->replay, 0, sizeof(link->replay));
        link->replay_set = false;
        link->installed = false;
        link->pending_set = false;
        link->nonce_used = false;
}

static COLD fn wifi_keep(wifi_link address_to link)
{
        network_deadline check;
        p8 now[6];

        if (!network_deadline_begin(address_of check, WIFI_KEEP_CHECK_SECONDS, 0))
        {
                radio_station_said(RADIO_SAID_NO_CLOCK);
                return;
        }
        for (;;)
        {
                positive seconds;
                positive nanoseconds;
                bipolar ready = wifi_wait(link, address_of check);
                bipolar there;

                if (ready < 0)
                {
                        radio_station_said(RADIO_SAID_SOCKETS);
                        return;
                }
                if (ready & 2)
                {
                        b32 news = nl80211_link_news(address_of link->session,
                                                     link->index);

                        if (news > 0 ||
                            (news < 0 && nl80211_station(address_of link->session,
                                                         link->index, now) == 0))
                                break;
                }
                if (ready & 1)
                        wifi_eapol_take(link);

                //      Whatever woke it, the link is looked at every thirty
                //      seconds: news about other radios and frames from
                //      anyone in range must not push the look back for good.
                //      A question the kernel did not answer is no answer.
                if (network_deadline_left(address_of check, address_of seconds,
                                          address_of nanoseconds))
                        continue;
                if (!network_deadline_begin(address_of check, WIFI_KEEP_CHECK_SECONDS, 0))
                {
                        radio_station_said(RADIO_SAID_NO_CLOCK);
                        return;
                }
                there = nl80211_station(address_of link->session, link->index, now);
                if (!there)
                        break;
                if (there > 0 && memory_compare(now, link->bssid, 6))
                        wifi_roam(link, now);
        }
        //      Sent away, or the access point stopped answering: the next
        //      join tries another with the same name first.
        radio_avoid_add(link->bssid);
}

static COLD bool wifi_link_mac(string_address name, p8 address_to mac)
{
        netlink_search search;
        bipolar handle = netlink_open_groups(0);

        memory_fill(address_of search, 0, sizeof(search));
        if (handle < 0)
                return false;
        scope_exit(socket_close((b32)handle));
        search.wanted = name;
        if (netlink_link_find((b32)handle, address_of search) < 0 ||
            !search.has_hardware)
                return false;
        memory_copy(mac, search.hardware, 6);
        return true;
}

typedef struct
{
        nl80211 address_to session;
        p8 address_to bssid;
        p32 sequence;
        p32 port;
        p32 index;
        bipolar result;
        bool done;
        bool got_ack;
        bool associated;
} nl80211_join_turn;

/* One message of a join's wait: the acknowledgment of the connect, then the
   connect's own news. Anything that ends the wait is its result. */
static COLD bool nl80211_join_take(netlink_header address_to header,
                                   address_any context)
{
        nl80211_join_turn address_to turn = (nl80211_join_turn address_to)context;
        p8 command;

        if (header->type == NLMSG_IS_ERROR && header->sequence == turn->sequence &&
            header->port == turn->port)
        {
                bipolar ack = netlink_status(header, false);

                turn->got_ack = true;
                if (ack < 0)
                {
                        turn->result = ack;
                        turn->done = true;
                }
                return !turn->done;
        }

        command = nl80211_news(turn->session, header, turn->index);
        if (command == NL80211_CMD_CONNECT)
        {
                p16 status = nl80211_find_u16(header, NL80211_ATTR_STATUS_CODE, 0);
                positive mac_length = 0;
                p8 address_to mac;

                if (nl80211_attr(header, NL80211_ATTR_TIMED_OUT))
                {
                        turn->result = -110;
                        turn->done = true;
                }
                else if (status)
                {
                        turn->result = -111;
                        turn->done = true;
                }
                else
                {
                        mac = (p8 address_to)netlink_find(header, GENL_HEADER,
                                                          NL80211_ATTR_MAC,
                                                          address_of mac_length);
                        if (mac && mac_length >= 6)
                                memory_copy(turn->bssid, mac, 6);
                        turn->associated = true;
                }
        }
        else if (command == NL80211_CMD_DISCONNECT && turn->got_ack)
        {
                turn->result = -111;
                turn->done = true;
        }
        return !turn->done;
}

/* How often a join on a socket with no mlme news asks the kernel whether it
   is there yet. */
#define NL80211_POLL_NANOSECONDS 200000000

/* One wait on the join's socket: until the deadline, or, when it is the
   asking that tells of the association (asking), until the next time to ask
   if that comes first, which is NETWORK_TRY_AGAIN: nothing came, and the
   deadline has not. */
static COLD bipolar nl80211_join_wait(nl80211 address_to session,
                                      const network_deadline address_to deadline, bool asking)
{
        network_deadline slice;
        positive seconds;
        positive nanoseconds;
        bipolar got;

        if (!asking)
                return network_wait_readable_until(session->handle, deadline);
        if (!network_deadline_left(deadline, address_of seconds,
                                   address_of nanoseconds))
                return 0;
        /* Keep the join's original clock origin. A fresh slice must not
           renew an expired join when unrelated news keeps the socket ready. */
        slice = *deadline;
        if (seconds || nanoseconds > NL80211_POLL_NANOSECONDS)
                slice.budget = deadline->budget -
                               (seconds * NETWORK_NANOSECONDS + nanoseconds) +
                               NL80211_POLL_NANOSECONDS;
        got = network_wait_readable_until(session->handle, address_of slice);
        return !got && network_deadline_left(deadline, address_of seconds,
                                             address_of nanoseconds)
                   ? NETWORK_TRY_AGAIN
                   : got;
}

static COLD bipolar nl80211_wait_associated(nl80211 address_to session, p32 sequence,
                                       p32 index, p8 address_to bssid)
{
        netlink_buffer reply = {0};
        network_deadline deadline;
        nl80211_join_turn turn = {.session = session,
                                  .bssid = bssid,
                                  .sequence = sequence,
                                  .index = index};

        if (!network_deadline_begin(address_of deadline, NL80211_CONNECT_SECONDS, 0))
                return -1;

        scope_exit(netlink_forget(address_of reply));
        for (;;)
        {
                //      A socket that heard no mlme news is the one the join
                //      learns of the association from by asking, so what it
                //      waits for is the next time to ask as well as the
                //      deadline.
                bool asking = turn.got_ack && !turn.associated && !session->mlme;
                bipolar got;

                if (asking && nl80211_station(session, index, bssid) > 0)
                        return 0;

                got = nl80211_join_wait(session, address_of deadline, asking);
                if (got == NETWORK_TRY_AGAIN)
                        continue;
                if (got <= 0)
                {
                        if (turn.got_ack && !turn.associated &&
                            nl80211_station(session, index, bssid) > 0)
                                return 0;
                        return got < 0 ? got : -110;
                }

                turn.port = 0;
                got = netlink_receive(session->handle, address_of reply,
                                      address_of turn.port);
                if (got == NETWORK_INTERRUPTED || got == NETWORK_TRY_AGAIN)
                        continue;
                if (got < 0)
                        return got;

                netlink_each(address_of reply, nl80211_join_take, address_of turn);
                if (turn.done || (turn.got_ack && turn.associated))
                        return turn.done ? turn.result : 0;
        }
}

static COLD bipolar nl80211_connect(nl80211 address_to session, p32 index,
                               p8 address_to ssid, positive ssid_length,
                               p8 address_to pmk, bool offload, p8 address_to bssid,
                               p32 frequency, const wifi_link address_to link)
{
        netlink_buffer request = {0};
        p32 sequence = netlink_sequence_take();
        p32 open = NL80211_AUTHTYPE_OPEN;
        p32 version = NL80211_WPA_VERSION_2;
        p32 ccmp = WLAN_CIPHER_CCMP;
        p32 psk = WLAN_SUITE_IEEE | link->akm;
        p32 mfp = NL80211_MFP_REQUIRED;
        bipolar sent;

        scope_exit(netlink_forget(address_of request));
        if (!nl80211_begin(address_of request, session->family,
                           NL80211_CMD_CONNECT, NLM_REQUEST | NLM_ACK, sequence))
                return -1;

        nl80211_attribute_u32(address_of request, NL80211_ATTR_IFINDEX, index);
        netlink_attribute_add(address_of request, NL80211_ATTR_SSID, ssid,
                              ssid_length);
        nl80211_attribute_u32(address_of request, NL80211_ATTR_AUTH_TYPE, open);
        //      The access point chosen from a fresh scan, rather than
        //      whichever the kernel's cache holds by this name.
        if (bssid)
        {
                netlink_attribute_add(address_of request, NL80211_ATTR_MAC, bssid, 6);
                if (frequency)
                        nl80211_attribute_u32(address_of request, NL80211_ATTR_WIPHY_FREQ,
                                              frequency);
        }

        if (pmk)
        {
                netlink_attribute_add(address_of request, NL80211_ATTR_PRIVACY, null,
                                      0);
                nl80211_attribute_u32(address_of request, NL80211_ATTR_WPA_VERSIONS,
                                      version);
                nl80211_attribute_u32(address_of request,
                                      NL80211_ATTR_CIPHER_SUITES_PAIRWISE, ccmp);
                nl80211_attribute_u32(address_of request,
                                      NL80211_ATTR_CIPHER_SUITE_GROUP, ccmp);
                nl80211_attribute_u32(address_of request, NL80211_ATTR_AKM_SUITES,
                                      psk);
                /* The RSN element for the association request, which a
                   driver whose station management is mac80211's builds from
                   what it is handed and nothing else. Without it the access
                   point was asked to associate a station that offered no
                   security at all: hostapd took it, then started 802.1X on
                   it, and the four-way handshake never began. The same bytes
                   go in message 2, which the access point compares to these. */
                netlink_attribute_add(address_of request, NL80211_ATTR_IE,
                                      (address_any)link->rsn, link->rsn_length);
                /* Management frame protection, which the kernel takes as
                   required or not at all: a driver with no connect of its
                   own (mac80211's kind) refuses the optional form outright.
                   Asked for, the kernel drops a deauthentication or
                   disassociation frame that is not protected. */
                if (link->pmf)
                        nl80211_attribute_u32(address_of request, NL80211_ATTR_USE_MFP, mfp);
                if (offload)
                        netlink_attribute_add(address_of request, NL80211_ATTR_PMK, pmk,
                                              32);
                else
                        netlink_attribute_add(address_of request,
                                              NL80211_ATTR_CONTROL_PORT, null, 0);
        }

        if (request.failed)
                return -1;

        sent = socket_send(session->handle, request.bytes, request.used, 0, 0, 0);
        if (sent < 0)
                return sent;
        return sequence;
}

/*
        WPA3-Personal's SAE (802.11-2020 12.4, RFC 7664's dragonfly) over
        the NIST P-256 group, group 19, on net.c's field and point routines.
        On the wire a scalar is 32 big-endian bytes and an element is x then
        y, 32 bytes each.

        The password element is found one of two ways. Hash-to-element
        (12.4.4.2.3) makes a point PT of the network's name and the password
        once, by two simplified SWU maps, with no loop and no branch on
        anything the password decides; it is used whenever the access point
        says it takes it, in its RSN extension element, which is every 6 GHz
        network and every one set to it alone. Hunting and pecking
        (12.4.4.2.2) is the method every WPA3 station has, used for the
        rest: every pass of the hunt is worked out whole, forty of them at
        least however early one lands, and the one that counts is kept by
        mask, which is the Dragonblood advice that the first implementations
        lacked.
*/
#define WIFI_SAE_GROUP 19
#define WIFI_SAE_PASSES 40
#define WIFI_SAE_PASSES_MOST 255
#define WIFI_SAE_SECONDS 10
#define WIFI_SAE_TOKEN_MOST 64
#define WIFI_SAE_TOKEN_STATUS 76
#define WIFI_SAE_H2E_STATUS 126
#define WIFI_SAE_TOKEN_EXTENSION 93

typedef struct
{
        p8 pwe[64];
        p8 rand[32];
        p8 scalar[32];
        p8 element[64];
        p8 peer_scalar[32];
        p8 peer_element[64];
        p8 kck[32];
        p8 pmk[32];
        p8 pmkid[16];
} wifi_sae;

/* 802.11's KDF-Hash-Length with SHA-256, for a length that is a whole
   number of hashes: HMAC of a counter from 1 (two bytes, little endian),
   the label, the context and the length in bits. */
static COLD fn wifi_sae_kdf(p8 address_to key, positive key_length, string_address label,
                            p8 address_to context, positive context_length,
                            p8 address_to out, positive bits)
{
        p8 block[2 + 24 + 64 + 2];
        positive label_length = string_length(label);
        positive length = 2 + label_length + context_length + 2;

        for (positive at = 0; at * 256 < bits; at++)
        {
                block[0] = (p8)(at + 1);
                block[1] = 0;
                memory_copy(block + 2, label, label_length);
                memory_copy(block + 2 + label_length, context, context_length);
                block[length - 2] = (p8)bits;
                block[length - 1] = (p8)(bits >> 8);
                crypto_hmac_sha256(key, key_length, block, length, out + at * 32);
        }
        crypto_forget(block, sizeof(block));
}

/* (p + 1) / 4, the exponent that takes a square root of a square of this
   field: p is 3 mod 4. */
static COLD fn wifi_sae_root_exponent(p64 address_to exponent)
{
        p64 carry = 1;

        for (positive i = 0; i < 4; i++)
                exponent[i] = (crypto_p256_p[i] >> 2) | (i < 3 ? crypto_p256_p[i + 1] << 62 : 0);
        for (positive i = 0; i < 4; i++)
        {
                p64 sum = exponent[i] + carry;

                carry = sum < carry;
                exponent[i] = sum;
        }
}

/* The two addresses, the higher first. */
static COLD fn wifi_sae_addresses(const p8 address_to first, const p8 address_to second,
                                  p8 address_to key)
{
        bool higher = memory_compare(first, second, 6) > 0;

        memory_copy(key, higher ? first : second, 6);
        memory_copy(key + 6, higher ? second : first, 6);
}

/*
        The password element by hunting and pecking, for the password and
        the two addresses: pwd-seed is HMAC-SHA-256 of the password and a
        counter under the addresses, pwd-value the KDF of it; the first
        value under p that is the x of a point is kept, with the y whose low
        bit is the seed's. Every pass is worked out in full, and the first
        that lands is kept by mask.
*/
static COLD bool wifi_sae_pwe(p8 address_to password, positive password_length,
                              p8 address_to first, p8 address_to second, p8 address_to pwe)
{
        p8 key[12];
        p8 data[64 + 1];
        p8 seed[32];
        p8 value[32];
        p8 p_bytes[32];
        p64 x[CRYPTO_FE_MAX], y[CRYPTO_FE_MAX], y2[CRYPTO_FE_MAX], b[CRYPTO_FE_MAX];
        p64 root[CRYPTO_FE_MAX], square[CRYPTO_FE_MAX], zero[CRYPTO_FE_MAX];
        p64 negative_y[CRYPTO_FE_MAX], exponent[CRYPTO_FE_MAX], scratch[CRYPTO_FE_MAX];
        p64 kept_x[CRYPTO_FE_MAX], kept_y[CRYPTO_FE_MAX];
        p64 found = 0;
        bool done = false;

        if (password_length > 64)
                return false;
        wifi_sae_addresses(first, second, key);
        crypto_fe_store_be(p_bytes, crypto_p256_p, 4);
        crypto_fe_load_be(b, crypto_p256_b_be, 4);
        crypto_fe_mul(b, b, crypto_p256_field.square, &crypto_p256_field);
        memory_fill(zero, 0, sizeof(zero));
        wifi_sae_root_exponent(exponent);
        memory_fill(kept_x, 0, sizeof(kept_x));
        memory_fill(kept_y, 0, sizeof(kept_y));
        memory_copy(data, password, password_length);
        for (positive pass = 1; pass <= WIFI_SAE_PASSES_MOST && !done; pass++)
        {
                p64 below;
                p64 residue;
                p64 lands;
                p64 odd;

                data[password_length] = (p8)pass;
                crypto_hmac_sha256(key, 12, data, password_length + 1, seed);
                wifi_sae_kdf(seed, 32, "SAE Hunting and Pecking", p_bytes, 32, value, 256);
                crypto_fe_load_be(x, value, 4);
                below = crypto_fe_subtract_raw(scratch, x, crypto_p256_p, 4);
                //      y^2 = x^3 - 3x + b, in Montgomery form.
                crypto_fe_mul(x, x, crypto_p256_field.square, &crypto_p256_field);
                crypto_fe_sqr(square, x, &crypto_p256_field);
                crypto_fe_mul(y2, square, x, &crypto_p256_field);
                crypto_fe_sub(y2, y2, x, &crypto_p256_field);
                crypto_fe_sub(y2, y2, x, &crypto_p256_field);
                crypto_fe_sub(y2, y2, x, &crypto_p256_field);
                crypto_fe_add(y2, y2, b, &crypto_p256_field);
                crypto_fe_power(root, y2, exponent, &crypto_p256_field);
                crypto_fe_sqr(square, root, &crypto_p256_field);
                crypto_fe_sub(scratch, square, y2, &crypto_p256_field);
                residue = crypto_fe_zero_bit(scratch, 4);
                lands = below & residue & (found ^ 1);
                //      The y whose low bit, as a plain integer, is the seed's.
                crypto_fe_mul(y, root, crypto_unit, &crypto_p256_field);
                odd = y[0] & 1;
                crypto_fe_sub(negative_y, zero, root, &crypto_p256_field);
                crypto_fe_select(root, root, negative_y, 4, odd ^ (p64)(seed[31] & 1));
                crypto_fe_select(kept_x, kept_x, x, 4, lands);
                crypto_fe_select(kept_y, kept_y, root, 4, lands);
                found |= lands;
                done = found && pass >= WIFI_SAE_PASSES;
        }
        crypto_fe_mul(kept_x, kept_x, crypto_unit, &crypto_p256_field);
        crypto_fe_mul(kept_y, kept_y, crypto_unit, &crypto_p256_field);
        crypto_fe_store_be(pwe, kept_x, 4);
        crypto_fe_store_be(pwe + 32, kept_y, 4);
        crypto_forget(data, sizeof(data));
        crypto_forget(seed, sizeof(seed));
        crypto_forget(value, sizeof(value));
        crypto_forget(x, sizeof(x));
        crypto_forget(y, sizeof(y));
        crypto_forget(y2, sizeof(y2));
        crypto_forget(root, sizeof(root));
        crypto_forget(square, sizeof(square));
        crypto_forget(negative_y, sizeof(negative_y));
        crypto_forget(scratch, sizeof(scratch));
        crypto_forget(kept_x, sizeof(kept_x));
        crypto_forget(kept_y, sizeof(kept_y));
        return found != 0;
}

/* A scalar from 1 to the group order less one, as 32 bytes. */
static COLD bool wifi_sae_scalar(p8 address_to out)
{
        p64 limbs[CRYPTO_FE_MAX];
        bool good = false;

        for (positive tries = 0; tries < 64 && !good; tries++)
        {
                if (system_random_fill(out, 32, 0) < 0)
                        break;
                good = crypto_scalar_from_int_be(limbs, out, 32, crypto_p256_n, 4);
        }
        crypto_forget(limbs, sizeof(limbs));
        return good;
}

/* out = (a + b) mod the group order, all as 32 bytes. */
static COLD fn wifi_sae_scalar_add(p8 address_to out, const p8 address_to a,
                                   const p8 address_to b)
{
        p64 x[CRYPTO_FE_MAX], y[CRYPTO_FE_MAX];

        crypto_fe_load_be(x, a, 4);
        crypto_fe_load_be(y, b, 4);
        crypto_fe_add(x, x, y, &crypto_p256_order);
        crypto_fe_store_be(out, x, 4);
        crypto_forget(x, sizeof(x));
        crypto_forget(y, sizeof(y));
}

/* k times the point (x then y), as bytes; false when it is infinity. */
static COLD bool wifi_sae_multiply(p8 address_to out, const p8 address_to k,
                                   const p8 address_to point)
{
        crypto_point base, result;
        p64 x[CRYPTO_FE_MAX], y[CRYPTO_FE_MAX], scalar[CRYPTO_FE_MAX];
        bool finite;

        crypto_fe_load_be(x, point, 4);
        crypto_fe_load_be(y, point + 32, 4);
        crypto_fe_load_be(scalar, k, 4);
        crypto_point_set_xy(&base, x, y, &crypto_p256_field);
        crypto_point_scalar_private(&result, &base, scalar, null);
        finite = !crypto_fe_is_zero(result.z, 4);
        crypto_point_affine(&result);
        crypto_fe_store_be(out, result.x, 4);
        crypto_fe_store_be(out + 32, result.y, 4);
        crypto_forget(scalar, sizeof(scalar));
        crypto_forget(&base, sizeof(base));
        crypto_forget(&result, sizeof(result));
        return finite;
}

/* The commit: scalar = (rand + mask) mod r, which is neither zero nor one,
   and element = the inverse of mask * PWE. */
static COLD bool wifi_sae_commit(wifi_sae address_to sae)
{
        p8 mask[32];
        p8 product[64];
        p64 y[CRYPTO_FE_MAX], zero[CRYPTO_FE_MAX];
        bool good = false;

        memory_fill(zero, 0, sizeof(zero));
        for (positive tries = 0; tries < 16 && !good; tries++)
        {
                p8 high = 0;

                if (!wifi_sae_scalar(sae->rand) || !wifi_sae_scalar(mask))
                        break;
                wifi_sae_scalar_add(sae->scalar, sae->rand, mask);
                for (positive at = 0; at < 31; at++)
                        high |= sae->scalar[at];
                good = (high || sae->scalar[31] > 1) && wifi_sae_multiply(product, mask, sae->pwe);
        }
        if (good)
        {
                crypto_fe_load_be(y, product + 32, 4);
                crypto_fe_sub(y, zero, y, &crypto_p256_field);
                memory_copy(sae->element, product, 32);
                crypto_fe_store_be(sae->element + 32, y, 4);
        }
        crypto_forget(mask, sizeof(mask));
        crypto_forget(product, sizeof(product));
        crypto_forget(y, sizeof(y));
        return good;
}

/* Whether the access point's commit is one to take, by what costs nothing:
   its scalar is above one and under the order, its element is a point of
   the curve, and the two are not our own sent back (a reflection). */
static COLD bool wifi_sae_commit_good(const wifi_sae address_to sae, const p8 address_to scalar,
                                      const p8 address_to element)
{
        p64 limbs[CRYPTO_FE_MAX];

        if (!crypto_scalar_from_int_be(limbs, scalar, 32, crypto_p256_n, 4) ||
            (limbs[0] == 1 && !limbs[1] && !limbs[2] && !limbs[3]))
                return false;
        if (!crypto_point_is_on_curve(element, element + 32, &crypto_p256_field,
                                      crypto_p256_b_be))
                return false;
        return memory_compare(scalar, sae->scalar, 32) || memory_compare(element, sae->element, 64);
}

/*
        The access point's commit taken in, which is where the cost is: two
        scalar multiplications. The shared secret K = rand * (peer-scalar *
        PWE + peer-element), of which only the x goes on: keyseed is its
        HMAC-SHA-256 under a key of zeros, the KCK and the PMK are the KDF
        of that over the sum of the two scalars, and the PMKID is the sum's
        high 128 bits. False when a point on the way is infinity.
*/
static COLD bool wifi_sae_take(wifi_sae address_to sae, const p8 address_to scalar,
                               const p8 address_to element)
{
        p8 shared[64], sum_point[64], term[64], keyseed[32], zeros[32], sum[32], keys[64];
        crypto_point first, second, total;
        p64 x[CRYPTO_FE_MAX], y[CRYPTO_FE_MAX];
        bool good;

        memory_fill(zeros, 0, sizeof(zeros));
        memory_copy(sae->peer_scalar, scalar, 32);
        memory_copy(sae->peer_element, element, 64);
        if (!wifi_sae_multiply(term, scalar, sae->pwe))
                return false;
        crypto_fe_load_be(x, term, 4);
        crypto_fe_load_be(y, term + 32, 4);
        crypto_point_set_xy(&first, x, y, &crypto_p256_field);
        crypto_fe_load_be(x, element, 4);
        crypto_fe_load_be(y, element + 32, 4);
        crypto_point_set_xy(&second, x, y, &crypto_p256_field);
        crypto_point_add(&total, &first, &second);
        if (crypto_fe_is_zero(total.z, 4))
                return false;
        crypto_point_affine(&total);
        crypto_fe_store_be(sum_point, total.x, 4);
        crypto_fe_store_be(sum_point + 32, total.y, 4);
        good = wifi_sae_multiply(shared, sae->rand, sum_point);
        if (good)
        {
                crypto_hmac_sha256(zeros, 32, shared, 32, keyseed);
                wifi_sae_scalar_add(sum, sae->scalar, scalar);
                wifi_sae_kdf(keyseed, 32, "SAE KCK and PMK", sum, 32, keys, 512);
                memory_copy(sae->kck, keys, 32);
                memory_copy(sae->pmk, keys + 32, 32);
                memory_copy(sae->pmkid, sum, 16);
        }
        crypto_forget(shared, sizeof(shared));
        crypto_forget(sum_point, sizeof(sum_point));
        crypto_forget(term, sizeof(term));
        crypto_forget(keyseed, sizeof(keyseed));
        crypto_forget(keys, sizeof(keys));
        crypto_forget(&first, sizeof(first));
        crypto_forget(&second, sizeof(second));
        crypto_forget(&total, sizeof(total));
        return good;
}

/* CN(KCK, counter, scalar, element, peer-scalar, peer-element): the two
   sides' confirms are the one computation with the roles swapped. */
static COLD fn wifi_sae_confirm(const p8 address_to kck, const p8 address_to counter,
                                const p8 address_to scalar, const p8 address_to element,
                                const p8 address_to other_scalar,
                                const p8 address_to other_element, p8 address_to out)
{
        p8 block[2 + 32 + 64 + 32 + 64];

        memory_copy(block, counter, 2);
        memory_copy(block + 2, scalar, 32);
        memory_copy(block + 34, element, 64);
        memory_copy(block + 98, other_scalar, 32);
        memory_copy(block + 130, other_element, 64);
        crypto_hmac_sha256((p8 address_to)kck, 32, block, sizeof(block), out);
}

/* A small number in Montgomery form. */
static COLD fn wifi_sae_small(p64 address_to out, p64 value)
{
        memory_fill(out, 0, 4 * sizeof(p64));
        out[0] = value;
        crypto_fe_mul(out, out, crypto_p256_field.square, &crypto_p256_field);
}

/* Forty-eight big-endian bytes modulo p, as a plain integer: the high
   sixteen times 2^256 (which is what the conversion to Montgomery form
   multiplies by) plus the low thirty-two brought under p once. */
static COLD fn wifi_sae_reduce(p64 address_to out, const p8 address_to bytes)
{
        p64 high[CRYPTO_FE_MAX], low[CRYPTO_FE_MAX], under[CRYPTO_FE_MAX];
        p64 less;

        memory_fill(high, 0, sizeof(high));
        high[0] = network_load_64(bytes + 8);
        high[1] = network_load_64(bytes);
        crypto_fe_load_be(low, bytes + 16, 4);
        less = crypto_fe_subtract_raw(under, low, crypto_p256_p, 4);
        crypto_fe_select(low, under, low, 4, less);
        crypto_fe_mul(high, high, crypto_p256_field.square, &crypto_p256_field);
        crypto_fe_add(out, high, low, &crypto_p256_field);
        crypto_forget(high, sizeof(high));
        crypto_forget(low, sizeof(low));
        crypto_forget(under, sizeof(under));
}

/* RFC 9380's simplified SWU map of u, a plain integer under p, to a point
   of the curve, as plain x and y: Z = -10, the two candidates and their
   squares chosen by mask, the square root by the exponent (p + 1) / 4, and
   the sign of y taken from the low bit of u. */
static COLD fn wifi_sae_sswu(p64 address_to x_out, p64 address_to y_out, const p64 address_to u)
{
        const crypto_field address_to f = &crypto_p256_field;
        p64 um[4], u2[4], t1[4], t2[4], m[4], t[4], z[4], a[4], b[4], zero[4], one[4];
        p64 x1a[4], x1b[4], x1[4], x2[4], gx1[4], gx2[4], y1[4], v[4], x[4], y[4], ny[4];
        p64 exponent[4];
        p64 is_zero, is_square, odd;

        wifi_sae_root_exponent(exponent);
        memory_fill(zero, 0, sizeof(zero));
        memory_copy(one, f->one, sizeof(one));
        wifi_sae_small(t1, 10);
        crypto_fe_sub(z, zero, t1, f);
        wifi_sae_small(t1, 3);
        crypto_fe_sub(a, zero, t1, f);
        crypto_fe_load_be(b, crypto_p256_b_be, 4);
        crypto_fe_mul(b, b, f->square, f);

        crypto_fe_mul(um, u, f->square, f);
        //      m = z^2 u^4 + z u^2, and t = 1 / m (zero for zero).
        crypto_fe_sqr(u2, um, f);
        crypto_fe_mul(t1, z, u2, f);
        crypto_fe_sqr(t2, t1, f);
        crypto_fe_add(m, t1, t2, f);
        crypto_fe_inv(t, m, f);
        //      x1 = b / (z a) when m is zero, else (-b / a) (1 + t).
        crypto_fe_mul(t1, z, a, f);
        crypto_fe_inv(t1, t1, f);
        crypto_fe_mul(x1a, b, t1, f);
        crypto_fe_sub(t1, zero, b, f);
        crypto_fe_inv(t2, a, f);
        crypto_fe_mul(t1, t1, t2, f);
        crypto_fe_add(t2, one, t, f);
        crypto_fe_mul(x1b, t1, t2, f);
        is_zero = crypto_fe_zero_bit(m, 4);
        crypto_fe_select(x1, x1b, x1a, 4, is_zero);
        //      gx1 = x1^3 + a x1 + b; x2 = z u^2 x1; gx2 likewise.
        crypto_fe_sqr(t1, x1, f);
        crypto_fe_mul(t1, t1, x1, f);
        crypto_fe_mul(t2, a, x1, f);
        crypto_fe_add(t1, t1, t2, f);
        crypto_fe_add(gx1, t1, b, f);
        crypto_fe_mul(t1, z, u2, f);
        crypto_fe_mul(x2, t1, x1, f);
        crypto_fe_sqr(t1, x2, f);
        crypto_fe_mul(t1, t1, x2, f);
        crypto_fe_mul(t2, a, x2, f);
        crypto_fe_add(t1, t1, t2, f);
        crypto_fe_add(gx2, t1, b, f);
        //      Whichever of gx1 and gx2 is a square gives the point.
        crypto_fe_power(y1, gx1, exponent, f);
        crypto_fe_sqr(t1, y1, f);
        crypto_fe_sub(t1, t1, gx1, f);
        is_square = crypto_fe_zero_bit(t1, 4);
        crypto_fe_select(v, gx2, gx1, 4, is_square);
        crypto_fe_select(x, x2, x1, 4, is_square);
        crypto_fe_power(y, v, exponent, f);
        //      The y whose low bit is u's.
        crypto_fe_mul(t1, y, crypto_unit, f);
        odd = t1[0] & 1;
        crypto_fe_sub(ny, zero, y, f);
        crypto_fe_select(y, y, ny, 4, odd ^ (u[0] & 1));
        crypto_fe_mul(x_out, x, crypto_unit, f);
        crypto_fe_mul(y_out, y, crypto_unit, f);
        crypto_forget(um, sizeof(um));
        crypto_forget(u2, sizeof(u2));
        crypto_forget(t1, sizeof(t1));
        crypto_forget(t2, sizeof(t2));
        crypto_forget(m, sizeof(m));
        crypto_forget(t, sizeof(t));
        crypto_forget(x1, sizeof(x1));
        crypto_forget(x1a, sizeof(x1a));
        crypto_forget(x1b, sizeof(x1b));
        crypto_forget(x2, sizeof(x2));
        crypto_forget(gx1, sizeof(gx1));
        crypto_forget(gx2, sizeof(gx2));
        crypto_forget(y1, sizeof(y1));
        crypto_forget(v, sizeof(v));
        crypto_forget(x, sizeof(x));
        crypto_forget(y, sizeof(y));
        crypto_forget(ny, sizeof(ny));
}

/* PT for a network's name and a password: HKDF-Extract of the password
   salted by the name, two HKDF-Expands to 48 bytes ("SAE Hash to Element
   u1 P1" and "u2 P2"), each reduced by p and mapped, and the sum of the
   two points, as x then y. */
static COLD bool wifi_sae_pt(p8 address_to ssid, positive ssid_length, p8 address_to password,
                             positive password_length, p8 address_to pt)
{
        p8 seed[32];
        p8 value[48];
        p64 u[CRYPTO_FE_MAX], x[CRYPTO_FE_MAX], y[CRYPTO_FE_MAX];
        crypto_point point[2], total;
        bool good = true;

        crypto_hkdf_extract(ssid, ssid_length, password, password_length, seed);
        for (positive which = 0; which < 2; which++)
        {
                string_address label = which ? (string_address) "SAE Hash to Element u2 P2"
                                             : (string_address) "SAE Hash to Element u1 P1";

                good &= crypto_hkdf_expand(seed, (p8 address_to)label, 25, value, 48);
                wifi_sae_reduce(u, value);
                wifi_sae_sswu(x, y, u);
                crypto_point_set_xy(&point[which], x, y, &crypto_p256_field);
        }
        crypto_point_add(&total, &point[0], &point[1]);
        good &= !crypto_fe_is_zero(total.z, 4);
        crypto_point_affine(&total);
        crypto_fe_store_be(pt, total.x, 4);
        crypto_fe_store_be(pt + 32, total.y, 4);
        crypto_forget(seed, sizeof(seed));
        crypto_forget(value, sizeof(value));
        crypto_forget(u, sizeof(u));
        crypto_forget(x, sizeof(x));
        crypto_forget(y, sizeof(y));
        crypto_forget(point, sizeof(point));
        crypto_forget(&total, sizeof(total));
        return good;
}

/* The element of two stations from PT: PT times HKDF-Extract under 32
   zeros of the higher address then the lower, brought to 1 .. r - 1 (one
   subtraction, since a hash is under twice r - 1, then one more). */
static COLD bool wifi_sae_pwe_from_pt(const p8 address_to pt, const p8 address_to first,
                                      const p8 address_to second, p8 address_to pwe)
{
        p8 key[12];
        p8 zeros[32];
        p8 value[32];
        p64 v[CRYPTO_FE_MAX], below[CRYPTO_FE_MAX], one[CRYPTO_FE_MAX], diff[CRYPTO_FE_MAX];
        p64 less;
        p64 carry = 1;
        bool good;

        wifi_sae_addresses(first, second, key);
        memory_fill(zeros, 0, sizeof(zeros));
        crypto_hkdf_extract(zeros, 32, key, 12, value);
        crypto_fe_load_be(v, value, 4);
        memory_fill(one, 0, sizeof(one));
        one[0] = 1;
        crypto_fe_subtract_raw(below, crypto_p256_n, one, 4);
        less = crypto_fe_subtract_raw(diff, v, below, 4);
        crypto_fe_select(v, diff, v, 4, less);
        for (positive i = 0; i < 4; i++)
        {
                p64 sum = v[i] + carry;

                carry = sum < carry;
                v[i] = sum;
        }
        crypto_fe_store_be(value, v, 4);
        good = wifi_sae_multiply(pwe, value, pt);
        crypto_forget(value, sizeof(value));
        crypto_forget(v, sizeof(v));
        return good;
}

/*
        One authentication frame of the exchange, as an access point sent
        it: its transaction and status, and what the two it reads say.

          commit (1), status 0 or 126   the group; for group 19, the scalar
                                        and the element, then only whole
                                        elements (an identifier, rejected
                                        groups)
          token asked for (1, 76)        the group, then the token: the rest
                                        of the frame, or with
                                        hash-to-element a token container
                                        (255, n, 93, token)
          confirm (2), status 0          the send-confirm and the confirm,
                                        and nothing after them
          any other status              nothing more

        False for a frame that is no SAE authentication frame from bssid
        (transmitter and network both), that is cut short or runs on past
        what it says it is, or whose token is empty or over
        WIFI_SAE_TOKEN_MOST.
*/
typedef struct
{
        p16 transaction;
        p16 status;
        p16 group;
        p16 send_confirm;
        const p8 address_to scalar;
        const p8 address_to element;
        const p8 address_to confirm;
        const p8 address_to token;
        positive token_length;
} wifi_sae_frame;

static COLD bool wifi_sae_frame_read(const p8 address_to frame, positive size,
                                     const p8 address_to bssid, bool h2e,
                                     wifi_sae_frame address_to out)
{
        byte_reader reader = byte_reader_open(frame, size);
        p16 control = byte_reader_u16le(&reader);
        const p8 address_to transmitter;
        const p8 address_to network;
        p16 algorithm;

        memory_fill(out, 0, sizeof(*out));
        (void)byte_reader_skip(&reader, 2 + 6);
        transmitter = byte_reader_take(&reader, 6);
        network = byte_reader_take(&reader, 6);
        (void)byte_reader_skip(&reader, 2);
        algorithm = byte_reader_u16le(&reader);
        out->transaction = byte_reader_u16le(&reader);
        out->status = byte_reader_u16le(&reader);
        //      A management frame of the authentication subtype, not
        //      protected, from the access point and in its network.
        if (!byte_reader_ok(&reader) || (control & 0x40ff) != 0x00b0 || algorithm != 3 ||
            memory_compare(transmitter, bssid, 6) || memory_compare(network, bssid, 6))
                return false;
        if (out->transaction == 1 &&
            (out->status == 0 || out->status == WIFI_SAE_H2E_STATUS))
        {
                out->group = byte_reader_u16le(&reader);
                if (out->group != WIFI_SAE_GROUP)
                        return byte_reader_ok(&reader);
                out->scalar = byte_reader_take(&reader, 32);
                out->element = byte_reader_take(&reader, 64);
                while (byte_reader_ok(&reader) && byte_reader_left(&reader))
                {
                        (void)byte_reader_u8(&reader);
                        (void)byte_reader_vector8(&reader);
                }
                return byte_reader_ok(&reader);
        }
        if (out->transaction == 1 && out->status == WIFI_SAE_TOKEN_STATUS)
        {
                out->group = byte_reader_u16le(&reader);
                if (h2e)
                {
                        byte_reader container;

                        if (byte_reader_u8(&reader) != 255)
                                return false;
                        container = byte_reader_vector8(&reader);
                        if (byte_reader_u8(&container) != WIFI_SAE_TOKEN_EXTENSION ||
                            !byte_reader_end(&reader))
                                return false;
                        reader = container;
                }
                out->token_length = byte_reader_left(&reader);
                out->token = byte_reader_take(&reader, out->token_length);
                return byte_reader_ok(&reader) && out->token_length &&
                       out->token_length <= WIFI_SAE_TOKEN_MOST;
        }
        if (out->transaction == 2 && !out->status)
        {
                out->send_confirm = byte_reader_u16le(&reader);
                out->confirm = byte_reader_take(&reader, 32);
                return byte_reader_ok(&reader) && byte_reader_end(&reader);
        }
        return out->transaction == 1 || out->transaction == 2;
}

/* A commit as the body of an authentication frame: transaction 1, status
   0 (126 when the exchange uses hash-to-element), the group, the token an
   access point asked for, the scalar and the element. The token goes
   before the scalar, or, for hash-to-element, after the element in a
   container element of its own. */
static COLD positive wifi_sae_commit_body(const wifi_sae address_to sae, const p8 address_to token,
                                          positive token_length, p8 address_to out, bool h2e)
{
        positive at = 0;

        out[at++] = 1, out[at++] = 0;
        out[at++] = h2e ? WIFI_SAE_H2E_STATUS : 0, out[at++] = 0;
        out[at++] = WIFI_SAE_GROUP, out[at++] = 0;
        if (token_length && !h2e)
        {
                memory_copy(out + at, token, token_length);
                at += token_length;
        }
        memory_copy(out + at, sae->scalar, 32);
        memory_copy(out + at + 32, sae->element, 64);
        at += 96;
        if (token_length && h2e)
        {
                out[at++] = 255;
                out[at++] = (p8)(1 + token_length);
                out[at++] = WIFI_SAE_TOKEN_EXTENSION;
                memory_copy(out + at, token, token_length);
                at += token_length;
        }
        return at;
}

/*
        The exchange's state as the access point's frames move it, one
        netlink message at a time. What it costs is bounded however many
        frames come, from the access point or in its name: at most
        WIFI_SAE_FRAMES_MOST authentication events are read; a commit is
        checked for what costs nothing (wifi_sae_commit_good) and only one,
        the first that passes, is taken, at two scalar multiplications;
        one token round; and the first confirm decides. A commit that does
        not pass, a confirm before any commit, and a frame from anyone else
        are passed over. A refusal, the link sent away and the kernel's
        timeout end it: -111 before our confirm went, -13 after it (the
        password, as the access point saw it), -110 for the timeout. body
        is a frame to send when the message is read, or nothing.
*/
#define WIFI_SAE_FRAMES_MOST 64

typedef struct
{
        wifi_sae address_to sae;
        const p8 address_to bssid;
        p32 index;
        p16 family;
        p32 port;
        bool h2e;
        positive frames;
        bool token_sent;
        bool taken;
        bool done;
        bipolar result;
        p8 body[2 + 2 + 2 + WIFI_SAE_TOKEN_MOST + 96 + 3];
        positive body_length;
} wifi_sae_turn;

static COLD bool wifi_sae_end(wifi_sae_turn address_to turn, bipolar result)
{
        turn->done = true;
        turn->result = result;
        return false;
}

static COLD bool wifi_sae_news(netlink_header address_to header, address_any context)
{
        wifi_sae_turn address_to turn = (wifi_sae_turn address_to)context;
        wifi_sae_frame read;
        p8 address_to frame;
        positive size = 0;
        p8 command;

        if (turn->done)
                return false;
        if (header->type == NLMSG_IS_ERROR)
                return header->port == turn->port && netlink_status(header, true) < 0
                           ? wifi_sae_end(turn, turn->taken ? -13 : -111)
                           : true;
        if (header->type != turn->family ||
            nl80211_find_u32(header, NL80211_ATTR_IFINDEX, 0) != turn->index)
                return true;
        command = nl80211_command(header);
        if (command == NL80211_CMD_DEAUTHENTICATE || command == NL80211_CMD_DISASSOCIATE)
                return wifi_sae_end(turn, turn->taken ? -13 : -111);
        if (command != NL80211_CMD_AUTHENTICATE)
                return true;
        if (++turn->frames > WIFI_SAE_FRAMES_MOST)
                return wifi_sae_end(turn, -111);
        if (nl80211_attr(header, NL80211_ATTR_TIMED_OUT))
                return wifi_sae_end(turn, -110);
        frame = (p8 address_to)netlink_find(header, GENL_HEADER, NL80211_ATTR_FRAME,
                                            address_of size);
        if (!frame || !wifi_sae_frame_read(frame, size, turn->bssid, turn->h2e, address_of read))
                return true;
        if (read.transaction == 1 && read.status == WIFI_SAE_TOKEN_STATUS)
        {
                //      Anti-clogging: the same commit again with the token
                //      it asked for, once.
                if (turn->token_sent || turn->taken)
                        return wifi_sae_end(turn, -111);
                turn->token_sent = true;
                turn->body_length = wifi_sae_commit_body(turn->sae, read.token, read.token_length,
                                                         turn->body, turn->h2e);
                return true;
        }
        if (read.transaction == 1 && (!read.status || read.status == WIFI_SAE_H2E_STATUS))
        {
                //      One commit is taken in; before it, a commit of the
                //      other method is a refusal of ours.
                if (turn->taken)
                        return true;
                if (read.status != (turn->h2e ? WIFI_SAE_H2E_STATUS : 0))
                        return wifi_sae_end(turn, -111);
                if (read.group != WIFI_SAE_GROUP ||
                    !wifi_sae_commit_good(turn->sae, read.scalar, read.element))
                        return true;
                turn->taken = true;
                if (!wifi_sae_take(turn->sae, read.scalar, read.element))
                        return wifi_sae_end(turn, -111);
                turn->body[0] = 2, turn->body[1] = 0, turn->body[2] = 0, turn->body[3] = 0;
                turn->body[4] = 1, turn->body[5] = 0;
                wifi_sae_confirm(turn->sae->kck, turn->body + 4, turn->sae->scalar,
                                 turn->sae->element, turn->sae->peer_scalar,
                                 turn->sae->peer_element, turn->body + 6);
                turn->body_length = 6 + 32;
                return true;
        }
        if (read.transaction == 2 && !read.status)
        {
                p8 counter[2];
                p8 want[32];
                bool same;

                if (!turn->taken)
                        return true;
                counter[0] = (p8)read.send_confirm;
                counter[1] = (p8)(read.send_confirm >> 8);
                wifi_sae_confirm(turn->sae->kck, counter, turn->sae->peer_scalar,
                                 turn->sae->peer_element, turn->sae->scalar,
                                 turn->sae->element, want);
                same = crypto_same(want, read.confirm, 32);
                crypto_forget(want, sizeof(want));
                return wifi_sae_end(turn, same ? 0 : -13);
        }
        return read.status ? wifi_sae_end(turn, turn->taken ? -13 : -111) : true;
}

/*
        The work SAE asks of the station before the access point has proved
        anything: the password element (a hunt of forty passes or more, or
        PT and its multiple), a commit and, once its commit comes, two
        scalar multiplications more. Anybody in range can make a join start
        it again and again -- a twin of the name that refuses every commit,
        a refusal forged in the real one's name -- so the exchanges are
        counted, in /run/moonwater/wifi.sae, by the boot clock: at most
        WIFI_SAE_PEER_MOST with one access point and WIFI_SAE_ALL_MOST with
        all of them in any WIFI_SAE_WINDOW_MS. One over is refused before
        any of the work, as WIFI_REFUSE_SAE_BUDGET, which the machine's pass
        waits on as it waits on a wrong password.
*/
#define RADIO_SAE_PATH NET_STATE_DIR "/wifi.sae"
#define WIFI_SAE_WINDOW_MS 60000u
#define WIFI_SAE_PEER_MOST 6
#define WIFI_SAE_ALL_MOST 24

typedef struct
{
        p8 bssid[6];
        p8 unused[2];
        p64 at;
} wifi_sae_spent;

static COLD bool wifi_sae_budget(const p8 address_to bssid)
{
        wifi_sae_spent list[WIFI_SAE_ALL_MOST + 1];
        bipolar got;
        positive count;
        p64 now = system_clock_ns(HOST_CLOCK_BOOTTIME) / 1000000;
        positive kept = 1;
        positive peer = 0;

        if (radio_station >= 0)
                return radio_station_number(RADIO_ASK_SAE_BUDGET, null, 0, bssid, 0, 0) != 0;
        got = file_read_once_at(AT_FDCWD, RADIO_SAE_PATH, list + 1,
                                WIFI_SAE_ALL_MOST * sizeof(list[0]));
        count = got > 0 ? (positive)got / sizeof(list[0]) : 0;

        for (positive at = 1; at <= count; at++)
                if (list[at].at <= now && now - list[at].at < WIFI_SAE_WINDOW_MS)
                {
                        peer += !memory_compare(list[at].bssid, bssid, 6);
                        list[kept++] = list[at];
                }
        if (peer >= WIFI_SAE_PEER_MOST || kept > WIFI_SAE_ALL_MOST)
                return false;
        memory_fill(list, 0, sizeof(list[0]));
        memory_copy(list[0].bssid, bssid, 6);
        list[0].at = now;
        host_state_ready();
        host_write_file(RADIO_SAE_PATH, (p8 address_to)list, kept * sizeof(list[0]), 0600, false);
        return true;
}

static COLD bipolar nl80211_authenticate(nl80211 address_to session, p32 index,
                                         p8 address_to ssid, positive ssid_length,
                                         p8 address_to bssid, p32 frequency,
                                         p8 address_to data, positive data_length)
{
        netlink_buffer request = {0};
        p32 sequence = netlink_sequence_take();
        p32 type = NL80211_AUTHTYPE_SAE;
        bipolar sent;

        scope_exit(netlink_forget(address_of request));
        if (!nl80211_begin(address_of request, session->family, NL80211_CMD_AUTHENTICATE,
                           NLM_REQUEST, sequence))
                return -1;
        nl80211_attribute_u32(address_of request, NL80211_ATTR_IFINDEX, index);
        nl80211_attribute_u32(address_of request, NL80211_ATTR_WIPHY_FREQ, frequency);
        netlink_attribute_add(address_of request, NL80211_ATTR_MAC, bssid, 6);
        netlink_attribute_add(address_of request, NL80211_ATTR_SSID, ssid, ssid_length);
        nl80211_attribute_u32(address_of request, NL80211_ATTR_AUTH_TYPE, type);
        netlink_attribute_add(address_of request, NL80211_ATTR_AUTH_DATA, data, data_length);
        if (request.failed)
                return -1;
        sent = socket_send(session->handle, request.bytes, request.used, 0, 0, 0);
        return sent < 0 ? sent : 0;
}

/* The next datagram of mlme news, through visit, before the deadline: 1
   when one was read, 0 at the deadline, negative when the socket failed. */
static COLD bipolar wifi_mlme_next(nl80211 address_to session,
                                   const network_deadline address_to deadline,
                                   netlink_visitor visit, address_any context,
                                   p32 address_to port)
{
        for (;;)
        {
                netlink_buffer reply = {0};
                bipolar got = network_wait_readable_until(session->handle, deadline);

                if (got <= 0)
                        return got;
                got = netlink_receive(session->handle, address_of reply, port);
                if (got == NETWORK_INTERRUPTED || got == NETWORK_TRY_AGAIN)
                {
                        netlink_forget(address_of reply);
                        continue;
                }
                if (got >= 0)
                        netlink_each(address_of reply, visit, context);
                netlink_forget(address_of reply);
                return got < 0 ? got : 1;
        }
}

/*
        The SAE exchange with the access point bssid, through the kernel's
        authentication command, as wpa_supplicant runs it for a driver of
        mac80211's kind, which has no connect of its own for SAE: each
        commit and confirm body goes in NL80211_CMD_AUTHENTICATE (the
        transaction and status first, the algorithm left to the kernel), and
        the access point's frames come back as events of that command on the
        socket the commands go out on. A command is sent without waiting for
        its acknowledgement, since a transaction that reads to it throws the
        frame events it passes away; an error for it still comes, and is a
        refusal. 0 with the PMK and PMKID in sae, else as wifi_sae_news says.
*/
static COLD bipolar wifi_sae_exchange(nl80211 address_to session, p32 index, p8 address_to ssid,
                                      positive ssid_length, p8 address_to bssid,
                                      p32 frequency, wifi_sae address_to sae, bool h2e)
{
        wifi_sae_turn turn = {.sae = sae, .bssid = bssid, .index = index,
                              .family = session->family, .h2e = h2e};
        network_deadline deadline;
        bipolar failed;

        turn.body_length = wifi_sae_commit_body(sae, null, 0, turn.body, h2e);
        failed = nl80211_authenticate(session, index, ssid, ssid_length, bssid, frequency,
                                      turn.body, turn.body_length);
        turn.body_length = 0;
        if (failed < 0 || !network_deadline_begin(address_of deadline, WIFI_SAE_SECONDS, 0))
                return failed < 0 ? failed : -1;
        while (!turn.done)
        {
                failed = wifi_mlme_next(session, address_of deadline, wifi_sae_news,
                                        address_of turn, address_of turn.port);
                if (failed <= 0)
                        return failed < 0 ? failed : -110;
                //      What the datagram asked for, unless it also ended the
                //      exchange in a refusal.
                if (turn.body_length && (!turn.done || !turn.result))
                {
                        failed = nl80211_authenticate(session, index, ssid, ssid_length, bssid,
                                                      frequency, turn.body, turn.body_length);
                        turn.body_length = 0;
                        if (failed < 0)
                                return failed;
                }
        }
        return turn.result;
}

/* The status of an association response from bssid: false for a frame that
   is not one, or is cut short. */
static COLD bool wifi_assoc_status_read(const p8 address_to frame, positive size,
                                        const p8 address_to bssid, p16 address_to status)
{
        byte_reader reader = byte_reader_open(frame, size);
        p16 control = byte_reader_u16le(&reader);
        const p8 address_to transmitter;

        (void)byte_reader_skip(&reader, 2 + 6);
        transmitter = byte_reader_take(&reader, 6);
        (void)byte_reader_skip(&reader, 6 + 2 + 2);
        address_to status = byte_reader_u16le(&reader);
        return byte_reader_ok(&reader) &&
               ((control & 0x40ff) == 0x0010 || (control & 0x40ff) == 0x0030) &&
               !memory_compare(transmitter, bssid, 6);
}

typedef struct
{
        const p8 address_to bssid;
        p32 index;
        p16 family;
        p32 port;
        positive frames;
        bool done;
        bipolar result;
} wifi_assoc_turn;

static COLD bool wifi_assoc_news(netlink_header address_to header, address_any context)
{
        wifi_assoc_turn address_to turn = (wifi_assoc_turn address_to)context;
        p8 address_to frame;
        positive size = 0;
        p16 status = 0;
        p8 command;

        if (turn->done)
                return false;
        if (header->type == NLMSG_IS_ERROR)
        {
                if (header->port != turn->port || netlink_status(header, true) >= 0)
                        return true;
                turn->done = true;
                turn->result = -111;
                return false;
        }
        if (header->type != turn->family ||
            nl80211_find_u32(header, NL80211_ATTR_IFINDEX, 0) != turn->index)
                return true;
        command = nl80211_command(header);
        if (command == NL80211_CMD_DEAUTHENTICATE || command == NL80211_CMD_DISASSOCIATE ||
            (command == NL80211_CMD_ASSOCIATE && ++turn->frames > WIFI_SAE_FRAMES_MOST))
                turn->result = -111;
        else if (command != NL80211_CMD_ASSOCIATE)
                return true;
        else if (nl80211_attr(header, NL80211_ATTR_TIMED_OUT))
                turn->result = -110;
        else
        {
                frame = (p8 address_to)netlink_find(header, GENL_HEADER, NL80211_ATTR_FRAME,
                                                    address_of size);
                if (!frame || !wifi_assoc_status_read(frame, size, turn->bssid, address_of status))
                        return true;
                turn->result = status ? -111 : 0;
        }
        turn->done = true;
        return false;
}

/* The association that follows a finished exchange: the RSN element (with
   the PMKID the exchange made, and the RSN extension element after it for
   hash-to-element), the cipher and key management suites, and protected
   management frames required. */
static COLD bipolar nl80211_associate(nl80211 address_to session, p32 index, p8 address_to ssid,
                                      positive ssid_length, p8 address_to bssid,
                                      p32 frequency, const wifi_link address_to link)
{
        netlink_buffer request = {0};
        p32 sequence = netlink_sequence_take();
        p32 version = NL80211_WPA_VERSION_2;
        p32 ccmp = WLAN_CIPHER_CCMP;
        p32 akm = WLAN_SUITE_IEEE | link->akm;
        p32 mfp = NL80211_MFP_REQUIRED;
        wifi_assoc_turn turn = {.bssid = bssid, .index = index, .family = session->family};
        network_deadline deadline;
        bipolar sent;

        scope_exit(netlink_forget(address_of request));
        if (!nl80211_begin(address_of request, session->family, NL80211_CMD_ASSOCIATE,
                           NLM_REQUEST, sequence))
                return -1;
        nl80211_attribute_u32(address_of request, NL80211_ATTR_IFINDEX, index);
        netlink_attribute_add(address_of request, NL80211_ATTR_MAC, bssid, 6);
        netlink_attribute_add(address_of request, NL80211_ATTR_SSID, ssid, ssid_length);
        nl80211_attribute_u32(address_of request, NL80211_ATTR_WIPHY_FREQ, frequency);
        netlink_attribute_add(address_of request, NL80211_ATTR_IE, (address_any)link->rsn,
                              link->rsn_length);
        netlink_attribute_add(address_of request, NL80211_ATTR_PRIVACY, null, 0);
        nl80211_attribute_u32(address_of request, NL80211_ATTR_WPA_VERSIONS, version);
        nl80211_attribute_u32(address_of request, NL80211_ATTR_CIPHER_SUITES_PAIRWISE, ccmp);
        nl80211_attribute_u32(address_of request, NL80211_ATTR_CIPHER_SUITE_GROUP, ccmp);
        nl80211_attribute_u32(address_of request, NL80211_ATTR_AKM_SUITES, akm);
        nl80211_attribute_u32(address_of request, NL80211_ATTR_USE_MFP, mfp);
        netlink_attribute_add(address_of request, NL80211_ATTR_CONTROL_PORT, null, 0);
        if (request.failed)
                return -1;
        sent = socket_send(session->handle, request.bytes, request.used, 0, 0, 0);
        if (sent < 0 || !network_deadline_begin(address_of deadline, NL80211_CONNECT_SECONDS, 0))
                return sent < 0 ? sent : -1;
        while (!turn.done)
        {
                bipolar got = wifi_mlme_next(session, address_of deadline, wifi_assoc_news,
                                             address_of turn, address_of turn.port);

                if (got <= 0)
                        return got < 0 ? got : -110;
        }
        return turn.result;
}

/* Whether the wireless interface has a station: a station the kernel could
   not be asked about counts as one, so that a failed question is no reason
   to join again over a link that stands. */
static COLD bool nl80211_associated(void)
{
        nl80211 session;
        nl80211_iface iface;
        p8 mac[6];
        bool up = false;

        if (nl80211_open(address_of session) < 0)
                return false;
        bipolar asked = nl80211_interface(address_of session, address_of iface);

        //      No card at all is no station; a question that failed for
        //      another reason counts as one.
        if (asked == 0)
                up = nl80211_station(address_of session, iface.index, mac) != 0;
        else
                up = asked != -ERROR_NO_DEVICE;
        nl80211_close(address_of session);
        return up;
}

static COLD bipolar radio_bss_choose(nl80211 address_to session, p32 index,
                                     p8 address_to ssid, positive ssid_length, bool secured,
                                     p8 address_to bssid, p32 address_to frequency,
                                     bool force, p8 rules, bool key_only,
                                     wifi_rsn_caps address_to caps, wifi_seen address_to seen);
static COLD p8 radio_rules_get(p8 address_to ssid, positive ssid_length);
static COLD fn radio_rules_put(p8 address_to ssid, positive ssid_length, p8 mask, p8 what);

/*
        WPA3 with the access point chosen: the exchange counted against its
        budget, the password element (hash-to-element when the access point
        takes it, the hunt otherwise), the commit and the exchange, the
        association with the PMKID it made, and the four-way handshake on
        its PMK. The EAPOL socket is opened before the association, since
        message 1 follows it by a few milliseconds.
*/
static COLD bipolar wifi_sae_join(wifi_link address_to link, p8 address_to ssid,
                                  positive ssid_length, p8 address_to pass, positive pass_length,
                                  p8 address_to bssid, p32 frequency, bool h2e)
{
        wifi_sae sae;
        bipolar failed;
        bool made;

        scope_exit(crypto_forget(address_of sae, sizeof(sae)));
        memory_fill(address_of sae, 0, sizeof(sae));
        if (!wifi_sae_budget(bssid))
                return WIFI_REFUSE_SAE_BUDGET;
        if (link->eapol < 0)
        {
                failed = net_packet_open(link->index, ETH_P_PAE);
                if (failed < 0)
                        return failed;
                link->eapol = (b32)failed;
        }
        nl80211_disconnect(address_of link->session, link->index);
        memory_fill(link->bssid, 0, 6);
        if (h2e)
        {
                p8 point[64];

                made = wifi_sae_pt(ssid, ssid_length, pass, pass_length, point) &&
                       wifi_sae_pwe_from_pt(point, link->sta, bssid, sae.pwe);
                crypto_forget(point, sizeof(point));
        }
        else
                made = wifi_sae_pwe(pass, pass_length, link->sta, bssid, sae.pwe);
        if (!made || !wifi_sae_commit(address_of sae))
                return -1;
        failed = wifi_sae_exchange(address_of link->session, link->index, ssid, ssid_length,
                                   bssid, frequency, address_of sae, h2e);
        if (failed)
                return failed;
        memory_copy(link->pmk, sae.pmk, 32);
        link->rsn_length = (p8)wifi_rsn_build(link->rsn, 8, link->pmf, sae.pmkid);
        //      The RSN extension element, in the association request and in
        //      message 2 alike: bit 5, hash-to-element.
        if (h2e)
        {
                link->rsn[link->rsn_length++] = 244;
                link->rsn[link->rsn_length++] = 1;
                link->rsn[link->rsn_length++] = 0x20;
        }
        failed = nl80211_associate(address_of link->session, link->index, ssid, ssid_length,
                                   bssid, frequency, link);
        if (failed)
                return failed;
        memory_copy(link->bssid, bssid, 6);
        return wifi_mac_set(link->sta) ? wifi_handshake(link) : -1;
}

/*
        A join, into link: on success the link keeps its sockets and keys for
        the keeper, with a four-way handshake of its own where it needs one.
        -113 when a scan heard no network by this name. What it asks of the
        access point it chose is wifi_join_plan's, at this build's tier and
        with what the saved network has taught it; a refusal of the plan's
        is made before anything is sent. A PSK is made only once an access
        point is chosen and asks for one, since a network that is not there,
        or that is joined with WPA3, is not worth the four thousand rounds.
        force asks for a scan whenever the cache has no access point to join;
        the machine's own pass leaves that to what a scan for the name found
        lately (radio_bss_choose).
*/
/* Whether the access point the last choice made was one a join had given
   up on (radio_avoid): the best there was, behind every other. */
static bool radio_pick_given_up;

/* nl80211_join's answer when a second look found no other access point. */
#define RADIO_JOIN_NO_OTHER 1

static COLD bipolar nl80211_join_once(p8 address_to ssid, positive ssid_length,
                                      p8 address_to pass, positive pass_length, bool force,
                                      bool again, wifi_link address_to link)
{
        nl80211_iface iface;
        p8 chosen[6];
        p32 frequency = 0;
        bipolar picked = -1;
        bipolar route;
        bipolar failed;
        bipolar sequence;
        bool offload = false;
        bool secured = pass_length != 0;
        bool key_only = pass_length == 64;
        bool keyed = false;
        bool kept = false;
        p8 rules = 0;
        wifi_rsn_caps caps;
        wifi_plan plan;

        wifi_link_close(link);
        scope_exit(if (!kept) wifi_link_close(link));
        memory_fill(chosen, 0, 6);
        memory_fill(address_of caps, 0, sizeof(caps));
        memory_fill(address_of plan, 0, sizeof(plan));
        //      Whatever kept the last link's keys is gone before the station
        //      that runs this is forked (radio_join_apart): its EAPOL socket
        //      would hear this join's message 1 as a rekey.
        failed = nl80211_open(address_of link->session);
        if (!failed)
                failed = nl80211_interface(address_of link->session, address_of iface);
        if (failed < 0)
                return failed;
        link->index = iface.index;

        route = netlink_open_groups(0);
        if (route >= 0)
        {
                netlink_link_up((b32)route, iface.index);
                socket_close((b32)route);
        }

        if (iface.has_mac)
                memory_copy(link->sta, iface.mac, 6);
        else
                wifi_link_mac((string_address)iface.name, link->sta);

        if (secured)
                rules = radio_rules_get(ssid, ssid_length);
        picked = radio_bss_choose(address_of link->session, iface.index, ssid, ssid_length,
                                  secured, chosen, address_of frequency, force, rules, key_only,
                                  address_of caps, address_of link->seen);
        if (again && (picked <= 0 || radio_pick_given_up))
                return RADIO_JOIN_NO_OTHER;
        if (!picked)
                return -113;

        link->akm = 2;
        if (secured)
        {
                //      With no scan to read the access point from, what the
                //      network taught stands in for its elements: WPA2, with
                //      protection where it offered that once. WPA3 needs an
                //      access point to send its exchange to, and there is none.
                if (picked < 0)
                {
                        caps.akms = rules & WIFI_RULE_SAE ? 0 : WIFI_AKM_PSK;
                        caps.pmf = rules & (WIFI_RULE_PMF | WIFI_RULE_SAE) ? WIFI_PMF_CAPABLE : 0;
                        caps.group = WIFI_CIPHER_CCMP;
                        caps.pairwise = WIFI_CIPHER_CCMP;
                }
                failed = wifi_join_plan(MOONWATER_STRICT_NET, address_of caps, rules, key_only,
                                        address_of plan);
                if (failed < 0)
                {
                        radio_avoid_add(picked > 0 ? chosen : null);
                        return failed;
                }
                link->akm = plan.akm;
                link->pmf = plan.pmf;
        }

        //      WPA3 first where it is the plan, and WPA2 after it only where
        //      the plan allows that and the exchange came to nothing for a
        //      reason that is not the password; then the driver's own
        //      four-way handshake where it has one, and if that fails, once
        //      more with this one.
        for (;;)
        {
                link->version = wifi_key_version(link->akm);
                if (secured && link->akm == 8)
                {
                        failed = wifi_sae_join(link, ssid, ssid_length, pass, pass_length, chosen,
                                               frequency, caps.h2e != 0);
                        if (failed && failed != -13 && failed != WIFI_REFUSE_ELEMENTS &&
                            plan.fallback)
                        {
                                //      A new association, and none of the
                                //      last one's handshake.
                                link->akm = plan.fallback;
                                link->pmf = caps.pmf ? WIFI_PMF_CAPABLE | WIFI_PMF_REQUIRED : 0;
                                plan.fallback = 0;
                                link->answered = 0;
                                link->replay_set = link->installed = false;
                                link->pending_set = link->nonce_used = false;
                                link->gtk_installed = link->igtk_installed = 0;
                                memory_fill(link->replay, 0, sizeof(link->replay));
                                if (link->eapol >= 0)
                                        socket_close(link->eapol);
                                link->eapol = -1;
                                continue;
                        }
                        break;
                }
                link->rsn_length = (p8)wifi_rsn_build(link->rsn, link->akm, link->pmf, null);
                if (secured && !keyed)
                {
                        if (!wifi_psk(ssid, ssid_length, pass, pass_length, link->pmk))
                        {
                                failed = -22;
                                break;
                        }
                        keyed = true;
                        offload = link->akm == 2 &&
                                  nl80211_psk_offload(address_of link->session, iface.wiphy);
                }
                if (secured && !offload && link->eapol < 0)
                {
                        failed = net_packet_open(iface.index, ETH_P_PAE);
                        if (failed < 0)
                                break;
                        link->eapol = (b32)failed;
                }
                nl80211_disconnect(address_of link->session, iface.index);
                memory_fill(link->bssid, 0, 6);
                sequence = nl80211_connect(address_of link->session, iface.index, ssid,
                                           ssid_length, secured ? link->pmk : null, offload,
                                           picked > 0 ? chosen : null, frequency, link);
                failed = sequence < 0 ? sequence
                                      : nl80211_wait_associated(address_of link->session,
                                                                (p32)sequence, iface.index,
                                                                link->bssid);
                if (!failed && !wifi_mac_set(link->bssid))
                        nl80211_station(address_of link->session, iface.index, link->bssid);
                if (!failed && link->eapol >= 0)
                        failed = wifi_mac_set(link->sta) && wifi_mac_set(link->bssid)
                                     ? wifi_handshake(link)
                                     : -1;
                if (!failed || !secured || !offload)
                        break;
                offload = false;
        }

        if (failed)
        {
                nl80211_disconnect(address_of link->session, iface.index);
                //      Not this one again first, if another answers to the name.
                radio_avoid_add(wifi_mac_set(link->bssid) ? link->bssid
                                : picked > 0          ? chosen
                                                      : null);
        }
        else if (secured)
        {
                //      What this join has taught the network: an access point
                //      by its name protects its management frames, and WPA3
                //      is what it is joined with -- or what the access point
                //      said, under the key, to keep to (Transition Disable).
                p8 learned = (link->pmf && caps.pmf ? WIFI_RULE_PMF : 0) |
                             (link->akm == 8 ||
                                      (link->transition_disable & WIFI_TRANSITION_WPA3_PERSONAL)
                                  ? WIFI_RULE_SAE
                                  : 0);

                if (learned & ~rules)
                        radio_rules_put(ssid, ssid_length, learned, learned);
        }
        kept = !failed;
        return failed;
}

/*
        A join. A failure at an access point puts it behind every other by the
        name (radio_avoid_add), and in the machine's own pass the air is looked
        at once more at once: another by the name is asked now, not after the
        wait the failure earns. A louder twin that refuses every join -- WPA3
        under a password its owner never chose, say -- otherwise held the real
        one off for as long as that wait grew, a minute at the last. A join a
        person asked for answers at once and pays for no second look, but for
        an access point that never answered at all: one heard a moment ago
        and gone since is still in the kernel's list of what was heard, and
        the add said "did not associate" with the network itself in range,
        as wpa_supplicant goes on to the next. A wrong password at the one
        access point there is finds nothing else and waits as it did.
*/
static COLD bipolar nl80211_join(p8 address_to ssid, positive ssid_length, p8 address_to pass,
                                 positive pass_length, bool force, wifi_link address_to link)
{
        bipolar failed = nl80211_join_once(ssid, ssid_length, pass, pass_length, force, false,
                                           link);
        bipolar again;

        if (!failed || (force && failed != -110) || failed == -19 || failed == -113 ||
            failed == WIFI_REFUSE_SAE_BUDGET)
                return failed;
        again = nl80211_join_once(ssid, ssid_length, pass, pass_length, force, true, link);
        return again == RADIO_JOIN_NO_OTHER ? failed : again;
}

static bipolar nl80211_disconnect(nl80211 address_to session, p32 index)
{
        netlink_buffer request = {0};
        p32 sequence = netlink_sequence_take();

        if (!nl80211_begin(address_of request, session->family,
                           NL80211_CMD_DISCONNECT, NLM_REQUEST | NLM_ACK,
                           sequence))
                return -1;

        nl80211_attribute_u32(address_of request, NL80211_ATTR_IFINDEX, index);
        return netlink_transact(session->handle, address_of request, sequence,
                                null, null);
}

#endif


#define RADIO_SSID_MOST 32
#define RADIO_PASS_MOST 64
#define RADIO_WIFI_MOST 16
#define RADIO_RFKILL_WLAN 1
#define RADIO_RFKILL_BLUETOOTH 2
#define RADIO_RFKILL_CHANGE_ALL 3
#define RADIO_LOCK_PATH NET_STATE_DIR "/radio.lock"

typedef struct
{
        p8 ssid[RADIO_SSID_MOST + 1];
        p8 pass[RADIO_PASS_MOST + 1];
        p8 ssid_length;
        p8 pass_length;
} radio_network;

static fn radio_net_wake(void)
{
        bipolar handle;
        p8 one = '1';

        host_state_ready();
        system_call_4(syscall(mknodat), AT_FDCWD,
                      (positive)(string_address)NET_WAKE_PATH, S_IFIFO | 0600, 0);
        /* The FIFO the net loop made or this did, and nothing else: the
           open followed a link standing at the name and wrote a '1' over
           the first byte of whatever it named, as root. */
        handle = system_open_at(AT_FDCWD, NET_WAKE_PATH,
                                FILE_READ_WRITE | O_NONBLOCK | O_NOFOLLOW |
                                    O_CLOEXEC);
        if (handle < 0)
                return;
        if (!file_handle_is_pipe(handle))
        {
                system_close(handle);
                return;
        }

        system_write_all((positive)handle, address_of one, 1);
        system_close(handle);
}

/* One word and its newline over a state file. The room is a timezone name's
   room, because the clock's words come through here too. */
static bipolar radio_write_word(string_address path, string_address word)
{
        p8 line[96];
        p8 have[96];

        string_copy_bounded(line, word, sizeof(line));
        string_append_bounded(line, "\n", sizeof(line));
        //      A word that is already there is not written again: two syncs,
        //      of the file and of its directory, at every boot for nothing.
        if (host_read_state(path, have, sizeof(have)) == (bipolar)string_length(line) &&
            !string_compare(have, line))
                return 0;
        return host_write_file(path, line, string_length(line), 0644, true);
}

/* A word kept for the next boot, or the reason it could not be, the kernel's and
   not a guess, under the name of what was being kept: 1 once it is said. */
static b32 host_save(string_address label, string_address path, string_address word)
{
        bipolar failed = radio_write_word(path, word);

        return failed < 0 ? host_fail(label, failed) : 0;
}

static bipolar radio_lock(bool wait)
{
        return host_lock(RADIO_LOCK_PATH, wait);
}

static fn radio_unlock(bipolar handle)
{
        if (handle < 0)
                return;

        system_call_2(syscall(flock), (positive)handle, RADIO_LOCK_UN);
        system_close(handle);
}

static bipolar radio_rfkill(p8 type, bool block)
{
        bipolar handle = system_open_at(AT_FDCWD, "/dev/rfkill",
                                        O_WRONLY | O_CLOEXEC | O_NONBLOCK);
        bipolar sent;

        //      No /dev/rfkill is a machine with nothing to switch, and that
        //      is no failure to say.
        if (handle < 0)
                return handle == -ENOENT ? 0 : handle;

        sent = ul_rfkill_send(handle, 0, type, RADIO_RFKILL_CHANGE_ALL, block);
        system_close(handle);
        return sent;
}

/* Whether a radio's word and its rfkill write both went through; when not,
   the first that did not is said (to somebody waiting to hear it) and
   answered. */
static b32 radio_failed(bool report, string_address path, bipolar kept, bipolar sent)
{
        if (kept >= 0 && sent >= 0)
                return 0;
        if (report)
                host_fail(kept < 0 ? path : (string_address) "/dev/rfkill", kept < 0 ? kept : sent);
        return 1;
}

/* A radio's switch: the word that outlasts a boot, and the block that is the
   radio itself. */
static b32 radio_switch(string_address path, p8 type, bool on, bool report)
{
        bipolar kept = radio_write_word(path, on ? "on" : "off");

        return radio_failed(report, path, kept, radio_rfkill(type, !on));
}

/* Whether a line has no control byte: what a saved list may hold and still be
   read, and the zone and server names of the clock. */
static bool radio_text_plain(string_address text, positive length)
{
        positive at;

        for (at = 0; at < length; at++)
                if (text[at] < 32)
                        return false;
        return true;
}

/* Whether a name is one radio_display shows as it is, character for character:
   printable ASCII and whole UTF-8 characters from U+00A0 up, and no control
   byte, DEL, C1 control or byte that starts no character. What the verbs
   store is held to what is shown. */
static bool radio_text_shown(string_address text)
{
        positive length = string_length(text);

        for (positive at = 0; at < length;)
        {
                bool printable;

                at += file_terminal_step((const p8 address_to)text + at, length - at, true,
                                         address_of printable);
                if (!printable)
                        return false;
        }
        return true;
}

/* The next line of text, its start and length (the newline is neither), with
   the cursor past it; false once there is none. */
static bool host_line_next(p8 address_to text, positive size, positive address_to at,
                           positive address_to start, positive address_to length)
{
        if (*at >= size)
                return false;
        *start = *at;
        *length = memory_span_without_byte(text + *at, '\n', size - *at);
        *at += *length + (*at + *length < size);
        return true;
}

/* The saved networks, up to room of them. cut, when asked for, says that
   there was more than this read: a full buffer, or rows left over. */
static positive radio_wifi_load(radio_network address_to into, positive room,
                                bool address_to cut)
{
        p8 text[8192];
        bipolar got = host_read_state(NET_WIFI_LIST, text, sizeof(text));
        positive count = 0;
        positive at = 0;
        positive start;
        positive length;
        bool want_ssid = true;

        memory_fill(into, 0, sizeof(radio_network) * room);
        if (cut)
                *cut = false;
        if (got <= 0)
                return 0;
        /* text holds the saved passphrases in the clear, exactly as the
           radio_network array below does, so it is scrubbed the same way
           before this frame is left to whatever runs in it next. */

        while (count < room &&
               host_line_next(text, (positive)got, address_of at, address_of start,
                              address_of length))
        {
                if (want_ssid)
                {
                        if (!length)
                                continue;
                        if (length > RADIO_SSID_MOST ||
                            !radio_text_plain((string_address)(text + start), length))
                        {
                                want_ssid = false;
                                continue;
                        }
                        memory_copy(into[count].ssid, text + start, length);
                        into[count].ssid[length] = end;
                        into[count].ssid_length = (p8)length;
                        into[count].pass[0] = end;
                        into[count].pass_length = 0;
                        want_ssid = false;
                        continue;
                }

                if (into[count].ssid_length)
                {
                        if (length &&
                            (length > RADIO_PASS_MOST ||
                             !radio_text_plain((string_address)(text + start),
                                               length)))
                        {
                                memory_fill(address_of into[count], 0,
                                            sizeof(into[count]));
                        }
                        else
                        {
                                memory_copy(into[count].pass, text + start, length);
                                into[count].pass[length] = end;
                                into[count].pass_length = (p8)length;
                                count++;
                        }
                }
                want_ssid = true;
        }

        if (cut)
                *cut = got >= (bipolar)sizeof(text) - 1 || (count == room && at < (positive)got);
        if (!want_ssid && count < room && into[count].ssid_length)
                count++;

        crypto_forget(text, sizeof(text));
        return count;
}

static bipolar radio_wifi_save(radio_network address_to networks, positive count)
{
        p8 text[8192];
        positive used = 0;
        positive at;

        scope_exit(crypto_forget(text, sizeof(text)));
        for (at = 0; at < count; at++)
        {
                if (used + networks[at].ssid_length + networks[at].pass_length +
                        2 >=
                    sizeof(text))
                        return -1;
                memory_copy(text + used, networks[at].ssid,
                            networks[at].ssid_length);
                used += networks[at].ssid_length;
                text[used++] = '\n';
                memory_copy(text + used, networks[at].pass,
                            networks[at].pass_length);
                used += networks[at].pass_length;
                text[used++] = '\n';
        }

        return host_write_file(NET_WIFI_LIST, text, used, 0600, true);
}

/*
        What each saved network has taught the join, or its owner has said
        of it, beside the list in /root/wifi.rules: a line "WORD NAME" for
        each, the word one of radio_rule_words. pmf: an access point of the
        name offered management frame protection, and one that does not is
        never joined; sae: it was joined with WPA3, and is never joined with
        WPA2 again; wpa2: its owner lets the tight tier join it with WPA2
        (moonwater wifi add SSID - wpa2). The first two are learnt, by a join
        that worked, and forgotten with the network or when it is added again
        with another password, which is its owner saying that it changed; the
        third is what the last add of it said. A name has no line feed, and
        the first space ends the word.
*/
#define RADIO_RULES_PATH "/root/wifi.rules"
#define RADIO_RULES_ROOM 4096

static const string_address radio_rule_words[] = {
    (string_address) "pmf", (string_address) "sae", (string_address) "wpa2"};

/* The rule a line says, as its bit, for the name; 0 for none. */
static p8 radio_rule_of(const p8 address_to line, positive length, const p8 address_to ssid,
                        positive ssid_length)
{
        for (positive bit = 0; bit < 3; bit++)
        {
                positive word = string_length(radio_rule_words[bit]);

                if (length == word + 1 + ssid_length &&
                    !memory_compare(line, radio_rule_words[bit], word) && line[word] == ' ' &&
                    !memory_compare(line + word + 1, ssid, ssid_length))
                        return (p8)(1u << bit);
        }
        return 0;
}

static COLD p8 radio_rules_get(p8 address_to ssid, positive ssid_length)
{
        p8 text[RADIO_RULES_ROOM];
        bipolar got;
        positive at = 0;
        positive start;
        positive length;
        p8 rules = 0;

        if (radio_station >= 0)
                return (p8)radio_station_number(RADIO_ASK_RULES_GET, ssid, ssid_length, null, 0, 0);
        got = host_read_state(RADIO_RULES_PATH, text, sizeof(text));

        while (got > 0 && host_line_next(text, (positive)got, address_of at, address_of start,
                                         address_of length))
                rules |= radio_rule_of(text + start, length, ssid, ssid_length);
        return rules;
}

/* The rules of ssid made what in the bits of mask, the others kept as they
   are; and the lines of a name no longer saved are dropped, which keeps the
   file to the saved networks' three each. */
static COLD fn radio_rules_put(p8 address_to ssid, positive ssid_length, p8 mask, p8 what)
{
        p8 text[RADIO_RULES_ROOM];
        p8 kept[RADIO_RULES_ROOM];
        radio_network networks[RADIO_WIFI_MOST];
        positive count;
        bipolar got;
        p8 rules = 0;
        positive at = 0;
        positive used = 0;
        positive start;
        positive length;
        bool unknown;

        if (radio_station >= 0)
        {
                (void)radio_station_number(RADIO_ASK_RULES_PUT, ssid, ssid_length, null, mask,
                                           what);
                return;
        }
        count = radio_wifi_load(networks, RADIO_WIFI_MOST, null);
        //      A saved list that is there but does not read is not known to
        //      be empty, so the rules are kept as they are; a list that is
        //      not there at all has no networks, and its rules go.
        unknown = !count && system_access_at(AT_FDCWD, NET_WIFI_LIST, 0) == 0;
        got = host_read_state(RADIO_RULES_PATH, text, sizeof(text));

        while (got > 0 && host_line_next(text, (positive)got, address_of at, address_of start,
                                         address_of length))
        {
                bool saved = unknown;
                p8 rule = radio_rule_of(text + start, length, ssid, ssid_length);

                rules |= rule;
                if (rule)
                        continue;
                for (positive one = 0; one < count && !saved; one++)
                        saved = radio_rule_of(text + start, length, networks[one].ssid,
                                              networks[one].ssid_length) != 0;
                if (saved && used + length + 1 <= sizeof(kept))
                {
                        memory_copy(kept + used, text + start, length);
                        used += length;
                        kept[used++] = '\n';
                }
        }
        crypto_forget(networks, sizeof(networks));
        rules = (p8)((rules & ~mask) | (what & mask));
        for (positive bit = 0; bit < 3; bit++)
        {
                positive word = string_length(radio_rule_words[bit]);

                if (!(rules & (1u << bit)) || used + word + 1 + ssid_length + 1 > sizeof(kept))
                        continue;
                memory_copy(kept + used, radio_rule_words[bit], word);
                used += word;
                kept[used++] = ' ';
                memory_copy(kept + used, ssid, ssid_length);
                used += ssid_length;
                kept[used++] = '\n';
        }
        host_write_file(RADIO_RULES_PATH, kept, used, 0600, true);
}

/* ---- wifi: why there is nothing to join with ---- */

/*
        The one line that says why wifi cannot be used, from what the kernel
        shows anybody: sysfs for the cards, their drivers and the rfkill
        switches, /dev/kmsg for what a driver said as it gave up (readable
        without privilege here, since the image leaves DMESG_RESTRICT off),
        and the firmware directories for whether the file it asked for is
        there now.

        The two drivers the image carries fail differently without their
        firmware. rtw88 fails its probe and leaves the card with no driver;
        mt7921e stays bound and never registers a radio. Both leave "Direct
        firmware load for NAME failed" against the card's address, so the
        card is found by its PCI class, 0x0280, and the file by that line.
        A USB radio has no class of its own, so it is found only by a wifi
        driver bound to it.
*/

#define RADIO_WHY_ROOM 192
#define RADIO_LAST_PATH NET_STATE_DIR "/wifi.last"

typedef struct
{
        p16 vendor;
        p16 device;
        string_address name;
} radio_chip;

/* The names these parts are sold by. Anything else is its vendor and IDs. */
static const radio_chip radio_chips[] = {
    {0x10ec, 0xc822, (string_address) "Realtek RTL8822CE"},
    {0x10ec, 0xb822, (string_address) "Realtek RTL8822BE"},
    {0x10ec, 0xc821, (string_address) "Realtek RTL8821CE"},
    {0x10ec, 0xd723, (string_address) "Realtek RTL8723DE"},
    {0x10ec, 0x8852, (string_address) "Realtek RTL8852AE"},
    {0x10ec, 0xb852, (string_address) "Realtek RTL8852BE"},
    {0x10ec, 0xc852, (string_address) "Realtek RTL8852CE"},
    {0x14c3, 0x7961, (string_address) "MediaTek MT7921"},
    {0x14c3, 0x0608, (string_address) "MediaTek MT7921K"},
    {0x14c3, 0x0616, (string_address) "MediaTek MT7922"},
    {0x14c3, 0x7920, (string_address) "MediaTek MT7920"},
    {0x14c3, 0x7925, (string_address) "MediaTek MT7925"},
    {0x17cb, 0x1103, (string_address) "Qualcomm QCA2066"},
    {0x168c, 0x003e, (string_address) "Qualcomm Atheros QCA6174"},
    {0x8086, 0x2723, (string_address) "Intel Wi-Fi 6 AX200"},
    {0x8086, 0x2725, (string_address) "Intel Wi-Fi 6E AX210"},
    {0x8086, 0x272b, (string_address) "Intel Wi-Fi 7 BE200"},
};

static const radio_chip radio_vendors[] = {
    {0x10ec, 0, (string_address) "Realtek"},  {0x14c3, 0, (string_address) "MediaTek"},
    {0x8086, 0, (string_address) "Intel"},    {0x168c, 0, (string_address) "Qualcomm Atheros"},
    {0x17cb, 0, (string_address) "Qualcomm"}, {0x14e4, 0, (string_address) "Broadcom"},
    {0x0bda, 0, (string_address) "Realtek"},  {0x0e8d, 0, (string_address) "MediaTek"},
    {0x148f, 0, (string_address) "Ralink"},
};

/* The drivers a USB radio is known by, as the start of their names. */
static const string_address radio_usb_drivers[] = {
    (string_address) "rtw88_",    (string_address) "rtw89_",   (string_address) "rtl8xxxu",
    (string_address) "rtl8192cu", (string_address) "rtl8187",  (string_address) "mt7601u",
    (string_address) "mt76x0u",   (string_address) "mt76x2u",  (string_address) "mt7663u",
    (string_address) "mt7921u",   (string_address) "mt7925u",  (string_address) "rt2800usb",
    (string_address) "rt73usb",   (string_address) "ath9k_htc", (string_address) "carl9170",
    (string_address) "ath10k_usb", (string_address) "ath6kl_usb", (string_address) "ar5523",
    (string_address) "brcmfmac",  (string_address) "zd1211rw", (string_address) "mwifiex_usb",
};

typedef struct
{
        p8 address[48];
        p8 name[64];
        p8 driver[48];
        bool found;
} radio_card;

/* Pieces joined into a bounded line; a null ends the list early. */
static fn radio_line(p8 address_to into, positive room, string_address a,
                     string_address b, string_address c, string_address d,
                     string_address e)
{
        string_address parts[5] = {a, b, c, d, e};

        into[0] = end;
        for (positive at = 0; at < 5 && parts[at]; at++)
                string_append_bounded(into, parts[at], room);
}

static p32 radio_hex(p8 address_to text)
{
        //      The text is terminated, and a terminator is not a hexadecimal
        //      digit, so the run ends at the terminator whatever the bound
        //      is and there is no length to measure first.
        if (text[0] == '0' && (text[1] | 32) == 'x')
                text += 2;
        return (p32)string_digits_hexadecimal_max(text, (positive)-1, null);
}

static fn radio_hex4(p8 address_to into, p32 value)
{
        p8 wire[2];

        network_store_16(wire, (p16)value);
        memory_into_hex(into, wire, sizeof(wire));
        into[4] = end;
}

static bipolar radio_sys_read(string_address directory, string_address name,
                              string_address leaf, p8 address_to into, positive room)
{
        p8 path[256];

        if (!string_join(path, sizeof(path), directory, "/", name, leaf))
        {
                into[0] = end;
                return -1;
        }
        return host_read_kernel((string_address)path, into, room);
}

/* The name of the driver bound to a device, or empty. */
static fn radio_sys_driver(string_address directory, string_address name,
                           p8 address_to into, positive room)
{
        p8 path[256];
        p8 target[256];
        bipolar got;
        positive from = 0;

        into[0] = end;
        if (!string_join(path, sizeof(path), directory, "/", name, (string_address) "/driver"))
                return;
        got = system_read_link_at(AT_FDCWD, path, target, sizeof(target) - 1);
        if (got <= 0)
                return;
        p8 address_to slash = memory_last_of(target, '/', (positive)got);

        if (slash)
                from = (positive)(slash - target) + 1;
        target[got] = end;
        string_copy_bounded(into, target + from, room);
}

static fn radio_card_name(radio_card address_to card, p32 vendor, p32 device,
                          string_address kind)
{
        p8 ids[12];
        string_address maker = null;

        for (positive at = 0; at < array_count(radio_chips); at++)
                if (radio_chips[at].vendor == vendor && radio_chips[at].device == device)
                {
                        string_copy_bounded(card->name, radio_chips[at].name,
                                            sizeof(card->name));
                        return;
                }
        for (positive at = 0; at < array_count(radio_vendors); at++)
                if (radio_vendors[at].vendor == vendor)
                        maker = radio_vendors[at].name;
        radio_hex4(ids, vendor);
        ids[4] = ':';
        radio_hex4(ids + 5, device);
        radio_line(card->name, sizeof(card->name), maker ? maker : (string_address) "",
                   maker ? (string_address) " " : (string_address) "", kind,
                   (string_address) " ", ids);
}

static bool radio_pci_visit(string_address directory, string_address name,
                            address_any context)
{
        radio_card address_to card = (radio_card address_to)context;
        p8 text[24];
        p32 vendor;

        if (radio_sys_read(directory, name, (string_address) "/class", text,
                           sizeof(text)) < 0 ||
            !host_starts((string_address)text, (string_address) "0x0280"))
                return true;
        radio_sys_read(directory, name, (string_address) "/vendor", text, sizeof(text));
        vendor = radio_hex(text);
        radio_sys_read(directory, name, (string_address) "/device", text, sizeof(text));
        radio_card_name(card, vendor, radio_hex(text), (string_address) "wireless");
        string_copy_bounded(card->address, name, sizeof(card->address));
        radio_sys_driver(directory, name, card->driver, sizeof(card->driver));
        card->found = true;
        return false;
}

static bool radio_usb_visit(string_address directory, string_address name,
                            address_any context)
{
        radio_card address_to card = (radio_card address_to)context;
        p8 driver[48];
        p8 parent[48];
        p8 text[16];
        p32 vendor;
        positive at = 0;
        bool wifi = false;

        if (!string_find(name, (string_address) ":"))
                return true;
        radio_sys_driver(directory, name, driver, sizeof(driver));
        for (positive which = 0; which < array_count(radio_usb_drivers); which++)
                if (host_starts((string_address)driver, radio_usb_drivers[which]))
                        wifi = true;
        if (!wifi)
                return true;

        while (name[at] && name[at] != ':' && at + 1 < sizeof(parent))
        {
                parent[at] = name[at];
                at++;
        }
        parent[at] = end;
        radio_sys_read(directory, (string_address)parent, (string_address) "/idVendor",
                       text, sizeof(text));
        vendor = radio_hex(text);
        radio_sys_read(directory, (string_address)parent, (string_address) "/idProduct",
                       text, sizeof(text));
        radio_card_name(card, vendor, radio_hex(text), (string_address) "USB wireless");
        string_copy_bounded(card->address, name, sizeof(card->address));
        string_copy_bounded(card->driver, driver, sizeof(card->driver));
        card->found = true;
        return false;
}

static bool radio_net_visit(string_address directory, string_address name,
                            address_any context)
{
        p8 path[256];

        if (string_join(path, sizeof(path), directory, "/", name, (string_address) "/phy80211") &&
            system_access_at(AT_FDCWD, path, 0) >= 0)
        {
                *(bool address_to)context = true;
                return false;
        }
        return true;
}

/* 2 when a wireless switch is hard-blocked, 1 when only soft, else 0. */
static bool radio_rfkill_visit(string_address directory, string_address name,
                               address_any context)
{
        p8 address_to state = (p8 address_to)context;
        p8 text[16];

        if (radio_sys_read(directory, name, (string_address) "/type", text,
                           sizeof(text)) < 0 ||
            !string_equals((string_address)text, (string_address) "wlan"))
                return true;
        if (radio_sys_read(directory, name, (string_address) "/hard", text,
                           sizeof(text)) >= 0 &&
            string_equals((string_address)text, (string_address) "1"))
                *state = 2;
        else if (radio_sys_read(directory, name, (string_address) "/soft", text,
                                sizeof(text)) >= 0 &&
                 string_equals((string_address)text, (string_address) "1") &&
                 *state < 1)
                *state = 1;
        return true;
}

/* 2 when a wireless switch is hard-blocked, 1 when only soft, else 0. */
static p8 radio_rfkill_blocked(void)
{
        p8 state = 0;

        host_each_entry((string_address) "/sys/class/rfkill", radio_rfkill_visit,
                        address_of state);
        return state;
}

/*
        What the kernel said about one device: the last firmware file a load
        failed for, and the error its last failed probe gave. A record is
        "PRIORITY,SEQUENCE,TIME,FLAGS;TEXT" and a device's messages start
        its text with its driver and its address.
*/
typedef struct
{
        p8 firmware[128];
        p8 probe[16];
} radio_said;

static fn radio_kernel_said(string_address address, radio_said address_to said)
{
        p8 record[2048];
        p8 load[96];
        p8 probe[96];
        bipolar handle;

        said->firmware[0] = end;
        said->probe[0] = end;
        radio_line(load, sizeof(load), address,
                   (string_address) ": Direct firmware load for ", null, null, null);
        radio_line(probe, sizeof(probe), address,
                   (string_address) ": probe with driver ", null, null, null);
        handle = system_open_at(AT_FDCWD, "/dev/kmsg", FILE_READ | O_NONBLOCK | O_CLOEXEC);
        if (handle < 0)
                return;

        for (;;)
        {
                bipolar got = system_read_once(handle, record, sizeof(record) - 1);
                string_address text;
                string_address found;
                positive at;

                // Interrupted, or a record overwritten before it was read.
                if (got == -4 || got == -32)
                        continue;
                if (got <= 0)
                        break;
                record[got] = end;
                text = string_find(record, (string_address) ";");
                if (!text)
                        continue;
                text++;
                at = (positive)(string_first_of_or_end(text, '\n') - text);
                text[at] = end;

                found = string_find(text, load);
                if (found)
                {
                        found += string_length(load);
                        for (at = 0; found[at] && found[at] != ' ' &&
                                     at + 1 < sizeof(said->firmware);
                             at++)
                                said->firmware[at] = found[at];
                        said->firmware[at] = end;
                        continue;
                }
                found = string_find(text, probe);
                if (found && (found = string_find(found, (string_address) "with error ")))
                        string_copy_bounded(said->probe, found + 11, sizeof(said->probe));
        }
        system_close(handle);
}

/* Where request_firmware looks, in its order, as the file or its .zst/.xz. */
static bool radio_firmware_present(string_address name)
{
        static const string_address tails[] = {(string_address) "", (string_address) ".zst",
                                               (string_address) ".xz"};
        p8 custom[256];
        p8 release[80];
        p8 updates[160];
        p8 versioned[160];
        p8 path[512];
        string_address roots[5];
        positive count = 0;

        host_read_kernel((string_address) "/sys/module/firmware_class/parameters/path",
                       custom, sizeof(custom));
        host_read_kernel((string_address) "/proc/sys/kernel/osrelease", release,
                       sizeof(release));
        radio_line(updates, sizeof(updates), (string_address) "/lib/firmware/updates/",
                   release, null, null, null);
        radio_line(versioned, sizeof(versioned), (string_address) "/lib/firmware/",
                   release, null, null, null);
        if (custom[0])
                roots[count++] = custom;
        roots[count++] = updates;
        roots[count++] = (string_address) "/lib/firmware/updates";
        roots[count++] = versioned;
        roots[count++] = (string_address) "/lib/firmware";

        for (positive root = 0; root < count; root++)
                for (positive tail = 0; tail < array_count(tails); tail++)
                {
                        radio_line(path, sizeof(path), roots[root], (string_address) "/",
                                   name, tails[tail], null);
                        if (system_access_at(AT_FDCWD, path, 0) >= 0)
                                return true;
                }
        return false;
}

static bool radio_has_interface(void)
{
        bool radio = false;

        host_each_entry((string_address) "/sys/class/net", radio_net_visit,
                        address_of radio);
        return radio;
}

/*
        A radio's word as the machine reads it: 1 on, 0 off. With no word kept, wifi and bluetooth are off, as a
        machine that has not asked for them has them, and moonwater wifi on or bluetooth on is what raises one. A
        machine that was using one before this default keeps it: a saved network or a remembered device leaves the
        word unsaid, -1, which every caller reads as on, as it always did.
*/
static bipolar radio_kept(string_address path)
{
        radio_network networks[1];
        p8 text[16];
        bool used;

        //      A word that says neither is no switch raised: off, as with none.
        if (host_read_word(path, text, sizeof(text)) >= 0)
                return host_onoff((string_address)text) > 0 ? 1 : 0;

        if (string_equals(path, NET_WIFI_POWER))
        {
                used = radio_wifi_load(networks, 1, null) != 0;
                crypto_forget(networks, sizeof(networks));
        }
        else
                used = host_read_state(NET_BLUETOOTH_LIST, text, 2) > 0;

        return used ? -1 : 0;
}

/*
        Why wifi cannot be used, into why, or false when the machine shows
        nothing wrong: a radio is there and nothing blocks it.
*/
static bool radio_wifi_why(p8 address_to why, positive room)
{
        radio_card card;
        radio_said said;
        p8 rfkill = 0;

        why[0] = end;
        if (radio_kept(NET_WIFI_POWER) == 0)
        {
                radio_line(why, room, (string_address) "switched off (moonwater wifi on)",
                           null, null, null, null);
                return true;
        }

        rfkill = radio_rfkill_blocked();
        if (rfkill == 2)
        {
                radio_line(why, room,
                           (string_address) "blocked by rfkill (hard): a switch or key on the machine",
                           null, null, null, null);
                return true;
        }

        if (radio_has_interface())
        {
                if (rfkill == 1)
                        radio_line(why, room, (string_address) "blocked by rfkill (soft)",
                                   null, null, null, null);
                return rfkill == 1;
        }

        memory_fill(address_of card, 0, sizeof(card));
        host_each_entry((string_address) "/sys/bus/pci/devices", radio_pci_visit,
                        address_of card);
        if (!card.found)
                host_each_entry((string_address) "/sys/bus/usb/devices", radio_usb_visit,
                                address_of card);
        if (!card.found)
        {
                radio_line(why, room, (string_address) "no wireless hardware found", null,
                           null, null, null);
                return true;
        }

        radio_kernel_said(card.address, address_of said);
        if (said.firmware[0] && !radio_firmware_present(said.firmware))
                radio_line(why, room, card.name, (string_address) " found, firmware ",
                           said.firmware, (string_address) " missing", null);
        else if (said.firmware[0])
                radio_line(why, room, card.name, (string_address) " found, firmware ",
                           said.firmware,
                           (string_address) " came after its driver gave up; reboot",
                           null);
        else if (!card.driver[0] && said.probe[0])
                radio_line(why, room, card.name,
                           (string_address) " found, its driver failed with error ",
                           said.probe, null, null);
        else if (!card.driver[0])
                radio_line(why, room, card.name,
                           (string_address) " found, no driver for it in this image",
                           null, null, null);
        else
                radio_line(why, room, card.name, (string_address) " found, driver ",
                           card.driver, (string_address) " gave it no interface", null);
        return true;
}

/* ---- wifi: the networks in the air ---- */

/*
        What the radio has heard, one row a network: its strongest access
        point's signal and channel, what it asks of a station, and whether
        this machine is joined to it or has it saved. The kernel keeps the
        last scan's results; a new scan is asked for only when the last one
        asked for here is older than RADIO_AIR_STALE_MS, only by root, and
        waited on for RADIO_AIR_WAIT_SECONDS at most. What the kernel holds
        cannot say how old it is as a whole: a join scans for its one name,
        which leaves a list of the few that answered looking fresh, so the
        time of the last whole scan is kept in RADIO_SCAN_PATH. A join the
        machine is making scans too; the kernel then answers EBUSY, the wait
        is for that scan's results, and those are not a whole scan.

        A name is the network's to choose and arrives over the air, so it is
        written through radio_display: printable ASCII and valid UTF-8 from
        U+00A0 up as they are, every other byte as \xNN, so no name can put
        a control sequence in front of the terminal's parser.
*/

#define NL80211_CMD_GET_SCAN 32
#define NL80211_CMD_TRIGGER_SCAN 33
#define NL80211_CMD_NEW_SCAN_RESULTS 34
#define NL80211_CMD_SCAN_ABORTED 35
#define NL80211_ATTR_BSS 47
#define NL80211_BSS_BSSID 1
#define NL80211_BSS_FREQUENCY 2
#define NL80211_BSS_CAPABILITY 5
#define NL80211_BSS_INFORMATION_ELEMENTS 6
#define NL80211_BSS_SIGNAL_MBM 7
#define NL80211_BSS_STATUS 9
#define NL80211_BSS_SEEN_MS_AGO 10
#define NL80211_BSS_BEACON_IES 11
#define NL80211_BSS_STATUS_ASSOCIATED 1
#define NL80211_ATTR_STA_INFO 21
#define NL80211_STA_INFO_STA_FLAGS 17

#define RADIO_AIR_MOST 64
#define RADIO_AIR_SHOWN 16
#define RADIO_AIR_STALE_MS 30000u
#define RADIO_AIR_WAIT_SECONDS 5
#define RADIO_SCAN_PATH NET_STATE_DIR "/wifi.scan"

enum
{
        RADIO_OPEN,
        RADIO_WEP,
        RADIO_WPA,
        RADIO_WPA2,
        RADIO_WPA23,
        RADIO_WPA3,
        RADIO_OWE,
        RADIO_EAP,
        RADIO_TKIP,
        RADIO_PMF,
        RADIO_FT,
};

static const string_address radio_security_words[] = {
    (string_address) "open", (string_address) "WEP",    (string_address) "WPA",
    (string_address) "WPA2", (string_address) "WPA2/3", (string_address) "WPA3",
    (string_address) "OWE",  (string_address) "802.1X", (string_address) "TKIP",
    (string_address) "PMF",  (string_address) "FT/EXT",
};

/* Whether moonwater can join what a network asks for: open, WPA2 (PSK or
   PSK-SHA256), WPA3 (SAE) or both, with protected management frames or
   without. */
static bool radio_security_joinable(p8 security)
{
        return security == RADIO_OPEN || security == RADIO_WPA2 || security == RADIO_WPA23 ||
               security == RADIO_WPA3 || security == RADIO_PMF;
}

typedef struct
{
        p8 ssid[RADIO_SSID_MOST + 1];
        p8 ssid_length;
        p8 security;
        //      What the access point's last beacon asked, where the kernel
        //      keeps one apart from the frame its other elements came from.
        p8 beacon_security;
        //      Whether this access point, or another by the name, asks only
        //      what the join can answer: a row is the strongest by its name,
        //      and a louder twin that asks for more is no reason to say the
        //      name cannot be joined.
        bool joinable;
        bool joined;
        b32 mbm;
        p32 frequency;
        p32 seen;
        p8 bssid[6];
        //      What its two frames offer a join, together (wifi_rsn_caps).
        wifi_rsn_caps caps;
} radio_heard;

typedef struct
{
        radio_heard heard[RADIO_AIR_MOST];
        positive count;
        p32 freshest;
        bool any;
} radio_air;

/* A cipher suite as wifi_rsn_caps keeps it. */
static p8 radio_cipher_bit(p32 suite)
{
        return suite == 0x000fac04u   ? WIFI_CIPHER_CCMP
               : suite == 0x000fac02u ? WIFI_CIPHER_TKIP
                                      : WIFI_CIPHER_OTHER;
}

/*
        What an RSN element asks of a station, as a class for a scan's row,
        and into caps what it offers a join. The join answers PSK (00-0F-AC:2),
        PSK-SHA256 (6) and SAE (8), with CCMP as the group cipher and among
        the pairwise ones, with protected management frames or without. A
        network that asks for something else is a class of its own, with the
        thing it asks for as its name: TKIP or any other cipher but CCMP, and
        FT or the extended-key SAE suites with nothing the join answers
        beside them. An element cut short, or one that names no key
        management the join knows, is WPA2 with what it did say: what is
        not there is what the join always asked, CCMP and PSK.
*/
static p8 radio_rsn_security(p8 address_to element, positive length,
                             wifi_rsn_caps address_to caps)
{
        byte_reader reader = byte_reader_open(element, length);
        byte_reader pairwise;
        positive count;
        positive which;
        p32 group;
        p16 capabilities = 0;
        bool psk = false, sae = false, ft = false, eap = false, owe = false;

        memory_fill(caps, 0, sizeof(*caps));
        caps->group = WIFI_CIPHER_CCMP;
        caps->pairwise = WIFI_CIPHER_CCMP;
        caps->akms = WIFI_AKM_PSK;
        //      The version and the group cipher, then the pairwise ciphers
        //      and the key management suites, each a count and that many
        //      four byte suites, then the capabilities.
        (void)byte_reader_skip(&reader, 2);
        group = byte_reader_u32(&reader);
        count = byte_reader_u16le(&reader);
        pairwise = byte_reader_window(&reader, 4 * count);
        count = byte_reader_u16le(&reader);
        if (!byte_reader_ok(&reader))
                return RADIO_WPA2;
        caps->group = radio_cipher_bit(group);
        caps->pairwise = 0;
        caps->akms = 0;
        while (byte_reader_left(&pairwise) >= 4)
                caps->pairwise |= radio_cipher_bit(byte_reader_u32(&pairwise));
        for (which = 0; which < count && byte_reader_left(&reader) >= 4; which++)
        {
                p32 oui = byte_reader_u24(&reader);
                p8 suite = byte_reader_u8(&reader);

                if (oui != 0x000fac)
                        caps->akms |= WIFI_AKM_OTHER;
                else if (suite == 2 || suite == 6)
                {
                        psk = true;
                        caps->akms |= suite == 2 ? WIFI_AKM_PSK : WIFI_AKM_PSK_SHA256;
                }
                else if (suite == 8)
                {
                        sae = true;
                        caps->akms |= WIFI_AKM_SAE;
                }
                else
                {
                        ft |= suite == 4 || suite == 9 || suite == 24 || suite == 25;
                        owe |= suite == 18;
                        eap |= suite != 4 && suite != 9 && suite != 24 && suite != 25 &&
                               suite != 18;
                        caps->akms |= WIFI_AKM_OTHER;
                }
        }
        if (which == count && byte_reader_left(&reader) >= 2)
                capabilities = byte_reader_u16le(&reader);
        caps->pmf = (p8)((capabilities & 0x0080 ? WIFI_PMF_CAPABLE : 0) |
                         (capabilities & 0x0040 ? WIFI_PMF_REQUIRED : 0));

        if (!psk && !sae && (ft || owe || eap))
                return ft ? RADIO_FT : owe ? RADIO_OWE : RADIO_EAP;
        if (!psk && !sae)
        {
                caps->akms |= WIFI_AKM_PSK;
                psk = true;
        }
        if (caps->group != WIFI_CIPHER_CCMP || !(caps->pairwise & WIFI_CIPHER_CCMP))
                return RADIO_TKIP;
        if (!psk)
                return RADIO_WPA3;
        if (caps->pmf & WIFI_PMF_REQUIRED)
                return RADIO_PMF;
        return sae ? RADIO_WPA23 : RADIO_WPA2;
}

/* What the elements of one frame ask of a station: the first RSN element's
   class, else a WPA vendor element, else what the capability's privacy bit
   says; and into caps what the first RSN element offers, with whether the
   first RSN extension element takes SAE's hash-to-element (bit 5 of its
   first byte). */
static p8 radio_ies_security(p8 address_to elements, positive size, p16 capability,
                             wifi_rsn_caps address_to caps)
{
        byte_reader ies = byte_reader_open(elements, elements ? size : 0);
        bool wpa = false, rsn = false, extension = false;
        p8 security = RADIO_OPEN;
        p8 h2e = 0;

        memory_fill(caps, 0, sizeof(*caps));
        while (byte_reader_left(&ies))
        {
                p8 id = byte_reader_u8(&ies);
                byte_reader data = byte_reader_vector8(&ies);
                positive span = byte_reader_left(&data);

                if (!byte_reader_ok(&ies))
                        break;
                if (id == 48 && !rsn)
                {
                        rsn = true;
                        security = radio_rsn_security((p8 address_to)byte_reader_here(&data),
                                                      span, caps);
                }
                else if (id == 244 && !extension)
                {
                        extension = true;
                        h2e = span && (byte_reader_u8(&data) & 0x20) ? 1 : 0;
                }
                else if (id == 221 && span >= 4 && byte_reader_u32(&data) == 0x0050f201)
                        wpa = true;
        }
        if (rsn)
        {
                caps->h2e = h2e;
                return security;
        }
        return wpa ? RADIO_WPA : (capability & 0x10) ? RADIO_WEP : RADIO_OPEN;
}

/* The first element of id in a frame's elements, whole, into out: its
   length, 0 for none or one cut short. */
static positive radio_ies_element(p8 address_to elements, positive size, p8 id,
                                  p8 address_to out)
{
        byte_reader ies = byte_reader_open(elements, elements ? size : 0);

        while (byte_reader_left(&ies))
        {
                byte_reader whole = ies;
                p8 kind = byte_reader_u8(&ies);
                byte_reader data = byte_reader_vector8(&ies);
                positive span = byte_reader_left(&data);

                if (!byte_reader_ok(&ies))
                        break;
                if (kind == id)
                {
                        memory_copy(out, byte_reader_take(&whole, 2 + span), 2 + span);
                        return 2 + span;
                }
        }
        return 0;
}

/* One access point of a scan dump: false for a message that is not one. seen,
   when asked for, is the access point's RSN element and RSN extension element
   as each of its two frames showed them (wifi_seen), from the same message. */
static bool radio_bss_read(netlink_header address_to header, radio_heard address_to one,
                           wifi_seen address_to seen)
{
        positive length = 0;
        positive size = 0;
        p8 address_to bss;
        p8 address_to elements;
        p8 address_to value;
        byte_reader ies;
        p8 address_to beacon;
        positive beacon_size = 0;
        bool named = false;
        p16 capability = 0;

        if (nl80211_command(header) != NL80211_CMD_NEW_SCAN_RESULTS)
                return false;
        bss = (p8 address_to)netlink_find(header, GENL_HEADER, NL80211_ATTR_BSS,
                                          address_of length);
        if (!bss)
                return false;

        memory_fill(one, 0, sizeof(*one));
        one->mbm = -10000;
        one->seen = ~(p32)0;
        one->security = RADIO_OPEN;
        value = (p8 address_to)netlink_find_span(bss, length, NL80211_BSS_BSSID,
                                                 address_of size);
        if (value && size >= 6)
                memory_copy(one->bssid, value, 6);
        value = (p8 address_to)netlink_find_span(bss, length, NL80211_BSS_SIGNAL_MBM,
                                                 address_of size);
        if (value && size >= 4)
                one->mbm = memory_load_unaligned(b32, value);
        value = (p8 address_to)netlink_find_span(bss, length, NL80211_BSS_FREQUENCY,
                                                 address_of size);
        if (value && size >= 4)
                one->frequency = memory_load_unaligned(p32, value);
        value = (p8 address_to)netlink_find_span(bss, length, NL80211_BSS_STATUS,
                                                 address_of size);
        one->joined = value && size >= 4 &&
                      memory_load_unaligned(p32, value) == NL80211_BSS_STATUS_ASSOCIATED;
        value = (p8 address_to)netlink_find_span(bss, length, NL80211_BSS_CAPABILITY,
                                                 address_of size);
        if (value && size >= 2)
                capability = memory_load_unaligned(p16, value);
        value = (p8 address_to)netlink_find_span(bss, length, NL80211_BSS_SEEN_MS_AGO,
                                                 address_of size);
        if (value && size >= 4)
                one->seen = memory_load_unaligned(p32, value);

        elements = (p8 address_to)netlink_find_span(bss, length,
                                                    NL80211_BSS_INFORMATION_ELEMENTS,
                                                    address_of size);
        beacon = (p8 address_to)netlink_find_span(bss, length, NL80211_BSS_BEACON_IES,
                                                  address_of beacon_size);
        if (!elements)
        {
                elements = beacon;
                size = beacon_size;
        }
        ies = byte_reader_open(elements, elements ? size : 0);
        while (byte_reader_left(&ies))
        {
                p8 id = byte_reader_u8(&ies);
                byte_reader data = byte_reader_vector8(&ies);
                positive span = byte_reader_left(&data);

                if (!byte_reader_ok(&ies))
                        break;
                //      The first name element is the access point's name, the one
                //      the kernel joins by: a later one is a beacon built to be
                //      read differently, and an empty or over-long first leaves
                //      it nameless.
                if (id == 0 && !named)
                {
                        named = true;
                        if (span <= RADIO_SSID_MOST)
                        {
                                memory_copy(one->ssid, byte_reader_here(&data), span);
                                one->ssid_length = (p8)span;
                        }
                }
        }
        one->security = radio_ies_security(elements, elements ? size : 0, capability,
                                           address_of one->caps);
        one->beacon_security = one->security;
        if (beacon && beacon != elements)
        {
                wifi_rsn_caps beaconed;

                one->beacon_security = radio_ies_security(beacon, beacon_size, capability,
                                                          address_of beaconed);
                wifi_caps_merge(address_of one->caps, address_of beaconed);
        }
        one->joinable = radio_security_joinable(one->security) ||
                        radio_security_joinable(one->beacon_security);
        if (seen)
        {
                memory_fill(seen, 0, sizeof(*seen));
                seen->rsn_length[0] = (p16)radio_ies_element(elements, size, 48, seen->rsn[0]);
                seen->rsnx_length[0] = (p16)radio_ies_element(elements, size, 244, seen->rsnx[0]);
                if (beacon && beacon != elements)
                {
                        seen->rsn_length[1] = (p16)radio_ies_element(beacon, beacon_size, 48,
                                                                     seen->rsn[1]);
                        seen->rsnx_length[1] = (p16)radio_ies_element(beacon, beacon_size, 244,
                                                                      seen->rsnx[1]);
                }
        }
        return true;
}

/* Keep one row per name and, when the bounded display is full, the strongest
   rows rather than whichever names the kernel happened to dump first.  Scan
   dump order is not signal order: sixty-four weak forged names used to hide a
   stronger real network merely by arriving before it.  The associated row is
   never evicted, even when its current signal is the weakest. */
static fn radio_air_keep(radio_air address_to air,
                         const radio_heard address_to one)
{
        // A hidden network's name is empty or zeros; it has no row.
        if (memory_span_byte(one->ssid, 0, one->ssid_length) == one->ssid_length)
                return;

        for (positive at = 0; at < air->count; at++)
        {
                radio_heard address_to have = air->heard + at;

                if (have->ssid_length != one->ssid_length ||
                    memory_compare(have->ssid, one->ssid, one->ssid_length))
                        continue;
                /* `joined` belongs to one BSSID, not to the SSID aggregate.
                   Never combine an associated row's bit with a stronger
                   twin's security, channel and address: that presents the
                   twin as the network carrying the live association.  An
                   associated BSS owns the row; otherwise the strongest BSS
                   owns the whole row, not three selected fields from it. */
                bool joinable = have->joinable || one->joinable;

                if ((one->joined && !have->joined) ||
                    (one->joined == have->joined && one->mbm > have->mbm))
                        *have = *one;
                have->joinable = joinable;
                return;
        }
        if (air->count < RADIO_AIR_MOST)
                air->heard[air->count++] = *one;
        else
        {
                positive weakest = positive_max;

                for (positive at = 0; at < air->count; at++)
                        if (!air->heard[at].joined &&
                            (weakest == positive_max ||
                             air->heard[at].mbm < air->heard[weakest].mbm))
                                weakest = at;
                if (weakest != positive_max &&
                    (one->joined || one->mbm > air->heard[weakest].mbm))
                        air->heard[weakest] = *one;
        }
}

static bool radio_air_seen(netlink_header address_to header, address_any context)
{
        radio_air address_to air = (radio_air address_to)context;
        radio_heard one;

        if (!radio_bss_read(header, address_of one, null))
                return true;
        /* How old the scan is, from what is not joined: the network the
           machine is on stays in the kernel's list, renewed by every
           beacon, while the rest expire after thirty seconds, so a list of
           that one alone is a scan long gone. */
        if (one.seen != ~(p32)0 && !one.joined && (!air->any || one.seen < air->freshest))
        {
                air->freshest = one.seen;
                air->any = true;
        }

        radio_air_keep(air, address_of one);
        return true;
}

/* Whether the station the machine is associated through is authorized: a
   join whose password is wrong is associated for the eight seconds its
   handshake waits, and is not joined for any of them. */
static bool radio_authorized_seen(netlink_header address_to header, address_any context)
{
        positive length = 0;
        positive size = 0;
        p8 address_to info = (p8 address_to)netlink_find(header, GENL_HEADER,
                                                         NL80211_ATTR_STA_INFO,
                                                         address_of length);
        p8 address_to flags = info ? (p8 address_to)netlink_find_span(
                                          info, length, NL80211_STA_INFO_STA_FLAGS,
                                          address_of size)
                                   : null;

        if (flags && size >= 8 &&
            (memory_load_unaligned(p32, flags + 4) & (1u << NL80211_STA_FLAG_AUTHORIZED)))
                *(bool address_to)context = true;
        return true;
}

static bool radio_authorized(nl80211 address_to session, p32 index)
{
        netlink_buffer request = {0};
        p32 sequence = netlink_sequence_take();
        bool authorized = false;

        if (!nl80211_begin(address_of request, session->family, NL80211_CMD_GET_STATION,
                           NLM_REQUEST | NLM_DUMP, sequence))
                return false;
        nl80211_attribute_u32(address_of request, NL80211_ATTR_IFINDEX, index);
        netlink_transact(session->handle, address_of request, sequence,
                         radio_authorized_seen, address_of authorized);
        return authorized;
}

/*
        The whole scans asked for here, a line each: the boot-time milliseconds
        it came in at, a space, and the name it asked for, which is none for a
        scan of the air alone. A join asks for its own name, so what a scan
        found of a network that does not answer to the wildcard (a hidden one)
        is known only of the name that was asked; and what the last scan of
        any name came to is what a status goes by. A line is kept for as long
        as it says anything, which is the time a scan is not asked for again.
*/
#define RADIO_SCAN_KEEP 16
#define RADIO_SCAN_ROOM (RADIO_SCAN_KEEP * (24 + RADIO_SSID_MOST))

typedef struct
{
        p64 when;
        string_address name;
        positive name_length;
        string_address next;
} radio_scan_line;

/* The line at the cursor, which moves past it: false at the end. */
static bool radio_scan_line_next(string_address address_to cursor, radio_scan_line address_to line)
{
        string_address at = address_to cursor;
        string_address end_of;

        while (*at == '\n')
                at++;
        if (!*at)
                return false;
        line->when = 0;
        for (; byte_is_digit((p8)*at); at++)
                line->when = line->when * 10 + (p64)(*at - '0');
        if (*at == ' ')
                at++;
        end_of = string_first_of_or_end(at, '\n');
        line->name = at;
        line->name_length = (positive)(end_of - at);
        address_to cursor = end_of;
        return true;
}

/* Milliseconds since the last whole scan asked for here, or the most there is.
   With a name, only a scan that asked for that name counts: one for another
   name, or for none, did not ask a hidden network to answer. */
static p64 radio_scan_age(p8 address_to ssid, positive ssid_length)
{
        p8 text[RADIO_SCAN_ROOM];
        p64 now = system_clock_ns(HOST_CLOCK_BOOTTIME) / 1000000;
        p64 best = ~(p64)0;
        radio_scan_line line;
        string_address cursor = (string_address)text;

        if (radio_station >= 0)
                return (p64)radio_station_number(RADIO_ASK_SCAN_AGE, ssid, ssid_length, null, 0,
                                                 0);
        if (host_read_word(RADIO_SCAN_PATH, text, sizeof(text)) <= 0)
                return best;
        while (radio_scan_line_next(address_of cursor, address_of line))
                if ((!ssid || (line.name_length == ssid_length &&
                               !memory_compare(line.name, ssid, ssid_length))) &&
                    line.when <= now && now - line.when < best)
                        best = now - line.when;
        return best;
}

/* A whole scan that asked for ssid, now: its line first, and the others as
   long as they are within the time they stand for. */
static fn radio_scan_note(p8 address_to ssid, positive ssid_length)
{
        p8 old[RADIO_SCAN_ROOM];
        p8 text[RADIO_SCAN_ROOM];
        p8 number[24];
        p64 now = system_clock_ns(HOST_CLOCK_BOOTTIME) / 1000000;
        radio_scan_line line;
        string_address cursor = (string_address)old;
        positive lines = 1;
        positive used;

        if (radio_station >= 0)
        {
                (void)radio_station_number(RADIO_ASK_SCAN_NOTE, ssid, ssid_length, null, 0, 0);
                return;
        }

        bipolar_into_string(number, (bipolar)now);
        string_copy_bounded(text, number, sizeof(text));
        string_append_bounded(text, " ", sizeof(text));
        used = string_length(text);
        memory_copy(text + used, ssid, ssid_length);
        text[used + ssid_length] = '\n';
        used += ssid_length + 1;
        text[used] = end;

        if (host_read_word(RADIO_SCAN_PATH, old, sizeof(old)) > 0)
                while (lines < RADIO_SCAN_KEEP && radio_scan_line_next(address_of cursor, address_of line))
                        if (line.when <= now && now - line.when <= RADIO_AIR_STALE_MS &&
                            !(line.name_length == ssid_length &&
                              !memory_compare(line.name, ssid, ssid_length)) &&
                            used + 24 + line.name_length + 1 < sizeof(text))
                        {
                                bipolar_into_string(number, (bipolar)line.when);
                                string_append_bounded(text, number, sizeof(text));
                                string_append_bounded(text, " ", sizeof(text));
                                used = string_length(text);
                                memory_copy(text + used, line.name, line.name_length);
                                text[used + line.name_length] = '\n';
                                used += line.name_length + 1;
                                text[used] = end;
                                lines++;
                        }
        host_state_ready();
        host_write_file(RADIO_SCAN_PATH, text, used, 0644, false);
}

/* The kernel's list of what the air holds, each access point to visit. */
static fn radio_air_dump(nl80211 address_to session, p32 index, netlink_visitor visit,
                         address_any context)
{
        netlink_buffer request = {0};
        p32 sequence = netlink_sequence_take();

        if (!nl80211_begin(address_of request, session->family, NL80211_CMD_GET_SCAN,
                           NLM_REQUEST | NLM_DUMP, sequence))
                return;
        nl80211_attribute_u32(address_of request, NL80211_ATTR_IFINDEX, index);
        netlink_transact(session->handle, address_of request, sequence, visit, context);
}

/*
        A scan asked for and waited on: true when its results came, false
        for an abort, a refusal or the time up. Active, with the wildcard
        name and the one being joined when there is one -- a scan with no
        names at all is a passive one, a tenth of a second or more on every
        channel -- and flushed, so the list after it holds what answered
        and nothing the cache kept from before. For a join, only the 2.4
        and 5 GHz channels: those at 6 GHz are all listened to rather than
        asked, which on a radio with that band made the whole scan outlast
        the five seconds it is given, so a network at 6 GHz alone (WPA3 with
        hash-to-element, which the join has) is not one a join finds. A
        radio refuses a list with a channel it has none of, at once, so the
        join asks for the widest list first
        (channel 14, 144 and 169 to 177 as well, where a regulatory domain
        has them), then for the list most radios take, then for the whole
        scan.
*/
#define RADIO_SCAN_WHOLE 0
#define RADIO_SCAN_JOIN 1
#define RADIO_SCAN_JOIN_WIDE 2

//      First, last and step in MHz, and 1 for the channels only the widest
//      list holds.
static const p16 radio_join_channels[][4] = {
    {2412, 2472, 5, 0},  {5180, 5320, 20, 0}, {5500, 5700, 20, 0}, {5745, 5825, 20, 0},
    {2484, 2484, 5, 1},  {5720, 5720, 20, 1}, {5845, 5885, 20, 1}};

typedef struct
{
        nl80211 address_to session;
        p8 address_to ssid;
        positive ssid_length;
        p32 index;
        p32 sequence;
        bool acked;
        bool whole;
        bool ended;
        bool refused;
        bipolar status;
} radio_scan_turn;

/* One message of a scan's wait: the acknowledgment of the trigger and then
   the results or the abort. */
static bool radio_scan_take(netlink_header address_to header, address_any context)
{
        radio_scan_turn address_to turn = (radio_scan_turn address_to)context;
        p8 command;

        // Refused for any reason but a scan already running: no results are
        // coming, so the cached ones stand.
        if (header->type == NLMSG_IS_ERROR && header->sequence == turn->sequence &&
            !turn->acked)
        {
                turn->status = netlink_status(header, false);
                turn->acked = true;
                turn->whole = turn->status >= 0;
                turn->refused = turn->status < 0 && turn->status != -16;
                turn->ended = turn->refused;
                return !turn->ended;
        }

        command = nl80211_news(turn->session, header, turn->index);
        if (command != NL80211_CMD_NEW_SCAN_RESULTS && command != NL80211_CMD_SCAN_ABORTED)
                return true;

        //      When, and for which name: the next join asks the same of its own.
        if (turn->whole && command == NL80211_CMD_NEW_SCAN_RESULTS)
                radio_scan_note(turn->ssid, turn->ssid_length);
        turn->whole = command == NL80211_CMD_NEW_SCAN_RESULTS;
        turn->ended = true;
        return false;
}

static bool radio_air_scan(nl80211 address_to session, p32 index, p8 address_to ssid,
                           positive ssid_length, positive tier)
{
        netlink_buffer request = {0};
        netlink_buffer reply = {0};
        network_deadline deadline;
        p32 sequence = netlink_sequence_take();
        radio_scan_turn turn = {.session = session, .index = index, .sequence = sequence};
        positive nest;

        if (ssid_length > RADIO_SSID_MOST)
                ssid_length = 0;
        turn.ssid = ssid;
        turn.ssid_length = ssid_length;
        if (!session->scan ||
            socket_option_set(session->handle, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP,
                              address_of session->scan, sizeof(session->scan)) < 0)
                return false;
        scope_exit(netlink_forget(address_of request); netlink_forget(address_of reply));
        if (!nl80211_begin(address_of request, session->family, NL80211_CMD_TRIGGER_SCAN,
                           NLM_REQUEST | NLM_ACK, sequence))
                return false;
        nl80211_attribute_u32(address_of request, NL80211_ATTR_IFINDEX, index);
        nest = netlink_nested_begin(address_of request, NL80211_ATTR_SCAN_SSIDS);
        netlink_attribute_add(address_of request, 1, null, 0);
        if (ssid_length)
                netlink_attribute_add(address_of request, 2, ssid, ssid_length);
        netlink_nested_end(address_of request, nest);
        if (tier)
        {
                p16 channel = 0;

                nest = netlink_nested_begin(address_of request,
                                            NL80211_ATTR_SCAN_FREQUENCIES);
                for (positive band = 0;
                     band < sizeof(radio_join_channels) / sizeof(radio_join_channels[0]);
                     band++)
                        for (p32 mhz = radio_join_channels[band][0];
                             (!radio_join_channels[band][3] || tier == RADIO_SCAN_JOIN_WIDE) &&
                             mhz <= radio_join_channels[band][1];
                             mhz += radio_join_channels[band][2])
                                netlink_attribute_add(address_of request, ++channel,
                                                      address_of mhz, 4);
                netlink_nested_end(address_of request, nest);
        }
        nl80211_attribute_u32(address_of request, NL80211_ATTR_SCAN_FLAGS,
                              NL80211_SCAN_FLAG_FLUSH);
        if (request.failed ||
            socket_send(session->handle, request.bytes, request.used, 0, 0, 0) < 0 ||
            !network_deadline_begin(address_of deadline, RADIO_AIR_WAIT_SECONDS, 0))
                return false;

        while (network_wait_readable_until(session->handle, address_of deadline) > 0)
        {
                p32 local_port = 0;
                bipolar got = netlink_receive(session->handle, address_of reply,
                                              address_of local_port);

                if (got == NETWORK_INTERRUPTED || got == NETWORK_TRY_AGAIN)
                        continue;
                if (got < 0)
                        break;
                netlink_each(address_of reply, radio_scan_take, address_of turn);
                if (turn.ended)
                {
                        return turn.refused ? tier && turn.status == -22 &&
                                                  radio_air_scan(session, index, ssid,
                                                                 ssid_length, tier - 1)
                                            : turn.whole;
                }
        }
        return false;
}

/*
        The networks in the air, strongest first. fresh is RADIO_AIR_CACHED
        for what was heard last and nothing more, RADIO_AIR_STALE for a new
        scan when that is stale, RADIO_AIR_NOW for one whatever it is; a
        scan needs root and a link that is up. False when there is no
        wireless interface to ask.
*/
#define RADIO_AIR_CACHED 0
#define RADIO_AIR_STALE 1
#define RADIO_AIR_NOW 2
#define RADIO_AIR_JOINABLE 4

static bool radio_air_take(radio_air address_to air, p8 fresh)
{
        nl80211 session;
        nl80211_iface iface;
        bool joinable = (fresh & RADIO_AIR_JOINABLE) != 0;

        fresh &= RADIO_AIR_NOW | RADIO_AIR_STALE;

        memory_fill(air, 0, sizeof(*air));
        if (nl80211_open(address_of session) < 0)
                return false;
        scope_exit(nl80211_close(address_of session));
        if (nl80211_interface(address_of session, address_of iface) < 0)
                return false;
        radio_air_dump(address_of session, iface.index, radio_air_seen, air);
        if (fresh && bowl_is_root() &&
            (fresh == RADIO_AIR_NOW || !air->any || air->freshest > RADIO_AIR_STALE_MS ||
             radio_scan_age(null, 0) > RADIO_AIR_STALE_MS))
        {
                bipolar route = netlink_open_groups(0);

                if (route >= 0)
                {
                        netlink_link_up((b32)route, iface.index);
                        socket_close((b32)route);
                }
                radio_air_scan(address_of session, iface.index, null, 0,
                               joinable ? RADIO_SCAN_JOIN_WIDE : RADIO_SCAN_WHOLE);
                memory_fill(air, 0, sizeof(*air));
                radio_air_dump(address_of session, iface.index, radio_air_seen, air);
        }
        {
                bool joined = false;

                for (positive at = 0; at < air->count; at++)
                        joined |= air->heard[at].joined;
                if (joined && !radio_authorized(address_of session, iface.index))
                        for (positive at = 0; at < air->count; at++)
                                air->heard[at].joined = false;
        }

        for (positive at = 1; at < air->count; at++)
                for (positive back = at; back > 0 && air->heard[back].mbm >
                                                         air->heard[back - 1].mbm;
                     back--)
                {
                        radio_heard swap = air->heard[back];

                        air->heard[back] = air->heard[back - 1];
                        air->heard[back - 1] = swap;
                }
        return true;
}

/*
        The access points given up on in the last hour: sent the machine
        away, stopped answering, or failed a join. The next join tries any
        other that answers to the same name before one of these, and of
        these the one given up on longest ago first. Anybody in range can
        beacon twins of a saved network that ask for WPA2 and fail every
        join, and louder than the real one: with four remembered for a
        minute and the loudest first among those not remembered, five that
        failed quickly or four that failed slowly took turns falling off the
        list and being tried again, and the real one never was. Sixty-four
        kept for an hour hold sixty-three twins that each take the longest a
        join can, so each is tried once before the real one is, and again
        only after it; more twins than that are the limit of any list this
        size.
*/
#define RADIO_AVOID_PATH NET_STATE_DIR "/wifi.avoid"
#define RADIO_AVOID_MOST 64
#define RADIO_AVOID_MS 3600000u

typedef struct
{
        p8 bssid[6];
        p8 unused[2];
        p64 until;
} radio_avoid;

_Static_assert(RADIO_AVOID_MOST * sizeof(radio_avoid) == RADIO_ASK_AVOID_ROOM,
               "the broker's answer holds the whole avoid list");

static positive radio_avoid_load(radio_avoid address_to into)
{
        bipolar got;
        positive count;
        positive kept = 0;
        p64 now = system_clock_ns(HOST_CLOCK_BOOTTIME) / 1000000;

        if (radio_station >= 0)
        {
                radio_ask ask;
                radio_answer answer;

                memory_fill(address_of ask, 0, sizeof(ask));
                ask.what = RADIO_ASK_AVOID_LOAD;
                radio_station_ask(address_of ask, address_of answer);
                if (answer.count > RADIO_AVOID_MOST)
                        return 0;
                memory_copy(into, answer.avoid, (positive)answer.count * sizeof(radio_avoid));
                return (positive)answer.count;
        }
        got = file_read_once_at(AT_FDCWD, RADIO_AVOID_PATH, into,
                                RADIO_AVOID_MOST * sizeof(radio_avoid));
        count = got > 0 ? (positive)got / sizeof(radio_avoid) : 0;

        for (positive at = 0; at < count; at++)
                if (into[at].until > now && into[at].until - now <= RADIO_AVOID_MS)
                        into[kept++] = into[at];
        return kept;
}

static COLD fn radio_avoid_add(p8 address_to bssid)
{
        radio_avoid list[RADIO_AVOID_MOST + 1];
        positive count;
        positive kept = 1;

        if (!bssid || !wifi_mac_set(bssid))
                return;
        if (radio_station >= 0)
        {
                (void)radio_station_number(RADIO_ASK_AVOID_ADD, null, 0, bssid, 0, 0);
                return;
        }
        count = radio_avoid_load(list + 1);
        memory_fill(list, 0, sizeof(list[0]));
        memory_copy(list[0].bssid, bssid, 6);
        list[0].until = system_clock_ns(HOST_CLOCK_BOOTTIME) / 1000000 + RADIO_AVOID_MS;
        for (positive at = 1; at <= count && kept < RADIO_AVOID_MOST; at++)
                if (memory_compare(list[at].bssid, bssid, 6))
                        list[kept++] = list[at];
        host_state_ready();
        host_write_file(RADIO_AVOID_PATH, (p8 address_to)list, kept * sizeof(radio_avoid),
                        0600, false);
}

typedef struct
{
        p8 address_to ssid;
        positive ssid_length;
        radio_avoid address_to avoid;
        positive avoid_count;
        radio_heard best;
        //      When the best was given up on until, 0 for never.
        p64 until;
        bool found;
        bool secured;
        bool fits;
        //      What the saved network has taught the join, whether its secret
        //      is a key of 64 hex digits, and the best's elements as seen.
        p8 rules;
        bool key_only;
        wifi_seen seen;
} radio_pick;

/* Whether what an access point's frame asks of a station is what a saved
   network wants: WPA2 or WPA3 for one with a password, nothing for one
   without. */
static bool radio_security_fits(bool secured, p8 security)
{
        return secured ? security == RADIO_WPA2 || security == RADIO_WPA23 ||
                             security == RADIO_WPA3 || security == RADIO_PMF
                       : security == RADIO_OPEN;
}

/* The strongest the kernel still lists by the name, one given up on only
   if no other is, and the one given up on longest ago before the rest. */
static bool radio_pick_seen(netlink_header address_to header, address_any context)
{
        radio_pick address_to pick = (radio_pick address_to)context;
        radio_heard one;
        wifi_seen seen;
        p64 until = 0;
        bool fits;

        if (!radio_bss_read(header, address_of one, address_of seen) ||
            one.seen > RADIO_AIR_STALE_MS ||
            !wifi_mac_set(one.bssid) || one.ssid_length != pick->ssid_length ||
            memory_compare(one.ssid, pick->ssid, pick->ssid_length))
                return true;
        for (positive at = 0; at < pick->avoid_count; at++)
                if (!memory_compare(pick->avoid[at].bssid, one.bssid, 6))
                        until = pick->avoid[at].until;
        //      What the saved network asks of an access point: WPA2 for one
        //      with a password, nothing for one without. A twin by the same
        //      name that offers anything else, and beacons louder than the
        //      real one, is the last resort and not the first: the kernel
        //      will not join it, and asking cost a join its whole timeout.
        //      The frame the kernel keeps the elements of is the latest, a
        //      probe response as well as a beacon, and any station can send
        //      one in the real access point's name without an RSN element:
        //      what its last beacon asked counts as well, so one forged
        //      response does not push the real one out behind a twin. And
        //      of a secured one, what the join's plan would refuse: a twin
        //      that offers less than the network taught it (protection,
        //      WPA3) is exactly what a downgrade needs, and goes last.
        fits = radio_security_fits(pick->secured, one.security) ||
               radio_security_fits(pick->secured, one.beacon_security);
        if (pick->secured && fits)
        {
                wifi_plan plan;

                fits = wifi_join_plan(MOONWATER_STRICT_NET, address_of one.caps, pick->rules,
                                      pick->key_only, address_of plan) == 0;
        }
        if (pick->found && (fits < pick->fits ||
                            (fits == pick->fits &&
                             (until > pick->until ||
                              (until == pick->until && one.mbm <= pick->best.mbm)))))
                return true;
        pick->best = one;
        pick->found = true;
        pick->until = until;
        pick->fits = fits;
        pick->seen = seen;
        return true;
}

/* Whether the last choice of an access point answered "none" from the scan of
   a moment ago and not from one of its own. */
static bool radio_scan_declined;

/*
        The access point a join asks for by its BSSID. A join by name alone
        took whatever the kernel's cache named, and the cache keeps an
        access point for thirty seconds after it last heard it: after a
        disconnect that was the one that had just stopped answering, and
        authentication with it timed out, again and again. So the cache is
        trusted for anything but an access point given up on, and when it
        has nothing else by the name it is flushed and the air scanned for
        the name -- by a join that was asked for, always, and by the
        machine's own pass (force false) only when the cache has an access
        point to doubt or no scan asked for this name came in the last
        thirty seconds, as radio_air_take holds a status to: a saved network
        out of range was scanned for, five seconds of it, again for every
        network and every pass. 1 with one
        chosen, 0 when the scan, or the one a moment ago, heard none by this
        name, -1 when no scan could be had: the join then goes by name. With
        one chosen, caps is what it offers and seen what it showed.
*/
static COLD bipolar radio_bss_choose(nl80211 address_to session, p32 index,
                                     p8 address_to ssid, positive ssid_length, bool secured,
                                     p8 address_to bssid, p32 address_to frequency,
                                     bool force, p8 rules, bool key_only,
                                     wifi_rsn_caps address_to caps, wifi_seen address_to seen)
{
        radio_scan_declined = false;
        radio_avoid avoid[RADIO_AVOID_MOST];
        radio_pick pick = {.ssid = ssid,
                           .ssid_length = ssid_length,
                           .avoid = avoid,
                           .secured = secured,
                           .rules = rules,
                           .key_only = key_only};
        bipolar heard = 1;
        bool scanned = false;

        pick.avoid_count = radio_avoid_load(avoid);
        radio_air_dump(session, index, radio_pick_seen, address_of pick);
        if ((!pick.found || pick.until || !pick.fits) &&
            (force || pick.found || radio_scan_age(ssid, ssid_length) > RADIO_AIR_STALE_MS))
        {
                scanned = true;
                heard = radio_air_scan(session, index, ssid, ssid_length,
                                       RADIO_SCAN_JOIN_WIDE)
                            ? 1
                            : -1;
                pick.found = false;
                radio_air_dump(session, index, radio_pick_seen, address_of pick);
        }
        //      Nothing by the name and no scan asked for: not in range as of
        //      the scan a moment ago, which is no new failure to wait longer for.
        radio_scan_declined = !pick.found && !scanned;
        radio_pick_given_up = pick.found && pick.until != 0;
        if (!pick.found)
                return heard > 0 ? 0 : -1;
        memory_copy(bssid, pick.best.bssid, 6);
        address_to frequency = pick.best.frequency;
        address_to caps = pick.best.caps;
        address_to seen = pick.seen;
        return 1;
}

/* A channel's number from its frequency, as the kernel numbers it
   (ieee80211_freq_khz_to_channel), and 0 off every band. The sum here
   before took 5000 from anything at 4900 MHz or over in unsigned
   arithmetic: the 4.9 GHz band printed as channel 858993439, the 6 GHz
   band's channel 2 as 187 and 60 GHz as 6 GHz channels by the thousand. */
static p32 radio_channel(p32 mhz)
{
        return mhz == 2484                    ? 14
               : mhz >= 2407 && mhz < 2484    ? (mhz - 2407) / 5
               : mhz >= 4910 && mhz <= 4980   ? (mhz - 4000) / 5
               : mhz >= 5000 && mhz < 5925    ? (mhz - 5000) / 5
               : mhz == 5935                  ? 2
               : mhz >= 5950 && mhz <= 7115   ? (mhz - 5950) / 5
               : mhz >= 58320 && mhz <= 70200 ? (mhz - 56160) / 2160
                                              : 0;
}

static radio_heard address_to radio_air_find(radio_air address_to air, string_address ssid)
{
        positive length = string_length(ssid);

        for (positive at = 0; at < air->count; at++)
                if (air->heard[at].ssid_length == length &&
                    !memory_compare(air->heard[at].ssid, ssid, length))
                        return air->heard + at;
        return null;
}

/*
        The station's filter: its architecture, then the calls that need no
        more than their number, then the four whose arguments decide, then
        death. A socket is netlink (route or generic) or the EAPOL packet
        socket and nothing else; an option is a netlink group joined or left,
        or extended acks; memory is never executable. Its broker's channel is
        a socket it already holds, spoken to with sendto and recvfrom.
*/
#define RADIO_NETLINK_DROP_MEMBERSHIP 2

static COLD fn radio_filter_build(net_filter address_to filter)
{
        static const p32 plain[] = {
            (p32)syscall(recvfrom), (p32)syscall(sendto), (p32)syscall(ppoll),
            (p32)syscall(clock_gettime), (p32)syscall(getrandom),
            (p32)syscall(close), (p32)syscall(bind), (p32)syscall(getsockname),
            (p32)syscall(nanosleep), (p32)syscall(clock_nanosleep),
            (p32)syscall(munmap), (p32)syscall(mremap), (p32)syscall(madvise),
            (p32)syscall(exit_group), (p32)syscall(exit),
            (p32)syscall(rt_sigreturn), (p32)syscall(restart_syscall),
#ifdef NET_APART_RISCV_HWPROBE
            NET_APART_RISCV_HWPROBE,
#endif
        };
        static const p32 datagram[] = {SOCK_DGRAM};
        static const p32 netlink_protocols[] = {NETLINK_ROUTE, NETLINK_GENERIC};
        static const p32 netlink_options[] = {NETLINK_ADD_MEMBERSHIP,
                                              RADIO_NETLINK_DROP_MEMBERSHIP,
                                              NETLINK_EXT_ACK};
        const p32 eapol[] = {(p32)network_order_16(ETH_P_PAE)};
        positive count = array_count(plain);
        positive call;
        positive family;

        filter->used = 0;
        net_filter_put(filter, NET_FILTER_LOAD, 0, 0, NET_FILTER_ARCH);
        net_filter_put(filter, NET_FILTER_EQUAL, 1, 0, NET_APART_ARCH);
        net_filter_answer(filter, SECCOMP_RET_KILL_PROCESS);
        net_filter_put(filter, NET_FILTER_LOAD, 0, 0, NET_FILTER_NUMBER);
        for (positive at = 0; at < count; at++)
                net_filter_put(filter, NET_FILTER_EQUAL, (p8)(count - at), 0, plain[at]);
        net_filter_put(filter, NET_FILTER_ALWAYS, 0, 0, 1);
        net_filter_answer(filter, SECCOMP_RET_ALLOW);

        //      socket: netlink route or generic, or a packet socket of EAPOL's
        //      ethertype; datagrams, with SOCK_NONBLOCK or SOCK_CLOEXEC.
        call = net_filter_when(filter, (p32)syscall(socket));
        net_filter_load(filter, 0);
        family = net_filter_when(filter, AF_NETLINK);
        net_filter_require(filter, 1, ~(p32)(SOCK_NONBLOCK | SOCK_CLOEXEC), datagram,
                           array_count(datagram));
        net_filter_require(filter, 2, ~(p32)0, netlink_protocols,
                           array_count(netlink_protocols));
        net_filter_answer(filter, SECCOMP_RET_ALLOW);
        net_filter_past(filter, family);
        family = net_filter_when(filter, AF_PACKET);
        net_filter_require(filter, 1, ~(p32)(SOCK_NONBLOCK | SOCK_CLOEXEC), datagram,
                           array_count(datagram));
        net_filter_require(filter, 2, ~(p32)0, eapol, array_count(eapol));
        net_filter_answer(filter, SECCOMP_RET_ALLOW);
        net_filter_past(filter, family);
        net_filter_answer(filter, SECCOMP_RET_KILL_PROCESS);
        net_filter_past(filter, call);

        //      setsockopt: a netlink group joined or left, extended acks.
        call = net_filter_when(filter, (p32)syscall(setsockopt));
        net_filter_load(filter, 1);
        family = net_filter_when(filter, SOL_NETLINK);
        net_filter_require(filter, 2, ~(p32)0, netlink_options,
                           array_count(netlink_options));
        net_filter_answer(filter, SECCOMP_RET_ALLOW);
        net_filter_past(filter, family);
        net_filter_answer(filter, SECCOMP_RET_KILL_PROCESS);
        net_filter_past(filter, call);

        //      mmap: anything but executable.
        call = net_filter_when(filter, (p32)syscall(mmap));
        net_filter_load(filter, 2);
        net_filter_put(filter, NET_FILTER_ANY_BIT, 1, 0, NET_APART_EXECUTABLE);
        net_filter_answer(filter, SECCOMP_RET_ALLOW);
        net_filter_answer(filter, SECCOMP_RET_KILL_PROCESS);
        net_filter_past(filter, call);

        net_filter_answer(filter, SECCOMP_RET_KILL_PROCESS);
}

#define RADIO_CAP_NET_ADMIN 12
#define RADIO_CAP_NET_RAW 13
#define RADIO_CAPABILITY_VERSION 0x20080522u
#define RADIO_PR_SET_KEEPCAPS 8
#define RADIO_PR_CAPBSET_DROP 24
#define RADIO_PR_CAP_AMBIENT 47
#define RADIO_PR_CAP_AMBIENT_CLEAR_ALL 4

/*
        The station made what it may be: every capability but CAP_NET_ADMIN
        (nl80211, the link up) and CAP_NET_RAW (the EAPOL socket) out of its
        bounding set, nobody when it is root, those two raised again and no
        other, nothing ambient, not dumpable, no new privileges, then its
        filter. A user namespace that maps root and not nobody keeps root,
        which owns nothing outside it (net_apart_nobody). False at the first
        refusal, and nothing is joined.
*/
static COLD bool radio_station_confine(void)
{
        struct
        {
                p32 version;
                b32 pid;
        } head = {RADIO_CAPABILITY_VERSION, 0};
        struct
        {
                p32 effective;
                p32 permitted;
                p32 inheritable;
        } data[2] = {{0}};
        p32 keep = 1u << RADIO_CAP_NET_ADMIN | 1u << RADIO_CAP_NET_RAW;
        net_filter filter;

        for (positive cap = 0; cap < 64; cap++)
        {
                bipolar dropped;

                if (cap == RADIO_CAP_NET_ADMIN || cap == RADIO_CAP_NET_RAW)
                        continue;
                dropped = system_call_5(syscall(prctl), RADIO_PR_CAPBSET_DROP, cap, 0, 0, 0);
                if (dropped == -EINVAL)
                        break;
                if (dropped < 0)
                        return false;
        }
        if (system_call_5(syscall(prctl), RADIO_PR_SET_KEEPCAPS, 1, 0, 0, 0) < 0 ||
            !net_apart_nobody())
                return false;
        data[0].effective = data[0].permitted = keep;
        radio_filter_build(address_of filter);
        return system_call_2(syscall(capset), (positive)address_of head,
                             (positive)data) == 0 &&
               system_call_5(syscall(prctl), RADIO_PR_CAP_AMBIENT,
                             RADIO_PR_CAP_AMBIENT_CLEAR_ALL, 0, 0, 0) >= 0 &&
               system_call_5(syscall(prctl), PR_SET_DUMPABLE, 0, 0, 0, 0) == 0 &&
               system_call_5(syscall(prctl), PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0 &&
               net_filter_install(address_of filter);
}

/* The station: confined, then the join, its status to the broker, and for
   as long as the link stands, its keeper. */
static DEAD_END COLD fn radio_station_run(b32 channel, bipolar broker, p8 address_to ssid,
                                          positive ssid_length, p8 address_to pass,
                                          positive pass_length, bool force)
{
        wifi_link link = {.session = {.handle = -1}, .eapol = -1};
        p32 kept[1] = {(p32)channel};
        radio_ask ask;
        radio_answer answer;
        bipolar failed = WIFI_REFUSE_CONFINE;
        bool confined;

        radio_station = channel;
        confined = system_call_5(syscall(prctl), PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0) == 0 &&
                   system_call(syscall(getppid)) == broker &&
                   descriptors_close_outside(3, kept, 1) && radio_station_confine();
        if (confined)
                failed = nl80211_join(ssid, ssid_length, pass, pass_length, force,
                                      address_of link);
        crypto_forget(pass, pass_length);
        radio_secrets_forget();
        memory_fill(address_of ask, 0, sizeof(ask));
        ask.what = RADIO_ASK_JOINED;
        ask.status = failed;
        radio_station_ask(address_of ask, address_of answer);
        if (!failed)
        {
                //      The join's scan news is nothing to the keeper, which
                //      would wake for every scan anyone asked for.
                if (link.session.scan)
                        socket_option_set(link.session.handle, SOL_NETLINK,
                                          RADIO_NETLINK_DROP_MEMBERSHIP,
                                          address_of link.session.scan,
                                          sizeof(link.session.scan));
                wifi_keep(address_of link);
        }
        wifi_link_close(address_of link);
        system_call_1(syscall(exit_group), 0);
        __builtin_unreachable();
}

/* A status the join can return: joined, an errno, an access point's reason
   for ending the handshake, or one of the join's own refusals. */
static CONST COLD bool radio_join_status(b64 status)
{
        return (status <= 0 && status >= -4095) ||
               (status <= -1000 && status > -1000 - 65536) ||
               (status >= WIFI_REFUSE_FIRST && status <= WIFI_REFUSE_LAST) ||
               status == WIFI_REFUSE_CONFINE;
}

/* What the broker does for one question, with checks on every field: a name
   no longer than a name, a line to log only as a number it has a line for. */
static COLD fn radio_broker_answer(radio_ask address_to ask, radio_answer address_to answer)
{
        p8 address_to ssid = ask->named ? ask->ssid : null;
        positive ssid_length = ask->named ? ask->ssid_length : 0;

        memory_fill(answer, 0, sizeof(*answer));
        if (ssid_length > sizeof(ask->ssid))
                return;
        switch (ask->what)
        {
        case RADIO_ASK_RULES_GET:
                answer->answer = ssid ? radio_rules_get(ssid, ssid_length) : 0;
                break;
        case RADIO_ASK_RULES_PUT:
                if (ssid)
                        radio_rules_put(ssid, ssid_length, ask->mask, ask->rules);
                break;
        case RADIO_ASK_AVOID_LOAD:
                answer->count = radio_avoid_load((radio_avoid address_to)answer->avoid);
                break;
        case RADIO_ASK_AVOID_ADD:
                radio_avoid_add(ask->bssid);
                break;
        case RADIO_ASK_SCAN_AGE:
                answer->answer = (bipolar)radio_scan_age(ssid, ssid_length);
                break;
        case RADIO_ASK_SCAN_NOTE:
                if (ssid)
                        radio_scan_note(ssid, ssid_length);
                break;
        case RADIO_ASK_SAE_BUDGET:
                answer->answer = wifi_sae_budget(ask->bssid);
                break;
        case RADIO_ASK_SAID:
                if (ask->status == RADIO_SAID_NO_CLOCK)
                        radio_keeper_said("no clock to check the link by");
                else if (ask->status == RADIO_SAID_SOCKETS)
                        radio_keeper_said("its sockets failed");
                break;
        }
}

/*
        The broker: root, detached, holding the keeper's lock. It forks the
        station, forgets every password it carries, then answers questions
        until the station ends; the join's status goes to whoever asked for
        the join, once, through told. A status that is no status the join
        returns is the station's failure.
*/
static DEAD_END COLD fn radio_broker(b32 told, p8 address_to ssid, positive ssid_length,
                                     p8 address_to pass, positive pass_length, bool force)
{
        b32 ends[2];
        bipolar broker = system_call(syscall(getpid));
        bipolar station;
        positive raw = 0;
        bool joined = false;

        if (host_lock_file(RADIO_KEEPER_PATH, HOST_FILE_SETLK, null) < 0 ||
            system_call_4(syscall(socketpair), AF_UNIX,
                          RADIO_SOCK_SEQPACKET | SOCK_CLOEXEC, 0, (positive)ends) < 0)
                system_call_1(syscall(exit_group), 1);
        station = system_fork();
        if (station < 0)
                system_call_1(syscall(exit_group), 1);
        if (!station)
                radio_station_run(ends[1], broker, ssid, ssid_length, pass, pass_length, force);
        system_close(ends[1]);
        crypto_forget(pass, pass_length);
        radio_secrets_forget();

        for (;;)
        {
                radio_ask ask;
                radio_answer answer;
                network_deadline deadline;
                bipolar ready;

                //      The station asks when it has to and may keep the link
                //      for days: an hour's wait at a time, for ever.
                if (!network_deadline_begin(address_of deadline, 3600, 0))
                        break;
                ready = network_wait_readable_until(ends[0], address_of deadline);
                if (!ready)
                        continue;
                if (ready < 0 ||
                    socket_receive(ends[0], address_of ask, sizeof(ask), MSG_DONTWAIT, null,
                                   null) != (bipolar)sizeof(ask))
                        break;
                if (ask.what == RADIO_ASK_JOINED && told >= 0)
                {
                        b64 status = ask.status;

                        if (!radio_join_status(status))
                                status = WIFI_REFUSE_APART;
                        joined = status == 0;
                        if (joined)
                        {
                                host_state_ready();
                                host_write_file(RADIO_KEPT_PATH, (p8 address_to) "1\n", 2, 0644,
                                                false);
                        }
                        (void)system_write_all((positive)told, address_of status, sizeof(status));
                        system_close(told);
                        told = -1;
                        memory_fill(address_of answer, 0, sizeof(answer));
                }
                else if (ask.what == RADIO_ASK_JOINED)
                        break;
                else
                        radio_broker_answer(address_of ask, address_of answer);
                if (socket_send(ends[0], address_of answer, sizeof(answer), MSG_NOSIGNAL, null,
                                0) != (bipolar)sizeof(answer))
                        break;
        }
        system_close(ends[0]);
        system_call_2(syscall(kill), (positive)station, SIGKILL);
        (void)system_wait4_retry(station, address_of raw, 0, null);
        //      A station that ended of its own -- the link went -- leaves
        //      nothing kept. One that died (a signal, its filter) left a link
        //      whose keys nobody keeps: wifi.kept stays, so the machine's next
        //      pass joins again, and the log says so.
        if (joined && (raw & 0x7f) == 0)
                system_remove_at(AT_FDCWD, RADIO_KEPT_PATH, 0);
        else if (joined)
                radio_keeper_said("its station died; the link is joined again");
        system_call_1(syscall(exit_group), 0);
        __builtin_unreachable();
}

/* A join, apart: whatever kept the last link is ended, a broker is forked
   that outlives this process, and its word of the join is waited for. */
static COLD bipolar radio_join_apart(p8 address_to ssid, positive ssid_length,
                                     p8 address_to pass, positive pass_length, bool force)
{
        b32 told[2];
        b64 status = WIFI_REFUSE_APART;
        bipolar got;

        radio_keeper_stop();
        if (system_call_2(syscall(pipe2), (positive)told, O_CLOEXEC) < 0)
                return WIFI_REFUSE_APART;
        //      The broker's stdio is replaced; an end that is one of the
        //      first three would be lost to that.
        if (told[1] < 3 || told[0] < 3)
        {
                system_close(told[0]);
                system_close(told[1]);
                return WIFI_REFUSE_APART;
        }
        if (host_detach((p32)told[1], descriptors_none))
                radio_broker(told[1], ssid, ssid_length, pass, pass_length, force);
        system_close(told[1]);
        got = system_read_retry((positive)told[0], address_of status, sizeof(status));
        system_close(told[0]);
        return got == (bipolar)sizeof(status) ? (bipolar)status : WIFI_REFUSE_APART;
}

// One byte spelled \xNN, as a name that is shown spells what it does not show: four bytes, not ended.
static positive radio_spell(p8 address_to into, p8 byte)
{
        into[0] = '\\';
        into[1] = 'x';
        return 2 + memory_into_hex(into + 2, address_of byte, 1);
}

/* A name made safe to print, and how many columns it takes. */
static positive radio_display(p8 address_to into, positive room, p8 address_to name,
                              positive length)
{
        positive used = 0;
        positive columns = 0;
        positive at = 0;

        while (at < length && used + 5 < room)
        {
                bool printable;
                positive step = file_terminal_step(name + at, length - at, true,
                                                   address_of printable);

                if (printable && used + step < room)
                {
                        memory_copy(into + used, name + at, step);
                        used += step;
                        columns++;
                        at += step;
                        continue;
                }
                //      What is not shown is spelled a byte at a time.
                used += radio_spell(into + used, name[at]);
                columns += 4;
                at++;
        }
        into[used] = end;
        return columns;
}

/* One row: marker, name, signal, security, band and channel. */
static fn radio_air_row(radio_heard address_to heard, positive width, bool saved)
{
        p8 name[RADIO_SSID_MOST * 4 + 1];
        p8 line[320];
        p8 number[24];
        positive columns = radio_display(name, sizeof(name), heard->ssid, heard->ssid_length);
        bipolar dbm = heard->mbm / 100;
        p32 mhz = heard->frequency;
        positive bars = dbm >= -55 ? 4 : dbm >= -67 ? 3 : dbm >= -75 ? 2 : dbm >= -85 ? 1 : 0;
        string_address band = mhz >= 58320  ? (string_address) "60 GHz"
                              : mhz >= 5925 ? (string_address) "6 GHz"
                              : mhz >= 4900 ? (string_address) "5 GHz"
                                            : (string_address) "2.4 GHz";
        p32 channel = radio_channel(mhz);

        radio_line(line, sizeof(line), heard->joined ? (string_address) "* "
                                       : saved       ? (string_address) "+ "
                                                     : (string_address) "  ",
                   name, null, null, null);
        for (; columns < width + 2; columns++)
                string_append_bounded(line, (string_address) " ", sizeof(line));
        bipolar_into_string(number, dbm);
        for (positive pad = string_length(number); pad < 4; pad++)
                string_append_bounded(line, (string_address) " ", sizeof(line));
        radio_line(line + string_length(line), sizeof(line) - string_length(line), number,
                   (string_address) " dBm ", null, null, null);
        for (positive at = 0; at < 4; at++)
                string_append_bounded(line, at < bars ? (string_address) "#" : (string_address) ".",
                                      sizeof(line));
        string_append_bounded(line, (string_address) "  ", sizeof(line));
        string_append_bounded(line, radio_security_words[heard->security], sizeof(line));
        for (positive pad = string_length(radio_security_words[heard->security]); pad < 8;
             pad++)
                string_append_bounded(line, (string_address) " ", sizeof(line));
        bipolar_into_string(number, (bipolar)channel);
        radio_line(line + string_length(line), sizeof(line) - string_length(line), band,
                   (string_address) " ch ", number, null, null);
        string_format(log, host_label "%s\n", line);
}

/* ---- wifi: what the last join said, and the password asked for ---- */

/* The reason a join gave, as the words that follow the network's name. */
/* What the join's plan says of the access point heard by a saved network's
   name, the refusal or 0: a twin that offers less than the network taught
   is said for what it is, whether or not the machine's pass has asked it. */
static COLD bipolar radio_heard_refused(const radio_heard address_to heard,
                                        const radio_network address_to networks,
                                        positive count)
{
        for (positive at = 0; at < count; at++)
        {
                wifi_plan plan;
                wifi_rsn_caps caps = heard->caps;

                if (networks[at].ssid_length != heard->ssid_length ||
                    memory_compare(networks[at].ssid, heard->ssid, heard->ssid_length))
                        continue;
                if (!networks[at].pass_length)
                        return 0;
                return wifi_join_plan(MOONWATER_STRICT_NET, address_of caps,
                                      radio_rules_get((p8 address_to)networks[at].ssid,
                                                      networks[at].ssid_length),
                                      networks[at].pass_length == 64, address_of plan);
        }
        return 0;
}

static string_address radio_join_words(bipolar failed)
{
        static p8 words[48];

        if (failed <= -1000 && failed > -1000 - 65536)
        {
                p8 number[24];

                bipolar_into_string(number, -1000 - failed);
                radio_line(words, sizeof(words),
                           (string_address) "ended the handshake (reason ", number,
                           (string_address) ")", null, null);
                return (string_address)words;
        }
        return failed == -110   ? (string_address) "did not associate"
               : failed == -111 ? (string_address) "refused the join"
               : failed == -13  ? (string_address) "did not accept the password"
               : failed == -62  ? (string_address) "did not begin the handshake"
               : failed == -121 ? (string_address) "stopped answering in the handshake"
               : failed == -113 ? (string_address) "is not in range"
               : failed == WIFI_REFUSE_TKIP
                   ? (string_address) "uses TKIP (WPA1), which is broken and never joined; "
                                      "set the access point to WPA2 or WPA3 with AES (CCMP)"
               : failed == WIFI_REFUSE_CIPHER
                   ? (string_address) "uses a cipher other than AES (CCMP), which moonwater "
                                      "does not offer"
               : failed == WIFI_REFUSE_PMF_GONE
                   ? (string_address) "no longer offers management frame protection, which it "
                                      "did (an impostor or a changed access point; wifi remove "
                                      "and add it again to accept that)"
               : failed == WIFI_REFUSE_SAE_GONE
                   ? (string_address) "no longer offers WPA3, which it was joined with (an "
                                      "impostor or a changed access point; wifi remove and add "
                                      "it again to accept that)"
               : failed == WIFI_REFUSE_NO_PMF
                   ? (string_address) "does not protect its management frames, which this "
                                      "machine requires"
               : failed == WIFI_REFUSE_WPA2
                   ? (string_address) "offers WPA2 alone, which this machine joins only for a "
                                      "network saved with wpa2 (moonwater wifi add SSID - wpa2)"
               : failed == WIFI_REFUSE_KEY_ONLY
                   ? (string_address) "asks for WPA3, which needs the password itself and not a "
                                      "key of 64 hex digits"
               : failed == WIFI_REFUSE_SUITE
                   ? (string_address) "asks for key management moonwater does not offer"
               : failed == WIFI_REFUSE_SAE_BUDGET
                   ? (string_address) "was asked for WPA3 more often than a minute allows; it is "
                                      "tried again later"
               : failed == WIFI_REFUSE_ELEMENTS
                   ? (string_address) "showed other security in its handshake than in its "
                                      "beacon (an impostor, or a downgrade on the way)"
               : failed == WIFI_REFUSE_CONFINE
                   ? (string_address) "was not joined: the Wi-Fi station cannot be confined "
                                      "on this kernel (no seccomp filter)"
               : failed == WIFI_REFUSE_APART
                   ? (string_address) "was not joined: the confined Wi-Fi station ended "
                                      "without a word"
                   : (string_address) "could not be joined";
}

/* A join's failure that is the network's doing, which radio_join_words
   says, rather than the machine's. */
static bool radio_join_said(bipolar failed)
{
        return failed == -110 || failed == -111 || failed == -13 || failed == -62 ||
               failed == -121 || failed == -113 ||
               (failed >= WIFI_REFUSE_FIRST && failed <= WIFI_REFUSE_LAST) ||
               (failed <= -1000 && failed > -1000 - 65536);
}

/*
        The network the machine last failed to join, why, when, and how many
        times in a row for that reason, for bare wifi and status and for the
        machine's own pass, which leaves a network alone for a while after
        it was not there or refused the password (radio_join_wait): three
        seconds, then twice as long each time, to a minute. Gone once a join
        works. The text is the name, the code, the boot-time milliseconds and
        the count, a line each.
*/
typedef struct
{
        p8 ssid[RADIO_SSID_MOST + 1];
        bipolar failed;
        p64 at;
        positive count;
} radio_last;

static bool radio_last_get(radio_last address_to last)
{
        p8 text[RADIO_SSID_MOST + 64];
        p8 address_to code;
        p8 address_to when;
        p8 address_to count;
        positive at;

        if (host_read_word(RADIO_LAST_PATH, text, sizeof(text)) <= 0)
                return false;
        at = (positive)(string_first_of_or_end(text, '\n') - text);
        if (!text[at] || at > RADIO_SSID_MOST)
                return false;
        text[at] = end;
        string_copy_bounded(last->ssid, text, sizeof(last->ssid));

        //      Each of the rest ends at its line: a number is read from its
        //      end backwards, and runs into the next line's digits otherwise.
        code = text + at + 1;
        when = (p8 address_to)string_first_of_or_end(code, '\n');
        last->at = 0;
        last->count = 0;
        if (*when)
        {
                *when++ = end;
                count = (p8 address_to)string_first_of_or_end(when, '\n');
                if (*count)
                {
                        *count++ = end;
                        last->count = string_to_positive(count);
                }
                last->at = string_to_positive(when);
        }
        last->failed = -(bipolar)string_to_positive(code);
        return true;
}

static fn radio_last_set(string_address ssid, bipolar failed)
{
        p8 text[RADIO_SSID_MOST + 80];
        p8 number[24];
        radio_last before;
        positive count = 1;
        positive used;

        if (!failed || failed == -19)
        {
                system_remove_at(AT_FDCWD, RADIO_LAST_PATH, 0);
                return;
        }
        //      Another time for the same name and reason, or the first.
        //      A pass that asked nothing of the air, and was told by the scan of
        //      a moment ago, has not found the network no further away.
        if (radio_last_get(address_of before) && before.failed == failed &&
            string_equals((string_address)before.ssid, ssid))
                count = before.count + (radio_scan_declined ? 0 : 1);
        bipolar_into_string(number, -failed);
        radio_line(text, sizeof(text), ssid, (string_address) "\n", number,
                   (string_address) "\n", null);
        used = string_length(text);
        bipolar_into_string(number, (bipolar)(system_clock_ns(HOST_CLOCK_BOOTTIME) / 1000000));
        radio_line(text + used, sizeof(text) - used, number, (string_address) "\n", null, null,
                   null);
        used = string_length(text);
        bipolar_into_string(number, (bipolar)count);
        radio_line(text + used, sizeof(text) - used, number, (string_address) "\n", null, null,
                   null);
        host_state_ready();
        host_write_file(RADIO_LAST_PATH, text, string_length(text), 0644, false);
}

/* Milliseconds the machine's own pass leaves the network that failed this
   way alone, from the time it did: only a network that was not there,
   refused the password or was refused by the join's own rules (which no
   pass will change, and each of which a scan found) is waited on, three
   seconds the first time and twice as long each, to a minute. */
static p64 radio_join_wait(const radio_last address_to last)
{
        positive doubled = last->count > 1 ? last->count - 1 : 0;
        p64 wait;

        if ((last->failed != -113 && last->failed != -13 &&
             (last->failed < WIFI_REFUSE_FIRST || last->failed > WIFI_REFUSE_LAST)) ||
            !last->at)
                return 0;
        wait = (p64)3000 << (doubled > 5 ? 5 : doubled);
        return wait > 60000 ? 60000 : wait;
}

/* Whether the last join failed so lately that the next one is left to wait:
   the pass and a restore both ask. */
static bool radio_join_waiting(void)
{
        radio_last last;

        return radio_last_get(address_of last) &&
               system_clock_ns(HOST_CLOCK_BOOTTIME) / 1000000 <
                   last.at + radio_join_wait(address_of last);
}

/*
        A password, without argv: ps shows every process's arguments to
        every user. At a terminal it is asked for with the echo off and the
        line read here, a byte at a time, so Control-C and Control-D cancel
        with the terminal put back rather than killing the process with the
        echo still off. Anywhere else it is one line of standard input.
        Negative when cancelled; else its length, 0 for an open network.
*/
#define RADIO_TERMINAL_GET 0x5401
#define RADIO_TERMINAL_SET 0x5402

static bipolar radio_password_read(p8 address_to into, positive room,
                                   string_address ssid, bool only_asked,
                                   bool group)
{
        p8 saved[64];
        p8 quiet[64];
        p8 shown[RADIO_SSID_MOST * 4 + 1];
        bool terminal = system_call_3(syscall(ioctl), 0, RADIO_TERMINAL_GET,
                                      (positive)saved) >= 0;
        positive used = 0;
        bipolar result = 0;

        into[0] = end;
        if (only_asked && !terminal)
                return 0;
        if (terminal)
        {
                p32 modes;

                memory_copy(quiet, saved, sizeof(quiet));
                modes = memory_load_unaligned(p32, quiet + 12);
                modes &= ~(p32)(0x8 | 0x2 | 0x1); // ECHO, ICANON, ISIG
                memory_copy(quiet + 12, address_of modes, 4);
                quiet[17 + 6] = 1; // VMIN
                quiet[17 + 5] = 0; // VTIME
                radio_display(shown, sizeof(shown), ssid, string_length(ssid));
                if (group)
                        host_say(log_error, host_label "secret for %s: ", shown);
                else
                        host_say(log_error, host_label "password for %s (empty for an open network): ",
                                 shown);
                system_call_3(syscall(ioctl), 0, RADIO_TERMINAL_SET, (positive)quiet);
        }

        for (;;)
        {
                p8 byte;
                bipolar got = system_read_once(0, address_of byte, 1);

                if (got == -4)
                        continue;
                if (got <= 0)
                {
                        //      The end of input with nothing before it is no
                        //      answer: a pass or a pipe that failed would
                        //      otherwise save the network as an open one.
                        //      An empty line is the answer for an open one.
                        if (!used)
                                result = -1;
                        break;
                }
                if (byte == '\n' || byte == '\r')
                        break;
                if (terminal)
                {
                        if (byte == 3 || (byte == 4 && !used))
                        {
                                result = -1;
                                break;
                        }
                        if (byte == 0x7f || byte == 8)
                        {
                                while (used && (into[used - 1] & 0xc0) == 0x80)
                                        used--;
                                if (used)
                                        used--;
                                continue;
                        }
                        if (byte == 0x15)
                        {
                                used = 0;
                                continue;
                        }
                        if (byte < 32)
                                continue;
                }
                else if (byte < 32)
                {
                        //      Not what somebody typed: a NUL would end it
                        //      where it is kept, and what lay before the NUL
                        //      would be saved as the password.
                        result = -1;
                        break;
                }
                if (used + 1 < room)
                        into[used++] = byte;
        }

        if (terminal)
        {
                system_call_3(syscall(ioctl), 0, RADIO_TERMINAL_SET, (positive)saved);
                host_say(log_error, "\n");
        }
        into[used] = end;
        crypto_forget(quiet, sizeof(quiet));
        //      A password given up on (Control-C or Control-D after some of
        //      it was typed) leaves nothing of it in the caller's bytes.
        if (result < 0)
                crypto_forget(into, room);
        return result < 0 ? result : (bipolar)used;
}

/* One network joined, asked for (force) or by the machine's own pass. */
static bipolar radio_wifi_join(string_address ssid, string_address pass, bool force)
{
        bipolar failed = -19;
        positive ssid_length = string_length(ssid);
        positive pass_length = pass ? string_length(pass) : 0;
        p64 started;

        if (!ssid_length || ssid_length > RADIO_SSID_MOST)
                return -22;

        if (pass_length && !wifi_pass_fits((p8 address_to)pass, pass_length))
                return -22;

        started = system_clock_ns(HOST_CLOCK_BOOTTIME);
        for (;;)
        {
                failed = radio_join_apart((p8 address_to)ssid, ssid_length,
                                          (p8 address_to)pass, pass_length, force);
                if (failed != -19)
                        break;
                //      No wireless interface yet: waited for only where the
                //      machine shows a card on its way. A machine with none
                //      would hold every boot, and every `wifi on`, for the
                //      whole wait, and the keeper joins when a card arrives.
                if (!radio_has_interface() ||
                    system_clock_ns(HOST_CLOCK_BOOTTIME) - started >= 8000000000)
                        break;
                host_pause(200000000);
        }

        return failed;
}

static bipolar radio_wifi_leave(void)
{
        nl80211 session;
        nl80211_iface iface;
        bipolar failed;

        failed = nl80211_open(address_of session);
        if (failed < 0)
                return failed;

        radio_keeper_stop();
        failed = nl80211_interface(address_of session, address_of iface);
        if (!failed)
                failed = nl80211_disconnect(address_of session, iface.index);

        nl80211_close(address_of session);
        return failed;
}

static b32 radio_wifi_bring(bool say)
{
        radio_network networks[RADIO_WIFI_MOST];
        positive count = radio_wifi_load(networks, RADIO_WIFI_MOST, null);
        positive at;
        bipolar failed = 0;
        bool joined = false;
        b32 switched = radio_switch(NET_WIFI_POWER, RADIO_RFKILL_WLAN, true, say);

        radio_secret_hold(networks, sizeof(networks));
        scope_exit(radio_secret_let_go(networks));

        if (!say && nl80211_associated())
        {
                radio_net_wake();
                crypto_forget(networks, sizeof(networks));
                return 0;
        }

        for (at = 0; at < count; at++)
        {
                failed = radio_wifi_join((string_address)networks[at].ssid,
                                         (string_address)networks[at].pass, say);
                if (!failed)
                {
                        joined = true;
                        if (say)
                        {
                                p8 shown[RADIO_SSID_MOST * 4 + 1];

                                radio_display(shown, sizeof(shown), networks[at].ssid,
                                              networks[at].ssid_length);
                                host_say(log, host_label "wifi joined %s\n", (string_address)shown);
                        }
                        break;
                }
                if (failed == -19)
                        break;
        }
        //      One pass is one failure, whichever of the networks it was the
        //      last of: its count is what the next pass waits by.
        if (count)
                radio_last_set((string_address)networks[at < count ? at : count - 1].ssid,
                               failed);

        radio_net_wake();
        crypto_forget(networks, sizeof(networks));

        if (!count)
        {
                if (say && !switched)
                        host_say(log, host_label "wifi on\n");
                return switched;
        }

        if (joined)
                return switched;

        if (say && failed == -19)
        {
                p8 why[RADIO_WHY_ROOM];

                return radio_wifi_why(why, sizeof(why))
                           ? host_refuse("wifi: %s\n", why)
                           : host_refuse("no wireless interface\n");
        }
        if (say)
                return radio_join_said(failed) || failed == WIFI_REFUSE_CONFINE ||
                               failed == WIFI_REFUSE_APART
                           ? host_refuse("the network %s\n", radio_join_words(failed))
                           : host_fail("wifi", failed ? failed : -1);
        return 1;
}

static b32 radio_wifi_on(bool say)
{
        bipolar lock = radio_lock(true);
        b32 result;

        if (lock < 0)
                return say ? host_fail("wifi", lock) : 1;
        result = radio_wifi_bring(say);
        radio_unlock(lock);
        return result;
}

/* Wifi off: the word kept, the link left, the radio blocked. report is for
   somebody waiting to hear what did not go through. */
static b32 radio_wifi_off(bool report)
{
        bipolar lock = radio_lock(true);
        bipolar kept;
        b32 failed;

        if (lock < 0)
                return report ? host_fail("wifi", lock) : 1;
        kept = radio_write_word(NET_WIFI_POWER, "off");
        radio_wifi_leave();
        failed = radio_failed(report, NET_WIFI_POWER, kept,
                              radio_rfkill(RADIO_RFKILL_WLAN, true));
        radio_net_wake();
        radio_unlock(lock);
        return failed;
}

/* The network put on the saved list, or put there again with the password it
   was last given. -E2BIG when the list is full, -EFBIG when it is more than
   this reads and would be written back short. */
static bipolar radio_wifi_store(string_address ssid, string_address pass,
                                bool address_to same)
{
        radio_network networks[RADIO_WIFI_MOST];
        bool cut;
        positive count = radio_wifi_load(networks, RADIO_WIFI_MOST, address_of cut);
        positive at;
        positive ssid_length = string_length(ssid);
        positive pass_length = pass ? string_length(pass) : 0;
        bipolar failed = 0;

        for (at = 0; at < count; at++)
                if (string_equals((string_address)networks[at].ssid, ssid))
                        break;

        //      The same network with the same password is already what the
        //      list says: nothing is written for it, and the caller is told.
        address_to same = at < count && networks[at].pass_length == pass_length &&
                          string_equals((string_address)networks[at].pass,
                                        pass ? pass : (string_address) "");

        if (cut)
                failed = -EFBIG;
        else if (address_to same)
                failed = 0;
        else if (at == count && count++ == RADIO_WIFI_MOST)
                failed = -E2BIG;
        else
        {
                memory_fill(networks[at].ssid, 0, sizeof(networks[at].ssid));
                memory_copy(networks[at].ssid, ssid, ssid_length);
                networks[at].ssid[ssid_length] = end;
                networks[at].ssid_length = (p8)ssid_length;
                memory_fill(networks[at].pass, 0, sizeof(networks[at].pass));
                if (pass_length)
                        memory_copy(networks[at].pass, pass, pass_length);
                networks[at].pass[pass_length] = end;
                networks[at].pass_length = (p8)pass_length;
                failed = radio_wifi_save(networks, count);
        }

        crypto_forget(networks, sizeof(networks));
        return failed;
}

/* What a network that asks for what the join cannot give is told, after
   "asks for": TKIP (and WPA, whose cipher it is) is broken and the fix is at
   the access point; the rest are not built. */
static fn radio_unjoinable(p8 address_to into, positive room, p8 security)
{
        bool broken = security == RADIO_WPA || security == RADIO_TKIP;

        radio_line(into, room,
                   security == RADIO_WPA ? (string_address) "WPA (TKIP)"
                                         : radio_security_words[security],
                   broken ? (string_address) ", which is broken and never joined; set the "
                                             "access point to WPA2 or WPA3 with AES (CCMP)"
                          : (string_address) ", which moonwater cannot join yet",
                   null, null, null);
}

/* Everything of `wifi add` that happens with the radio lock held. */
static b32 radio_wifi_enter(string_address ssid, string_address pass, bool wpa2)
{
        radio_air air;
        radio_heard address_to heard = null;
        positive pass_length = pass ? string_length(pass) : 0;
        bool same;
        //      Asked, and pinned, before the network is saved: a saved network is what makes a radio with no word on, so
        //      a network saved with the pin not yet written (a failed write, a cut) would raise the radio at the next boot.
        bool off = radio_kept(NET_WIFI_POWER) == 0;
        bipolar pinned = off ? radio_write_word(NET_WIFI_POWER, "off") : 0;
        bipolar stored;
        bipolar failed;
        p8 why[RADIO_WHY_ROOM];

        b32 switched;

        return_if(pinned < 0, host_fail(NET_WIFI_POWER, pinned));
        stored = radio_wifi_store(ssid, pass, address_of same);
        if (stored == -E2BIG)
                return host_refuse("too many saved networks\n");
        if (stored == -EFBIG)
                return host_refuse("%s is too long to change here\n", NET_WIFI_LIST);
        return_if(stored < 0, host_fail(NET_WIFI_LIST, stored));
        //      What the network taught the join stands while its password
        //      does; whether WPA2 is let through the tight tier is what
        //      this add says.
        radio_rules_put((p8 address_to)ssid, string_length(ssid),
                        (p8)((same ? 0 : WIFI_RULE_PMF | WIFI_RULE_SAE) | WIFI_RULE_WPA2),
                        wpa2 ? WIFI_RULE_WPA2 : 0);

        //      A radio that is off stays off: the network is saved, and moonwater wifi on is what joins it.
        if (off)
        {
                host_say(log, host_label "wifi saved %s; wifi is off, moonwater wifi on joins it\n", ssid);
                return 0;
        }
        switched = radio_switch(NET_WIFI_POWER, RADIO_RFKILL_WLAN, true, true);

        /*      What the air already says about it, from the last scan and
                without asking for another: a network that asks for what the
                join cannot give is saved and not tried, which would only
                have waited out the association timeout to say less. */
        if (radio_air_take(address_of air, RADIO_AIR_STALE | RADIO_AIR_JOINABLE) &&
            ((heard = radio_air_find(address_of air, ssid)) ||
             (radio_air_take(address_of air, RADIO_AIR_NOW | RADIO_AIR_JOINABLE) &&
              (heard = radio_air_find(address_of air, ssid)))))
        {
                //      Said again by a script at every boot, with a link that
                //      holds: nothing is left and joined over.
                if (same && heard->joined && host_lock_owner(RADIO_KEEPER_PATH) > 0 &&
                    nl80211_associated())
                {
                        host_say(log, host_label "wifi already joined %s\n", ssid);
                        return switched;
                }
                if (!heard->joinable)
                {
                        radio_unjoinable(why, sizeof(why), heard->security);
                        return host_refuse("saved, but it asks for %s\n", why);
                }
                if (heard->security != RADIO_OPEN && !pass_length)
                        return host_refuse("saved with no password, but it "
                                           "asks for one\n");
        }

        /*      No radio at all is not one that is still arriving: the join's
                eight seconds of asking are for a card whose interface is on
                its way at boot. */
        failed = radio_has_interface() ? radio_wifi_join(ssid, pass, true) : -19;
        radio_last_set(ssid, failed);

        radio_net_wake();
        if (failed == -19)
                return radio_wifi_why(why, sizeof(why))
                           ? host_refuse("saved; wifi: %s\n", why)
                           : host_refuse("saved, but there is no "
                                         "wireless interface\n");
        if (failed == -113)
                return host_refuse("saved, but it is not in range\n");
        if (radio_join_said(failed))
        {
                if (failed == -110 &&
                    radio_air_take(address_of air, RADIO_AIR_CACHED) &&
                    air.count && !radio_air_find(address_of air, ssid))
                        return host_refuse("saved, but it is not in range\n");
                return host_refuse("saved, but the network %s\n",
                                   radio_join_words(failed));
        }
        if (failed == WIFI_REFUSE_CONFINE || failed == WIFI_REFUSE_APART)
                return host_refuse("saved, but the network %s\n", radio_join_words(failed));
        return_if(failed < 0, host_fail("wifi", failed));
        if (pass && pass[0])
                crypto_forget((address_any)pass, string_length(pass));

        host_say(log, host_label "wifi joined %s\n", ssid);
        return switched;
}

static b32 radio_wifi_add(string_address ssid, string_address pass, bool wpa2)
{
        positive ssid_length = string_length(ssid);
        positive pass_length = pass ? string_length(pass) : 0;
        bipolar lock;
        b32 result;

        if (!ssid_length || ssid_length > RADIO_SSID_MOST)
                return host_refuse("that network name is empty or too long\n");
        if (pass_length > RADIO_PASS_MOST)
                return host_refuse("that password is too long\n");
        if (pass_length && !wifi_pass_fits((p8 address_to)pass, pass_length))
                return host_refuse("a WPA password is 8 to 63 characters, "
                                   "or a key of 64 hex digits\n");
        if (!radio_text_shown(ssid) || (pass_length && !radio_text_shown(pass)))
                return host_refuse("that network name cannot be stored\n");

        /*      The radio lock from here, before the network is saved: the
                machine's own pass, finding it saved and nothing joined,
                joined it first while this scanned, and this then left that
                join to make its own. */
        lock = radio_lock(true);
        return_if(lock < 0, host_fail("wifi", lock));
        //      The broker and the station a join forks wipe their copies of
        //      the password (radio_secrets); this process forgets its own
        //      before the lock is let go.
        if (pass_length)
                radio_secret_hold((address_any)pass, pass_length);
        result = radio_wifi_enter(ssid, pass, wpa2);
        if (pass_length)
        {
                radio_secret_let_go((address_any)pass);
                crypto_forget((address_any)pass, pass_length);
        }
        radio_unlock(lock);
        return result;
}

/* Everything of `wifi remove` that happens with the radio lock held. */
static b32 radio_wifi_forget(string_address ssid)
{
        radio_network networks[RADIO_WIFI_MOST];
        bool cut;
        positive count = radio_wifi_load(networks, RADIO_WIFI_MOST, address_of cut);
        positive at;
        bool was_joined = false;
        bipolar failed;

        for (at = 0; at < count; at++)
                if (string_equals((string_address)networks[at].ssid, ssid))
                        break;

        //      A list longer than the rows read would be written back without
        //      what was not read: twenty networks, one forgotten, and fifteen left.
        if (cut)
        {
                crypto_forget(networks, sizeof(networks));
                return host_refuse("%s is too long to change here\n", NET_WIFI_LIST);
        }
        if (at == count)
        {
                crypto_forget(networks, sizeof(networks));
                return host_refuse("no saved network is called %s\n", ssid);
        }

        //      What the last scan says, before the network is gone from the
        //      list the join would have read.
        {
                radio_air air;
                radio_heard address_to heard;

                if (radio_air_take(address_of air, RADIO_AIR_STALE | RADIO_AIR_JOINABLE) &&
                    (heard = radio_air_find(address_of air, ssid)))
                        was_joined = heard->joined;
        }

        for (; at + 1 < count; at++)
                memory_copy(address_of networks[at], address_of networks[at + 1],
                            sizeof(networks[at]));
        count--;
        crypto_forget(address_of networks[count], sizeof(networks[count]));

        //      Its rules first: a remove cut between the two leaves the
        //      network saved, and the next remove does both.
        radio_rules_put((p8 address_to)ssid, string_length(ssid),
                        WIFI_RULE_PMF | WIFI_RULE_SAE | WIFI_RULE_WPA2, 0);
        failed = radio_wifi_save(networks, count);
        crypto_forget(networks, sizeof(networks));
        return_if(failed < 0, host_fail(NET_WIFI_LIST, failed));

        {
                radio_last last;

                if (radio_last_get(address_of last) &&
                    string_equals((string_address)last.ssid, ssid))
                        radio_last_set(ssid, 0);
        }

        if (was_joined)
                (void)radio_wifi_leave();
        radio_net_wake();

        host_say(log, host_label "wifi forgot %s%s\n", ssid,
                 was_joined ? " and left it" : "");
        return 0;
}

/*
        Forget a saved network. The list is rewritten without it, and the
        password with it. A network the machine is joined to when this runs
        is left too: it was saved so the machine would rejoin it, and once it
        is not saved nothing else would ever ask for the connection to end.
*/
static b32 radio_wifi_remove(string_address ssid)
{
        positive ssid_length = string_length(ssid);
        bipolar lock;
        b32 result;

        if (!ssid_length || ssid_length > RADIO_SSID_MOST)
                return host_refuse("that network name is empty or too long\n");

        lock = radio_lock(true);
        return_if(lock < 0, host_fail("wifi", lock));
        result = radio_wifi_forget(ssid);
        radio_unlock(lock);
        return result;
}

/*
        Wired, on and off, the way wifi has them.

        Off is remembered on /root, so it holds across a reboot, and does two
        things: the walk that picks a link to ask for a lease on no longer
        considers a wired one, and every wired link is taken down, so the
        lease it held is given up by the same carrier news a pulled cable
        sends and the machine moves to whatever else has carrier. On raises
        the links again and wakes the watcher to choose among them. Only a
        link with Ethernet framing that is neither loopback nor a wireless
        station is wired here.
*/
static b32 radio_wired_set(bool on)
{
        netlink_wired wired;
        bipolar handle;
        bipolar failed;

        if (host_save(NET_WIRED_POWER, NET_WIRED_POWER, on ? "on" : "off"))
                return 1;

        handle = netlink_open_groups(0);
        return_if(handle < 0, host_fail("wired", handle));

        failed = netlink_wired_list((b32)handle, address_of wired);
        if (failed >= 0)
                for (positive at = 0; at < wired.count; at++)
                        if (((wired.link[at].flags & IFF_UP) != 0) != on)
                                (void)netlink_link_flag_up((b32)handle, wired.link[at].index, on);
        socket_close((b32)handle);
        radio_net_wake();

        return_if(failed < 0, host_fail("wired", failed));
        host_say(log, host_label "wired %s%s\n", on ? "on" : "off",
                 wired.count ? "" : " (this machine has no wired link)");
        return 0;
}

static b32 radio_wired_status(void)
{
        netlink_wired wired;
        bipolar handle = netlink_open_groups(0);
        bipolar failed = handle < 0 ? handle : netlink_wired_list((b32)handle, address_of wired);

        if (handle >= 0)
                socket_close((b32)handle);
        string_format(log, host_label "wired %s\n", net_wired_off() ? "off" : "on");
        if (failed >= 0)
                for (positive at = 0; at < wired.count; at++)
                        string_format(log, host_label "  %w: %s, %s\n",
                                      writer_terminal_name, wired.link[at].name,
                                      (wired.link[at].flags & IFF_UP) ? "up" : "down",
                                      (wired.link[at].flags & IFF_RUNNING) ? "carrier"
                                                                           : "no carrier");
        log_flush();
        return failed < 0 ? host_fail("wired", failed) : 0;
}

/*
        Bare wifi: the switch and the saved networks as they always were,
        then why wifi cannot be used, or what is in the air -- the saved and
        joined ones marked -- and why the machine is not joined when it is
        not. Scripts that read the first lines still find them first.
*/
static b32 radio_wifi_status(void)
{
        radio_network networks[RADIO_WIFI_MOST];
        positive count = radio_wifi_load(networks, RADIO_WIFI_MOST, null);
        positive at;
        bool off = radio_kept(NET_WIFI_POWER) == 0;
        bool joined = false;
        p8 why[RADIO_WHY_ROOM];
        radio_last last;
        radio_air air;

        string_format(log, host_label "wifi %s\n",
                      off ? (string_address) "off" : (string_address) "on");
        for (at = 0; at < count; at++)
        {
                p8 shown[RADIO_SSID_MOST * 4 + 1];

                radio_display(shown, sizeof(shown), networks[at].ssid,
                              networks[at].ssid_length);
                string_format(log, host_label "  %s\n", (string_address)shown);
        }

        if (radio_wifi_why(why, sizeof(why)) ||
            !radio_air_take(address_of air, RADIO_AIR_STALE))
        {
                host_say(log, host_label "wifi: %s\n",
                         why[0] ? (string_address)why
                                : (string_address) "no wireless interface");
                crypto_forget(networks, sizeof(networks));
                return 0;
        }

        if (!air.count)
                string_format(log, host_label "no networks heard%s\n",
                              bowl_is_root() ? (string_address) ""
                                             : (string_address) " (a new scan needs root)");
        else
        {
                positive width = 4;
                positive shown = air.count < RADIO_AIR_SHOWN ? air.count : RADIO_AIR_SHOWN;

                for (at = 0; at < shown; at++)
                {
                        p8 name[RADIO_SSID_MOST * 4 + 1];
                        positive columns = radio_display(name, sizeof(name),
                                                         air.heard[at].ssid,
                                                         air.heard[at].ssid_length);

                        if (columns > width)
                                width = columns > 28 ? 28 : columns;
                }
                for (at = 0; at < shown; at++)
                {
                        bool saved = false;

                        for (positive have = 0; have < count; have++)
                                saved |= networks[have].ssid_length ==
                                             air.heard[at].ssid_length &&
                                         !memory_compare(networks[have].ssid,
                                                         air.heard[at].ssid,
                                                         air.heard[at].ssid_length);
                        radio_air_row(air.heard + at, width, saved);
                }
                if (air.count > shown)
                        string_format(log, host_label "  and %p more\n",
                                      air.count - shown);
        }

        for (at = 0; at < air.count; at++)
                joined |= air.heard[at].joined;
        if (!joined && count && !off)
        {
                radio_heard address_to heard = null;

                last.ssid[0] = end;
                last.failed = 0;
                if (radio_last_get(address_of last))
                        heard = radio_air_find(address_of air, last.ssid);
                p8 name[RADIO_SSID_MOST * 4 + 1];

                radio_display(name, sizeof(name), last.ssid, string_length(last.ssid));
                if (heard && !heard->joinable)
                {
                        radio_unjoinable(why, sizeof(why), heard->security);
                        string_format(log, host_label "not joined: %s asks for %s\n",
                                      (string_address)name, (string_address)why);
                }
                else if (heard)
                        string_format(log, host_label "not joined: %s %s\n",
                                      (string_address)name,
                                      radio_join_words(radio_heard_refused(heard, networks, count)
                                                           ?: last.failed));
                else
                {
                        bool near = false;
                        bool said = false;

                        //      No join has failed of late, or not of one in the
                        //      air: a saved network heard only from an access
                        //      point its plan refuses is said for what it is.
                        for (at = 0; at < count && !said; at++)
                        {
                                radio_heard address_to one =
                                    radio_air_find(address_of air, networks[at].ssid);
                                bipolar refused =
                                    one ? radio_heard_refused(one, networks, count) : 0;

                                near |= one != null;
                                if (refused)
                                {
                                        radio_display(name, sizeof(name), networks[at].ssid,
                                                      networks[at].ssid_length);
                                        string_format(log, host_label "not joined: %s %s\n",
                                                      (string_address)name,
                                                      radio_join_words(refused));
                                        said = true;
                                }
                        }
                        if (!near)
                                string_format(log, host_label "not joined: no saved "
                                                              "network is in range\n");
                }
        }

        host_say(log, host_label "* joined  + saved  "
                                 "moonwater wifi add SSID asks for its password\n");
        crypto_forget(networks, sizeof(networks));
        return 0;
}

/* The remembered word and the rfkill switch, set together. report is for
   the person who asked; restoring the machine's own choice stays quiet. */
static b32 radio_bluetooth_power(bool on, bool report)
{
        return radio_switch(NET_BLUETOOTH_POWER, RADIO_RFKILL_BLUETOOTH, on, report);
}

/* Remember a device's name or forget it. Nothing here pairs or connects: the
   list is the names a person keeps, shown by `moonwater bluetooth`, and a name
   added turns the radio on for whatever does pair. It is read, changed and
   written back whole under the lock the wifi verbs take, so two runs at once
   cannot lose each other's names. */
static b32 radio_bluetooth_edit(string_address identity, bool add)
{
        p8 text[4096];
        p8 kept[sizeof(text) + 1];
        positive length = string_length(identity);
        positive used = 0;
        positive at = 0;
        positive start;
        positive size;
        bipolar got;
        bipolar lock;
        bipolar failed = 0;
        bool found = false;
        bool off;

        if (!length || length > 128)
                return host_refuse("that bluetooth name is empty or too long\n");
        if (add && !radio_text_shown(identity))
                return host_refuse("that bluetooth name cannot be stored\n");

        lock = radio_lock(true);
        return_if(lock < 0, host_fail("bluetooth", lock));
        //      Asked, and pinned, under the lock and before the list is written: a remembered device is what makes a
        //      radio with no word on, and a bluetooth on that comes between would be written over.
        off = radio_kept(NET_BLUETOOTH_POWER) == 0;
        if (add && off)
        {
                failed = radio_write_word(NET_BLUETOOTH_POWER, "off");
                if (failed < 0)
                {
                        radio_unlock(lock);
                        return host_fail(NET_BLUETOOTH_POWER, failed);
                }
        }
        got = host_read_state(NET_BLUETOOTH_LIST, text, sizeof(text));
        if (got < 0)
                got = 0;

        while (host_line_next(text, (positive)got, address_of at, address_of start,
                              address_of size))
        {
                if (size == length && !memory_compare(text + start, identity, length))
                {
                        found = true;
                        if (!add)
                                continue;
                }
                memory_copy(kept + used, text + start, size);
                kept[used += size] = '\n';
                used++;
        }

        //      1: nothing by that name to forget, 2: no room for another,
        //      4: more of a list than was read, which would be written back
        //      short; or the list's own error when it would not be written.
        if (got >= (bipolar)sizeof(text) - 1)
                failed = 4;
        else if (!add && !found)
                failed = 1;
        else if (add && !found && used + length + 1 >= sizeof(kept))
                failed = 2;
        else if (add != found)
        {
                if (add)
                {
                        memory_copy(kept + used, identity, length);
                        kept[used += length] = '\n';
                        used++;
                }
                failed = host_write_file(NET_BLUETOOTH_LIST, kept, used, 0644, true);
        }
        radio_unlock(lock);

        if (failed == 1)
                return host_refuse("no remembered bluetooth device is called %s\n", identity);
        if (failed == 2)
                return host_refuse("too many saved bluetooth devices\n");
        if (failed == 4)
                return host_refuse("%s is too long to change here\n", NET_BLUETOOTH_LIST);
        return_if(failed, host_fail(NET_BLUETOOTH_LIST, failed));
        //      A name remembered is for a radio that is on to find it.
        failed = add && !off ? radio_bluetooth_power(true, true) : 0;
        host_say(log, host_label "bluetooth %s %s%s\n", add ? "remembered" : "forgot", identity,
                 add && off ? "; bluetooth is off, moonwater bluetooth on raises it" : "");
        return failed;
}

static b32 radio_bluetooth_status(void)
{
        p8 text[4096];
        bipolar got = host_read_state(NET_BLUETOOTH_LIST, text, sizeof(text));
        bool off = radio_kept(NET_BLUETOOTH_POWER) == 0;
        positive at = 0;
        positive start;
        positive length;

        string_format(log, host_label "bluetooth %s\n",
                      off ? (string_address) "off" : (string_address) "on");
        while (got > 0 && host_line_next(text, (positive)got, address_of at,
                                         address_of start, address_of length))
                if (length)
                {
                        p8 shown[128 * 4 + 1];

                        radio_display(shown, sizeof(shown), text + start,
                                      length > 128 ? 128 : length);
                        string_format(log, host_label "  %s\n", (string_address)shown);
                }
        log_flush();
        return 0;
}

static string_address radio_internet_word(void)
{
        return net_internet_prefer() == NETLINK_PREFER_WIFI
                   ? (string_address) "wifi"
                   : (string_address) "wired";
}

/* The preference this session goes by, the copy of /root's that /run holds. */
static bipolar radio_internet_run(string_address which)
{
        p8 line[8];

        string_copy_bounded(line, which, sizeof(line));
        string_append_bounded(line, "\n", sizeof(line));
        host_state_ready();
        return host_write_text(NET_INTERNET_RUN, line);
}

/* What is kept comes first: a /run that said wifi over a /root that kept
   wired was put back by radio_internet_copy a pass later. */
static b32 radio_internet_set(string_address which)
{
        bipolar failed;

        host_state_ready();
        if (host_save(NET_INTERNET_ROOT, NET_INTERNET_ROOT, which))
                return 1;
        failed = radio_internet_run(which);
        return_if(failed < 0, host_fail(NET_INTERNET_RUN, failed));
        radio_net_wake();

        host_say(log, host_label "internet prefers %s\n", which);
        return 0;
}

static b32 radio_internet_status(void)
{
        host_say(log, host_label "internet prefers %s\n",
                 radio_internet_word());
        return 0;
}

static fn radio_internet_copy(void)
{
        p8 text[16];
        p8 have[16];

        if (host_read_word(NET_INTERNET_ROOT, text, sizeof(text)) < 0)
                return;
        if (host_read_word(NET_INTERNET_RUN, have, sizeof(have)) >= 0 &&
            string_equals(have, text))
                return;

        if (radio_internet_run((string_address)text) >= 0)
                radio_net_wake();
}

static bool radio_wifi_wanted(bipolar power)
{
        radio_network networks[1];
        bool wanted;

        if (power >= 0)
                return power;
        wanted = radio_wifi_load(networks, 1, null) != 0;
        crypto_forget(networks, sizeof(networks));
        return wanted;
}

/*
        The join the machine loop forks, and only that, reaped once it ends:
        wait4 is asked for that pid alone. A wait on -1 would take every child
        of the machine process with it, the NTP query's status (on a pipe)
        and any job the machine script put in the background, whose wait
        then found no such child.
*/
static bipolar radio_child;

static fn radio_reap(void)
{
        positive status = 0;

        if (radio_child > 0 &&
            system_call_4(syscall(wait4), (positive)radio_child,
                          (positive)address_of status, 1, 0) != 0)
                radio_child = 0;
}

static fn radio_wifi_keep(void)
{
        bipolar lock;
        bipolar child;
        bool lost = false;

        //      A keeper holding its lock is a link the machine joined and has
        //      not lost: nothing to ask the kernel or walk sysfs for every
        //      pass while it stands.
        if (radio_child > 0 || host_lock_owner(RADIO_KEEPER_PATH))
                return;
        //      Blocked by somebody's `rfkill block wlan`, or by a switch on
        //      the machine, it is left so until `moonwater wifi on` says
        //      otherwise.
        if (radio_rfkill_blocked())
                return;
        /*      A saved network and no radio forked a join every pass that
                spent eight seconds finding no interface. The radio's arrival
                -- firmware put in place, a card rebound, a stick plugged in
                -- is what the next pass after it notices. */
        if (!radio_has_interface())
                return;
        if (nl80211_associated())
        {
                //      A link nobody here joined is nobody here's to keep. One
                //      whose keeper was started and is gone has lost its keys
                //      with it, and would die at the access point's next
                //      rekey: it is joined again.
                if (system_access_at(AT_FDCWD, RADIO_KEPT_PATH, 0) < 0)
                        return;
                lost = true;
        }
        else if (radio_join_waiting())
                return;

        lock = radio_lock(false);
        if (lock < 0)
                return;

        child = system_fork();
        if (child < 0)
        {
                radio_unlock(lock);
                return;
        }
        if (child)
        {
                system_close(lock);
                radio_child = child;
                return;
        }

        if (lost)
                radio_wifi_leave();
        radio_wifi_bring(false);
        radio_unlock(lock);
        system_call_1(syscall(exit), 0);
}

/* Whether the wifi is as a restore leaves it: a link a keeper holds, a join
   that failed a moment ago (the machine's pass asks again when it is time),
   or a radio left off and not linked. A boot restores twice, once when the
   disk is taken and once when the machine process starts, and the second
   finds it done, or finds it tried. */
static bool radio_wifi_restored(bipolar wifi)
{
        if (wifi == 0)
                return radio_rfkill_blocked() && !nl80211_associated();
        return host_lock_owner(RADIO_KEEPER_PATH) || radio_join_waiting();
}

static fn radio_restore(void)
{
        bipolar wifi = radio_kept(NET_WIFI_POWER);
        bipolar bluetooth = radio_kept(NET_BLUETOOTH_POWER);

        radio_internet_copy();

        if (!radio_wifi_restored(wifi))
        {
                if (wifi == 0)
                        radio_wifi_off(false);
                else if (radio_wifi_wanted(wifi))
                        radio_wifi_on(false);
        }

        if (bluetooth >= 0)
                radio_bluetooth_power(bluetooth, false);

        /*      The watcher took its first pass before /root was the kept
                disk, so it knew none of what was set there: a wired-only
                machine, a wired-off or wifi-first priority, a wifi that was
                already associated. Nothing above woke it in those cases, and
                its idle wait had grown to thirty seconds by the time bowl
                asked for a lease. */
        radio_net_wake();
}

static fn radio_recover(void)
{
        p8 verdict[HOST_NAME_ROOM + 16];

        radio_reap();
        if (host_read_word(HOST_VERDICT, verdict, sizeof(verdict)) >= 0 &&
            string_has_prefix(verdict, "ask "))
                return;

        radio_internet_copy();

        if (radio_wifi_wanted(radio_kept(NET_WIFI_POWER)))
                radio_wifi_keep();
}

/* What a verb says it did, when it did not fail. */
static b32 radio_done(b32 failed, string_address what)
{
        if (!failed)
                host_say(log, host_label "%s\n", what);
        return failed;
}

//      What is asked is judged before who asks: a word that is no switch is a
//      usage page for anyone, and root's refusal is for what would have been
//      done. host_main's table gives each verb its own function, as every
//      other verb has.
static b32 host_priority(string_address address_to arguments, positive count)
{
        string_address word = count > 2 ? arguments[2] : null;

        if (count == 2)
                return radio_internet_status();
        return_if(!string_equals(word, "internet"), host_usage());
        if (count == 3)
                return radio_internet_status();
        if (count != 4 || (!string_equals(arguments[3], "wired") &&
                           !string_equals(arguments[3], "wifi")))
                return host_usage();
        host_need_root();
        return radio_internet_set(arguments[3]);
}

static b32 host_wired(string_address address_to arguments, positive count)
{
        bipolar power = count == 3 ? host_onoff(arguments[2]) : -1;

        if (count < 3)
                return radio_wired_status();
        return_if(power < 0, host_usage());
        host_need_root();
        return radio_wired_set(power);
}

static b32 host_wifi(string_address address_to arguments, positive count)
{
        string_address word = count > 2 ? arguments[2] : null;
        bipolar power = count == 3 ? host_onoff(word) : -1;

        if (count < 3)
                return radio_wifi_status();
        if (power >= 0)
        {
                host_need_root();
                return power ? radio_wifi_on(true)
                             : radio_done(radio_wifi_off(true), "wifi off");
        }
        if (string_equals(word, "add") && count >= 4 && count <= 6)
        {
                p8 pass[256];
                positive length;
                //      wpa2 after the password, or after - : the network may
                //      be joined with WPA2 where the tight tier asks for WPA3.
                bool wpa2 = count == 6;

                return_if(wpa2 && !string_equals(arguments[5], (string_address) "wpa2"),
                          host_usage());
                //      A password in the argument is in ps and in the
                //      history of any shell that does not know this verb;
                //      the tight tier refuses the form, and anywhere it is
                //      taken a person at a terminal is told the other.
                return_if(MOONWATER_STRICT_NET >= STRICT_TIGHT && count >= 5 &&
                              !string_equals(arguments[4], (string_address) "-"),
                          host_refuse("a password on the command line is "
                                      "refused: give it on standard input "
                                      "with -\n"));
                host_need_root();
                scope_exit(crypto_forget(pass, sizeof(pass)));

                /*      No password: asked for at a terminal, open
                        anywhere else, as it always was. "-": one
                        line of standard input. Either keeps it out
                        of argv, where ps shows it to everybody. */
                if (count == 4 || string_equals(arguments[4], (string_address) "-"))
                {
                        if (radio_password_read(pass, sizeof(pass), arguments[3],
                                                count == 4, false) < 0)
                                return host_refuse("nothing saved\n");
                }
                else
                {
                        if (stream_is_terminal(2))
                                host_say(log_error, host_label "a password on the "
                                         "command line is seen by ps: - reads it "
                                         "without that\n");
                        length = string_length(arguments[4]);
                        if (length >= sizeof(pass))
                                return host_refuse("that password is too long\n");
                        memory_copy(pass, arguments[4], length + 1);
                        crypto_forget(arguments[4], length);
                }
                return radio_wifi_add(arguments[3], pass, wpa2);
        }
        if (string_equals(word, "remove") && count == 4)
        {
                host_need_root();
                return radio_wifi_remove(arguments[3]);
        }
        return host_usage();
}

static b32 host_bluetooth(string_address address_to arguments, positive count)
{
        string_address word = count > 2 ? arguments[2] : null;
        bipolar power = count == 3 ? host_onoff(word) : -1;

        if (count < 3)
                return radio_bluetooth_status();
        if (power >= 0)
        {
                host_need_root();
                return radio_done(radio_bluetooth_power(power, true),
                                  power ? "bluetooth on" : "bluetooth off");
        }
        if ((string_equals(word, "add") || string_equals(word, "remove")) && count == 4)
        {
                host_need_root();
                return radio_bluetooth_edit(arguments[3], string_equals(word, "add"));
        }
        return host_usage();
}

/* ---- tune: the switches the kernel keeps in sysfs. ---- */

/*
        Airplane mode, the screen's brightness, the power profile, the CPU's
        boost and SMT, the battery's charge limit and sleep: each is a file
        the kernel already offers, so each is a verb here and not a tool to
        install. A verb with no value says where the switch stands, to anybody;
        one with a value needs root. A machine without the hardware gets one
        line that says so.

        What should outlast a boot -- the profile, the CPU switches, the
        charge limit, and what airplane mode switched off -- is kept on
        /root/tune, one "key value" line each, and put back by tune_restore
        when the machine starts. Brightness is not
        kept: the screen is the thing being looked at, and the last level is
        what the firmware brings it back at.
*/
#define TUNE_KEPT "/root/tune"
#define TUNE_SYS_BACKLIGHT "/sys/class/backlight"
#define TUNE_SYS_CPU "/sys/devices/system/cpu"
#define TUNE_SYS_BATTERY "/sys/class/power_supply"
#define TUNE_SYS_PROFILE "/sys/firmware/acpi/platform_profile"
#define TUNE_SYS_POWER "/sys/power"

/* Whether a file can be opened to be read, closing it. */
static bool tune_exists(string_address path)
{
        bipolar handle = system_open_at(AT_FDCWD, path, FILE_READ | O_CLOEXEC);

        if (handle < 0)
                return false;
        system_close((positive)handle);
        return true;
}

/* Whether a name in the cpu directory is a processor's: cpu and its number. */
static bool tune_is_cpu(string_address name)
{
        return name[0] == 'c' && name[1] == 'p' && name[2] == 'u' && byte_is_digit(name[3]);
}

/* The best of a directory's entries, by the rank a visitor gives each, and the
   first in the directory among equals. */
typedef struct
{
        p8 name[64];
        positive rank;
} tune_choice;

static fn tune_choose(tune_choice address_to choice, string_address name, positive rank)
{
        if (rank > choice->rank && string_length(name) < sizeof(choice->name))
        {
                choice->rank = rank;
                string_copy(choice->name, name);
        }
}

/* A kernel file's word is the one asked for. */
static bool tune_says(string_address directory, string_address name, string_address leaf,
                      string_address want)
{
        p8 text[16];

        return radio_sys_read(directory, name, leaf, text, sizeof(text)) > 0 &&
               string_equals((string_address)text, want);
}

/* One decimal number from a kernel file; false when it is not there or not one. */
static bool tune_number(string_address path, positive address_to value)
{
        p8 text[32];
        positive used;

        if (host_read_kernel(path, text, sizeof(text)) <= 0)
                return false;
        *value = string_digits_max(text, 18, address_of used);
        return used && !text[used];
}

static bool tune_word(string_address path, p8 address_to into, positive room)
{
        return host_read_kernel(path, into, room) > 0;
}

/* Write a value to a sysfs attribute. The name is never followed if it is a link. */
static bipolar tune_write(string_address path, string_address text)
{
        bipolar handle = system_open_at(AT_FDCWD, path,
                                        O_WRONLY | FILE_TRUNCATE | O_CLOEXEC | O_NOFOLLOW);
        positive length = string_length(text);
        system_write_result wrote;

        if (handle < 0)
                return handle;
        wrote = system_write_all_checked((positive)handle, (const address_any)text, length);
        system_close((positive)handle);
        return wrote.bytes == length ? 0 : wrote.error ? wrote.error : -ERROR_INPUT_OUTPUT;
}

static bipolar tune_write_number(string_address path, positive value)
{
        p8 number[24];

        positive_into_string(number, value);
        return tune_write(path, (string_address)number);
}

/* Whether a word is in a blank-separated list. */
static bool tune_has(p8 address_to text, string_address word)
{
        positive at = 0;
        positive length = string_length(word);

        while (text[at])
        {
                positive start = at;

                while (text[at] && text[at] != ' ')
                        at++;
                if (at - start == length && !memory_compare(text + start, word, length))
                        return true;
                while (text[at] == ' ')
                        at++;
        }
        return false;
}

/* Whether a word is in a file's blank-separated list. */
static bool tune_lists(string_address path, string_address word)
{
        p8 text[256];

        return tune_word(path, text, sizeof(text)) && tune_has(text, word);
}

/* Whether a line of the kept settings is the one for a key: the key, a blank,
   then the value. */
static bool tune_line_is(p8 address_to line, positive length, string_address key)
{
        positive size = string_length(key);

        return length > size && !memory_compare(line, key, size) && line[size] == ' ';
}

/* The kept settings, as far as the room goes: what the bytes are, or negative
   when none are kept or the file is not a plain one. */
static bipolar tune_load(p8 address_to text, positive room)
{
        return host_read_state(TUNE_KEPT, text, room);
}

/* The kept value for a key among settings already loaded, or false. */
static bool tune_find(p8 address_to text, positive size, string_address key,
                      p8 address_to into, positive room)
{
        positive at = 0;
        positive start;
        positive length;
        positive value;

        while (host_line_next(text, size, address_of at, address_of start,
                              address_of length))
                if (tune_line_is(text + start, length, key))
                {
                        value = length - string_length(key) - 1;
                        if (value >= room)
                                return false;
                        memory_copy(into, text + start + length - value, value);
                        into[value] = end;
                        return true;
                }
        return false;
}

/* The kept value for a key, or false. */
static bool tune_kept(string_address key, p8 address_to into, positive room)
{
        p8 text[512];
        bipolar got = tune_load(text, sizeof(text));

        return got > 0 && tune_find(text, (positive)got, key, into, room);
}

/* The keeping itself: the other lines stay, and an empty value drops the key. */
static bipolar tune_store(string_address key, string_address value)
{
        p8 text[512];
        p8 out[640];
        bipolar got = tune_load(text, sizeof(text));
        positive at = 0;
        positive used = 0;
        positive start;
        positive length;
        positive size = string_length(key);

        //      A file that filled the room is longer than this has read, and
        //      would be written back without the rest of it.
        if (got >= (bipolar)sizeof(text) - 1)
                return -EFBIG;
        if (got < 0)
                got = 0;
        while (host_line_next(text, (positive)got, address_of at, address_of start,
                              address_of length))
        {
                if (tune_line_is(text + start, length, key))
                        continue;
                if (used + length + 1 >= sizeof(out))
                        return -EFBIG;
                memory_copy(out + used, text + start, length);
                used += length;
                out[used++] = '\n';
        }
        if (value[0])
        {
                if (used + size + string_length(value) + 2 >= sizeof(out))
                        return -EFBIG;
                memory_copy(out + used, key, size);
                used += size;
                out[used++] = ' ';
                memory_copy(out + used, value, string_length(value));
                used += string_length(value);
                out[used++] = '\n';
        }
        host_state_ready();
        return host_write_file(TUNE_KEPT, out, used, 0644, true);
}

/* Keep a value for a key: 0, or the error that kept it from being kept. Four
   verbs at once each read the file and wrote it whole, and what was left was
   the last one's key, so they take turns on the lock the radio verbs use. */
static bipolar tune_keep(string_address key, string_address value)
{
        bipolar lock = radio_lock(true);
        bipolar failed;

        if (lock < 0)
                return lock;
        failed = tune_store(key, value);
        radio_unlock(lock);
        return failed;
}

/* tune_keep, with what it could not do said: 0, or 1 when it was. */
static b32 tune_remember(string_address key, string_address value)
{
        bipolar failed = tune_keep(key, value);

        return failed < 0 ? host_fail(TUNE_KEPT, failed) : 0;
}

/* A whole percent, "N", "N%", "+N" or "-N": the sign says relative. */
static bool tune_percent(string_address text, bool address_to relative, bool address_to lower,
                         positive address_to value)
{
        positive at = 0;
        positive number;
        positive used;

        *relative = *lower = false;
        if (text[at] == '+' || text[at] == '-')
        {
                *relative = true;
                *lower = text[at] == '-';
                at++;
        }
        if (!byte_is_digit(text[at]))
                return false;
        //      Nineteen digits is as many as a word holds, and more is not a
        //      percent whatever it starts with.
        number = string_digits_max(text + at, 19, address_of used);
        at += used;
        if (number > 100000)
                return false;
        if (text[at] == '%')
                at++;
        *value = number;
        return !text[at];
}

/* moonwater airplane [on|off]: every radio, at once, and back as they were.
   What was on is kept in /root/tune as the key airplane, so that a radio that
   was off before stays off after, and the block of every radio, the modem
   included, outlasts a boot. With nothing kept both come back. */
static b32 tune_airplane(string_address address_to arguments, positive count)
{
        bool wifi_off = radio_kept(NET_WIFI_POWER) == 0;
        bool bluetooth_off = radio_kept(NET_BLUETOOTH_POWER) == 0;
        p8 was[24];
        bool kept = tune_kept("airplane", was, sizeof(was));
        bool on;
        b32 failed;
        bipolar sent;

        //      Airplane mode is the record that it was asked for, with every radio still off: radios that are off
        //      because nothing raised them are not airplane mode, and raising one by hand ends it.
        if (count == 2)
        {
                host_say(log, host_label "airplane %s\n",
                         kept && wifi_off && bluetooth_off ? "on" : "off");
                return 0;
        }
        return_if(count != 3 || host_onoff(arguments[2]) < 0, host_usage());
        host_need_root();
        on = host_onoff(arguments[2]) > 0;

        //      Type zero is every radio, the modem included.
        if (on)
        {
                //      What was on is kept, and none when nothing was, so that off gives back exactly that: a
                //      machine whose radios are off by default stays so across on and off. Asked again while it
                //      holds, the first record stands.
                failed = kept && wifi_off && bluetooth_off
                             ? 0
                             : tune_remember("airplane", wifi_off && bluetooth_off ? "none"
                                                         : wifi_off ? "bluetooth"
                                                         : bluetooth_off ? "wifi" : "wifi bluetooth");
                failed |= radio_wifi_off(true);
                failed |= radio_bluetooth_power(false, true);
                sent = radio_rfkill(0, true);
        }
        else
        {
                bool wifi_back = kept && tune_has(was, "wifi");
                bool bluetooth_back = kept && tune_has(was, "bluetooth");

                sent = radio_rfkill(0, false);
                failed = 0;
                if (wifi_back)
                {
                        failed |= radio_switch(NET_WIFI_POWER, RADIO_RFKILL_WLAN, true, true);
                        (void)radio_wifi_on(false);
                }
                else if (wifi_off)
                        (void)radio_rfkill(RADIO_RFKILL_WLAN, true);
                if (bluetooth_back)
                        failed |= radio_bluetooth_power(true, true);
                else if (bluetooth_off)
                        (void)radio_rfkill(RADIO_RFKILL_BLUETOOTH, true);
                if (kept)
                        failed |= tune_remember("airplane", "");
        }
        failed |= sent < 0 ? host_fail("/dev/rfkill", sent) : 0;
        return radio_done(failed, on ? "airplane on" : "airplane off");
}

/* The panel's own backlight before a raw one the driver also offers, the way
   systemd-backlight orders them: firmware's, then the platform's, then raw. */
static bool tune_backlight_visit(string_address directory, string_address name,
                                 address_any context)
{
        tune_choose((tune_choice address_to)context, name,
                    tune_says(directory, name, "/type", "firmware") ? 3
                    : tune_says(directory, name, "/type", "platform") ? 2 : 1);
        return true;
}

/* moonwater brightness [N|N%|+N|-N]: the panel's backlight, in percent. */
static b32 tune_brightness(string_address address_to arguments, positive count)
{
        p8 name[64];
        p8 maximum_path[160];
        p8 current_path[160];
        positive maximum = 0;
        positive current = 0;
        positive percent = 0;
        bool relative = false;
        bool lower = false;

        if (count > 3 || (count == 3 && !tune_percent(arguments[2], address_of relative,
                                                      address_of lower, address_of percent)))
                return host_usage();
        if (count == 3)
                host_need_root();

        tune_choice panel = {.rank = 0};

        host_each_entry(TUNE_SYS_BACKLIGHT, tune_backlight_visit, address_of panel);
        string_copy(name, panel.name);
        if (!panel.rank ||
            !string_join(maximum_path, sizeof(maximum_path), TUNE_SYS_BACKLIGHT "/",
                         (string_address)name, "/max_brightness") ||
            !string_join(current_path, sizeof(current_path), TUNE_SYS_BACKLIGHT "/",
                         (string_address)name, "/brightness") ||
            !tune_number(maximum_path, address_of maximum) || !maximum ||
            !tune_number(current_path, address_of current))
        {
                //      Where it stands is an answer, and "nowhere" is one.
                if (count == 2)
                {
                        host_say(log, host_label "this machine has no backlight\n");
                        return 0;
                }
                return host_refuse("this machine has no backlight to set\n");
        }

        if (count == 2)
        {
                host_say(log, host_label "brightness %p%% (%p of %p)\n",
                         (current * 100 + maximum / 2) / maximum, current, maximum);
                return 0;
        }

        {
                positive now = (current * 100 + maximum / 2) / maximum;
                positive target = percent;
                positive value;
                bipolar failed;

                if (relative)
                        target = lower ? (percent > now ? 0 : now - percent) : now + percent;
                if (target > 100)
                        target = 100;
                //      A percent above nought is never rounded down to a dark screen.
                value = (maximum * target + 99) / 100;
                if (target && !value)
                        value = 1;
                failed = tune_write_number(current_path, value);
                return_if(failed < 0, host_fail("brightness", failed));
                host_say(log, host_label "brightness %p%%\n", target);
        }
        return 0;
}

/* A battery of the machine's own, and best the one that can limit its charge.
   A mouse or a keyboard reports its battery here too, with the scope Device,
   and is not the one a charge limit is for. */
static bool tune_battery_visit(string_address directory, string_address name, address_any context)
{
        p8 path[160];

        if (tune_says(directory, name, "/type", "Battery") &&
            !tune_says(directory, name, "/scope", "Device"))
        {
                bool limited = string_join(path, sizeof(path), directory, "/", name,
                                           "/charge_control_end_threshold") &&
                               tune_exists(path);

                tune_choose((tune_choice address_to)context, name, limited ? 2 : 1);
        }
        return true;
}

static bool tune_battery(p8 address_to name, positive room)
{
        tune_choice battery = {.rank = 0};

        host_each_entry(TUNE_SYS_BATTERY, tune_battery_visit, address_of battery);
        return battery.rank && string_length(battery.name) < room &&
               (string_copy(name, battery.name), true);
}

static bipolar tune_charge_apply(string_address percent_text)
{
        p8 name[64];
        p8 path[160];
        bipolar failed = -ENODEV;

        if (!tune_battery(name, sizeof(name)))
                return failed;
        if (!string_join(path, sizeof(path), TUNE_SYS_BATTERY "/", (string_address)name,
                         "/charge_control_end_threshold"))
                return -ERROR_INVALID;
        return tune_write(path, percent_text);
}

/* moonwater charge [limit N|off]: where the battery stops charging. */
static b32 tune_charge(string_address address_to arguments, positive count)
{
        p8 name[64];
        p8 path[160];
        positive capacity = 0;
        positive limit = 0;
        p8 status[24];
        bool have_limit;

        if (count != 2 && !(count == 4 && string_equals(arguments[2], "limit") &&
                            (string_equals(arguments[3], "off") ||
                             byte_is_digit(arguments[3][0]))))
                return host_usage();

        if (!tune_battery(name, sizeof(name)))
        {
                if (count == 2)
                {
                        host_say(log, host_label "this machine has no battery\n");
                        return 0;
                }
                return host_refuse("this machine has no battery\n");
        }

        if (count == 2)
        {
                have_limit = string_join(path, sizeof(path), TUNE_SYS_BATTERY "/",
                                         (string_address)name, "/charge_control_end_threshold") &&
                             tune_number(path, address_of limit);
                if (string_join(path, sizeof(path), TUNE_SYS_BATTERY "/", (string_address)name,
                                "/capacity"))
                        (void)tune_number(path, address_of capacity);
                status[0] = end;
                if (string_join(path, sizeof(path), TUNE_SYS_BATTERY "/", (string_address)name,
                                "/status"))
                        (void)tune_word(path, status, sizeof(status));
                string_format(log, host_label "battery %p%%, %s", capacity, (string_address)status);
                if (have_limit)
                        string_format(log, "; charging stops at %p%%\n", limit);
                else
                        string_format(log, "; this battery has no charge limit\n");
                log_flush();
                return 0;
        }

        {
                positive number = 100;
                bipolar failed;

                if (!string_equals(arguments[3], "off"))
                {
                        positive used;

                        number = string_digits_max(arguments[3], 4, address_of used);
                        if (arguments[3][used] || number < 20 || number > 100)
                                return host_refuse("a charge limit is 20 to 100 percent\n");
                }
                host_need_root();
                {
                        p8 text[8];

                        positive_into_string(text, number);
                        failed = tune_charge_apply((string_address)text);
                        if (failed < 0)
                                return failed == -ENODEV
                                           ? host_refuse("this machine has no battery\n")
                                           : failed == -ENOENT
                                                 ? host_refuse("this battery has no charge limit\n")
                                                 : host_fail("charge", failed);
                        //      A limit of a full charge is the default: nothing to bring back.
                        if (tune_remember("charge.limit", number == 100 ? (string_address)"" : (string_address)text))
                                return 1;
                }
                host_say(log, host_label "charging stops at %p%%\n", number);
                return 0;
        }
}

typedef struct
{
        string_address governor;
        string_address preference;
        positive written;
} tune_profile;

static bool tune_profile_visit(string_address directory, string_address name, address_any context)
{
        tune_profile address_to profile = (tune_profile address_to)context;
        p8 path[160];
        p8 policy[160];

        if (!tune_is_cpu(name) ||
            !string_join(policy, sizeof(policy), directory, "/", name) ||
            string_append_bounded(policy, "/cpufreq/", sizeof(policy)) >= sizeof(policy))
                return true;
        if (profile->governor && string_join(path, sizeof(path), (string_address)policy,
                                             "scaling_available_governors") &&
            tune_lists(path, profile->governor) &&
            string_join(path, sizeof(path), (string_address)policy, "scaling_governor"))
                profile->written += tune_write(path, profile->governor) >= 0;
        if (profile->preference && string_join(path, sizeof(path), (string_address)policy,
                                               "energy_performance_available_preferences") &&
            tune_lists(path, profile->preference) &&
            string_join(path, sizeof(path), (string_address)policy,
                        "energy_performance_preference"))
                profile->written += tune_write(path, profile->preference) >= 0;
        return true;
}

/* The governor and preference a profile means, on every processor that has them: how many writes went through. */
static positive tune_profile_cpus(string_address governor, string_address preference)
{
        tune_profile profile = {governor, preference, 0};

        host_each_entry(TUNE_SYS_CPU, tune_profile_visit, address_of profile);
        return profile.written;
}

/* Apply one of performance, balanced and powersave; false when the machine has no way to. */
static bool tune_power_apply(string_address profile)
{
        bool performance = string_equals(profile, "performance");
        bool balanced = string_equals(profile, "balanced");
        string_address platform = performance ? (string_address)"performance"
                                  : balanced ? (string_address)"balanced" : (string_address)"low-power";
        bool any = false;

        if (!performance && !balanced && !string_equals(profile, "powersave"))
                return false;
        if (!tune_lists(TUNE_SYS_PROFILE "_choices", platform) && !performance && !balanced)
                //      Firmware that names its lowest profile quiet or cool.
                platform = tune_lists(TUNE_SYS_PROFILE "_choices", "quiet") ? (string_address)"quiet"
                           : tune_lists(TUNE_SYS_PROFILE "_choices", "cool") ? (string_address)"cool"
                                                                              : platform;
        if (tune_lists(TUNE_SYS_PROFILE "_choices", platform))
                any = tune_write(TUNE_SYS_PROFILE, platform) >= 0;

        //      A way to is a write that went through: a governor that can be read
        //      and never set is no profile applied.
        any |= tune_profile_cpus(performance ? (string_address)"performance"
                                 : balanced ? (string_address)"schedutil" : (string_address)"powersave",
                                 performance ? (string_address)"performance"
                                 : balanced ? (string_address)"balance_performance" : (string_address)"power") > 0;
        return any;
}

/* moonwater power [performance|balanced|powersave] */
static b32 tune_power(string_address address_to arguments, positive count)
{
        if (count == 2)
        {
                p8 profile[32];
                p8 governor[32];
                p8 preference[40];
                p8 kept[24];

                profile[0] = governor[0] = preference[0] = end;
                (void)tune_word(TUNE_SYS_PROFILE, profile, sizeof(profile));
                (void)tune_word(TUNE_SYS_CPU "/cpu0/cpufreq/scaling_governor", governor, sizeof(governor));
                (void)tune_word(TUNE_SYS_CPU "/cpu0/cpufreq/energy_performance_preference", preference,
                                sizeof(preference));
                if (!profile[0] && !governor[0])
                {
                        host_say(log, host_label "this machine has no power profile\n");
                        return 0;
                }
                kept[0] = end;
                (void)tune_kept("power", kept, sizeof(kept));
                string_format(log, host_label "power %s", kept[0] ? (string_address)kept : (string_address)"(not set here)");
                if (profile[0])
                        string_format(log, "; platform profile %s", (string_address)profile);
                if (governor[0])
                        string_format(log, "; governor %s", (string_address)governor);
                if (preference[0])
                        string_format(log, "; preference %s", (string_address)preference);
                host_say(log, "\n");
                return 0;
        }
        if (count != 3 || (!string_equals(arguments[2], "performance") &&
                           !string_equals(arguments[2], "balanced") &&
                           !string_equals(arguments[2], "powersave")))
                return host_usage();
        host_need_root();
        if (!tune_power_apply(arguments[2]))
                return host_refuse("this machine has no power profile to set\n");
        if (tune_remember("power", arguments[2]))
                return 1;
        host_say(log, host_label "power %s\n", arguments[2]);
        return 0;
}

/* The boost switch the machine has, or -ENOENT when it has neither. */
static bipolar tune_cpu_boost(bool on)
{
        bipolar failed = tune_write(TUNE_SYS_CPU "/cpufreq/boost", on ? "1" : "0");

        return failed == -ENOENT ? tune_write(TUNE_SYS_CPU "/intel_pstate/no_turbo", on ? "0" : "1")
                                 : failed;
}

static bipolar tune_cpu_smt(bool on)
{
        return tune_write(TUNE_SYS_CPU "/smt/control", on ? "on" : "off");
}

/* A processor with no switch to read is online: cpu0 has none. */
static bool tune_online_visit(string_address directory, string_address name, address_any context)
{
        p8 path[160];
        p8 word[4];

        if (tune_is_cpu(name))
                *(positive address_to)context += !string_join(path, sizeof(path), directory, "/",
                                                              name) ||
                                                 string_append_bounded(path, "/online", sizeof(path)) >= sizeof(path) ||
                                                 !tune_word(path, word, sizeof(word)) || word[0] == '1';
        return true;
}

/* moonwater cpu [boost|smt on|off] [online|offline N] */
static b32 tune_cpu(string_address address_to arguments, positive count)
{
        if (count == 2)
        {
                p8 smt[24];
                p8 boost[8];
                positive online = 0;

                boost[0] = smt[0] = end;
                if (tune_exists(TUNE_SYS_CPU "/cpufreq/boost"))
                        (void)tune_word(TUNE_SYS_CPU "/cpufreq/boost", boost, sizeof(boost));
                else if (tune_exists(TUNE_SYS_CPU "/intel_pstate/no_turbo"))
                {
                        (void)tune_word(TUNE_SYS_CPU "/intel_pstate/no_turbo", boost, sizeof(boost));
                        boost[0] = boost[0] == '0' ? '1' : '0';
                }
                (void)tune_word(TUNE_SYS_CPU "/smt/control", smt, sizeof(smt));
                host_each_entry(TUNE_SYS_CPU, tune_online_visit, address_of online);
                string_format(log, host_label "cpu: %p online", online);
                if (boost[0])
                        string_format(log, "; boost %s", boost[0] == '1' ? "on" : "off");
                if (smt[0])
                        string_format(log, "; smt %s", (string_address)smt);
                host_say(log, "\n");
                return 0;
        }
        if (count == 4 && (string_equals(arguments[2], "boost") || string_equals(arguments[2], "smt")) &&
            host_onoff(arguments[3]) >= 0)
        {
                bool on = host_onoff(arguments[3]);
                bool boost = string_equals(arguments[2], "boost");
                bipolar failed;

                host_need_root();
                failed = boost ? tune_cpu_boost(on) : tune_cpu_smt(on);
                if (failed == -ENOENT)
                        return host_refuse(boost ? "this machine has no boost switch\n"
                                                 : "this machine has no SMT switch\n");
                return_if(failed < 0, host_fail(boost ? "cpu boost" : "cpu smt", failed));
                //      Both default to on: nothing to bring back then.
                if (tune_remember(boost ? "cpu.boost" : "cpu.smt", on ? "" : "off"))
                        return 1;
                host_say(log, host_label "cpu %s %s\n", arguments[2], arguments[3]);
                return 0;
        }
        if (count == 4 && (string_equals(arguments[2], "online") || string_equals(arguments[2], "offline")) &&
            byte_is_digit(arguments[3][0]))
        {
                p8 path[160];
                positive number;
                positive used;
                p8 text[8];

                number = string_digits_max(arguments[3], 5, address_of used);
                if (arguments[3][used])
                        return host_refuse("a cpu is a number of at most five digits\n");
                if (!number)
                        return host_refuse("cpu 0 stays\n");
                host_need_root();
                bipolar failed;

                positive_into_string(text, number);
                failed = string_join(path, sizeof(path), TUNE_SYS_CPU "/cpu", (string_address)text,
                                     "/online")
                             ? tune_write(path, string_equals(arguments[2], "online") ? "1" : "0")
                             : -ENOENT;
                if (failed == -ENOENT)
                        return host_refuse("that cpu cannot be switched\n");
                return_if(failed < 0, host_fail("cpu", failed));
                host_say(log, host_label "cpu %s %p\n", arguments[2], number);
                return 0;
        }
        return host_usage();
}

/*
        A machine asleep in s2idle wakes only for what is armed to wake it, and
        the kernel leaves a keyboard's wakeup disabled: nothing but the power
        button then brings it back, which from the chair is a machine that
        never woke. PS/2 ports and USB keyboards (an interface of class 03
        speaking the boot keyboard protocol, 01) are armed before sleeping and
        left armed. Not the whole HID class: a handheld's own controller is
        HID too, presents itself as keyboard and mouse, and woke the machine
        a few seconds after it slept. What is armed is said, and after the
        wake the kernel's own wakeup sources are asked which of them fired.
*/
static bool tune_usb_keyboard(string_address bus, string_address name)
{
        for (positive interface = 0; interface < 4; interface++)
        {
                p8 path[192];
                p8 word[16];
                p8 tail[40];
                p8 number[4] = {(p8)('0' + interface), 0, 0, 0};

                if (!string_join(tail, sizeof(tail), "/", name, ":1.") ||
                    string_append_bounded(tail, (string_address)number, sizeof(tail)) >= sizeof(tail) ||
                    !string_join(path, sizeof(path), bus, (string_address)tail,
                                 "/bInterfaceClass") ||
                    !tune_word(path, word, sizeof(word)) || !string_equals((string_address)word, "03") ||
                    !string_join(path, sizeof(path), bus, (string_address)tail,
                                 "/bInterfaceProtocol") ||
                    !tune_word(path, word, sizeof(word)) || !string_equals((string_address)word, "01"))
                        continue;
                return true;
        }
        return false;
}

/* What is armed to wake the machine: a PS/2 port, or a USB device with a boot keyboard on it. */
static bool tune_wake_visit(string_address directory, string_address name, address_any context)
{
        p8 path[192];
        p8 word[16];

        //      An interface's own name, 1-2:1.0, has no wakeup file.
        if (*(bool address_to)context && (string_first_of(name, ':') || !tune_usb_keyboard(directory, name)))
                return true;
        if (!string_join(path, sizeof(path), directory, "/", name) ||
            string_append_bounded(path, "/power/wakeup", sizeof(path)) >= sizeof(path))
                return true;
        if (tune_word(path, word, sizeof(word)) && string_equals((string_address)word, "disabled") &&
            tune_write(path, "enabled") >= 0)
                host_say(log, host_label "wakes on %s\n", name);
        return true;
}

static fn tune_wake_arm(string_address bus, bool usb)
{
        host_each_entry(bus, tune_wake_visit, address_of usb);
}

#define TUNE_WAKE_MOST 128
#define TUNE_WAKE_CLASS "/sys/class/wakeup"

typedef struct
{
        p8 name[48];
        positive count;
} tune_source;

typedef struct
{
        tune_source source[TUNE_WAKE_MOST];
        positive kept;
        positive seen;
} tune_sources;

static bool tune_source_visit(string_address directory, string_address name, address_any context)
{
        tune_sources address_to sources = (tune_sources address_to)context;
        tune_source address_to source = sources->source + sources->kept;
        p8 path[160];

        sources->seen++;
        if (sources->kept == TUNE_WAKE_MOST || string_length(name) >= sizeof(source->name))
                return true;
        string_copy(source->name, name);
        if (!string_join(path, sizeof(path), directory, "/", name, "/event_count") ||
            !tune_number(path, address_of source->count))
                source->count = 0;
        sources->kept++;
        return true;
}

/* The event count of every wakeup source there is, by name. */
static fn tune_wake_counts(tune_sources address_to sources)
{
        sources->kept = sources->seen = 0;
        host_each_entry(TUNE_WAKE_CLASS, tune_source_visit, sources);
}

#define TUNE_LOG_LINES 60
#define TUNE_LOG_WIDTH 160
#define TUNE_LOG_ROOM (TUNE_LOG_LINES * TUNE_LOG_WIDTH + 512)

/* The wakeup sources that fired since the counts were taken, said and put first in text for /root/sleep.log. A source is the one of its name: one that came or went while the machine slept is not matched with whatever stood where it did in the directory. */
static fn tune_wake_report(tune_sources address_to before, p8 address_to text)
{
        tune_sources after;
        bool any = false;

        tune_wake_counts(address_of after);
        string_copy_bounded(text, "woken by:", TUNE_LOG_ROOM);
        for (positive at = 0; at < after.kept; at++)
        {
                p8 path[160];
                p8 label[48];
                positive was = 0;

                for (positive then = 0; then < before->kept; then++)
                        if (string_equals(before->source[then].name, after.source[at].name))
                                was = before->source[then].count;
                if (after.source[at].count == was)
                        continue;
                if (!string_join(path, sizeof(path), TUNE_WAKE_CLASS, "/", after.source[at].name,
                                 "/name") ||
                    !tune_word(path, label, sizeof(label)))
                        string_copy_bounded(label, after.source[at].name, sizeof(label));
                string_append_bounded(text, " ", TUNE_LOG_ROOM);
                string_append_bounded(text, (string_address)label, TUNE_LOG_ROOM);
                any = true;
        }
        if (!any)
                string_append_bounded(text, " nothing the kernel counted", TUNE_LOG_ROOM);
        if (after.seen > after.kept || before->seen > before->kept)
                string_append_bounded(text, " (some sources past the room were not counted)", TUNE_LOG_ROOM);
        host_say(log, host_label "%s\n", (string_address)text);
        string_append_bounded(text, "\n", TUNE_LOG_ROOM);
}

/* What the kernel says went wrong, when it says anything: the device, the step and the error. */
static fn tune_suspend_failure(void)
{
        p8 device[64];
        p8 step[32];
        positive errno_value = 0;

        if (!tune_word(TUNE_SYS_POWER "/suspend_stats/last_failed_dev", device, sizeof(device)) || !device[0])
                return;
        if (!tune_word(TUNE_SYS_POWER "/suspend_stats/last_failed_step", step, sizeof(step)))
                step[0] = 0;
        (void)tune_number(TUNE_SYS_POWER "/suspend_stats/last_failed_errno", address_of errno_value);
        host_say(log, host_label "the kernel stopped at %s (%s), last error %d\n",
                 (string_address)device, (string_address)step, (b32)errno_value);
}

/*
        The last kernel lines about the card, the suspend and Canvas, put after
        the wake in /root/sleep.log: the next report from a machine whose
        screen did not come back names the step that failed without anyone
        having to run dmesg on a machine that shows nothing.
*/
static fn tune_kernel_lines(p8 address_to text)
{
        static const string_address wanted[] = {
            (string_address) "moonwater canvas", (string_address) "PM:", (string_address) "amdgpu",
            (string_address) "drm", (string_address) "backlight"};
        p8 record[2048];
        p8 ring[TUNE_LOG_LINES][TUNE_LOG_WIDTH];
        positive count = 0;
        bipolar handle = system_open_at(AT_FDCWD, "/dev/kmsg", FILE_READ | O_NONBLOCK | O_CLOEXEC);

        if (handle < 0)
                return;
        for (;;)
        {
                bipolar got = system_read_once(handle, record, sizeof(record) - 1);
                string_address line;
                bool keep = false;

                // Interrupted, or a record overwritten before it was read.
                if (got == -4 || got == -32)
                        continue;
                if (got <= 0)
                        break;
                record[got] = end;
                line = string_find(record, (string_address) ";");
                if (!line)
                        continue;
                line++;
                ((p8 address_to)line)[string_first_of_or_end(line, '\n') - line] = end;
                for (positive at = 0; at < sizeof(wanted) / sizeof(wanted[0]); at++)
                        keep |= string_find(line, wanted[at]) != null;
                if (keep)
                        string_copy_bounded(ring[count++ % TUNE_LOG_LINES], line, TUNE_LOG_WIDTH);
        }
        system_close((positive)handle);
        string_append_bounded(text, "kernel log lines:\n", TUNE_LOG_ROOM);
        for (positive at = count > TUNE_LOG_LINES ? count - TUNE_LOG_LINES : 0; at < count; at++)
        {
                string_append_bounded(text, (string_address)ring[at % TUNE_LOG_LINES], TUNE_LOG_ROOM);
                string_append_bounded(text, "\n", TUNE_LOG_ROOM);
        }
}

/*
        How the sleep went as the kernel counts it: whether the platform ever
        reached its deepest state (last_hw_sleep is microseconds in it, and a
        fan that spins through a sleep is usually a nought there) and how many
        suspends failed.
*/
static fn tune_sleep_stats(p8 address_to text)
{
        static const string_address names[] = {
            (string_address) "success", (string_address) "fail", (string_address) "last_hw_sleep",
            (string_address) "total_hw_sleep", (string_address) "max_hw_sleep"};

        string_append_bounded(text, "suspend_stats:", TUNE_LOG_ROOM);
        for (positive at = 0; at < sizeof(names) / sizeof(names[0]); at++)
        {
                p8 path[96];
                p8 number[24];
                positive value = 0;

                if (!string_join(path, sizeof(path), TUNE_SYS_POWER "/suspend_stats/", names[at]) ||
                    !tune_number(path, address_of value))
                        continue;
                positive_into_string(number, value);
                string_append_bounded(text, " ", TUNE_LOG_ROOM);
                string_append_bounded(text, names[at], TUNE_LOG_ROOM);
                string_append_bounded(text, "=", TUNE_LOG_ROOM);
                string_append_bounded(text, (string_address)number, TUNE_LOG_ROOM);
        }
        string_append_bounded(text, "\n", TUNE_LOG_ROOM);
}

#define TUNE_BACKLIGHTS 4
#define TUNE_NO_LEVEL ((positive)-1)

typedef struct
{
        p8 name[64];
        positive level;
} tune_light;

typedef struct
{
        tune_light light[TUNE_BACKLIGHTS];
        positive kept;
} tune_lights;

static bool tune_light_visit(string_address directory, string_address name, address_any context)
{
        tune_lights address_to lights = (tune_lights address_to)context;
        tune_light address_to light = lights->light + lights->kept;
        p8 path[160];

        if (lights->kept == TUNE_BACKLIGHTS || string_length(name) >= sizeof(light->name))
                return true;
        string_copy(light->name, name);
        if (!string_join(path, sizeof(path), directory, "/", name, "/brightness") ||
            !tune_number(path, address_of light->level))
                light->level = TUNE_NO_LEVEL;
        lights->kept++;
        return true;
}

/* Each backlight's level, by name, before the machine sleeps. */
static fn tune_backlight_save(tune_lights address_to lights)
{
        lights->kept = 0;
        host_each_entry(TUNE_SYS_BACKLIGHT, tune_light_visit, lights);
}

/*
        And put back after it. The panel's own driver keeps its level across a
        suspend on the machines where that works, and a wake that left the
        backlight off or at nought (bl_power still blanked, or a level the
        firmware picked) is a screen that is on and dark. The level is written
        back whatever the file says: it is what the class device last stored,
        and after a wake the panel may not be doing it. Each by its name, so a
        panel that came or went while the machine slept is not given another's.
*/
static fn tune_backlight_restore(tune_lights address_to lights)
{
        for (positive at = 0; at < lights->kept; at++)
        {
                string_address name = (string_address)lights->light[at].name;
                p8 path[160];
                positive blank = 0;

                if (string_join(path, sizeof(path), TUNE_SYS_BACKLIGHT, "/", name, "/bl_power") &&
                    tune_number(path, address_of blank) && blank &&
                    tune_write(path, "0") >= 0)
                        host_say(log, host_label "backlight %s was blanked, unblanked\n", name);
                if (lights->light[at].level == TUNE_NO_LEVEL ||
                    !string_join(path, sizeof(path), TUNE_SYS_BACKLIGHT, "/", name, "/brightness"))
                        continue;
                //      Written even when the file already says so: it is the value
                //      the class device last stored, not what the panel is doing.
                if (tune_write_number(path, lights->light[at].level) >= 0)
                        host_say(log, host_label "backlight %s set to %p\n", name, lights->light[at].level);
        }
}

/* A kernel switch turned on for the length of a sleep, and what it was. */
typedef struct
{
        string_address path;
        p8 was[8];
        bool changed;
} tune_flag;

static fn tune_flag_on(tune_flag address_to flag, string_address path)
{
        flag->path = path;
        flag->changed = tune_word(path, flag->was, sizeof(flag->was)) &&
                        !string_equals((string_address)flag->was, "1") &&
                        tune_write(path, "1") >= 0;
}

static fn tune_flag_back(tune_flag address_to flag)
{
        if (flag->changed)
                (void)tune_write(flag->path, (string_address)flag->was);
}

/* moonwater sleep and moonwater hibernate: the kernel's own suspend and hibernate. */
static b32 tune_suspend(string_address verb, string_address state)
{
        tune_sources sources;
        tune_lights lights;
        tune_flag messages;
        tune_flag times;
        p8 text[TUNE_LOG_ROOM];

        if (!tune_lists(TUNE_SYS_POWER "/state", state))
                return host_refuse(string_equals(state, "mem")
                                       ? "this kernel does not offer sleep\n"
                                       : "this kernel does not offer hibernate\n");
        host_need_root();
        //      So a sleep that never wakes leaves the device it stopped at in the
        //      log, and what they were is put back when it is over.
        tune_flag_on(address_of messages, TUNE_SYS_POWER "/pm_debug_messages");
        tune_flag_on(address_of times, TUNE_SYS_POWER "/pm_print_times");
        tune_wake_arm("/sys/bus/usb/devices", true);
        tune_wake_arm("/sys/bus/serio/devices", false);
        if (string_equals(state, "mem"))
        {
                p8 mode[64];

                //      Which kind of sleep, as the kernel spells it: [s2idle] deep.
                if (tune_word(TUNE_SYS_POWER "/mem_sleep", mode, sizeof(mode)))
                        host_say(log, host_label "sleeping, mem_sleep %s\n", (string_address)mode);
        }
        tune_wake_counts(address_of sources);
        tune_backlight_save(address_of lights);
        system_call(syscall(sync));
        {
                bipolar failed = tune_write(TUNE_SYS_POWER "/state", state);

                if (failed < 0)
                {
                        tune_suspend_failure();
                        text[0] = end;
                        tune_sleep_stats(text);
                        tune_kernel_lines(text);
                        (void)host_write_text("/root/sleep.log", (string_address)text);
                        tune_flag_back(address_of messages);
                        tune_flag_back(address_of times);
                        return host_fail(verb, failed);
                }
        }
        //      Reached again once the machine has woken.
        tune_wake_report(address_of sources, text);
        host_say(log, host_label "awake again\n");
        //      What a sleep can leave behind: the panel's level, the link (the
        //      watcher is asked to look again, since a carrier that came back
        //      before it was listening is news it never heard), and the
        //      settings the machine put in place at boot.
        tune_backlight_restore(address_of lights);
        radio_net_wake();
        tune_restore();
        tune_sleep_stats(text);
        tune_kernel_lines(text);
        (void)host_write_text("/root/sleep.log", (string_address)text);
        tune_flag_back(address_of messages);
        tune_flag_back(address_of times);
        return 0;
}

/* What was kept, put back at boot. */
static fn tune_restore(void)
{
        p8 text[512];
        p8 value[24];
        bipolar got = tune_load(text, sizeof(text));

        if (got <= 0)
                return;
        if (tune_find(text, (positive)got, "power", value, sizeof(value)))
                (void)tune_power_apply((string_address)value);
        if (tune_find(text, (positive)got, "cpu.boost", value, sizeof(value)) &&
            string_equals((string_address)value, "off"))
                (void)tune_cpu_boost(false);
        if (tune_find(text, (positive)got, "cpu.smt", value, sizeof(value)) &&
            string_equals((string_address)value, "off"))
                (void)tune_cpu_smt(false);
        if (tune_find(text, (positive)got, "charge.limit", value, sizeof(value)))
                (void)tune_charge_apply((string_address)value);
        //      Airplane mode is every radio off, and the modem has no word of its own.
        if (tune_find(text, (positive)got, "airplane", value, sizeof(value)) &&
            radio_kept(NET_WIFI_POWER) == 0 && radio_kept(NET_BLUETOOTH_POWER) == 0)
                (void)radio_rfkill(0, true);
}

/* moonwater battery [NAME|auto|none]: the battery the settings window's Battery row reads. auto (the default) is the
   first present battery; none is no battery; NAME is a battery or UPS the kernel names under power_supply. It is kept
   in /root/tune like the charge limit, with auto kept as nothing. */
static b32 tune_battery_verb(string_address address_to arguments, positive count)
{
        p8 text[512];
        p8 value[72];
        bipolar got;
        string_address name;
        string_address stored;

        if (count == 1)
        {
                got = tune_load(text, sizeof(text));
                if (got > 0 && tune_find(text, (positive)got, "battery", value, sizeof(value)) && value[0])
                        host_say(log, host_label "battery %s\n", (string_address)value);
                else
                        host_say(log, host_label "battery auto\n");
                return 0;
        }
        if (count != 2)
                return host_usage();

        name = arguments[1];
        if (string_equals(name, (string_address) "auto"))
                stored = (string_address) "";
        else if (string_equals(name, (string_address) "none"))
                stored = (string_address) "none";
        else
        {
                positive at = 0;

                // A name is one directory entry: no slash, no dot-name, and a battery or a UPS under the kernel's power_supply.
                while (name[at] && name[at] != '/' && at < 64)
                        at++;
                if (!at || name[0] == '.' || name[at] || at >= 64 ||
                    !(tune_says((string_address)TUNE_SYS_BATTERY, name, (string_address) "/type", (string_address) "Battery") ||
                      tune_says((string_address)TUNE_SYS_BATTERY, name, (string_address) "/type", (string_address) "UPS")))
                        return host_refuse("this machine has no battery called %s\n", name);
                stored = name;
        }
        host_need_root();
        if (tune_remember("battery", stored))
                return 1;
        host_say(log, host_label "battery %s\n", name);
        return 0;
}

/* sleep and hibernate take no words, and differ in what they ask the kernel for. */
static b32 tune_sleep_verb(string_address address_to arguments, positive count)
{
        return count == 2 ? tune_suspend(arguments[1], "mem") : host_usage();
}

static b32 tune_hibernate_verb(string_address address_to arguments, positive count)
{
        return count == 2 ? tune_suspend(arguments[1], "disk") : host_usage();
}

/* ---- usb: which devices plugged in are let in. ---- */

/*
        moonwater usb: what a device plugged in may do.

        The kernel holds the switches. Every USB device and every interface
        of one has an authorized file in sysfs: a device at 0 is not
        configured (the kernel has read its device descriptor and its
        strings, and nothing binds to it), and an interface at 0 binds no
        driver. A root hub has two defaults for what arrives below it,
        authorized_default for a new device and interface_authorized_default
        for its interfaces, and usbcore's own authorized_default is what a
        controller that arrives later starts with. This is the policy that
        turns them, kept in /root/usb.policy a line each:

          mode off|lock                     lock holds what is new; off is the kernel's own
          class NAME allow|deny             what any device may bring up besides its rule
          allow VID:PID/SERIAL [CLASS,...]  a device let in, as the classes it had then
          deny VID:PID/SERIAL               a device held, locked or not

        One function judges a device (usb_judge) and one puts the judgement
        in force (usb_apply); boot, every command and the watcher call the
        two and nothing else, so they cannot disagree. The watcher is a
        process of its own, started when the policy has something to hold,
        that hears the kernel's uevents and judges again at each burst.

        A rule names a device by what it says of itself, and a device says
        what it likes: a rule stops a device the owner never let in, not one
        that copies the identity of one that was.
*/
#define USB_POLICY "/root/usb.policy"
#define USB_LOG "/root/usb.log"
#define USB_LOCK_PATH HOST_STATE "/usb.lock"
#define USB_WATCHER_PATH HOST_STATE "/usb.watcher"
// Held by the one first lock that is waiting for USB to settle; there once a settle has ended this boot.
#define USB_SETTLE_PATH HOST_STATE "/usb.settle"
#define USB_SETTLED_PATH HOST_STATE "/usb.settled"
// A lock in force that /root/usb.policy does not keep: the policy, and which /root it is for and why.
#define USB_RUN_POLICY HOST_STATE "/usb.policy"
#define USB_UNKEPT_PATH HOST_STATE "/usb.unkept"
#define USB_DEVICES "/sys/bus/usb/devices"
#define USB_PROBE "/sys/bus/usb/drivers_probe"
#define USB_MODULE_DEFAULT "/sys/module/usbcore/parameters/authorized_default"
// What one census keeps, and how many rules a policy holds.
#define USB_MOST 64
#define USB_INTERFACES_MOST 256
#define USB_RULES_MOST 64
#define USB_PORT_ROOM 32
#define USB_KEY_ROOM 128
// Where a device is by names that do not move between boots: its controller, its root hub, its port path.
#define USB_PLACE_ROOM 64
#define USB_CONTROLLER_SET "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ._:-"
// A rule as text: its id, its key, its place, and the classes an allow rule keeps.
#define USB_RULE_ROOM (USB_KEY_ROOM + USB_PLACE_ROOM + 80)
#define USB_SERIAL_ROOM 512
#define USB_TEXT_ROOM 160
// How much of what a device says of itself is shown, at most.
#define USB_SHOWN 64
#define USB_POLICY_ROOM 16384
#define USB_LOG_MOST 65536
#define USB_LOG_LINE 640
#define USB_LOG_SHOWN 20
#define USB_LOG_ASKED 1000
// NETLINK_KOBJECT_UEVENT, and the kernel's group on it.
#define USB_UEVENT 15
#define USB_REWALK_NS ((p64)10000000000)
#define USB_POLL_NS ((p64)250000000)
#define USB_SETTLE_NS ((p64)50000000)
// A first boot's lock waits for USB to settle: unchanged this long, up this long at least, and at most.
#define USB_STILL_NS ((p64)2000000000)
#define USB_UP_LEAST_NS ((p64)3000000000)
#define USB_UP_MOST_NS ((p64)15000000000)

/*
        What an interface is, by its class (and, where one class is two
        things, its subclass and protocol). Storage is 08; hid 03 (keyboards,
        mice, and every security key and game pad besides); audio 01; video
        0e, the still cameras of 06 and the audio-video devices of 10; net
        the communications classes 02 and 0a (Ethernet over CDC, NCM, MBIM,
        RNDIS, and the modems of ACM with them) and RNDIS where it is
        e0/01/03 or ef/04; bluetooth e0/01/01 and e0/01/04; hub 09. Other is
        everything else: printers, smart cards, firmware update (fe), and the
        vendor-specific ff that most wifi sticks and many Ethernet adapters
        speak.
*/
enum
{
        USB_STORAGE,
        USB_HID,
        USB_AUDIO,
        USB_VIDEO,
        USB_NET,
        USB_BLUETOOTH,
        USB_HUB,
        USB_OTHER,
        USB_KINDS
};

static const string_address usb_kinds[USB_KINDS] = {
    "storage", "hid", "audio", "video", "net", "bluetooth", "hub", "other",
};

static p8 usb_kind(p32 class, p32 subclass, p32 protocol)
{
        switch (class)
        {
        case 0x08:
                return USB_STORAGE;
        case 0x03:
                return USB_HID;
        case 0x01:
                return USB_AUDIO;
        case 0x06:
        case 0x0e:
        case 0x10:
                return USB_VIDEO;
        case 0x02:
        case 0x0a:
                return USB_NET;
        case 0x09:
                return USB_HUB;
        case 0xe0:
                return subclass != 1                     ? USB_OTHER
                       : protocol == 1 || protocol == 4 ? USB_BLUETOOTH
                       : protocol == 3                  ? USB_NET
                                                        : USB_OTHER;
        case 0xef:
                return subclass == 4 ? USB_NET : USB_OTHER;
        }
        return USB_OTHER;
}

static bipolar usb_kind_named(string_address name)
{
        positive at = string_table_find(name, usb_kinds, sizeof(usb_kinds[0]), USB_KINDS);

        return at < USB_KINDS ? (bipolar)at : -1;
}

// The kind length bytes of a policy's line name; -1 for none.
static bipolar usb_kind_word(string_address text, positive length)
{
        p8 name[16];

        if (!length || length >= sizeof(name))
                return -1;
        memory_copy(name, text, length);
        name[length] = end;
        return usb_kind_named((string_address)name);
}

typedef struct
{
        p8 port[USB_PORT_ROOM];    // its name in sysfs: 1-4.1, or usb1 for a root hub
        p8 place[USB_PLACE_ROOM];  // 0000:00:14.0/0002/4.1 (a root hub's: 0000:00:14.0/0002), or empty
        p8 id[10];                 // vvvv:pppp
        p8 devnum[8];              // which enumeration this is: another at the same port has another
        p8 key[USB_KEY_ROOM];      // its serial as usb_key spells it, or -
        p8 product[USB_TEXT_ROOM]; // what it says it is, as it says it
        positive product_length;
        bool root;
        bool says_fixed;
        bool fixed; // it and every hub above it say fixed
        bool authorized;
} usb_device;

typedef struct
{
        p8 name[USB_PORT_ROOM]; // 1-4.1:1.0
        p8 kind;
        bool authorized;
        bool bound;
        bool fresh; // authorized by this pass, and asked to probe
} usb_interface;

typedef struct
{
        usb_device device[USB_MOST];
        usb_interface interface[USB_INTERFACES_MOST];
        positive count;
        positive interfaces;
        positive dropped;
} usb_census;

#define USB_ALLOW 1
#define USB_DENY 2

typedef struct
{
        p8 kind;
        p8 kinds; // an allow rule's classes, a bit each: what the device was when it was allowed (0: all)
        p8 id[10];
        p8 key[USB_KEY_ROOM];
        p8 port[USB_PLACE_ROOM]; // for a key of -, the place the rule is pinned to (or, written before places, the port)
} usb_rule;

typedef struct
{
        usb_rule rule[USB_RULES_MOST];
        positive count;
        positive skipped; // lines that are no rule, and rules past the most
        p8 allowed;       // a bit for each kind a device no rule names may bring up
        bool lock;
        bool kept;    // the file is there, or a lock this /root could not keep is in force (unkept)
        bool known;   // and could be read: it is root's
        bool cut;     // longer than is read
        bool unkept;  // what is read is USB_RUN_POLICY: /root/usb.policy could not be written or read
        bipolar error; // why /root/usb.policy could not be read, when it could not
} usb_policy;

/*
        A serial, in one spelling everywhere: shown by usb, kept in the
        policy and the log, typed after VID:PID/ and compared. The device
        chooses it, so every byte a terminal or the policy's lines could act
        on is \xNN, as memory_into_escaped spells them (a control, a blank,
        a quote, a backslash, anything past ASCII), and a leading - is too,
        which leaves - alone to mean none. A serial too long to spell in the
        room is -# and the sixteen hex digits of its xxh64, which no serial
        spells (a key begins with - only as these two): a serial cut short
        would name every device that starts the same way, and spelled as
        none it was pinned to its port, where a device with no serial and
        the same VID:PID was the same device to its rule. Anyone who can read
        the serial can copy it, hashed or not, as with any serial.
*/
static fn usb_key(p8 address_to into, p8 address_to serial, bipolar length)
{
        positive skip = length > 0 && serial[0] == '-';
        positive2 spelled;

        string_copy(into, "-");
        if (length <= 0)
                return;
        if (skip)
                memory_copy(into, "\\x2d", 4);
        spelled = memory_into_escaped(into + 4 * skip, serial + skip, (positive)length - skip,
                                      USB_KEY_ROOM - 1 - 4 * skip, 63);
        if (spelled.x != (positive)length - skip)
        {
                p64 hash = hash_xxh64(serial, (positive)length, 0);
                p8 bytes[8];

                for (positive at = 0; at < 8; at++)
                        bytes[at] = (p8)(hash >> (56 - 8 * at));
                memory_copy(into, "-#", 2);
                into[2 + memory_into_hex(into + 2, bytes, 8)] = end;
                return;
        }
        into[spelled.y + 4 * skip] = end;
}

/* Whether a typed serial is one usb_key could have spelled: printable ASCII,
   \xNN where it stands for a byte, and those bytes spelled back the same. A
   control byte typed is refused here, as is anything else the policy's own
   lines could not keep. */
static bool usb_key_typed(string_address text)
{
        p8 raw[USB_KEY_ROOM];
        p8 again[USB_KEY_ROOM];
        positive length = string_length(text);
        positive used = 0;

        if (!length || length >= USB_KEY_ROOM || string_equals(text, "-"))
                return false;
        for (positive at = 0; at < length; at++)
        {
                positive digits = 0;

                if (!byte_is_graphic(text[at]))
                        return false;
                if (text[at] != '\\')
                {
                        raw[used++] = (p8)text[at];
                        continue;
                }
                if (text[at + 1] != 'x')
                        return false;
                raw[used++] = (p8)string_digits_hexadecimal_max(text + at + 2, 2, address_of digits);
                if (digits != 2)
                        return false;
                at += 3;
        }
        usb_key(again, raw, (bipolar)used);
        return string_equals((string_address)again, text);
}

// A port as sysfs names a device below a root hub: 1-4, 1-4.1, 3-10.2.1.
static bool usb_port_valid(string_address text)
{
        positive length = string_length(text);
        positive bus = string_span_of_set(text, "0123456789");

        return bus && length < USB_PORT_ROOM && text[bus] == '-' && byte_is_digit(text[bus + 1]) &&
               text[length - 1] != '.' && !string_find(text, "..") &&
               string_span_of_set(text + bus + 1, "0123456789.") == length - bus - 1;
}

/*
        A place: the controller a root hub belongs to, as the root hub's own
        serial names it (its bus address: 0000:00:14.0 for a PCI one), the
        root hub's product (0002 for USB 2, 0003 for USB 3: an xHCI has one of
        each, behind the same address), and the port path below it, as the
        device's sysfs name has it after the bus number: 0000:00:14.0/0002/4.1.
        The bus number in 1-4.1 is the order controllers came up in, which a
        machine whose controllers probe at once (EHCI does) can change between
        boots, and a rule pinned to it then held the keyboard it was made for.
*/
static bool usb_place_valid(string_address text)
{
        positive length = string_length(text);
        positive controller = string_span_of_set(text, USB_CONTROLLER_SET);
        string_address path = text + controller + 6;

        return controller && length < USB_PLACE_ROOM && text[controller] == '/' &&
               string_span_of_set(text + controller + 1, "0123456789abcdef") == 4 && text[controller + 5] == '/' &&
               byte_is_digit(path[0]) && text[length - 1] != '.' && !string_find(path, "..") &&
               string_span_of_set(path, "0123456789.") == string_length(path);
}

/* VID:PID, VID:PID/SERIAL or VID:PID/-@PLACE, into a rule's id, key and
   place; -@PORT, a port by bus number as rules were pinned before places,
   is read as it was. The key is empty when VID:PID was all there was, for
   the present devices to say. */
static bool usb_id_parse(string_address text, usb_rule address_to rule)
{
        static const p8 hex[] = "0123456789abcdefABCDEF";
        string_address rest;

        rule->key[0] = end;
        rule->port[0] = end;
        if (string_length(text) < 9 || text[4] != ':' || string_span_of_set(text, (string_address)hex) != 4 ||
            string_span_of_set(text + 5, (string_address)hex) != 4 || (text[9] && text[9] != '/'))
                return false;
        memory_copy(rule->id, text, 9);
        memory_to_lower_ascii(rule->id, 9);
        rule->id[9] = end;
        if (!text[9])
                return true;
        rest = text + 10;
        if (string_has_prefix(rest, "-@"))
        {
                if (!usb_port_valid(rest + 2) && !usb_place_valid(rest + 2))
                        return false;
                string_copy(rule->key, "-");
                string_copy(rule->port, rest + 2);
                return true;
        }
        //      A serial too long to keep, as usb_key spells it.
        if (string_has_prefix(rest, "-#"))
        {
                if (string_length(rest) != 18 || string_span_of_set(rest + 2, "0123456789abcdef") != 16)
                        return false;
                string_copy(rule->key, rest);
                return true;
        }
        if (!usb_key_typed(rest))
                return false;
        string_copy(rule->key, rest);
        return true;
}

// A rule as the policy keeps it and usb shows it: its id, and an allow rule's classes after a blank.
static fn usb_rule_text(p8 address_to into, positive room, usb_rule address_to rule)
{
        bool pinned = string_equals((string_address)rule->key, "-");

        (void)string_join(into, room, (string_address)rule->id, "/", (string_address)rule->key,
                          pinned ? (string_address) "@" : (string_address) "",
                          pinned ? (string_address)rule->port : (string_address) "");
        for (positive kind = 0, first = 1; kind < USB_KINDS; kind++)
                if ((rule->kinds >> kind) & 1)
                {
                        string_append_bounded(into, first ? (string_address) " " : (string_address) ",", room);
                        string_append_bounded(into, usb_kinds[kind], room);
                        first = 0;
                }
}

// The classes of a list a comma apart (storage,hid), as a rule keeps them; false for a word that is none.
static bool usb_kinds_parse(string_address text, p8 address_to kinds)
{
        *kinds = 0;
        while (*text)
        {
                positive length = (positive)(string_first_of_or_end(text, ',') - text);
                bipolar kind = usb_kind_word(text, length);

                if (kind < 0)
                        return false;
                *kinds |= (p8)(1u << kind);
                text += length + (text[length] == ',');
        }
        return *kinds != 0;
}

/* A sysfs file of a device or an interface, as it is but for the newline the
   kernel ends it with: a serial or a product is the device's to choose, and
   nothing else of it is trimmed. Its length, or negative. */
static bipolar usb_read(string_address name, string_address leaf, p8 address_to into, positive room)
{
        p8 path[HOST_PATH_ROOM];
        bipolar got;

        into[0] = end;
        if (!string_join(path, sizeof(path), USB_DEVICES "/", name, "/", leaf))
                return -1;
        got = file_slurp_once_at(AT_FDCWD, (string_address)path, into, room);
        if (got > 0 && into[got - 1] == '\n')
                into[--got] = end;
        return got;
}

static bool usb_says(string_address name, string_address leaf, string_address word)
{
        p8 text[16];

        return usb_read(name, leaf, text, sizeof(text)) >= 0 && string_equals((string_address)text, word);
}

/* One file of a device set to a word, when it says another: a device already
   as it should be is never written, so a pass that changes nothing
   re-enumerates nothing. */
static bipolar usb_set(string_address name, string_address leaf, string_address word)
{
        p8 path[HOST_PATH_ROOM];

        if (usb_says(name, leaf, word))
                return 0;
        if (!string_join(path, sizeof(path), USB_DEVICES "/", name, "/", leaf))
                return -1;
        return tune_write((string_address)path, word);
}

/*
        The usb lock. A pass holds it while it writes authorized files, and
        the kernel answers a device's only once the device has (a set
        configuration, with its timeouts, for one that stalls): so every
        taker waits a bounded time, a verb says why after two seconds, and
        an interrupted wait is asked again. Negative: the open's own error,
        or -ETIMEDOUT.
*/
#define USB_LOCK_WAIT_NS ((p64)20000000000)
#define USB_LOCK_SAY_NS ((p64)2000000000)
#define USB_LOCK_POLL_NS ((p64)50000000)

static bipolar usb_lock_take(bool say)
{
        p64 start = system_clock_ns(HOST_CLOCK_BOOTTIME);
        bool said = false;

        for (;;)
        {
                bipolar lock = host_lock(USB_LOCK_PATH, false);
                p64 waited = system_clock_ns(HOST_CLOCK_BOOTTIME) - start;

                if (lock >= 0 || (lock != -EAGAIN && lock != -EINTR))
                        return lock;
                if (waited >= USB_LOCK_WAIT_NS)
                        return -ETIMEDOUT;
                if (say && !said && waited >= USB_LOCK_SAY_NS)
                {
                        host_say(log, host_label "usb waits for the usb lock: the watcher is judging a device "
                                                 "that is slow to answer\n");
                        said = true;
                }
                host_pause(USB_LOCK_POLL_NS);
        }
}

// A verb that could not take the usb lock, said: it changed nothing.
static b32 usb_lock_refused(bipolar error)
{
        b32 owner = host_lock_owner(USB_WATCHER_PATH);

        if (error != -ETIMEDOUT)
                return host_fail(USB_LOCK_PATH, error);
        return owner ? host_refuse("the usb lock is still held after 20 seconds (the watcher, pid %p, is judging a "
                                   "device that does not answer); nothing was changed\n",
                                   (positive)owner)
                     : host_refuse("the usb lock is still held after 20 seconds; nothing was changed\n");
}

/*
        Interfaces authorized by a pass are asked to bind (drivers_probe)
        once the usb lock is let go: a driver's probe of a device that stalls
        waits out its own timeouts, and the next verb is not held behind it.
*/
static p8 usb_probes[USB_INTERFACES_MOST][USB_PORT_ROOM];
static positive usb_probe_count;

static fn usb_probe_flush(void)
{
        for (positive at = 0; at < usb_probe_count; at++)
                (void)tune_write(USB_PROBE, (string_address)usb_probes[at]);
        usb_probe_count = 0;
}

/* A device's authorized set, unless the device at its port is no longer the
   one the census read: one plugged in since at the same port (or the same
   one enumerated again) has another devnum, and the next pass judges it. */
static bipolar usb_set_device(usb_device address_to device, string_address word)
{
        p8 now[sizeof(device->devnum)];

        usb_read((string_address)device->port, "devnum", now, sizeof(now));
        if (!string_equals((string_address)now, (string_address)device->devnum))
                return -ESTALE;
        return usb_set((string_address)device->port, "authorized", word);
}

static bool usb_census_visit(string_address directory, string_address name, address_any opaque)
{
        usb_census address_to census = (usb_census address_to)opaque;
        positive length = string_length(name);
        p8 word[USB_SERIAL_ROOM];

        (void)directory;
        if (length >= USB_PORT_ROOM ||
            string_span_of_set(name, "0123456789abcdefghijklmnopqrstuvwxyz.:-") != length)
                return true;

        if (string_first_of(name, ':'))
        {
                usb_interface address_to one;
                p32 class;
                p32 subclass;
                p8 path[HOST_PATH_ROOM];

                if (census->interfaces == USB_INTERFACES_MOST)
                {
                        census->dropped++;
                        return true;
                }
                one = census->interface + census->interfaces++;
                memory_zero(one, sizeof(*one));
                string_copy(one->name, name);
                usb_read(name, "bInterfaceClass", word, sizeof(word));
                class = radio_hex(word);
                usb_read(name, "bInterfaceSubClass", word, sizeof(word));
                subclass = radio_hex(word);
                usb_read(name, "bInterfaceProtocol", word, sizeof(word));
                one->kind = usb_kind(class, subclass, radio_hex(word));
                one->authorized = usb_says(name, "authorized", "1");
                one->bound = string_join(path, sizeof(path), USB_DEVICES "/", name, "/driver") &&
                             system_access_at(AT_FDCWD, (string_address)path, 0) >= 0;
                return true;
        }

        usb_device address_to device;
        p8 vendor[8];
        p8 product[8];
        bipolar got;

        if (census->count == USB_MOST)
        {
                census->dropped++;
                return true;
        }
        if (!host_starts(name, "usb") && !usb_port_valid(name))
                return true;
        //      A device that went while it was read is not there.
        if (usb_read(name, "idVendor", vendor, sizeof(vendor)) != 4 ||
            usb_read(name, "idProduct", product, sizeof(product)) != 4 ||
            string_span_of_set((string_address)vendor, "0123456789abcdef") != 4 ||
            string_span_of_set((string_address)product, "0123456789abcdef") != 4)
                return true;
        device = census->device + census->count++;
        memory_zero(device, sizeof(*device));
        string_copy(device->port, name);
        (void)string_join(device->id, sizeof(device->id), (string_address)vendor, ":",
                          (string_address)product);
        device->root = host_starts(name, "usb");
        (void)usb_read(name, "devnum", device->devnum, sizeof(device->devnum));
        got = usb_read(name, "serial", word, sizeof(word));
        usb_key(device->key, word, got);
        //      A root hub's serial is its controller's bus address, the kernel's to say: the start of a place.
        if (device->root && got > 0 && string_span_of_set((string_address)word, USB_CONTROLLER_SET) == (positive)got &&
            !string_join(device->place, sizeof(device->place), (string_address)word, "/", (string_address)product))
                device->place[0] = end;
        got = usb_read(name, "product", device->product, sizeof(device->product));
        device->product_length = got > 0 ? (positive)got : 0;
        device->says_fixed = usb_says(name, "removable", "fixed");
        device->authorized = usb_says(name, "authorized", "1");
        return true;
}

static usb_device address_to usb_find(usb_census address_to census, string_address port)
{
        for (positive at = 0; at < census->count; at++)
                if (string_equals((string_address)census->device[at].port, port))
                        return census->device + at;
        return null;
}

// Whether an interface is one of a device's: its name is the device's and a colon.
static bool usb_owns(usb_device address_to device, usb_interface address_to one)
{
        positive length = string_length((string_address)device->port);

        return !string_compare_max((string_address)one->name, (string_address)device->port, length) &&
               one->name[length] == ':';
}

// Whether a device shows any interface: whether it is configured.
static bool usb_shows(usb_census address_to census, usb_device address_to device)
{
        for (positive at = 0; at < census->interfaces; at++)
                if (usb_owns(device, census->interface + at))
                        return true;
        return false;
}

/*
        Every device and interface the kernel shows, in the order of their
        ports. Fixed is what a device says of the port it is on, and a hub's
        descriptor says it of its own ports: a hub somebody plugs in can call
        every port it has fixed. So a device is fixed only when it says so
        and so does every hub between it and the root hub.
*/
static b32 usb_device_order(address_any left, address_any right)
{
        return string_compare((string_address)((usb_device address_to)left)->port,
                              (string_address)((usb_device address_to)right)->port);
}

static b32 usb_interface_order(address_any left, address_any right)
{
        return string_compare((string_address)((usb_interface address_to)left)->name,
                              (string_address)((usb_interface address_to)right)->name);
}

static fn usb_census_take(usb_census address_to census)
{
        census->count = 0;
        census->interfaces = 0;
        census->dropped = 0;
        host_each_entry(USB_DEVICES, usb_census_visit, census);

        //      Names are unique, so the order is the one any sort gives.
        qsort(census->device, census->count, sizeof(census->device[0]), usb_device_order);
        qsort(census->interface, census->interfaces, sizeof(census->interface[0]), usb_interface_order);

        for (positive at = 0; at < census->count; at++)
        {
                usb_device address_to device = census->device + at;
                p8 up[USB_PORT_ROOM];

                device->fixed = !device->root && device->says_fixed;
                string_copy(up, device->port);
                while (device->fixed)
                {
                        p8 address_to dot = memory_last_of(up, '.', string_length((string_address)up));
                        usb_device address_to above;

                        //      No dot: what is above it is the root hub.
                        if (!dot)
                                break;
                        *dot = end;
                        above = usb_find(census, (string_address)up);
                        device->fixed = above && above->says_fixed;
                }
        }

        //      A device's place is its root hub's and the path after the bus number (usb_place_valid).
        for (positive at = 0; at < census->count; at++)
        {
                usb_device address_to device = census->device + at;
                positive bus = string_span_of_set((string_address)device->port, "0123456789");
                p8 hub[USB_PORT_ROOM];
                usb_device address_to root;

                if (device->root || !string_join(hub, sizeof(hub), "usb", (string_address)device->port))
                        continue;
                hub[3 + bus] = end;
                root = usb_find(census, (string_address)hub);
                if (root && root->place[0] &&
                    !string_join(device->place, sizeof(device->place), (string_address)root->place, "/",
                                 (string_address)device->port + bus + 1))
                        device->place[0] = end;
        }
}

/* Whether a rule names a device: its id, and its serial wherever it is; with
   no serial (-), the place it is pinned to. A pin written before places
   (usb_port_valid's 1-4) is read as it was, by the port its bus number
   gives -- and as it was, it names a device whose serial is too long to
   keep as well, which was spelled - then -- until usb lock or usb allow
   writes it again by place or by the serial's hash. */
static bool usb_rule_names(usb_rule address_to rule, usb_device address_to device)
{
        bool none = string_equals((string_address)device->key, "-");

        if (!string_equals((string_address)rule->id, (string_address)device->id))
                return false;
        if (!string_equals((string_address)rule->key, "-"))
                return string_equals((string_address)rule->key, (string_address)device->key);
        if (usb_port_valid((string_address)rule->port))
                return (none || string_has_prefix(device->key, "-#")) &&
                       string_equals((string_address)rule->port, (string_address)device->port);
        return none && device->place[0] && string_equals((string_address)rule->port, (string_address)device->place);
}

static usb_rule address_to usb_rule_for(usb_policy address_to policy, usb_device address_to device)
{
        for (positive at = 0; at < policy->count; at++)
                if (usb_rule_names(policy->rule + at, device))
                        return policy->rule + at;
        return null;
}

/*
        Why a device is let in or held, which is the whole policy. A deny rule
        holds it in either mode, a fixed one included: the owner said so of
        that device, and fixed only keeps the lock's default off what is
        built into the machine. Off lets in the rest. Locked, a fixed device
        or one an allow rule names is let in (usb_interface_let says which of
        its interfaces), and one no rule names is let in only when some class
        is allowed, with only the interfaces of those classes; with every
        class denied it is held, configured never.
*/
enum
{
        USB_WHY_OPEN,
        USB_WHY_FIXED,
        USB_WHY_RULE,
        USB_WHY_DENY,
        USB_WHY_CLASS,
        USB_WHY_NONE
};

static const string_address usb_why[] = {"open", "fixed", "rule", "deny rule", "class", "no rule"};

#define usb_let(why) ((why) != USB_WHY_DENY && (why) != USB_WHY_NONE)

static p8 usb_judge(usb_policy address_to policy, usb_device address_to device)
{
        usb_rule address_to rule = usb_rule_for(policy, device);

        if (rule && rule->kind == USB_DENY)
                return USB_WHY_DENY;
        if (!policy->lock)
                return USB_WHY_OPEN;
        if (device->fixed)
                return USB_WHY_FIXED;
        if (rule)
                return USB_WHY_RULE;
        return policy->allowed ? USB_WHY_CLASS : USB_WHY_NONE;
}

/* An interface of a device let in: all of them, open or fixed; those of the
   classes allowed, for one no rule names; and for one a rule names, those of
   the classes it had when it was allowed (all, for a rule that says none)
   and of the classes allowed besides. A device a rule names that shows a
   class it never had -- a stick that comes back as a keyboard too -- has
   that interface held. */
static bool usb_interface_let(usb_policy address_to policy, usb_device address_to device, p8 why,
                              usb_interface address_to one)
{
        usb_rule address_to rule = why == USB_WHY_RULE ? usb_rule_for(policy, device) : null;
        bool class = (policy->allowed >> one->kind) & 1;

        if (why == USB_WHY_CLASS)
                return class;
        if (rule && rule->kinds)
                return class || ((rule->kinds >> one->kind) & 1);
        return usb_let(why);
}

/* What a device says of itself, safe to print as radio_display makes a
   network's name safe, with a quote and a backslash spelled as well so a
   quoted field ends where it seems to; no more than USB_SHOWN of its bytes,
   and ... after one that was cut. */
static fn usb_shown(p8 address_to into, positive room, p8 address_to text, positive length)
{
        positive used = 0;
        positive at = 0;
        bool cut = length > USB_SHOWN;

        if (cut)
                length = USB_SHOWN;
        into[0] = end;
        while (at < length && used + 8 < room)
        {
                positive span = at + memory_escape_index(text + at, length - at, HEX_QUOTE | HEX_SLASH);

                radio_display(into + used, room - used - 5, text + at, span - at);
                used += string_length((string_address)into + used);
                if (span < length)
                {
                        used += radio_spell(into + used, text[span]);
                        into[used] = end;
                        span++;
                }
                at = span;
        }
        if (cut)
                string_append_bounded(into, "...", room);
}

/*
        A line of the log: when, in UTC, and what was decided. The log is
        root's (0600, beside the policy), is only ever added to, and is cut
        to its newer half once it passes USB_LOG_MOST. Whoever writes holds
        the usb lock, so nothing adds to the log while it is cut. What a
        device says of itself is in it only as usb_key and usb_shown spell
        it.
*/
static fn usb_log_line(string_address what)
{
        static p8 text[USB_LOG_MOST / 2];
        p8 line[USB_LOG_LINE + 32];
        p8 when[32];
        time_t stamp = time(null);
        tm broken;
        bipolar handle;
        bipolar size;
        bipolar got;
        positive from;

        gmtime_r(address_of stamp, address_of broken);
        strftime(when, sizeof(when), "%Y-%m-%dT%H:%M:%SZ ", address_of broken);
        (void)string_join(line, sizeof(line) - 1, (string_address)when, what);
        string_append_bounded(line, "\n", sizeof(line));

        handle = host_open_state(AT_FDCWD, USB_LOG, 0600, FILE_APPEND);
        if (handle < 0)
                return;
        (void)system_write_all_checked((positive)handle, (const address_any)line,
                                       string_length((string_address)line));
        size = system_seek(handle, 0, 2);
        system_close(handle);
        if (size <= USB_LOG_MOST)
                return;

        //      The newer half, read from the end, from the first whole line in
        //      it: a log another hand grew past the room keeps its newest lines.
        handle = system_open_at(AT_FDCWD, USB_LOG, FILE_READ | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
        if (handle < 0)
                return;
        got = file_transfer_exact(syscall(pread64), handle, text, sizeof(text), (p64)size - sizeof(text));
        system_close(handle);
        if (got <= 0)
                return;
        from = memory_span_without_byte(text, '\n', (positive)got);
        from += from < (positive)got;
        (void)host_write_file(USB_LOG, text + from, (positive)got - from, 0600, true);
}

/* A decision about a device, to the log, and to the terminal of whoever
   asked for it. */
static fn usb_tell(bool say, string_address what, usb_device address_to device, p8 why)
{
        p8 line[USB_LOG_LINE];
        p8 shown[USB_SHOWN * 4 + 8];

        usb_shown(shown, sizeof(shown), device->product, device->product_length);
        (void)string_join(line, sizeof(line), what, " ", (string_address)device->port, " ",
                          (string_address)device->id, "/", (string_address)device->key, " (",
                          usb_why[why], ") \"", (string_address)shown, "\"");
        usb_log_line((string_address)line);
        if (say)
                host_say(log, host_label "usb %s\n", line);
}

static fn usb_tell_interface(bool say, string_address what, usb_interface address_to one, p8 why, bool let)
{
        p8 line[USB_LOG_LINE];
        bool class = why == USB_WHY_CLASS;

        (void)string_join(line, sizeof(line), "interface ", (string_address)one->name, " ",
                          usb_kinds[one->kind], " ", what, " (",
                          class            ? (string_address) "class "
                          : why == USB_WHY_RULE && !let ? (string_address) "not a class of its rule"
                                                         : usb_why[why],
                          class ? usb_kinds[one->kind] : (string_address) "", ")");
        usb_log_line((string_address)line);
        if (say)
                host_say(log, host_label "usb %s\n", line);
}

static fn usb_tell_text(bool say, string_address first, string_address second)
{
        p8 line[USB_LOG_LINE];

        (void)string_join(line, sizeof(line), first, second);
        usb_log_line((string_address)line);
        if (say)
                host_say(log, host_label "usb %s\n", line);
}

/*
        What the root hubs and usbcore take for what arrives. Locked, the
        interfaces of a device that is configured wait to be judged, and with
        every class denied a new device is not configured at all; off is the
        kernel's own. Whichever of a hub's two becomes 0 is written first,
        so no moment between the writes binds a driver that neither the old
        policy nor the new would have. usbcore's is what a controller that
        arrives later starts with: 0 while locked, so its devices wait until
        the watcher has seen it and set its own.
*/
/*
        A default the kernel would not take leaves a root hub configuring new
        devices while the mode is lock. It is said in the log when that
        starts and when it ends (USB_DEFAULTS_PATH says it is failing, from
        one process to the next), and the watcher walks every quarter second
        until a pass sets it (usb_defaults_failing), judging each new device
        as it comes, where it waited ten seconds for a uevent.
*/
#define USB_DEFAULTS_PATH HOST_STATE "/usb.defaults"

static bool usb_defaults_failing;

static fn usb_defaults(usb_policy address_to policy, usb_census address_to census)
{
        string_address device = policy->lock && !policy->allowed ? "0" : "1";
        string_address interface = policy->lock ? "0" : "1";
        bool held = true;
        p8 have[8];

        for (positive at = 0; at < census->count; at++)
        {
                string_address hub = (string_address)census->device[at].port;

                if (!census->device[at].root)
                        continue;
                if (device[0] == '0')
                        held &= usb_set(hub, "authorized_default", device) >= 0;
                held &= usb_set(hub, "interface_authorized_default", interface) >= 0;
                held &= usb_set(hub, "authorized_default", device) >= 0;
        }
        if (host_read_kernel(USB_MODULE_DEFAULT, have, sizeof(have)) >= 0 &&
            !string_equals((string_address)have, policy->lock ? "0" : "1"))
                held &= tune_write(USB_MODULE_DEFAULT, policy->lock ? "0" : "1") >= 0;
        usb_defaults_failing = system_access_at(AT_FDCWD, USB_DEFAULTS_PATH, 0) >= 0;
        if (held == usb_defaults_failing)
        {
                if (held)
                        (void)system_remove_at(AT_FDCWD, USB_DEFAULTS_PATH, 0);
                else
                        (void)host_write_text(USB_DEFAULTS_PATH, "1\n");
                usb_tell_text(false, held ? "the defaults hold again" : "a default could not be set: a new device may be "
                                                                         "configured until a pass sets it", "");
        }
        usb_defaults_failing = !held;
}

/* The device of the last pass at the same port with the same identity. */
static usb_device address_to usb_before(usb_census address_to before, usb_device address_to device)
{
        usb_device address_to was = before ? usb_find(before, (string_address)device->port) : null;

        return was && string_equals((string_address)was->id, (string_address)device->id) &&
                               string_equals((string_address)was->key, (string_address)device->key)
                   ? was
                   : null;
}

/*
        Each interface of a device that is let in, as the policy says: every
        one wanted is authorized before any is probed, since a driver such as
        cdc_ether binds two interfaces at once. An interface authorized binds
        nothing by itself: the kernel probes it when asked through
        drivers_probe, which usb_probe_flush asks once the usb lock is let
        go. One held that the last pass (before) did not have is logged
        once. A device enumerated again since the census (another devnum)
        is left to the next pass.
*/
static fn usb_interfaces(usb_policy address_to policy, usb_census address_to census,
                         usb_device address_to device, p8 why, usb_census address_to before, bool say)
{
        p8 now[sizeof(device->devnum)];

        //      Not the device the census read: the next pass judges what is there.
        usb_read((string_address)device->port, "devnum", now, sizeof(now));
        if (!string_equals((string_address)now, (string_address)device->devnum))
                return;
        for (positive at = 0; at < census->interfaces; at++)
        {
                usb_interface address_to one = census->interface + at;
                bool let;
                bool news = before != null;

                if (!usb_owns(device, one))
                        continue;
                let = usb_interface_let(policy, device, why, one);
                for (positive was = 0; news && was < before->interfaces; was++)
                        news = !string_equals((string_address)before->interface[was].name, (string_address)one->name);
                if (news && !let && !one->authorized)
                        usb_tell_interface(false, "held", one, why, let);
                if (let == one->authorized ||
                    usb_set((string_address)one->name, "authorized", let ? "1" : "0") < 0)
                        continue;
                one->authorized = let;
                one->fresh = let && !one->bound;
                if (one->fresh && usb_probe_count < USB_INTERFACES_MOST)
                        string_copy(usb_probes[usb_probe_count++], (string_address)one->name);
                usb_tell_interface(say, let ? "allowed" : "held", one, why, let);
        }
}

/*
        One pass of the policy over a census: the defaults, then every device
        as usb_judge says and the interfaces of each one let in. before is the
        last pass's census, whose devices are not news (null: none are). The
        answer is how many devices this pass let in, whose interfaces the
        kernel has just made and the next pass judges.
*/
static positive usb_apply(usb_policy address_to policy, usb_census address_to census,
                          usb_census address_to before, bool say)
{
        positive opened = 0;

        usb_defaults(policy, census);
        for (positive at = 0; at < census->count; at++)
        {
                usb_device address_to device = census->device + at;
                usb_device address_to was;
                p8 why;
                bool let;

                if (device->root)
                        continue;
                why = usb_judge(policy, device);
                let = usb_let(why);
                was = usb_before(before, device);
                if (before && !was)
                        usb_tell(false, device->authorized ? "seen allowed" : "seen held", device, why);
                if (let != device->authorized && usb_set_device(device, let ? "1" : "0") >= 0)
                {
                        usb_tell(say, let ? "allowed" : "held", device, why);
                        opened += let;
                        continue;
                }
                if (let && device->authorized)
                        usb_interfaces(policy, census, device, why, before, say);
        }
        return opened;
}

/* Passes until one lets nothing new in (three at most), each over a census
   of its own. before is what the first pass takes as known (null: nothing is
   news); after, when asked, is left holding the last census. */
static fn usb_apply_all(usb_policy address_to policy, usb_census address_to before,
                        usb_census address_to after, bool say)
{
        static usb_census census[2];
        positive pass = 0;
        usb_census address_to last = before;

        for (; pass < 3; pass++)
        {
                usb_census address_to now = census + (pass & 1);
                positive opened;

                usb_census_take(now);
                opened = usb_apply(policy, now, last, say);
                last = now;
                if (!opened)
                        break;
        }
        if (after && last != after)
                memory_copy(after, last, sizeof(*after));
}

static bool usb_policy_line(usb_policy address_to policy, string_address line)
{
        usb_rule rule;
        positive skip;

        if (string_equals(line, "mode lock") || string_equals(line, "mode off"))
        {
                policy->lock = line[5] == 'l';
                return true;
        }
        if (string_has_prefix(line, "class "))
        {
                string_address word = string_first_of(line + 6, ' ');
                bipolar kind = word ? usb_kind_word(line + 6, (positive)(word - line - 6)) : -1;

                if (kind < 0 || (!string_equals(word + 1, "allow") && !string_equals(word + 1, "deny")))
                        return false;
                if (word[1] == 'a')
                        policy->allowed |= (p8)(1u << kind);
                else
                        policy->allowed &= (p8) ~(1u << kind);
                return true;
        }
        rule.kind = string_has_prefix(line, "allow ") ? USB_ALLOW
                    : string_has_prefix(line, "deny ") ? USB_DENY
                                                       : 0;
        rule.kinds = 0;
        skip = rule.kind == USB_ALLOW ? 6 : 5;
        if (!rule.kind || policy->count == USB_RULES_MOST)
                return false;
        //      An allow rule's classes follow its id after a blank, which no
        //      id has: usb_key spells a blank in a serial.
        {
                string_address blank = string_first_of(line + skip, ' ');

                if (blank && (rule.kind != USB_ALLOW || !usb_kinds_parse(blank + 1, address_of rule.kinds)))
                        return false;
                if (blank)
                        *blank = end;
        }
        if (!usb_id_parse(line + skip, address_of rule) || !rule.key[0])
                return false;
        policy->rule[policy->count++] = rule;
        return true;
}

/* Which /root this is, as the kernel names it (its device and inode): a take
   binds the disk's over the session's, and a lock held in /run for one is
   not the other's. Empty when it cannot be said. */
static fn usb_root_mark(p8 address_to into, positive room)
{
        system_path_identity facts;
        p8 major[24];
        p8 minor[24];
        p8 inode[24];

        into[0] = end;
        if (system_path_identity_at(AT_FDCWD, "/root", 0, SYSTEM_PATH_STATX_IDENTITY, address_of facts) < 0)
                return;
        positive_into_string(major, facts.device_major);
        positive_into_string(minor, facts.device_minor);
        positive_into_string(inode, facts.inode);
        if (!string_join(into, room, (string_address)major, ":", (string_address)minor, ":", (string_address)inode))
                into[0] = end;
}

/* Whether a lock this /root could not keep is in force (USB_UNKEPT_PATH
   names this /root on its first line), and why, from its second. */
static bool usb_unkept_why(p8 address_to why, positive room)
{
        p8 text[256];
        p8 mark[64];
        bipolar got = host_read_state(USB_UNKEPT_PATH, text, sizeof(text));
        positive line;
        positive rest;

        why[0] = end;
        if (got <= 0)
                return false;
        usb_root_mark(mark, sizeof(mark));
        line = memory_span_without_byte(text, '\n', (positive)got);
        if (!mark[0] || line != string_length((string_address)mark) || memory_compare(text, mark, line))
                return false;
        line += line < (positive)got;
        rest = memory_span_without_byte(text + line, '\n', (positive)got - line);
        string_copy_bounded(why, (string_address)text + line, rest < room ? rest + 1 : room);
        return true;
}

/* The policy as /root/usb.policy keeps it. Not there is off with no rules;
   not readable (not root) is the same, and known false says so. A line that
   is no line of the policy is left out and counted. Where a lock this /root
   could not keep is in force (usb_policy_unkept), what is read is that
   lock, in /run, and unkept says so. */
static fn usb_policy_load(usb_policy address_to policy)
{
        static p8 text[USB_POLICY_ROOM];
        bipolar got = host_read_state(USB_POLICY, text, sizeof(text));
        positive at = 0;
        positive start;
        positive length;
        p8 why[8];

        memory_zero(policy, sizeof(*policy));
        policy->error = got < 0 ? got : 0;
        policy->known = got >= 0 || got == -ENOENT;
        if (got < 0 && usb_unkept_why(why, sizeof(why)))
        {
                got = host_read_state(USB_RUN_POLICY, text, sizeof(text));
                policy->unkept = got >= 0;
                policy->known |= policy->unkept;
        }
        policy->kept = got >= 0;
        policy->cut = got >= (bipolar)sizeof(text) - 1;
        while (got > 0 && host_line_next(text, (positive)got, address_of at, address_of start,
                                         address_of length))
        {
                p8 line[USB_RULE_ROOM];

                //      A file cut short ends in a line cut short, which could
                //      be a serial cut short: it is not read as a rule.
                if (!length || (policy->cut && start + length >= (positive)got))
                        continue;
                if (length >= sizeof(line))
                {
                        policy->skipped++;
                        continue;
                }
                memory_copy(line, text + start, length);
                line[length] = end;
                policy->skipped += !usb_policy_line(policy, (string_address)line);
        }
}

// The policy as its file keeps it, into text; how long it is.
static positive usb_policy_text(usb_policy address_to policy, p8 address_to text, positive room)
{
        string_copy_bounded(text, policy->lock ? "mode lock\n" : "mode off\n", room);
        for (positive kind = 0; kind < USB_KINDS; kind++)
        {
                string_append_bounded(text, "class ", room);
                string_append_bounded(text, usb_kinds[kind], room);
                string_append_bounded(text, (policy->allowed >> kind) & 1 ? " allow\n" : " deny\n", room);
        }
        for (positive at = 0; at < policy->count; at++)
        {
                p8 rule[USB_RULE_ROOM];

                usb_rule_text(rule, sizeof(rule), policy->rule + at);
                string_append_bounded(text, policy->rule[at].kind == USB_ALLOW ? "allow " : "deny ", room);
                string_append_bounded(text, (string_address)rule, room);
                string_append_bounded(text, "\n", room);
        }
        return string_length((string_address)text);
}

/* The policy over /root/usb.policy, synced; a lock held in /run for want of
   it is let go once it is kept. */
static bipolar usb_policy_write(usb_policy address_to policy)
{
        static p8 text[USB_POLICY_ROOM];
        bipolar failed = host_write_file(USB_POLICY, text, usb_policy_text(policy, text, sizeof(text)), 0600, true);

        if (failed >= 0)
        {
                (void)system_remove_at(AT_FDCWD, USB_UNKEPT_PATH, 0);
                (void)system_remove_at(AT_FDCWD, USB_RUN_POLICY, 0);
        }
        return failed;
}

static b32 usb_policy_save(usb_policy address_to policy)
{
        bipolar failed = usb_policy_write(policy);

        return failed < 0 ? host_fail(USB_POLICY, failed) : 0;
}

/*
        A lock put in force that /root/usb.policy does not keep -- it could
        not be written (a disk full or read only), or what is there cannot be
        read and is not written over at boot: the policy in /run, which the
        watcher and every verb read in its place until the machine restarts
        or a verb keeps one, and why, beside which /root it is for.
*/
static fn usb_policy_unkept(usb_policy address_to policy, string_address what, bipolar error)
{
        static p8 text[USB_POLICY_ROOM];
        p8 note[256];
        p8 mark[64];

        usb_root_mark(mark, sizeof(mark));
        (void)host_write_file(USB_RUN_POLICY, text, usb_policy_text(policy, text, sizeof(text)), 0600, false);
        (void)string_join(note, sizeof(note), (string_address)mark, "\n" USB_POLICY " ", what, ": ",
                          file_reason(error), "\n");
        (void)host_write_file(USB_UNKEPT_PATH, note, string_length((string_address)note), 0600, false);
}

// Whether the policy holds anything, which is what the watcher is for.
static bool usb_watched(usb_policy address_to policy)
{
        bool deny = false;

        for (positive at = 0; at < policy->count; at++)
                deny |= policy->rule[at].kind == USB_DENY;
        return policy->kept && (policy->lock || deny);
}

/* A rule naming a device of a census. An allow rule keeps the classes of its
   interfaces, those authorized only when every is asked for false; one not
   configured shows none, and its rule lets in whatever it is. */
static fn usb_rule_from(usb_rule address_to rule, p8 kind, usb_device address_to device,
                        usb_census address_to census, bool every)
{
        rule->kind = kind;
        rule->kinds = 0;
        for (positive at = 0; kind == USB_ALLOW && at < census->interfaces; at++)
                if (usb_owns(device, census->interface + at) && (every || census->interface[at].authorized))
                        rule->kinds |= (p8)(1u << census->interface[at].kind);
        string_copy(rule->id, (string_address)device->id);
        string_copy(rule->key, (string_address)device->key);
        string_copy(rule->port, !string_equals((string_address)device->key, "-") ? (string_address) ""
                                : device->place[0]                              ? (string_address)device->place
                                                                                : (string_address)device->port);
}

static bool usb_rule_same(usb_rule address_to one, usb_rule address_to other)
{
        return string_equals((string_address)one->id, (string_address)other->id) &&
               string_equals((string_address)one->key, (string_address)other->key) &&
               string_equals((string_address)one->port, (string_address)other->port);
}

/* The rules of the policy but those that name the same device as rule, and
   any that names device (plugged in now, or null): a pin written before
   places goes with the rule that replaces it. How many went. */
static positive usb_rules_drop(usb_policy address_to policy, usb_rule address_to rule, usb_device address_to device)
{
        positive kept = 0;
        positive count = policy->count;

        for (positive at = 0; at < count; at++)
                if (!usb_rule_same(policy->rule + at, rule) && !(device && usb_rule_names(policy->rule + at, device)))
                        policy->rule[kept++] = policy->rule[at];
        policy->count = kept;
        return count - kept;
}

/*
        The watcher: a process of its own for as long as the policy holds
        something. A uevent is only a wake-up and is never read: any of them,
        or a socket that overflowed, is another walk of sysfs, one walk for a
        burst (a device and its interfaces are several), and one every ten
        seconds whatever is heard. Where no uevent socket can be had it walks
        every quarter of a second, and so does it while a default will not
        hold (soon). Kernel-level holding (the defaults above) is what makes
        its latency safe: a new device is held by the kernel until a pass
        lets it in. The datagrams of a burst are dropped up to a count and a
        deadline: every kernel uevent wakes it (a trusted keyboard's battery
        reports among them), and what is left of a flood wakes the next wait
        at once, one pass for each.
*/
#define USB_DRAIN_MOST 512
#define USB_DRAIN_NS ((p64)100000000)

static fn usb_wait(bipolar events, bool soon)
{
        timespec span = {soon ? 0 : USB_REWALK_NS / 1000000000, soon ? USB_POLL_NS : 0};
        p64 until;

        if (events < 0)
        {
                host_pause(USB_POLL_NS);
                return;
        }
        if (descriptor_wait_readable(events, address_of span, null) <= 0)
                return;
        host_pause(USB_SETTLE_NS);
        until = system_clock_ns(HOST_CLOCK_BOOTTIME) + USB_DRAIN_NS;
        for (positive drained = 0; drained < USB_DRAIN_MOST; drained++)
                if (socket_receive((b32)events, null, 0, MSG_DONTWAIT, 0, 0) < 0 ||
                    system_clock_ns(HOST_CLOCK_BOOTTIME) >= until)
                        break;
}

static b32 usb_watch_run(void)
{
        static usb_census before;
        bipolar held = host_lock_file(USB_WATCHER_PATH, HOST_FILE_SETLK, null);
        bipolar events;
        bool first = true;
        bool waiting = false;

        if (held < 0)
                return 1;
        events = netlink_open_protocol(USB_UEVENT, 1);
        //      No USB here, or the sysfs it was walking is gone: nothing to hold.
        while (system_access_at(AT_FDCWD, USB_DEVICES, 0) >= 0)
        {
                usb_policy policy;
                bipolar lock = usb_lock_take(false);

                //      A verb that has held the lock this long, or no lock to be had: the pass is
                //      asked again soon, and the log says so once (without the lock, as it cannot have it).
                if (lock < 0)
                {
                        if (!waiting)
                                usb_tell_text(false, "the watcher waits for the usb lock: ", file_reason(lock));
                        waiting = true;
                        usb_wait(events, true);
                        continue;
                }
                waiting = false;
                usb_policy_load(address_of policy);
                if (!usb_watched(address_of policy))
                {
                        radio_unlock(lock);
                        break;
                }
                usb_apply_all(address_of policy, first ? null : address_of before, address_of before, false);
                radio_unlock(lock);
                usb_probe_flush();
                first = false;
                usb_wait(events, usb_defaults_failing);
        }
        if (events >= 0)
                socket_close((b32)events);
        system_close(held);
        return 0;
}

static fn usb_watch_start(void)
{
        if (host_lock_owner(USB_WATCHER_PATH))
                return;
        if (!host_detach(descriptors_none, descriptors_none))
                return;
        (void)usb_watch_run();
        system_call_1(syscall(exit_group), 0);
}

/* The watcher ended, waited for as long as the usb lock is: one in a write
   to a device that stalls ends when the kernel gives up on it. */
static fn usb_watch_stop(void)
{
        p64 start = system_clock_ns(HOST_CLOCK_BOOTTIME);

        if (!host_lock_signal(USB_WATCHER_PATH, SIGTERM))
                return;
        while (host_lock_owner(USB_WATCHER_PATH) && system_clock_ns(HOST_CLOCK_BOOTTIME) - start < USB_LOCK_WAIT_NS)
                host_pause(USB_LOCK_POLL_NS);
}

/*
        Lock, with the usb lock held: every device plugged in now that no rule
        names becomes an allow rule, every class is denied, and the policy is
        put in force. Locking so cuts off nothing in use -- the keyboard this
        was typed on, the mouse, the stick the data is on -- and that is the
        one net against a machine left with no way in: the lock never holds a
        device present when it runs, so a machine whose only keyboard is USB
        keeps it, whatever /proc/bus/input/devices says of others. usb_apply
        has no such net on purpose. A machine whose keyboard is gone (a kiosk
        with none, a keyboard swapped since) would let in the first keyboard
        plugged in after, which is the device this is for. A device a deny
        rule holds keeps its rule, and a fixed one needs none. Each rule keeps
        the classes of the interfaces that work now. A device the kernel
        holds unconfigured (usbcore.authorized_default=2 holds every one at
        boot) shows none, and a rule written then let in whatever it came
        back as -- a keyboard's rule let the same identity bring up storage
        and a network: usb_configure_held configures it first, its
        interfaces waiting, and its rule keeps the classes they say, as
        usb_rule_verb's allow does (a device none of whose interfaces work
        keeps the classes it has). One that cannot be configured, or shows
        no interface once it is, is not kept: held, it is let in by usb
        allow PORT. Locked already, the lock keeps only what the policy lets
        work: a device it holds stays held. A lock carried over another
        /root's, which that /root's policy put in force this boot (the
        session's, before the disk was taken), is the same: what that lock
        holds was judged, and the first lock of this /root does not let it in.

        A lock that cannot be kept is put in force all the same: a boot's
        first lock that could not write /root/usb.policy (a full or read-only
        disk), or that found something there it cannot read and does not
        write over, used to leave the machine as the kernel had it -- on a
        sec_host_locked image every USB device held, the keyboard too, and
        nothing said -- and usb lock refused with the held devices it had
        configured left half done. Now the lock is held in /run
        (usb_policy_unkept), in force until the machine restarts or a verb
        keeps a policy, the log and usb say why, and usb lock answers 1
        with the reason.
*/
enum
{
        USB_LOCK_ASKED,
        USB_LOCK_FIRST,
        USB_LOCK_CARRIED
};

/*
        A boot's first lock trusts what is plugged in once USB has settled,
        and a device that keeps plugging and unplugging keeps it from
        settling until the cap, fifteen seconds of uptime, when the lock was
        taken over whatever was there at that instant, the device doing the
        plugging included. So the settle keeps when each device (its port and
        identity) was first seen, and at the lock a device younger than the
        quiet time (USB_STILL_NS: first seen in the last two seconds of the
        settle, or since, and seen to arrive: what the settle's first census
        found is not young) is given no rule and is held: a lock taken because
        everything was quiet for two seconds has no such device, and one
        taken at the cap does not let in what was still coming and going.
        What it costs: a device the owner plugs in during those last two
        seconds, or one that resets itself as the cap falls, is held and
        waits for usb allow PORT, and on a machine whose command line holds
        every device a keyboard that was only then enumerating has to be
        let in from another one or by ssh. It does not stop a second device,
        plugged in early and left still while another keeps the settle
        going: that one is trusted, as whatever is plugged in at the first
        boot is.
*/
typedef struct
{
        p8 port[USB_PORT_ROOM];
        p8 id[10];
        p8 key[USB_KEY_ROOM];
        p64 since;
} usb_age;

typedef struct
{
        usb_age age[USB_MOST];
        positive count;
        p64 cutoff; // first seen after this is young
} usb_settle;

static usb_age address_to usb_age_of(usb_settle address_to settle, usb_device address_to device)
{
        for (positive at = 0; at < settle->count; at++)
                if (string_equals((string_address)settle->age[at].port, (string_address)device->port) &&
                    string_equals((string_address)settle->age[at].id, (string_address)device->id) &&
                    string_equals((string_address)settle->age[at].key, (string_address)device->key))
                        return settle->age + at;
        return null;
}

static bool usb_young(usb_settle address_to settle, usb_device address_to device)
{
        usb_age address_to age = settle ? usb_age_of(settle, device) : null;

        return settle && (!age || age->since > settle->cutoff);
}

static positive usb_configure_held(usb_policy address_to policy, usb_census address_to census,
                                   usb_settle address_to settle)
{
        positive count = 0;

        for (positive at = 0; at < census->count; at++)
        {
                usb_device address_to device = census->device + at;

                if (device->root || device->fixed || device->authorized || usb_rule_for(policy, device) ||
                    usb_young(settle, device))
                        continue;
                //      Every root hub's new interfaces wait before the first is
                //      configured, so nothing binds before the policy says.
                for (positive hub = 0; !count && hub < census->count; hub++)
                        if (census->device[hub].root)
                                (void)usb_set((string_address)census->device[hub].port,
                                              "interface_authorized_default", "0");
                count += usb_set_device(device, "1") >= 0;
        }
        return count;
}

static b32 usb_lock_held(bool say, p8 how, usb_settle address_to settle)
{
        static usb_census census;
        usb_policy policy;
        bool stay;
        bool write;
        positive kept = 0;
        bipolar error;

        usb_policy_load(address_of policy);
        if (policy.cut)
                return host_refuse("%s is too long to change here\n", USB_POLICY);
        stay = policy.lock || how == USB_LOCK_CARRIED;
        usb_census_take(address_of census);
        //      Refused before anything is configured: at most each device no rule names is a rule more.
        for (positive at = 0, more = 0; at < census.count; at++)
                if (!census.device[at].root && !census.device[at].fixed &&
                    !usb_rule_for(address_of policy, census.device + at) && policy.count + ++more > USB_RULES_MOST)
                        return host_refuse("more devices are plugged in than the %p rules a policy keeps; "
                                           "nothing was locked\n",
                                           (positive)USB_RULES_MOST);
        if (!stay && usb_configure_held(address_of policy, address_of census, settle))
                usb_census_take(address_of census);
        for (positive at = 0; at < census.count; at++)
        {
                usb_device address_to device = census.device + at;
                usb_rule address_to had = usb_rule_for(address_of policy, device);
                usb_rule rule;

                //      A pin written before places is written again by the place of the device it names,
                //      or by its serial's hash for a device whose serial is too long to keep.
                if (had && string_equals((string_address)had->key, "-") && usb_port_valid((string_address)had->port))
                {
                        if (!string_equals((string_address)device->key, "-"))
                        {
                                string_copy(had->key, (string_address)device->key);
                                had->port[0] = end;
                        }
                        else if (device->place[0])
                                string_copy(had->port, (string_address)device->place);
                }
                if (device->root || device->fixed || had)
                        continue;
                if (usb_young(settle, device))
                {
                        usb_tell(false, "not kept, it came while USB was settling:", device, USB_WHY_NONE);
                        continue;
                }
                usb_rule_from(address_of rule, USB_ALLOW, device, address_of census, false);
                if (!rule.kinds && !stay)
                        usb_rule_from(address_of rule, USB_ALLOW, device, address_of census, true);
                if (stay && (!device->authorized || (usb_shows(address_of census, device) && !rule.kinds)))
                        continue;
                if (!device->authorized || !rule.kinds)
                {
                        usb_tell(false, "not kept", device, USB_WHY_NONE);
                        continue;
                }
                //      Counted above, unless the census took a device more since.
                if (policy.count == USB_RULES_MOST)
                        break;
                policy.rule[policy.count++] = rule;
                kept++;
        }
        policy.lock = true;
        policy.allowed = 0;
        //      At boot what cannot be read is not written over: it may be the owner's, on a disk that is failing.
        write = how == USB_LOCK_ASKED || policy.known;
        error = write ? usb_policy_write(address_of policy) : policy.error;
        if (error < 0)
        {
                usb_policy_unkept(address_of policy, write ? "could not be written" : "cannot be read", error);
                usb_tell_text(false, "the lock is in force until the machine restarts, and not kept: ", file_reason(error));
        }
        for (positive at = policy.count - kept; at < policy.count; at++)
        {
                p8 rule[USB_RULE_ROOM];

                usb_rule_text(rule, sizeof(rule), policy.rule + at);
                usb_tell_text(false, "rule added: allow ", (string_address)rule);
        }
        usb_tell_text(false, "mode lock", "");
        usb_apply_all(address_of policy, null, null, say);
        if (error < 0)
                return host_refuse(USB_POLICY " could not be written (%s): the lock is in force until the machine "
                                   "restarts\n",
                                   file_reason(error));
        if (say)
                host_say(log, host_label "usb lock: %p devices plugged in now are kept as rules; "
                                         "a new one waits for usb allow\n",
                         kept);
        return 0;
}

static b32 usb_lock_now(void)
{
        bipolar lock = usb_lock_take(true);
        b32 failed;

        if (lock < 0)
                return usb_lock_refused(lock);
        failed = usb_lock_held(true, USB_LOCK_ASKED, null);
        radio_unlock(lock);
        usb_probe_flush();
        //      A lock in force but not kept is watched as well.
        usb_watch_start();
        return failed;
}

static b32 usb_unlock_now(void)
{
        bipolar lock = usb_lock_take(true);
        usb_policy policy;
        b32 failed;

        if (lock < 0)
                return usb_lock_refused(lock);
        usb_policy_load(address_of policy);
        if (policy.cut)
        {
                radio_unlock(lock);
                return host_refuse("%s is too long to change here\n", USB_POLICY);
        }
        policy.lock = false;
        failed = usb_policy_save(address_of policy);
        if (!failed)
        {
                usb_tell_text(false, "mode off", "");
                usb_apply_all(address_of policy, null, null, true);
        }
        radio_unlock(lock);
        usb_probe_flush();
        if (failed)
                return failed;
        if (!usb_watched(address_of policy))
                usb_watch_stop();
        host_say(log, host_label "usb unlock: every device is let in but those a deny rule holds\n");
        return 0;
}

/*
        The device a word names: a port, or an id. A VID:PID alone is the one
        device plugged in now that says it, and is refused when none or more
        than one does; a whole id may name a device that is not plugged in.
        rule is the id the policy keeps, device the device plugged in now that
        it names, or null.
*/
static b32 usb_resolve(string_address word, usb_census address_to census, usb_rule address_to rule,
                       usb_device address_to address_to device)
{
        positive count = 0;

        *device = null;
        usb_census_take(census);
        if (usb_port_valid(word))
        {
                *device = usb_find(census, word);
                return_if(!*device, host_refuse("no device is plugged in at %s\n", word));
                usb_rule_from(rule, 0, *device, census, true);
                return 0;
        }
        return_if(!usb_id_parse(word, rule),
                  host_refuse("%w is no port (like 1-4.1) and no device (VID:PID, or VID:PID/SERIAL as usb shows it)\n",
                              writer_terminal_name, word));
        for (positive at = 0; at < census->count; at++)
        {
                usb_device address_to one = census->device + at;

                if (one->root || !string_equals((string_address)one->id, (string_address)rule->id) ||
                    (rule->key[0] && !usb_rule_names(rule, one)))
                        continue;
                count++;
                *device = one;
        }
        if (rule->key[0])
                return 0;
        return_if(!count, host_refuse("no %s is plugged in; name it as VID:PID/SERIAL\n", rule->id));
        return_if(count > 1, host_refuse("%p devices plugged in are %s; name one by its port or its serial, "
                                         "as usb shows them\n",
                                         count, rule->id));
        usb_rule_from(rule, 0, *device, census, true);
        return 0;
}

/*
        allow and deny: a rule for the device the word names, in place of any
        rule it had, put in force at once and at every plug after. An allow
        rule for a device plugged in keeps the classes it shows now, every
        interface it has; one held and not configured is configured first,
        under the lock (the watcher would otherwise take it for a device no
        rule names and hold it again), so that its interfaces, which wait
        while the policy is locked, can say what it is. A whole id for a
        device that is not plugged in lets it in whole.
*/
static b32 usb_rule_verb(string_address word, p8 kind)
{
        static usb_census census;
        usb_rule rule;
        usb_device address_to device;
        usb_policy policy;
        p8 text[USB_RULE_ROOM];
        bipolar lock = usb_lock_take(true);
        b32 failed;

        if (lock < 0)
                return usb_lock_refused(lock);
        failed = usb_resolve(word, address_of census, address_of rule, address_of device);
        usb_policy_load(address_of policy);
        if (!failed && policy.cut)
                failed = host_refuse("%s is too long to change here\n", USB_POLICY);
        if (!failed && kind == USB_ALLOW && device && !device->authorized && usb_set_device(device, "1") >= 0)
        {
                p8 port[USB_PORT_ROOM];
                p8 devnum[sizeof(device->devnum)];

                usb_tell(true, "allowed", device, USB_WHY_RULE);
                string_copy(port, (string_address)device->port);
                string_copy(devnum, (string_address)device->devnum);
                usb_census_take(address_of census);
                device = usb_find(address_of census, (string_address)port);
                //      Its rule is written from what it shows now, so it has to be the same device.
                if (!device || !string_equals((string_address)device->devnum, (string_address)devnum))
                        failed = host_refuse("the device at %s went while it was let in; no rule was kept\n", port);
        }
        if (!failed && device)
                usb_rule_from(address_of rule, kind, device, address_of census, true);
        rule.kind = kind;
        if (!failed)
                (void)usb_rules_drop(address_of policy, address_of rule, device);
        if (!failed && policy.count == USB_RULES_MOST)
                failed = host_refuse("the policy keeps %p rules at most; usb forget one first\n",
                                     (positive)USB_RULES_MOST);
        if (!failed)
        {
                policy.rule[policy.count++] = rule;
                failed = usb_policy_save(address_of policy);
        }
        if (failed)
        {
                radio_unlock(lock);
                return failed;
        }
        usb_rule_text(text, sizeof(text), address_of rule);
        usb_tell_text(false, kind == USB_ALLOW ? "rule added: allow " : "rule added: deny ",
                      (string_address)text);
        usb_apply_all(address_of policy, null, null, true);
        radio_unlock(lock);
        usb_probe_flush();
        if (usb_watched(address_of policy))
                usb_watch_start();
        host_say(log, host_label "usb %s %s%s\n", kind == USB_ALLOW ? "allows" : "holds", text,
                 device ? ", now and at every plug" : "; it is not plugged in now");
        return 0;
}

static b32 usb_forget(string_address word)
{
        static usb_census census;
        usb_rule rule;
        usb_device address_to device = null;
        usb_policy policy;
        p8 text[USB_RULE_ROOM];
        bipolar lock;
        positive matched = 0;
        b32 failed;

        if (usb_port_valid(word))
        {
                failed = usb_resolve(word, address_of census, address_of rule, address_of device);
                if (failed)
                        return failed;
        }
        else
                return_if(!usb_id_parse(word, address_of rule),
                          host_refuse("%w is no port (like 1-4.1) and no device (VID:PID, or VID:PID/SERIAL "
                                      "as usb shows it)\n",
                                      writer_terminal_name, word));

        lock = usb_lock_take(true);
        if (lock < 0)
                return usb_lock_refused(lock);
        usb_policy_load(address_of policy);
        //      A VID:PID alone is the one rule for it.
        if (!rule.key[0])
                for (positive at = 0; at < policy.count; at++)
                        if (string_equals((string_address)policy.rule[at].id, (string_address)rule.id) &&
                            matched++ == 0)
                                rule = policy.rule[at];
        usb_rule_text(text, sizeof(text), address_of rule);
        if (policy.cut || matched > 1 || !rule.key[0] || !usb_rules_drop(address_of policy, address_of rule, device))
        {
                radio_unlock(lock);
                return policy.cut     ? host_refuse("%s is too long to change here\n", USB_POLICY)
                       : matched > 1 ? host_refuse("%p rules are for %s; name one as usb shows it\n", matched,
                                                    rule.id)
                                      : host_refuse("no rule is for %s\n", rule.key[0] ? text : rule.id);
        }
        failed = usb_policy_save(address_of policy);
        if (!failed)
        {
                usb_tell_text(false, "rule removed: ", (string_address)text);
                usb_apply_all(address_of policy, null, null, true);
        }
        radio_unlock(lock);
        usb_probe_flush();
        if (failed)
                return failed;
        if (!usb_watched(address_of policy))
                usb_watch_stop();
        host_say(log, host_label "usb forgot %s\n", text);
        return 0;
}

static fn usb_classes_say(usb_policy address_to policy)
{
        string_format(log, host_label "  classes:");
        for (positive kind = 0; kind < USB_KINDS; kind++)
                string_format(log, "%s %s %s", kind ? "," : "", usb_kinds[kind],
                              !policy->known                    ? "unknown"
                              : (policy->allowed >> kind) & 1 ? "allow"
                                                                : "deny");
        string_format(log, "\n");
}

static b32 usb_class(string_address address_to arguments, positive count)
{
        bipolar kind = count > 3 ? usb_kind_named(arguments[3]) : -1;
        usb_policy policy;
        bipolar lock;
        b32 failed;
        bool allow;

        if (count == 3 || (count == 4 && kind >= 0))
        {
                usb_policy_load(address_of policy);
                if (count == 3)
                        usb_classes_say(address_of policy);
                else
                        string_format(log, host_label "usb class %s %s\n", usb_kinds[kind],
                                      !policy.known                     ? "unknown"
                                      : (policy.allowed >> kind) & 1 ? "allow"
                                                                       : "deny");
                log_flush();
                return 0;
        }
        return_if(count != 5 || kind < 0 ||
                      (!string_equals(arguments[4], "allow") && !string_equals(arguments[4], "deny")),
                  host_usage());
        host_need_root();
        allow = arguments[4][0] == 'a';
        lock = usb_lock_take(true);
        if (lock < 0)
                return usb_lock_refused(lock);
        usb_policy_load(address_of policy);
        if (policy.cut)
        {
                radio_unlock(lock);
                return host_refuse("%s is too long to change here\n", USB_POLICY);
        }
        policy.allowed = allow ? policy.allowed | (p8)(1u << kind) : policy.allowed & (p8) ~(1u << kind);
        failed = usb_policy_save(address_of policy);
        if (!failed)
        {
                p8 line[48];

                (void)string_join(line, sizeof(line), usb_kinds[kind], allow ? " allow" : " deny");
                usb_tell_text(false, "class ", (string_address)line);
                usb_apply_all(address_of policy, null, null, true);
        }
        radio_unlock(lock);
        usb_probe_flush();
        if (failed)
                return failed;
        host_say(log, host_label "usb class %s %s%s\n", usb_kinds[kind], allow ? "allow" : "deny",
                 policy.lock ? "" : " (in force while usb is locked)");
        return 0;
}

static b32 usb_log_show(positive asked)
{
        static p8 text[USB_LOG_MOST + USB_LOG_LINE * 2];
        bipolar got = host_read_state(USB_LOG, text, sizeof(text));
        positive at;

        if (got == -ENOENT)
        {
                host_say(log, host_label "usb has decided nothing yet\n");
                return 0;
        }
        return_if(got < 0, host_fail(USB_LOG, got));
        //      The start of the asked-th line from the end: past the asked-th newline counted back, a
        //      newline ending the last byte not counted.
        {
                positive2 nth = got > 1 ? memory_nth_last_of(text, '\n', (positive)got - 1, asked) : (positive2){0, 0};

                at = nth.y == asked ? nth.x + 1 : 0;
        }
        //      The log holds only what usb_tell spelled; it is shown as a name is
        //      all the same, so a line written by anything else cannot act.
        for (positive start = at; start < (positive)got;)
        {
                positive length = memory_span_without_byte(text + start, '\n', (positive)got - start);

                text[start + length] = end;
                string_format(log, "%w\n", writer_terminal_name, (string_address)text + start);
                start += length + 1;
        }
        log_flush();
        return 0;
}

// The words of the kernel's defaults, for usb and for whoever cannot read the policy.
static bool usb_kernel_says(usb_census address_to census, p8 address_to into, positive room)
{
        bool locked = false;
        p8 word[8];

        string_copy_bounded(into, "  defaults:", room);
        for (positive at = 0; at < census->count; at++)
        {
                string_address hub = (string_address)census->device[at].port;
                p8 device[8];

                if (!census->device[at].root)
                        continue;
                usb_read(hub, "authorized_default", device, sizeof(device));
                usb_read(hub, "interface_authorized_default", word, sizeof(word));
                locked |= string_equals((string_address)device, "0") || string_equals((string_address)word, "0");
                string_append_bounded(into, " ", room);
                string_append_bounded(into, hub, room);
                string_append_bounded(into, " device ", room);
                string_append_bounded(into, (string_address)device, room);
                string_append_bounded(into, " interface ", room);
                string_append_bounded(into, (string_address)word, room);
                string_append_bounded(into, ",", room);
        }
        host_read_kernel(USB_MODULE_DEFAULT, word, sizeof(word));
        string_append_bounded(into, " new controllers ", room);
        string_append_bounded(into, word[0] ? (string_address)word : (string_address) "unknown", room);
        return locked;
}

/*
        moonwater usb: the mode, the classes, the defaults the kernel holds,
        whether the watcher runs, then a line for every device below a root
        hub -- its port, its id and serial, allowed or held as the kernel has
        it and why the policy says so, its interfaces, and last what it says
        it is, quoted, since that is its own to say -- and the rules. For
        somebody who cannot read the policy the mode is what the kernel's
        defaults show, and the rest of it is unknown.
*/
/* Who holds the watcher's lock, asked without making or writing its file:
   0 for nobody; known false for somebody who cannot open it (it is root's,
   0600), who used to be told that no watcher ran. */
static b32 usb_watcher_owner(bool address_to known)
{
        host_record_lock lock = {.type = HOST_FILE_WRLCK};
        bipolar handle = system_open_at(AT_FDCWD, USB_WATCHER_PATH,
                                        FILE_READ | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
        bipolar asked;

        *known = handle >= 0 || handle == -ENOENT;
        if (handle < 0)
                return 0;
        do
                asked = system_call_3(syscall(fcntl), (positive)handle, HOST_FILE_GETLK, (positive)address_of lock);
        while (asked == -EINTR);
        system_close(handle);
        *known = asked >= 0;
        return asked < 0 || lock.type == HOST_FILE_UNLCK ? 0 : lock.pid;
}

static b32 usb_status(void)
{
        static usb_census census;
        usb_policy policy;
        p8 line[USB_LOG_LINE];
        bool told;
        b32 watcher = usb_watcher_owner(address_of told);
        bool locked;

        usb_policy_load(address_of policy);
        usb_census_take(address_of census);
        locked = usb_kernel_says(address_of census, line, sizeof(line));
        string_format(log, host_label "usb %s\n", (policy.known ? policy.lock : locked) ? "lock" : "unlock");
        usb_classes_say(address_of policy);
        string_format(log, host_label "%s\n", line);
        if (watcher)
                string_format(log, host_label "  watcher running, pid %p\n", (positive)watcher);
        else if (!told)
                string_format(log, host_label "  whether the watcher runs is root's to see\n");
        else
                string_format(log, host_label "  watcher not running\n");
        if (!policy.known)
                string_format(log, host_label "  the rules are root's\n");
        if (policy.unkept)
        {
                p8 why[256];

                (void)usb_unkept_why(why, sizeof(why));
                string_format(log, host_label "  in force until the machine restarts, not kept: %s\n", (string_address)why);
        }
        if (policy.skipped)
                string_format(log, host_label "  %p lines of %s are no rule and are left out\n", policy.skipped,
                              USB_POLICY);
        if (policy.cut)
                string_format(log, host_label "  %s is longer than is read\n", USB_POLICY);
        if (!census.count)
                string_format(log, host_label "  no USB controller\n");

        for (positive at = 0; at < census.count; at++)
        {
                usb_device address_to device = census.device + at;
                p8 shown[USB_SHOWN * 4 + 8];
                p8 why = usb_judge(address_of policy, device);

                if (device->root)
                        continue;
                usb_shown(shown, sizeof(shown), device->product, device->product_length);
                (void)string_join(line, sizeof(line), "  ", (string_address)device->port, " ",
                                  (string_address)device->id, "/", (string_address)device->key,
                                  device->authorized ? " allowed (" : " held (",
                                  policy.known ? usb_why[why] : (string_address) "unknown",
                                  policy.known && usb_let(why) != device->authorized
                                      ? (usb_let(why) ? "; usb apply lets it in" : "; usb apply holds it")
                                      : "",
                                  ")");
                for (positive one = 0; one < census.interfaces; one++)
                {
                        usb_interface address_to face = census.interface + one;

                        if (!usb_owns(device, face))
                                continue;
                        string_append_bounded(line, "; ", sizeof(line));
                        string_append_bounded(line, (string_address)face->name + string_length((string_address)device->port) + 1,
                                              sizeof(line));
                        string_append_bounded(line, " ", sizeof(line));
                        string_append_bounded(line, usb_kinds[face->kind], sizeof(line));
                        string_append_bounded(line, face->authorized ? " allowed" : " held", sizeof(line));
                }
                string_format(log, host_label "%s; \"%s\"\n", line, shown);
        }
        if (census.dropped)
                string_format(log, host_label "  and %p more the census does not keep\n", census.dropped);
        for (positive at = 0; at < policy.count; at++)
        {
                usb_rule_text(line, sizeof(line), policy.rule + at);
                string_format(log, host_label "  rule %s %s\n", policy.rule[at].kind == USB_ALLOW ? "allow" : "deny",
                              line);
        }
        log_flush();
        return 0;
}

static b32 usb_apply_verb(void)
{
        bipolar lock = usb_lock_take(true);
        usb_policy policy;

        if (lock < 0)
                return usb_lock_refused(lock);
        usb_policy_load(address_of policy);
        usb_apply_all(address_of policy, null, null, true);
        radio_unlock(lock);
        usb_probe_flush();
        if (usb_watched(address_of policy))
                usb_watch_start();
        host_say(log, host_label "usb applied\n");
        return 0;
}

/*
        At boot, once /root is the one this session keeps: the policy put in
        force over what enumerated before init, and the watcher started when
        the policy holds anything. Every verdict restores. An ask's /root is
        the session's own, and on a machine whose command line holds every
        USB device (usbcore.authorized_default=2) the keyboard that answers
        the question is let in only here; the take that follows the answer
        restores again, with the disk's policy. The first restore of a boot
        logs every device as seen. A hardened machine (STRICT_HOST tight or
        locked) with no policy is locked once, at its first boot, by
        usb_first_lock: what is plugged in then is what it trusts, so a
        desktop's own keyboard keeps working. The default tier changes nothing
        until the owner locks.

        Whether a /root keeps a policy is what decides, and not whether a
        watcher runs: an ask's session locks its own /root and its watcher
        is running when the disk is taken, and a disk with no policy then
        locked nothing, the watcher found no policy in the disk's /root and
        ended, and the kernel was left holding every new device at the
        session's defaults with usb saying unlock. On the default tier a
        policy-less /root taken after another one's lock was in force puts
        the kernel's own defaults back, which is what no policy means there.
*/
static fn usb_first_lock(void);

static fn usb_restore(void)
{
        static usb_census before;
        bool first = !host_lock_owner(USB_WATCHER_PATH);
        usb_policy policy;
        bipolar lock = usb_lock_take(false);

        //      The policy is put in force at boot whether or not the lock can be had.
        if (lock < 0)
                usb_tell_text(false, "boot applies the policy without the usb lock: ", file_reason(lock));
        usb_policy_load(address_of policy);
        if (policy.kept)
        {
                before.count = 0;
                usb_apply_all(address_of policy, first ? address_of before : null, null, false);
        }
        else if (!first && policy.known && MOONWATER_STRICT_HOST < STRICT_TIGHT)
                usb_apply_all(address_of policy, null, null, false);
        radio_unlock(lock);
        usb_probe_flush();
        if (!policy.kept && (MOONWATER_STRICT_HOST >= STRICT_TIGHT || !policy.known))
                usb_first_lock();
        else if (usb_watched(address_of policy))
                usb_watch_start();
}

// Every port below a root hub, and whether its device is configured: what a settle compares.
static fn usb_ports(usb_census address_to census, p8 address_to into, positive room)
{
        into[0] = end;
        for (positive at = 0; at < census->count; at++)
                if (!census->device[at].root)
                {
                        string_append_bounded(into, (string_address)census->device[at].port, room);
                        string_append_bounded(into, census->device[at].authorized ? (string_address) "+ " : (string_address) "- ",
                                              room);
                }
}

/*
        A hardened machine's first lock, once USB has settled: the devices of
        a boot go on enumerating after init starts (a hub and what is behind
        it the longest), and a snapshot taken before they have would hold the
        very keyboard it was meant to keep, which the first guest that tried
        it showed (tablet and keyboard enumerated after the restore, and held
        by it). The census is taken again every quarter second until it has
        not changed for two seconds and the machine has been up three, or it
        has been up fifteen; then the lock, under the usb lock and only if
        nothing has made a policy meanwhile, and then this is the watcher. A
        process of its own does it, so the boot waits for none of it; the
        devices it has not yet judged are as the kernel has them (held from
        the start by usbcore.authorized_default=2 where the command line says
        so). A machine first locked long after it booted (setup use of a disk
        with no policy) is locked at once.

        One first lock settles at a time (USB_SETTLE_PATH): another asked for
        meanwhile -- the take's restore and the machine process's both ask --
        ends at once, since the one settling locks whichever /root is in
        place when it is done. Once a settle has ended this boot
        (USB_SETTLED_PATH), USB has settled and a later first lock -- the
        disk's, taken after an ask's session locked its own /root -- locks
        at once, carried: what the session's lock held stays held, and the
        devices it let in are kept. The session's watcher, if it still runs,
        reads the disk's policy at its next pass; this one ends when it
        cannot be the watcher.
*/
static fn usb_first_lock(void)
{
        static usb_census census;
        static usb_settle settle;
        static usb_settle next;
        static p8 seen[USB_MOST * (USB_PORT_ROOM + 2) + 1];
        static p8 now[sizeof(seen)];
        p64 still = 0;
        p64 uptime = 0;
        p64 began = 0;
        usb_policy policy;
        bipolar settling;
        bipolar lock;
        bool carried;

        if (!host_detach(descriptors_none, descriptors_none))
                return;
        settling = host_lock_file(USB_SETTLE_PATH, HOST_FILE_SETLK, null);
        if (settling < 0)
                system_call_1(syscall(exit_group), 0);
        carried = system_access_at(AT_FDCWD, USB_SETTLED_PATH, 0) >= 0;
        seen[0] = end;
        settle.count = 0;
        while (!carried)
        {
                uptime = system_clock_ns(HOST_CLOCK_BOOTTIME);
                began = began ? began : uptime;
                usb_census_take(address_of census);
                //      When each device was first seen, carried from census to census.
                next.count = 0;
                for (positive at = 0; at < census.count; at++)
                {
                        usb_device address_to device = census.device + at;
                        usb_age address_to was = usb_age_of(address_of settle, device);
                        usb_age address_to age;

                        if (device->root)
                                continue;
                        age = next.age + next.count++;
                        string_copy(age->port, (string_address)device->port);
                        string_copy(age->id, (string_address)device->id);
                        string_copy(age->key, (string_address)device->key);
                        age->since = was ? was->since : uptime;
                }
                memory_copy(address_of settle, address_of next, sizeof(settle));
                usb_ports(address_of census, now, sizeof(now));
                if (!string_equals((string_address)now, (string_address)seen) || !still)
                {
                        string_copy(seen, (string_address)now);
                        still = uptime;
                }
                if (uptime >= USB_UP_MOST_NS || (uptime >= USB_UP_LEAST_NS && uptime - still >= USB_STILL_NS))
                        break;
                host_pause(USB_POLL_NS);
        }
        //      Young is what was seen to arrive: what the first census found is not, so a lock taken long after
        //      boot, whose settle ends at its first census, keeps everything plugged in.
        settle.cutoff = uptime > began + USB_STILL_NS ? uptime - USB_STILL_NS : began;
        (void)host_write_text(USB_SETTLED_PATH, "1\n");
        lock = usb_lock_take(false);
        if (lock < 0)
                usb_tell_text(false, "the first lock is taken without the usb lock: ", file_reason(lock));
        usb_policy_load(address_of policy);
        if (!policy.kept)
                (void)usb_lock_held(false, carried ? USB_LOCK_CARRIED : USB_LOCK_FIRST, carried ? null : address_of settle);
        radio_unlock(lock);
        usb_probe_flush();
        system_close(settling);
        (void)usb_watch_run();
        system_call_1(syscall(exit_group), 0);
}

// The watcher again, if it ended while the policy still holds something.
static fn usb_recover(void)
{
        usb_policy policy;

        if (host_lock_owner(USB_WATCHER_PATH))
                return;
        usb_policy_load(address_of policy);
        if (usb_watched(address_of policy))
                usb_watch_start();
}

/*
        What is asked is judged before who asks: a word usb does not take is a
        usage page for anybody, a change needs root, and reading the mode and
        the devices needs nothing.
*/
static b32 host_usb(string_address address_to arguments, positive count)
{
        string_address word = count > 2 ? arguments[2] : null;

        if (count == 2)
                return usb_status();
        if (string_equals(word, "class"))
                return usb_class(arguments, count);
        if (count == 3 && string_equals(word, "lock"))
        {
                host_need_root();
                return usb_lock_now();
        }
        if (count == 3 && string_equals(word, "unlock"))
        {
                host_need_root();
                return usb_unlock_now();
        }
        if (count == 4 && (string_equals(word, "allow") || string_equals(word, "deny")))
        {
                host_need_root();
                return usb_rule_verb(arguments[3], word[0] == 'a' ? USB_ALLOW : USB_DENY);
        }
        if (count == 4 && string_equals(word, "forget"))
        {
                host_need_root();
                return usb_forget(arguments[3]);
        }
        if (string_equals(word, "log") && count <= 4)
        {
                positive asked = USB_LOG_SHOWN;
                positive used = 0;

                if (count == 4)
                        asked = string_digits_max(arguments[3], 4, address_of used);
                return_if(count == 4 && (!used || arguments[3][used] || !asked || asked > USB_LOG_ASKED),
                          host_usage());
                host_need_root();
                return usb_log_show(asked);
        }
        if (count == 3 && string_equals(word, "apply"))
        {
                host_need_root();
                return usb_apply_verb();
        }
        if (count == 3 && string_equals(word, "watch"))
        {
                b32 owner = host_lock_owner(USB_WATCHER_PATH);

                host_need_root();
                return_if(owner, host_refuse("the usb watcher is already running, pid %p\n", (positive)owner));
                return usb_watch_run();
        }
        return host_usage();
}

/* ---- locale: the timezone and the clock, and the SNTP that sets it. ---- */

/*
        Timezone, NTP and keyboard layout, as moonwater verbs.

        Choices live on /root so an image update keeps them. The machine
        starts NTP itself: restore forks the first query before init, and the
        wait loop keeps walking servers until the clock is set. Sampling takes
        five samples and keeps the lowest delay unless /root/ntp.sampling
        says off. The kernel
        adds that offset with adjtimex; a kiss-o-death drops the server.

        The forked query is not asked after with kill. A pid that has
        exited but not been waited for is still a pid, so kill(pid, 0)
        answers zero for a zombie exactly as it does for a live child: the
        poll would see its first query running for ever. It says how it
        went on a pipe (locale_child), and is waited for once it has.

        THE STEP AND THE SLEW HAVE BEEN RUN

        Both have been carried out against a kernel: KVM guests were set and
        slewed by this code and read back against a panel of stratum-1
        servers, one reading a minute (55d6b775 has the figures), and the
        choice between stepping and slewing is checked against crafted
        offsets either side of the threshold in the machine lane. Every ADJ_
        and STA_ value here is also held against uapi/linux/timex.h, and
        every timex word index against the struct's own offsets.

        The reason to read a granted call apart from an accepted one is in
        the constants below. ADJ_SETOFFSET was 0x80, which is ADJ_TAI, for as long as
        this file has had it -- so no correction this program ever
        computed reached the clock. It survived because adjtimex answers a
        wrong mode the same way it answers a right one: with the clock
        state, a non-negative number the caller reads as success. It also
        cleared STA_UNSYNC on the way past, so the machine went on to
        report itself synchronised. Anyone adding a mode here should know
        that a non-negative return proves the call was accepted and
        nothing else, and that there is no test that can tell you
        otherwise, because nothing unprivileged can ask the kernel to
        demonstrate which mode a bit meant.
*/

/* ---- SNTP: how the clock above is asked what the time is. ---- */

/*
        Experimental C standard library

        SNTP: RFC 5905 on-wire offset and delay, then a clock filter

        A single sample's offset error is (d_fwd - d_rev) / 2, and that is
        bounded by half the round-trip delay. Integer-second arithmetic and
        planting the server's transmit fraction as tv_nsec both throw that
        bound away. Five samples with the smallest delay kept is the RFC
        5905 clock filter, not an average: averaging a one-sided queue spike
        poisons the estimate.

        The clock is stepped by that offset with adjtimex ADJ_SETOFFSET, not
        by reading the clock again and planting a new wall time: the gap
        between those two traps would be extra error. A machine whose clock
        is still at the epoch must be allowed a decades-long first step; one
        whose clock already looks like a civil date may not jump more than a
        day, and a refresh of a synchronised clock may not jump more than
        two seconds. Past two seconds no single server is believed: another
        answer has to agree (sntp_choose). A kiss-o-death, a stratum 0, or a
        root delay/dispersion worse than a second abandons that server
        rather than collecting more samples from it.

        No step lands before SNTP_WALL_LEAST, which is not a guess at what
        clocks read but the date of this source (MOONWATER_CLOCK_FLOOR in
        net.c, where the certificate check reads the same floor): a clock
        set earlier than the code that set it is wrong by construction,
        which is systemd's TIME_EPOCH rule. Moving it forward with each
        release is what keeps an answer from taking the clock back to when a
        revoked or expired certificate was still good. And no step lands
        before clock_trust_floor either, the latest time an authenticated
        answer or this machine's own uptime ever put in /root/clock.good, so
        a forged answer cannot take a machine back to a date it was already
        past.

        SNTP_WALL_MOST ends that window at the last second sntp_load_stamp
        can read, 2104-02-26 09:42:23 UTC: a stamp whose high bit is set is
        era 0 (1968 through 2036-02-07 06:28:16, when the 32 bit seconds
        wrap), one whose high bit is clear is era 1 (that moment through
        2104), and a stamp does not say which, so past 2104 the reading
        would be wrong by 136 years. It used to end at 2036-01-01 -- a round
        date before the wrap -- and a clock past it could no longer be set
        from the network at all: every server's answer was out of the window.
        The offset sum is two differences of four terms the window bounds,
        and at 2104 it is 8.47 of the 9.22 the type holds.

        T1 is read, the originate stamp is written, and the packet is sent
        with nothing else in between. T4 is read the moment recv returns.
        The conversion of those timespecs into nanoseconds waits until after
        the trap. This tree has no vDSO: clock_gettime is a syscall, and
        that cost dwarfs the stores, so the C must not add a second one.

        WHAT HAS BEEN MEASURED, AND WHAT HAS NOT

        Against five servers at once, with the box's own clock held
        synchronised by something else as the reference, this file's
        answers sat within 193 microseconds of it; a single-sample client
        with userspace stamps, asked the same servers in the same minute,
        spread to 1207. Both agree on sign and scale, so the difference
        is the five-sample filter and the kernel stamps, not a disagreement
        about what time it is.

        The two kernel stamps are worth, at the median of real exchanges:
        10355 ns for the arrival stamp, and 1729 ns to one server and 2320
        to another for the departure stamp. Both are one-sided, which is
        why they land in the offset at all -- the formula assumes the path
        is symmetric. Round-trip asymmetry itself cannot be measured from
        one end and so cannot be corrected here; across the servers above
        it accounts for a spread of several milliseconds, which is larger
        than everything this file does about anything else.

        What is not proven is the clock being set. Nothing here can take
        CLOCK_REALTIME on a machine that is not ours to disturb, so the
        step and slew paths in the locale section below have never been
        executed against a
        kernel that carried them out. What is checked is the request: the
        mode words against uapi/linux/timex.h, and the decision between
        stepping and slewing against crafted offsets in the machine lane.
        A reader should take "the right thing is asked for" from this and
        not "the asking has been seen to work".

        Dawn Larsson - Apache 2.0 license
        github.com/dawnlarsson/moonwater

        www.dawning.dev
*/

#ifndef STANDARD_MODERN_C_NET_SNTP
#define STANDARD_MODERN_C_NET_SNTP

#define SNTP_PORT 123
#define SNTP_PACKET 48
/* One byte beyond the 48-byte message lets recvmsg tell an exact reply from a
   longer datagram.  The default reads the first 48 bytes of a longer one, as
   ntpd and chrony do (extension fields, a MAC); the tight tier takes only the
   exact shape, because bytes after the message are otherwise accepted without
   being parsed.  A buffer of exactly SNTP_PACKET would make both read 48. */
#define SNTP_REPLY_ROOM (SNTP_PACKET + 1)
#define SNTP_SECONDS 2
#define SNTP_SAMPLES 5
#define SNTP_SERVERS 3
#define SNTP_UNIX 2208988800u
#define SNTP_LI_VN_MODE 0x23
#define SNTP_NANOSECONDS 1000000000ull
#define SNTP_OK 0
//      A query's answers are numbers of their own, far from the errors a
//      system call gives: the clock's refusal to be set is one of those, and
//      -EPERM was SNTP_NO_SERVER and -EIO was SNTP_RATE_LIMITED, so file_reason
//      said "No such process" of an answer out of range.
#define SNTP_ANSWER (-100000)
#define SNTP_NO_SERVER (SNTP_ANSWER - 1)
#define SNTP_NO_REPLY (SNTP_ANSWER - 2)
#define SNTP_MALFORMED (SNTP_ANSWER - 3)
#define SNTP_BAD_SERVER (SNTP_ANSWER - 4)
#define SNTP_RATE_LIMITED (SNTP_ANSWER - 5)
#define SNTP_UNCONFIRMED (SNTP_ANSWER - 6)
#define SNTP_DENIED (SNTP_ANSWER - 7)
//      The answer was earlier than this machine has already been: its build,
//      or the last time it was sure (clock_trust_floor).
#define SNTP_BELOW_FLOOR (SNTP_ANSWER - 8)
//      An NTS server said its cookie was no good (NTSN): establish keys again.
#define SNTP_NTS_NAK (SNTP_ANSWER - 9)
//      A plain answer put the clock further from the time NTS gave this boot
//      than the crystal can have drifted since.
#define SNTP_CONTRADICTS (SNTP_ANSWER - 10)
//      The query could not be confined (net_apart), so nothing was asked; or
//      its confined child ended without an answer that could be believed.
#define SNTP_CONFINE (SNTP_ANSWER - 11)
#define SNTP_APART (SNTP_ANSWER - 12)
#define SNTP_KISS_RATE 0x52415445u /* "RATE" */
#define SNTP_KISS_DENY 0x44454e59u /* "DENY" */
#define SNTP_KISS_RSTR 0x52535452u /* "RSTR" */
#define SNTP_DELAY_MOST_NS ((bipolar)2 * (bipolar)SNTP_NANOSECONDS)
#define SNTP_OFFSET_MOST_NS ((bipolar)24 * 3600 * (bipolar)SNTP_NANOSECONDS)
#define SNTP_OFFSET_SYNCED_NS ((bipolar)2 * (bipolar)SNTP_NANOSECONDS)
#define SNTP_WALL_LEAST MOONWATER_CLOCK_FLOOR
#define SNTP_WALL_MOST 4233462143ll  /* 2104-02-26 09:42:23, the era 1 stamp ends */
#define SNTP_WALL_LEAST_NS \
        ((bipolar)SNTP_WALL_LEAST * (bipolar)SNTP_NANOSECONDS)
#define SNTP_WALL_MOST_NS \
        ((bipolar)SNTP_WALL_MOST * (bipolar)SNTP_NANOSECONDS)
//      What a person who asks for the time by hand may be told to step by:
//      anything the window holds.
#define SNTP_OFFSET_ANY_NS (SNTP_WALL_MOST_NS - SNTP_WALL_LEAST_NS)
#define SNTP_TIMESPEC_SECONDS_MOST 9223372035ull
#define SNTP_ERA ((bipolar)4294967296)
#define SNTP_SHORT_SECOND 0x10000u
#define SNTP_TIMESTAMPNS 35
#define SNTP_TIMESTAMPING 37
#define SNTP_TIMESTAMPING_WANT 2194u /* TX_SOFTWARE|SOFTWARE|OPT_ID|TSONLY */
#define SNTP_ERRQUEUE 0x2000
#define SNTP_DONTWAIT 0x40
#define SNTP_MESSAGE_WORDS 7
#define SNTP_CONTROL_WORDS 24
#define SNTP_MSG_CTRUNC 0x8
#define SNTP_CONTROL_HEAD (sizeof(positive) + 8)
#define SNTP_ERRQUEUE_MOST 4
#define SNTP_SOL_IP 0
#define SNTP_IP_RECVERR 11
#define SNTP_ERROR_TIMESTAMPING 4 /* SO_EE_ORIGIN_TIMESTAMPING */
#define SNTP_ERROR_BYTES 16       /* struct sock_extended_err */
#define SNTP_ERROR_ORIGIN 4       /* ee_origin within it */
#define SNTP_ERROR_SEQUENCE 12    /* ee_data, which carries the id */
#define SNTP_CONTROL_DATA                          \
        ((SNTP_CONTROL_HEAD + sizeof(positive) - 1) & \
         ~(sizeof(positive) - 1))

typedef struct
{
        bipolar offset_ns;
        bipolar delay_ns;
        bipolar distance_ns;
        bool ok;
        //      The server's address, so that one server answering under three
        //      names is counted once.
        p32 address;
} sntp_sample;

static inline INLINE CONST bool sntp_wall_ok(bipolar ns)
{
        return ns >= SNTP_WALL_LEAST_NS && ns <= SNTP_WALL_MOST_NS;
}

static inline INLINE CONST bool sntp_within(bipolar ns, bipolar most)
{
        return ns >= -most && ns <= most;
}

/*
        A local stamp may sit anywhere from the epoch to the end of the
        window, because a machine that has never been told the time boots
        at zero. It may not sit past the window: t1 and t4 are the only
        two terms sntp_offset_delay adds that are not already bounded by
        the wire format, and an unbounded one overflows the sum.
*/
static inline INLINE CONST bool sntp_local_ok(bipolar ns)
{
        return ns >= 0 && ns <= SNTP_WALL_MOST_NS;
}

static inline INLINE CONST bipolar sntp_timespec_ns(p64 seconds, p64 nanoseconds)
{
        if (nanoseconds >= SNTP_NANOSECONDS || seconds > SNTP_TIMESPEC_SECONDS_MOST)
                return -1;
        return (bipolar)seconds * (bipolar)SNTP_NANOSECONDS +
               (bipolar)nanoseconds;
}

static inline INLINE bipolar sntp_now_ns(void)
{
        p64 now[2] = {0, 0};

        if (system_call_2(syscall(clock_gettime), CLOCK_REALTIME,
                          (positive)now) < 0)
                return -1;
        return sntp_timespec_ns(now[0], now[1]);
}

/*
        The transmit stamp goes out to be echoed back, and the echo is the
        only thing telling us a reply is ours. Nothing reads this field as a
        clock: t1 comes from the kernel's transmit timespec. It is therefore
        an authenticator, not a timestamp, and every one of its 64 bits can
        and must be unpredictable.

        This used to put the public wall-clock seconds in the high half and
        ask for the low half with GRND_NONBLOCK. On a machine whose pool was
        not ready it fell back to the equally public clock fraction, leaving
        only the UDP source port between an off-path forged reply and a clock
        change. Blocking for the CSPRNG is the same policy DNS uses for its
        transaction id: no entropy means no security-sensitive transaction.
*/
static inline INLINE bool sntp_put_stamp(p8 address_to field)
{
        return system_random_fill(field, 8, 0) == 0;
}

static inline INLINE PURE bipolar sntp_load_stamp(p8 address_to field)
{
        p32 ntp_seconds = network_load_32(field);
        p32 ntp_frac = network_load_32(field + 4);
        bipolar unix_seconds = (bipolar)ntp_seconds - (bipolar)SNTP_UNIX;
        bipolar unix_nsec =
            (bipolar)(((p64)ntp_frac * SNTP_NANOSECONDS) >> 32);

        if (!(ntp_seconds & 0x80000000u))
                unix_seconds += SNTP_ERA;
        return unix_seconds * (bipolar)SNTP_NANOSECONDS + unix_nsec;
}

static inline INLINE CONST bool sntp_short_ok(p32 word)
{
        return !(word & 0x80000000u) && word <= SNTP_SHORT_SECOND;
}

// An NTP short, sixteen bits of seconds and sixteen of fraction, in
// nanoseconds; sntp_short_ok has held it to a second.
static inline INLINE CONST bipolar sntp_short_ns(p32 word)
{
        return (bipolar)(((p64)word * SNTP_NANOSECONDS) >> 16);
}

/*
        How far this sample's offset can be from the truth: half its own
        round trip, which the path's asymmetry can take all of, plus how far
        the server's clock can be from the source it follows -- half its
        root delay and the whole of its root dispersion. RFC 5905 calls the
        sum the root distance and ranks servers by it. Half the round trip
        alone was what the kernel was told the error was, which leaves out
        the server's own and understates it for any server not at stratum 1.
*/
static inline INLINE CONST bipolar sntp_distance_ns(bipolar delay_ns,
                                                    p32 root_delay,
                                                    p32 root_dispersion)
{
        return delay_ns / 2 + sntp_short_ns(root_delay) / 2 +
               sntp_short_ns(root_dispersion);
}

static inline INLINE fn sntp_offset_delay(bipolar t1, bipolar t2, bipolar t3,
                                          bipolar t4,
                                          bipolar address_to offset_ns,
                                          bipolar address_to delay_ns)
{
        address_to offset_ns = ((t2 - t1) + (t3 - t4)) / 2;
        address_to delay_ns = (t4 - t1) - (t3 - t2);
}

static CONST COLD bool sntp_sample_sane(bipolar t1, bipolar t2, bipolar t3,
                                   bipolar t4, bipolar offset_ns,
                                   bipolar delay_ns, bipolar most)
{
        if (t1 < 0 || t4 < t1 || t3 < t2 || !sntp_wall_ok(t2) || !sntp_wall_ok(t3) ||
            delay_ns < 0 || delay_ns > SNTP_DELAY_MOST_NS)
                return false;
        //      A clock that tells the time already is moved by at most
        //      what was asked; one that does not has nothing to be moved
        //      from, and the window bounds what it can be told.
        if (most <= SNTP_OFFSET_SYNCED_NS)
                return sntp_within(offset_ns, most);
        return !sntp_wall_ok(t1) || sntp_within(offset_ns, most);
}

static COLD bool sntp_target_ok(bipolar now, bipolar offset_ns,
                           bipolar address_to target)
{
        if (offset_ns > 0 && now > bipolar_max - offset_ns)
                return false;
        if (offset_ns < 0 && now < bipolar_min - offset_ns)
                return false;
        address_to target = now + offset_ns;
        return sntp_wall_ok(address_to target);
}

static PURE COLD bipolar sntp_pick(sntp_sample address_to row, positive count)
{
        bipolar best = -1;
        positive at;

        for (at = 0; at < count; at++)
                if (row[at].ok &&
                    (best < 0 || row[at].delay_ns < row[best].delay_ns ||
                     (row[at].delay_ns == row[best].delay_ns &&
                      (bipolar)at > best)))
                        best = (bipolar)at;
        return best;
}

/*
        Which server's answer to believe, of up to SNTP_SERVERS. Each answer
        is an interval, its offset give or take its root distance, and the
        true offset lies in every correct server's. An answer whose interval
        meets fewer than half of the others' is a falseticker -- its clock is
        wrong, or its path is lopsided past what its round trip admits -- and
        is set aside; of the rest the least root distance wins, which is RFC
        5905's rule for the system peer. Taking the first server that
        answered, which is what was done, believed a falseticker whenever one
        answered first.

        Two that disagree cannot say which is wrong, and one cannot disagree
        with anything. They used to all stand, so the least root distance
        won -- a number the answer states about itself, which a forger sets
        to nothing: one on-path answer 2 h out beat the honest server it
        contradicted. Now no answer moves the clock past SNTP_OFFSET_SYNCED_NS
        unless another answer's interval meets it, and one that leaves the
        clock within that needs no second word. -1 when nothing may be
        believed.
*/
static PURE COLD bipolar sntp_choose(sntp_sample address_to row, positive count)
{
        bipolar best = -1;
        bool any_kept = false;
        bool kept[SNTP_SERVERS];
        positive meets[SNTP_SERVERS];

        for (positive at = 0; at < count; at++)
        {
                positive others = 0;

                kept[at] = false;
                meets[at] = 0;
                if (!row[at].ok)
                        continue;
                for (positive other = 0; other < count; other++)
                {
                        if (other == at || !row[other].ok)
                                continue;
                        //      The same server again is no second opinion,
                        //      whatever name it was reached under.
                        if (row[other].address == row[at].address)
                                continue;
                        others++;
                        if (row[at].offset_ns - row[at].distance_ns <=
                                row[other].offset_ns + row[other].distance_ns &&
                            row[other].offset_ns - row[other].distance_ns <=
                                row[at].offset_ns + row[at].distance_ns)
                                meets[at]++;
                }
                kept[at] = others < 2 || meets[at] * 2 >= others;
                any_kept |= kept[at];
        }

        for (positive at = 0; at < count; at++)
                if (row[at].ok && (kept[at] || !any_kept) &&
                    (meets[at] ||
                     sntp_within(row[at].offset_ns, SNTP_OFFSET_SYNCED_NS)) &&
                    (best < 0 || row[at].distance_ns < row[best].distance_ns))
                        best = (bipolar)at;
        return best;
}

/*
        t4 is meant to be when the reply arrived. Read after recv returns
        it is when this process next ran, which is the arrival plus however
        long the packet waited in the socket and however long the scheduler
        took to wake us. Half of that lands in the offset, and on an idle
        machine talking to a real server it measured a shade over eleven
        microseconds -- four hundred times the whole of the arithmetic that
        follows, and nothing the arithmetic can do anything about.

        SO_TIMESTAMPNS makes the kernel record the arrival in the softirq
        that takes the packet off the device and hand it over as a control
        message. Reading it needs recvmsg rather than recvfrom, and the
        socket calls in lib.c are recvfrom, so the trap is made here
        the way this file already traps for clock_gettime.

        msghdr is seven pointer-width words -- the name and its length,
        the vector and its count, the control buffer and its length, and
        the flags -- which is its shape on every target this tree builds.
        A control message is a pointer-width length, then a level and a
        type of four bytes each, then the payload at the next word.

        Nothing here is required to work. A kernel that refuses the option
        or a path that delivers no control message leaves the stamp unset,
        and the caller reads the clock itself exactly as before.
*/
/*
        The walk is apart from the trap because it is the half that fails
        quietly: a wrong offset finds no message, and a receive that found
        no message is indistinguishable from a kernel that sent none. That
        reads as "the timestamp did not help" rather than as a mistake, so
        it is reached here by its own name, and the machine check
        (sntp_math_ok in test/checks.c) hands it buffers laid out by
        hand -- including ones whose length words lie.

        Every field is read from a buffer the kernel filled, and the two
        lengths are believed only as far as the buffer goes: a message
        claiming to be longer than what is left ends the walk.
*/
static p8 address_to sntp_control_find(p8 address_to control, positive length,
                                       b32 level, b32 type, positive least)
{
        positive at = 0;

        while (at + SNTP_CONTROL_DATA <= length)
        {
                positive size = address_to(positive address_to)(control + at);

                if (size < SNTP_CONTROL_DATA || size > length - at)
                        break;
                if (address_to(b32 address_to)(control + at + sizeof(positive)) ==
                        level &&
                    address_to(b32 address_to)(control + at + sizeof(positive) +
                                               4) == type &&
                    size - SNTP_CONTROL_DATA >= least)
                        return control + at + SNTP_CONTROL_DATA;
                at += (size + sizeof(positive) - 1) & ~(sizeof(positive) - 1);
        }
        return null;
}

static bool sntp_control_stamp(p8 address_to control, positive length,
                               b32 kind, p64 address_to arrived)
{
        p8 address_to data = sntp_control_find(control, length, SOL_SOCKET,
                                               kind, 2 * sizeof(p64));

        if (!data)
                return false;
        arrived[0] = address_to(p64 address_to)data;
        arrived[1] = address_to(p64 address_to)(data + sizeof(p64));
        return true;
}

/*
        What of the control buffer is to be believed. The kernel says when
        it had more to put in than there was room for, and the stamps and
        the id are read in pairs, so a buffer it cut short is not read at
        all: the exchange takes its stamps from the clock, as it does with
        none. The buffer holds the three messages a send and a receive
        can leave, 32, 64 and 48 bytes, with room to spare.
*/
static inline INLINE CONST positive sntp_control_held(positive flags,
                                                      positive filled)
{
        return flags & SNTP_MSG_CTRUNC ? 0 : filled;
}

/*
        recvmsg into one buffer and the control words beside it, with the
        control length the kernel filled put in held.
*/
static HOT bipolar sntp_receive_message(b32 handle, p8 address_to into,
                                        positive room,
                                        positive address_to control,
                                        positive address_to held,
                                        positive flags)
{
        positive message[SNTP_MESSAGE_WORDS];
        positive vector[2] = {(positive)into, room};
        bipolar got;

        memory_zero(message, sizeof(message));
        memory_zero(control, SNTP_CONTROL_WORDS * sizeof(positive));
        message[2] = (positive)vector;
        message[3] = 1;
        message[4] = (positive)control;
        message[5] = SNTP_CONTROL_WORDS * sizeof(positive);
        got = system_call_3(syscall(recvmsg), (positive)handle,
                            (positive)message, flags);
        address_to held = got < 0 ? 0 : sntp_control_held(message[6], message[5]);
        return got;
}

static HOT bipolar sntp_receive_stamped(b32 handle, p8 address_to reply,
                                        positive room,
                                        p64 address_to arrived,
                                        bool address_to stamped)
{
        positive control[SNTP_CONTROL_WORDS];
        positive held = 0;
        bipolar got = sntp_receive_message(handle, reply, room, control,
                                           address_of held, SNTP_DONTWAIT);

        address_to stamped = sntp_control_stamp((p8 address_to)control, held,
                                                SNTP_TIMESTAMPNS, arrived);
        return got;
}

/*
        t1 has the same trouble t4 had, at the other end. It is read
        before the send trap, so it is the moment before the kernel is
        entered, and the packet leaves after the protocol stack has run.
        The offset formula assumes the two directions are symmetric, so a
        head start on the send side alone goes straight into the answer at
        half its size: measured against the kernel's own departure stamp,
        a median of 1729 ns to one server and 2320 ns to another.

        SOF_TIMESTAMPING_TX_SOFTWARE records the moment the packet is
        given to the driver and queues it on the socket's error queue.
        Measured on a real route it is already there when send returns, 50
        times out of 50, so one recvmsg that refuses to wait collects it
        and no poll is needed.

        Draining it is not optional once the option is on. A socket with
        anything on its error queue reports POLLERR, and the wait below
        asks about readability and would be woken by that for ever. One
        non-blocking read after each send empties it, which 100 exchanges
        across two servers confirm: no POLLERR survived into the wait.

        OPT_TSONLY keeps the packet itself off the queue, so what comes
        back is the timestamp and the error header beside it. The walk
        steps over anything that is not the timestamp, which is what lets
        a real ICMP error sit there without being mistaken for one.
*/
/*
        OPT_ID is already asked for, so the kernel numbers every transmit
        stamp with a counter that starts at zero when the option is set
        and rises by one per send. Five sends on one socket came back 0,
        1, 2, 3, 4.

        The number does not travel in the timestamp. It is in the error
        header beside it, as ee_data, and the same header says in
        ee_origin whether this queue entry is a timestamp at all or a
        real ICMP error that happens to be sitting there. Reading both is
        what makes the stamp provably the one belonging to the send being
        timed, rather than whichever stamp was on the queue -- a
        distinction that only bites if a drain is ever missed, which is
        exactly the case that cannot be tested from outside.

        A kernel that sends no error header, or one whose numbering does
        not line up, leaves the stamp unclaimed and the exchange falls
        back to the userspace reading, as it does when the option is
        refused outright.
*/
static bool sntp_control_sequence(p8 address_to control, positive length,
                                  p32 address_to sequence)
{
        p8 address_to body = sntp_control_find(control, length, SNTP_SOL_IP,
                                               SNTP_IP_RECVERR,
                                               SNTP_ERROR_BYTES);

        if (!body || body[SNTP_ERROR_ORIGIN] != SNTP_ERROR_TIMESTAMPING)
                return false;
        address_to sequence = address_to(p32 address_to)(body + SNTP_ERROR_SEQUENCE);
        return true;
}

static HOT bool sntp_transmit_stamp(b32 handle, p32 wanted,
                                    p64 address_to departed)
{
        positive control[SNTP_CONTROL_WORDS];
        p8 sink[SNTP_PACKET];
        p64 stamp[2];
        bool found = false;

        for (positive round = 0; round < SNTP_ERRQUEUE_MOST; round++)
        {
                positive held = 0;
                p32 sequence = 0;

                if (sntp_receive_message(handle, sink, sizeof(sink), control,
                                         address_of held,
                                         SNTP_ERRQUEUE | SNTP_DONTWAIT) < 0)
                        break;
                if (sntp_control_stamp((p8 address_to)control, held,
                                       SNTP_TIMESTAMPING, stamp) &&
                    sntp_control_sequence((p8 address_to)control, held,
                                          address_of sequence) &&
                    sequence == wanted)
                {
                        departed[0] = stamp[0];
                        departed[1] = stamp[1];
                        found = true;
                }
        }
        return found;
}

/*
        Everything below is read out of forty-eight bytes a stranger sent.
        The socket is connected, so the kernel has already dropped a
        datagram whose source is not the server's, but an on-path answer
        and a blind one aimed at an open port both arrive here.

        The origin stamp is the check that carries the weight: it is the
        transmit stamp we planted, echoed back, and an answer that does
        not carry it was not an answer to our question. It is dropped and
        the wait resumes rather than ending the exchange, because a late
        reply to an earlier sample is not a reason to give up on this one.

        Stratum zero is a kiss-o-death and the four bytes at 12 say which.
        RATE is the server asking to be asked less often, which is a
        different answer from DENY and RSTR, which tell this machine to
        stay away: each is reported separately so the policy above can wait
        instead of walking to the next server and asking again immediately,
        and wait longer for the one that says no.
*/
static COLD bipolar sntp_reply_ok(p8 address_to reply, p8 address_to request)
{
        if (memory_compare(reply + 24, request + 40, 8))
                return SNTP_NO_REPLY;
        //      Mode 4 is a server's answer; versions 1 to 4 are the ones
        //      there are, and 0, 5, 6 and 7 are not an NTP answer at all.
        if ((reply[0] & 0x7) != 4 || !((reply[0] >> 3) & 7) || ((reply[0] >> 3) & 7) > 4)
                return SNTP_BAD_SERVER;
        if (!reply[1])
        {
                p32 kiss = network_load_32(reply + 12);

                return kiss == SNTP_KISS_RATE ? SNTP_RATE_LIMITED
                       : kiss == SNTP_KISS_DENY || kiss == SNTP_KISS_RSTR
                           ? SNTP_DENIED
                           : SNTP_BAD_SERVER;
        }
        if ((reply[0] >> 6) == 3 || reply[1] >= 16)
                return SNTP_BAD_SERVER;
        if (!sntp_short_ok(network_load_32(reply + 4)) ||
            !sntp_short_ok(network_load_32(reply + 8)))
                return SNTP_BAD_SERVER;
        return SNTP_OK;
}

/*
        One reply made into a sample, or the reason it is not one: the
        checks above on the header, then the four stamps. t1 and t4 are the
        local departure and arrival, t4 as the kernel stamped it.
*/
static COLD bipolar sntp_reply_sample(p8 address_to reply,
                                      p8 address_to request, bipolar t1,
                                      bipolar t4, bipolar most,
                                      sntp_sample address_to into)
{
        bipolar verdict = sntp_reply_ok(reply, request);
        bipolar t2;
        bipolar t3;
        bipolar offset = 0;
        bipolar delay = 0;

        if_rare (verdict < 0)
                return verdict;
        if_rare (!sntp_local_ok(t4))
                return SNTP_MALFORMED;
        t2 = sntp_load_stamp(reply + 32);
        t3 = sntp_load_stamp(reply + 40);
        /*
                The reference stamp is when the server last set its own
                clock, so it sits at or before the stamp it transmits. A
                second of slack, because the two are read at different
                moments and a server whose reference is one tick the wrong
                side of transmit would otherwise be refused for ever: the
                sample loop stops on BAD_SERVER, so that server is not asked
                again. A reference of all zeros is a server that does not
                say: RFC 5905 reads it as never synchronised, and a server
                that keeps no such clock sends it at stratum 1 all the same.
                The echo of our own stamp is what says the reply is ours,
                so it is no reason to refuse one, and only one that states a
                time is held to the window.
        */
        if_rare ((network_load_32(reply + 16) | network_load_32(reply + 20)) &&
                 (!sntp_wall_ok(sntp_load_stamp(reply + 16)) ||
                  sntp_load_stamp(reply + 16) >
                      t3 + (bipolar)SNTP_NANOSECONDS))
                return SNTP_BAD_SERVER;
        sntp_offset_delay(t1, t2, t3, t4, address_of offset, address_of delay);
        if_rare (!sntp_sample_sane(t1, t2, t3, t4, offset, delay, most))
                return SNTP_MALFORMED;
        into->offset_ns = offset;
        into->delay_ns = delay;
        into->distance_ns = sntp_distance_ns(delay, network_load_32(reply + 4),
                                             network_load_32(reply + 8));
        into->ok = true;
        return SNTP_OK;
}


/*
        One request and its reply. With nts the request carries NTS's
        extension fields (nts_request), and a reply is only an answer when
        nts_reply authenticates it: anything else is noise, charged to the
        discard allowance like any other, except an NTSN that says the
        cookie is no good. An NTS reply is longer than 48 bytes by design and
        is parsed whole, so the tight tier's exact size is for plain replies;
        one that fills the room is cut and is no answer.
*/
static HOT bipolar sntp_exchange(b32 handle,
                                 network_deadline address_to deadline,
                                 bipolar most, p32 address_to sequence,
                                 nts_exchange address_to nts,
                                 sntp_sample address_to into)
{
        p8 request[NTS_PACKET_ROOM];
        p8 reply[NTS_PACKET_ROOM + 1];
        positive request_length = SNTP_PACKET;
        p64 sent[2];
        p64 got[2];
        p64 spare[2];
        p32 mine;
        bipolar t1;
        bipolar wait;
        bipolar received;
        bipolar verdict;
        positive discarded = 0;
        bool stamped;

        into->ok = false;
        memory_fill(request, 0, SNTP_PACKET);
        request[0] = SNTP_LI_VN_MODE;

        //      The packet is whole, extension fields and their tag included,
        //      before the clock is read for t1: the crypto is not in the
        //      offset (and t1 and t4 are the kernel's stamps regardless).
        if_rare (!sntp_put_stamp(request + 40))
                return SNTP_NO_REPLY;
        if (nts)
        {
                request_length = nts_request(nts, request, sizeof(request));
                if_rare (!request_length)
                        return SNTP_NO_REPLY;
        }
        if_rare (system_call_2(syscall(clock_gettime), CLOCK_REALTIME,
                               (positive)sent) < 0)
                return SNTP_NO_REPLY;
        if_rare (socket_send(handle, request, request_length, 0, 0, 0) < 0)
                return SNTP_NO_REPLY;
        mine = address_to sequence;
        address_to sequence = mine + 1;
        (void)sntp_transmit_stamp(handle, mine, sent);
        t1 = sntp_timespec_ns(sent[0], sent[1]);
        if_rare (!sntp_local_ok(t1))
                return SNTP_NO_REPLY;

        for (;;)
        {
                wait = network_wait_readable_until(handle, deadline);
                if_rare (wait <= 0)
                        return SNTP_NO_REPLY;
                received = sntp_receive_stamped(handle, reply,
                                                nts ? sizeof(reply)
                                                    : SNTP_REPLY_ROOM,
                                                got, address_of stamped);
                if_rare (!stamped &&
                         system_call_2(syscall(clock_gettime), CLOCK_REALTIME,
                                       (positive)got) < 0)
                        return SNTP_NO_REPLY;
                if_rare (received == -ECONNREFUSED || received == -EHOSTUNREACH ||
                         received == -ENETUNREACH)
                        //      ICMP says nobody is there; waiting out the
                        //      deadline would not make it so.
                        return SNTP_NO_SERVER;
                if_rare (received < 0)
                {
                        /*
                                Nothing was readable, so what woke the
                                wait was the error queue: a transmit
                                stamp that was not yet there when the
                                send drained for it. Take it off now --
                                t1 is already decided, so the stamp is
                                of no further use -- because a socket
                                with anything on that queue reports
                                POLLERR, and leaving it would wake this
                                wait again immediately, and again, until
                                the deadline ran out.
                        */
                        (void)sntp_transmit_stamp(handle, mine, spare);
                        if (discarded++ == NETWORK_DISCARD_MAX)
                                return SNTP_NO_REPLY;
                        continue;
                }
                if_rare (received < SNTP_PACKET ||
                         (nts ? received > NTS_PACKET_ROOM
                              : MOONWATER_STRICT_NET >= STRICT_TIGHT &&
                                    received != SNTP_PACKET))
                {
                        if (discarded++ == NETWORK_DISCARD_MAX)
                                return SNTP_NO_REPLY;
                        continue;
                }
                if (nts)
                {
                        bipolar heard = nts_reply(nts, reply, (positive)received);

                        if (heard == NTS_REPLY_NAK)
                                return SNTP_NTS_NAK;
                        if (heard != NTS_REPLY_OK)
                        {
                                if (discarded++ == NETWORK_DISCARD_MAX)
                                        return SNTP_NO_REPLY;
                                continue;
                        }
                }
                verdict = sntp_reply_sample(reply, request, t1,
                                            sntp_timespec_ns(got[0], got[1]),
                                            most, into);
                if_rare (verdict == SNTP_NO_REPLY)
                {
                        if (discarded++ == NETWORK_DISCARD_MAX)
                                return SNTP_NO_REPLY;
                        continue;
                }
                return verdict;
        }
}

static COLD bipolar sntp_query_at(p32 server, p16 port, bool filter,
                                  bipolar most, nts_exchange address_to nts,
                                  sntp_sample address_to answer)
{
        socket_address_internet where = {
            .family = AF_INET,
            .port = network_order_16(port),
            .host = network_order_32(server),
        };
        network_deadline deadline;
        sntp_sample row[SNTP_SAMPLES];
        positive want = filter ? SNTP_SAMPLES : 1;
        b32 want_stamp = 1;
        p32 want_transmit = SNTP_TIMESTAMPING_WANT;
        p32 sequence = 0;
        positive at;
        bipolar handle;
        bipolar best;
        bipolar failed = SNTP_NO_REPLY;

        handle = socket_new(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        if (handle < 0)
                return SNTP_NO_SERVER;
        scope_exit(socket_close((b32)handle));
        if (socket_connect((b32)handle, address_of where, sizeof(where)) < 0)
                return SNTP_NO_SERVER;
        (void)socket_option_set((b32)handle, SOL_SOCKET, SNTP_TIMESTAMPNS,
                                address_of want_stamp, sizeof(want_stamp));
        (void)socket_option_set((b32)handle, SOL_SOCKET, SNTP_TIMESTAMPING,
                                address_of want_transmit,
                                sizeof(want_transmit));

        memory_zero(row, sizeof(row));
        for (at = 0; at < want; at++)
        {
                //      Each sample has its own wait. They used to share one
                //      of SNTP_SECONDS times the count, so a datagram lost
                //      first spent all of it and the samples after it were
                //      sent with no time to be answered in: one lost packet
                //      in four ended a query with no answer.
                if (!network_deadline_begin(address_of deadline, SNTP_SECONDS,
                                            0))
                        break;
                failed = sntp_exchange((b32)handle, address_of deadline, most,
                                       address_of sequence, nts, row + at);
                //      A server that is not there, or has said no, is not
                //      asked again for the rest of the samples; nor is one
                //      that refused the cookie, which needs new keys first.
                if (failed == SNTP_NO_SERVER || failed == SNTP_BAD_SERVER ||
                    failed == SNTP_RATE_LIMITED || failed == SNTP_DENIED ||
                    failed == SNTP_NTS_NAK)
                        break;
        }

        best = sntp_pick(row, want);
        if (best < 0)
                return failed < 0 ? failed : SNTP_NO_REPLY;
        address_to answer = row[best];
        answer->address = server;
        return SNTP_OK;
}

/*
        A time server asked apart. The machine's NTP child asks as root, and
        what a server sends is read by sntp_reply_sample and, with NTS, by
        nts_reply, so the name's lookup, the exchanges and both readers run
        in a confined child (net_apart, with a time query's filter: recvmsg
        and the two timestamp options besides a fetch's calls). It answers
        with its status, the sample it picked and, with NTS, the cookies the
        replies left. The status is believed as one sntp_query_at returns;
        the sample as one sntp_reply_sample could have made (taken, a delay
        within SNTP_DELAY_MOST_NS, a distance not below half of it, an
        offset inside the NTP window, and inside most when the clock is
        synchronised, from an address); the cookies as nts_ke_believed holds
        them. The keys never come back: the caller still has its own.
*/
_Static_assert(SNTP_TIMESTAMPNS == NET_APART_SO_TIMESTAMPNS &&
                   SNTP_TIMESTAMPING == NET_APART_SO_TIMESTAMPING,
               "the time query's filter allows the options it sets");

typedef struct
{
        b64 status;
        sntp_sample sample;
        p64 count;
        p16 length[NTS_COOKIES];
        p8 cookie[NTS_COOKIES][NTS_COOKIE_MAX];
} sntp_apart_said;

typedef struct
{
        string_address name;
        p16 port;
        bool filter;
        bipolar most;
        nts_exchange address_to nts;
} sntp_apart_ask;

static COLD bool sntp_apart_task(address_any context, bipolar answer)
{
        sntp_apart_ask address_to ask = context;
        sntp_apart_said said;
        //      The name or the address as the HTTP client takes it, which is
        //      the one place a host is looked up.
        p32 host = ask->name && ask->name[0] ? http_lookup(ask->name) : 0;
        bool wrote;

        memory_zero(address_of said, sizeof said);
        said.status = host ? sntp_query_at(host, ask->port, ask->filter, ask->most,
                                           ask->nts, address_of said.sample)
                           : SNTP_NO_SERVER;
        if (ask->nts)
        {
                said.count = ask->nts->state->count;
                memory_copy(said.length, ask->nts->state->length,
                            sizeof said.length);
                memory_copy(said.cookie, ask->nts->state->cookie,
                            sizeof said.cookie);
        }
        wrote = system_write_all((positive)answer, address_of said, sizeof said) ==
                sizeof said;
        crypto_forget(address_of said, sizeof said);
        return wrote;
}

static COLD bool sntp_apart_believed(const sntp_apart_said address_to said,
                                     bipolar most, bool nts)
{
        const sntp_sample address_to sample = address_of said->sample;

        if (said->status != SNTP_OK &&
            (said->status > SNTP_ANSWER - 1 || said->status < SNTP_CONTRADICTS))
                return false;
        if (said->status == SNTP_OK &&
            (!sample->ok || !sample->address || sample->delay_ns < 0 ||
             sample->delay_ns > SNTP_DELAY_MOST_NS ||
             sample->distance_ns < sample->delay_ns / 2 ||
             !sntp_within(sample->offset_ns, SNTP_WALL_MOST_NS) ||
             (most <= SNTP_OFFSET_SYNCED_NS &&
              !sntp_within(sample->offset_ns, most))))
                return false;
        if (!nts)
                return !said->count;
        if (said->count > NTS_COOKIES)
                return false;
        for (positive at = 0; at < said->count; at++)
                if (!said->length[at] || said->length[at] > NTS_COOKIE_MAX)
                        return false;
        return true;
}

static COLD bipolar sntp_query_apart(string_address name, p16 port, bool filter,
                                     bipolar most, nts_exchange address_to nts,
                                     sntp_sample address_to answer)
{
        sntp_apart_ask ask = {name, port, filter, most, nts};
        byte_store got = {0};
        sntp_apart_said said;
        bipolar ran = net_apart(sntp_apart_task, address_of ask, -1, address_of got,
                                sizeof said, true);
        bool whole = ran == NET_APART_OK && got.used == sizeof said;
        bipolar status = ran == NET_APART_UNCONFINED ? SNTP_CONFINE : SNTP_APART;

        if (whole)
                memory_copy(address_of said, got.bytes, sizeof said);
        if (got.bytes)
                crypto_forget(got.bytes, got.used);
        byte_store_release(address_of got);
        if (whole && sntp_apart_believed(address_of said, most, nts != null))
        {
                status = (bipolar)said.status;
                if (status == SNTP_OK)
                        address_to answer = said.sample;
                if (nts)
                {
                        nts->state->count = (positive)said.count;
                        memory_copy(nts->state->length, said.length,
                                    sizeof said.length);
                        memory_copy(nts->state->cookie, said.cookie,
                                    sizeof said.cookie);
                }
        }
        crypto_forget(address_of said, sizeof said);
        return status;
}

#endif


#define LOCALE_ZONE_MODE_PATH "/root/timezone.mode"
#define LOCALE_ZONE_NETWORK_PATH HOST_STATE "/timezone.network"
#define LOCALE_NTP_PATH "/root/ntp"
#define LOCALE_NTP_SERVER_PATH "/root/ntp.server"
#define LOCALE_NTP_SAMPLING_PATH "/root/ntp.sampling"
#define LOCALE_NTS_PATH "/root/ntp.nts"
#define LOCALE_NTS_SERVERS_PATH "/root/ntp.nts.servers"
#define LOCALE_NTS_STATE_PATH HOST_STATE "/nts"
#define LOCALE_NTS_FAILED_PATH HOST_STATE "/nts.failed"
#define LOCALE_NTP_LAST_PATH HOST_STATE "/ntp.last"
#define LOCALE_NTP_LAST_ROOM 96
#define LOCALE_KEYBOARD_PATH "/root/keyboard"
#define LOCALE_CANVAS_SCALE_PATH "/root/canvas.scale"
#define LOCALE_CANVAS_MODES_PATH "/root/canvas.modes"
#define LOCALE_NTP_DEFAULT_SERVER "pool.ntp.org"
#define LOCALE_NTP_SERVER_ROOM 80
#define LOCALE_NTP_RETRY_LEAST 1
#define LOCALE_NTP_RETRY_MOST 900
#define LOCALE_NTP_AGAIN 1800
#define LOCALE_NTP_AGAIN_FIRST 256
#define LOCALE_NTP_LEARN_LEAST_NS ((bipolar)60 * 1000000000)
#define LOCALE_NTP_FREQ_MOST ((bipolar)500 << 16)
#define LOCALE_NTP_NOISE_NS ((bipolar)500000)
#define LOCALE_NTP_RATE_AGAIN 300
#define LOCALE_NTP_EXIT_RATE 2
#define LOCALE_NTP_EXIT_LONG 3
#define LOCALE_LOCK_PATH HOST_STATE "/locale.lock"
#define LOCALE_NTP_LOCK_PATH HOST_STATE "/ntp.lock"
#define LOCALE_NTP_STEP_NS ((bipolar)128 * 1000000)
#define LOCALE_NTP_STEP_FIRST_NS ((bipolar)1000000)
#define LOCALE_NTP_TIMECONST 6
#define LOCALE_TIMEX_OFFSET 1
#define LOCALE_TIMEX_FREQUENCY 2
#define LOCALE_TIMEX_CONSTANT 6
/*
        The kernel's own numbering, from uapi/linux/timex.h. ADJ_SETOFFSET
        is 0x0100. It was 0x80 here, which is ADJ_TAI: a request to set the
        TAI offset from the constant word, not to step the clock from the
        time words. The kernel read word 6, which is zero, wrote that as
        the system TAI offset, ignored the offset we had gone to such
        lengths to measure, and returned the clock state -- a number the
        caller reads as success. So every sample, every filter and every
        guard below fed a call that could not set the clock, said it
        had, and cleared STA_UNSYNC on the way out so the machine reported
        itself synchronised.
*/
#define ADJ_OFFSET 0x0001
#define ADJ_FREQUENCY 0x0002
#define ADJ_MAXERROR 0x0004
#define ADJ_ESTERROR 0x0008
#define ADJ_STATUS 0x0010
#define ADJ_TIMECONST 0x0020
#define ADJ_SETOFFSET 0x0100
#define ADJ_NANO 0x2000
#define STA_PLL 0x0001
#define STA_UNSYNC 0x0040
#define STA_FREQHOLD 0x0080
#define STA_NANO 0x2000

/*
        A query that runs in a child of the machine process, and how its
        answer gets back.

        It used to come back as the child's exit status, collected by a
        wait4 on its pid, while the machine loop reaped every child it had
        with wait4(-1): the status was gone before this side asked, and a
        kiss-o-death never slowed anything down. The answer is one byte on a
        pipe, which nothing else in the process reads; a child that dies
        without writing reads as end of file, which is a failure like any
        other. The byte is the last thing the child does, so it is a zombie
        a moment later, and it is waited for here, blocking: the wait that
        did not block found it still running 2,994 times in 3,000 and never
        came back, so a query that ended left its pid, and its pipe, for
        the rest of the boot.
*/
typedef struct
{
        bipolar pid;
        bipolar answer;
} locale_child;

#define LOCALE_CHILD_RUNNING (-1)
#define LOCALE_CHILD_IDLE (-2)

static bipolar locale_child_fork(locale_child address_to child)
{
        b32 ends[2];
        bipolar pid = system_child_fork(ends, O_CLOEXEC | O_NONBLOCK);

        if (pid < 0)
                return pid;
        if (!pid)
        {
                child->pid = 0;
                child->answer = ends[1];
                //      Nothing of the machine process stays open in it: a copy
                //      of a descriptor of its lives as long as the query does,
                //      and the machine's attachment on /dev/spark once lived
                //      with it.
                (void)descriptors_close_except(3, (p32)ends[1], descriptors_none);
                return 0;
        }
        child->pid = pid;
        child->answer = ends[0];
        return pid;
}

static DEAD_END fn locale_child_end(locale_child address_to child, p8 code)
{
        (void)system_write_once(child->answer, address_of code, 1);
        system_call_1(syscall(exit), code);
        __builtin_unreachable();
}

//      A query nobody wants any more, ended and waited for.
static fn locale_child_stop(locale_child address_to child)
{
        if (child->pid <= 0)
                return;
        system_child_reap(child->pid, child->answer, true);
        child->pid = 0;
        child->answer = -1;
}

//      LOCALE_CHILD_IDLE with none running, LOCALE_CHILD_RUNNING while it
//      runs, then the byte it ended with -- once.
static bipolar locale_child_poll(locale_child address_to child)
{
        p8 code = 1;
        bipolar got;

        if (child->pid <= 0)
                return LOCALE_CHILD_IDLE;
        got = system_read_once(child->answer, address_of code, 1);
        if (got == -EAGAIN || got == -EINTR)
                return LOCALE_CHILD_RUNNING;
        //      The byte or the end of file says it is done; any other
        //      answer of read says nothing, and is not waited for.
        system_child_reap(child->pid, child->answer, got < 0);
        child->pid = 0;
        child->answer = -1;
        return got == 1 ? code : 1;
}

//      A query forked to run task, whose answer is the byte it ends with: the
//      child never comes back from here. False when there was no child to
//      make, which the caller waits out as it does a query that failed.
typedef p8 (*locale_task)(address_any context);

static bool locale_child_run(locale_child address_to child, locale_task task,
                             address_any context)
{
        bipolar forked = locale_child_fork(child);

        if (!forked)
                locale_child_end(child, task(context));
        return forked > 0;
}

//      Seconds from now, in the boot clock's nanoseconds.
static p64 locale_after(p64 now, positive seconds)
{
        return now + (p64)seconds * SNTP_NANOSECONDS;
}

//      A wait doubled, to at most the longest it is allowed.
static positive locale_doubled(positive wait, positive most)
{
        return wait * 2 < most ? wait * 2 : most;
}

static p64 locale_ntp_next;
static positive locale_ntp_retry = LOCALE_NTP_RETRY_LEAST;
/*
        When the last query that set the clock ended, and how long until the
        next: the forked query reads the first, which the fork hands it, to
        tell how fast the clock ran. It is the end of the query the machine
        saw and not the start of the walk, which is up to a minute earlier
        and would put that into the frequency it learns.
*/
static p64 locale_ntp_synced;
static positive locale_ntp_every = LOCALE_NTP_AGAIN_FIRST;
static locale_child locale_ntp_child = {0, -1};

static fn locale_ntp_keep(bool online, bool synced, p64 now);
static bipolar locale_ntp_apply(bool by_hand);

//      What the last answer moved the clock by, and who gave it, for the
//      person who asked by hand. The scheduled queries run in a child and
//      nobody reads these there.
static bipolar locale_ntp_moved_ns;
static p8 locale_ntp_answered[LOCALE_NTP_SERVER_ROOM];
//      Why the last NTS server could not be asked, for the person who asked.
static p8 locale_nts_why[112];

/*
        What a setting says to a caller who cannot read it. /root is root's,
        and what a machine nobody has told anything answers -- ntp on, UTC,
        the layout us -- is a fact about that machine and not about a caller
        who could not look: it says so instead.
*/
#define LOCALE_NEEDS_ROOT "(needs root)"

static bool locale_readable(string_address path, p8 address_to into,
                            positive room)
{
        bipolar got = host_read_word(path, into, room);

        return got != -EACCES && got != -EPERM;
}

//      A switch as it is said: on with nothing set, and whatever else it
//      holds is off.
static string_address locale_switch_say(string_address path)
{
        p8 word[16];

        if (!locale_readable(path, word, sizeof(word)))
                return (string_address)LOCALE_NEEDS_ROOT;
        return !word[0] || string_equals(word, "on") ? (string_address) "on"
                                                     : (string_address) "off";
}

static bool locale_switch_on(string_address path)
{
        return !string_equals(locale_switch_say(path), "off");
}

static bool locale_ntp_wanted(void)
{
        return locale_switch_on(LOCALE_NTP_PATH);
}

static bool locale_ntp_sampling_wanted(void)
{
        return locale_switch_on(LOCALE_NTP_SAMPLING_PATH);
}

static bool locale_clock_synced(void)
{
        return logger_clock_synced(null);
}

/*
        What a stored zone is called when it is shown. A country code or a
        zone name comes back as the table spells it; a fixed offset stored as
        "<+0530>-5:30" is shown as UTC+05:30, which is how it was asked for,
        rather than the POSIX spelling that exists to get the sign right.
*/
static fn locale_zone_describe(string_address zone, p8 address_to into,
                               positive room)
{
        string_address named = clock_zone_named(zone);
        positive at = 3;

        if (named)
        {
                string_copy_bounded(into, named, room);
                return;
        }
        if (zone[0] != '<' || room < 16)
        {
                string_copy_bounded(into, zone[0] ? zone : (string_address) "UTC",
                                    room);
                return;
        }
        memory_copy(into, "UTC", 3);
        for (positive in = 1; zone[in] && zone[in] != '>' && at + 2 < room; in++)
        {
                if (in == 4)
                        into[at++] = ':';
                into[at++] = zone[in];
        }
        into[at] = end;
}

/*
        A zone this machine can keep: a name or country code from the table,
        an offset as a clock shows it, or a POSIX zone string the parser takes
        whole. The written form lands in into -- the table's spelling for a
        name, the POSIX form for an offset -- so what is stored means the same
        to every program that reads it, not only to this one.
*/
static bool locale_zone_resolve(string_address name, p8 address_to into,
                                positive room)
{
        string_address named;
        bool parsed;

        if (!name || !name[0])
                return false;
        named = clock_zone_named(name);
        if (named)
        {
                string_copy_bounded(into, named, room);
                return true;
        }
        if (clock_zone_offset(name, into, room))
                return true;

        //      Read whole: what the grammar leaves over is kept as typed in
        //      the file every bowl copies. clock_tz_whole leaves the
        //      process's zone set to what it read, so the one in force is put
        //      back whichever way it goes.
        parsed = string_length(name) < room && clock_tz_whole(name);
        tzset();
        if (!parsed)
                return false;
        string_copy_bounded(into, name, room);
        return true;
}

/*
        Every name the table holds, as the list a person greps: the country
        codes wrapped on a few lines, then one zone a line with what it is
        called. Any tzdata link also works ("Asia/Calcutta", "US/Eastern"),
        and is stored as the zone it points to.
*/
static b32 locale_zone_list(void)
{
        bool code = false;
        positive column = 0;
        string_address name;
        string_address spoken;

        string_format(log, "  codes");
        column = 7;
        for (positive at = 0; (name = clock_zone_known(at, address_of code));
             at++)
        {
                if (code)
                {
                        if (column + 3 > 78)
                        {
                                string_format(log, "\n       ");
                                column = 7;
                        }
                        string_format(log, " %s", name);
                        column += 3;
                        continue;
                }
                if (column)
                {
                        string_format(log, "\n  zones  " TERM_DIM
                                           "tzdata " CLOCK_ZONE_TZDATA
                                           "; any tzdata link works too"
                                           TERM_RESET "\n");
                        column = 0;
                }
                spoken = clock_zone_spoken(name);
                string_format(log, "    %s  " TERM_DIM "%s" TERM_RESET "\n",
                              name, spoken ? spoken : (string_address) "");
        }
        string_format(log, "  offsets  +1  -5  +5:30  UTC+2"
                           TERM_DIM "   +1 is an hour ahead of UTC,"
                           " with no daylight saving" TERM_RESET "\n");
        host_say(log, "  auto     " TERM_DIM "the zone Cloudflare places "
                      "this network in, asked once per network"
                      TERM_RESET "\n");
        return 0;
}

/*
        A zone as a person reads it: the name it is stored under, and what it
        is called -- "Europe/Stockholm, Central European Time". An offset has
        no name in words, so it says what it is instead.
*/
static fn locale_zone_title(string_address zone, p8 address_to into,
                            positive room)
{
        p8 shown[80];
        string_address spoken = clock_zone_spoken(zone[0] ? zone
                                                          : (string_address) "UTC");

        locale_zone_describe(zone, shown, sizeof(shown));
        string_copy_bounded(into, shown, room);
        if (spoken && !string_equals(spoken, shown))
        {
                string_append_bounded(into, ", ", room);
                string_append_bounded(into, spoken, room);
        }
        else if (zone[0] == '<')
                string_append_bounded(into,
                                      ", a fixed offset with no daylight saving",
                                      room);
}

//      The zone in force is the one the machine keeps: a TZ in the caller's
//      environment (sudo's env_keep, ssh's AcceptEnv) is the caller's own, and
//      read here it set the before and after of a change, and the offset the
//      kernel is told, to a zone nobody chose for the machine.
static fn locale_zone_own(void)
{
        (void)unsetenv((string_address) "TZ");
}

//      The wall clock now, in the zone in force, with its abbreviation.
static fn locale_zone_moment(p8 address_to into, positive room)
{
        time_t stamp = time(null);
        tm broken;

        tzset();
        if (!localtime_r(address_of stamp, address_of broken) ||
            !strftime(into, room, "%Y-%m-%d %H:%M:%S %Z", address_of broken))
                string_copy_bounded(into, "(the clock could not be read)", room);
}

/*
        How the zone came to be what it is, as /root/timezone.mode says:
        "manual" after moonwater timezone ZONE, "auto" and where the answer
        came from once the network has been asked.

        With no mode file the zone file decides. Before auto existed the only
        way to get a /root/timezone was a person typing one, so a machine
        carrying one from then is manual -- auto would otherwise overwrite
        the zone they chose with wherever Cloudflare places them -- and a
        machine with neither has never been told anything, which is auto.
*/
static fn locale_zone_mode(p8 address_to into, positive room)
{
        p8 zone[80];

        host_read_word(LOCALE_ZONE_MODE_PATH, into, room);
        if (into[0])
                return;
        host_read_word(CLOCK_ZONE_PATH, zone, sizeof(zone));
        if (zone[0])
                string_copy_bounded(into, "manual", room);
}

static bool locale_zone_manual(void)
{
        p8 mode[48];

        locale_zone_mode(mode, sizeof(mode));
        return string_has_prefix(mode, "manual");
}

static fn locale_zone_how(p8 address_to into, positive room)
{
        p8 mode[48];

        locale_zone_mode(mode, sizeof(mode));
        if (string_has_prefix(mode, "manual"))
                string_copy_bounded(into, "(manual)", room);
        else if (string_has_prefix(mode, "auto cloudflare"))
                string_copy_bounded(into, "(auto, from Cloudflare)", room);
        else if (string_has_prefix(mode, "auto country ") && mode[13])
        {
                string_copy_bounded(into, "(auto, from country ", room);
                string_append_bounded(into, mode + 13, room);
                string_append_bounded(into, ")", room);
        }
        else
                string_copy_bounded(into, "(auto, waiting for the network)",
                                    room);
}

//      The zone as it is shown and how it came to be, or false when /root is
//      not the caller's to read.
static bool locale_zone_read(p8 address_to title, positive title_room,
                             p8 address_to how, positive how_room)
{
        p8 zone[80];

        if (!locale_readable(CLOCK_ZONE_PATH, zone, sizeof(zone)))
                return false;
        locale_zone_title(zone, title, title_room);
        locale_zone_how(how, how_room);
        return true;
}

static b32 locale_zone_status(void)
{
        p8 title[128];
        p8 how[48];
        p8 when[48];

        if (!locale_zone_read(title, sizeof(title), how, sizeof(how)))
        {
                host_say(log, host_label "timezone " LOCALE_NEEDS_ROOT "\n");
                return 0;
        }
        locale_zone_moment(when, sizeof(when));
        string_format(log, host_label "timezone %s " TERM_DIM "%s" TERM_RESET
                                      "\n", title, how);
        host_say(log, "  local %s\n", when);
        return 0;
}

/*
        The kernel's own idea of the zone, sys_tz.

        Almost nothing reads it -- programs here read /root/timezone and
        bowls read their /etc/localtime -- but FAT does: every timestamp on
        the EFI partition and on a USB stick is written in local time using
        it, so left at zero a file saved at noon in Stockholm shows as ten in
        the morning on the Mac or Windows machine it is carried to. It is a
        plain offset, so it is re-applied whenever the offset changes, which
        includes the two nights a year daylight saving does.

        The trap is the first call. The first settimeofday that carries a
        zone and no time makes Linux decide the hardware clock keeps local
        time and move the system clock by the offset -- an hour into the
        future for Sweden, the moment the zone is set. Passing a zero zone
        first spends that first call on nothing, because the kernel only
        warps when the offset is not zero. hwclock does the same.
*/
#define LOCALE_ZONE_NOT_APPLIED ((bipolar)1 << 40)

static bipolar locale_zone_kernel_east = LOCALE_ZONE_NOT_APPLIED;
static bool locale_zone_kernel_first_spent;

static bipolar locale_zone_east_now(void)
{
        time_t stamp = time(null);
        tm broken;

        if (!localtime_r(address_of stamp, address_of broken))
                return 0;
        return (bipolar)broken.tm_gmtoff;
}

static bipolar locale_zone_kernel(void)
{
        b32 zone[2] = {0, 0};
        bipolar east = locale_zone_east_now();
        bipolar failed;

        if (east == locale_zone_kernel_east)
                return 0;
        if (!locale_zone_kernel_first_spent)
        {
                failed = system_call_2(syscall(settimeofday), 0,
                                       (positive)zone);
                if (failed < 0)
                        return failed;
                locale_zone_kernel_first_spent = true;
        }
        zone[0] = (b32)(-east / 60);
        failed = system_call_2(syscall(settimeofday), 0, (positive)zone);
        if (failed < 0)
                return failed;
        locale_zone_kernel_east = east;
        return 0;
}

//      The zone, everywhere that does not read /root/timezone for itself:
//      the kernel, and every bowl's /etc/localtime. Says where it went.
static fn locale_zone_list_add(p8 address_to into, positive room,
                                string_address what)
{
        if (into[0])
                string_append_bounded(into, ", ", room);
        string_append_bounded(into, what, room);
}

static fn locale_zone_apply(void)
{
        positive refused = 0;
        bipolar kernel = locale_zone_kernel();
        b32 host = bowl_write_localtime_host();
        positive bowls = bowl_write_localtime_all(address_of refused);
        p8 took[96] = {0};
        p8 kept[128] = {0};
        p8 count[32];
        positive at;

        if (kernel >= 0)
                locale_zone_list_add(took, sizeof(took), "the kernel");
        else
        {
                locale_zone_list_add(kept, sizeof(kept), "the kernel (");
                string_append_bounded(kept, file_reason(kernel), sizeof(kept));
                string_append_bounded(kept, ")", sizeof(kept));
        }
        locale_zone_list_add(host ? kept : took, host ? sizeof(kept) : sizeof(took),
                             "this system");
        for (positive side = 0; side < 2; side++)
        {
                positive n = side ? refused : bowls;

                if (!n)
                        continue;
                at = positive_into(count, n);
                string_copy_bounded(count + at, n == 1 ? " bowl" : " bowls",
                                    sizeof(count) - at);
                locale_zone_list_add(side ? kept : took,
                                     side ? sizeof(kept) : sizeof(took), count);
        }
        if (took[0])
                string_format(log, "  applied to  %s\n", took);
        if (kept[0])
                string_format(log, "  refused by  %s\n", kept);
}

/*
        The zone and how it came to be, as one change. Under a lock, so that
        the machine's own query -- which looks at the mode and writes the
        zone it was told, and meant to leave a zone somebody typed alone --
        cannot do both between a hand's two writes and leave Cloudflare's
        zone with the word manual beside it; and in the order that leaves
        the least when it is cut short: a zone somebody chose is marked
        manual first, so that a crash after that leaves the old zone with
        the word that keeps it, and one the network chose is written first,
        so that nobody reads the new word beside the old zone. A write made
        by the machine says -EBUSY of a zone that has gone manual since it
        asked.
*/
static bipolar locale_zone_write(string_address zone, string_address mode,
                                 bool by_hand)
{
        bipolar lock = host_lock(LOCALE_LOCK_PATH, true);
        bool manual = string_has_prefix(mode, "manual");
        bipolar failed = 0;

        if (lock < 0)
                return lock;
        if (!by_hand && locale_zone_manual())
                failed = -EBUSY;
        else
        {
                if (manual)
                        failed = radio_write_word(LOCALE_ZONE_MODE_PATH, mode);
                //      No zone is the mode alone: auto asked of a zone that
                //      was typed keeps the zone until the network names one.
                if (failed >= 0 && zone)
                        failed = radio_write_word(CLOCK_ZONE_PATH, zone);
                if (failed >= 0 && !manual)
                        failed = radio_write_word(LOCALE_ZONE_MODE_PATH, mode);
        }
        radio_unlock(lock);
        return failed;
}

//      Setting prints what the clock now reads, so a wrong zone -- an offset
//      with the sign the other way round, a country in the wrong half of a
//      continent -- is visible at the moment it is set, not at the next
//      meeting.
static b32 locale_zone_store(string_address zone, string_address mode)
{
        p8 was[80];
        p8 before[48];
        p8 after[48];
        p8 title[128];
        p8 how[48];
        p8 old_shown[80];
        bipolar failed;

        host_read_word(CLOCK_ZONE_PATH, was, sizeof(was));
        locale_zone_describe(was, old_shown, sizeof(old_shown));
        locale_zone_moment(before, sizeof(before));

        failed = locale_zone_write(zone, mode, true);
        return_if(failed < 0, host_fail("timezone", failed));

        locale_zone_moment(after, sizeof(after));
        locale_zone_title(zone, title, sizeof(title));
        locale_zone_how(how, sizeof(how));
        string_format(log, host_label "timezone %s " TERM_DIM "%s" TERM_RESET
                                      "\n", title, how);
        string_format(log, "  before  %s  " TERM_DIM "%s" TERM_RESET "\n",
                      before, old_shown);
        string_format(log, "  after   %s\n", after);
        locale_zone_apply();
        log_flush();
        return 0;
}

static b32 locale_zone_set(string_address name)
{
        p8 zone[80];

        if (!radio_text_plain(name, string_length(name)) ||
            !locale_zone_resolve(name, zone, sizeof(zone)))
                return host_refuse("unknown timezone %s -- "
                                   "moonwater timezone list shows the names\n",
                                   name);
        return locale_zone_store(zone, "manual");
}

/*
        Auto: the zone the network says this machine is in.

        Cloudflare geolocates every request it answers, and its speed test
        says what it found in a response header: speed.cloudflare.com's
        /__down?bytes=0 comes back empty with "timezone: Europe/Stockholm"
        and "country: SE". That endpoint is the speed test's own and nobody
        promised it, so there are two ways down from it: the country it gave,
        read as the zone most of that country's people live in, and failing
        that /cdn-cgi/trace, which every Cloudflare host serves and which
        says "loc=SE". A name the table does not hold is never stored -- it
        falls to the country, and with nothing at all the zone stays what it
        was.

        One request, over TLS, is sent when a network appears: at boot once
        there is a default route, and again when the route's interface,
        gateway or gateway's hardware address changes -- a new lease, another
        wifi network. Not more often than every three minutes, backing off to
        an hour while it fails, and never in manual mode. The request carries
        nothing but the connection itself, and Cloudflare sees that address
        either way; moonwater timezone ZONE turns it off.

        The ask runs in a child of the machine process, like the NTP query,
        after the clock has been set if it is going to be, since a clock at
        the epoch fails every certificate. What the answer was asked for is
        kept in /run/moonwater/timezone.network so a hand-run
        moonwater timezone auto and the machine do not ask twice.
*/
#define LOCALE_AUTO_URL "https://speed.cloudflare.com/__down?bytes=0"
#define LOCALE_AUTO_TRACE_URL "https://cloudflare.com/cdn-cgi/trace"
#define LOCALE_AUTO_LEAST 180
#define LOCALE_AUTO_MOST 3600
#define LOCALE_AUTO_CLOCK_WAIT 60
#define LOCALE_NETWORK_ROOM 96
#define LOCALE_ROUTE_GATEWAY 0x2

typedef struct
{
        p8 zone[64];
        p8 mode[32];
} locale_auto_answer;

/*
        Which network this is: the default route's interface and gateway,
        and the gateway's hardware address when the neighbour table has it.
        Empty with no default route. Two networks that both hand out
        192.168.1.1 differ in the last part, which is why it is there.
        Whether there is any route at all is the answer: a machine with an
        address and no gateway is on a network, though nothing that lives
        past it can be asked.
*/
static bool locale_network(p8 address_to into, positive room)
{
        bool any = false;
        p8 table[4096];
        p8 gateway[16] = {0};
        p8 device[20] = {0};
        bipolar got;
        string_address line;

        into[0] = end;
        got = host_read_kernel("/proc/net/route", table, sizeof(table));
        if (got <= 0)
                return false;
        for (line = (string_address)table; line && line[0];)
        {
                string_address next = string_first_of(line, '\n');
                p8 word[5][24];
                positive words = 0;
                string_address at = line;

                if (next)
                        address_to(p8 address_to)next = end;
                while (words < 5 && at[0])
                {
                        positive length = 0;

                        at += string_span(at, string_set_blanks);
                        while (at[0] && at[0] != ' ' && at[0] != '\t' &&
                               length + 1 < sizeof(word[0]))
                                word[words][length++] = (p8)(at++)[0];
                        word[words][length] = end;
                        if (length)
                                words++;
                }
                any |= words >= 4 && !string_equals(word[0], "Iface");
                //      Iface Destination Gateway Flags: a default route
                //      through a gateway.
                if (words >= 4 && string_equals(word[1], "00000000") &&
                    ((positive)string_to_number_unsigned(word[3], null, 16) &
                     LOCALE_ROUTE_GATEWAY))
                {
                        string_copy_bounded(device, word[0], sizeof(device));
                        string_copy_bounded(gateway, word[2], sizeof(gateway));
                        break;
                }
                line = next ? next + 1 : null;
        }
        if (!device[0])
                return any;

        string_copy_bounded(into, device, room);
        string_append_bounded(into, " ", room);
        string_append_bounded(into, gateway, room);

        //      /proc/net/arp names the gateway dotted, the route in the
        //      kernel's own byte order as hex; read the one as the other.
        {
                p32 raw = (p32)string_to_number_unsigned(gateway, null, 16);
                p8 dotted[20];
                positive at = 1;

                dotted[0] = '\n';
                at += host_into(dotted + at, bytes_reverse_32(raw));
                dotted[at++] = ' ';
                dotted[at] = end;
                got = host_read_kernel("/proc/net/arp", table, sizeof(table));
                if (got > 0)
                {
                        //      IP address, HW type, Flags, HW address: the
                        //      first colon on the gateway's line is two
                        //      digits into its hardware address.
                        p8 address_to row = (p8 address_to)memory_search(
                            table, (positive)got, dotted, string_length(dotted));
                        p8 address_to stop = row ? (p8 address_to)memory_search(
                                                       row + 1,
                                                       (positive)(table + got - row - 1),
                                                       "\n", 1)
                                                 : null;
                        positive span = row ? (positive)((stop ? stop : table + got) - row)
                                            : 0;
                        p8 address_to colon = row ? (p8 address_to)memory_search(
                                                        row, span, ":", 1)
                                                  : null;

                        if (colon && colon + 15 <= row + span &&
                            memory_compare(colon - 2, "00:00:00:00:00:00", 17))
                        {
                                p8 mac[18];

                                memory_copy(mac, colon - 2, 17);
                                mac[17] = end;
                                string_append_bounded(into, " ", room);
                                string_append_bounded(into, mac, room);
                        }
                }
        }
        return true;
}

/*
        The same network: the same interface and gateway, and the same
        hardware address unless one side has not learned it yet -- the
        neighbour table fills in after the first packet, and that is not a
        new network.
*/
static positive locale_network_route(string_address text)
{
        positive at = 0;
        positive spaces = 0;

        while (text[at] && !(text[at] == ' ' && ++spaces == 2))
                at++;
        return at;
}

static bool locale_network_same(string_address now, string_address asked)
{
        positive a = locale_network_route(now);
        positive b = locale_network_route(asked);

        if (!now[0] || !asked[0] || a != b || memory_compare(now, asked, a))
                return false;
        if (!now[a] || !asked[b])
                return true;
        return string_equals(now + a, asked + b);
}

//      A header's value or a trace line's, if it is a plain word that fits.
static bool locale_auto_word(string_address value, positive length,
                             p8 address_to into, positive room)
{
        if (!value || !length || length >= room)
                return false;
        for (positive at = 0; at < length; at++)
        {
                p8 c = (p8)value[at];

                if (!byte_is_alnum(c) && c != '/' && c != '_' && c != '-' &&
                    c != '+')
                        return false;
        }
        memory_copy(into, value, length);
        into[length] = end;
        return true;
}

static bool locale_auto_country(string_address code,
                                locale_auto_answer address_to answer)
{
        string_address zone = clock_zone_country(code);

        if (!zone)
                return false;
        string_copy_bounded(answer->zone, zone, sizeof(answer->zone));
        string_copy_bounded(answer->mode, "auto country ", sizeof(answer->mode));
        string_append_bounded(answer->mode, code, sizeof(answer->mode));
        memory_to_upper_ascii(answer->mode + 13, string_length(answer->mode + 13));
        return true;
}

/*
        What an answer says, read the way everything from the network is:
        each word checked for shape, then required to be a name the table
        holds before it is believed. The speed test's headers first -- its
        zone, else its country -- and the trace's loc= line otherwise.
*/
static bool locale_auto_from_headers(p8 address_to head, positive length,
                                     locale_auto_answer address_to answer)
{
        p8 word[64];
        positive size = 0;
        string_address value;
        string_address named;

        value = http_header(head, length, (string_address) "timezone",
                            address_of size, null);
        if (locale_auto_word(value, size, word, sizeof(word)) &&
            string_first_of(word, '/') && (named = clock_zone_named(word)))
        {
                string_copy_bounded(answer->zone, named, sizeof(answer->zone));
                string_copy_bounded(answer->mode, "auto cloudflare",
                                    sizeof(answer->mode));
                return true;
        }
        value = http_header(head, length, (string_address) "country",
                            address_of size, null);
        return locale_auto_word(value, size, word, sizeof(word)) &&
               locale_auto_country(word, answer);
}

static bool locale_auto_from_trace(p8 address_to body, positive length,
                                   locale_auto_answer address_to answer)
{
        p8 word[8];
        positive at = 0;

        while (at + 4 <= length)
        {
                positive stop = at + memory_span_without_byte(body + at, '\n',
                                                              length - at);

                if (!memory_compare(body + at, "loc=", 4))
                {
                        positive size = stop - at - 4;

                        if (size && body[at + 4 + size - 1] == '\r')
                                size--;
                        return locale_auto_word((string_address)(body + at + 4),
                                                size, word, sizeof(word)) &&
                               locale_auto_country(word, answer);
                }
                at = stop + 1;
        }
        return false;
}

//      The whole question, and nothing stored: the zone and how it was
//      found, or the reason none was.
static bipolar locale_auto_ask(locale_auto_answer address_to answer)
{
        http_buffer head = {0};
        http_buffer body = {0};
        bipolar status;
        bool found;

        memory_zero(answer, sizeof(address_to answer));
        status = http_run((string_address)LOCALE_AUTO_URL,
                          address_of http_manners_zone, true, -1,
                          address_of body, null, null, address_of head);
        found = !status &&
                locale_auto_from_headers(head.bytes, head.used, answer);
        byte_store_release(address_of head);
        byte_store_release(address_of body);
        if (found)
                return 0;
        status = http_run((string_address)LOCALE_AUTO_TRACE_URL,
                          address_of http_manners_zone, true, -1,
                          address_of body, null, null, null);
        found = !status &&
                locale_auto_from_trace(body.bytes, body.used, answer);
        byte_store_release(address_of body);
        return status ? status : found ? 0 : HTTP_MALFORMED;
}

/*
        The question, apart. The machine asks it as root, so all of it -- the
        TLS, the HTTP and the reading of the headers and the trace -- runs in
        a confined child (net_apart), and what comes back is a status and two
        words. They are believed only as names the table holds: the zone as
        clock_zone_named spells it, the country through locale_auto_country
        again. What this process writes and logs is the table's spelling,
        never the child's bytes.
*/
typedef struct
{
        b64 status;
        locale_auto_answer answer;
} locale_auto_said;

static bool locale_auto_ask_task(address_any context, bipolar answer)
{
        locale_auto_said said;

        (void)context;
        memory_zero(address_of said, sizeof said);
        said.status = locale_auto_ask(address_of said.answer);
        return system_write_all((positive)answer, address_of said, sizeof said) ==
               sizeof said;
}

//      What the child said, as far as it can be believed: its status when
//      http_run could have returned it, and a zone only as the table spells
//      it -- a zone name, or a country code that names one.
static bipolar locale_auto_believe(locale_auto_said address_to said,
                                   locale_auto_answer address_to answer)
{
        string_address named;

        memory_zero(answer, sizeof(address_to answer));
        if (said->status)
                return http_apart_status(said->status) ? (bipolar)said->status
                                                       : HTTP_APART;
        said->answer.zone[sizeof(said->answer.zone) - 1] = end;
        said->answer.mode[sizeof(said->answer.mode) - 1] = end;
        if (string_equals(said->answer.mode, (string_address) "auto cloudflare"))
        {
                named = clock_zone_named(said->answer.zone);
                if (!named || !string_equals(named, said->answer.zone))
                        return HTTP_MALFORMED;
                string_copy_bounded(answer->zone, named, sizeof(answer->zone));
                string_copy_bounded(answer->mode, "auto cloudflare",
                                    sizeof(answer->mode));
                return 0;
        }
        if (string_length(said->answer.mode) == 15 &&
            !memory_compare(said->answer.mode, "auto country ", 13) &&
            locale_auto_country(said->answer.mode + 13, answer))
                return 0;
        memory_zero(answer, sizeof(address_to answer));
        return HTTP_MALFORMED;
}

static bipolar locale_auto_ask_apart(locale_auto_answer address_to answer)
{
        byte_store got = {0};
        locale_auto_said said;
        bipolar ran = net_apart(locale_auto_ask_task, null, -1, address_of got,
                                sizeof said, false);
        bool whole = ran == NET_APART_OK && got.used == sizeof said;

        memory_zero(answer, sizeof(address_to answer));
        if (whole)
                memory_copy(address_of said, got.bytes, sizeof said);
        byte_store_release(address_of got);
        if (!whole)
                return ran == NET_APART_UNCONFINED ? HTTP_CONFINE : HTTP_APART;
        return locale_auto_believe(address_of said, answer);
}

static string_address locale_auto_reason(bipolar status)
{
        switch (status)
        {
        case HTTP_NO_HOST:
                return (string_address) "the name did not resolve";
        case HTTP_NO_ROUTE:
                return (string_address) "no route to it";
        case HTTP_TLS:
        case HTTP_UNTRUSTED:
        case HTTP_EXPIRED:
        case HTTP_NOT_YET:
        case HTTP_MISMATCH:
        case HTTP_UNCHECKED:
                return (string_address) "TLS failed -- is the clock right?";
        case HTTP_REVOKED:
                return (string_address) "its certificate is revoked";
        case HTTP_UNLOGGED:
                return (string_address) "its certificate was not logged (CT)";
        case HTTP_OVERLAY:
                return (string_address) "what moonwater security published does not read";
        case HTTP_CLOCK:
                return (string_address) "the clock is unset";
        case HTTP_STATUS:
                return (string_address) "it answered with an error";
        case HTTP_MALFORMED:
                return (string_address) "the answer named no zone";
        case HTTP_CONFINE:
                return (string_address) "the question could not be confined here";
        case HTTP_APART:
                return (string_address) "the confined question died";
        }
        return (string_address) "no answer";
}

static fn locale_auto_asked(string_address network)
{
        if (!network || !network[0])
        {
                system_remove_at(AT_FDCWD, LOCALE_ZONE_NETWORK_PATH, 0);
                return;
        }
        host_state_ready();
        radio_write_word(LOCALE_ZONE_NETWORK_PATH, network);
}

/*
        The zone a network answer names, put where a zone goes. By hand it
        says what it did the way moonwater timezone ZONE does; in the
        machine's child it only writes, and the machine's own loop carries
        the new offset to the kernel on its next turn.
*/
static bipolar locale_auto_take(locale_auto_answer address_to answer,
                                bool loud)
{
        p8 was[80];
        p8 mode[48];

        host_read_word(CLOCK_ZONE_PATH, was, sizeof(was));
        host_read_word(LOCALE_ZONE_MODE_PATH, mode, sizeof(mode));
        if (loud)
                return locale_zone_store(answer->zone, answer->mode) ? -1 : 0;
        if (string_equals(was, answer->zone) &&
            string_equals(mode, answer->mode))
                return 0;
        if (locale_zone_write(answer->zone, answer->mode, false) < 0)
                return -1;
        tzset();
        (void)bowl_write_localtime_host();
        (void)bowl_write_localtime_all(null);
        {
                string_address line[] = {"timezone ", answer->zone, " (",
                                         answer->mode, ")", null};

                host_kmsg(line);
        }
        return 0;
}

//      moonwater timezone auto, and the auto half of time sync.
static b32 locale_zone_auto(void)
{
        locale_auto_answer answer;
        p8 network[LOCALE_NETWORK_ROOM];
        p8 zone[80];
        bipolar status;

        if (locale_zone_manual())
        {
                bipolar kept = locale_zone_write(null, "auto", true);

                return_if(kept < 0, host_fail("timezone", kept));
        }
        locale_network(network, sizeof(network));
        status = locale_auto_ask_apart(address_of answer);
        if (status)
        {
                locale_auto_asked(null);
                host_read_word(CLOCK_ZONE_PATH, zone, sizeof(zone));
                host_say(log_error, host_label "timezone auto: Cloudflare "
                                    "could not be asked (%s); keeping %s "
                                    "until the network answers\n",
                         locale_auto_reason(status),
                         zone[0] ? (string_address)zone
                                 : (string_address) "UTC");
                return 1;
        }
        if (locale_auto_take(address_of answer, true) < 0)
                return 1;
        locale_auto_asked(network);
        return 0;
}

static p64 locale_auto_next;
static positive locale_auto_wait = LOCALE_AUTO_LEAST;
static locale_child locale_auto_child = {0, -1};

//      The question put to Cloudflare in the child, for the network it was
//      forked on: 0 when the zone was taken.
static p8 locale_auto_task(address_any network)
{
        locale_auto_answer answer;
        bool took = !locale_auto_ask_apart(address_of answer) &&
                    !locale_auto_take(address_of answer, false);

        if (took)
                locale_auto_asked((string_address)network);
        return took ? 0 : 1;
}

//      A question that was not put or not answered waits, longer each time.
static fn locale_auto_failed(p64 now)
{
        locale_auto_next = locale_after(now, locale_auto_wait);
        locale_auto_wait = locale_doubled(locale_auto_wait, LOCALE_AUTO_MOST);
}

static fn locale_auto_keep(bool ntp_wanted, bool synced, p64 now,
                           string_address network)
{
        bipolar ended = locale_child_poll(address_of locale_auto_child);
        p8 asked[LOCALE_NETWORK_ROOM];

        if (ended == LOCALE_CHILD_RUNNING)
                return;
        if (ended != LOCALE_CHILD_IDLE)
        {
                if (ended)
                        locale_auto_failed(now);
                else
                {
                        locale_auto_next = locale_after(now, LOCALE_AUTO_LEAST);
                        locale_auto_wait = LOCALE_AUTO_LEAST;
                }
                return;
        }
        //      With no network there is nothing to ask, and nothing to read
        //      a file for.
        if (!network[0])
                return;
        //      The network is looked at once in a while and not at every
        //      turn: what it was asked about and whether the zone was typed
        //      are two files read, and neither changes without a new network
        //      -- which clears this wait -- or a hand that has asked itself.
        if (locale_auto_next && now < locale_auto_next)
                return;
        host_read_word(LOCALE_ZONE_NETWORK_PATH, asked, sizeof(asked));
        if (locale_network_same(network, asked))
        {
                //      The gateway's address arrived after the answer did.
                if (string_length(network) > string_length(asked))
                        locale_auto_asked(network);
                locale_auto_next = locale_after(now, LOCALE_AUTO_LEAST);
                return;
        }
        //      A certificate is only as good as the clock that checks it:
        //      while NTP is on and has not set it, wait a minute for it.
        if (ntp_wanted && !synced &&
            now < locale_after(0, LOCALE_AUTO_CLOCK_WAIT))
                return;
        if (locale_zone_manual())
        {
                locale_auto_next = locale_after(now, LOCALE_AUTO_LEAST);
                return;
        }

        if (!locale_child_run(address_of locale_auto_child, locale_auto_task,
                              (address_any)network))
                locale_auto_failed(now);
}

/*
        moonwater time: the clock now, and by hand what otherwise happens on
        its own -- a network query, and the zone put back everywhere.
*/
static b32 locale_time_status(void)
{
        time_t stamp;
        tm broken;
        p8 when[40];

        locale_zone_status();
        stamp = time(null);
        gmtime_r(address_of stamp, address_of broken);
        strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", address_of broken);
        string_format(log, "  utc   %s\n", when);
        if (stamp < 0 || (p64)stamp < clock_trust_floor())
                host_say(log, "  clock unset: it reads earlier than this "
                              "system's build or the last time it was sure, "
                              "so certificates cannot be checked;\n"
                              "  moonwater time sync sets it\n");
        host_say(log, "  ntp %s, %s\n", locale_switch_say(LOCALE_NTP_PATH),
                 locale_clock_synced() ? "synchronised" : "waiting");
        return 0;
}

static b32 locale_time_sync(void)
{
        bipolar failed;

        locale_ntp_moved_ns = 0;
        locale_ntp_answered[0] = end;
        failed = locale_ntp_apply(true);
        if (failed == SNTP_BELOW_FLOOR)
                string_format(log, host_label "time: %s answered earlier than "
                                   "this machine has already been (its build "
                                   "or the last time it was sure), so the "
                                   "clock was left alone; remove "
                                   "/root/clock.good if that is wrong\n",
                              locale_ntp_answered);
        else if (failed < 0 && locale_ntp_answered[0])
                string_format(log, host_label "time: %s answered, but the "
                                   "clock could not be set: %s\n",
                              locale_ntp_answered,
                              failed == SNTP_MALFORMED
                                  ? (string_address) "the time it gave is "
                                                     "outside the years this "
                                                     "machine accepts"
                                  : file_reason(failed));
        else if (failed == SNTP_CONTRADICTS)
                string_format(log, host_label "time: %s answered, but it is "
                                   "further from the time NTS gave this boot "
                                   "than the clock can have drifted, so the "
                                   "clock was left alone\n",
                              locale_ntp_answered);
        else if (failed == SNTP_UNCONFIRMED)
                string_format(log, host_label "time: no second server agreed "
                                   "with a step past 2 s, so the clock was "
                                   "left alone\n");
        else if (failed < 0)
        {
                string_format(log, host_label "time: no server answered%s\n",
                              failed == SNTP_RATE_LIMITED
                                  ? " -- asked too often, try in a minute"
                              : failed == SNTP_DENIED
                                  ? " -- a server told this machine to stay away"
                              : failed == SNTP_CONFINE
                                  ? " -- the query cannot be confined here, so "
                                    "nothing was asked"
                              : failed == SNTP_APART
                                  ? " -- the confined query died"
                                  : "");
                if (locale_nts_why[0])
                        string_format(log, "  NTS %s\n", locale_nts_why);
        }
        else
        {
                bipolar moved = locale_ntp_moved_ns;
                positive whole = (positive)(moved < 0 ? -moved : moved);
                p8 fraction[4];

                positive_into_padded(fraction, whole / 1000000 % 1000, 3, '0');
                fraction[3] = end;
                //      Under a millisecond reads as +0.000, not -0.000.
                string_format(log, host_label "time moved %s%p.%s s by %s\n",
                              moved < 0 && whole >= 1000000 ? "-" : "+",
                              whole / 1000000000,
                              fraction, locale_ntp_answered);
        }
        locale_zone_kernel_east = LOCALE_ZONE_NOT_APPLIED;
        log_flush();
        //      In auto the zone is asked for again as well, which is the one
        //      Cloudflare request a person can make by hand; it prints its
        //      own before and after and applies what it found.
        if (!locale_zone_manual() && !locale_zone_auto())
                return failed < 0 ? 1 : 0;
        locale_zone_status();
        locale_zone_apply();
        log_flush();
        return failed < 0 ? 1 : 0;
}

/*
        The root distance, in the microseconds the kernel keeps its error
        bounds in: half the round trip and the server's own error. That is
        how far the offset can be from the truth, and it is what the kernel
        is told the clock's error now is.

        Telling it is not optional. The kernel adds 500 microseconds a second
        to its maximum error and marks the clock unsynchronised the moment
        that passes sixteen seconds -- and a clock nobody gave an error to
        sits at sixteen seconds already. Every correction here cleared
        STA_UNSYNC and never set ADJ_MAXERROR, so the next second put the
        flag back: moonwater ntp said waiting a second after any answer, and
        the machine, reading that as a failure, asked the pool again every
        few seconds for as long as it ran. With the error given, the flag
        stays down for the eight hours it takes 500 us/s to grow a
        millisecond into sixteen seconds, and the next query comes long
        before that.
*/
#define LOCALE_TIMEX_ESTERROR 4
#define LOCALE_NTP_ERROR_MOST_US 1000000

static CONST positive locale_ntp_error_us(bipolar distance_ns)
{
        bipolar us = (distance_ns < 0 ? 0 : distance_ns) / 1000 + 1;

        return (positive)(us > LOCALE_NTP_ERROR_MOST_US
                              ? LOCALE_NTP_ERROR_MOST_US
                              : us);
}

static fn locale_clock_mark_synced(positive error_us)
{
        positive words[LOGGER_TIMEX_WORDS] = {0};

        words[0] = ADJ_STATUS | ADJ_MAXERROR | ADJ_ESTERROR;
        words[LOGGER_TIMEX_STATUS] = STA_PLL | STA_FREQHOLD;
        words[LOGGER_TIMEX_MAXERROR] = error_us;
        words[LOCALE_TIMEX_ESTERROR] = error_us;
        system_call_1(syscall(adjtimex), (positive)words);
}

/*
        A correction applied once and then left alone is only right at
        the moment it lands. What carries the clock between polls is a
        crystal, and LOCALE_NTP_AGAIN is half an hour: ten parts per
        million, which is an ordinary one, is eighteen milliseconds of
        drift by the next query. That is three orders of magnitude past
        every other error on this path put together, and no amount of
        care measuring the offset touches any of it. Stepping and then
        free-running for 1800 seconds spends the whole measurement in
        the first instant and then throws it away.

        The kernel keeps a phase-locked loop for exactly this, and it
        keeps its frequency estimate across our polls -- which is what
        this program needs, because the query runs in a forked child
        that exits, so nothing held in memory survives to the next one.
        Handing the offset to that loop with ADJ_OFFSET and STA_PLL lets
        the kernel steer the clock's phase; how fast it runs is learned
        below instead (locale_ntp_learned), since the loop was measured
        learning it far too slowly, and polls start at four minutes and
        double to half an hour while that is being learned.

        A step is still right when the clock is far out. Slewing never
        moves time backwards, which is what a log, a build and a file
        timestamp all want, but the kernel slews at a bounded rate, so a
        large offset would take longer to walk off than the gap between
        polls. The split is at 128 ms, where ntpd puts it.

        A step also cancels any slew still in progress, with an
        ADJ_OFFSET of zero in the same request: the pending phase
        adjustment was computed against a clock this request is about to
        move, and applying both would correct twice.
*/
/*
        The first answer after boot is stepped to from a millisecond out,
        not slewed to: the clock then was set from a real-time clock that
        keeps whole seconds, so it starts some tens of milliseconds wrong,
        and the kernel slews those away at its own pace -- measured, 86 ms
        took nineteen minutes to come within one. Nothing has read the time
        yet to see it move, which is when ntpd -g and chrony's makestep
        step too. After that, only 128 ms is worth a step.
*/
static bool locale_ntp_first;

//      Whether a plain answer puts the clock further from the time NTS gave
//      (known seconds, given elapsed boot seconds ago) than a crystal can
//      have drifted since: 1000 ppm, which no real one reaches, and five
//      seconds for the whole-second stamps it was kept in.
static CONST bool locale_plain_apart(bipolar now, bipolar offset_ns, p64 known,
                                     p64 elapsed)
{
        bipolar apart;

        if (!known || now < 0)
                return false;
        apart = now + offset_ns - (bipolar)known * (bipolar)SNTP_NANOSECONDS;
        if (apart < 0)
                apart = -apart;
        return apart > ((bipolar)5 + (bipolar)(elapsed / 1000)) *
                           (bipolar)SNTP_NANOSECONDS;
}

static bool locale_ntp_wants_step(bipolar offset_ns)
{
        bipolar most = locale_ntp_first ? LOCALE_NTP_STEP_FIRST_NS
                                        : LOCALE_NTP_STEP_NS;

        return offset_ns >= most || offset_ns <= -most;
}

/*
        How fast the clock runs is learned here, at every poll, and not by
        the kernel's loop. The loop learned too slowly to matter: handed a
        guest whose tick ran 96 ppm fast, it had learned 31 of them after
        thirteen minutes, and a step -- which ADJ_SETOFFSET makes without
        touching the frequency at all -- taught it nothing, so a crystal
        fast enough to be stepped at every poll, 71 ppm at half an hour,
        was stepped at every poll for ever and ran up its whole drift
        between them: 144 ms, measured, every time.

        What a poll finds past the slew the kernel still has in hand ran up
        since the last poll that set the clock, and over that time is the
        rate the clock is off by. It is learned in the proportion it stands
        above the measurement's own noise, some hundreds of microseconds:
        a clock 20 ms out is learned almost whole at once, and 150 us is
        under a quarter learned, so a reading that is mostly noise moves
        the frequency little. STA_FREQHOLD keeps the kernel's loop to the
        phase, so the two do not both answer the same offset. Not learned:
        an elapsed shorter than a minute, and an offset past a thousand
        parts per million of it, which is a clock that was set wrong
        rather than one that ran fast. Against a panel of stratum-1
        servers, in time compressed four times, a guest whose tick ran 400
        ppm fast was held within 0.2 ms after its second poll, where
        without this it ran 168 ms out before every step; an ordinary
        clock was a median 136 us out against 604.
*/
static CONST bipolar locale_ntp_learned(bipolar offset_ns, bipolar elapsed_ns,
                                        bipolar frequency)
{
        bipolar milliseconds = elapsed_ns / 1000000;
        bipolar magnitude;
        bipolar learned;

        if (elapsed_ns < LOCALE_NTP_LEARN_LEAST_NS ||
            offset_ns > elapsed_ns / 1000 || offset_ns < -(elapsed_ns / 1000))
                return frequency;
        magnitude = offset_ns < 0 ? -offset_ns : offset_ns;
        /*
                Offset over elapsed, in the kernel's 2^-16 ppm, taken whole
                and remainder so neither product can pass 2^63; and the
                gain, whose product with the rate did pass it for an offset
                past 140 s -- 39 hours since the last poll that set the
                clock is enough -- and came out with the wrong sign. Past
                four seconds the gain is within 1e-4 of one and is left out.
        */
        learned = offset_ns / milliseconds * 65536 +
                  offset_ns % milliseconds * 65536 / milliseconds;
        if (magnitude < (bipolar)1 << 32)
                learned = learned * magnitude /
                          (magnitude + LOCALE_NTP_NOISE_NS);
        learned += frequency;
        if (learned > LOCALE_NTP_FREQ_MOST)
                return LOCALE_NTP_FREQ_MOST;
        if (learned < -LOCALE_NTP_FREQ_MOST)
                return -LOCALE_NTP_FREQ_MOST;
        return learned;
}

static fn locale_ntp_discipline_words(bipolar offset_ns, positive error_us,
                                      positive address_to words)
{
        //      The time of a step is whole seconds and the nanoseconds left
        //      after them, which are not negative: a second short is -1 and
        //      999999999 and not 0 and -1.
        bipolar seconds = clock_floor_divide(offset_ns, (bipolar)SNTP_NANOSECONDS);

        memory_zero(words, LOGGER_TIMEX_WORDS * sizeof(positive));
        words[LOGGER_TIMEX_STATUS] = STA_PLL | STA_FREQHOLD;
        words[LOGGER_TIMEX_MAXERROR] = error_us;
        words[LOCALE_TIMEX_ESTERROR] = error_us;
        if (locale_ntp_wants_step(offset_ns))
        {
                words[0] = ADJ_SETOFFSET | ADJ_OFFSET | ADJ_NANO | ADJ_STATUS |
                           ADJ_MAXERROR | ADJ_ESTERROR;
                words[LOCALE_TIMEX_OFFSET] = 0;
                words[LOGGER_TIMEX_TIME_SEC] = (positive)seconds;
                words[LOGGER_TIMEX_TIME_NSEC] =
                    (positive)(offset_ns - seconds * (bipolar)SNTP_NANOSECONDS);
                return;
        }
        words[0] = ADJ_OFFSET | ADJ_TIMECONST | ADJ_NANO | ADJ_STATUS |
                   ADJ_MAXERROR | ADJ_ESTERROR;
        words[LOCALE_TIMEX_OFFSET] = (positive)offset_ns;
        words[LOCALE_TIMEX_CONSTANT] = LOCALE_NTP_TIMECONST;
}


static const char locale_ntp_fallback[][24] = {
        LOCALE_NTP_DEFAULT_SERVER,
        "time.google.com",
        "time.cloudflare.com",
        "216.239.35.0",
        "216.239.35.4",
        "162.159.200.1",
        "162.159.200.123",
};

static bipolar locale_ntp_apply_offset(bipolar offset_ns, bipolar distance_ns)
{
        bipolar now;
        bipolar target = 0;
        positive words[LOGGER_TIMEX_WORDS];
        p64 stamp[2];
        bipolar failed;

        now = sntp_now_ns();
        if (now < 0)
                return now;
        if (!sntp_target_ok(now, offset_ns, address_of target))
                return SNTP_MALFORMED;
        if (target < (bipolar)clock_trust_floor() * (bipolar)SNTP_NANOSECONDS)
                return SNTP_BELOW_FLOOR;

        locale_ntp_first = !locale_ntp_synced;
        locale_ntp_discipline_words(offset_ns, locale_ntp_error_us(distance_ns),
                                    words);
        if (locale_ntp_synced)
        {
                positive state[LOGGER_TIMEX_WORDS] = {0};
                bipolar elapsed = (bipolar)(system_clock_ns(HOST_CLOCK_BOOTTIME) -
                                            locale_ntp_synced);
                bipolar frequency;

                if (system_call_1(syscall(adjtimex), (positive)state) >= 0)
                {
                        bipolar pending = (bipolar)state[LOCALE_TIMEX_OFFSET];

                        if (!(state[LOGGER_TIMEX_STATUS] & STA_NANO))
                                pending *= 1000;
                        frequency = locale_ntp_learned(
                            offset_ns - pending, elapsed,
                            (bipolar)state[LOCALE_TIMEX_FREQUENCY]);
                        if (frequency != (bipolar)state[LOCALE_TIMEX_FREQUENCY])
                        {
                                words[0] |= ADJ_FREQUENCY;
                                words[LOCALE_TIMEX_FREQUENCY] = (positive)frequency;
                        }
                }
        }
        failed = system_call_1(syscall(adjtimex), (positive)words);
        if_common (failed >= 0)
                return 0;

        now = sntp_now_ns();
        if (now < 0)
                return now;
        if (!sntp_target_ok(now, offset_ns, address_of target))
                return SNTP_MALFORMED;
        //      A time in the window is after 1970, so it needs no flooring.
        stamp[0] = (p64)(target / (bipolar)SNTP_NANOSECONDS);
        stamp[1] = (p64)(target % (bipolar)SNTP_NANOSECONDS);
        failed = system_call_2(syscall(clock_settime), CLOCK_REALTIME,
                               (positive)stamp);
        if (failed < 0)
        {
                stamp[1] = stamp[1] / 1000;
                failed = system_call_2(syscall(settimeofday), (positive)stamp, 0);
        }
        if (failed < 0)
                return failed;
        locale_clock_mark_synced(locale_ntp_error_us(distance_ns));
        return 0;
}

/*
        The last time this machine knew for certain, kept in /root/clock.good
        for the next boot's floor (clock_trust_floor, net.c).

        What may be written is what no forged answer can have moved. After an
        authenticated (NTS) answer, /run/moonwater/clock.auth holds the wall
        second it set and the boot second it was set at, and the time now is
        that wall second plus the boot seconds since: the machine's own
        uptime, which no later step of the wall clock changes. With no such
        answer this boot, the wall clock is not read at all: what is written
        is the floor the boot began with (/run/moonwater/clock.base, kept once
        a boot) plus the seconds the machine has run, which is a lower bound
        on the time whatever any answer said, so no forgery, plain or not, can
        put the floor ahead of the truth and lock the real time out; it lags
        the truth by the time the machine spent switched off. The file only
        ever goes forward, is written in place and never through a link, and
        is written after every authenticated answer, every six hours of
        uptime and when the machine stops.
*/
#define LOCALE_CLOCK_AUTH_PATH HOST_STATE "/clock.auth"
#define LOCALE_CLOCK_BASE_PATH HOST_STATE "/clock.base"
#define LOCALE_CLOCK_GOOD_EVERY ((p64)6 * 3600 * SNTP_NANOSECONDS)

static p64 locale_clock_next;

//      A decimal number of at most twelve digits at text + *at, moving *at
//      past it; false when there is none.
static bool locale_clock_number(string_address text, positive address_to at,
                                p64 address_to value)
{
        p64 number = 0;
        positive from = address_to at;

        while (text[address_to at] >= '0' && text[address_to at] <= '9' &&
               address_to at - from < 12)
        {
                number = number * 10 + (p64)(text[address_to at] - '0');
                address_to at += 1;
        }
        if (address_to at == from)
                return false;
        address_to value = number;
        return true;
}

//      What the authenticated answer puts the time at now, or 0; with
//      elapsed, the boot seconds since it was given.
static p64 locale_clock_authenticated_now(p64 address_to elapsed)
{
        p8 text[48];
        positive at = 0;
        p64 wall;
        p64 booted;
        p64 now_boot = system_clock_ns(HOST_CLOCK_BOOTTIME) / SNTP_NANOSECONDS;

        host_read_word(LOCALE_CLOCK_AUTH_PATH, text, sizeof(text));
        if (!locale_clock_number((string_address)text, address_of at, address_of wall) ||
            text[at++] != ' ' ||
            !locale_clock_number((string_address)text, address_of at, address_of booted) ||
            text[at] || booted > now_boot)
                return 0;
        if (elapsed)
                address_to elapsed = now_boot - booted;
        return wall + (now_boot - booted);
}

//      An authenticated answer has set the clock: the wall second it set
//      and the boot second it was set at.
static fn locale_clock_authenticated(void)
{
        p8 text[48];
        time_t stamp = time(null);
        positive length;

        if (stamp < 0)
                return;
        length = positive_into(text, (positive)stamp);
        text[length++] = ' ';
        length += positive_into(text + length,
                                (positive)(system_clock_ns(HOST_CLOCK_BOOTTIME) /
                                           SNTP_NANOSECONDS));
        text[length++] = '\n';
        host_state_ready();
        (void)host_write_file(LOCALE_CLOCK_AUTH_PATH, text, length, 0644,
                              false);
}

//      The floor this boot started with, which /run/moonwater/clock.base
//      keeps so that every process can ask, or 0.
static p64 locale_clock_base(void)
{
        p8 text[24];
        positive at = 0;
        p64 value;

        host_read_word(LOCALE_CLOCK_BASE_PATH, text, sizeof(text));
        return locale_clock_number((string_address)text, address_of at, address_of value) &&
                       !text[at]
                   ? value : 0;
}

static fn locale_clock_base_keep(void)
{
        p8 text[24];
        positive length;

        if (locale_clock_base())
                return;
        host_state_ready();
        length = positive_into(text, (positive)clock_trust_floor());
        text[length++] = '\n';
        (void)host_write_file(LOCALE_CLOCK_BASE_PATH, text, length, 0644, false);
}

static bool locale_clock_persist(bool sync)
{
        p64 floor = clock_trust_floor();
        p64 value = locale_clock_authenticated_now(null);
        p8 text[24];
        positive length;

        if (!value)
        {
                p64 base = locale_clock_base();

                //      Nothing a forged answer can have moved: the floor the
                //      boot started with and the seconds the machine has run
                //      since.
                if (!base)
                        return false;
                value = base + system_clock_ns(HOST_CLOCK_BOOTTIME) /
                                   SNTP_NANOSECONDS;
        }
        if (value <= floor || value > CLOCK_GOOD_MOST)
                return false;
        length = positive_into(text, (positive)value);
        text[length++] = '\n';
        return host_write_file(CLOCK_GOOD_PATH, text, length, 0644, sync) >= 0;
}

//      Every six hours of uptime, from the machine loop; a minute later
//      when there was nothing yet to keep.
static fn locale_clock_keep(void)
{
        p64 now = system_clock_ns(HOST_CLOCK_BOOTTIME);

        if (now < locale_clock_next)
                return;
        locale_clock_next = now + (locale_clock_persist(false)
                                       ? LOCALE_CLOCK_GOOD_EVERY
                                       : (p64)60 * SNTP_NANOSECONDS);
}

//      When the machine stops, from the shell that stops it.
static fn host_clock_stop(void)
{
        (void)locale_clock_persist(true);
}

static bipolar locale_ntp_ask(string_address server, bool filter, bipolar most,
                              sntp_sample address_to answer)
{
        if (!server || !server[0] ||
            !radio_text_plain(server, string_length(server)))
                return SNTP_NO_SERVER;
        return sntp_query_apart(server, SNTP_PORT, filter, most, null, answer);
}

/*
        An unauthenticated answer does not get to undo an authenticated one.
        When NTS has set the clock this boot, what it said is known to within
        the drift of the crystal since (1000 ppm is a bound no real one
        reaches, plus five seconds), and a plain answer that puts the clock
        further from that is refused: it is a forgery, or a pool server that
        is wrong, and either way the authenticated time is the better one.
*/
static bool locale_plain_contradicts(bipolar offset_ns)
{
        p64 elapsed = 0;
        p64 known = locale_clock_authenticated_now(address_of elapsed);

        return locale_plain_apart(sntp_now_ns(), offset_ns, known, elapsed);
}

static bipolar locale_ntp_take(string_address server,
                               const sntp_sample address_to answer,
                               bool authenticated)
{
        bipolar applied;
        p8 last[LOCALE_NTP_LAST_ROOM];
        positive length;

        locale_ntp_moved_ns = answer->offset_ns;
        string_copy_bounded(locale_ntp_answered, server,
                            sizeof(locale_ntp_answered));
        if (authenticated)
                string_append_bounded(locale_ntp_answered, " (NTS)",
                                      sizeof(locale_ntp_answered));
        else if (locale_plain_contradicts(answer->offset_ns))
                return SNTP_CONTRADICTS;
        applied = locale_ntp_apply_offset(answer->offset_ns, answer->distance_ns);
        if (applied < 0)
                return applied;
        //      What set the clock, for the status page, and what it now
        //      knows for sure.
        string_copy_bounded(last, authenticated ? "nts " : "plain ", sizeof(last));
        string_append_bounded(last, server, sizeof(last));
        length = string_length(last);
        last[length++] = '\n';
        host_state_ready();
        (void)host_write_file(LOCALE_NTP_LAST_PATH, last, length, 0644, false);
        if (authenticated)
        {
                locale_clock_authenticated();
                (void)locale_clock_persist(false);
        }
        return applied;
}

/*
        A server answering RATE is telling us we ask too often. Walking to
        the next name and asking that one immediately is not an answer to
        it, and when the next name is another address of the same pool it
        is the complaint repeated. The verdict is carried out of the walk
        so a cycle that ended in nothing but rate limits waits properly
        instead of coming back in a second and doing it again.
*/
/*
        With sampling on and no server of the machine's own, the walk asks
        on after the first answer until SNTP_SERVERS have answered, and
        sntp_choose picks which to believe. Sampling off takes the first
        answer -- including one from /root/ntp.server -- unless it would
        step a clock that already has a plausible wall time more than two
        seconds, when a second server is asked for: sntp_choose believes no
        such step on one word. A clock nobody has set takes the first answer
        there is, with sampling or not: it has no useful time to compare it
        with, and waiting for the others is a minute on a machine whose
        network is slow to come.
*/
//      Whether the server at address is one of the first count of heard.
static bool locale_ntp_heard(const sntp_sample address_to heard, positive count,
                             p32 address)
{
        return array_any(at, count, heard[at].address == address);
}

/*
        Network Time Security, as the machine uses it.

        NTS is preferred: a machine asks for time from the key-establishment
        servers it knows (the defaults below, or the lines of
        /root/ntp.nts.servers, each a name or name:port) before it asks the
        pool, and an answer that authenticated is what sets an unset clock.
        The pool is the fallback, and only when no NTS server could be
        reached; its answers are marked plain (moonwater ntp says which set
        the clock last), are never written to the floor as known, and are
        refused when they put the clock further from the NTS time of this
        boot than a crystal can have drifted. A network that blocks TCP 4460
        costs one try of a few seconds, and then the pool is used without
        trying NTS again for two minutes (by hand, always). A server named in
        /root/ntp.server is the machine's own choice and is asked alone, in
        plain, as it always was, unless NTS is only: then nothing plain is
        asked, that server included.

        The keys and cookies live in /run/moonwater/nts (0600, in place, never
        through a link, read back as a plain file with every field checked: a
        torn or planted file is no state and the next query establishes
        afresh), since the scheduled query runs in a child of the machine
        process that is gone when it answers.
*/
#define LOCALE_NTS_MAGIC 0x3153544eu /* "NTS1" */
#define LOCALE_NTS_BACKOFF_NS ((p64)120 * SNTP_NANOSECONDS)
#define LOCALE_NTS_SERVERS_MOST 4
#define LOCALE_NTS_ENTRY_ROOM 80
#define LOCALE_NTS_SERVERS_ROOM 1024

static const char locale_nts_default[][24] = {
        "time.cloudflare.com",
        "nts.netnod.se",
        "ptbtime1.ptb.de",
};

static positive locale_nts_mode(void)
{
        p8 word[16];

        host_read_word(LOCALE_NTS_PATH, word, sizeof(word));
        return nts_mode_of((string_address)word);
}

//      The mode as it is said, or that the caller cannot read it.
static string_address locale_nts_say(void)
{
        p8 word[16];
        positive mode;

        if (!locale_readable(LOCALE_NTS_PATH, word, sizeof(word)))
                return (string_address)LOCALE_NEEDS_ROOT;
        mode = nts_mode_of((string_address)word);
        return mode == NTS_OFF   ? (string_address) "off"
               : mode == NTS_ONLY ? (string_address) "only"
                                  : (string_address) "on";
}

//      "name" or "name:port", as the key-establishment server it names.
static bool locale_nts_entry(string_address entry, p8 address_to host,
                             p16 address_to port)
{
        positive length = string_length(entry);
        positive colon = length;
        positive number = 0;

        for (positive at = 0; at < length; at++)
                if (entry[at] == ':')
                        colon = at;
        if (colon < length)
        {
                if (colon + 1 == length || length - colon > 6)
                        return false;
                for (positive at = colon + 1; at < length; at++)
                {
                        if (!byte_is_digit((p8)entry[at]))
                                return false;
                        number = number * 10 + (positive)(entry[at] - '0');
                }
                if (!number || number > 65535)
                        return false;
        }
        if (!nts_name_ok((const p8 address_to)entry, colon))
                return false;
        memory_copy(host, entry, colon);
        host[colon] = end;
        address_to port = colon < length ? (p16)number : NTS_KE_PORT;
        return true;
}

//      The lines of the servers' file that are servers: each a valid entry,
//      of fewer than stride bytes once its trailing blanks are gone. A line
//      is never cut to fit, so one too long is no server rather than its
//      first stride bytes; and when the file was longer than the buffer the
//      last line, cut where the buffer ends, is not one either. At most
//      LOCALE_NTS_SERVERS_MOST are taken; returns the count.
static positive locale_nts_lines(const p8 address_to text, positive length,
                                 bool complete, p8 address_to into,
                                 positive stride)
{
        positive count = 0;
        positive at = 0;

        if (!complete)
        {
                while (length && text[length - 1] != '\n')
                        length--;
        }
        while (at < length && count < LOCALE_NTS_SERVERS_MOST)
        {
                positive stop = at;
                positive size;
                p8 line[LOCALE_NTS_ENTRY_ROOM];
                p8 host[NTS_SERVER_MAX];
                p16 port;

                while (stop < length && text[stop] != '\n')
                        stop++;
                size = stop - at;
                while (size && (text[at + size - 1] == '\r' || text[at + size - 1] == ' '))
                        size--;
                if (size < stride)
                {
                        memory_copy(line, text + at, size);
                        line[size] = end;
                        if (locale_nts_entry((string_address)line, host, address_of port))
                                string_copy_bounded(into + count++ * stride,
                                                    (string_address)line, stride);
                }
                at = stop + 1;
        }
        return count;
}

//      The settings' servers, else the defaults: the lines of the file that
//      are valid entries (locale_nts_lines), to LOCALE_NTS_SERVERS_MOST.
//      Returns the count.
static positive locale_nts_servers(p8 address_to into, positive stride)
{
        p8 text[LOCALE_NTS_SERVERS_ROOM];
        bipolar got = host_read_state(LOCALE_NTS_SERVERS_PATH, text, sizeof(text));
        positive count = 0;

        if (got > 0)
                count = locale_nts_lines(text, (positive)got,
                                         got < (bipolar)sizeof(text) - 1,
                                         into, stride);
        if (count)
                return count;
        for (count = 0; count < array_count(locale_nts_default); count++)
                string_copy_bounded(into + count * stride,
                                    (string_address)locale_nts_default[count],
                                    stride);
        return count;
}

static bool locale_nts_state_ok(const nts_state address_to state)
{
        return nts_state_ok(state) && state->origin_port &&
               nts_name_ok(state->origin, string_length((string_address)state->origin));
}

static bool locale_nts_load(nts_state address_to state)
{
        p8 bytes[4 + sizeof(nts_state) + 2];
        bipolar got = file_slurp_regular_at(AT_FDCWD,
                                            (string_address)LOCALE_NTS_STATE_PATH,
                                            bytes, sizeof(bytes), O_NOFOLLOW);
        bool good = got == (bipolar)(4 + sizeof(nts_state)) &&
                    network_load_32(bytes) == LOCALE_NTS_MAGIC;

        if (good)
        {
                memory_copy(state, bytes + 4, sizeof(nts_state));
                //      Fields that are read as text end where they should.
                state->origin[NTS_SERVER_MAX - 1] = end;
                state->server[NTS_SERVER_MAX - 1] = end;
                good = locale_nts_state_ok(state);
        }
        crypto_forget(bytes, sizeof(bytes));
        if (!good)
                memory_zero(state, sizeof(*state));
        return good;
}

static fn locale_nts_save(const nts_state address_to state)
{
        p8 bytes[4 + sizeof(nts_state)];

        network_store_32(bytes, LOCALE_NTS_MAGIC);
        memory_copy(bytes + 4, state, sizeof(nts_state));
        host_state_ready();
        (void)host_write_file(LOCALE_NTS_STATE_PATH, bytes, sizeof(bytes), 0600,
                              false);
        crypto_forget(bytes, sizeof(bytes));
}

//      A negotiated time server that is not the key-establishment server's
//      own name has to be a public address, unless the key-establishment
//      server is not one: a name in an authenticated reply is still a name
//      someone chose, and this is the same test a redirect gets.
static bool locale_nts_server_ok(string_address host, string_address server)
{
        p32 ip;

        if (!server[0] || string_equals(server, host))
                return true;
        ip = http_lookup_apart(server);
        return ip && (http_address_public(ip) ||
                      !http_address_public(http_lookup_apart(host)));
}

static fn locale_nts_why_set(string_address host, string_address why)
{
        string_copy_bounded((string_address)locale_nts_why, host,
                            sizeof(locale_nts_why));
        string_append_bounded((string_address)locale_nts_why, ": ",
                              sizeof(locale_nts_why));
        string_append_bounded((string_address)locale_nts_why, why,
                              sizeof(locale_nts_why));
}

//      One key-establishment server asked for the time: the exchange with
//      its cookies, or the key establishment first when there is no state
//      or none left, and once more if the server says the cookie is no
//      good. The answer is authenticated or there is none. reached says a
//      name resolved, so a network that blocks NTS is told from one that is
//      not up yet.
static COLD bipolar locale_nts_ask(string_address entry, bool filter,
                                   sntp_sample address_to answer,
                                   bool address_to reached)
{
        static nts_state state;
        nts_exchange exchange;
        p8 host[NTS_SERVER_MAX];
        p16 port;
        bool have;
        bool said = false;
        bipolar status = SNTP_NO_SERVER;

        if (!locale_nts_entry(entry, host, address_of port))
                return SNTP_NO_SERVER;
        have = locale_nts_load(&state) &&
               string_equals((string_address)state.origin, (string_address)host) &&
               state.origin_port == port && state.count;
        for (positive attempt = 0; attempt < 2; attempt++)
        {
                string_address server;

                if (!have)
                {
                        //      Until an authenticated answer has been taken this
                        //      boot, the certificate is judged as of the floor.
                        bipolar why = nts_ke_apart(
                            (string_address)host, port,
                            !locale_clock_authenticated_now(null), &state);

                        if (why)
                        {
                                address_to reached |= why != HTTP_NO_HOST;
                                locale_nts_why_set((string_address)host,
                                                   locale_auto_reason(why));
                                said = true;
                                status = SNTP_NO_SERVER;
                                break;
                        }
                        string_copy_bounded((string_address)state.origin,
                                            (string_address)host,
                                            sizeof(state.origin));
                        state.origin_port = port;
                        if (!locale_nts_state_ok(&state))
                                break;
                        locale_nts_save(&state);
                }
                server = state.server[0] ? (string_address)state.server
                                         : (string_address)host;
                address_to reached = true;
                if (!locale_nts_server_ok((string_address)host, server))
                {
                        locale_nts_why_set((string_address)host,
                                           (string_address) "it named a time "
                                           "server that is not a public "
                                           "address");
                        said = true;
                        status = SNTP_NO_SERVER;
                        break;
                }
                nts_exchange_open(&exchange, &state);
                //      An authenticated answer is believed wherever the window
                //      puts it.
                status = sntp_query_apart(server, state.port, filter,
                                          SNTP_OFFSET_ANY_NS, &exchange, answer);
                nts_exchange_close(&exchange);
                locale_nts_save(&state);
                if (status != SNTP_NTS_NAK)
                        break;
                have = false;
        }
        if (status < 0 && !said)
                locale_nts_why_set((string_address)host,
                                   status == SNTP_NTS_NAK
                                       ? (string_address) "its cookies were "
                                                          "refused twice"
                                       : (string_address) "no authenticated "
                                                          "answer");
        crypto_forget(&state, sizeof(state));
        return status;
}

//      NTS servers have been tried and failed within the last two minutes.
static bool locale_nts_backed_off(bool by_hand)
{
        p8 text[24];
        positive at = 0;
        p64 then;

        host_read_word(LOCALE_NTS_FAILED_PATH, text, sizeof(text));
        return !by_hand &&
               locale_clock_number((string_address)text, address_of at, address_of then) &&
               !text[at] &&
               system_clock_ns(HOST_CLOCK_BOOTTIME) <
                   then * SNTP_NANOSECONDS + LOCALE_NTS_BACKOFF_NS;
}

static fn locale_nts_failed(void)
{
        p8 text[24];
        positive length = positive_into(
            text, (positive)(system_clock_ns(HOST_CLOCK_BOOTTIME) / SNTP_NANOSECONDS));

        text[length++] = '\n';
        host_state_ready();
        (void)host_write_file(LOCALE_NTS_FAILED_PATH, text, length, 0644, false);
}

/*
        The NTS servers in order, until one answer may be believed: the
        first one does on a clock that is unset (it has nothing to
        contradict) and for the only server there is, and so does one that
        moves a set clock two seconds or less; a step past two seconds on a
        clock that is set is taken on two servers' words, authenticated or
        not, so one wrong or compromised server cannot make it (sntp_choose,
        as for the pool). heard says some answer authenticated, which ends
        the walk whatever became of it: the pool is not asked to overrule
        authenticated time.
*/
static bipolar locale_nts_walk(bool address_to heard_any, bool address_to reached)
{
        p8 servers[LOCALE_NTS_SERVERS_MOST * LOCALE_NTS_ENTRY_ROOM];
        positive count = locale_nts_servers(servers, LOCALE_NTS_ENTRY_ROOM);
        bool filter = locale_ntp_sampling_wanted();
        sntp_sample row[SNTP_SERVERS];
        positive from[SNTP_SERVERS];
        positive heard = 0;
        bipolar failed = SNTP_NO_SERVER;
        bool rated = false;
        bool denied = false;
        bipolar now = sntp_now_ns();
        bool alone = count == 1 || now < 0 || !sntp_wall_ok(now) ||
                     now < (bipolar)clock_trust_floor() * (bipolar)SNTP_NANOSECONDS;

        for (positive at = 0; at < count && heard < SNTP_SERVERS; at++)
        {
                failed = locale_nts_ask((string_address)servers + at * LOCALE_NTS_ENTRY_ROOM,
                                        filter, row + heard, reached);
                if (failed < 0)
                {
                        rated |= failed == SNTP_RATE_LIMITED;
                        denied |= failed == SNTP_DENIED;
                        continue;
                }
                if (locale_ntp_heard(row, heard, row[heard].address))
                        continue;
                from[heard++] = at;
                if (alone ||
                    (heard == 1 && sntp_within(row[0].offset_ns, SNTP_OFFSET_SYNCED_NS)) ||
                    (heard > 1 && sntp_choose(row, heard) >= 0))
                        break;
        }
        address_to heard_any = heard != 0;
        if (heard)
        {
                bipolar chosen = alone ? 0 : sntp_choose(row, heard);

                if (chosen < 0)
                        return SNTP_UNCONFIRMED;
                return locale_ntp_take((string_address)servers +
                                           from[chosen] * LOCALE_NTS_ENTRY_ROOM,
                                       row + chosen, true);
        }
        return denied ? SNTP_DENIED : rated ? SNTP_RATE_LIMITED : failed;
}

static bipolar locale_ntp_plain(bool by_hand)
{
        p8 server[LOCALE_NTP_SERVER_ROOM];
        bipolar failed = SNTP_NO_SERVER;
        positive at;
        bool filter = locale_ntp_sampling_wanted();
        bipolar now = sntp_now_ns();
        //      A clock nobody has set has no time of its own to be moved
        //      from, and the window bounds what it can be told.
        bool unset = now >= 0 && !sntp_wall_ok(now);
        //      How far an answer may move the clock: two seconds when it
        //      tells the time, a day when it does not, and for a person who
        //      asked, anything the window holds -- a clock that is a week
        //      out is refused by every sample otherwise, with nothing to
        //      bring it back.
        bipolar most = by_hand ? SNTP_OFFSET_ANY_NS
                       : locale_clock_synced() ? SNTP_OFFSET_SYNCED_NS
                                               : SNTP_OFFSET_MOST_NS;
        bool rated = false;
        bool denied = false;
        sntp_sample heard[SNTP_SERVERS];
        b32 heard_from[SNTP_SERVERS];
        positive heard_count = 0;
        positive wanted;

        host_read_word(LOCALE_NTP_SERVER_PATH, server, sizeof(server));
        if (server[0])
        {
                failed = locale_ntp_ask((string_address)server, filter, most,
                                        heard);
                if (failed >= 0)
                {
                        heard_from[0] = array_count(locale_ntp_fallback);
                        heard_count = 1;
                        //      Plain time is unauthenticated: one server's
                        //      word moves a clock that is set by two
                        //      seconds at most, by hand or not, and a
                        //      further step waits for a second server (or
                        //      for NTS, or for `date -s` from a person who
                        //      knows the time).
                        if (unset || sntp_within(heard[0].offset_ns,
                                                 SNTP_OFFSET_SYNCED_NS))
                                return locale_ntp_take((string_address)server,
                                                       heard, false);
                }
                rated = failed == SNTP_RATE_LIMITED;
                denied = failed == SNTP_DENIED;
        }

        //      A named server remains the first choice, but its name is not
        //      an independent confirmation of a large clock step.
        wanted = heard_count ? 2
                 : filter && !server[0] && !unset ? SNTP_SERVERS
                                                  : 1;
        for (at = 0; at < array_count(locale_ntp_fallback) &&
                     heard_count < wanted; at++)
        {
                if (server[0] &&
                    string_equals((string_address)server,
                                  (string_address)locale_ntp_fallback[at]))
                        continue;
                failed = locale_ntp_ask((string_address)locale_ntp_fallback[at],
                                        filter, most, heard + heard_count);
                //      Another name for a server already heard from is that
                //      server saying it again: the network's resolver can
                //      name every one of these the same host. It is not
                //      counted, and the next name is asked.
                if (failed >= 0 &&
                    !locale_ntp_heard(heard, heard_count,
                                      heard[heard_count].address))
                        heard_from[heard_count++] = (b32)at;
                else if (failed == SNTP_RATE_LIMITED)
                        rated = true;
                else if (failed == SNTP_DENIED)
                        denied = true;
                //      A far step is not taken on one server's word.
                if (heard_count == 1 && wanted < 2 && !unset &&
                    !sntp_within(heard[0].offset_ns, SNTP_OFFSET_SYNCED_NS))
                        wanted = 2;
        }

        if (heard_count)
        {
                bipolar chosen = sntp_choose(heard, heard_count);

                if (chosen < 0 && heard_count == 1 && unset)
                        chosen = 0;
                if (chosen < 0)
                        return SNTP_UNCONFIRMED;
                return locale_ntp_take(
                    heard_from[chosen] == array_count(locale_ntp_fallback)
                        ? (string_address)server
                        : (string_address)locale_ntp_fallback[heard_from[chosen]],
                    heard + chosen, false);
        }
        return denied ? SNTP_DENIED : rated ? SNTP_RATE_LIMITED : failed;
}

/*
        NTS first, then the pool. An answer an NTS server authenticated ends
        the walk whatever became of it (taken, refused below the floor, or
        unconfirmed): the pool is not asked to overrule authenticated time.
        With none, a network that answered but would not do NTS is left for
        two minutes (locale_nts_failed), and the pool is asked unless NTS is
        only.
*/
static bipolar locale_ntp_walk(bool by_hand)
{
        p8 named[LOCALE_NTP_SERVER_ROOM];
        positive mode = locale_nts_mode();
        bipolar failed = SNTP_NO_SERVER;
        bool heard = false;
        bool reached = false;

        locale_nts_why[0] = end;
        host_read_word(LOCALE_NTP_SERVER_PATH, named, sizeof(named));
        if (mode != NTS_OFF && (!named[0] || mode == NTS_ONLY))
        {
                if (locale_nts_backed_off(by_hand))
                        string_copy_bounded((string_address)locale_nts_why,
                                            "not asked: none answered in the "
                                            "last two minutes",
                                            sizeof(locale_nts_why));
                else
                {
                        failed = locale_nts_walk(address_of heard,
                                                 address_of reached);
                        if (heard)
                                return failed;
                        if (reached)
                                locale_nts_failed();
                }
        }
        if (mode == NTS_ONLY)
                return failed;
        return locale_ntp_plain(by_hand);
}

/*
        One query at a time, whoever asks. The offset is measured against the
        clock as it is and applied relative to it, so two that measured
        before either applied would both move the clock by the same amount:
        the machine's own query and `moonwater time sync` or `ntp on` typed
        while it runs. The machine's gives way and is asked again, and a
        hand waits its turn, and says so, since a walk is up to a minute.
*/
static bipolar locale_ntp_apply(bool by_hand)
{
        bipolar lock = host_lock(LOCALE_NTP_LOCK_PATH, false);
        bipolar failed;

        if (lock == -EAGAIN)
        {
                if (!by_hand)
                        return SNTP_NO_REPLY;
                host_say(log, host_label "time: another query is running; "
                                         "waiting for it\n");
                lock = host_lock(LOCALE_NTP_LOCK_PATH, true);
        }
        failed = locale_ntp_walk(by_hand);
        radio_unlock(lock);
        return failed;
}

//      The server asked first: the one named, or the pool's name.
static string_address locale_ntp_server_say(p8 address_to into, positive room)
{
        if (!locale_readable(LOCALE_NTP_SERVER_PATH, into, room))
                return (string_address)LOCALE_NEEDS_ROOT;
        if (!into[0])
                string_copy_bounded(into, LOCALE_NTP_DEFAULT_SERVER, room);
        return (string_address)into;
}

//      moonwater ntp nts: whether it is asked for first, who is asked, and
//      what set the clock last (nts: authenticated; plain: the pool, which
//      anyone on the path could have forged).
static fn locale_nts_status(bool label)
{
        p8 servers[LOCALE_NTS_SERVERS_MOST * LOCALE_NTS_ENTRY_ROOM];
        p8 last[LOCALE_NTP_LAST_ROOM];
        string_address said = locale_nts_say();
        positive count;

        if (label)
                string_format(log, host_label "ntp nts %s", said);
        else
                string_format(log, "  ntp nts %s", said);
        if (!string_equals(said, LOCALE_NEEDS_ROOT))
        {
                count = locale_nts_servers(servers, LOCALE_NTS_ENTRY_ROOM);
                string_format(log, ", servers");
                for (positive at = 0; at < count; at++)
                        string_format(log, " %s",
                                      (string_address)servers + at * LOCALE_NTS_ENTRY_ROOM);
        }
        host_read_word(LOCALE_NTP_LAST_PATH, last, sizeof(last));
        host_say(log, "; the clock was last set by %s\n",
                 last[0] ? (string_address)last
                         : (string_address) "nothing this boot");
}

static b32 locale_ntp_status(void)
{
        p8 server[LOCALE_NTP_SERVER_ROOM];

        host_say(log, host_label "ntp %s, %s, sampling %s, %s\n",
                 locale_switch_say(LOCALE_NTP_PATH),
                 locale_ntp_server_say(server, sizeof(server)),
                 locale_switch_say(LOCALE_NTP_SAMPLING_PATH),
                 locale_clock_synced() ? "synchronised" : "waiting");
        locale_nts_status(false);
        return 0;
}

//      moonwater ntp nts [on|off|only|servers [default|NAME[:PORT]...]]
static b32 locale_nts_command(string_address address_to arguments,
                              positive count)
{
        string_address word = count > 3 ? arguments[3] : null;

        if (count == 3)
        {
                locale_nts_status(true);
                return 0;
        }
        if (string_equals(word, "servers"))
        {
                p8 text[LOCALE_NTS_SERVERS_MOST * LOCALE_NTS_ENTRY_ROOM];
                positive length = 0;
                bipolar kept;

                if (count == 4)
                {
                        locale_nts_status(true);
                        return 0;
                }
                return_if(count - 4 > LOCALE_NTS_SERVERS_MOST, host_usage());
                host_need_root();
                if (count == 5 && string_equals(arguments[4], "default"))
                {
                        kept = system_remove_at(AT_FDCWD, LOCALE_NTS_SERVERS_PATH, 0);
                        return_if(kept < 0 && kept != -ENOENT,
                                  host_fail("ntp nts servers", kept));
                        locale_nts_status(true);
                        return 0;
                }
                for (positive at = 4; at < count; at++)
                {
                        p8 host[NTS_SERVER_MAX];
                        p16 port;
                        positive size = string_length(arguments[at]);

                        if (size >= LOCALE_NTS_ENTRY_ROOM ||
                            !locale_nts_entry(arguments[at], host, address_of port))
                                return host_refuse("%s is not a server: a name or "
                                                   "address, or NAME:PORT\n",
                                                   arguments[at]);
                        memory_copy(text + length, arguments[at], size);
                        length += size;
                        text[length++] = '\n';
                }
                kept = host_write_file(LOCALE_NTS_SERVERS_PATH, text, length,
                                       0644, true);
                return_if(kept < 0, host_fail("ntp nts servers", kept));
                locale_nts_status(true);
                return 0;
        }
        return_if(count != 4 || (!string_equals(word, "on") &&
                                 !string_equals(word, "off") &&
                                 !string_equals(word, "only")),
                  host_usage());
        host_need_root();
        if (host_save("ntp nts", LOCALE_NTS_PATH, word))
                return 1;
        if (!string_equals(word, "off"))
                locale_ntp_next = 0;
        locale_nts_status(true);
        return 0;
}

static b32 locale_ntp_sampling_status(void)
{
        host_say(log, host_label "ntp sampling %s\n",
                 locale_switch_say(LOCALE_NTP_SAMPLING_PATH));
        return 0;
}

//      moonwater ntp on|off, and ntp sampling on|off; turning ntp on asks
//      at once.
static b32 locale_ntp_set(bool sampling, string_address word)
{
        if (host_save("ntp", sampling ? LOCALE_NTP_SAMPLING_PATH : LOCALE_NTP_PATH, word))
                return 1;
        if (!sampling && host_onoff(word) > 0)
        {
                locale_ntp_next = 0;
                if (locale_ntp_apply(true) < 0)
                        word = "on, waiting for a reply";
        }
        host_say(log, host_label "ntp %s%s\n", sampling ? "sampling " : "",
                 word);
        return 0;
}

/*
        moonwater ntp server [NAME|auto]: the server asked first. A name or a
        dotted address that fits the word the walk reads it into, and not a
        longer one cut to fit; auto goes back to the pool and the names
        beside it.
*/
static bool locale_ntp_server_ok(string_address name)
{
        positive length = string_length(name);

        if (!length || length >= LOCALE_NTP_SERVER_ROOM ||
            !byte_is_alnum((p8)name[0]) || string_equals(name, "auto"))
                return false;
        for (positive at = 0; at < length; at++)
                if (!byte_is_alnum((p8)name[at]) && name[at] != '.' &&
                    name[at] != '-')
                        return false;
        return true;
}

static b32 locale_ntp_server_status(void)
{
        p8 server[LOCALE_NTP_SERVER_ROOM];
        string_address said = locale_ntp_server_say(server, sizeof(server));

        host_say(log, host_label "ntp server %s\n", said);
        return 0;
}

static b32 locale_ntp_server_set(string_address name)
{
        bool automatic = string_equals(name, "auto");
        bipolar kept;

        if (!automatic && !locale_ntp_server_ok(name))
                return host_refuse("%s is not a server name: letters, digits, "
                                   "dots and hyphens, under 80 characters, or "
                                   "auto for the pool\n", name);
        if (automatic)
        {
                kept = system_remove_at(AT_FDCWD, LOCALE_NTP_SERVER_PATH, 0);
                if (kept == -ENOENT)
                        kept = 0;
        }
        else
                kept = radio_write_word(LOCALE_NTP_SERVER_PATH, name);
        return_if(kept < 0, host_fail("ntp server", kept));
        //      Asked of the new server at once, as `ntp on` does.
        if (locale_ntp_wanted() && locale_ntp_apply(true) < 0)
                host_say(log, host_label "ntp server %s, waiting for a "
                                         "reply\n", name);
        else
                host_say(log, host_label "ntp server %s\n", name);
        return 0;
}

//      A layout by the name this machine takes, and by the one XKB calls it:
//      uk is gb there, and the country codes sv and nb are se and no.
static const struct
{
        char name[8];
        char xkb[4];
} locale_keyboards[] = {
        {"us", "us"}, {"uk", "gb"}, {"gb", "gb"}, {"de", "de"}, {"se", "se"},
        {"sv", "se"}, {"no", "no"}, {"nb", "no"}, {"dk", "dk"}, {"fi", "fi"},
        {"fr", "fr"}, {"es", "es"}, {"it", "it"},
};

static bool locale_keyboard_ok(string_address name)
{
        positive at;

        for (at = 0; at < array_count(locale_keyboards); at++)
                if (string_equals(name, (string_address)locale_keyboards[at].name))
                        return true;
        return false;
}

//      The layout that is kept: us when none is, and nothing when it is not
//      the caller's to read.
static string_address locale_keyboard_say(p8 address_to name, positive room)
{
        if (!locale_readable(LOCALE_KEYBOARD_PATH, name, room))
                return (string_address)LOCALE_NEEDS_ROOT;
        if (!name[0])
                string_copy_bounded(name, "us", room);
        return (string_address)name;
}

static b32 locale_keyboard_status(void)
{
        p8 name[16];

        host_say(log, host_label "keyboard %s\n",
                 locale_keyboard_say(name, sizeof(name)));
        return 0;
}

//      The three lines `status` shows of this region's settings.
static fn locale_status_lines(void)
{
        p8 title[128];
        p8 how[48];
        p8 keyboard[16];

        if (locale_zone_read(title, sizeof(title), how, sizeof(how)))
                string_format(log, "  timezone %s %s\n", title, how);
        else
                string_format(log, "  timezone " LOCALE_NEEDS_ROOT "\n");
        string_format(log, "  ntp %s, sampling %s\n",
                      locale_switch_say(LOCALE_NTP_PATH),
                      locale_switch_say(LOCALE_NTP_SAMPLING_PATH));
        string_format(log, "  keyboard %s\n",
                      locale_keyboard_say(keyboard, sizeof(keyboard)));
}

static b32 locale_keyboard_list(void)
{
        string_format(log, "  layouts");
        for (positive at = 0; at < array_count(locale_keyboards); at++)
                string_format(log, " %s",
                              (string_address)locale_keyboards[at].name);
        host_say(log, "\n" TERM_DIM "  a layout's code is its country's,"
                      " so moonwater timezone takes it too" TERM_RESET
                      "\n");
        return 0;
}

static b32 locale_keyboard_set(string_address name)
{
        if (!locale_keyboard_ok(name))
                return host_refuse("unknown keyboard layout %s -- "
                                   "moonwater keyboard list shows them\n",
                                   name);
        //      Kept in /root, which the compositor watches: it takes the
        //      layout as soon as the file is written and when it starts.
        if (host_save("keyboard", LOCALE_KEYBOARD_PATH, name))
                return 1;
        host_say(log, host_label "keyboard %s\n", name);
        return 0;
}

/*
        The two settings of how Canvas draws, which are the machine's and not
        the image's: the pixel scale, a number or auto, and which mode a real
        screen is driven at, the largest it lists or the one it marks
        preferred.

        Asked bare, set with a word. A word is tried on the kernel first,
        which refuses what it does not take and keeps what it is given with
        Canvas off, and only then kept in /root, as the keyboard layout is;
        the machine process puts the kept one back when it starts, and a
        kernel command line (canvas.scale=2, canvas.modes=preferred) gives
        the first frame the one before that. The compositor reads the scale
        once, as it starts, so a new one is the desktop's when it restarts.
*/
static bool locale_canvas_scale_ok(string_address word)
{
        return string_equals(word, "auto") ||
               (word[0] >= '1' && word[0] < '1' + SPARK_CANVAS_SCALE_MOST &&
                !word[1]);
}

static bool locale_canvas_modes_ok(string_address word)
{
        return string_equals(word, "largest") || string_equals(word, "preferred");
}

static bipolar locale_canvas_live(bool scale, string_address word)
{
        struct canvas_control control;

        memory_zero(address_of control, sizeof(control));
        control.request = scale ? SPARK_CANVAS_SET_SCALE : SPARK_CANVAS_SET_MODES;
        if (scale)
                control.scale_setting = string_equals(word, "auto")
                    ? SPARK_CANVAS_SCALE_AUTO : (unsigned int)(word[0] - '0');
        else
                control.modes = string_equals(word, "preferred")
                    ? SPARK_CANVAS_MODES_PREFERRED : SPARK_CANVAS_MODES_LARGEST;
        return host_spark_once(SPARK_IOCTL_CANVAS, address_of control, FILE_READ);
}

//      What is in force, from the kernel when it can be asked, and else what
//      is kept: auto and the largest mode when nothing is.
static b32 locale_canvas_status(bool scale)
{
        struct canvas_control control;
        string_address path = (string_address)(scale ? LOCALE_CANVAS_SCALE_PATH
                                                     : LOCALE_CANVAS_MODES_PATH);
        p8 kept[16];
        bool readable = locale_readable(path, kept, sizeof(kept));
        string_address and_kept = kept[0] ? ", kept across boots" : "";

        memory_zero(address_of control, sizeof(control));
        control.request = SPARK_CANVAS_STATUS;
        if (host_spark_once(SPARK_IOCTL_CANVAS, address_of control, FILE_READ) < 0)
        {
                if (!readable)
                        host_say(log, host_label "%s " LOCALE_NEEDS_ROOT "\n",
                                 scale ? "scale" : "modes");
                else
                        host_say(log, host_label "%s %s, Canvas is not asking\n",
                                 scale ? "scale" : "modes",
                                 kept[0] ? (string_address)kept
                                         : (string_address)(scale ? "auto" : "largest"));
                return 0;
        }

        if (!scale)
                host_say(log, host_label "modes %s%s\n",
                         control.modes == SPARK_CANVAS_MODES_PREFERRED
                             ? "preferred" : "largest",
                         and_kept);
        //      Not running, the scale in force is the last desktop's: what is
        //      said is what Canvas will start with.
        else if (!control.running && control.scale_setting)
                host_say(log, host_label "scale %p, Canvas takes it when it starts%s\n",
                         (positive)control.scale_setting, and_kept);
        else if (!control.running)
                host_say(log, host_label "scale auto, Canvas asks its screens when it "
                                         "starts%s\n", and_kept);
        else if (control.scale_setting)
                host_say(log, host_label "scale %p, set%s%s; the desktop takes it when it restarts\n",
                         (positive)control.scale,
                         control.scale_origin == SPARK_CANVAS_ORIGIN_COMMAND_LINE
                             ? " on the kernel command line" : "",
                         and_kept);
        else if (control.scale_dpi)
                host_say(log, host_label "scale %p, auto: the panel is %p dots to the inch%s\n",
                         (positive)control.scale, (positive)control.scale_dpi,
                         and_kept);
        else
                host_say(log, host_label "scale %p, auto: no screen says a size that can be "
                                         "trusted%s\n",
                         (positive)control.scale, and_kept);
        return 0;
}

//      moonwater canvas scale [N|auto] and moonwater canvas modes [largest|preferred]
static b32 locale_canvas_setting(string_address address_to arguments, positive count)
{
        bool scale = string_equals(arguments[2], "scale");
        string_address word = count > 3 ? arguments[3] : null;
        bipolar live;

        return_if(count > 4, host_usage());
        if (!word)
                return locale_canvas_status(scale);

        if (scale && !locale_canvas_scale_ok(word))
                return host_refuse("scale is auto or a number from 1 to %p, not %s\n",
                                   (positive)SPARK_CANVAS_SCALE_MOST, word);
        if (!scale && !locale_canvas_modes_ok(word))
                return host_refuse("modes are largest or preferred, not %s\n", word);
        //      The kernel first, so a refusal keeps nothing; Canvas not being
        //      there at all keeps the word and takes it when it starts.
        live = locale_canvas_live(scale, word);
        if (live == -EPERM)
                return host_refuse("setting how Canvas draws needs root (CAP_SYS_ADMIN)\n");
        if (live == -EINVAL)
                return host_refuse("Canvas does not take %s\n", word);
        if (host_save(scale ? "canvas scale" : "canvas modes",
                      scale ? LOCALE_CANVAS_SCALE_PATH : LOCALE_CANVAS_MODES_PATH,
                      word))
                return 1;
        if (live < 0 && live != -ENOENT)
        {
                host_say(log_error, host_label "%s %s is kept, but Canvas did not take "
                                    "it: %s\n",
                         scale ? "scale" : "modes", word, file_reason(live));
                return 1;
        }
        return locale_canvas_status(scale);
}

/*
        The desktop's ink and the pointer's look and size are rows of the settings table, kept in /root as the
        scale is and taken by the compositor as soon as a file is written (it watches /root, as it does for the
        keyboard layout) and again whenever it starts. Nothing is asked of Canvas, so what is said is what is
        kept: the default when nothing is. What is left here is the verb that has no row of its own.
*/
/*
        The plain rows of the settings table (src/sh/registry.c), which are
        all read and set here and nowhere else. A bare verb says where the
        row stands, to anybody who can read /root: the word kept, or the
        standard when none is. One word sets it: the word is judged first
        (a word that is none of the row's is refused, exit 1), then root is
        asked for, and the row is kept in its file as the word normalised,
        and said again. default, and the row's reset word, put it back by
        keeping that word. More words than one is a usage page. Whoever
        applies the file (the compositor, which watches /root) takes it as
        it is written.
*/

//      What the row's line starts with: its say, or its verb without the first word.
static fn host_setting_says(const struct settings_row address_to row, p8 address_to into, positive room)
{
        positive at = 0;

        into[0] = end;
        if (row->say)
        {
                string_copy_bounded(into, (string_address)row->say, room);
                return;
        }
        for (positive word = 1; word < setting_words(row) && at + 1 < room; word++)
        {
                if (word > 1)
                        into[at++] = ' ';
                string_copy_bounded(into + at, (string_address)row->read[word], room - at);
                at += string_length(into + at);
        }
}

//      What a row says it is: the word kept or the standard, and that it is root's to read when /root is closed to the caller.
static string_address host_setting_value(const struct settings_row address_to row, p8 address_to word, p8 address_to shown)
{
        if (!locale_readable((string_address)row->path, word, 32))
                return (string_address)LOCALE_NEEDS_ROOT;
        return setting_shown(row, (string_address)word, shown, 48);
}

static b32 host_setting_status(const struct settings_row address_to row)
{
        p8 word[32], shown[48], says[48];

        host_setting_says(row, says, sizeof(says));
        host_say(log, host_label "%s %s\n", (string_address)says, host_setting_value(row, word, shown));
        return 0;
}

//      What a word that is none of a row's is told, in the row's words or made from its kind.
static b32 host_setting_refuse(const struct settings_row address_to row, string_address word)
{
        p8 says[48];

        if (row->refusal)
                return host_refuse((string_address)row->refusal, word);
        host_setting_says(row, says, sizeof(says));
        string_format(log_error, host_label);
        if (row->type == SETTING_NUMBER)
                host_say(log_error, "%s is a number from %p to %p %s, %sor default, not %s\n", (string_address)says,
                         (positive)row->low, (positive)row->high, (string_address)(row->unit ? row->unit : ""),
                         row->choices ? (string_address)"off, " : (string_address)"", word);
        else
                host_say(log_error, row->kind == SETTINGS_SWITCH ? "%s is on or off, not %s\n" : "%s is %s, not %s\n",
                         (string_address)says, row->kind == SETTINGS_SWITCH ? word : (string_address)row->choices, word);
        return 1;
}

//      The row the words name (found by setting_find), read or set.
static b32 host_setting_run(const struct settings_row address_to row, string_address address_to arguments, positive count)
{
        positive words = setting_words(row);
        p8 kept[32], says[48];
        long value;

        return_if(count - 1 > words + 1, host_usage());
        if (count - 1 == words)
                return host_setting_status(row);
        if (!setting_parse(row, arguments[1 + words], kept, sizeof(kept), &value))
                return host_setting_refuse(row, arguments[1 + words]);
        if (!bowl_is_root())
        {
                if (row->need)
                        return host_refuse("setting %s needs root (CAP_SYS_ADMIN)\n", (string_address)row->need);
                host_need_root();
        }
        host_setting_says(row, says, sizeof(says));
        if (host_save((string_address)says, (string_address)row->path, (string_address)kept))
                return 1;
        return host_setting_status(row);
}

//      The plain rows whose verbs start with these words, each said: what a verb that has more than one setting answers bare.
static b32 host_setting_group(string_address address_to words, positive count)
{
        for (positive at = 0; at < SETTINGS_ROWS; at++)
        {
                const struct settings_row address_to row = &settings_rows[at];
                positive word = 0;

                if (row->how != SETTING_PLAIN || setting_words(row) < count)
                        continue;
                while (word < count && string_equals(words[word], (string_address)row->read[word]))
                        word++;
                if (word == count)
                        host_setting_status(row);
        }
        return 0;
}

//      moonwater canvas cursor: bare it says the size and the look in one line, then the rows that are
//      the shake's; any other word is a usage page (the rows have words of their own, found before this).
static b32 locale_canvas_cursor(string_address address_to arguments, positive count)
{
        p8 word[32], shown[48], other[32], more[48];
        string_address shake[] = {(string_address) "canvas", (string_address) "cursor", (string_address) "shake"};

        return_if(count != 3, host_usage());
        host_say(log, host_label "cursor size %s, look %s\n",
                 host_setting_value(setting_by_path(SETTING_CURSOR_SIZE_PATH), word, shown),
                 host_setting_value(setting_by_path(SETTING_CURSOR_LOOK_PATH), other, more));
        (void)arguments;
        return host_setting_group(shake, 3);
}

//      A verb with no words of its own but its rows': bare it says them all.
static b32 host_setting_verb(string_address address_to arguments, positive count)
{
        return_if(count != 2, host_usage());
        return host_setting_group(arguments + 1, 1);
}

/*
        What the schedule makes of a query that ended, or of a look between
        queries. A success is only a success if the kernel still says the
        clock is synchronised when it is looked at: any change of
        clocksource clears that (timekeeping_notify ends in ntp_clear), and
        x86 changes it by itself about 1.5 s into a boot, when the TSC's
        refined calibration replaces tsc-early, and again whenever the
        watchdog gives up on the TSC. A first answer that landed before the
        switch left a correct clock marked unsynchronised and the next query
        256 s away: on a guest whose entropy was ready at once, 6 boots in 6
        synced at 1.4 s, were unsynchronised at 1.5 s, and were still
        waiting at 42 s. So an answer the kernel has forgotten is asked for
        again at the retry pace, whether that is seen as the child ends or
        on a later look -- but not over a RATE answer's wait, which leaves
        the retry pace at its slowest.
*/
static fn locale_ntp_schedule(bipolar ended, bool synced, p64 now)
{
        if (!ended && synced)
        {
                //      Soon at first, while the loop has not learned how
                //      fast this clock runs, and half-hourly once it has
                //      had the time to.
                locale_ntp_retry = LOCALE_NTP_RETRY_LEAST;
                locale_ntp_synced = now;
                locale_ntp_next = locale_after(now, locale_ntp_every);
                locale_ntp_every = locale_doubled(locale_ntp_every,
                                                  LOCALE_NTP_AGAIN);
        }
        else if (ended == LOCALE_NTP_EXIT_RATE)
        {
                locale_ntp_retry = LOCALE_NTP_RETRY_MOST;
                locale_ntp_next = locale_after(now, LOCALE_NTP_RATE_AGAIN);
        }
        else if (ended == LOCALE_NTP_EXIT_LONG)
                locale_ntp_next = locale_after(now, LOCALE_NTP_AGAIN);
        else if (ended != LOCALE_CHILD_IDLE)
        {
                //      Doubling from a second to a quarter of an hour: a
                //      machine that cannot reach a server does not walk
                //      seven names every eight seconds for ever.
                locale_ntp_next = locale_after(now, locale_ntp_retry);
                locale_ntp_retry = locale_doubled(locale_ntp_retry,
                                                  LOCALE_NTP_RETRY_MOST);
        }
        else if (locale_ntp_synced && !synced &&
                 locale_ntp_retry == LOCALE_NTP_RETRY_LEAST &&
                 locale_ntp_next > locale_after(now, LOCALE_NTP_RETRY_LEAST))
                locale_ntp_next = locale_after(now, LOCALE_NTP_RETRY_LEAST);
}

/*
        Whether NTP was wanted at the machine's last turn, which the wake
        below reads rather than the file again; and which network the turn
        was on, so that a different one, or the first, is a new chance for
        the queries that wait out a failure.
*/
static bool locale_ntp_on;
static bool locale_online_held;
static p8 locale_network_held[LOCALE_NETWORK_ROOM];

//      The walk in the child, and the class of what became of it.
static p8 locale_ntp_task(address_any context)
{
        bipolar failed = locale_ntp_apply(false);

        (void)context;
        return failed >= 0 ? 0
               : failed == SNTP_RATE_LIMITED ? LOCALE_NTP_EXIT_RATE
               : failed == SNTP_DENIED || failed > SNTP_ANSWER
                   ? LOCALE_NTP_EXIT_LONG
                   : 1;
}

static fn locale_ntp_keep(bool online, bool synced, p64 now)
{
        bipolar ended;

        //      `ntp off` ends a query in flight and waits for it: left to
        //      run it would set the clock after it was told not to, and
        //      nothing would reap it or close its pipe.
        if (!locale_ntp_on)
        {
                locale_child_stop(address_of locale_ntp_child);
                return;
        }
        ended = locale_child_poll(address_of locale_ntp_child);
        if (ended == LOCALE_CHILD_RUNNING)
                return;
        locale_ntp_schedule(ended, synced, now);
        if (ended != LOCALE_CHILD_IDLE || (locale_ntp_next && now < locale_ntp_next) ||
            !online)
                return;

        //      No child to make is a query that failed: a machine out of
        //      processes does not ask four times a second.
        if (!locale_child_run(address_of locale_ntp_child, locale_ntp_task,
                              null))
                locale_ntp_schedule(1, synced, now);
}

static fn locale_restore(void)
{
        p8 zone[80];
        p8 keyboard[16];

        locale_zone_own();

        host_read_word(CLOCK_ZONE_PATH, zone, sizeof(zone));
        if (zone[0])
        {
                tzset();
                (void)locale_zone_kernel();
                (void)bowl_write_localtime_host();
                (void)bowl_write_localtime_all(null);
        }

        //      How Canvas draws, as it was left: a word the kernel does not
        //      take is not one that was ever kept.
        host_read_word(LOCALE_CANVAS_SCALE_PATH, keyboard, sizeof(keyboard));
        if (keyboard[0] && locale_canvas_scale_ok(keyboard))
                locale_canvas_live(true, keyboard);
        host_read_word(LOCALE_CANVAS_MODES_PATH, keyboard, sizeof(keyboard));
        if (keyboard[0] && locale_canvas_modes_ok(keyboard))
                locale_canvas_live(false, keyboard);

        locale_ntp_next = 0;
        locale_ntp_retry = LOCALE_NTP_RETRY_LEAST;
        locale_ntp_synced = 0;
        locale_ntp_every = LOCALE_NTP_AGAIN_FIRST;
        //      The floor this boot began with, once a boot, from the disk's
        //      /root when one was taken.
        locale_clock_base_keep();
        locale_auto_next = 0;
        locale_auto_wait = LOCALE_AUTO_LEAST;
        //      Only the machine process asks the time. `moonwater boot` comes
        //      through here too when a disk is taken, and the machine
        //      process, released by the same verdict, asks as well: two
        //      queries that each measured an offset before either applied it
        //      stepped the clock twice.
        locale_ntp_on = locale_ntp_wanted();
        if (host_machine_self && locale_ntp_on)
                locale_ntp_keep(true, locale_clock_synced(),
                                system_clock_ns(HOST_CLOCK_BOOTTIME));
}

/*
        How long the machine loop may sleep before locale_ntp_keep must look
        again, at most the loop's own wake. A query in flight is polled every
        quarter second and a retry is woken for when it falls due: the first
        ask of a boot can run before the lease (its DNS id waits on the same
        entropy the DHCP transaction id does). Never 0, which the machine
        wait reads as not waiting at all; a retry already past due (its fork
        failed) is looked at again in a quarter second rather than in a
        spin. For ten seconds after a query that set the clock it is a
        quarter second as well, so that the kernel dropping that
        synchronisation at the boot's clocksource switch (see
        locale_ntp_schedule) is seen at once and not at the next three-second
        wake: three instrumented boots lost it at 1.5 s and asked again only
        at 5.6 s. In the first minute of a boot with no clock set yet it is
        a second, because the network that comes up then is the one the
        clock waits for.
*/
static unsigned int locale_wake_ms(unsigned int most)
{
        p64 now;
        p64 due;

        if (locale_ntp_child.pid > 0)
                return most < 250 ? most : 250;
        if (!locale_ntp_on)
                return most;
        now = system_clock_ns(HOST_CLOCK_BOOTTIME);
        if (!locale_ntp_synced && now < 60000000000ull && most > 1000)
                return 1000;
        if (!locale_ntp_next)
                return most;
        if (locale_ntp_synced && now - locale_ntp_synced < 10000000000ull)
                return most < 250 ? most : 250;
        due = locale_ntp_next <= now ? 250
                                     : (locale_ntp_next - now) / 1000000 + 1;
        return due < most ? (unsigned int)due : most;
}

//      Daylight saving moves the offset twice a year without anyone setting
//      anything; the kernel's copy follows it here. Bowls need nothing, since
//      their file carries the rule rather than the offset.
static fn locale_recover(void)
{
        p8 network[LOCALE_NETWORK_ROOM];
        bool online = locale_network(network, sizeof(network));
        bool synced;
        p64 now;

        (void)locale_zone_kernel();
        locale_ntp_on = locale_ntp_wanted();
        //      A network that is not the one of the last turn, or the first
        //      after none, is not a reason to go on waiting out a failure
        //      that was the last one's: both queries are asked at once.
        if (online && (!locale_online_held ||
                       (network[0] &&
                        !locale_network_same(network, locale_network_held))))
        {
                locale_ntp_next = 0;
                locale_ntp_retry = LOCALE_NTP_RETRY_LEAST;
                locale_auto_next = 0;
                locale_auto_wait = LOCALE_AUTO_LEAST;
        }
        locale_online_held = online;
        string_copy_bounded(locale_network_held, network,
                            sizeof(locale_network_held));
        synced = locale_clock_synced();
        now = system_clock_ns(HOST_CLOCK_BOOTTIME);
        locale_ntp_keep(online, synced, now);
        locale_auto_keep(locale_ntp_on, synced, now, network);
        locale_clock_keep();
}

/*
        What this machine is called.

        Two words and a hyphen, rolled the first time a machine boots and kept
        in /root with the other settings, so a name made on a live stick goes
        to the disk it installs on, `update` keeps it and `wipe` leaves it.
        There are five hundred and twelve of each, so a roll is eighteen bits
        and two machines of one person's are very unlikely to share a name;
        pairing is what settles one that does. The words are lowercase
        letters only and the name is a valid host name, which is what the
        kernel, a prompt and a router will all take.

        The kernel's own default is the same on every machine this image is
        built for, which is why it is not the name: `hostname NAME` changes
        what the kernel says for the rest of the session and nothing else,
        where this is what the machine is.
*/
#define NAME_PATH "/root/name"
#define NAME_ROOM 80
#define NAME_WORDS 512
#define NAME_LETTERS "abcdefghijklmnopqrstuvwxyz0123456789-"

static const p8 name_adjectives[] =
    "ample aqua arctic ashen astral azure balmy bashful bold brave bright brisk "
    "bronze breezy bubbly burly calm candid careful cheery chilly civil clever "
    "cloudy coastal cobalt cozy crisp curious dainty dandy dapper daring "
    "dashing dewy distant dreamy dusky eager earnest early earthy elated "
    "electric elegant emerald epic even faint fancy fearless fiery fine fleet "
    "floral fluffy fluent foggy fond frank frosty fuzzy gallant gentle giddy "
    "gilded glad gleaming glossy glowing golden graceful grand grassy great "
    "green hardy hasty hazy hearty hidden humble hushed icy idle indigo ivory "
    "jaunty jolly jovial joyful keen kind lively lofty lucid lucky lunar lush "
    "magic merry mellow mighty mild minty misty modest mossy mythic natural "
    "neat nifty nimble noble oaken orbital pastel patient peaceful pearly perky "
    "plucky plush polar polite precise proud pure quaint quick quiet radiant "
    "rapid regal rosy royal rustic sable sandy savvy scarlet serene shady sharp "
    "shiny silent silky silver sleek smart smooth snowy snug solar sonic spry "
    "stable starry steady stellar still stormy sturdy subtle sunny super swift "
    "tame teal tender tidal tidy tiny tranquil trusty twilit upbeat urban "
    "valiant velvet verdant vibrant vivid warm wavy whole wily windy wintry "
    "wise witty woolly young zany zesty zippy acoustic adept aerial agile airy "
    "alert alpine amiable amused ancient angular antique apt ardent artful "
    "atomic avid awake aware balanced beaming benign blazing blithe blooming "
    "bonny boundless bouncy bountiful bracing brainy brawny brimming broad "
    "buoyant busy buttery cardinal carefree cascading celestial central certain "
    "charming cheerful chief chipper choice chosen circular classic clean clear "
    "cloudless clustered colorful comfy compact composed concise content cool "
    "copper countless courtly crafty creamy crimson cubic cuddly curly cute "
    "deft deluxe dense devoted diamond digital diligent divine double downy "
    "drifting driven dual dynamic eastern ebony ecstatic elastic elfin elite "
    "endless energetic enigmatic equal erudite ethereal exact exotic expert "
    "fabled fair faithful famed fanciful fast fertile festive fit flaxen "
    "fleeting flexible flowing flying formal fortunate fragrant free fresh "
    "friendly frozen fruitful full funky future galactic gamma genial genuine "
    "gifted glacial glittering gleeful global gracious gradual grateful "
    "grounded growing guiding gusty halcyon handy happy harmonic heady heavenly "
    "helpful heroic honest hopeful humming hybrid ideal immense inner inspired "
    "intent intrepid inventive iron jazzy jeweled jumbo just kindly knowing "
    "lacy lasting legendary level liberal light likely limber linen lithe "
    "little lone long loyal lyrical majestic mature maximal mental metallic "
    "milky mindful mirrored mobile molten moonlit morning mountain musical "
    "mystic narrow native nautical nearby neon nested neutral new next "
    "nocturnal north northern novel oceanic offbeat open optimal orange organic "
    "original outer oval pacific paper parallel peachy perfect pink placid "
    "plain playful pleasant plentiful pocket poised polished polka portable "
    "positive potent practical primal prismatic private prized prompt "
    "prosperous quantum quilted rare ready real reborn refined regular relaxed "
    "reliable remote resolute rested rhythmic rich ringing rippling rising "
    "robust rolling rooted round roving rugged running sacred safe sailing "
    "savory scenic secret select settled shimmering shining simple sincere "
    "singing skilled slender slow small smiling snappy social soft solid solo "
    "soothing sound southern sparkling special speedy spiral splendid spotted "
    "square standing steel sterling stout strong studious sublime summer sunlit "
    "supreme sure sweet synthetic";

static const p8 name_nouns[] =
    "comet nebula quasar pulsar galaxy orbit meteor asteroid planet moon "
    "eclipse aurora cosmos rocket probe satellite lander rover capsule shuttle "
    "station beacon horizon zenith equinox solstice wizard sorcerer mage druid "
    "knight bard dragon griffin phoenix unicorn sprite pixie golem elf dwarf "
    "oracle sage ranger paladin alchemist wanderer pilgrim voyager explorer "
    "pioneer captain skipper sailor mariner otter falcon heron lynx fox wolf "
    "bear owl raven crane ibis panda koala lemur llama alpaca gecko newt toad "
    "frog turtle dolphin whale seal walrus narwhal orca manta squid octopus "
    "crab lobster shrimp beetle firefly moth mantis cricket bee ant robin finch "
    "wren sparrow magpie osprey condor kestrel egret stork puffin penguin "
    "pelican albatross swan goose duck badger beaver bison camel caribou "
    "cheetah cougar coyote dingo donkey eagle elk ferret gazelle giraffe goat "
    "hare hawk hedgehog hippo horse hyena ibex impala jackal jaguar kangaroo "
    "kiwi leopard lion lizard marmot meerkat mole moose mouse mule ocelot "
    "opossum oryx parrot pigeon pony porcupine puma quail rabbit raccoon ram "
    "reindeer rhino salmon seahorse sloth snail sparrowhawk squirrel starling "
    "tapir tiger tortoise trout toucan turkey vole weasel wombat yak zebra "
    "maple willow cedar birch aspen oak pine fern moss lotus orchid daisy "
    "clover thistle ivy bamboo cactus poppy tulip violet lily jasmine lavender "
    "thyme basil mint rosemary fennel ginger pepper mango lemon peach cherry "
    "berry honey biscuit pretzel waffle muffin noodle pancake pudding cookie "
    "cocoa almond walnut pecan hazel acorn olive plum apricot melon papaya "
    "guava lime citrus canyon glacier meadow lagoon harbor island reef delta "
    "dune ridge summit valley tundra prairie oasis grove forest river creek "
    "brook spring cascade geyser volcano plateau mesa cliff cavern grotto fjord "
    "bay cove shore beach lake pond marsh swamp steppe savanna jungle orchard "
    "garden lantern compass anchor kettle teapot mirror prism lighthouse bridge "
    "tower castle window ribbon button lamp clock harp flute drum violin cello "
    "piano trumpet kite sail mast paddle canoe kayak sled wagon cart wheel gear "
    "lever pulley hammock tent cabin cottage barn windmill fountain statue arch "
    "gate path trail road bench swing slide ladder rope knot basket bucket jar "
    "bottle cup bowl plate spoon fork pan pot kiln forge anvil loom spindle "
    "needle thimble quilt blanket pillow candle torch ember spark flame cinder "
    "ash coal pebble marble crystal gem pearl opal ruby topaz jade amber garnet "
    "quartz agate onyx jasper coral shell atom photon quark proton neutron "
    "electron vector matrix tensor fractal helix prime lattice cipher token "
    "signal packet socket kernel module driver router switch relay beam laser "
    "maser radar sonar echo pulse wave ripple tide current breeze gust gale "
    "squall storm thunder lightning rainbow cloud mist fog frost snow sleet "
    "hail dew rain drizzle sunbeam moonbeam stardust starlight twilight sunrise "
    "sunset daybreak midnight noon dusk dawn hour minute season harvest bloom "
    "sprout seedling branch petal blossom bud vine root trunk leaf feather "
    "scale paw whisker tail mane hoof horn tusk antler tuba banjo ukulele "
    "guitar bagpipe fiddle bell chime gong cymbal tambourine whistle trombone "
    "oboe clarinet bassoon sitar zither lyre lute dulcimer harmonica accordion "
    "kazoo marimba xylophone anemone bluebell buttercup chickadee dandelion "
    "dragonfly hummingbird kingfisher nightingale periwinkle sandpiper "
    "snowflake tangerine";

/* The word at an index in a list of words one space apart, the empty word for
   an index past the last. */
static fn name_word(const p8 address_to list, positive size, positive index,
                    p8 address_to into, positive room)
{
        string_address word = (string_address)list +
                              memory_nth_of((address_any)list, ' ', size, index).x;
        positive length = (positive)(string_first_of_or_end(word, ' ') - word);

        if (length >= room)
                length = room - 1;
        memory_copy_apart(into, word, length);
        into[length] = end;
}

static fn name_roll(p8 address_to into, positive room)
{
        p8 noun[16];
        p32 bits;

        system_random_fill(address_of bits, sizeof(bits), 0);

        name_word(name_adjectives, sizeof(name_adjectives) - 1, bits & (NAME_WORDS - 1),
                  into, room);
        string_append_bounded(into, "-", room);
        name_word(name_nouns, sizeof(name_nouns) - 1, (bits >> 9) & (NAME_WORDS - 1),
                  noun, sizeof(noun));
        string_append_bounded(into, noun, room);
}

static bool link_name_good(string_address name);

/* Lowercase letters, digits and hyphens, a letter or digit at each end: one
   label of a host name, and not the word that rolls a new one. It is also
   what `moonwater link` can say, which is the shorter limit (under 32) and
   leaves out the words that command takes in a name's place, so the name a
   machine has is always one it can be linked by. */
static bool name_valid(string_address name)
{
        positive length = string_length(name);

        return link_name_good(name) && string_span_of_set(name, NAME_LETTERS) == length &&
               name[length - 1] != '-' && !string_equals(name, "random");
}

static bipolar name_apply(string_address name)
{
        bipolar applied = system_call_2(syscall(sethostname), (positive)name,
                                        string_length(name));

        if (applied >= 0)
                bowl_hosts_write(name);
        return applied;
}

//      A roll that could not be saved, kept so that the next ask in this
//      process is the same name and not another roll.
static p8 name_unsaved[NAME_ROOM];

/* The saved name, rolled and saved first when there is none or it does not
   read. Says whether it had to roll in `rolled`, and whether the name is
   saved at the end: false when /root would not take it. */
static bool name_ensure(p8 address_to into, positive room, bool address_to rolled)
{
        host_read_word(NAME_PATH, into, room);
        address_to rolled = !name_valid(into);

        if (!address_to rolled)
                return true;

        if (name_unsaved[0])
                string_copy_bounded(into, (string_address)name_unsaved, room);
        else
                name_roll(into, room);
        if (radio_write_word(NAME_PATH, into) >= 0)
        {
                name_unsaved[0] = 0;
                return true;
        }
        string_copy_bounded((string_address)name_unsaved, (string_address)into,
                            sizeof(name_unsaved));
        return false;
}

static fn name_restore(void)
{
        p8 name[NAME_ROOM];
        bool rolled;

        (void)name_ensure(name, sizeof(name), address_of rolled);
        (void)name_apply(name);

        if (rolled)
                host_say(log, host_label "this machine is called %s; "
                                         "moonwater name changes it\n", name);
}

/*
        moonwater debug: what the desktop is doing and why, read from the head
        of the window table the compositor keeps -- its card, whether the
        pointer has a plane, the overlay, how many frames the card showed
        straight from a window's buffer, and why the top window was not at the
        last present. The table is taken from /dev/spark, which hands it to
        anyone, and else from its path; nothing in it is written, and no
        window is taken.
*/
static b32 host_debug(string_address address_to arguments, positive count)
{
        static const string_address refusals[] = {
            "nothing", "there is no /dev/udmabuf", "udmabuf refused a buffer", "the card refused a buffer's import",
            "the card refused a buffer's framebuffer", "the card refused a flip", "the table is no memfd (memfd_create)",
            "the table is no memfd", "the table is no memfd (its size)", "the table is no memfd (its seals)",
            "the table is no memfd (its mode)", "the table is no memfd (its link)"};
        static const string_address reasons[] = {
            "shown straight", "no card", "the table is no memfd", "the pointer is drawn in software",
            "no window, or one dragged or resized", "it is glass", "it is not at the size it drew",
            "it is not wholly on the screen", "it is too short past its corners and close button", "it meets the tabs",
            "the card has no overlay for it", "the card refused its framebuffer", "the card refused the overlay"};
        const struct windows address_to table;
        long spark, file = -1, mapped;
        char driver[17];

        (void)arguments;
        return_if(count > 2, host_usage());
        spark = window_call(WINDOW_SYS_OPENAT, -100, (long)"/dev/spark", WINDOW_OPEN_FLAGS, 0, 0, 0);
        if (spark >= 0)
        {
                file = window_call(WINDOW_SYS_IOCTL, spark, WINDOWS_IOCTL_TABLE_GET, 0, 0, 0, 0);
                window_call(WINDOW_SYS_CLOSE, spark, 0, 0, 0, 0, 0);
        }
        if (file < 0)
                file = window_call(WINDOW_SYS_OPENAT, -100, (long)WINDOWS_TABLE, 0x80000, 0, 0, 0);
        if (file < 0)
        {
                host_say(log, "canvas\n  no compositor is running\n");
                return 0;
        }
        // PROT_READ, MAP_SHARED
        mapped = window_call(WINDOW_SYS_MMAP, 0, (long)sizeof(struct windows), 1, 1, file, 0);
        window_call(WINDOW_SYS_CLOSE, file, 0, 0, 0, 0, 0);
        if (mapped < 0 && mapped > -4096)
                return host_fail("debug", (b32)mapped);
        table = (const struct windows address_to)mapped;
        memory_copy(driver, table->driver, sizeof(table->driver));
        driver[16] = 0;
        host_say(log, "canvas\n");
        host_say(log, "  card            %s, %px%p, scale %p\n", driver[0] ? (string_address)driver : (string_address) "none",
                 (positive)table->output_width, (positive)table->output_height, (positive)table->scale);
        host_say(log, "  pointer         %s\n", table->pointer_plane ? (string_address) "on a plane of its own"
                                                                    : (string_address) "drawn in software");
        host_say(log, "  overlay         %s\n", table->overlay > 0    ? (string_address) "found"
                                               : table->overlay < 0 ? (string_address) "none, or refused"
                                                                    : (string_address) "not asked yet");
        host_say(log, "  shown straight  %p frames\n", (positive)table->scanned);
        host_say(log, "  refused         %s\n",
                 table->refused < sizeof(refusals) / sizeof(refusals[0]) ? refusals[table->refused] : (string_address) "?");
        host_say(log, "  top window      %s%s\n", table->unscanned ? (string_address) "not shown straight: " : (string_address) "",
                 table->unscanned < sizeof(reasons) / sizeof(reasons[0]) ? reasons[table->unscanned] : (string_address) "?");
        window_call(WINDOW_SYS_MUNMAP, mapped, (long)sizeof(struct windows), 0, 0, 0, 0);
        return 0;
}

static b32 host_name(string_address address_to arguments, positive count)
{
        p8 name[NAME_ROOM];
        bool rolled;
        bipolar applied;

        return_if(count > 3, host_usage());

        if (count == 2)
        {
                file_machine machine;

                //      /root is root's. Anyone else is told what the kernel
                //      calls the machine, which is the name this applies; so
                //      is root when /root will not keep a roll, because a
                //      name that is not kept is not made up again at every
                //      question.
                if (bowl_is_root() &&
                    name_ensure(name, sizeof(name), address_of rolled))
                {
                        if (rolled)
                                (void)name_apply(name);
                }
                else if (file_machine_read(address_of machine))
                        string_copy_bounded(name, machine.node, sizeof(name));
                else
                        return host_fail("name", -EIO);

                //      What the kernel says can be whatever the root of a
                //      UTS namespace set, escape and bell included.
                host_say(log, "%w\n", writer_terminal_name, name);
                return 0;
        }

        host_need_root();

        if (string_equals(arguments[2], "random"))
                name_roll(name, sizeof(name));
        else
        {
                string_copy_bounded(name, arguments[2], sizeof(name));
                memory_to_lower_ascii(name, string_length(name));

                if (!name_valid(name))
                        return host_refuse("%s is not a machine name: lowercase "
                                           "letters, digits and hyphens, a "
                                           "letter or digit at each end, under "
                                           "32 characters, and not random or "
                                           "a word moonwater link takes\n",
                                           arguments[2]);
        }

        //      Kept first, so a machine whose kernel will not take the name still
        //      has it at the next boot, and says so when it does not.
        if (host_save(NAME_PATH, NAME_PATH, name))
                return 1;
        applied = name_apply(name);
        if (applied < 0)
        {
                host_say(log_error, host_label "%s is saved, but the "
                                    "kernel did not take it: %s\n",
                         name, file_reason(applied));
                return 1;
        }

        host_say(log, "%s\n", name);
        return 0;
}

static b32 host_time(string_address address_to arguments, positive count)
{
        locale_zone_own();
        if (count == 2)
                return locale_time_status();
        return_if(count != 3 || !string_equals(arguments[2], "sync"), host_usage());
        host_need_root();
        return locale_time_sync();
}

static b32 host_timezone(string_address address_to arguments, positive count)
{
        string_address word = count > 2 ? arguments[2] : null;

        locale_zone_own();
        if (count == 2)
                return locale_zone_status();
        return_if(count != 3, host_usage());
        if (string_equals(word, "list"))
                return locale_zone_list();
        host_need_root();
        if (string_equals(word, "auto"))
                return locale_zone_auto();
        return locale_zone_set(word);
}

static b32 host_ntp(string_address address_to arguments, positive count)
{
        string_address word = count > 2 ? arguments[2] : null;

        locale_zone_own();
        if (count == 2)
                return locale_ntp_status();
        if (string_equals(word, "server"))
        {
                if (count == 3)
                        return locale_ntp_server_status();
                return_if(count != 4, host_usage());
                host_need_root();
                return locale_ntp_server_set(arguments[3]);
        }
        if (string_equals(word, "nts"))
                return locale_nts_command(arguments, count);
        if (string_equals(word, "sampling"))
        {
                if (count == 3)
                        return locale_ntp_sampling_status();
                return_if(count != 4 || host_onoff(arguments[3]) < 0, host_usage());
                host_need_root();
                return locale_ntp_set(true, arguments[3]);
        }
        return_if(count != 3 || host_onoff(word) < 0, host_usage());
        host_need_root();
        return locale_ntp_set(false, word);
}

//      The layout in XKB's name, alone on a line: what a compositor's
//      XKB_DEFAULT_LAYOUT is, for a script to read.
static b32 locale_keyboard_xkb(void)
{
        p8 name[16];
        string_address held = locale_keyboard_say(name, sizeof(name));
        positive found = array_where(at, array_count(locale_keyboards),
                                     string_equals(held, (string_address)locale_keyboards[at].name));

        return_if(found == array_count(locale_keyboards),
                  host_refuse("keyboard %s has no XKB name\n", held));
        host_say(log, "%s\n", (string_address)locale_keyboards[found].xkb);
        return 0;
}

static b32 host_keyboard(string_address address_to arguments, positive count)
{
        string_address word = count > 2 ? arguments[2] : null;

        locale_zone_own();
        if (count == 2)
                return locale_keyboard_status();
        //      repeat is the rows of the settings table: delay and rate, each by its own words.
        if (string_equals(word, "repeat"))
                return count == 3 ? host_setting_group(arguments + 1, 2) : host_usage();
        return_if(count != 3, host_usage());
        if (string_equals(word, "list"))
                return locale_keyboard_list();
        if (string_equals(word, "xkb"))
                return locale_keyboard_xkb();
        host_need_root();
        return locale_keyboard_set(word);
}

/*
        moonwater desktop: what is on the screen once the machine has started.

        The compositor is the desktop unless this says otherwise. A bowl profile's
        session can be the desktop instead (bowl profile desktop installs one):
        it takes the display from Canvas the way any program that opens the card
        does, Canvas lets go and drops the keys while it holds it, and has the
        keyboard back when it ends. Canvas still starts first. off is the text
        console: Canvas is turned off as soon as it has started.

        The choice is /root/desktop, as the keyboard's is /root/keyboard. The
        machine script's canvas on line runs `desktop boot`, once for each boot,
        which waits for the verdict so that /bowls is there and starts the
        choice in a session of its own, and `desktop start` and `desktop stop`
        are the same by hand. A session is its leader's number, which is its
        session's too, and stopping is ending everything in it, again and again
        until nothing is: KDE's wrapper starts KWin again when only KWin ends.
*/
#define DESKTOP_PATH "/root/desktop"
#define DESKTOP_SESSION HOST_STATE "/desktop"
#define DESKTOP_BOOTED HOST_STATE "/desktop.booted"
#define DESKTOP_LOG HOST_STATE "/desktop.log"
#define DESKTOP_MEMBERS 1024
#define DESKTOP_GRACE_PASSES 5
#define DESKTOP_PASSES 25
#define DESKTOP_PASS_NS ((p64)200000000)

static string_address desktop_choice(p8 address_to name, positive room)
{
        if (host_read_word(DESKTOP_PATH, name, room) <= 0 || !name[0])
                string_copy_bounded(name, "canvas", room);
        return (string_address)name;
}

/* Whether the profile's session script is where `bowl profile` puts it. */
static bool desktop_installed(string_address name)
{
        p8 path[HOST_PATH_ROOM];
        p8 want[HOST_NAME_ROOM + 40];
        p8 held[HOST_NAME_ROOM + 40];
        bipolar got;

        return_if(!bowl_find_profile(name) ||
                      !host_join(path, sizeof(path), BOWL_EXPOSE_DIRECTORY "/", name) ||
                      !bowl_profile_header(want, sizeof(want), name),
                  false);

        got = file_read_once_at(AT_FDCWD, path, held, sizeof(held));
        return got >= (bipolar)string_length(want) &&
               !string_compare_max(held, want, string_length(want));
}

static positive desktop_scan(positive session, p32 address_to pids, positive room,
                             bool address_to leader);

// The session of a KWin that is running, 0 when none is: how a session nobody recorded is found.
static positive desktop_compositor_session(void)
{
        static system_snapshot sample;

        return_if(!system_snapshot_take(address_of sample, SPARK_SNAPSHOT_PROCESS, false), 0);
        for (positive at = 0; at < sample.header.process_count; at++)
        {
                const struct snapshot_process address_to process = sample.processes + at;

                if (process->state != 'Z' && process->session > 1 &&
                    !string_compare_max((string_address)process->command, "kwin_wayland", sizeof("kwin_wayland") - 1))
                        return process->session;
        }
        return 0;
}

/*
        The session desktop off and stop end: the one the verb recorded, and where nothing of it is left (or none was
        recorded, as when a session was started some other way) the session of the KWin that is running. A session
        that was in no record was left running by desktop off, with Canvas gone: its compositor and the session bus
        held the keyboard, and the console came back black and deaf until a reboot.
*/
static positive desktop_session(void)
{
        p8 word[24];
        p32 member;
        bool leader;
        positive recorded = host_read_word(DESKTOP_SESSION, word, sizeof(word)) <= 0 ? 0 : string_to_positive(word);
        positive found;

        return_if(recorded && desktop_scan(recorded, address_of member, 1, address_of leader), recorded);
        found = desktop_compositor_session();
        return found ? found : recorded;
}

/*
        The processes of a session: how many there are, the first room of them
        in pids, and whether its own leader is one. A session that was logged
        out of leaves what it started behind it, PipeWire and its manager among
        them, with no leader and nothing on the screen: that is not a desktop
        running, though it is something stop still ends.
*/
static positive desktop_scan(positive session, p32 address_to pids, positive room,
                             bool address_to leader)
{
        static system_snapshot sample;
        positive found = 0;

        address_to leader = false;
        return_if(!session ||
                      !system_snapshot_take(address_of sample, SPARK_SNAPSHOT_PROCESS, false),
                  0);

        for (positive at = 0; at < sample.header.process_count; at++)
        {
                const struct snapshot_process address_to process = sample.processes + at;

                if (process->session != session || process->state == 'Z')
                        continue;

                address_to leader |= process->pid == session;
                if (found < room)
                        pids[found] = process->pid;
                found++;
        }

        return found;
}

/* The session script of a profile, started where nothing else is. */
static b32 desktop_launch(string_address name)
{
        p8 text[HOST_PATH_ROOM];
        p8 number[24];
        bipolar output;
        bipolar child;

        host_state_ready();
        return_if(!host_join(text, sizeof(text), "export MOONWATER_SESSION=1; exec " BOWL_EXPOSE_DIRECTORY "/", name),
                  host_refuse("%s is too long a name\n", name));

        output = host_open_state(AT_FDCWD, DESKTOP_LOG, 0600, FILE_WRITE);
        child = host_event_start(text, output);
        if (output >= 0)
                system_close(output);
        return_if(child < 0, host_fail("desktop", child));

        positive_into_string(number, (positive)child);
        host_write_text(DESKTOP_SESSION, number);
        return 0;
}

/*
        Every process of the last session, ended: asked to finish for a second,
        then killed, until none is left. Done from a process of its own in a
        session of its own, since the shell that asks is often one of the
        session's. Whether it did, as a status.
*/
static b32 desktop_end(void)
{
        positive session = desktop_session();
        p32 pids[DESKTOP_MEMBERS];
        bipolar child = system_fork();
        positive status = 0;
        bool leader;

        return_if(child < 0, host_fail("desktop", child));

        if (child)
        {
                system_wait4_retry(child, address_of status, 0, null);
                return wait_status_code(status) ? 1 : 0;
        }

        system_call(syscall(setsid));
        for (positive pass = 0; pass < DESKTOP_PASSES; pass++)
        {
                positive found = desktop_scan(session, pids, DESKTOP_MEMBERS, address_of leader);

                if (!found)
                        system_call_1(syscall(exit), 0);

                for (positive at = 0; at < found && at < DESKTOP_MEMBERS; at++)
                        system_call_2(syscall(kill), pids[at],
                                      pass < DESKTOP_GRACE_PASSES ? SIGTERM : SIGKILL);
                host_pause(DESKTOP_PASS_NS);
        }

        system_call_1(syscall(exit), 1);
        return 1;
}

#define DESKTOP_SUPERVISOR HOST_STATE "/supervisor.pid"
#define DESKTOP_COMPOSITOR HOST_STATE "/compositor.pid"
#define DESKTOP_KIDS HOST_STATE "/desktop.kids"

// The pid a file of the machine script holds, or 0.
static positive desktop_pid_of(string_address path)
{
        p8 word[24];

        return_if(host_read_word(path, word, sizeof(word)) <= 0, 0);
        return string_to_positive(word);
}

static bool desktop_alive(positive pid)
{
        return pid > 1 && system_call_2(syscall(kill), pid, 0) == 0;
}

/*
        The compositor of the machine's own desktop, ended for another desktop to have the display. A profile's
        session (KWin), weston or any other compositor cannot become the card's master while this one is: KWin
        says it has no outputs and Plasma that it has no screen to put a panel on. The supervisor goes first so that
        the compositor is not started again, as the machine script's desktop_stop does it, and what the compositor
        carried (the log, the terminals, the key answerer) goes after it, the programs the script kept the pids of.
        Nothing is done where no compositor is up.
*/
static fn desktop_yield(void)
{
        positive supervisor = desktop_pid_of((string_address)DESKTOP_SUPERVISOR),
                 compositor = desktop_pid_of((string_address)DESKTOP_COMPOSITOR);
        p8 kids[256];
        bipolar got;

        if (supervisor > 1)
                system_call_2(syscall(kill), supervisor, SIGTERM);
        if (compositor > 1)
                system_call_2(syscall(kill), compositor, SIGTERM);
        for (positive pass = 0; pass < 60 && desktop_alive(compositor); pass++)
                host_pause(50000000);
        if (desktop_alive(compositor))
        {
                system_call_2(syscall(kill), compositor, SIGKILL);
                host_pause(100000000);
        }

        got = file_read_once_at(AT_FDCWD, (p8 address_to)DESKTOP_KIDS, kids, sizeof(kids));
        for (positive at = 0, pid = 0, digits = 0; got > 0 && at <= (positive)got; at++)
        {
                p8 byte = at < (positive)got ? kids[at] : '\n';

                if (byte >= '0' && byte <= '9' && digits < 9)
                        pid = pid * 10 + (positive)(byte - '0'), digits++;
                else
                {
                        if (digits && pid > 1)
                                system_call_2(syscall(kill), pid, SIGTERM);
                        pid = digits = 0;
                }
        }
        (void)system_remove_at(AT_FDCWD, DESKTOP_SUPERVISOR, 0);
        (void)system_remove_at(AT_FDCWD, DESKTOP_COMPOSITOR, 0);
        (void)system_remove_at(AT_FDCWD, DESKTOP_KIDS, 0);
}

// Whether Canvas is running in the kernel: -1 when it cannot be asked.
static int desktop_canvas_running(void)
{
        struct canvas_control control;

        return host_canvas_request(SPARK_CANVAS_STATUS, address_of control) < 0 ? -1 : control.running != 0;
}

/*
        The choice, put in force. on is the compositor: a session of a profile ends, and Canvas is turned on if it is
        off, or off and on if it is on with no compositor (the machine script starts the compositor from the event
        that turns it on). off is the text console: a session ends and Canvas is turned off. A profile is its
        session, over Canvas, with the compositor out of its way. At boot the machine script has started the
        compositor already, or has not for a profile, so on is nothing then.
*/
static b32 desktop_put(string_address name, bool boot)
{
        p32 pid;
        bool leader;
        positive left;

        if (string_equals(name, "canvas") || string_equals(name, "off"))
        {
                bool want = string_equals(name, "canvas");

                return_if(want && boot, 0);
                left = desktop_scan(desktop_session(), address_of pid, 1, address_of leader);
                return_if(left && desktop_end(), host_refuse("what is left of the last session would not end\n"));

                if (!want)
                {
                        //      The machine script ends the compositor from the event Canvas off fires; the card is
                        //      free when it has gone, and a compositor started straight after (weston, a session)
                        //      is refused its master until then.
                        positive compositor = desktop_pid_of((string_address)DESKTOP_COMPOSITOR);

                        return_if(desktop_canvas_running() <= 0, 0);
                        return_if(host_canvas_off(), 1);
                        for (positive pass = 0; pass < 100 && desktop_alive(compositor); pass++)
                                host_pause(50000000);
                        return 0;
                }
                if (desktop_canvas_running() <= 0)
                        return host_canvas_on();
                return_if(desktop_alive(desktop_pid_of((string_address)DESKTOP_COMPOSITOR)), 0);
                return_if(host_canvas_off(), 1);
                return host_canvas_on();
        }

        return_if(!desktop_installed(name),
                  host_refuse("the profile %s is not installed: bowl profile installs it\n", name));
        left = desktop_scan(desktop_session(), address_of pid, 1, address_of leader);
        return_if(leader, host_refuse("the desktop %s is already running\n", name));
        return_if(left && desktop_end(),
                  host_refuse("what is left of the last session would not end\n"));

        // The compositor of the machine's own desktop has the card's master, and a session cannot have the display
        // until it lets go: not at boot, where none was started for a profile, but when the choice is made on a
        // machine that is up.
        desktop_yield();
        return desktop_launch(name);
}

static b32 desktop_start(bool boot)
{
        p8 name[HOST_NAME_ROOM];

        desktop_choice(name, sizeof(name));
        return desktop_put((string_address)name, boot);
}

/*
        From the machine script, when Canvas has started: the choice, once for
        a boot, in a process of its own that waits for the disks. The script
        goes on to its next line at once: the events behind it wait on it.
*/
static b32 desktop_boot(void)
{
        bipolar made;

        host_need_root();
        host_state_ready();
        made = system_open_at_mode(AT_FDCWD, DESKTOP_BOOTED,
                                   FILE_WRITE | FILE_EXCLUSIVE | O_NOFOLLOW | O_CLOEXEC,
                                   0600);
        return_if(made < 0, 0);
        system_close(made);
        return_if(system_fork(), 0);

        system_call(syscall(setsid));
        host_verdict_settled();
        system_call_1(syscall(exit), (positive)desktop_start(true));
        return 0;
}

static b32 desktop_stop(void)
{
        p32 pid;
        bool leader;

        host_need_root();
        return_if(!desktop_scan(desktop_session(), address_of pid, 1, address_of leader),
                  host_refuse("no desktop session is running\n"));
        return_if(desktop_end(), host_refuse("the desktop session would not end\n"));
        host_say(log, host_label "desktop stopped\n");
        return 0;
}

static b32 desktop_status(void)
{
        p8 name[HOST_NAME_ROOM];
        p32 pid;
        bool leader;

        desktop_choice(name, sizeof(name));
        if (string_equals(name, "canvas") || string_equals(name, "off"))
                host_say(log, host_label "desktop %s\n", string_equals(name, "canvas") ? (string_address) "on" : (string_address)name);
        else if (!desktop_installed(name))
                host_say(log, host_label "desktop %s, not installed: bowl profile %s "
                                         "installs it\n", (string_address)name,
                         (string_address)name);
        else
        {
                desktop_scan(desktop_session(), address_of pid, 1, address_of leader);
                host_say(log, host_label "desktop %s, %s\n", (string_address)name,
                         leader ? "running" : "not running");
        }
        return 0;
}

/* moonwater desktop [on|off|PROFILE|start|stop]: the choice kept and put in force now. */
static b32 host_desktop(string_address address_to arguments, positive count)
{
        string_address word = count > 2 ? arguments[2] : null;
        string_address kept;

        return_if(count == 2, desktop_status());
        return_if(count != 3, host_usage());

        return_if(string_equals(word, "boot"), desktop_boot());
        if (string_equals(word, "start"))
        {
                host_need_root();
                return desktop_start(false);
        }
        return_if(string_equals(word, "stop"), desktop_stop());

        host_need_root();
        kept = string_equals(word, "on") ? (string_address) "canvas" : word;
        if (!string_equals(word, "on") && !string_equals(word, "off"))
        {
                return_if(!bowl_find_profile(word),
                          host_refuse("unknown desktop %s: on, off, or a profile of "
                                      "bowl profile\n", word));
                return_if(!desktop_installed(word),
                          host_refuse("the profile %s is not installed: bowl profile "
                                      "installs it\n", word));
        }

        return_if(host_save("desktop", DESKTOP_PATH, kept), 1);
        host_say(log, host_label "desktop %s\n", word);
        return desktop_put(kept, false);
}

/*
        moonwater dns [ADDRESS...|auto]: the servers names are asked of.

        One to three IPv4 addresses are kept in /root/dns and written to
        /etc/resolv.conf at once, and every lease after writes them in place
        of its own resolver: a lease's server is the network's, which is right
        until it is not (a router that is slow, or that answers for names the
        machine would rather not send there). auto forgets them, and the file
        is written the way the lease would write it. Bare says what the file
        names now and whose choice that is. 1.1.1.1 is neither: it is the
        server of a network that named none, and where a lookup asks when the
        file names no server at all.
*/
/*
        moonwater dns tls [on|off|only]: whether names are asked over TLS
        (the DNS over TLS comment in net.c says how). Kept in /root/dns.tls
        and published in /run/moonwater/dns.tls, which the resolver reads,
        so that a lookup by a user who cannot read /root follows it too; no
        word kept is no published file, and then the tier's default holds.
*/
#define DNS_TLS_KEPT "/root/dns.tls"

static fn dns_tls_publish(void)
{
        p8 word[16];

        host_read_word(DNS_TLS_KEPT, word, sizeof(word));
        if (!word[0])
        {
                (void)system_remove_at(AT_FDCWD, DNS_TLS_PATH, 0);
                return;
        }
        host_state_ready();
        (void)radio_write_word(DNS_TLS_PATH, (string_address)word);
}

static string_address dns_tls_word(positive mode)
{
        return mode == DNS_TLS_ON     ? (string_address) "on"
               : mode == DNS_TLS_ONLY ? (string_address) "only"
                                      : (string_address) "off";
}

static fn dns_say(bool label)
{
        p32 servers[DNS_SERVERS_MAX];
        p32 kept[DNS_SERVERS_MAX];
        p8 held[96];
        p8 text[24];
        positive count = dns_servers((string_address)"/etc/resolv.conf",
                                     servers, array_count(servers));

        if (label)
                string_format(log, host_label "dns");
        else
                string_format(log, "  dns");
        for (positive at = 0; at < count; at++)
                string_format(log, " %s", net_host_text(text, servers[at]));
        if (!count)
                string_format(log, " none named, lookups ask %s",
                              net_host_text(text, DNS_FALLBACK));
        else if (!locale_readable(NET_DNS_PATH, held, sizeof(held)))
                string_format(log, " " LOCALE_NEEDS_ROOT);
        else
                string_format(log, "%s", net_dns_kept(kept) ? " (kept)" : " (the network's)");
        //      The label's escapes are format text: as an argument %s would
        //      print them as the characters they are spelt with.
        host_say(log, label ? "\n" host_label "dns tls %s\n" : "\n  dns tls %s\n",
                 dns_tls_word(dns_tls_mode()));
}

static b32 dns_tls_verb(string_address address_to arguments, positive count)
{
        string_address word = count > 3 ? arguments[3] : null;

        if (count == 4)
        {
                return_if(!string_equals(word, "on") && !string_equals(word, "off") &&
                          !string_equals(word, "only"), host_usage());
                host_need_root();
                return_if(host_save("dns tls", DNS_TLS_KEPT, word), 1);
                dns_tls_publish();
        }
        else
                return_if(count != 3, host_usage());
        host_say(log, host_label "dns tls %s\n", dns_tls_word(dns_tls_mode()));
        return 0;
}

static b32 dns_set(string_address address_to words, positive count)
{
        p32 servers[DNS_SERVERS_MAX];
        p8 line[96];
        bipolar failed;

        if (count > DNS_SERVERS_MAX)
                return host_refuse("dns takes %p servers at most, which is all a lookup "
                                   "reads; auto for the network's\n",
                                   (positive)DNS_SERVERS_MAX);
        line[0] = end;
        for (positive at = 0; at < count; at++)
        {
                bipolar host = string_to_host(words[at]);

                if (!net_dns_usable(host))
                        return host_refuse("%s is not a server address: a dotted IPv4 "
                                           "address a machine can be at, or auto for the "
                                           "network's\n", words[at]);
                for (positive before = 0; before < at; before++)
                        if (servers[before] == (p32)host)
                                return host_refuse("%s is named twice\n", words[at]);
                servers[at] = (p32)host;
                if (at)
                        string_append_bounded(line, " ", sizeof(line));
                string_append_bounded(line, words[at], sizeof(line));
        }

        failed = radio_write_word(NET_DNS_PATH, line);
        return_if(failed < 0, host_fail("dns", failed));
        failed = net_write_resolv_servers((string_address) "/etc/resolv.conf",
                                          servers, count);
        return_if(failed < 0, host_fail("kept, but /etc/resolv.conf", failed));
        dns_say(true);
        return 0;
}

static b32 dns_auto(void)
{
        bipolar failed = system_remove_at(AT_FDCWD, NET_DNS_PATH, 0);

        return_if(failed < 0 && failed != -ENOENT, host_fail("dns", failed));
        failed = net_write_resolv(net_dns_lease());
        return_if(failed < 0, host_fail("forgotten, but /etc/resolv.conf", failed));
        dns_say(true);
        return 0;
}

//      What was kept is the file before any lease writes it: a machine that
//      boots with no network at all still asks the servers it was told to.
static fn dns_restore(void)
{
        p32 servers[DNS_SERVERS_MAX];
        positive count = net_dns_kept(servers);

        dns_tls_publish();
        if (count)
                (void)net_write_resolv_servers((string_address) "/etc/resolv.conf",
                                               servers, count);
}

static b32 host_dns(string_address address_to arguments, positive count)
{
        if (count == 2)
        {
                dns_say(true);
                return 0;
        }
        if (string_equals(arguments[2], "tls"))
                return dns_tls_verb(arguments, count);
        host_need_root();
        return count == 3 && string_equals(arguments[2], "auto") ? dns_auto()
                                                                 : dns_set(arguments + 2, count - 2);
}

/*
        moonwater security: what TLS trusts and refuses besides what is built
        in.

        Two lists, each added to and taken from the same way. trust is the
        anchors: anchors.inc's, and a CA certificate root names (a private or
        a company's CA), which is then trusted as a built-in one is and held
        to the same rules -- a CA, a key that may sign certificates, P-256,
        P-384 or RSA of 2048 to 4096 bits, and no name constraints, since the
        built-in anchors carry none and one added would be trusted outside
        them. block is the keys refused, by the SHA-256 of the DER
        SubjectPublicKeyInfo (what openssl, Chrome's CRLSet and Mozilla's
        OneCRL name a key by, and what a Certificate Transparency log's id
        is): a built-in anchor so named stops being one, a CA under it stops
        being a step to one, a leaf of it is refused, and a CT log of it
        counts no SCT. Taking a built-in key out of trust is blocking it; a
        block wins over a trust of the same key; and tls_blocked_keys, which
        are built in, stay refused, since nothing here can lift them.

        Both are kept in the security list of the image's settings (as init
        and exit are, so they go along with setup install and update) and
        published in /run/moonwater/security, which every TLS client reads
        and only root can write (tls_overlay_read in net.c says how it is
        read). Bare says what that file holds, to anybody.
*/
#define HOST_SECURITY TLS_OVERLAY_PATH
#define HOST_SECURITY_DER_MOST 8192

/*
        The switches, each its word, the word that is its default, the word
        that turns it (an entry of the security list, TLS_SWITCH_*), and
        whether the tight tier has it turned whatever is kept. Each only
        tightens, so none can take back what a build or a tier asks.
*/
static const struct
{
        string_address word;
        string_address standard;
        string_address turned;
        p16 id;
        bool tight;
} security_switches[] = {
    {"tls12", "on", "off", TLS_SWITCH_TLS12_OFF, false},
    {"aia", "on", "off", TLS_SWITCH_AIA_OFF, false},
    {"revocation", "soft", "hard", TLS_SWITCH_REVOCATION_HARD, true},
    {"ct", "off", "on", TLS_SWITCH_CT_ON, true},
    {"http", "on", "off", TLS_SWITCH_HTTP_OFF, false},
};

/* The file of what the settings' security list holds, or none when it holds
   nothing, which is the built-in trust as it has always been. */
static fn security_publish(host_settings address_to settings)
{
        static p8 file[TLS_OVERLAY_HEAD + TLS_OVERLAY_ROOM];
        host_setting setting;
        positive used = TLS_OVERLAY_HEAD;
        positive at = 0;

        while (settings && host_settings_next(settings, address_of at, address_of setting))
        {
                positive size = SPARK_SETTINGS_ENTRY + spark_settings_padded(setting.entry.length);

                if (setting.entry.list != SPARK_SETTINGS_SECURITY ||
                    used + size > sizeof file)
                        continue;
                memory_copy_apart(file + used, settings->payload + setting.at, size);
                used += size;
        }
        if (used == TLS_OVERLAY_HEAD)
        {
                (void)system_remove_at(AT_FDCWD, HOST_SECURITY, 0);
                return;
        }
        memory_copy_apart(file, TLS_OVERLAY_MAGIC, 8);
        for (positive k = 0; k < 4; k++)
        {
                file[8 + k] = (p8)((used - TLS_OVERLAY_HEAD) >> (8 * k));
                file[12 + k] = 0;
        }
        host_state_ready();
        (void)file_publish_bytes(HOST_SECURITY, file, used, 0644, false);
}

/* A key as it is named on a line: 64 hexadecimal digits. */
static string_address security_hex(const p8 address_to digest)
{
        static p8 text[65];

        memory_into_hex(text, (address_any)digest, 32);
        text[64] = end;
        return (string_address)text;
}

/* A key's type as a column of eight: P-256, P-384, or RSA and its bits. */
static string_address security_kind(p8 curve, positive length)
{
        static p8 text[16];
        positive used;

        if (curve == 1)
                return (string_address) "P-256   ";
        if (curve == 2)
                return (string_address) "P-384   ";
        string_copy(text, "RSA-");
        used = 4 + positive_into_string(text + 4, length * 8);
        while (used < 8)
                text[used++] = ' ';
        text[used] = end;
        return (string_address)text;
}

/* A key named on the line: 64 hexadecimal digits, or the 44 characters of
   base64 a Certificate Transparency log list spells a log id in. */
static bool security_digest_word(string_address word, p8 address_to digest)
{
        positive length = string_length(word);
        bool loose = false;

        return (length == 64 && memory_from_hex(digest, word, 32)) ||
               (length == 44 && memory_from_base64(digest, word, 44, 32, address_of loose) &&
                !loose);
}

/* A certificate in a file, PEM (the first CERTIFICATE block) or DER, into der:
   its length, or 0 when the file holds neither. */
static positive security_certificate_read(string_address path, p8 address_to der,
                                          positive room)
{
        static p8 text[HOST_SECURITY_DER_MOST * 2];
        static p8 joined[HOST_SECURITY_DER_MOST * 2];
        static const char begin[] = "-----BEGIN CERTIFICATE-----";
        static const char finish[] = "-----END CERTIFICATE-----";
        bipolar got = file_slurp_regular_at(AT_FDCWD, path, text, sizeof text, 0);
        string_address start;
        string_address stop;
        positive used = 0;
        positive bytes;
        bool loose = false;

        if (got <= 0 || (positive)got >= sizeof text - 1)
                return 0;
        if (text[0] == 0x30)
        {
                if ((positive)got > room)
                        return 0;
                memory_copy(der, text, (positive)got);
                return (positive)got;
        }
        start = string_find((string_address)text, begin);
        stop = start ? string_find(start, finish) : null;
        if (!stop)
                return 0;
        for (start += sizeof begin - 1; start < stop; start++)
                if (*start != '\n' && *start != '\r' && *start != ' ' && *start != '\t')
                        joined[used++] = (p8)*start;
        if (used < 4 || used % 4)
                return 0;
        bytes = used / 4 * 3 - (joined[used - 1] == '=') - (joined[used - 2] == '=');
        if (bytes > room ||
            !memory_from_base64(der, (string_address)joined, used, bytes, address_of loose) ||
            loose)
                return 0;
        return bytes;
}

/* A key named by a word: its digest, from the word or from the certificate
   in the file it names. Says why not and answers false. */
static bool security_digest_of(string_address word, p8 address_to digest)
{
        static p8 der[HOST_SECURITY_DER_MOST];
        tls_cert cert;
        positive length;

        if (security_digest_word(word, digest))
                return true;
        length = security_certificate_read(word, der, sizeof der);
        if (!length)
        {
                host_refuse("%s is not a key's SHA-256 (64 hexadecimal digits, or 44 "
                            "characters of base64) or a file with a certificate in it\n",
                            word);
                return false;
        }
        if (tls_parse_cert(der, length, address_of cert, null) ||
            !tls_spki_digest(address_of cert, true, digest))
        {
                host_refuse("%s holds a certificate this cannot read\n", word);
                return false;
        }
        return true;
}

/* The settings entry an anchor added is kept as: its curve, three zero bytes,
   its RSA exponent low first, the SHA-256 of its subject Name and its key
   (tls_overlay_read). Its length, or 0 with the reason said. */
static positive security_anchor_entry(string_address path, p8 address_to entry,
                                      p8 address_to digest)
{
        static p8 der[HOST_SECURITY_DER_MOST];
        byte_reader text;
        tls_added_anchor added;
        tls_cert cert;
        positive length = security_certificate_read(path, der, sizeof der);
        positive key_length;

        if (!length)
                return host_refuse("%s is not a file with a certificate in it, PEM or "
                                   "DER\n", path), 0;
        if (tls_parse_cert(der, length, address_of cert, null))
                return host_refuse("%s holds a certificate this cannot read\n", path), 0;
        if (!cert.ca || (cert.key_usage && !cert.key_cert_sign))
                return host_refuse("%s is not a CA: it does not say CA:TRUE, or its key may "
                                   "not sign certificates\n", path), 0;
        if (cert.name_constraints)
                return host_refuse("%s has name constraints: the built-in anchors carry "
                                   "none, so it would be trusted for every name\n", path), 0;
        if (tls_key_blocked(address_of cert))
                return host_refuse("%s has a key that is refused: built in as compromised, "
                                   "or one anyone can compute\n", path), 0;
        memory_zero(entry, SPARK_SETTINGS_TRUST_MOST);
        entry[0] = cert.curve;
        if (cert.curve == 3)
        {
                positive skip = 0;

                while (skip < cert.modulus_length && !cert.modulus[skip])
                        skip++;
                key_length = cert.modulus_length - skip;
                if (key_length > 512 || cert.exponent > 0xffffffffull)
                        return host_refuse("%s has a key the built-in anchors could not "
                                           "have: P-256, P-384, or RSA of 2048 to 4096 bits "
                                           "with an odd exponent\n", path), 0;
                for (positive k = 0; k < 4; k++)
                        entry[4 + k] = (p8)(cert.exponent >> (8 * k));
                memory_copy(entry + 40, cert.modulus + skip, key_length);
        }
        else
        {
                key_length = cert.curve == 1 ? 64 : 96;
                memory_copy(entry + 40, cert.qx + 48 - key_length / 2, key_length / 2);
                memory_copy(entry + 40 + key_length / 2, cert.qy + 48 - key_length / 2,
                            key_length / 2);
        }
        crypto_sha256_of(cert.subject, cert.subject_length, entry + 8);
        //      Held to what the reader of the published file takes, by the reader.
        text = byte_reader_open(entry, 40 + key_length);
        if (!tls_overlay_anchor_read(address_of text, address_of added))
                return host_refuse("%s has a key the built-in anchors could not have: "
                                   "P-256, P-384, or RSA of 2048 to 4096 bits with an odd "
                                   "exponent\n", path), 0;
        memory_copy(digest, added.spki, 32);
        return 40 + key_length;
}

/* The digest of a security entry's key, a block's or an anchor's. */
static bool security_entry_digest(host_setting address_to setting, p8 address_to digest)
{
        byte_reader text = byte_reader_open(setting->text, setting->entry.length);
        tls_added_anchor added;

        if (setting->entry.kind == SPARK_SETTINGS_BLOCK && setting->entry.length == 32)
        {
                memory_copy(digest, setting->text, 32);
                return true;
        }
        if (setting->entry.kind != SPARK_SETTINGS_TRUST ||
            !tls_overlay_anchor_read(address_of text, address_of added))
                return false;
        memory_copy(digest, added.spki, 32);
        return true;
}

/* The entry of a kind whose key is digest, or whose key's hex starts with the
   word given (eight digits at least, and only one such): false when none or
   more than one is. */
static bool security_entry_find(host_settings address_to settings, p8 kind,
                                string_address word, const p8 address_to digest,
                                host_setting address_to into)
{
        host_setting setting;
        positive at = 0;
        positive found = 0;
        positive length = string_length(word);

        while (host_settings_next(settings, address_of at, address_of setting))
        {
                p8 have[32];

                if (setting.entry.list != SPARK_SETTINGS_SECURITY || setting.entry.kind != kind ||
                    !security_entry_digest(address_of setting, have))
                        continue;
                if (digest ? !memory_compare(have, digest, 32)
                           : length >= 8 && length < 64 &&
                                 !memory_compare_ascii_case(security_hex(have), word, length))
                {
                        address_to into = setting;
                        found++;
                }
        }
        return found == 1;
}

//      Which of tls_blocked_keys a digest is, or -1.
static bipolar security_built_in_block(const p8 address_to digest)
{
        for (positive i = 0; i < array_count(tls_blocked_keys); i++)
                if (!memory_compare(tls_blocked_keys[i], digest, 32))
                        return (bipolar)i;
        return -1;
}

//      Which of tls_anchors has the key of a digest, or -1.
static bipolar security_built_in_anchor(const p8 address_to digest)
{
        p8 have[32];
        tls_cert root;

        for (positive i = 0; i < array_count(tls_anchors); i++)
                if (tls_anchor_key(tls_anchors + i, address_of root) &&
                    tls_spki_digest(address_of root, false, have) &&
                    !memory_compare(have, digest, 32))
                        return (bipolar)i;
        return -1;
}

/* What the published file holds, as the clients read it, for the lines. */
static tls_overlay security_shown;

static bool security_read(void)
{
        tls_overlay_load(address_of security_shown);
        if (security_shown.state != TLS_OVERLAY_BAD)
                return true;
        host_say(log, host_label "security: " HOST_SECURITY " does not read, so no "
                                 "certificate is trusted until it does; boot and every "
                                 "change here write it again\n");
        return false;
}

static fn security_trust_lines(bool every)
{
        p8 digest[32];
        tls_cert root;

        if (!every)
                string_format(log, "  trust  built-in  %p anchors, which moonwater security "
                                   "trust lists\n",
                              (positive)array_count(tls_anchors));
        for (positive i = 0; every && i < array_count(tls_anchors); i++)
                if (tls_anchor_key(tls_anchors + i, address_of root) &&
                    tls_spki_digest(address_of root, false, digest))
                        string_format(log, "  trust  built-in  %s  %s%s\n",
                                      security_kind(tls_anchors[i].curve, tls_anchors[i].key_length),
                                      security_hex(digest),
                                      tls_anchor_refused(i) ? "  (blocked)" : "");
#ifdef TLS_BENCH_ANCHOR
        //      A harness's own root, which only its builds have.
        if (every)
        {
                p8 key[96];

                memory_copy(key, tls_bench_anchor_x, 48);
                memory_copy(key + 48, tls_bench_anchor_y, 48);
                if (tls_kept_key(2, 0, key, sizeof key, address_of root) &&
                    tls_spki_digest(address_of root, false, digest))
                        string_format(log, "  trust  built-in  %s  %s%s\n", security_kind(2, 96),
                                      security_hex(digest), tls_bench_refused() ? "  (blocked)" : "");
        }
#endif
        for (positive i = 0; i < security_shown.anchors; i++)
                string_format(log, "  trust  added     %s  %s%s\n",
                              security_kind(security_shown.anchor[i].curve,
                                            security_shown.anchor[i].key_length),
                              security_hex(security_shown.anchor[i].spki),
                              security_shown.anchor[i].refused ? "  (blocked)" : "");
}

static fn security_block_lines(void)
{
        for (positive i = 0; i < array_count(tls_blocked_keys); i++)
                string_format(log, "  block  built-in  key       %s\n",
                              security_hex(tls_blocked_keys[i]));
        for (positive i = 0; i < security_shown.blocks; i++)
                string_format(log, "  block  added     key       %s\n",
                              security_hex(security_shown.block[i]));
}

/* A switch's line: its word and where it stands, as the file and the tier
   have it. */
static fn security_switch_line(positive at, bool label)
{
        bool forced = security_switches[at].tight && MOONWATER_STRICT_NET >= STRICT_TIGHT;
        bool turned = forced || ((security_shown.switches >> (security_switches[at].id - 1)) & 1);

        string_format(log, label ? host_label "security %s %s%s\n" : "  %s %s%s\n",
                      security_switches[at].word,
                      turned ? security_switches[at].turned : security_switches[at].standard,
                      forced ? ", as the tight tier has it" : "");
}

static b32 security_say(string_address which)
{
        bool readable = security_read();

        //      The lines name the anchors this process's overlay refuses.
        memory_copy_apart(address_of tls_overlay_now, address_of security_shown,
                          sizeof tls_overlay_now);
        for (positive at = 0; which && at < array_count(security_switches); at++)
                if (string_equals(which, security_switches[at].word))
                        security_switch_line(at, true);
        for (positive at = 0; !which && at < array_count(security_switches); at++)
                security_switch_line(at, false);
        if (!which || string_equals(which, "trust"))
                security_trust_lines(which != null);
        if (!which || string_equals(which, "block"))
                security_block_lines();
        log_flush();
        return readable ? 0 : 1;
}

/* A switch set to a word: its entry added when the word turns it, taken out
   when the word is its default, then saved and published. */
static b32 security_switch(positive which, string_address word)
{
        host_settings settings;
        host_setting setting;
        host_setting found;
        bool turn = string_equals(word, security_switches[which].turned);
        bool have = false;
        positive at = 0;
        p16 id = security_switches[which].id;
        string_address failed;
        b32 answer = 0;

        return_if(!turn && !string_equals(word, security_switches[which].standard), host_usage());
        host_need_root();
        if (!host_acquire())
                return 1;
        host_state_ready();
        host_settings_session(address_of settings);
        while (host_settings_next(address_of settings, address_of at, address_of setting))
                if (setting.entry.list == SPARK_SETTINGS_SECURITY &&
                    setting.entry.kind == SPARK_SETTINGS_SWITCH && setting.entry.id == id)
                {
                        found = setting;
                        have = true;
                }
        if (have == turn)
        {
                host_say(log, host_label "security %s %s\n", security_switches[which].word, word);
                goto done;
        }
        if (turn)
        {
                failed = host_settings_add(address_of settings, SPARK_SETTINGS_SECURITY,
                                           SPARK_SETTINGS_SWITCH, (string_address) "", 0,
                                           address_of id);
                if (failed)
                {
                        answer = host_settings_refused("security", failed, address_of settings);
                        goto done;
                }
        }
        else
                host_settings_drop(address_of settings, address_of found);
        string_format(log, host_label "security %s %s; ", security_switches[which].word, word);
        answer = host_settings_save(address_of settings) ? 0 : 1;
        security_publish(address_of settings);
        if (!turn && security_switches[which].tight && MOONWATER_STRICT_NET >= STRICT_TIGHT)
                host_say(log, host_label "security %s stays %s: the tight tier has it so\n",
                         security_switches[which].word, security_switches[which].turned);

done:
        host_release();
        return answer;
}

/* One entry added to or taken from a list, then saved and published. */
static b32 security_change(bool trust, bool adding, string_address word)
{
        static p8 entry[SPARK_SETTINGS_TRUST_MOST];
        host_settings settings;
        host_setting setting;
        string_address list = trust ? (string_address) "trust" : (string_address) "block";
        string_address failed;
        p8 digest[32];
        positive length = 32;
        bool named = true;
        b32 answer = 0;
        p16 id = SPARK_SETTINGS_BY_HAND;

        host_need_root();
        //      What the word names is read before anything waits: a file that
        //      is no certificate costs a line and not the settings' lock. A
        //      remove also takes the first hexadecimal digits of a key that
        //      was added, eight at least, as its line shows them.
        if (trust && adding)
        {
                length = security_anchor_entry(word, entry, digest);
                return_if(!length, 1);
        }
        else if (adding || security_digest_word(word, digest) ||
                 system_access_at(AT_FDCWD, word, 0) == 0)
        {
                return_if(!security_digest_of(word, digest), 1);
        }
        else
        {
                positive size = string_length(word);
                bool odd = false;

                named = false;
                for (positive at = 0; at < size; at++)
                        odd |= !byte_is_hexadecimal((p8)word[at]);
                return_if(odd || size < 8,
                          host_refuse("security %s remove takes a key's SHA-256, eight "
                                      "hexadecimal digits of it at least, or a file with "
                                      "a certificate in it\n", list));
        }
        if (!trust && !adding && named && security_built_in_block(digest) >= 0)
                return host_refuse("%s is built in and stays refused; nothing here lifts "
                                   "it\n", security_hex(digest));
        if (trust && !adding && named && security_built_in_anchor(digest) >= 0)
                return host_refuse("%s is a built-in anchor; moonwater security block add "
                                   "takes it out of trust\n", security_hex(digest));
        if (adding && security_built_in_block(digest) >= 0)
        {
                host_say(log, host_label "security %s: %s is refused, built in%s\n", list,
                         security_hex(digest), trust ? ", and a block wins" : "");
                return trust ? 1 : 0;
        }
        if (trust && adding && security_built_in_anchor(digest) >= 0)
        {
                host_say(log, host_label "security trust: %s is a built-in anchor\n",
                         security_hex(digest));
                return 0;
        }
        if (!host_acquire())
                return 1;
        host_state_ready();
        host_settings_session(address_of settings);

        if (trust && adding &&
            security_entry_find(address_of settings, SPARK_SETTINGS_BLOCK, word, digest,
                                address_of setting))
        {
                answer = host_refuse("security trust: %s is blocked, and a block wins; "
                                     "moonwater security block remove takes it out\n",
                                     security_hex(digest));
                goto done;
        }
        if (adding &&
            security_entry_find(address_of settings,
                                trust ? SPARK_SETTINGS_TRUST : SPARK_SETTINGS_BLOCK, word,
                                digest, address_of setting))
        {
                host_say(log, host_label "security %s: %s is there already\n", list,
                         security_hex(digest));
                goto done;
        }
        if (adding)
        {
                positive have = 0;
                positive at = 0;

                while (host_settings_next(address_of settings, address_of at, address_of setting))
                        have += setting.entry.list == SPARK_SETTINGS_SECURITY &&
                                setting.entry.kind == (trust ? SPARK_SETTINGS_TRUST
                                                             : SPARK_SETTINGS_BLOCK);
                if (have >= (trust ? TLS_OVERLAY_ANCHORS : TLS_OVERLAY_BLOCKS))
                {
                        answer = host_refuse("security %s holds %p at most; remove one "
                                             "first\n", list,
                                             (positive)(trust ? TLS_OVERLAY_ANCHORS
                                                              : TLS_OVERLAY_BLOCKS));
                        goto done;
                }
                failed = host_settings_add(address_of settings, SPARK_SETTINGS_SECURITY,
                                           trust ? SPARK_SETTINGS_TRUST : SPARK_SETTINGS_BLOCK,
                                           trust ? (string_address)entry : (string_address)digest,
                                           length, address_of id);
                if (failed)
                {
                        answer = host_settings_refused("security", failed, address_of settings);
                        goto done;
                }
                string_format(log, host_label "security %s added %s; ", list,
                              security_hex(digest));
        }
        else
        {
                if (!security_entry_find(address_of settings,
                                         trust ? SPARK_SETTINGS_TRUST : SPARK_SETTINGS_BLOCK,
                                         word, named ? digest : null, address_of setting))
                {
                        //      host_refuse names one thing: the list is in the words.
                        answer = trust ? host_refuse("security trust has no one entry %s\n",
                                                     named ? security_hex(digest) : word)
                                       : host_refuse("security block has no one entry %s\n",
                                                     named ? security_hex(digest) : word);
                        goto done;
                }
                security_entry_digest(address_of setting, digest);
                host_settings_drop(address_of settings, address_of setting);
                string_format(log, host_label "security %s removed %s; ", list,
                              security_hex(digest));
        }
        answer = host_settings_save(address_of settings) ? 0 : 1;
        security_publish(address_of settings);

done:
        host_release();
        return answer;
}

static b32 host_security(string_address address_to arguments, positive count)
{
        bool trust = count > 2 && string_equals(arguments[2], "trust");

        if (count == 2)
                return security_say(null);
        for (positive at = 0; at < array_count(security_switches); at++)
                if (string_equals(arguments[2], security_switches[at].word))
                {
                        return_if(count > 4, host_usage());
                        return count == 3 ? security_say(arguments[2])
                                          : security_switch(at, arguments[3]);
                }
        return_if(!trust && !string_equals(arguments[2], "block"), host_usage());
        if (count == 3)
                return security_say(arguments[2]);
        return_if(count != 5 || (!string_equals(arguments[3], "add") &&
                                 !string_equals(arguments[3], "remove")),
                  host_usage());
        return security_change(trust, string_equals(arguments[3], "add"), arguments[4]);
}

/*
        Bound events, and the init and exit lists, as one verb.

        A kernel event is one line. init and exit stay lists, because more
        than one thing runs at boot and at stop. An empty line puts the
        event's default back. What is not the default is kept in the image
        and put back at the next boot.
*/
static fn host_bind_say(bool label, struct bind_control address_to control)
{
        p16 line = host_machine_event_line(control->event);
        string_address name = (string_address)control->name;
        bool said = false;

        host_prefix(label);
        if (line)
                string_format(log, "%s: %s:%p", name, host_machine_where(),
                              (positive)line);
        else if (control->command[0])
                string_format(log, "%s: %s", name,
                              host_plain((string_address)control->command));
        else if (control->flags & SPARK_BIND_DEFAULT)
                string_format(log, "%s", name);
        else
                //      The kernel keeps a line somebody set from anyone but
                //      root, and clears the DEFAULT flag so that this can say so.
                string_format(log, "%s: set, and root's to read", name);

        if (control->runs)
        {
                string_format(log, "  (ran %p time%s", (positive)control->runs,
                              control->runs == 1 ? "" : "s");
                said = true;
        }
        if (control->flags & SPARK_BIND_RUNNING)
        {
                string_format(log, said ? ", running now" : "  (running now");
                said = true;
        }
        if (control->flags & SPARK_BIND_PENDING)
        {
                string_format(log, said ? ", queued" : "  (queued");
                said = true;
        }
        string_format(log, said ? ")\n" : "\n");
}

static fn host_bind_forget(host_settings address_to settings, p16 event)
{
        host_setting setting;
        positive at;
        bool dropped;

        for (;;)
        {
                at = 0;
                dropped = false;
                while (host_settings_next(settings, address_of at, address_of setting))
                {
                        if (setting.entry.list == SPARK_SETTINGS_BIND &&
                            setting.entry.id == event)
                        {
                                host_settings_drop(settings, address_of setting);
                                dropped = true;
                                break;
                        }
                }
                if (!dropped)
                        return;
        }
}

/*
        Whether the line to be set will fit in the block that keeps it, asked
        before the kernel is: a block full of init and exit entries refused
        the line after the kernel had taken it, and the command said refused
        about a line that was live. An empty line puts the default back and
        needs no room. Null when it fits or when there is no copy to ask --
        the kernel's own refusal comes first for anyone it refuses.
*/
static string_address host_bind_fits(host_settings address_to settings,
                                     unsigned int event, string_address command)
{
        p16 id = (p16)event;

        if (!host_settings_session(settings))
                return null;

        host_bind_forget(settings, id);
        if (!*command)
                return null;

        return host_settings_add(settings, SPARK_SETTINGS_BIND, SPARK_SETTINGS_COMMAND,
                                 command, string_length(command), address_of id);
}

static b32 host_bind_keep(unsigned int event, struct bind_control address_to control)
{
        host_settings settings;
        p16 id = (p16)event;
        string_address failed;

        host_state_ready();
        host_settings_session(address_of settings);
        host_bind_forget(address_of settings, id);
        if (!(control->flags & SPARK_BIND_DEFAULT))
        {
                failed = host_settings_add(address_of settings, SPARK_SETTINGS_BIND,
                                           SPARK_SETTINGS_COMMAND,
                                           (string_address)control->command,
                                           string_length((string_address)control->command),
                                           address_of id);
                return_if(failed, host_settings_refused("bind", failed, address_of settings));
        }
        return host_settings_save(address_of settings) ? 0 : 1;
}

static fn host_bind_apply(host_settings address_to settings)
{
        host_setting setting;
        p8 text[SPARK_SETTINGS_TEXT_MOST + 1];
        struct bind_control control;
        positive at = 0;
        bipolar device;

        device = system_open_at(AT_FDCWD, SPARK_DEVICE, FILE_READ | O_CLOEXEC);
        if (device < 0)
                return;

        while (host_settings_next(settings, address_of at, address_of setting))
        {
                if (setting.entry.list != SPARK_SETTINGS_BIND)
                        continue;

                host_settings_text(text, address_of setting);
                host_bind_ioctl(device, SPARK_BIND_SET, setting.entry.id, text,
                                address_of control);
        }

        system_close(device);
}

static bipolar host_bind_each(bool label, bool required)
{
        struct bind_control control;
        bipolar device;
        bipolar failed;
        unsigned int event;
        unsigned int count = SPARK_BIND_EVENTS;

        device = system_open_at(AT_FDCWD, SPARK_DEVICE, FILE_READ | O_CLOEXEC);
        if (device < 0)
                return required ? device : 0;

        for (event = 1; event <= count; event++)
        {
                failed = host_bind_ioctl(device, SPARK_BIND_GET, event, null,
                                         address_of control);
                if (failed < 0)
                {
                        system_close(device);
                        return (required && event == 1) ? failed : 0;
                }
                if (event == 1 && control.count)
                        count = control.count;
                host_bind_say(label, address_of control);
        }

        system_close(device);
        return 0;
}

static b32 host_bind_events(void)
{
        bipolar failed = host_bind_each(false, true);

        return_if(failed < 0, host_fail(SPARK_DEVICE, failed));

        host_say(log, "  reset is the keyboard's reset/restart key; "
                      "a case reset button cannot be bound\n");
        return 0;
}

static fn host_bind_names(writer out)
{
        unsigned int event;
        bool first = true;

        for (event = 0; event < SPARK_BIND_EVENTS; event++)
        {
                string_format(out, "%s%s", first ? "" : ", ",
                              (string_address)spark_bind_event_name[event]);
                first = false;
        }
}

static unsigned int host_bind_named(string_address first, string_address second)
{
        p8 wanted[SPARK_BIND_NAME_MAX];
        unsigned int event;

        wanted[0] = end;
        string_append_bounded(wanted, first, sizeof(wanted));
        if (second)
        {
                string_append_bounded(wanted, " ", sizeof(wanted));
                string_append_bounded(wanted, second, sizeof(wanted));
        }

        for (event = 0; event < SPARK_BIND_EVENTS; event++)
                if (string_equals((string_address)spark_bind_event_name[event], wanted))
                        return event + 1;

        return 0;
}

static b32 host_bind_told(unsigned int event, string_address command)
{
        struct bind_control control;
        host_settings settings;
        p16 line = host_machine_event_line(event);
        string_address full;
        bipolar failed;

        if (line)
        {
                host_machine_refused(event && event <= SPARK_BIND_EVENTS
                                         ? (string_address)spark_bind_event_name[event - 1]
                                         : (string_address) "that event",
                                     line);
                return 1;
        }

        //      The line the event already runs, said again as a script says
        //      it at every boot: nothing is set, and the boot image is not
        //      written over with what it holds.
        if (*command && host_bind_request(SPARK_BIND_GET, event, null, address_of control) >= 0 &&
            !(control.flags & SPARK_BIND_DEFAULT) &&
            string_equals((string_address)control.command, command))
        {
                host_bind_say(true, address_of control);
                log_flush();
                return 0;
        }

        full = host_bind_fits(address_of settings, event, command);
        if (full)
        {
                host_settings_refused("bind", full, address_of settings);
                return 1;
        }

        failed = host_bind_request(SPARK_BIND_SET, event, command,
                                   address_of control);

        if (failed == -EPERM)
                return host_refuse(control.flags & SPARK_BIND_BOOT
                                           ? "setting what %s runs needs root (CAP_SYS_ADMIN and CAP_SYS_BOOT)\n"
                                           : "setting what %s runs needs root (CAP_SYS_ADMIN)\n",
                                   control.name[0] ? (string_address)control.name
                                                   : (string_address)"that event");
        if (failed == -ENAMETOOLONG)
                return host_refuse("that command is longer than the 255 bytes a bound event holds\n");
        return_if(failed < 0, host_fail(SPARK_DEVICE, failed));

        if (host_bind_keep(event, address_of control))
                return 1;
        host_bind_say(true, address_of control);
        log_flush();
        return 0;
}

/* Setting an event reads the kept settings and writes them back, one command at
   a time like the lists; anybody else is refused by the kernel before any of it. */
static b32 host_bind_tell(unsigned int event, string_address command)
{
        b32 outcome;

        return_if(!bowl_is_root(), host_bind_told(event, command));

        if (!host_acquire())
                return 1;

        outcome = host_bind_told(event, command);
        host_release();
        return outcome;
}

/* moonwater bind [init|exit|EVENT ...] */
static b32 host_bind(string_address address_to arguments, positive count)
{
        struct bind_control probe;
        p8 text[SPARK_BIND_COMMAND_MAX];
        unsigned int event;
        string_address second = null;
        positive words;
        positive length = 0;

        if (count == 2)
                return host_bind_events();

        if (string_equals(arguments[2], "init") || string_equals(arguments[2], "exit"))
        {
                arguments[1] = arguments[2];
                array_close_gap(arguments, count, 2);
                return host_settings_command(arguments, count - 1);
        }

        event = 0;
        words = 3;
        if (count >= 4)
        {
                event = host_bind_named(arguments[2], arguments[3]);
                if (event)
                {
                        second = arguments[3];
                        words = 4;
                }
        }
        if (!event)
                event = host_bind_named(arguments[2], null);

        if (!event)
        {
                bipolar failed = host_bind_request(SPARK_BIND_GET, 1, null, address_of probe);

                if (failed < 0)
                        return host_fail(SPARK_DEVICE, failed);

                if (second)
                        string_format(log_error,
                                      host_label "%s %s is not a bound event; the events are ",
                                      arguments[2], arguments[3]);
                else
                        string_format(log_error, host_label "%s is not a bound event; the events are ",
                                      arguments[2]);
                host_bind_names(log_error);
                host_say(log_error, "\n");
                return host_usage();
        }

        if (count == words)
        {
                struct bind_control control;
                bipolar failed = host_bind_request(SPARK_BIND_GET, event, null,
                                                   address_of control);

                return_if(failed < 0, host_fail(SPARK_DEVICE, failed));
                host_bind_say(true, address_of control);
                log_flush();
                return 0;
        }

        if (!host_words_join(text, sizeof(text), arguments + words, count - words,
                                 address_of length))
                return host_refuse("that command is longer than the 255 bytes a bound event holds\n");

        return host_bind_tell(event, text);
}

// The command ---------------------------------------------------

static fn host_title(writer out)
{
        string_format(out, TERM_BOLD "Moonwater" TERM_RESET "\n\n");
}

/*
        The pages, one row to a command as it is typed and what it does.

        The whole page, a wrong argument's page and `help VERB` are drawn from
        this one table, so the picture and the usage read as the same page. A row
        belongs to the verb its command starts with, or to every word of
        verbs where one row serves two. A row with no command is the line
        after the one above it. Most are on the main page and on their
        verb's own, 'b'; a verb with more to say than a line (setup, bind,
        link) has a row for the main page, 'm', and the rows of its own, 'f'.
        A row 'r' is a verb's rows from the table of settings (registry.c),
        in its order, each one a row of both: @canvas is every setting row
        whose words start with canvas.
*/
typedef struct
{
        string_address verbs;
        string_address command;
        string_address text;
        p8 kind;
} host_row;

static const host_row host_rows[] = {
    {null, "status", "this session as one page", 'b'},
    {null, "help [VERB]", "these commands, or the page of one", 'b'},
    {null, "setup", "where it runs: install, update, use, live", 'm'},
    {null, "setup", "where this session runs, and the installs found", 'f'},
    {null, "setup install DISK [removable] [yes]", "erase DISK and put Moonwater on it", 'f'},
    {null, "", "removable takes a disk that says it is; yes", 'f'},
    {null, "", "is for a script, which cannot be asked", 'f'},
    {null, "setup update [DISK]", "write this build over an install, keeping its data", 'f'},
    {null, "setup use [DISK]", "run this build with the data of an install", 'f'},
    {null, "setup live", "leave the disks alone this session", 'f'},
    {null, "wipe [yes]", "forget /home and /root, keep the machine; yes", 'b'},
    {null, "", "is for a script, which cannot be asked", 'b'},
    {null, "bind", "the events of the machine, and what each runs", 'b'},
    {null, "bind EVENT [COMMAND]", "one event; empty puts the default back", 'b'},
    {null, "bind init [add|remove ...]", "what runs at boot", 'm'},
    {null, "bind exit [add|remove ...]", "what runs when the machine stops", 'm'},
    {null, "bind poweroff [COMMAND]", "the power button [poweroff]", 'f'},
    {null, "bind canvas on|off [COMMAND]", "when the desktop starts or stops", 'f'},
    {null, "bind init", "what runs at every boot, with ids", 'f'},
    {null, "bind init add \"command\"", "run a command at boot, once disks settle", 'f'},
    {null, "bind init remove ID|\"command\"", "stop running one at boot", 'f'},
    {null, "@bind", null, 'r'},
    {null, "bind exit", "what runs at poweroff or reboot", 'f'},
    {null, "bind exit add \"command\"", "run a command before the disks go read-only", 'f'},
    {null, "bind exit remove ID|\"command\"", "stop running one at the stop", 'f'},
    {null, "canvas", "whether Canvas is on, and the screens it draws on", 'b'},
    {null, "@canvas", null, 'r'},
    {null, "desktop [on|off|PROFILE]", "the desktop, now and at every start; off leaves the text console [on]", 'b'},
    {null, "desktop start|stop", "begin the chosen profile now, or end its session", 'b'},
    {null, "settings", "the settings window, on the desktop of the compositor", 'b'},
    {null, "latency [reset]", "what the input handlers cost a report, and who has the display", 'b'},
    {null, "debug", "what the desktop is doing and why: the card, the pointer, windows shown straight", 'b'},
    {null, "@airplane", null, 'r'},
    {null, "@brightness", null, 'r'},
    {null, "@power", null, 'r'},
    {null, "@cpu", null, 'r'},
    {null, "cpu online|offline N", "hotplug one processor", 'b'},
    {null, "@charge", null, 'r'},
    {null, "@battery", null, 'r'},
    {"sleep hibernate", "sleep | hibernate", "suspend to RAM or to disk", 'b'},
    {null, "bios [reboot]", "whether firmware setup is on offer; reboot goes", 'b'},
    {null, "@wired", null, 'r'},
    {null, "@wifi", null, 'r'},
    {null, "wifi add SSID [PASSWORD|-] [wpa2]", "remember a network, joined once wifi is on; asks for", 'b'},
    {null, "", "the password, - reads it from stdin; wpa2 lets", 'b'},
    {null, "", "a tight tier join it without WPA3", 'b'},
    {null, "wifi remove SSID", "forget a saved network, and leave it", 'b'},
    {null, "@bluetooth", null, 'r'},
    {null, "bluetooth add NAME", "keep the name of a device, found once the radio is on", 'b'},
    {null, "bluetooth remove NAME", "forget a bluetooth device", 'b'},
    {null, "@usb", null, 'r'},
    {null, "usb allow|deny ID|PORT", "let one device in, or hold it, now and at every plug;", 'b'},
    {null, "", "ID is VID:PID, or VID:PID/SERIAL as usb shows it", 'b'},
    {null, "usb forget ID|PORT", "drop the rule of one device", 'b'},
    {null, "usb log [N]", "the last decisions, the newest last [20]", 'b'},
    {null, "usb apply", "judge every device again now, as boot does", 'b'},
    {null, "@priority", null, 'r'},
    {null, "@time", null, 'r'},
    {null, "@timezone", null, 'r'},
    {null, "timezone auto", "from the network, one Cloudflare request", 'b'},
    {null, "", "per network joined [auto]", 'b'},
    {null, "@ntp", null, 'r'},
    {null, "@keyboard", null, 'r'},
    {null, "@mouse", null, 'r'},
    {null, "@taskbar", null, 'r'},
    {null, "@name", null, 'r'},
    {null, "@dns", null, 'r'},
    {null, "security [trust|block|SWITCH ...]", "keys TLS trusts and refuses besides the built-in ones, and its switches", 'm'},
    {null, "security", "the built-in trust, and what was added to it or taken from it", 'f'},
    {null, "security trust", "every anchor, built in and added, by the SHA-256 of its key", 'f'},
    {null, "security trust add FILE", "trust a CA certificate, PEM or DER, as the built-in anchors are", 'f'},
    {null, "security trust remove SHA256", "stop trusting an anchor that was added", 'f'},
    {null, "security block", "every key refused, built in and added", 'f'},
    {null, "security block add SHA256|FILE", "refuse a key wherever it is: an anchor, a CA, a leaf, a CT log", 'f'},
    {null, "security block remove SHA256", "stop refusing a key that was added; a built-in one stays", 'f'},
    {null, "security tls12 [on|off]", "TLS 1.2 where a server offers nothing newer; off asks for 1.3 alone [on]", 'f'},
    {null, "security aia [on|off]", "fetch a CA a server left out, from where its certificate says [on]", 'f'},
    {null, "security revocation [soft|hard]", "hard refuses a certificate whose revocation cannot be told [soft]", 'f'},
    {null, "security ct [on|off]", "ask Certificate Transparency of every chain, not only at the tight tier [off]", 'f'},
    {null, "security http [on|off]", "plain http in wget and fetch, and redirects to it [on]", 'f'},
    {null, "link", "who this machine is linked with, and what each may do", 'f'},
    {null, "link [pair|NAME]", "machines linked by name, with a code", 'm'},
    {null, "link pair [NAME]", "a code, and wait for the other machine to use it", 'f'},
    {null, "link NAME CODE", "link to the machine called NAME, which waits", 'f'},
    {null, "link NAME [COMMAND...]", "a terminal on NAME, or one command with its status", 'f'},
    {null, "link key", "the public key of this machine, which link add takes", 'f'},
    {null, "link push NAME FILE PATH", "a file here to PATH there, whole or not at all", 'f'},
    {null, "link pull NAME PATH FILE", "PATH there to a file here", 'f'},
    {null, "link log NAME", "follow the kernel log of NAME", 'f'},
    {null, "link add NAME KEY [HOST[:PORT]]", "link by key, with no code", 'f'},
    {null, "link remove NAME", "stop knowing it", 'f'},
    {null, "link allow|deny NAME GRANT...", "shell run log files (any file but the link state)", 'f'},
    {null, "link group [NAME [SECRET|-] [allow GRANT...]]", "machines on one network that link themselves", 'f'},
    {null, "link group leave NAME [forget]", "stop, and forget the key of the group", 'f'},
    {null, "link on|off", "the listener, kept across boots [off]", 'f'},
    {null, "link port [N|auto]", "the udp port it takes, and `link add` assumes [22348]", 'f'},
};

/* The column the words of a row start in. A command that would not leave two
   blanks before it keeps them for the line below. */
#define HOST_COLUMN 36

static fn host_blank(writer out, positive count)
{
        static const p8 blanks[] = "                                        ";

        string_format(out, "%s", (string_address)blanks + sizeof(blanks) - 1 - count);
}

static fn host_row_write(writer out, const host_row address_to row)
{
        positive length = string_length(row->command);

        if (!length)
                host_blank(out, HOST_COLUMN);
        else
        {
                string_format(out, TERM_BOLD "  %s" TERM_RESET, row->command);
                if (length + 4 > HOST_COLUMN)
                {
                        string_format(out, "\n");
                        host_blank(out, HOST_COLUMN);
                }
                else
                        host_blank(out, HOST_COLUMN - 2 - length);
        }
        string_format(out, TERM_DIM "%s" TERM_RESET "\n", row->text);
}

/* Whether a row is one of verb's: the words it lists, else its command's first. */
static bool host_row_is(const host_row address_to row, string_address verb)
{
        string_address list = row->verbs ? row->verbs : row->command;
        positive length = string_length(verb);

        for (;;)
        {
                positive word = (positive)(string_first_of_or_end(list, ' ') - list);

                if (word == length && !string_compare_max(list, verb, length))
                        return true;
                if (!list[word] || !row->verbs)
                        return false;
                list += word + 1;
        }
}

/* The rows of the settings table whose words start with first: all of them, or those of verb when it is that word. */
static positive host_settings_rows_write(writer out, string_address first, string_address verb)
{
        positive written = 0, length = string_length(first);

        if (verb && string_compare(verb, first))
                return 0;
        for (positive at = 0; at < SETTINGS_ROWS; at++)
        {
                const struct settings_row address_to setting = &settings_rows[at];
                host_row row = {null, (string_address)setting->usage, (string_address)setting->help, 'b'};

                if (!setting->usage || string_compare_max((string_address)setting->usage, first, length) ||
                    (setting->usage[length] != ' ' && setting->usage[length]))
                        continue;
                host_row_write(out, &row);
                written++;
        }
        return written;
}

/*
        The main page when verb is null, else that verb's own: its full rows
        where it has them, and what the main page shows of it where it has
        not. How many rows were written, so that a word that is no verb is
        told from one.
*/
static positive host_rows_write(writer out, string_address verb)
{
        bool full = false;
        bool live = false;
        positive written = 0;

        for (positive at = 0; verb && at < array_count(host_rows); at++)
                full |= host_rows[at].kind == 'f' && host_rows[at].command[0] &&
                        host_row_is(host_rows + at, verb);

        for (positive at = 0; at < array_count(host_rows); at++)
        {
                const host_row address_to row = host_rows + at;

                if (row->kind == 'r')
                {
                        written += host_settings_rows_write(out, row->command + 1, verb);
                        continue;
                }
                if (row->command[0])
                        live = !verb || host_row_is(row, verb);
                if (live && (verb && full ? row->kind != 'm' : row->kind != 'f'))
                {
                        host_row_write(out, row);
                        written++;
                }
        }
        return written;
}

static fn host_page_write(writer out)
{
        host_rows_write(out, null);
        host_say(out,
                 "\n"
                 TERM_DIM "  Settings stay in the image this session started from.\n"
                 "  install takes the settings of this session; update keeps those of the disk.\n"
                 "  The machine script overwrites bind, init and exit.\n"
                 "  " HOST_MACHINE_SCRIPT " overlays the kernel builtin.\n" TERM_RESET);
}

/* A wrong argument's page: the verb's rows, or the whole of it for a word that is none. */
static b32 host_usage(void)
{
        if (!host_verb || !host_rows_write(log_error, host_verb))
        {
                host_title(log_error);
                host_page_write(log_error);
        }
        log_flush();
        return 2;
}

/*
        The wifi line of status, from what the kernel already holds: status
        never scans. Joined and where, why wifi cannot be used, or what the
        last join said.
*/
static fn host_status_wifi(void)
{
        p8 why[RADIO_WHY_ROOM];
        radio_last last;
        p8 name[RADIO_SSID_MOST * 4 + 1];
        radio_air air;

        if (radio_wifi_why(why, sizeof(why)))
        {
                string_format(log, "  wifi: %s\n", why);
                return;
        }
        if (radio_air_take(address_of air, RADIO_AIR_CACHED))
                for (positive at = 0; at < air.count; at++)
                        if (air.heard[at].joined)
                        {
                                radio_display(name, sizeof(name), air.heard[at].ssid,
                                              air.heard[at].ssid_length);
                                string_format(log, "  wifi joined %s\n", name);
                                return;
                        }
        if (radio_last_get(address_of last))
        {
                radio_display(name, sizeof(name), last.ssid, string_length(last.ssid));
                string_format(log, "  wifi on, not joined: %s %s\n", name,
                              radio_join_words(last.failed));
                return;
        }
        string_format(log, "  wifi on\n");
}

/*
        Where this session runs and what is on the disks: the build, whether
        the session is live, kept on a disk or waiting for an answer, every
        install found and the stick this image is on. Setup and status both
        open with it.
*/
static fn host_setup_state(void)
{
        p8 running[HOST_BUILD_ROOM];
        p8 verdict[HOST_NAME_ROOM + 16];
        host_census census;
        host_medium_search search;

        if (host_running_build(running, sizeof(running)))
                string_format(log, "  this is %s\n", running);
        else
                string_format(log, "  this build's version cannot be read\n");

        host_read_word(HOST_VERDICT, verdict, sizeof(verdict));
        if (string_has_prefix(verdict, "disk "))
                string_format(log, "  kept on %s: %s /root /home\n",
                              verdict + 5, BOWL_ROOT_DIRECTORY);
        else if (string_has_prefix(verdict, "ask "))
                string_format(log, "  waiting: %s has another build; "
                                   "moonwater setup use, update or live\n",
                              verdict + 4);
        else
                string_format(log, "  live session: nothing is kept after power "
                                   "off; moonwater setup install DISK keeps it\n");

        host_census_take(address_of census);
        host_census_said(address_of census);
        for (positive at = 0; at < census.count; at++)
        {
                host_install address_to install = census.found + at;

                host_install_read(install);
                if (!install->readable)
                        string_format(log, "  installed on %s, image unreadable\n",
                                      install->disk);
                else if (string_equals(install->build, running))
                        string_format(log, "  installed on %s, this build\n",
                                      install->disk);
                else
                        string_format(log, "  installed on %s, another build: %s\n",
                                      install->disk, install->build);
        }

        if (!census.count)
                string_format(log, "  not installed on any disk here\n");

        if (running[0] && host_medium_find(address_of search, running, null))
        {
                string_format(log, "  this image is on %s\n", search.name);
                host_unmount(HOST_MEDIUM);
        }
}

/*
        This session as one page: the build, the disks, Canvas, the bound
        events, init and exit, then the commands. Nothing is a log line;
        the words that name each fact stay as they are.
*/
static b32 host_status(void)
{
        host_settings settings;
        struct canvas_control canvas;

        host_state_ready();
        host_title(log);
        host_setup_state();

        string_format(log, "\n");

        if (host_canvas_request(SPARK_CANVAS_STATUS, address_of canvas) >= 0)
                host_canvas_write(false, address_of canvas);

        locale_status_lines();

        {
                p8 name[NAME_ROOM];

                host_read_word(NAME_PATH, name, sizeof(name));
                if (name_valid(name))
                        string_format(log, "  name %s\n", name);
        }

        dns_say(false);

        host_status_wifi();

        (void)host_bind_each(false, false);

        if (host_settings_session(address_of settings))
        {
                host_settings_lines(address_of settings, 0, true);
                string_format(log, "  init mount is %s\n",
                              settings.flags & SPARK_SETTINGS_MOUNT_OFF ? "off" : "on");
                string_format(log, "  init canvas is %s\n",
                              settings.flags & SPARK_SETTINGS_CANVAS_OFF ? "off" : "on");
                host_settings_lines(address_of settings, 1, true);
        }
        //      Else they are root's and not known to this caller: no line, where
        //      there was "init mount is on" and nothing run at boot as facts.

        string_format(log, "\n");
        host_page_write(log);
        return 0;
}

/* use and update: the named install, the one boot asked about, or the only one. */
static b32 host_answer(bool update, string_address disk)
{
        p8 verdict[HOST_NAME_ROOM + 16];
        host_census census;
        host_install address_to install = null;

        host_read_word(HOST_VERDICT, verdict, sizeof(verdict));
        if (disk && string_has_prefix(disk, "/dev/"))
                disk += 5;

        /*      The data this session already keeps. An update touches only the
                system partition, so it can go ahead underneath what is mounted;
                keeping a second disk's data on top of the first cannot. */
        if (string_has_prefix(verdict, "disk "))
        {
                if (disk && !string_equals(disk, verdict + 5))
                        return host_refuse("this session already keeps %s\n",
                                           verdict + 5);

                if (update)
                {
                        host_census_take(address_of census);
                        install = host_census_find(address_of census, verdict + 5);
                        return install ? host_update(install)
                                       : host_refuse("%s is no longer here\n",
                                                     verdict + 5);
                }

                host_say(log, host_label "already kept on %s\n", verdict + 5);
                return 0;
        }

        host_census_take(address_of census);

        if (disk)
                install = host_census_find(address_of census, disk);
        else if (string_has_prefix(verdict, "ask "))
                install = host_census_find(address_of census, verdict + 4);
        else if (census.count == 1)
                install = census.found;

        //      A disk that was named is answered about first: with two
        //      installs elsewhere, asking after a third said "more than one
        //      disk has Moonwater; name one" with its name run on the end.
        if (!install)
                return host_refuse(disk                ? "%s has no Moonwater install\n"
                                   : census.count > 1 ? "more than one disk has "
                                                        "Moonwater; name one%s\n"
                                                      : "no disk here has Moonwater "
                                                        "installed%s\n",
                                   disk ? disk : (string_address)"");

        return host_take(install, update);
}

#include "../waterlink/command.c"

/*
        Forget userspace, keep the machine.

        /home is emptied. /root is emptied except the overlay and the files
        an image update already leaves on the data partition. /bowls
        stays: that is the pre-installed software a kiosk starts after wipe.
        A kiosk's machine script says it at every boot, and a script says the
        word that goes with it: `wipe yes`. At a terminal it asks instead.

        What /root keeps is each setting's own path, taken from the macro the
        command that writes it names it by, so a setting added without a line
        here is not wiped by default. A file system mounted below /home or
        /root is not this machine's userspace and stays as it is.
*/
#define HOST_WIPE_KEEP(path) ((path) + sizeof("/root/") - 1)

//      The files of the settings table's plain rows are added when a wipe runs
//      (host_wipe_table), after the files named here by hand.
static string_address host_wipe_keep[96] = {
    HOST_WIPE_KEEP(HOST_MACHINE_SCRIPT),
    HOST_WIPE_KEEP(NET_WIFI_LIST),
    HOST_WIPE_KEEP(RADIO_RULES_PATH),
    HOST_WIPE_KEEP(NET_WIFI_POWER),
    HOST_WIPE_KEEP(NET_WIRED_POWER),
    HOST_WIPE_KEEP(NET_BLUETOOTH_LIST),
    HOST_WIPE_KEEP(NET_BLUETOOTH_POWER),
    HOST_WIPE_KEEP(USB_POLICY),
    HOST_WIPE_KEEP(USB_LOG),
    HOST_WIPE_KEEP(NET_INTERNET_ROOT),
    HOST_WIPE_KEEP(TUNE_KEPT),
    HOST_WIPE_KEEP(CLOCK_ZONE_PATH),
    HOST_WIPE_KEEP(LOCALE_ZONE_MODE_PATH),
    HOST_WIPE_KEEP(LOCALE_NTP_PATH),
    HOST_WIPE_KEEP(LOCALE_NTP_SERVER_PATH),
    HOST_WIPE_KEEP(LOCALE_NTP_SAMPLING_PATH),
    HOST_WIPE_KEEP(LOCALE_NTS_PATH),
    HOST_WIPE_KEEP(LOCALE_NTS_SERVERS_PATH),
    HOST_WIPE_KEEP(CLOCK_GOOD_PATH),
    HOST_WIPE_KEEP(LOCALE_KEYBOARD_PATH),
    HOST_WIPE_KEEP(LOCALE_CANVAS_SCALE_PATH),
    HOST_WIPE_KEEP(LOCALE_CANVAS_MODES_PATH),
    HOST_WIPE_KEEP(DESKTOP_PATH),
    HOST_WIPE_KEEP(NET_DNS_PATH),
    HOST_WIPE_KEEP(DNS_TLS_KEPT),
    HOST_WIPE_KEEP(NAME_PATH),
    HOST_WIPE_KEEP(LINK_SWITCH_PATH),
    HOST_WIPE_KEEP(LINK_KEY_PATH),
    HOST_WIPE_KEEP(LINK_PEERS_PATH),
    HOST_WIPE_KEEP(LINK_PORT_PATH),
    HOST_WIPE_KEEP(LINK_GROUPS_PATH),
    HOST_WIPE_KEEP(LINK_STAMPS_PATH),
    //  The text size every terminal follows, which is the module's own
    //  (TERMINAL_SIZE_DIR in src/canvas/ui/terminal.c) and so named here.
    "terminal",
    null,
};

static fn host_wipe_table(void)
{
        positive used = 0;

        while (host_wipe_keep[used])
                used++;
        for (positive at = 0; at < SETTINGS_ROWS && used + 1 < array_count(host_wipe_keep); at++)
                if (settings_rows[at].how == SETTING_PLAIN && settings_rows[at].path)
                        host_wipe_keep[used++] = (string_address)settings_rows[at].path + sizeof("/root/") - 1;
        host_wipe_keep[used] = null;
}

static b32 host_wipe_path(string_address path, positive mode,
                          string_address address_to keep)
{
        bipolar failed = bowl_reset_walk(path, BOWL_RESET_ONE_MOUNT, keep);

        //      An emptied directory that was not there is made, with the mode
        //      it is meant to have whatever the caller's umask says.
        if (!failed)
                failed = host_make_directory(path, mode, true);
        return failed < 0 ? host_fail(path, failed) : 0;
}

/*
        One command at a time changes what /root keeps: the radio lock is what
        wifi add and the verbs that write beside a list hold, and the settings
        lock is bind's and an install's. A wipe between a list's write and its
        rename took the half-written file and left `wifi add` with an error
        for a network it had just joined.
*/
static b32 host_wipe(bool said_yes)
{
        bipolar lock;
        b32 failed;

        host_need_root();

        if (!said_yes)
        {
                p8 answer[8];

                return_if(!host_asks(),
                          host_refuse("wiping forgets everything in /home and /root but the "
                                      "settings, and nothing here can be asked: "
                                      "moonwater wipe " HOST_YES "\n"));
                host_say(log, host_label "Wiping forgets everything in /home and /root but "
                                         "the settings. Type " HOST_YES " to go on: ");
                if (host_read_line(answer, sizeof(answer)) < 0 ||
                    !string_equals(answer, HOST_YES))
                {
                        host_say(log, host_label "nothing forgotten\n");
                        return 1;
                }
        }

        if (!host_acquire())
                return 1;
        lock = radio_lock(true);

        host_wipe_table();
        failed = host_wipe_path("/home", 0755, null) ||
                 host_wipe_path("/root", 0700, host_wipe_keep);

        radio_unlock(lock);
        host_release();
        if (!failed)
                host_say(log, host_label "userspace forgotten\n");
        return failed;
}

/*
        `moonwater setup`: a live session, or one that keeps its data on a
        disk, and the ways to change which. Arguments are judged before
        root is asked for, so a wrong one is a usage page for anyone.
*/
static b32 host_setup(string_address address_to arguments, positive count)
{
        string_address verb = count > 2 ? arguments[2] : null;
        bool update = verb && string_equals(verb, "update");

        if (!verb)
        {
                host_state_ready();
                host_title(log);
                host_setup_state();
                string_format(log, "\n");
                host_rows_write(log, "setup");
                log_flush();
                return 0;
        }

        if (string_equals(verb, "install"))
        {
                positive words = 4;
                bool removable = count > words && string_equals(arguments[words], "removable");
                bool said_yes;

                words += removable;
                said_yes = count > words && string_equals(arguments[words], HOST_YES);
                words += said_yes;
                return_if(count < 4 || count != words, host_usage());
                host_need_root();
                if (!said_yes && !host_asks())
                {
                        host_say(log_error, host_label "installing erases %s and nothing here "
                                                       "can be asked: moonwater setup install "
                                                       "%s%s " HOST_YES "\n",
                                 arguments[3], arguments[3],
                                 removable ? (string_address) " removable"
                                           : (string_address) "");
                        return 1;
                }
                host_state_ready();
                return host_install_disk(arguments[3], removable, said_yes);
        }

        if (string_equals(verb, "live") && count == 3)
        {
                host_need_root();
                host_state_ready();
                system_remove_at(AT_FDCWD, HOST_QUESTION, 0);
                host_verdict_set("live", "");
                host_say(log, host_label "the disks are left alone this session\n");
                return 0;
        }

        if ((update || string_equals(verb, "use")) && count <= 4)
        {
                host_need_root();
                host_state_ready();
                return host_answer(update, count == 4 ? arguments[3] : null);
        }

        return host_usage();
}

/* moonwater help [VERB]: the main page, or one verb's. */
static b32 host_help(string_address address_to arguments, positive count)
{
        return_if(count > 3, host_usage());

        if (count == 3)
        {
                host_verb = arguments[2];
                return host_rows_write(log, host_verb) ? (log_flush(), 0) : host_usage();
        }

        host_title(log);
        host_page_write(log);
        return 0;
}

static b32 host_status_verb(string_address address_to arguments, positive count)
{
        return count == 2 ? host_status() : host_usage();
}

static b32 host_wipe_verb(string_address address_to arguments, positive count)
{
        bool said_yes = count == 3 && string_equals(arguments[2], HOST_YES);

        return_if(count > 3 || (count == 3 && !said_yes), host_usage());
        return host_wipe(said_yes);
}

static b32 host_machine_verb(string_address address_to arguments, positive count)
{
        return count == 2 ? host_machine_run() : host_usage();
}

/* boot and ask are init's and the terminal's, and take no words. */
static b32 host_boot_verb(string_address address_to arguments, positive count)
{
        return_if(count > 2, host_usage());
        host_need_root();
        host_state_ready();
        return host_boot();
}

static b32 host_ask_verb(string_address address_to arguments, positive count)
{
        p8 verdict[HOST_NAME_ROOM + 16];

        return_if(count > 2, host_usage());
        host_need_root();
        return_if(!host_asks(), host_refuse("moonwater ask is a question for a terminal\n"));
        host_state_ready();

        host_read_word(HOST_VERDICT, verdict, sizeof(verdict));
        if (!string_has_prefix(verdict, "ask ") ||
            system_rename_at(AT_FDCWD, HOST_QUESTION, AT_FDCWD,
                             HOST_QUESTION_TAKEN, 0) < 0)
                return host_refuse("there is nothing to ask\n");

        host_question(verdict + 4);
        return 0;
}

/*
        moonwater settings: the settings window (src/canvas/ui/settings.c) on
        the compositor's desktop, which is a front end to this command. Up is
        what the machine script means by it: the window table there, and the
        process compositor.pid names alive. The window is started in a
        session of its own, as /settings with the face the compositor writes
        its tabs in (its -f) and that face's bold beside it, and this returns
        at once; a window already up is lifted by the second one, which then
        ends, so there is only ever one.
*/
#define HOST_SETTINGS_FACE "/usr/share/fonts/inter/Inter-Regular.ttf"

static b32 host_settings_window(string_address address_to arguments, positive count)
{
        p8 word[32], path[48] = "/proc/", line[512], face[256], bold[272];
        string_address words[8];
        positive pid = 0, length, at = 0, used = 0;
        bipolar handle, got = 0;

        (void)arguments;
        return_if(count != 2, host_usage());

        host_read_word(HOST_STATE "/compositor.pid", word, sizeof(word));
        for (positive digit = 0; word[digit] >= '0' && word[digit] <= '9' && digit < 10; digit++)
                pid = pid * 10 + (positive)(word[digit] - '0');
        return_if(!pid || system_call_2(syscall(kill), pid, 0) != 0 ||
                      system_access_at(AT_FDCWD, (string_address) "/run/moonwater/windows", 0) != 0,
                  host_refuse("the compositor's desktop is not up, and the settings window is one of its "
                              "windows: moonwater desktop on starts it\n"));

        // Its -f, from its command line, words a zero apart.
        string_copy(face, (string_address)HOST_SETTINGS_FACE);
        length = positive_into_string(path + 6, pid) + 6;
        string_copy(path + length, (string_address) "/cmdline");
        handle = system_open_at(AT_FDCWD, path, FILE_READ | O_CLOEXEC);
        if (handle >= 0)
        {
                got = system_read_once((b32)handle, line, sizeof(line) - 1);
                system_close(handle);
        }
        line[got > 0 ? got : 0] = end;
        while (got > 0 && at < (positive)got)
        {
                positive size = string_length(line + at);

                if (string_equals(line + at, "-f") && at + size + 1 < (positive)got)
                {
                        string_copy_bounded(face, line + at + size + 1, sizeof(face));
                        break;
                }
                at += size + 1;
        }

        // Its bold, where a face named Regular has one beside it.
        string_copy_bounded(bold, face, sizeof(bold));
        length = string_length(bold);
        for (positive from = 0; from + 7 <= length; from++)
                if (!memory_compare(bold + from, "Regular", 7) && length + 1 < sizeof(bold))
                {
                        string_copy_bounded(bold + from, (string_address) "Bold", sizeof(bold) - from);
                        string_copy_bounded(bold + from + 4, face + from + 7, sizeof(bold) - from - 4);
                        break;
                }

        words[used++] = (string_address) "/settings";
        words[used++] = (string_address) "-f";
        words[used++] = face;
        if (string_compare(bold, face) && system_access_at(AT_FDCWD, bold, 4) == 0)
        {
                words[used++] = (string_address) "-b";
                words[used++] = bold;
        }
        words[used] = null;

        if (!host_detach(descriptors_none, descriptors_none))
                return 0;
        (void)shell_exec_file(words[0], words, used, file_environment_all());
        system_call_1(syscall(exit), 127);
        return 1;
}

/*
        Every word the command answers to, and who answers. A verb that says
        what it does and nothing else (quiet) has its output set aside from
        its answer: standard output closed or full or a pipe whose reader has
        gone is no reason for `moonwater name` to say it failed after the name
        was kept, or for `status | head` to die of SIGPIPE; one that starts
        processes of its own keeps the signal as it found it. The table is
        host_main's own, where the lane that works out which applets can start
        a program reads the handlers it names.
*/

static b32 host_main()
{
        static const struct
        {
                string_address verb;
                b32 (*run)(string_address address_to arguments, positive count);
                bool quiet;
        } host_verbs[] = {
            {"status", host_status_verb, true},
            {"help", host_help, true},
            {"setup", host_setup, false},
            // Reading needs nothing, and the kernel decides who may set a bound
            // event. init and exit still need root.
            {"bind", host_bind, false},
            {"canvas", host_canvas, false},
            {"desktop", host_desktop, false},
            {"settings", host_settings_window, false},
            {"latency", host_latency, false},
            {"debug", host_debug, true},
            {"bios", host_bios, false},
            {"airplane", tune_airplane, false},
            {"brightness", tune_brightness, false},
            {"charge", tune_charge, false},
            {"battery", tune_battery_verb, false},
            {"power", tune_power, false},
            {"cpu", tune_cpu, false},
            {"sleep", tune_sleep_verb, false},
            {"hibernate", tune_hibernate_verb, false},
            {"wifi", host_wifi, false},
            {"bluetooth", host_bluetooth, false},
            {"priority", host_priority, false},
            {"wired", host_wired, false},
            {"usb", host_usb, false},
            {"timezone", host_timezone, false},
            {"ntp", host_ntp, false},
            {"keyboard", host_keyboard, false},
            {"mouse", host_setting_verb, false},
            {"taskbar", host_setting_verb, false},
            {"time", host_time, false},
            {"name", host_name, true},
            {"dns", host_dns, false},
            {"security", host_security, false},
            {"link", link_main, false},
            {"wipe", host_wipe_verb, true},
            {"machine", host_machine_verb, false},
            {"boot", host_boot_verb, false},
            {"ask", host_ask_verb, false},
        };

        string_address address_to arguments = program_argument_list();
        positive count = (positive)program_argument_count();
        positive previous[4];
        positive which;
        bool ignored = false;
        b32 answer;

        host_verb = count > 1 ? arguments[1] : null;
        memory_zero(plain_streams, sizeof(plain_streams));
        if (!host_verb)
        {
                host_title(log);
                host_page_write(log);
                return 0;
        }

        //      A plain row of the settings table is answered by its own words,
        //      the longest of them, before any verb's handler is asked.
        {
                positive used;
                int plain = setting_find(arguments + 1, count - 1, &used);

                if (plain >= 0)
                {
                        host_state_once = true;
                        return host_setting_run(&settings_rows[plain], arguments, count);
                }
        }

        which = string_table_find(host_verb, host_verbs, sizeof(host_verbs[0]),
                                  array_count(host_verbs));
        return_if(which == array_count(host_verbs), host_usage());

        host_state_once = host_verbs[which].run != host_machine_verb;
        if (host_verbs[which].quiet)
                ignored = system_signal_install(13, 1, 0, 0, previous);
        answer = host_verbs[which].run(arguments, count);
        if (host_verbs[which].quiet)
        {
                log_flush();
                log_failure_reset();
        }
        if (ignored)
                system_signal_action(13, previous, null, 8);
        return answer;
}

#define MOONWATER_CLI
#include "../moonwater/moonwater.c"

#undef log
#undef log_error
