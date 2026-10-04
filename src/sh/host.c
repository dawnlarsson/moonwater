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
#define HOST_VERDICT_NEXT HOST_STATE "/verdict.next"
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

#include "../moonwater/moonwater.c"

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
/*      How often a terminal looks for boot's verdict. It is one failed open,
        and the first prompt waits on it. */
#define HOST_VERDICT_POLL_NS ((p64)5000000)
#define HOST_MEDIUM_FLOOR_NS ((p64)5000000000)

#define HOST_CLOCK_REALTIME 0
#define HOST_CLOCK_BOOTTIME 7
#define HOST_BLKRRPART 0x125f
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
} host_install;

typedef struct
{
        host_install found[HOST_INSTALLS];
        positive count;
} host_census;

// Settings, defined further down with the commands that change them.
typedef struct spark_settings host_settings;

static fn host_settings_empty(host_settings address_to settings);
static b32 host_settings_image(string_address path, host_settings address_to into);
static bipolar host_settings_stamp(string_address path, host_settings address_to settings);
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
static fn host_usage_write(writer out);
static positive radio_display(p8 address_to into, positive room, p8 address_to name,
                              positive length);
static p16 host_machine_hook_line(p8 hook);
static p16 host_machine_event_line(unsigned int event);
static string_address host_machine_where(void);
static fn host_machine_refused(string_address name, p16 line);
static bool host_machine_stop(void);

//      builtin.c's, which stops the machine the way reboot does; it is
//      included after this file. The number is the one it defines.
fn shell_stop(writer write, positive command);
#ifndef REBOOT_RESTART
#define REBOOT_RESTART 0x01234567
#endif
static b32 host_machine_run(void);
static b32 host_radio(string_address address_to arguments, positive count);
static b32 host_tune(string_address address_to arguments, positive count);
static fn tune_restore(void);
static fn radio_restore(void);
static fn radio_recover(void);
static b32 host_locale(string_address address_to arguments, positive count);
static fn locale_restore(void);
//      Set by the machine process alone, in moonwater.c: only it asks the time.
static bool host_machine_self;
static fn name_restore(void);
static fn locale_recover(void);
static unsigned int locale_wake_ms(unsigned int most);
static b32 host_wipe(void);

// A line to the writer, then the buffer out: the two calls every report ends on.
#define host_say(...) \
        do \
        { \
                string_format(__VA_ARGS__); \
                log_flush(); \
        } while (0)

// The root check the commands that change the machine open with.
#define host_need_root(what) \
        do \
        { \
                if (!bowl_is_root()) \
                        return host_refuse("%s needs root\n", what); \
        } while (0)

static b32 host_refuse(string_address text, string_address name)
{
        string_format(log_error, host_label);
        host_say(log_error, text, name);
        return 1;
}

static b32 host_fail(string_address what, bipolar error)
{
        host_say(log_error, host_label "%s: %s\n", what, file_reason(error));
        return 1;
}

/* Both halves must fit, or into is truncated and the answer says so. */
static bool host_join(p8 address_to into, positive room, string_address left,
                      string_address right)
{
        return string_copy_bounded(into, left, room) < room &&
               string_append_bounded(into, right, room) < room;
}

static bool host_starts(string_address text, string_address prefix)
{
        return !string_compare_max(text, prefix, string_length(prefix));
}

static fn host_pause(p64 nanoseconds)
{
        timespec span = {nanoseconds / 1000000000, nanoseconds % 1000000000};

        sleep(address_of span);
}

/* A small sysfs or state file, without its trailing newline. */
static bipolar host_read_text(string_address path, p8 address_to into,
                              positive room)
{
        bipolar got = file_slurp_once_at(AT_FDCWD, path, into, room);

        if (got < 0)
        {
                into[0] = end;
                return got;
        }

        while (got > 0 && (into[got - 1] == '\n' || into[got - 1] == ' '))
                got--;

        into[got] = end;
        return got;
}

/* Persistent state is a regular file owned by this program. Opening it
   nonblocking first means a FIFO planted at a state name cannot stop a root
   command before it has a chance to reject the object; O_NOFOLLOW gives a
   planted final symlink the same refusal as the writers. */
static bipolar host_read_state(string_address path, p8 address_to into,
                               positive capacity)
{
        struct stat facts;
        bipolar handle;
        bipolar got;

        if (!capacity)
                return -1;
        handle = system_open_at(AT_FDCWD, path,
                                FILE_READ | O_NONBLOCK | O_NOFOLLOW |
                                    O_CLOEXEC);
        if (handle < 0)
                return handle;
        if (system_file_status(handle, address_of facts) < 0 ||
            !S_ISREG(facts.st_mode))
        {
                system_close(handle);
                into[0] = end;
                return -22;
        }
        got = system_read_retry((positive)handle, into, capacity - 1);
        system_close(handle);
        if (got >= 0)
                into[got] = end;
        return got;
}

/* A state word as host_read_state reads it, without its newline or the blanks
   after it; negative, and empty, when the file is not there or not a plain one. */
static bipolar host_read_word(string_address path, p8 address_to into, positive room)
{
        bipolar got = host_read_state(path, into, room);

        if (got < 0)
        {
                into[0] = end;
                return got;
        }
        while (got > 0 && (into[got - 1] == '\n' || into[got - 1] == ' '))
                into[--got] = end;
        return got;
}

/*
        A state file this writes as root, under /run/moonwater or /root, is
        opened where it is and never through a link: a name planted there
        first made the write truncate whatever it pointed at, with the
        secret some of these files carry (the wifi passwords, the settings)
        going into it. And its mode is set as well as asked for, since a
        mode given to open reaches only a file it creates: /root/wifi left
        at 0644 by anything before kept the passwords readable by all.
*/
static bipolar host_open_state(bipolar directory, string_address path,
                               positive mode)
{
        bipolar handle = system_open_output_at(directory, path, true, mode);
        bipolar moded;

        if (handle < 0)
                return handle;

        moded = system_call_2(syscall(fchmod), (positive)handle, mode);
        if (moded < 0)
        {
                system_close(handle);
                return moded;
        }

        return handle;
}

/* Bytes over a state file, made if it is not there. A choice that has to
   survive the power going is synced; a /run file that only says what this
   session is doing does not need to be. */
static bipolar host_write_file(string_address path, p8 address_to bytes,
                               positive length, positive mode, bool sync)
{
        p8 next[HOST_PATH_ROOM];
        bipolar handle;
        bipolar failed;

        if (!sync)
        {
                handle = host_open_state(AT_FDCWD, path, mode);
                if (handle < 0)
                        return handle;
                failed = storage_format_write(handle, bytes, length, 0);
                system_close(handle);
                return failed;
        }

        /*      A file that has to survive is written beside itself, synced,
                and renamed over the old one, and the directory is synced
                after: the old bytes are opened for writing never, so a crash
                or a signal part way (the saved wifi list, passwords and all,
                was truncated first and written second) leaves the list as it
                was or as it is to be, and nothing half of each. A leftover
                from a crash is removed and the name made again exclusively,
                which also never follows a planted link; the rename replaces
                a link at the final name instead of writing through it. */
        if (string_length(path) + 5 >= sizeof(next))
                return -36;
        string_copy_bounded(next, path, sizeof(next));
        string_append_bounded(next, ".new", sizeof(next));
        system_remove_at(AT_FDCWD, next, 0);
        handle = system_open_output_at(AT_FDCWD, next, false, mode);
        if (handle < 0)
                return handle;
        failed = system_call_2(syscall(fchmod), (positive)handle, mode);
        if (!failed)
                failed = storage_format_write(handle, bytes, length, 0);
        if (!failed)
                failed = system_call_1(syscall(fsync), (positive)handle);
        system_close(handle);
        if (!failed)
                failed = system_rename_at(AT_FDCWD, next, AT_FDCWD, path, 0);
        if (failed)
        {
                system_remove_at(AT_FDCWD, next, 0);
                return failed;
        }

        {
                bipolar parent;

                path_head_copy(next, sizeof(next), path);
                parent = system_open_at(AT_FDCWD, next,
                                        FILE_READ | O_DIRECTORY | O_CLOEXEC);
                if (parent >= 0)
                {
                        system_call_1(syscall(fsync), (positive)parent);
                        system_close(parent);
                }
        }
        return 0;
}

static bipolar host_write_text(string_address path, string_address text)
{
        return host_write_file(path, (p8 address_to)text, string_length(text),
                               0644, false);
}

/* One line from standard input, or negative at its end. */
static bipolar host_read_line(p8 address_to into, positive room)
{
        positive used = 0;

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
        }

        while (used && (into[used - 1] == '\r' || into[used - 1] == ' '))
                used--;

        into[used] = end;
        return (bipolar)used;
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

static fn host_state_ready(void)
{
        system_make_directory_at(AT_FDCWD, "/run", 0755);
        host_state_directory(HOST_STATE, 0755);
}

static fn host_verdict_set(string_address kind, string_address disk)
{
        p8 line[HOST_NAME_ROOM + 16];
        p8 text[HOST_NAME_ROOM + 16];

        if (!host_join(line, sizeof(line), kind, disk) ||
            !host_join(text, sizeof(text), line, "\n"))
                return;

        if (!host_write_text(HOST_VERDICT_NEXT, text))
                system_rename_at(AT_FDCWD, HOST_VERDICT_NEXT, AT_FDCWD,
                                 HOST_VERDICT, 0);
}

// Disks ---------------------------------------------------------

static bool host_name_valid(string_address name)
{
        if (!*name || string_length(name) >= HOST_NAME_ROOM)
                return false;

        for (; *name; name++)
                if (!byte_is_alnum(*name) && *name != '-' && *name != '_')
                        return false;

        return true;
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
                        return true;

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
            host_read_text(path, state, sizeof(state)) < 0 ||
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

        got = host_read_text("/proc/version", banner, sizeof(banner));
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
        made = system_make_directory_at(AT_FDCWD, target, 0700);
        if (made < 0 && made != -EEXIST)
                return made;

        return system_mount(device, target, type, flags, 0);
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
            !host_starts(path, "/dev/") ||
            !host_name_valid(path + sizeof("/dev/") - 1))
                return false;

        string_copy(name, path + sizeof("/dev/") - 1);
        return true;
}

/* Both partitions must still exist and still belong to one physical disk.
   Refresh all display names together so the rest of an operation cannot mix
   one old name with one newly resolved identity. */
static bool host_install_refresh(host_install address_to install)
{
        p8 system[HOST_NAME_ROOM];
        p8 data[HOST_NAME_ROOM];
        p8 system_disk[HOST_NAME_ROOM];
        p8 data_disk[HOST_NAME_ROOM];

        if (!host_partition_uuid_resolve(install->system_partuuid, system) ||
            !host_partition_uuid_resolve(install->data_partuuid, data) ||
            !host_parent(system, system_disk, sizeof(system_disk)) ||
            !host_parent(data, data_disk, sizeof(data_disk)) ||
            !string_equals(system_disk, data_disk))
                return false;

        string_copy(install->disk, system_disk);
        string_copy(install->system, system);
        string_copy(install->data, data);
        return true;
}

static fn host_unmount(string_address target)
{
        system_call_2(syscall(umount2), (positive)target, 0);
}

static fn host_install_read(host_install address_to install)
{
        p8 path[HOST_PATH_ROOM];

        install->readable = false;
        install->build[0] = end;

        if (!host_install_refresh(install) ||
            host_mount(install->system, HOST_LOOK, "vfat", HOST_READ_ONLY) < 0)
                return;

        if (host_join(path, sizeof(path), HOST_LOOK, HOST_IMAGE))
                install->readable = host_image_build(path, install->build,
                                                     sizeof(install->build));

        host_unmount(HOST_LOOK);
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

// Placing an image ----------------------------------------------

static b32 host_copy_file(string_address from, string_address to)
{
        bipolar source = system_open_at(AT_FDCWD, from, FILE_READ | O_CLOEXEC);
        bipolar target;
        bool copied;
        bipolar synced = 0;

        if (source < 0)
                return host_fail(from, source);

        target = system_open_at_mode(AT_FDCWD, to, FILE_WRITE | O_CLOEXEC, 0644);
        if (target < 0)
        {
                system_close(source);
                return host_fail(to, target);
        }

        copied = file_copy_handles(source, target);
        if (copied)
                synced = system_call_1(syscall(fsync), (positive)target);

        system_close(source);
        system_close(target);
        if (!copied)
                return host_refuse("%s could not be copied whole\n", from);

        return synced < 0 ? host_fail(to, synced) : 0;
}

/*
        The running image onto a mounted system partition, which is the whole
        of an update. The image there before is kept as previous.efi, for the
        firmware's shell or a stick to start when a build turns out bad; the
        new one is written beside the old and renamed over it, so a machine
        that loses power part way still has an image that starts.
*/
static b32 host_place_image(string_address system, string_address running,
                            host_settings address_to carry)
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

        if (system_access_at(AT_FDCWD, image, 0) >= 0 &&
            host_join(next, sizeof(next), system, HOST_KEPT_IMAGE_NEXT) &&
            host_join(kept, sizeof(kept), system, HOST_KEPT_IMAGE))
        {
                if (host_copy_file(image, next))
                        return 1;

                failed = system_rename_at(AT_FDCWD, next, AT_FDCWD, kept, 0);
                if (failed < 0)
                        return host_fail(kept, failed);
        }

        if (!host_join(next, sizeof(next), system, HOST_IMAGE_NEXT) ||
            host_copy_file(source, next))
                return 1;

        //      Both slots of the new image say what the disk is to keep, before
        //      the rename makes it the one that starts.
        failed = carry ? host_settings_stamp(next, carry) : 0;
        if (failed < 0)
                return host_fail(next, failed);

        failed = system_rename_at(AT_FDCWD, next, AT_FDCWD, image, 0);
        if (failed < 0)
                return host_fail(image, failed);

        if (!host_image_build(image, build, sizeof(build)) ||
            !string_equals(build, running))
                return host_refuse("%s did not read back as this build\n", image);

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

        failed = host_mount(install->data, HOST_DATA, "ext4", 0);

        if (failed < 0)
                return host_fail(install->data, failed);

        for (at = 0; at < array_count(host_kept); at++)
        {
                p8 on_disk[HOST_PATH_ROOM];

                host_join(on_disk, sizeof(on_disk), HOST_DATA, host_kept[at].path);
                failed = system_make_directory_at(AT_FDCWD, on_disk, host_kept[at].mode);
                if (failed < 0 && failed != -EEXIST)
                        break;

                failed = system_make_directory_at(AT_FDCWD, host_kept[at].path,
                                                  host_kept[at].mode);
                if (failed < 0 && failed != -EEXIST)
                        break;

                failed = system_mount(on_disk, host_kept[at].path, 0, MS_BIND, 0);
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

static b32 host_update(host_install address_to install)
{
        p8 running[HOST_BUILD_ROOM];
        host_medium_search search;
        bipolar mounted;
        b32 failed;

        if (!host_running_build(running, sizeof(running)))
                return host_refuse("%s cannot read its own build\n", "moonwater");

        if (!host_install_refresh(install))
                return host_refuse("%s is no longer here\n", install->disk);

        if (!host_medium_find(address_of search, running, install->disk))
                return host_refuse("this session's image is on no disk but %s, "
                                   "so there is nothing to update from\n",
                                   install->disk);

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

                host_settings_empty(address_of disk);
                if (host_join(path, sizeof(path), HOST_SYSTEM, HOST_IMAGE))
                        host_settings_image(path, address_of disk);

                disk.generation++;
                host_medium_fresh(disk.medium, sizeof(disk.medium));

                failed = host_place_image(HOST_SYSTEM, running, address_of disk);

                if (!failed)
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

/* A program to the end, its failure said; the install copies trees with cp. */
static b32 host_run(string_address address_to argv)
{
        positive status = 0;
        bipolar child = system_fork();
        bipolar reaped;

        if (child < 0)
                return host_fail(argv[0], child);

        if (child == 0)
        {
                // Through the launch decision every other exec here takes.
                (void)shell_exec_file(argv[0], argv, pointer_vector_count(argv),
                                      file_environment_all());
                system_call_1(syscall(exit), 127);
        }

        do
                reaped = system_call_4(syscall(wait4), child,
                                       (positive)address_of status, 0, 0);
        while (reaped == -4);

        if (reaped < 0)
                return host_fail(argv[0], reaped);

        if (status)
                return host_refuse("%s did not finish\n", argv[0]);

        return 0;
}

/* Use an install's data, updating it first when asked, and say so. */
static b32 host_take(host_install address_to install, bool update)
{
        system_remove_at(AT_FDCWD, HOST_QUESTION, 0);

        if (update && host_update(install))
                return 1;

        if (host_attach(install))
                return 1;

        host_verdict_set("disk ", install->disk);
        host_say(log, host_label "%s, /root and /home are kept on %s\n",
                 BOWL_ROOT_DIRECTORY, install->disk);
        radio_restore();
        name_restore();
        locale_restore();
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
        name_restore();
        host_events_boot(settings);
}

static b32 host_boot(void)
{
        p8 running[HOST_BUILD_ROOM];
        host_census census;
        host_install address_to chosen = null;
        host_settings settings;
        bool known;

        host_state_ready();
        host_running_build(running, sizeof(running));

        /*      The settings the image booted with. A kernel started without
                the EFI stub has none, and takes an install's of this build
                once the disks are found. */
        known = host_settings_booted(address_of settings);
        if (known)
        {
                host_settings_keep(address_of settings);
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

        for (positive at = 0; at < census.count; at++)
        {
                host_install_read(census.found + at);
                if (!chosen && running[0] && census.found[at].readable &&
                    string_equals(census.found[at].build, running))
                        chosen = census.found + at;
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

        chosen = census.found;
        host_write_text(HOST_QUESTION, "");
        host_verdict_set("ask ", chosen->disk);
        host_say(log, host_label "%s has Moonwater installed from another build.\n"
                      host_label "A terminal will ask what to do with it, or "
                      "moonwater setup use, update or live answers from a shell.\n",
                 chosen->disk);
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
                host_say(log, host_label "1, 2 or 3? [1] ");

                if (host_read_line(answer, sizeof(answer)) < 0)
                        break;

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

        while (host_read_text(HOST_VERDICT, verdict, sizeof(verdict)) < 0)
        {
                p64 uptime = system_clock_ns(HOST_CLOCK_BOOTTIME);

                if (uptime >= HOST_VERDICT_WAIT_NS)
                        return;

                if (!said && uptime >= HOST_LOOKING_NS)
                {
                        host_say(log, host_label "looking for Moonwater on "
                                                 "this machine's disks\n");
                        said = true;
                }

                host_pause(HOST_VERDICT_POLL_NS);
        }

        if (host_starts(verdict, "ask ") &&
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

        uuid[0] = guid[3];
        uuid[1] = guid[2];
        uuid[2] = guid[1];
        uuid[3] = guid[0];
        uuid[4] = guid[5];
        uuid[5] = guid[4];
        uuid[6] = guid[7];
        uuid[7] = guid[6];
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
                        return true;
                }

                host_pause(HOST_POLL_NS);
        }

        return false;
}

/*
        Refusals first, then one question, then the writes.

        The boot image has to be on some other disk -- it is what gets
        copied, and a disk holding the only copy is the one thing that must
        not be erased. A disk with anything mounted is refused by the kernel
        itself: the exclusive open fails. Removable media is refused unless
        asked for, because the stick being installed from is removable too.
*/
static b32 host_install_disk(string_address asked, bool removable)
{
        string_address name = host_starts(asked, "/dev/") ? asked + 5 : asked;
        p8 sysfs[HOST_PATH_ROOM];
        p8 path[HOST_PATH_ROOM];
        p8 text[128];
        p8 device[HOST_NAME_ROOM + 8];
        p8 running[HOST_BUILD_ROOM];
        p8 answer[HOST_NAME_ROOM];
        p8 random[80];
        host_medium_search search;
        host_install target;
        storage_format_identity identity;
        storage_format_partition parts[2];
        p64 bytes = 0;
        b32 sector = 0;
        p64 sectors;
        p64 first;
        p64 last;
        p64 align;
        bipolar handle;
        bipolar failed;

        if (!host_name_valid(name) ||
            !host_join(sysfs, sizeof(sysfs), "/sys/class/block/", name) ||
            system_access_at(AT_FDCWD, sysfs, 0) < 0)
                return host_refuse("there is no disk called %s\n", name);

        if (host_join(path, sizeof(path), sysfs, "/partition") &&
            system_access_at(AT_FDCWD, path, 0) >= 0)
                return host_refuse("%s is a partition; name the whole disk\n", name);

        if (host_join(path, sizeof(path), sysfs, "/ro") &&
            host_read_text(path, text, sizeof(text)) > 0 && string_equals(text, "1"))
                return host_refuse("%s is read-only\n", name);

        if (!removable && host_join(path, sizeof(path), sysfs, "/removable") &&
            host_read_text(path, text, sizeof(text)) > 0 && string_equals(text, "1"))
                return host_refuse("%s is removable media; moonwater setup "
                                   "install DISK removable takes it\n", name);

        if (!host_running_build(running, sizeof(running)))
                return host_refuse("%s cannot read its own build\n", "moonwater");

        /*      Read before anything is mounted to look for the image: an
                install takes this session's settings, which is how what was
                set on a live stick is still set on the disk it installed. */
        host_settings carry;

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

        if (failed < 0 || bytes < HOST_SMALLEST || sector < 512 ||
            !storage_gpt_span(bytes / (p64)sector, (p32)sector, address_of first,
                              address_of last))
        {
                system_close(handle);
                host_unmount(HOST_MEDIUM);
                return failed < 0 ? host_fail(device, failed)
                                  : host_refuse("%s is smaller than the 2 GiB an "
                                                "install needs\n", name);
        }

        if (!host_join(path, sizeof(path), sysfs, "/device/model") ||
            host_read_text(path, text, sizeof(text)) <= 0)
                string_copy(text, "a disk");
        host_plain_line(text);

        host_say(log, host_label "Installing erases everything on %s: %s, %p GiB.\n"
                      host_label "Type %s to go on: ",
                 name, text, bytes >> 30, name);

        if (host_read_line(answer, sizeof(answer)) < 0 ||
            !string_equals(answer, name))
        {
                system_close(handle);
                host_unmount(HOST_MEDIUM);
                host_say(log, host_label "nothing written\n");
                return 1;
        }

        failed = system_random_fill(random, sizeof(random), 0);

        memory_zero(parts, sizeof(parts));
        memory_copy(identity.uuid, random, 16);
        memory_copy(identity.hash_seed, random + 16, 16);
        tools_uuid_version(identity.uuid, 6, 4);
        tools_uuid_version(identity.hash_seed, 6, 4);
        tools_uuid_version(random + 32, 7, 4);
        memory_copy(parts[0].unique, random + 48, 16);
        memory_copy(parts[1].unique, random + 64, 16);
        tools_uuid_version(parts[0].unique, 7, 4);
        tools_uuid_version(parts[1].unique, 7, 4);
        identity.time = (p32)(system_clock_ns(HOST_CLOCK_REALTIME) / 1000000000);
        identity.label = "moonwater";

        //      A 512 MiB system partition at 1 MiB, then the rest, to 16 TiB.
        sectors = bytes / (p64)sector;
        align = HOST_ALIGN_BYTES / (p64)sector;
        memory_copy(parts[0].type, storage_gpt_system_type, 16);
        memory_copy(parts[1].type, storage_gpt_linux_type, 16);
        parts[0].name = HOST_SYSTEM_NAME;
        parts[1].name = HOST_DATA_NAME;
        parts[0].first = align;
        parts[0].last = align + HOST_SYSTEM_BYTES / (p64)sector - 1;
        parts[1].first = parts[0].last + 1;
        parts[1].last = (last + 1) / align * align - 1;
        if (parts[1].last - parts[1].first + 1 >
            STORAGE_EXT4_MOST * STORAGE_EXT4_BLOCK / (p64)sector)
                parts[1].last = parts[1].first +
                                STORAGE_EXT4_MOST * STORAGE_EXT4_BLOCK / (p64)sector - 1;

        if (!failed)
        {
                host_say(log, host_label "partitioning and formatting %s\n", name);

                failed = storage_format_zero(handle, 0, HOST_ALIGN_BYTES);
        }
        if (!failed)
                failed = storage_format_zero(handle, bytes - HOST_ALIGN_BYTES,
                                             HOST_ALIGN_BYTES);
        if (!failed)
                failed = storage_format_gpt(handle, sectors, (p32)sector, random + 32,
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

        for (positive tries = 0; !failed && tries < 20; tries++)
        {
                failed = system_control(handle, HOST_BLKRRPART, 0);
                if (failed != -ERROR_BUSY)
                        break;

                host_pause(HOST_POLL_NS);
        }

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

        failed = host_mount(target.system, HOST_SYSTEM, "vfat", HOST_WRITABLE);
        if (failed < 0)
        {
                host_unmount(HOST_MEDIUM);
                return host_fail(target.system, failed);
        }

        host_say(log, host_label "writing this build to %s, from %s, with this "
                                 "session's settings\n",
                 target.system, search.name);

        failed = host_place_image(HOST_SYSTEM, running, address_of carry);
        host_unmount(HOST_SYSTEM);
        host_unmount(HOST_MEDIUM);
        if (failed)
                return 1;

        failed = host_mount(target.data, HOST_DATA, "ext4", 0);
        if (failed < 0)
                return host_fail(target.data, failed);

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
                        host_unmount(HOST_DATA);
                        return 1;
                }
        }

        host_unmount(HOST_DATA);
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

                if (!host_starts(mount->source, "/dev/"))
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

// Bindings ------------------------------------------------------

/*
        One request to a bound event. SET when `command` is not null, then
        the kernel copies the row back. A command too long is handed over
        whole, cut at the size without a terminator, so the kernel's own
        refusal answers. The listing and boot apply keep the file open;
        opening once per event was twenty-two trips through /dev/spark for
        `moonwater bind` with no arguments.
*/
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
#define HOST_SETTINGS_NEXT HOST_STATE "/settings.next"
#define HOST_SETTINGS_SECTION ".mwset\0\0"
#define HOST_SETTINGS_PAGE 4096
#define HOST_SETTINGS_BLOCKS (SPARK_SETTINGS_SLOT / 512)
#define HOST_FIBMAP 1
#define HOST_FIGETBSZ 2
#define HOST_BLKFLSBUF 0x1261
#define HOST_FADVISE_DONTNEED 4

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

        The block also holds the startup list and Canvas at boot, which nothing
        reads yet: no command takes them until Canvas does, so none is a
        setting that is accepted and then does nothing.
*/
static const struct
{
        string_address verb;
        string_address word;
        p32 flag;
} host_switches[] = {
    {"init", "mount", SPARK_SETTINGS_MOUNT_OFF},
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
        if (into->entry.length > SPARK_SETTINGS_TEXT_MOST)
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
        struct spark_settings_entry entry;

        if (length > (list == SPARK_SETTINGS_BIND ? SPARK_SETTINGS_BIND_TEXT_MOST
                                                  : SPARK_SETTINGS_TEXT_MOST))
                return list == SPARK_SETTINGS_BIND ? "a bound command is 255 bytes at most"
                                                   : "an entry is 4096 bytes at most";

        if (host_settings_count(settings, list) >=
            (list == SPARK_SETTINGS_BIND ? SPARK_SETTINGS_BIND_MOST : SPARK_SETTINGS_LIST_MOST))
                return list == SPARK_SETTINGS_BIND ? "the bind table holds 48 events at most"
                                                   : "a list holds 16 entries at most";

        if (SPARK_SETTINGS_PAYLOAD - settings->length < SPARK_SETTINGS_ENTRY + room)
                return "the settings block is full";

        memory_zero(address_of entry, sizeof(entry));
        entry.list = list;
        entry.kind = kind;
        entry.length = (p16)length;

        //      A bound command's id is its event, which the caller names.
        if (list == SPARK_SETTINGS_BIND)
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

/* An entry named by its id, all digits, or by its exact words. */
static bool host_settings_find(host_settings address_to settings, p8 list,
                               string_address wanted, host_setting address_to into)
{
        p8 text[SPARK_SETTINGS_TEXT_MOST + 1];
        positive id = 0;
        positive at = 0;
        string_address stop = wanted;
        b32 range = 0;
        bool by_id = byte_is_digit(*wanted);

        if (by_id)
                id = string_to_number_unsigned_checked(wanted, address_of stop, 10,
                                                       address_of range);
        by_id = by_id && !*stop && !range && id <= 0xffff;

        while (host_settings_next(settings, address_of at, into))
        {
                if (into->entry.list != list)
                        continue;

                if (by_id ? into->entry.id == id
                          : (host_settings_text(text, into), string_equals(text, wanted)))
                        return true;
        }

        return false;
}

/* Words given apart, joined by one space the way a shell would read them. */
static bool host_settings_words(p8 address_to into, positive room,
                                string_address address_to words, positive count,
                                positive address_to length)
{
        into[0] = end;

        for (positive at = 0; at < count; at++)
                if ((at && string_append_bounded(into, " ", room) >= room) ||
                    string_append_bounded(into, words[at], room) >= room)
                        return false;

        address_to length = string_length(into);
        return true;
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
        Where an image keeps its settings: the file offset of its first slot,
        or 0 for an image without them. The section table says where, and both
        slots must carry the magic there before anything believes it.
*/
static p64 host_settings_section(bipolar handle)
{
        p8 head[HOST_SETTINGS_PAGE];
        p64 magic[2];
        positive pe;
        positive table;
        positive count;

        if (file_transfer_exact(syscall(pread64), handle, head, sizeof(head), 0) !=
                (bipolar)sizeof(head) ||
            head[0] != 'M' || head[1] != 'Z')
                return 0;

        pe = storage_le32(head + 0x3c);
        if (pe > sizeof(head) - 24 || memory_compare(head + pe, "PE\0\0", 4))
                return 0;

        count = storage_le16(head + pe + 6);
        table = pe + 24 + storage_le16(head + pe + 20);

        for (positive at = 0; at < count && table + (at + 1) * 40 <= sizeof(head); at++)
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
        bipolar handle = system_open_at(AT_FDCWD, HOST_SETTINGS, FILE_READ | O_CLOEXEC);
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
        bipolar handle;
        bipolar set;
        bool written = false;

        host_settings_seal(settings);
        host_state_ready();
        set = host_spark_once(SPARK_IOCTL_SETTINGS_SET, address_of request,
                              FILE_READ_WRITE);

        handle = host_open_state(AT_FDCWD, HOST_SETTINGS_NEXT, 0600);
        if (handle >= 0)
        {
                written = !storage_format_write(handle, (p8 address_to)settings,
                                                SPARK_SETTINGS_SLOT, 0) &&
                          system_rename_at(AT_FDCWD, HOST_SETTINGS_NEXT, AT_FDCWD,
                                           HOST_SETTINGS, 0) >= 0;
                system_close(handle);
                if (!written)
                        system_remove_at(AT_FDCWD, HOST_SETTINGS_NEXT, 0);
        }

        return written && (set >= 0 || set == -ENOENT || set == -ENODEV);
}

typedef struct
{
        string_address build;
        host_settings address_to session;
        p8 name[HOST_NAME_ROOM];
        p8 other[HOST_NAME_ROOM];
        positive count;
        host_settings found;
} host_settings_search;

/* A FAT partition whose image is this build and, when asked, this session's copy. */
static bool host_settings_visit(storage_identity address_to identity,
                                address_any opaque)
{
        host_settings_search address_to search = (host_settings_search address_to)opaque;
        string_address name = identity->path + sizeof("/dev/") - 1;
        p8 path[HOST_PATH_ROOM];
        p8 build[HOST_BUILD_ROOM];
        host_settings newest;

        if (!string_equals(identity->type, "vfat") || !host_name_valid(name) ||
            host_mount(name, HOST_MEDIUM, "vfat", HOST_READ_ONLY) < 0)
                return true;

        if (host_join(path, sizeof(path), HOST_MEDIUM, HOST_IMAGE) &&
            host_image_build(path, build, sizeof(build)) &&
            string_equals(build, search->build) &&
            host_settings_image(path, address_of newest) >= 0 &&
            (!search->session ||
             (newest.generation == search->session->generation &&
              !memory_compare(newest.medium, search->session->medium,
                              sizeof(newest.medium)))))
        {
                if (!search->count)
                {
                        string_copy(search->name, name);
                        memory_copy_apart(address_of search->found, address_of newest,
                                          SPARK_SETTINGS_SLOT);
                }
                else if (search->count == 1)
                        string_copy(search->other, name);

                search->count++;
        }

        host_unmount(HOST_MEDIUM);
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
        if (MOONWATER_STRICT >= STRICT_TIGHT)
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
} host_settings_place;

/* The partition blocks under the slot a write goes over, as its filesystem names them. */
static string_address host_settings_map(string_address name,
                                        host_settings_place address_to place)
{
        p8 path[HOST_PATH_ROOM];
        string_address failed = null;
        bipolar handle = -ERROR_INVALID;
        b32 size = 0;

        if (host_mount(name, HOST_MEDIUM, "vfat", HOST_READ_ONLY) < 0)
                return "could not be mounted";

        if (host_join(path, sizeof(path), HOST_MEDIUM, HOST_IMAGE))
                handle = system_open_at(AT_FDCWD, path, FILE_READ | O_CLOEXEC);

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

        host_unmount(HOST_MEDIUM);
        return failed;
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
static string_address host_settings_write(string_address name,
                                          host_settings address_to settings)
{
        host_settings_place place;
        host_settings slot;
        storage_mount_table table;
        p8 device[HOST_NAME_ROOM + 8];
        p8 path[HOST_PATH_ROOM];
        p8 text[8];
        p8 block[HOST_SETTINGS_PAGE];
        string_address failed = null;
        bipolar handle;
        p64 offset = 0;
        b32 newest;

        if (!host_join(device, sizeof(device), "/dev/", name) ||
            !host_join(path, sizeof(path), "/sys/class/block/", name) ||
            !host_join(path, sizeof(path), path, "/ro"))
                return "has a name too long to write";

        if (host_read_text(path, text, sizeof(text)) > 0 && string_equals(text, "1"))
                return "is read-only";

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

        if (host_mount(name, HOST_MEDIUM, "vfat", HOST_READ_ONLY) < 0)
                return "was written, and could not be mounted again to check it";

        handle = host_join(path, sizeof(path), HOST_MEDIUM, HOST_IMAGE)
                     ? system_open_at(AT_FDCWD, path, FILE_READ | O_CLOEXEC)
                     : -ERROR_INVALID;

        if (handle >= 0)
        {
                system_call_4(syscall(fadvise64), (positive)handle, 0, 0,
                              HOST_FADVISE_DONTNEED);
                if (!host_settings_slots(handle, place.slots, address_of offset))
                        offset = 0;
                system_close(handle);
        }

        host_unmount(HOST_MEDIUM);

        if (offset != place.offset ||
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
                failed = host_settings_write(search.name, settings);

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
                               (count == 4 && (string_equals(arguments[3], "on") ||
                                               string_equals(arguments[3], "off")));

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
                        if (count >= 3 && string_equals(arguments[2], "mount"))
                                ;
                        else
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

                if (string_equals(arguments[3], "off"))
                        settings->flags |= flag;
                else
                        settings->flags &= ~flag;

                string_format(log, host_label "%s %s %s; ", verb,
                              host_switches[at].word, arguments[3]);
                return HOST_SETTINGS_CHANGED;
        }

        {
                bool adding = string_equals(arguments[2], "add");

                if (!host_settings_words(text, sizeof(text), arguments + 3, count - 3,
                                         address_of length))
                        return host_settings_refused(verb, "an entry is 4096 bytes at most",
                                                     settings);

                if (!length)
                        return HOST_SETTINGS_USAGE;

                if (adding)
                {
                        failed = host_settings_add(settings, list, SPARK_SETTINGS_COMMAND,
                                                   text, length, address_of id);
                        if (failed)
                                return host_settings_refused(verb, failed, settings);

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

/*
        A command naming a file the machine will not have when the entry runs:
        a live session's files, and /root, /home and the bowls once boot stops
        mounting them, are gone at power off.
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

        while (text[length] && text[length] != ' ' && length + 1 < sizeof(path))
        {
                path[length] = text[length];
                length++;
        }
        path[length] = end;

        if (system_access_at(AT_FDCWD, path, 0) < 0)
                return;

        host_read_text(HOST_VERDICT, verdict, sizeof(verdict));

        for (positive at = 0; at < array_count(host_kept); at++)
        {
                positive prefix = string_length(host_kept[at].path);

                kept |= host_starts(path, host_kept[at].path) && path[prefix] == '/';
        }

        if (kept && host_starts(verdict, "disk ") &&
            !(settings->flags & SPARK_SETTINGS_MOUNT_OFF))
                return;

        host_say(log, host_label "%s is read when the entry runs and is not kept "
                                 "after power off; moonwater bind %s add \"$(cat %s)\" "
                                 "keeps the script itself\n",
                 path, verb, path);
}

static b32 host_settings_command(string_address address_to arguments, positive count)
{
        host_settings settings;
        p8 text[SPARK_SETTINGS_TEXT_MOST + 1];
        positive length = 0;
        b32 outcome;

        host_need_root("moonwater");

        if (!host_settings_shaped(arguments, count))
                return host_usage();

        host_state_ready();
        host_settings_session(address_of settings);

        outcome = host_settings_apply(address_of settings, arguments, count);
        if (outcome == HOST_SETTINGS_USAGE)
                return host_usage();
        if (outcome != HOST_SETTINGS_CHANGED)
                return outcome == HOST_SETTINGS_SHOWN ? 0 : 1;

        if (!host_settings_save(address_of settings))
                return 1;

        if (count > 3 && string_equals(arguments[2], "add") &&
            host_settings_words(text, sizeof(text), arguments + 3, count - 3,
                                address_of length))
                host_settings_note(address_of settings, arguments[1], text);

        return 0;
}

/* Both slots of an image not yet in place, as these settings. */
static bipolar host_settings_stamp(string_address path, host_settings address_to settings)
{
        bipolar handle = system_open_at(AT_FDCWD, path, FILE_READ_WRITE | O_CLOEXEC);
        bipolar failed = 0;
        p64 offset;

        if (handle < 0)
                return handle;

        offset = host_settings_section(handle);
        if (offset)
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
        return failed < 0 ? failed : 0;
}

/* The settings an install's own image starts with. */
static bool host_settings_install(host_install address_to install,
                                  host_settings address_to into)
{
        p8 path[HOST_PATH_ROOM];
        b32 found = -1;

        if (!host_install_refresh(install) ||
            host_mount(install->system, HOST_LOOK, "vfat", HOST_READ_ONLY) < 0)
                return false;

        if (host_join(path, sizeof(path), HOST_LOOK, HOST_IMAGE))
                found = host_settings_image(path, into);

        host_unmount(HOST_LOOK);
        return found >= 0;
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
        log -- the kernel log window -- says when each starts and ends, and
        neither the prompt nor the desktop waits for any of it.

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
#define HOST_EVENT_SHOWN 80

static string_address address_to host_event_environment(void)
{
        static string_address seed[] = {
            SPARK_COMMAND_ENVIRONMENT(SPARK_ENVIRONMENT_ENTRY) null};

        bowl_session_prepare("/root", null);
        return bowl_environment(seed);
}

/* A command as one short line: control bytes as spaces, and cut with ... past eighty. */
static fn host_event_shown(p8 address_to into, string_address text)
{
        positive at = 0;

        for (; text[at] && at < HOST_EVENT_SHOWN; at++)
                into[at] = (p8)text[at] < ' ' || text[at] == 0x7f ? ' ' : (p8)text[at];

        into[at] = end;
        if (text[at])
                string_append_bounded(into, "...", HOST_EVENT_SHOWN + 4);
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

        string_copy_bounded(line, "<6>[moonwater] ", sizeof(line) - 1);
        for (; *parts; parts++)
                string_append_bounded(line, *parts, sizeof(line) - 1);
        string_append_bounded(line, "\n", sizeof(line));

        handle = system_open_at(AT_FDCWD, "/dev/kmsg", 01 | O_CLOEXEC);
        if (handle < 0)
                return;

        system_call_3(syscall(write), (positive)handle, (positive)line, string_length(line));
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

        address_to stopped = false;

        for (;;)
        {
                bipolar reaped = system_call_4(syscall(wait4), (positive)child,
                                               (positive)address_of status,
                                               limit ? 1 : 0, 0);

                if (reaped == child)
                        return status;
                if (reaped == -4)
                        continue;
                if (reaped < 0)
                        return 0;

                if (system_clock_ns(HOST_CLOCK_BOOTTIME) - started >= limit)
                {
                        system_call_2(syscall(kill), (positive)(-child), SIGKILL);
                        system_call_2(syscall(kill), (positive)child, SIGKILL);
                        address_to stopped = true;
                        limit = 0;
                        continue;
                }

                host_pause(HOST_EVENT_POLL_NS);
        }
}

static fn host_events_boot(host_settings address_to settings)
{
        host_setting setting;
        positive at = 0;

        if (!settings || !host_settings_count(settings, SPARK_SETTINGS_INIT) ||
            host_machine_hook_line(MOONWATER_HOOK_INIT) || system_fork())
                return;

        //      The runner, from here on: its own session, outliving boot.
        system_call(syscall(setsid));
        host_state_ready();
        host_state_directory(HOST_EVENTS_INIT, 0700);

        for (;;)
        {
                p8 verdict[HOST_NAME_ROOM + 16];

                if (host_read_text(HOST_VERDICT, verdict, sizeof(verdict)) >= 0
                        ? !host_starts(verdict, "ask ")
                        : system_clock_ns(HOST_CLOCK_BOOTTIME) >= HOST_VERDICT_WAIT_NS)
                        break;

                host_pause(HOST_POLL_NS * 2);
        }

        while (host_settings_next(settings, address_of at, address_of setting))
        {
                p8 text[SPARK_SETTINGS_TEXT_MOST + 1];
                p8 shown[HOST_EVENT_SHOWN + 4];
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
                host_event_shown(shown, text);
                positive_into_string(id, setting.entry.id);

                if (!host_join(path, sizeof(path), HOST_EVENTS_INIT "/", id) ||
                    !host_join(status_path, sizeof(status_path), path, ".status") ||
                    !host_join(path, sizeof(path), path, ".log"))
                        continue;

                {
                        string_address line[] = {"init ", id, " started: ", shown, null};

                        host_kmsg(line);
                }

                host_write_text(status_path, "running\n");
                output = host_open_state(AT_FDCWD, path, 0600);
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
        Off is usually typed into a Canvas terminal, which closes under it, so
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

static fn host_canvas_write(string_address prefix,
                            struct canvas_control address_to control)
{
        if (!control->running)
        {
                string_format(log, "%sCanvas is off; moonwater canvas on starts it\n",
                              prefix);
                return;
        }

        string_format(log, "%sCanvas is on: %s, %p window%s\n", prefix,
                      (string_address)control->driver, (positive)control->windows,
                      control->windows == 1 ? "" : "s");

        for (positive at = 0; at < control->output_count && at < SPARK_CANVAS_OUTPUTS; at++)
                string_format(log, "%s  %s: %p by %p, %p Hz\n", prefix,
                              (string_address)control->output[at].connector,
                              (positive)control->output[at].width,
                              (positive)control->output[at].height,
                              (positive)control->output[at].refresh);

        if (control->detached)
                string_format(log, "%s%p window%s still open off the desktop\n", prefix,
                              (positive)control->detached,
                              control->detached == 1 ? "" : "s");

        if (control->suspended)
                string_format(log, "%sanother program holds the display; "
                                   "Canvas ignores input until it lets go\n",
                              prefix);

        string_format(log, "%slow-latency hold: %s, taken %p time%s; "
                           "canvas thread woke %p, frame timer fired %p\n",
                      prefix, control->latency_hold ? "held" : "idle",
                      (positive)control->latency_holds,
                      control->latency_holds == 1 ? "" : "s",
                      (positive)control->thread_passes,
                      (positive)control->frame_ticks);
}

static fn host_canvas_say(struct canvas_control address_to control)
{
        host_canvas_write(host_label, control);
        log_flush();
}

/* Whether the kernel console is a screen, where init's shell is on tty1. */
static bool host_console_is_screen(void)
{
        p8 active[128];

        if (host_read_text("/sys/class/tty/console/active", active, sizeof(active)) <= 0)
                return true;

        for (string_address at = (string_address)active; *at;)
        {
                if (host_starts(at, "tty") && byte_is_digit(at[3]))
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

        //      One is enough: each canvas off over a console that is not a
        //      screen forked another, and none ended, so after a few round
        //      trips tty1 had as many shells splitting its keys.
        if (!host_tty1_held())
                host_shell_start(argv, 1, "/dev/tty1", -1);
}

/*
        moonwater canvas log and moonwater canvas terminal: the windows a
        desktop starts with. Canvas opens neither by itself; the machine
        script asks for them on the canvas on event.
*/
static b32 host_canvas_open(positive request, string_address what)
{
        struct canvas_control control;
        bipolar failed = host_canvas_request(request, address_of control);

        if (failed == -EPERM)
                return host_refuse("opening %s needs root (CAP_SYS_ADMIN)\n", what);
        if (failed == -ENODEV)
                return host_refuse("Canvas is off, so there is nowhere to open %s\n", what);
        if (failed < 0)
                return host_fail(what, failed);
        return 0;
}

/* moonwater canvas [on|off|log|terminal] */
static b32 host_canvas(string_address address_to arguments, positive count)
{
        struct canvas_control control;
        bipolar failed;

        if (count > 3)
                return host_usage();

        if (count < 3)
        {
                failed = host_canvas_request(SPARK_CANVAS_STATUS, address_of control);
                if (failed < 0)
                        return host_fail(SPARK_DEVICE, failed);

                host_canvas_say(address_of control);
                return 0;
        }

        if (string_equals(arguments[2], "off"))
        {
                //      The banner is for a desktop about to close: said over a
                //      text console, without the right to close one, or with no
                //      Canvas in the kernel, it made the refusal after it a lie.
                if (!bowl_is_root())
                        return host_refuse("turning Canvas off needs root (CAP_SYS_ADMIN)%s\n", "");
                failed = host_canvas_request(SPARK_CANVAS_STATUS, address_of control);
                if (failed < 0)
                        return host_fail(SPARK_DEVICE, failed);
                if (!control.running)
                        return host_refuse("Canvas is already off%s\n", "");

                host_say(log, host_label "Canvas off: every window closes, this one too. "
                                         "On the text console, moonwater canvas on "
                                         "brings the desktop back.\n");

                system_signal_install(1, 1, 0, 0, null);

                failed = host_canvas_request(SPARK_CANVAS_OFF, address_of control);
                if (failed == -EPERM)
                        return host_refuse("turning Canvas off needs root (CAP_SYS_ADMIN)%s\n", "");
                if (failed == -EALREADY)
                        return host_refuse("Canvas is already off%s\n", "");
                if (failed < 0)
                        return host_fail("canvas off", failed);

                if (!host_console_is_screen())
                        host_tty1_shell();

                return 0;
        }

        if (string_equals(arguments[2], "log"))
                return host_canvas_open(SPARK_CANVAS_KERNEL_LOG, "the kernel log");

        if (string_equals(arguments[2], "terminal"))
                return host_canvas_open(SPARK_CANVAS_TERMINAL, "a terminal");

        if (string_equals(arguments[2], "on"))
        {
                failed = host_canvas_request(SPARK_CANVAS_ON, address_of control);
                if (failed == -EPERM)
                        return host_refuse("turning Canvas on needs root (CAP_SYS_ADMIN)%s\n", "");
                if (failed == -EALREADY)
                        return host_refuse("Canvas is already on%s\n", "");
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
                        return host_refuse("there is no display for Canvas to start on%s\n", "");
                if (failed < 0)
                        return host_fail("canvas on", failed);

                host_canvas_say(address_of control);
                return 0;
        }

        return host_usage();
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
        if (handle < 0)
                return host_fail("firmware setup", handle);
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

        host_need_root("moonwater bios reboot");
        if (offered == -ENODEV)
                return host_refuse("this machine did not start from UEFI firmware, so "
                                   "there is no setup to restart into%s\n", "");
        if (offered < 0)
                return host_fail("firmware setup", offered);
        if (!offered)
                return host_refuse("this firmware offers no setup screen at the next boot%s\n", "");

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
#define NL80211_ATTR_STA_FLAGS2 67
#define NL80211_ATTR_CONTROL_PORT 68
#define NL80211_ATTR_PRIVACY 70
#define NL80211_ATTR_STATUS_CODE 72
#define NL80211_ATTR_CIPHER_SUITES_PAIRWISE 73
#define NL80211_ATTR_CIPHER_SUITE_GROUP 74
#define NL80211_ATTR_WPA_VERSIONS 75
#define NL80211_ATTR_AKM_SUITES 76
#define NL80211_ATTR_REQ_IE 77
#define NL80211_ATTR_REKEY_DATA 122
#define NL80211_ATTR_SCAN_FLAGS 158
#define NL80211_ATTR_EXT_FEATURES 217
#define NL80211_ATTR_PMK 254

#define NL80211_IFTYPE_ADHOC 1
#define NL80211_IFTYPE_STATION 2
#define NL80211_IFTYPE_P2P_CLIENT 8
#define NL80211_SCAN_FLAG_FLUSH 2
#define NLA_F_NESTED 0x8000
#define NL80211_AUTHTYPE_OPEN 0
#define NL80211_WPA_VERSION_2 2
#define NL80211_KEYTYPE_GROUP 0
#define NL80211_KEYTYPE_PAIRWISE 1
#define NL80211_STA_FLAG_AUTHORIZED 1
#define NL80211_EXT_FEATURE_4WAY_HANDSHAKE_STA_PSK 15
#define WLAN_CIPHER_CCMP 0x000fac04u
#define WLAN_AKM_PSK 0x000fac02u
#define NL80211_CONNECT_SECONDS 20
#define WIFI_EAPOL_SECONDS 8
#define WIFI_EAPOL_HDR 99
#define WIFI_GTK_WRAP 24
#define WIFI_WRAP_MOST 408

typedef struct
{
        b32 handle;
        p16 family;
        p32 mlme;
        p32 scan;
} nl80211;

typedef struct
{
        p32 index;
        p32 wiphy;
        bool found;
        bool has_mac;
        p8 mac[6];
        p8 name[IFNAME_SIZE];
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
        positive cursor = 0;

        if (at && size >= 2)
                info->family = memory_load_unaligned(p16, at);

        groups = (p8 address_to)netlink_find(header, GENL_HEADER,
                                             CTRL_ATTR_MCAST_GROUPS,
                                             address_of groups_length);
        while (groups && cursor + sizeof(netlink_attribute) <= groups_length)
        {
                netlink_attribute address_to attribute =
                    (netlink_attribute address_to)(groups + cursor);
                positive payload;
                p8 address_to name;
                p8 address_to id;
                positive name_length = 0;
                positive id_length = 0;

                if (attribute->length < sizeof(netlink_attribute) ||
                    cursor + attribute->length > groups_length)
                        break;
                payload = attribute->length - sizeof(netlink_attribute);
                name = (p8 address_to)netlink_find_span(
                    groups + cursor + sizeof(netlink_attribute), payload,
                    CTRL_ATTR_MCAST_GRP_NAME, address_of name_length);
                id = (p8 address_to)netlink_find_span(
                    groups + cursor + sizeof(netlink_attribute), payload,
                    CTRL_ATTR_MCAST_GRP_ID, address_of id_length);
                if (name && name_length >= 4 && id && id_length >= 4 &&
                    !memory_compare(name, "mlme", 4))
                        info->mlme = memory_load_unaligned(p32, id);
                if (name && name_length >= 5 && id && id_length >= 4 &&
                    !memory_compare(name, "scan", 5))
                        info->scan = memory_load_unaligned(p32, id);
                cursor += netlink_align(attribute->length);
        }

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

        sequence = netlink_sequence_take();
        if (!nl80211_begin(address_of request, GENL_ID_CTRL, CTRL_CMD_GETFAMILY,
                           NLM_REQUEST | NLM_ACK, sequence))
        {
                socket_close((b32)handle);
                return -1;
        }

        netlink_attribute_add(address_of request, CTRL_ATTR_FAMILY_NAME, name,
                              sizeof(name));

        if (netlink_transact((b32)handle, address_of request, sequence,
                             nl80211_family_seen, address_of info) < 0 ||
            !info.family)
        {
                socket_close((b32)handle);
                return -19;
        }

        if (info.mlme &&
            socket_option_set((b32)handle, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP,
                              address_of info.mlme, sizeof(info.mlme)) >= 0)
                session->mlme = info.mlme;

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
        p8 address_to body = (p8 address_to)header + NETLINK_HEADER;
        p32 type;
        p32 index;
        positive length = 0;
        string_address name;

        if (header->length < NETLINK_HEADER + GENL_HEADER)
                return true;
        if (body[0] != NL80211_CMD_NEW_INTERFACE &&
            body[0] != NL80211_CMD_GET_INTERFACE)
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
        if (type != NL80211_IFTYPE_STATION || !index || found->found)
                return true;

        name = (string_address)netlink_find(header, GENL_HEADER,
                                            NL80211_ATTR_IFNAME,
                                            address_of length);
        found->index = index;
        found->found = true;
        found->wiphy = nl80211_find_u32(header, NL80211_ATTR_WIPHY, 0);
        found->name[0] = end;
        if (name && length)
        {
                if (length >= IFNAME_SIZE)
                        length = IFNAME_SIZE - 1;
                memory_copy(found->name, name, length);
                found->name[length] = end;
        }

        {
                positive mac_length = 0;
                p8 address_to mac = (p8 address_to)netlink_find(
                    header, GENL_HEADER, NL80211_ATTR_MAC, address_of mac_length);

                if (mac && mac_length >= 6)
                {
                        memory_copy(found->mac, mac, 6);
                        found->has_mac = true;
                }
        }

        return true;
}

static COLD bipolar nl80211_interface(nl80211 address_to session,
                                 nl80211_iface address_to found)
{
        netlink_buffer request = {0};
        p32 sequence = netlink_sequence_take();

        memory_fill(found, 0, sizeof(*found));
        if (!nl80211_begin(address_of request, session->family,
                           NL80211_CMD_GET_INTERFACE,
                           NLM_REQUEST | NLM_DUMP, sequence))
                return -1;

        return netlink_transact(session->handle, address_of request, sequence,
                                nl80211_iface_seen, found) < 0
                   ? -1
                   : (found->found ? 0 : -19);
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

/* WPA's PBKDF2 and PRF are HMAC-SHA-1, which is net.c's HMAC under a
   different digest and nothing else. */
static COLD fn wifi_hmac_sha1(p8 address_to key, positive key_length,
                         p8 address_to data, positive length, p8 address_to out)
{
        crypto_mac mac;

        crypto_hmac_open(address_of mac, DIGEST_SHA1, 20, key, key_length);
        crypto_hmac_write(address_of mac, data, length);
        crypto_hmac_close(address_of mac, out);
}

static COLD p8 wifi_nibble(p8 byte)
{
        if (byte_is_digit(byte))
                return (p8)(byte - '0');
        byte = byte_to_lower(byte);
        return (p8)(byte - 'a' + 10);
}

static COLD bool wifi_psk(p8 address_to ssid, positive ssid_length,
                     p8 address_to pass, positive pass_length, p8 address_to pmk)
{
        positive i;

        if (pass_length == 64)
        {
                for (i = 0; i < 64; i++)
                        if (!byte_is_hexadecimal(pass[i]))
                                return false;
                for (i = 0; i < 32; i++)
                        pmk[i] = (p8)((wifi_nibble(pass[i * 2]) << 4) |
                                      wifi_nibble(pass[i * 2 + 1]));
                return true;
        }

        if (pass_length < 8 || pass_length > 63 || !ssid_length ||
            ssid_length > 32)
                return false;

        //      IEEE 802.11i: PBKDF2-HMAC-SHA1 of the passphrase, the SSID as
        //      its salt, 4096 rounds, 256 bits.
        crypto_pbkdf2(DIGEST_SHA1, 20, pass, pass_length, ssid, ssid_length,
                      4096, pmk, 32);
        return true;
}

/* One byte of InvMixColumns.  The matrix's four rows are the same four
   coefficients rotated, so the row it is wanted for says where to start
   reading them, and the multiply is net.c's constant-time one -- the
   same GF(2^8) its S-box inversion runs in. */
static COLD p8 wifi_unmix(p8 address_to column, positive row)
{
        static const p8 factor[4] = {0x0e, 0x0b, 0x0d, 0x09};
        p8 mixed = 0;

        for (positive at = 0; at < 4; at++)
                mixed ^= crypto_aes_field_multiply(
                    column[at], factor[(4 + at - row) & 3]);

        return mixed;
}

/* The inverse S-box, built rather than written out: it is the forward box's
   inverse permutation, and net.c already computes that box in the field
   it lives in. Two hundred and fifty six hand-typed bytes are two hundred
   and fifty six chances to mistype one, and built this way the two boxes
   cannot disagree. The unwrap builds it once and lends it to every block. */
static COLD fn wifi_inverse_box(p8 address_to inverse)
{
        for (positive at = 0; at < 256; at++)
                inverse[crypto_aes_substitute((p8)at)] = (p8)at;
}

static COLD fn wifi_aes_decrypt(p8 address_to key, p8 address_to in,
                           p8 address_to out, const p8 address_to inverse)
{
        p8 round[176];
        p8 state[16];
        p8 hold[16];
        positive step;
        positive row;
        p8 temp;

        crypto_aes128_expand(key, round);
        memory_copy(state, in, 16);
        for (step = 0; step < 16; step++)
                state[step] ^= round[160 + step];

        /*
                The last round is every other round without InvMixColumns, so
                the loop runs once more and leaves from the middle rather
                than repeating its first three steps underneath itself.
        */
        for (step = 9;; step--)
        {
                //      InvShiftRows: row r of the state -- the bytes r, r+4,
                //      r+8 and r+12 -- rotates right by r.
                for (row = 1; row < 4; row++)
                        for (positive turn = 0; turn < row; turn++)
                        {
                                temp = state[row + 12];
                                state[row + 12] = state[row + 8];
                                state[row + 8] = state[row + 4];
                                state[row + 4] = state[row];
                                state[row] = temp;
                        }

                for (row = 0; row < 16; row++)
                        state[row] = inverse[state[row]];
                for (row = 0; row < 16; row++)
                        state[row] ^= round[step * 16 + row];
                if (!step)
                        break;

                memory_copy(hold, state, 16);
                for (row = 0; row < 4; row++)
                        for (positive at = 0; at < 4; at++)
                                state[row * 4 + at] =
                                    wifi_unmix(hold + row * 4, at);
        }

        memory_copy(out, state, 16);
        crypto_forget(round, sizeof(round));
        crypto_forget(state, sizeof(state));
        crypto_forget(hold, sizeof(hold));
}

static COLD bool wifi_kw_unwrap(p8 address_to kek, p8 address_to wrap, positive length,
                           p8 address_to plain, positive address_to plain_length)
{
        p8 block[16];
        p8 a[8];
        p8 inverse[256];
        p8 r[WIFI_WRAP_MOST - 8];
        positive words;
        positive round;
        positive i;
        p64 t;
        bool unwrapped;

        if (length < 24 || (length & 7) || length > WIFI_WRAP_MOST)
                return false;
        words = (length / 8) - 1;
        wifi_inverse_box(inverse);
        memory_copy(a, wrap, 8);
        memory_copy(r, wrap + 8, words * 8);
        for (round = 6; round > 0; round--)
                for (i = words; i > 0; i--)
                {
                        t = (p64)words * (round - 1) + i;
                        network_store_64(a, network_load_64(a) ^ t);
                        memory_copy(block, a, 8);
                        memory_copy(block + 8, r + (i - 1) * 8, 8);
                        wifi_aes_decrypt(kek, block, block, inverse);
                        memory_copy(a, block, 8);
                        memory_copy(r + (i - 1) * 8, block + 8, 8);
                }

        //      The check value, and the unwrapped key data forgotten here
        //      whether it held or not.
        unwrapped = network_load_64(a) == 0xa6a6a6a6a6a6a6a6ull;
        if (unwrapped)
        {
                memory_copy(plain, r, words * 8);
                address_to plain_length = words * 8;
        }
        crypto_forget(block, sizeof(block));
        crypto_forget(a, sizeof(a));
        crypto_forget(r, sizeof(r));
        return unwrapped;
}

static COLD fn wifi_ptk(p8 address_to pmk, p8 address_to ap, p8 address_to sta,
                   p8 address_to anonce, p8 address_to snonce, p8 address_to ptk)
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
        memory_copy(input, label, 22);
        input[22] = 0;
        memory_copy(input + 23, data, 76);
        for (which = 0; used < 64; which++)
        {
                input[99] = (p8)which;
                wifi_hmac_sha1(pmk, 32, input, 100, hash);
                memory_copy(ptk + used, hash, used + 20 > 64 ? 64 - used : 20);
                used += 20;
        }
        crypto_forget(data, sizeof(data));
        crypto_forget(input, sizeof(input));
        crypto_forget(hash, sizeof(hash));
}

static const p8 wifi_rsn_ie[] = {0x30, 0x14, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04,
                                 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00,
                                 0x00, 0x0f, 0xac, 0x02, 0x00, 0x00};

static COLD bool nl80211_ext_bit(p8 address_to bits, positive length, positive which)
{
        return which / 8 < length && (bits[which / 8] & (1u << (which % 8)));
}

static COLD bool nl80211_wiphy_seen(netlink_header address_to header, address_any context)
{
        nl80211_wiphy_query address_to query = (nl80211_wiphy_query address_to)context;
        p8 address_to body = (p8 address_to)header + NETLINK_HEADER;
        positive size = 0;
        p8 address_to bits;
        p32 wiphy;

        if (header->length < NETLINK_HEADER + GENL_HEADER)
                return true;
        if (body[0] != NL80211_CMD_NEW_WIPHY && body[0] != NL80211_CMD_GET_WIPHY)
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
        if (netlink_transact(session->handle, address_of request, sequence,
                             nl80211_wiphy_seen, address_of query) < 0)
                return false;
        return query.offload;
}

static COLD bool nl80211_station_seen(netlink_header address_to header,
                                 address_any context)
{
        p8 address_to body = (p8 address_to)header + NETLINK_HEADER;
        p8 address_to into = (p8 address_to)context;
        positive length = 0;
        p8 address_to mac;

        if (header->length < NETLINK_HEADER + GENL_HEADER)
                return true;
        if (body[0] != NL80211_CMD_NEW_STATION &&
            body[0] != NL80211_CMD_GET_STATION)
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

static COLD bool nl80211_station(nl80211 address_to session, p32 index, p8 address_to mac)
{
        netlink_buffer request = {0};
        p32 sequence = netlink_sequence_take();

        memory_fill(mac, 0, 6);
        if (!nl80211_begin(address_of request, session->family,
                           NL80211_CMD_GET_STATION, NLM_REQUEST | NLM_DUMP,
                           sequence))
                return false;
        nl80211_attribute_u32(address_of request, NL80211_ATTR_IFINDEX, index);
        if (netlink_transact(session->handle, address_of request, sequence,
                             nl80211_station_seen, mac) < 0)
                return false;
        return mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5];
}

static COLD bipolar nl80211_new_key(nl80211 address_to session, p32 index, p8 idx,
                               p32 type, p8 address_to mac, p8 address_to key,
                               positive key_length, p8 address_to seq,
                               positive seq_length, bool group_default)
{
        netlink_buffer request = {0};
        p32 sequence = netlink_sequence_take();
        p32 ccmp = WLAN_CIPHER_CCMP;

        if (!nl80211_begin(address_of request, session->family, NL80211_CMD_NEW_KEY,
                           NLM_REQUEST | NLM_ACK, sequence))
                return -1;
        nl80211_attribute_u32(address_of request, NL80211_ATTR_IFINDEX, index);
        netlink_attribute_add(address_of request, NL80211_ATTR_KEY_DATA, key,
                              key_length);
        netlink_attribute_add(address_of request, NL80211_ATTR_KEY_IDX, address_of idx,
                              1);
        nl80211_attribute_u32(address_of request, NL80211_ATTR_KEY_CIPHER, ccmp);
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
#define WIFI_NETLINK_DROP_MEMBERSHIP 2

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
        p8 replay[8];
        p8 answered;
        bool replay_set;
        bool installed;
        bool pending_set;
        bool nonce_used;
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

/* HMAC-SHA1-128 over the frame with its MIC field zero: written into the
   frame, or compared in constant time with the MIC it carries, which is
   left in place so that another key can be tried. */
static COLD bool wifi_eapol_mic(p8 address_to kck, p8 address_to frame, positive length,
                                bool check)
{
        p8 hash[20];
        p8 carried[16];
        bool same;

        memory_copy(carried, frame + 81, 16);
        memory_fill(frame + 81, 0, 16);
        wifi_hmac_sha1(kck, 16, frame, length, hash);
        same = crypto_same(carried, hash, 16);
        memory_copy(frame + 81, check ? carried : hash, 16);
        crypto_forget(hash, sizeof(hash));
        return same;
}

/* A key frame to the access point: its key information, the replay
   counter it answers, the SNonce and RSN element for message 2, and its
   MIC under kck. The key length is 0, as wpa_supplicant sends for RSN. */
static COLD bipolar wifi_eapol_reply(wifi_link address_to link, p8 address_to kck,
                                     p16 info, p8 address_to replay, bool nonce)
{
        p8 frame[WIFI_EAPOL_HDR + sizeof(wifi_rsn_ie)];
        positive length = WIFI_EAPOL_HDR + (nonce ? sizeof(wifi_rsn_ie) : 0);
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
        network_store_16(frame + 5, info);
        memory_copy(frame + 9, replay, 8);
        if (nonce)
        {
                memory_copy(frame + 17, link->snonce, 32);
                network_store_16(frame + 97, (p16)sizeof(wifi_rsn_ie));
                memory_copy(frame + 99, wifi_rsn_ie, sizeof(wifi_rsn_ie));
        }
        wifi_eapol_mic(kck, frame, length, false);
        memory_copy(to.addr, link->bssid, 6);
        sent = socket_send(link->eapol, frame, length, 0, address_of to, sizeof(to));
        crypto_forget(frame, sizeof(frame));
        return sent == (bipolar)length ? 0 : -1;
}

/* The GTK out of key data wrapped under the KEK: 1 with it, 0 when the
   data unwraps and holds none, -1 when it does not unwrap. */
static COLD bipolar wifi_gtk_take(p8 address_to kek, p8 address_to data, positive length,
                                  p8 address_to gtk, p8 address_to idx)
{
        p8 plain[WIFI_WRAP_MOST];
        positive size = 0;
        bipolar found = 0;
        byte_reader reader;

        if (!wifi_kw_unwrap(kek, data, length, plain, address_of size))
                return -1;
        reader = byte_reader_open(plain, size);
        while (byte_reader_left(&reader))
        {
                p8 kind = byte_reader_u8(&reader);
                byte_reader element = byte_reader_vector8(&reader);

                if (!byte_reader_ok(&reader))
                        break;
                //      The GTK KDE: 00-0F-AC:1, the key id, a reserved
                //      byte, then a CCMP key of sixteen bytes.
                if (kind == 0xdd && byte_reader_left(&element) == 4 + 2 + 16 &&
                    byte_reader_u32(&element) == 0x000fac01)
                {
                        p8 key_id = byte_reader_u8(&element);

                        (void)byte_reader_u8(&element);
                        if (key_id & 3)
                        {
                                address_to idx = (p8)(key_id & 3);
                                memory_copy(gtk, byte_reader_take(&element, 16), 16);
                                found = 1;
                                break;
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
        p8 nested[3 * 4 + 16 + 16 + 8];

        netlink_attribute address_to kek = (netlink_attribute address_to)nested;
        netlink_attribute address_to kck = (netlink_attribute address_to)(nested + 20);
        netlink_attribute address_to replay = (netlink_attribute address_to)(nested + 40);

        kek->length = 20;
        kek->type = 1;
        memory_copy(nested + 4, link->ptk + 16, 16);
        kck->length = 20;
        kck->type = 2;
        memory_copy(nested + 24, link->ptk, 16);
        replay->length = 12;
        replay->type = 3;
        memory_copy(nested + 44, link->replay, 8);
        if (nl80211_begin(address_of request, link->session.family,
                          NL80211_CMD_SET_REKEY_OFFLOAD, NLM_REQUEST | NLM_ACK, sequence))
        {
                nl80211_attribute_u32(address_of request, NL80211_ATTR_IFINDEX,
                                      link->index);
                netlink_attribute_add(address_of request,
                                      NL80211_ATTR_REKEY_DATA | NLA_F_NESTED, nested,
                                      sizeof(nested));
                netlink_transact(link->session.handle, address_of request, sequence,
                                 null, null);
        }
        crypto_forget(nested, sizeof(nested));
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
        one KRACK used to reset the nonces.
*/
static COLD bipolar wifi_eapol_step(wifi_link address_to link, p8 address_to frame,
                                    positive got)
{
        positive length = wifi_eapol_length(frame, got);
        p16 info = length ? network_load_16(frame + 5) : 0;
        bool pairwise = (info & WIFI_KEY_PAIRWISE) != 0;
        bool fresh = false;
        p8 address_to kck = null;
        p8 gtk[16];
        p8 idx = 0;
        bipolar found;
        bipolar failed = 0;

        if (!length || (info & 7) != 2 || !(info & WIFI_KEY_ACK) ||
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
                         link->pending);
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
                    wifi_eapol_mic(link->ptk, frame, length, true))
                        kck = link->ptk;
        }
        else if (info & WIFI_KEY_INSTALL)
        {
                //      The pending key for a new handshake's message 3; the
                //      installed one for a message 3 resent after it.
                fresh = link->pending_set && !memory_compare(frame + 17, link->anonce, 32) &&
                        wifi_eapol_mic(link->pending, frame, length, true);
                if (fresh)
                        kck = link->pending;
                else if (link->installed && wifi_eapol_mic(link->ptk, frame, length, true))
                        kck = link->ptk;
        }
        if (!kck)
                return 0;

        found = wifi_gtk_take(kck + 16, frame + 99, network_load_16(frame + 97), gtk,
                              address_of idx);
        if (found < 0 || (!pairwise && !found))
        {
                crypto_forget(gtk, sizeof(gtk));
                return 0;
        }
        memory_copy(link->replay, frame + 9, 8);
        link->replay_set = true;

        failed = wifi_eapol_reply(link, kck, pairwise ? 0x030a : 0x0302, link->replay,
                                  false);
        if (!failed && fresh && (!link->installed || memory_compare(link->ptk + 32,
                                                                    kck + 32, 16)))
                failed = nl80211_new_key(address_of link->session, link->index, 0,
                                         NL80211_KEYTYPE_PAIRWISE, link->bssid, kck + 32,
                                         16, null, 0, false);
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
            memory_compare(gtk, link->gtk[idx], 16))
        {
                failed = nl80211_new_key(address_of link->session, link->index, idx,
                                         NL80211_KEYTYPE_GROUP, null, gtk, 16, frame + 65,
                                         6, true);
                if (!failed)
                        memory_copy(link->gtk[idx], gtk, 16);
        }
        crypto_forget(gtk, sizeof(gtk));
        if (!failed && pairwise && !link->installed)
                failed = nl80211_authorize(address_of link->session, link->index,
                                           link->bssid);
        if (failed)
                return failed;
        link->installed = true;
        wifi_rekey_offload(link);
        return pairwise ? 1 : 2;
}

/* The link's news on the mlme socket: the 802.11 reason it went, 0 while
   it stands, -1 when the socket lost events and the link has to be asked
   after. Every interface's news comes here; only this one's counts. */
static COLD b32 nl80211_link_news(nl80211 address_to session, p32 index)
{
        netlink_buffer reply = {0};
        p32 local_port = 0;
        positive at = 0;
        b32 reason = 0;
        bipolar got = netlink_receive(session->handle, address_of reply,
                                      address_of local_port);

        if (got < 0)
        {
                netlink_forget(address_of reply);
                return got == NETWORK_INTERRUPTED ? 0 : -1;
        }
        while (!reason && at + NETLINK_HEADER <= reply.used)
        {
                netlink_header address_to header = (netlink_header address_to)(reply.bytes + at);
                p8 address_to body = (p8 address_to)header + NETLINK_HEADER;
                positive size = 0;
                p8 address_to frame;

                if (header->length < NETLINK_HEADER || header->length > reply.used - at)
                        break;
                at += netlink_align(header->length);
                if (header->type != session->family ||
                    header->length < NETLINK_HEADER + GENL_HEADER ||
                    nl80211_find_u32(header, NL80211_ATTR_IFINDEX, 0) != index)
                        continue;
                if (body[0] == NL80211_CMD_DISCONNECT)
                        reason = nl80211_find_u16(header, NL80211_ATTR_REASON_CODE, 1);
                else if (body[0] == NL80211_CMD_DEAUTHENTICATE ||
                         body[0] == NL80211_CMD_DISASSOCIATE)
                {
                        //      The frame itself: its reason follows the
                        //      24-byte management header.
                        frame = (p8 address_to)netlink_find(header, GENL_HEADER,
                                                            NL80211_ATTR_FRAME,
                                                            address_of size);
                        reason = frame && size >= 26 ? frame[24] | frame[25] << 8 : 1;
                        reason = reason ? reason : 1;
                }
        }
        netlink_forget(address_of reply);
        return reason;
}

/* The link's EAPOL socket and its mlme news, waited on together until the
   deadline: bit 1 when a frame is waiting, bit 2 news, 0 at the deadline. */
static COLD bipolar wifi_wait(wifi_link address_to link,
                              const network_deadline address_to deadline)
{
        for (;;)
        {
                system_poll_descriptor waited[2] = {
                    {link->eapol, SYSTEM_POLL_READ, 0},
                    {link->session.handle, SYSTEM_POLL_READ, 0}};
                positive seconds;
                positive nanoseconds;
                timespec limit;
                bipolar ready;

                if (!network_deadline_left(deadline, address_of seconds,
                                           address_of nanoseconds))
                        return 0;
                limit.tv_sec = (b64)seconds;
                limit.tv_nsec = (b64)nanoseconds;
                ready = system_poll_wait(waited, 2, address_of limit, null);
                if (ready == NETWORK_INTERRUPTED)
                        continue;
                if (ready <= 0)
                        return ready;
                if ((waited[0].returned | waited[1].returned) & SYSTEM_POLL_INVALID)
                        return -9;
                return (waited[0].returned ? 1 : 0) | (waited[1].returned ? 2 : 0);
        }
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
               from->halen == 6 && !memory_compare(from->addr, link->bssid, 6);
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
        The keeper: the link's keys, held past the join for as long as the
        link stands, in a process of its own with nothing else open. It
        answers the access point's rekeys and ends when the link goes, and
        the machine's next pass joins again. One at a time: it holds a write
        lock on wifi.keeper, and F_GETLK names it to whatever joins or
        leaves next, which ends it and waits until it is gone.
*/
#define RADIO_KEEPER_PATH NET_STATE_DIR "/wifi.keeper"
#define RADIO_FILE_GET_LOCK 5
#define RADIO_FILE_SET_LOCK 6
#define RADIO_FILE_SET_LOCK_WAIT 7
#define RADIO_FILE_WRITE_LOCK 1
#define RADIO_FILE_UNLOCKED 2

typedef struct
{
        b16 type;
        b16 whence;
        b64 start;
        b64 length;
        b32 pid;
        b32 pad;
} radio_file_lock;

static COLD bipolar radio_keeper_open(void)
{
        host_state_ready();
        return system_open_at_mode(AT_FDCWD, RADIO_KEEPER_PATH,
                                   FILE_READ_WRITE | FILE_CREATE | O_NOFOLLOW | O_CLOEXEC,
                                   0600);
}

static COLD fn radio_keeper_stop(void)
{
        radio_file_lock lock = {.type = RADIO_FILE_WRITE_LOCK};
        bipolar handle = radio_keeper_open();

        if (handle < 0)
                return;
        if (system_call_3(syscall(fcntl), (positive)handle, RADIO_FILE_GET_LOCK,
                          (positive)address_of lock) >= 0 &&
            lock.type != RADIO_FILE_UNLOCKED && lock.pid > 0)
        {
                system_call_2(syscall(kill), (positive)lock.pid, SIGKILL);
                lock.type = RADIO_FILE_WRITE_LOCK;
                lock.pid = 0;
                while (system_call_3(syscall(fcntl), (positive)handle,
                                     RADIO_FILE_SET_LOCK_WAIT,
                                     (positive)address_of lock) == -4)
                        ;
        }
        system_close(handle);
}

static COLD fn radio_avoid_add(p8 address_to bssid);

static COLD fn wifi_keep(wifi_link address_to link)
{
        for (;;)
        {
                network_deadline check;
                bipolar ready;
                p8 now[6];

                if (!network_deadline_begin(address_of check, WIFI_KEEP_CHECK_SECONDS, 0))
                        return;
                ready = wifi_wait(link, address_of check);
                if (ready < 0)
                        return;
                if (ready & 2)
                {
                        b32 news = nl80211_link_news(address_of link->session,
                                                     link->index);

                        if (news > 0 ||
                            (news < 0 && !nl80211_station(address_of link->session,
                                                          link->index, now)))
                                break;
                }
                if (ready & 1)
                        wifi_eapol_take(link);
                if (!ready && (!nl80211_station(address_of link->session, link->index, now) ||
                               memory_compare(now, link->bssid, 6)))
                        break;
        }
        //      Sent away, or the access point stopped answering: the next
        //      join tries another with the same name first.
        radio_avoid_add(link->bssid);
}

/* The joined link, from the join until its keeper takes it or it is let go. */
static wifi_link radio_joined = {.session = {.handle = -1}, .eapol = -1};

/* The keeper for the link just joined, if it has keys to keep, started
   once the joining process has said what it has to and forgotten the
   passwords it read. Forked twice, so that init reaps it and not the
   machine process, which waits only for its own join. */
static COLD fn radio_keeper_start(void)
{
        wifi_link address_to link = address_of radio_joined;
        radio_file_lock lock = {.type = RADIO_FILE_WRITE_LOCK};
        positive low = link->eapol < link->session.handle ? (positive)link->eapol
                                                          : (positive)link->session.handle;
        positive high = link->eapol < link->session.handle ? (positive)link->session.handle
                                                           : (positive)link->eapol;
        positive status = 0;
        bipolar quiet;
        bipolar handle;
        bipolar child = link->eapol >= 3 && link->session.handle >= 3 ? system_fork() : -1;

        if (child)
        {
                if (child > 0)
                        system_call_4(syscall(wait4), (positive)child,
                                      (positive)address_of status, 0, 0);
                wifi_link_close(link);
                return;
        }
        if (system_fork())
                system_call_1(syscall(exit_group), 0);

        //      Its own session, and nothing of the joining process open: not
        //      its terminal or pipes, which would never see an end, not the
        //      radio lock, which would never come free, and none of the
        //      machine process's descriptors either.
        system_call(syscall(setsid));
        quiet = system_open_at(AT_FDCWD, "/dev/null", FILE_READ_WRITE | O_CLOEXEC);
        for (positive at = 0; quiet >= 0 && at < 3; at++)
                if ((positive)quiet != at)
                        system_call_3(syscall(dup3), (positive)quiet, at, 0);
        if (low > 3)
                system_call_3(syscall(close_range), 3, low - 1, 0);
        if (high > low + 1)
                system_call_3(syscall(close_range), low + 1, high - 1, 0);
        system_call_3(syscall(close_range), high + 1, ~(p32)0, 0);
        //      The join's scan news is nothing to the keeper, which would
        //      wake for every scan anyone asked for.
        if (link->session.scan)
                socket_option_set(link->session.handle, SOL_NETLINK,
                                  WIFI_NETLINK_DROP_MEMBERSHIP,
                                  address_of link->session.scan,
                                  sizeof(link->session.scan));

        handle = radio_keeper_open();
        if (handle >= 0 && system_call_3(syscall(fcntl), (positive)handle,
                                         RADIO_FILE_SET_LOCK, (positive)address_of lock) >= 0)
                wifi_keep(link);
        wifi_link_close(link);
        system_call_1(syscall(exit_group), 0);
}

static COLD bool wifi_link_mac(string_address name, p8 address_to mac)
{
        netlink_search search;
        bipolar handle = netlink_open_groups(0);

        memory_fill(address_of search, 0, sizeof(search));
        if (handle < 0)
                return false;
        search.wanted = name;
        if (netlink_link_find((b32)handle, address_of search) < 0 ||
            !search.has_hardware)
        {
                socket_close((b32)handle);
                return false;
        }
        memory_copy(mac, search.hardware, 6);
        socket_close((b32)handle);
        return true;
}

static COLD bipolar nl80211_wait_associated(nl80211 address_to session, p32 sequence,
                                       p32 index, p8 address_to bssid)
{
        netlink_buffer reply = {0};
        network_deadline deadline;
        bool got_ack = false;
        bool associated = false;
        bipolar ack = 0;
        p64 last_poll = 0;

        if (!network_deadline_begin(address_of deadline, NL80211_CONNECT_SECONDS, 0))
                return -1;

        for (;;)
        {
                p32 local_port = 0;
                bipolar got;
                positive at = 0;

                if (got_ack && !associated && !session->mlme)
                {
                        p64 now = clock_monotonic_nanoseconds();

                        if (!last_poll || now - last_poll >= 200000000)
                        {
                                last_poll = now;
                                if (nl80211_station(session, index, bssid))
                                {
                                        netlink_forget(address_of reply);
                                        return 0;
                                }
                        }
                }

                got = network_wait_readable_until(session->handle, address_of deadline);
                if (got <= 0)
                {
                        netlink_forget(address_of reply);
                        if (got_ack && !associated &&
                            nl80211_station(session, index, bssid))
                                return 0;
                        return got < 0 ? got : -110;
                }

                got = netlink_receive(session->handle, address_of reply,
                                      address_of local_port);
                if (got == NETWORK_INTERRUPTED)
                        continue;
                if (got < 0)
                {
                        netlink_forget(address_of reply);
                        return got;
                }

                while (at + NETLINK_HEADER <= reply.used)
                {
                        netlink_header address_to header =
                            (netlink_header address_to)(reply.bytes + at);
                        p8 address_to body;

                        if (header->length < NETLINK_HEADER ||
                            at + header->length > reply.used)
                                break;
                        if (header->type == NLMSG_IS_ERROR &&
                            header->sequence == sequence &&
                            header->port == local_port)
                        {
                                ack = netlink_status(header, false);
                                got_ack = true;
                                if (ack < 0)
                                {
                                        netlink_forget(address_of reply);
                                        return ack;
                                }
                        }
                        else if (header->length >= NETLINK_HEADER + GENL_HEADER &&
                                 header->type == session->family)
                        {
                                body = (p8 address_to)header + NETLINK_HEADER;
                                if (nl80211_find_u32(header, NL80211_ATTR_IFINDEX, 0) ==
                                    index)
                                {
                                        if (body[0] == NL80211_CMD_CONNECT)
                                        {
                                                p16 status = nl80211_find_u16(
                                                    header, NL80211_ATTR_STATUS_CODE,
                                                    0);
                                                positive mac_length = 0;
                                                p8 address_to mac;

                                                if (nl80211_attr(header,
                                                                 NL80211_ATTR_TIMED_OUT))
                                                {
                                                        netlink_forget(address_of reply);
                                                        return -110;
                                                }
                                                if (status)
                                                {
                                                        netlink_forget(address_of reply);
                                                        return -111;
                                                }
                                                mac = (p8 address_to)netlink_find(
                                                    header, GENL_HEADER, NL80211_ATTR_MAC,
                                                    address_of mac_length);
                                                if (mac && mac_length >= 6)
                                                        memory_copy(bssid, mac, 6);
                                                associated = true;
                                        }
                                        else if (body[0] == NL80211_CMD_DISCONNECT &&
                                                 got_ack)
                                        {
                                                netlink_forget(address_of reply);
                                                return -111;
                                        }
                                }
                        }
                        at += netlink_align(header->length);
                }

                if (got_ack && associated)
                {
                        netlink_forget(address_of reply);
                        return 0;
                }
        }
}

static COLD bipolar nl80211_connect(nl80211 address_to session, p32 index,
                               p8 address_to ssid, positive ssid_length,
                               p8 address_to pmk, bool offload, p8 address_to bssid,
                               p32 frequency)
{
        netlink_buffer request = {0};
        p32 sequence = netlink_sequence_take();
        p32 open = NL80211_AUTHTYPE_OPEN;
        p32 version = NL80211_WPA_VERSION_2;
        p32 ccmp = WLAN_CIPHER_CCMP;
        p32 psk = WLAN_AKM_PSK;
        bipolar sent;

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
                                      (address_any)wifi_rsn_ie, sizeof(wifi_rsn_ie));
                if (offload)
                        netlink_attribute_add(address_of request, NL80211_ATTR_PMK, pmk,
                                              32);
                else
                        netlink_attribute_add(address_of request,
                                              NL80211_ATTR_CONTROL_PORT, null, 0);
        }

        if (request.failed)
        {
                netlink_forget(address_of request);
                return -1;
        }

        sent = socket_send(session->handle, request.bytes, request.used, 0, 0, 0);
        netlink_forget(address_of request);
        if (sent < 0)
                return sent;
        return sequence;
}

static COLD bool nl80211_associated(void)
{
        nl80211 session;
        nl80211_iface iface;
        p8 mac[6];
        bool up = false;

        if (nl80211_open(address_of session) < 0)
                return false;
        if (!nl80211_interface(address_of session, address_of iface))
                up = nl80211_station(address_of session, iface.index, mac);
        nl80211_close(address_of session);
        return up;
}

static COLD bipolar radio_bss_choose(nl80211 address_to session, p32 index,
                                     p8 address_to ssid, positive ssid_length, bool secured,
                                     p8 address_to bssid, p32 address_to frequency);

static COLD bool wifi_mac_set(p8 address_to mac)
{
        return (mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5]) != 0;
}

/*
        A join, into link: on success with a four-way handshake of its own,
        the link keeps its sockets and keys for the keeper; otherwise they
        are let go. -113 when a scan heard no network by this name.
*/
static COLD bipolar nl80211_join(p8 address_to ssid, positive ssid_length, p8 address_to pmk,
                                 wifi_link address_to link)
{
        nl80211_iface iface;
        p8 chosen[6];
        p32 frequency = 0;
        bipolar picked = -1;
        bipolar route;
        bipolar failed;
        bipolar sequence;
        bool offload = false;

        wifi_link_close(link);
        memory_fill(chosen, 0, 6);
        //      Whatever keeps the last link's keys goes first: its EAPOL
        //      socket would hear this join's message 1 as a rekey.
        radio_keeper_stop();
        failed = nl80211_open(address_of link->session);
        if (!failed)
                failed = nl80211_interface(address_of link->session, address_of iface);
        if (failed < 0)
        {
                wifi_link_close(link);
                return failed;
        }
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

        picked = radio_bss_choose(address_of link->session, iface.index, ssid, ssid_length,
                                  pmk != null, chosen, address_of frequency);
        if (!picked)
        {
                wifi_link_close(link);
                return -113;
        }

        if (pmk)
        {
                memory_copy(link->pmk, pmk, 32);
                offload = nl80211_psk_offload(address_of link->session, iface.wiphy);
        }

        //      The driver's own four-way handshake first where it has one;
        //      if that fails, once more with this one.
        for (;;)
        {
                if (pmk && !offload && link->eapol < 0)
                {
                        failed = net_packet_open(iface.index, ETH_P_PAE);
                        if (failed < 0)
                                break;
                        link->eapol = (b32)failed;
                }
                nl80211_disconnect(address_of link->session, iface.index);
                memory_fill(link->bssid, 0, 6);
                sequence = nl80211_connect(address_of link->session, iface.index, ssid,
                                           ssid_length, pmk, offload,
                                           picked > 0 ? chosen : null, frequency);
                failed = sequence < 0 ? sequence
                                      : nl80211_wait_associated(address_of link->session,
                                                                (p32)sequence, iface.index,
                                                                link->bssid);
                if (!failed && link->eapol >= 0)
                {
                        if (!wifi_mac_set(link->bssid))
                                nl80211_station(address_of link->session, iface.index,
                                                link->bssid);
                        failed = wifi_mac_set(link->sta) && wifi_mac_set(link->bssid)
                                     ? wifi_handshake(link)
                                     : -1;
                }
                if (!failed || !pmk || !offload)
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
                wifi_link_close(link);
        }
        else if (link->eapol < 0)
                wifi_link_close(link);
        return failed;
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
#define RADIO_LOCK_EX 2
#define RADIO_LOCK_NB 4
#define RADIO_LOCK_UN 8

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

/* The words a switch is set by: 1 for on, 0 for off, -1 for anything else. */
static bipolar host_onoff(string_address word)
{
        return string_equals(word, "on") ? 1 : string_equals(word, "off") ? 0 : -1;
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

/* What a switch's word says: 1 on, 0 off, -1 when it says neither or is not
   there, which is the same as on to everything that asks. */
static bipolar radio_power(string_address path)
{
        p8 text[16];

        return host_read_word(path, text, sizeof(text)) < 0 ? -1
                                                              : host_onoff((string_address)text);
}

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

        for (at = 0; at < count; at++)
        {
                if (used + networks[at].ssid_length + networks[at].pass_length +
                        2 >=
                    sizeof(text))
                {
                        crypto_forget(text, sizeof(text));
                        return -1;
                }
                memory_copy(text + used, networks[at].ssid,
                            networks[at].ssid_length);
                used += networks[at].ssid_length;
                text[used++] = '\n';
                memory_copy(text + used, networks[at].pass,
                            networks[at].pass_length);
                used += networks[at].pass_length;
                text[used++] = '\n';
        }

        {
                bipolar failed = host_write_file(NET_WIFI_LIST, text, used,
                                                 0600, true);

                crypto_forget(text, sizeof(text));
                return failed;
        }
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

/* directory/name/leaf, or false if it does not fit. */
static bool radio_sys_path(p8 address_to into, positive room, string_address directory,
                           string_address name, string_address leaf)
{
        return host_join(into, room, directory, (string_address) "/") &&
               string_append_bounded(into, name, room) < room &&
               string_append_bounded(into, leaf, room) < room;
}

static bipolar radio_sys_read(string_address directory, string_address name,
                              string_address leaf, p8 address_to into, positive room)
{
        p8 path[256];

        if (!radio_sys_path(path, sizeof(path), directory, name, leaf))
        {
                into[0] = end;
                return -1;
        }
        return host_read_text((string_address)path, into, room);
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
        if (!radio_sys_path(path, sizeof(path), directory, name,
                            (string_address) "/driver"))
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

        if (radio_sys_path(path, sizeof(path), directory, name,
                           (string_address) "/phy80211") &&
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

        host_read_text((string_address) "/sys/module/firmware_class/parameters/path",
                       custom, sizeof(custom));
        host_read_text((string_address) "/proc/sys/kernel/osrelease", release,
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
        Why wifi cannot be used, into why, or false when the machine shows
        nothing wrong: a radio is there and nothing blocks it.
*/
static bool radio_wifi_why(p8 address_to why, positive room)
{
        radio_card card;
        radio_said said;
        p8 rfkill = 0;

        why[0] = end;
        if (radio_power(NET_WIFI_POWER) == 0)
        {
                radio_line(why, room, (string_address) "switched off (moonwater wifi on)",
                           null, null, null, null);
                return true;
        }

        host_each_entry((string_address) "/sys/class/rfkill", radio_rfkill_visit,
                        address_of rfkill);
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
};

static const string_address radio_security_words[] = {
    (string_address) "open", (string_address) "WEP",    (string_address) "WPA",
    (string_address) "WPA2", (string_address) "WPA2/3", (string_address) "WPA3",
    (string_address) "OWE",  (string_address) "802.1X",
};

typedef struct
{
        p8 ssid[RADIO_SSID_MOST + 1];
        p8 ssid_length;
        p8 security;
        //      What the access point's last beacon asked, where the kernel
        //      keeps one apart from the frame its other elements came from.
        p8 beacon_security;
        bool joined;
        b32 mbm;
        p32 frequency;
        p32 seen;
        p8 bssid[6];
} radio_heard;

typedef struct
{
        radio_heard heard[RADIO_AIR_MOST];
        positive count;
        p32 freshest;
        bool any;
} radio_air;

/* What an RSN element's key management suites ask of a station. */
static p8 radio_rsn_security(p8 address_to element, positive length)
{
        byte_reader reader = byte_reader_open(element, length);
        positive count;
        bool psk = false, sae = false, eap = false, owe = false;

        //      The version and the group cipher, then the pairwise ciphers
        //      and the key management suites, each a count and that many
        //      four byte suites. What is cut short says WPA2.
        (void)byte_reader_skip(&reader, 2 + 4);
        count = byte_reader_u16le(&reader);
        (void)byte_reader_skip(&reader, 4 * count);
        count = byte_reader_u16le(&reader);
        if (!byte_reader_ok(&reader))
                return RADIO_WPA2;
        for (positive which = 0; which < count && byte_reader_left(&reader) >= 4; which++)
        {
                p32 oui = byte_reader_u24(&reader);
                p8 suite = byte_reader_u8(&reader);

                if (oui != 0x000fac)
                        continue;
                if (suite == 2 || suite == 4 || suite == 6)
                        psk = true;
                else if (suite == 8 || suite == 9 || suite == 24 || suite == 25)
                        sae = true;
                else if (suite == 18)
                        owe = true;
                else
                        eap = true;
        }
        return psk && sae ? RADIO_WPA23
               : sae      ? RADIO_WPA3
               : psk      ? RADIO_WPA2
               : owe      ? RADIO_OWE
               : eap      ? RADIO_EAP
                          : RADIO_WPA2;
}

/* What the elements of one frame ask of a station: the first RSN element's
   key management, else a WPA vendor element, else what the capability's
   privacy bit says. */
static p8 radio_ies_security(p8 address_to elements, positive size, p16 capability)
{
        byte_reader ies = byte_reader_open(elements, elements ? size : 0);
        bool wpa = false;

        while (byte_reader_left(&ies))
        {
                p8 id = byte_reader_u8(&ies);
                byte_reader data = byte_reader_vector8(&ies);
                positive span = byte_reader_left(&data);

                if (!byte_reader_ok(&ies))
                        break;
                if (id == 48)
                        return radio_rsn_security((p8 address_to)byte_reader_here(&data), span);
                if (id == 221 && span >= 4 && byte_reader_u32(&data) == 0x0050f201)
                        wpa = true;
        }
        return wpa ? RADIO_WPA : (capability & 0x10) ? RADIO_WEP : RADIO_OPEN;
}

/* One access point of a scan dump: false for a message that is not one. */
static bool radio_bss_read(netlink_header address_to header, radio_heard address_to one)
{
        p8 address_to body = (p8 address_to)header + NETLINK_HEADER;
        positive length = 0;
        positive size = 0;
        p8 address_to bss;
        p8 address_to elements;
        p8 address_to value;
        byte_reader ies;
        p8 address_to beacon;
        positive beacon_size = 0;
        bool rsn = false, wpa = false, named = false;
        p16 capability = 0;

        if (header->length < NETLINK_HEADER + GENL_HEADER ||
            body[0] != NL80211_CMD_NEW_SCAN_RESULTS)
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
                else if (id == 48 && !rsn)
                {
                        rsn = true;
                        one->security = radio_rsn_security(
                            (p8 address_to)byte_reader_here(&data), span);
                }
                else if (id == 221 && span >= 4 &&
                         byte_reader_u32(&data) == 0x0050f201)
                        wpa = true;
        }
        if (!rsn)
                one->security = wpa ? RADIO_WPA : (capability & 0x10) ? RADIO_WEP : RADIO_OPEN;
        one->beacon_security = beacon && beacon != elements
                                   ? radio_ies_security(beacon, beacon_size, capability)
                                   : one->security;
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
                if ((one->joined && !have->joined) ||
                    (one->joined == have->joined && one->mbm > have->mbm))
                        *have = *one;
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

        if (!radio_bss_read(header, address_of one))
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

/* Milliseconds since the last whole scan asked for here, or the most there is. */
static p64 radio_scan_age(void)
{
        p8 text[24];
        p64 now = system_clock_ns(HOST_CLOCK_BOOTTIME) / 1000000;
        p64 then;

        if (host_read_text(RADIO_SCAN_PATH, text, sizeof(text)) <= 0)
                return ~(p64)0;
        then = string_to_positive(text);
        return then <= now ? now - then : ~(p64)0;
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
        and 5 GHz channels: a network at 6 GHz asks for WPA3, which the
        join cannot give, and its channels are all listened to rather than
        asked, which on a radio with that band made the whole scan outlast
        the five seconds it is given. A radio without every one of those
        channels refuses the list, and is scanned whole.
*/
static const p16 radio_join_channels[][3] = {
    {2412, 2472, 5}, {5180, 5320, 20}, {5500, 5700, 20}, {5745, 5825, 20}};

static bool radio_air_scan(nl80211 address_to session, p32 index, p8 address_to ssid,
                           positive ssid_length, bool joinable)
{
        netlink_buffer request = {0};
        netlink_buffer reply = {0};
        network_deadline deadline;
        p32 sequence = netlink_sequence_take();
        p8 names[4 + 4 + RADIO_SSID_MOST + 3];
        netlink_attribute address_to wildcard = (netlink_attribute address_to)names;
        netlink_attribute address_to named = (netlink_attribute address_to)(names + 4);
        bool acked = false;
        bool whole = false;

        if (ssid_length > RADIO_SSID_MOST)
                ssid_length = 0;
        memory_fill(names, 0, sizeof(names));
        wildcard->length = 4;
        wildcard->type = 1;
        named->length = (p16)(4 + ssid_length);
        named->type = 2;
        if (ssid_length)
                memory_copy(names + 8, ssid, ssid_length);
        if (!session->scan ||
            socket_option_set(session->handle, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP,
                              address_of session->scan, sizeof(session->scan)) < 0)
                return false;
        if (!nl80211_begin(address_of request, session->family, NL80211_CMD_TRIGGER_SCAN,
                           NLM_REQUEST | NLM_ACK, sequence))
                return false;
        nl80211_attribute_u32(address_of request, NL80211_ATTR_IFINDEX, index);
        netlink_attribute_add(address_of request, NL80211_ATTR_SCAN_SSIDS | NLA_F_NESTED,
                              names, ssid_length ? 4 + netlink_align(4 + ssid_length) : 4);
        if (joinable)
        {
                p8 channels[37 * 8];
                positive used = 0;

                for (positive band = 0; band < 4; band++)
                        for (p32 mhz = radio_join_channels[band][0];
                             mhz <= radio_join_channels[band][1] && used < sizeof(channels);
                             mhz += radio_join_channels[band][2], used += 8)
                        {
                                netlink_attribute address_to one =
                                    (netlink_attribute address_to)(channels + used);

                                one->length = 8;
                                one->type = (p16)(used / 8 + 1);
                                memory_copy(channels + used + 4, address_of mhz, 4);
                        }
                netlink_attribute_add(address_of request,
                                      NL80211_ATTR_SCAN_FREQUENCIES | NLA_F_NESTED,
                                      channels, used);
        }
        nl80211_attribute_u32(address_of request, NL80211_ATTR_SCAN_FLAGS,
                              NL80211_SCAN_FLAG_FLUSH);
        if (request.failed ||
            socket_send(session->handle, request.bytes, request.used, 0, 0, 0) < 0 ||
            !network_deadline_begin(address_of deadline, RADIO_AIR_WAIT_SECONDS, 0))
        {
                netlink_forget(address_of request);
                return false;
        }
        netlink_forget(address_of request);

        while (network_wait_readable_until(session->handle, address_of deadline) > 0)
        {
                p32 local_port = 0;
                bipolar got = netlink_receive(session->handle, address_of reply,
                                              address_of local_port);
                positive at = 0;

                if (got == NETWORK_INTERRUPTED)
                        continue;
                if (got < 0)
                        break;
                while (at + NETLINK_HEADER <= reply.used)
                {
                        netlink_header address_to header =
                            (netlink_header address_to)(reply.bytes + at);
                        p8 address_to body = (p8 address_to)header + NETLINK_HEADER;

                        if (header->length < NETLINK_HEADER || at + header->length > reply.used)
                                break;
                        // Refused for any reason but a scan already running:
                        // no results are coming, so the cached ones stand.
                        if (header->type == NLMSG_IS_ERROR && header->sequence == sequence &&
                            !acked)
                        {
                                bipolar status = netlink_status(header, false);

                                acked = true;
                                whole = status >= 0;
                                if (status < 0 && status != -16)
                                {
                                        netlink_forget(address_of reply);
                                        return joinable && status == -22 &&
                                               radio_air_scan(session, index, ssid,
                                                              ssid_length, false);
                                }
                        }
                        else if (header->type == session->family &&
                                 header->length >= NETLINK_HEADER + GENL_HEADER &&
                                 (body[0] == NL80211_CMD_NEW_SCAN_RESULTS ||
                                  body[0] == NL80211_CMD_SCAN_ABORTED) &&
                                 nl80211_find_u32(header, NL80211_ATTR_IFINDEX, 0) == index)
                        {
                                if (whole && body[0] == NL80211_CMD_NEW_SCAN_RESULTS)
                                {
                                        p8 number[24];

                                        bipolar_into_string(
                                            number,
                                            (bipolar)(system_clock_ns(
                                                          HOST_CLOCK_BOOTTIME) /
                                                      1000000));
                                        host_state_ready();
                                        host_write_file(RADIO_SCAN_PATH, number, string_length(number),
                                                        0644, false);
                                }
                                whole = body[0] == NL80211_CMD_NEW_SCAN_RESULTS;
                                netlink_forget(address_of reply);
                                return whole;
                        }
                        at += netlink_align(header->length);
                }
        }
        netlink_forget(address_of reply);
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
        if (nl80211_interface(address_of session, address_of iface) < 0)
        {
                nl80211_close(address_of session);
                return false;
        }
        radio_air_dump(address_of session, iface.index, radio_air_seen, air);
        if (fresh && bowl_is_root() &&
            (fresh == RADIO_AIR_NOW || !air->any || air->freshest > RADIO_AIR_STALE_MS ||
             radio_scan_age() > RADIO_AIR_STALE_MS))
        {
                bipolar route = netlink_open_groups(0);

                if (route >= 0)
                {
                        netlink_link_up((b32)route, iface.index);
                        socket_close((b32)route);
                }
                radio_air_scan(address_of session, iface.index, null, 0, joinable);
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
        nl80211_close(address_of session);

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

static positive radio_avoid_load(radio_avoid address_to into)
{
        bipolar got = file_read_once_at(AT_FDCWD, RADIO_AVOID_PATH, into,
                                        RADIO_AVOID_MOST * sizeof(radio_avoid));
        positive count = got > 0 ? (positive)got / sizeof(radio_avoid) : 0;
        positive kept = 0;
        p64 now = system_clock_ns(HOST_CLOCK_BOOTTIME) / 1000000;

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
} radio_pick;

/* Whether what an access point's frame asks of a station is what a saved
   network wants: WPA2 for one with a password, nothing for one without. */
static bool radio_security_fits(bool secured, p8 security)
{
        return secured ? security == RADIO_WPA2 || security == RADIO_WPA23
                       : security == RADIO_OPEN;
}

/* The strongest the kernel still lists by the name, one given up on only
   if no other is, and the one given up on longest ago before the rest. */
static bool radio_pick_seen(netlink_header address_to header, address_any context)
{
        radio_pick address_to pick = (radio_pick address_to)context;
        radio_heard one;
        p64 until = 0;
        bool fits;

        if (!radio_bss_read(header, address_of one) || one.seen > RADIO_AIR_STALE_MS ||
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
        //      response does not push the real one out behind a twin.
        fits = radio_security_fits(pick->secured, one.security) ||
               radio_security_fits(pick->secured, one.beacon_security);
        if (pick->found && (fits < pick->fits ||
                            (fits == pick->fits &&
                             (until > pick->until ||
                              (until == pick->until && one.mbm <= pick->best.mbm)))))
                return true;
        pick->best = one;
        pick->found = true;
        pick->until = until;
        pick->fits = fits;
        return true;
}

/*
        The access point a join asks for by its BSSID. A join by name alone
        took whatever the kernel's cache named, and the cache keeps an
        access point for thirty seconds after it last heard it: after a
        disconnect that was the one that had just stopped answering, and
        authentication with it timed out, again and again. So the cache is
        trusted for anything but an access point given up on, and when it
        has nothing else by the name it is flushed and the air scanned for
        the name. 1 with one chosen, 0 when the scan heard none by this
        name, -1 when no scan could be had: the join then goes by name.
*/
static COLD bipolar radio_bss_choose(nl80211 address_to session, p32 index,
                                     p8 address_to ssid, positive ssid_length, bool secured,
                                     p8 address_to bssid, p32 address_to frequency)
{
        radio_avoid avoid[RADIO_AVOID_MOST];
        radio_pick pick = {
            .ssid = ssid, .ssid_length = ssid_length, .avoid = avoid, .secured = secured};
        bipolar heard = 1;

        pick.avoid_count = radio_avoid_load(avoid);
        radio_air_dump(session, index, radio_pick_seen, address_of pick);
        if (!pick.found || pick.until || !pick.fits)
        {
                heard = radio_air_scan(session, index, ssid, ssid_length, true) ? 1 : -1;
                pick.found = false;
                radio_air_dump(session, index, radio_pick_seen, address_of pick);
        }
        if (!pick.found)
                return heard > 0 ? 0 : -1;
        memory_copy(bssid, pick.best.bssid, 6);
        address_to frequency = pick.best.frequency;
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
                into[used++] = '\\';
                into[used++] = 'x';
                used += memory_into_hex(into + used, name + at, 1);
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

/* Whether moonwater can join what a network asks for. */
static bool radio_security_joinable(p8 security)
{
        return security == RADIO_OPEN || security == RADIO_WPA2 || security == RADIO_WPA23;
}

/* ---- wifi: what the last join said, and the password asked for ---- */

/* The reason a join gave, as the words that follow the network's name. */
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
                                : (string_address) "could not be joined";
}

/* A join's failure that is the network's doing, which radio_join_words
   says, rather than the machine's. */
static bool radio_join_said(bipolar failed)
{
        return failed == -110 || failed == -111 || failed == -13 || failed == -62 ||
               failed == -121 || failed == -113 || (failed <= -1000 && failed > -1000 - 65536);
}

/* The network the machine last failed to join and why, for bare wifi and
   status; gone once a join works. */
static fn radio_last_set(string_address ssid, bipolar failed)
{
        p8 text[RADIO_SSID_MOST + 32];
        p8 number[24];

        if (!failed || failed == -19)
        {
                system_remove_at(AT_FDCWD, RADIO_LAST_PATH, 0);
                return;
        }
        bipolar_into_string(number, -failed);
        radio_line(text, sizeof(text), ssid, (string_address) "\n", number,
                   (string_address) "\n", null);
        host_state_ready();
        host_write_file(RADIO_LAST_PATH, text, string_length(text), 0644, false);
}

static bool radio_last_get(p8 address_to ssid, positive room, bipolar address_to failed)
{
        p8 text[RADIO_SSID_MOST + 32];
        positive at = 0;

        if (host_read_text(RADIO_LAST_PATH, text, sizeof(text)) <= 0)
                return false;
        at = (positive)(string_first_of_or_end(text, '\n') - text);
        if (!text[at] || at > RADIO_SSID_MOST)
                return false;
        text[at] = end;
        string_copy_bounded(ssid, text, room);
        *failed = -(bipolar)string_to_positive(text + at + 1);
        return true;
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
                                   string_address ssid, bool only_asked)
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

/* A 64-character password is the key itself, in hex. */
static bool radio_hex_key(string_address pass)
{
        for (positive at = 0; pass[at]; at++)
                if (!byte_is_hexadecimal(pass[at]))
                        return false;
        return true;
}

static bipolar radio_wifi_join(string_address ssid, string_address pass)
{
        p8 pmk[32];
        bipolar failed = -19;
        bool secured = pass && pass[0];
        positive ssid_length = string_length(ssid);
        p64 started;

        if (!ssid_length || ssid_length > RADIO_SSID_MOST)
                return -22;

        if (secured && !wifi_psk((p8 address_to)ssid, ssid_length,
                                 (p8 address_to)pass, string_length(pass), pmk))
                return -22;

        started = system_clock_ns(HOST_CLOCK_BOOTTIME);
        for (;;)
        {
                failed = nl80211_join((p8 address_to)ssid, ssid_length,
                                      secured ? pmk : null, address_of radio_joined);
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

        crypto_forget(pmk, sizeof(pmk));
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

        if (!say && nl80211_associated())
        {
                radio_net_wake();
                crypto_forget(networks, sizeof(networks));
                return 0;
        }

        for (at = 0; at < count; at++)
        {
                failed = radio_wifi_join((string_address)networks[at].ssid,
                                         (string_address)networks[at].pass);
                radio_last_set((string_address)networks[at].ssid, failed);
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
                           : host_refuse("no wireless interface%s\n", "");
        }
        if (say)
                return radio_join_said(failed)
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
        radio_keeper_start();
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
static bipolar radio_wifi_store(string_address ssid, string_address pass)
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

        if (cut)
                failed = -EFBIG;
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

/* Everything of `wifi add` that happens with the radio lock held. */
static b32 radio_wifi_enter(string_address ssid, string_address pass)
{
        radio_air air;
        radio_heard address_to heard = null;
        positive pass_length = pass ? string_length(pass) : 0;
        bipolar stored = radio_wifi_store(ssid, pass);
        bipolar failed;
        p8 why[RADIO_WHY_ROOM];

        b32 switched;

        if (stored == -E2BIG)
                return host_refuse("too many saved networks%s\n", "");
        if (stored == -EFBIG)
                return host_refuse("%s is too long to change here\n", NET_WIFI_LIST);
        if (stored < 0)
                return host_fail(NET_WIFI_LIST, stored);

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
                if (!radio_security_joinable(heard->security))
                        return host_refuse("saved, but it asks for %s, which "
                                           "moonwater cannot join yet\n",
                                           radio_security_words[heard->security]);
                if (heard->security != RADIO_OPEN && !pass_length)
                        return host_refuse("saved with no password, but it "
                                           "asks for one%s\n",
                                           "");
        }

        /*      No radio at all is not one that is still arriving: the join's
                eight seconds of asking are for a card whose interface is on
                its way at boot. */
        failed = radio_has_interface() ? radio_wifi_join(ssid, pass) : -19;
        radio_last_set(ssid, failed);

        radio_net_wake();
        if (failed == -19)
                return radio_wifi_why(why, sizeof(why))
                           ? host_refuse("saved; wifi: %s\n", why)
                           : host_refuse("saved, but there is no "
                                         "wireless interface%s\n",
                                         "");
        if (failed == -113)
                return host_refuse("saved, but it is not in range%s\n", "");
        if (radio_join_said(failed))
        {
                if (failed == -110 &&
                    radio_air_take(address_of air, RADIO_AIR_CACHED) &&
                    air.count && !radio_air_find(address_of air, ssid))
                        return host_refuse("saved, but it is not in range%s\n", "");
                return host_refuse("saved, but the network %s\n",
                                   radio_join_words(failed));
        }
        if (failed < 0)
                return host_fail("wifi", failed);
        if (pass && pass[0])
                crypto_forget((address_any)pass, string_length(pass));

        host_say(log, host_label "wifi joined %s\n", ssid);
        return switched;
}

static b32 radio_wifi_add(string_address ssid, string_address pass)
{
        positive ssid_length = string_length(ssid);
        positive pass_length = pass ? string_length(pass) : 0;
        bipolar lock;
        b32 result;

        if (!ssid_length || ssid_length > RADIO_SSID_MOST)
                return host_refuse("that network name is empty or too long%s\n",
                                   "");
        if (pass_length > RADIO_PASS_MOST)
                return host_refuse("that password is too long%s\n", "");
        if (pass_length && (pass_length < 8 ||
                            (pass_length == 64 && !radio_hex_key(pass))))
                return host_refuse("a WPA password is 8 to 63 characters, "
                                   "or a key of 64 hex digits%s\n",
                                   "");
        if (!radio_text_shown(ssid) || (pass_length && !radio_text_shown(pass)))
                return host_refuse("that network name cannot be stored%s\n", "");

        /*      The radio lock from here, before the network is saved: the
                machine's own pass, finding it saved and nothing joined,
                joined it first while this scanned, and this then left that
                join to make its own. */
        lock = radio_lock(true);
        if (lock < 0)
                return host_fail("wifi", lock);
        result = radio_wifi_enter(ssid, pass);
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

        failed = radio_wifi_save(networks, count);
        crypto_forget(networks, sizeof(networks));
        if (failed < 0)
                return host_fail(NET_WIFI_LIST, failed);

        {
                p8 last[RADIO_SSID_MOST + 1];
                bipolar why;

                if (radio_last_get(last, sizeof(last), address_of why) &&
                    string_equals((string_address)last, ssid))
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
                return host_refuse("that network name is empty or too long%s\n",
                                   "");

        lock = radio_lock(true);
        if (lock < 0)
                return host_fail("wifi", lock);
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
        bipolar written = radio_write_word(NET_WIRED_POWER, on ? "on" : "off");

        if (written < 0)
                return host_fail(NET_WIRED_POWER, written);

        handle = netlink_open_groups(0);
        if (handle < 0)
                return host_fail("wired", handle);

        failed = netlink_wired_list((b32)handle, address_of wired);
        if (failed >= 0)
                for (positive at = 0; at < wired.count; at++)
                        if (((wired.link[at].flags & IFF_UP) != 0) != on)
                                (void)netlink_link_flag_up((b32)handle, wired.link[at].index, on);
        socket_close((b32)handle);
        radio_net_wake();

        if (failed < 0)
                return host_fail("wired", failed);
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
                        string_format(log, host_label "  %s: %s, %s\n", wired.link[at].name,
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
        bool off = radio_power(NET_WIFI_POWER) == 0;
        bool joined = false;
        p8 why[RADIO_WHY_ROOM];
        p8 last[RADIO_SSID_MOST + 1];
        bipolar failed = 0;
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

                last[0] = end;
                if (radio_last_get(last, sizeof(last), address_of failed))
                        heard = radio_air_find(address_of air, last);
                p8 name[RADIO_SSID_MOST * 4 + 1];

                radio_display(name, sizeof(name), last, string_length(last));
                if (heard && !radio_security_joinable(heard->security))
                        string_format(log, host_label "not joined: %s asks for %s, which "
                                                      "moonwater cannot join yet\n",
                                      (string_address)name,
                                      radio_security_words[heard->security]);
                else if (heard)
                        string_format(log, host_label "not joined: %s %s\n",
                                      (string_address)name, radio_join_words(failed));
                else
                {
                        bool near = false;

                        for (at = 0; at < count; at++)
                                near |= radio_air_find(address_of air,
                                                       networks[at].ssid) != null;
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

        if (!length || length > 128)
                return host_refuse("that bluetooth name is empty or too long%s\n",
                                   "");
        if (add && !radio_text_shown(identity))
                return host_refuse("that bluetooth name cannot be stored%s\n", "");

        lock = radio_lock(true);
        if (lock < 0)
                return host_fail("bluetooth", lock);
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
                return host_refuse("too many saved bluetooth devices%s\n", "");
        if (failed == 4)
                return host_refuse("%s is too long to change here\n", NET_BLUETOOTH_LIST);
        if (failed)
                return host_fail(NET_BLUETOOTH_LIST, failed);
        //      A name remembered is for a radio that is on to find it.
        failed = add ? radio_bluetooth_power(true, true) : 0;
        host_say(log, host_label "bluetooth %s %s\n", add ? "remembered" : "forgot", identity);
        return failed;
}

static b32 radio_bluetooth_status(void)
{
        p8 text[4096];
        bipolar got = host_read_state(NET_BLUETOOTH_LIST, text, sizeof(text));
        bool off = radio_power(NET_BLUETOOTH_POWER) == 0;
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

        if (!string_equals(which, "wired") && !string_equals(which, "wifi"))
                return host_usage();

        host_state_ready();
        failed = radio_write_word(NET_INTERNET_ROOT, which);
        if (failed < 0)
                return host_fail(NET_INTERNET_ROOT, failed);
        failed = radio_internet_run(which);
        if (failed < 0)
                return host_fail(NET_INTERNET_RUN, failed);
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
        The join the machine loop forks, and only that, reaped once it ends.
        A wait4 on -1 here took every child of the machine process with it:
        the NTP query's status (now on a pipe) and any job the machine
        script put in the background, whose wait then found no such child.
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

        radio_rfkill(RADIO_RFKILL_WLAN, false);
        /*      A saved network and no radio forked a join every pass that
                spent eight seconds finding no interface. The radio's arrival
                -- firmware put in place, a card rebound, a stick plugged in
                -- is what the next pass after it notices. */
        if (radio_child > 0 || !radio_has_interface() || nl80211_associated())
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

        radio_wifi_bring(false);
        radio_unlock(lock);
        radio_keeper_start();
        system_call_1(syscall(exit), 0);
}

static fn radio_restore(void)
{
        bipolar wifi = radio_power(NET_WIFI_POWER);
        bipolar bluetooth = radio_power(NET_BLUETOOTH_POWER);

        radio_internet_copy();

        if (wifi == 0)
                radio_wifi_off(false);
        else if (radio_wifi_wanted(wifi))
                radio_wifi_on(false);

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
        if (host_read_text(HOST_VERDICT, verdict, sizeof(verdict)) >= 0 &&
            host_starts(verdict, "ask "))
                return;

        radio_internet_copy();

        if (radio_wifi_wanted(radio_power(NET_WIFI_POWER)))
                radio_wifi_keep();
}

/* What a verb says it did, when it did not fail. */
static b32 radio_done(b32 failed, string_address what)
{
        if (!failed)
                host_say(log, host_label "%s\n", what);
        return failed;
}

static b32 host_radio(string_address address_to arguments, positive count)
{
        string_address verb = arguments[1];
        string_address word = count > 2 ? arguments[2] : null;
        bipolar power;
        bool mutate;

        if (string_equals(verb, "priority"))
        {
                if (count == 2)
                        return radio_internet_status();
                if (!string_equals(word, "internet"))
                        return host_usage();
                if (count == 3)
                        return radio_internet_status();
                if (count != 4)
                        return host_usage();
                host_need_root("moonwater");
                return radio_internet_set(arguments[3]);
        }

        mutate = count > 2;
        if (mutate && !bowl_is_root())
                return host_refuse("%s needs root\n", "moonwater");
        power = count == 3 ? host_onoff(word) : -1;

        if (string_equals(verb, "wired"))
        {
                if (count < 3)
                        return radio_wired_status();
                if (power >= 0)
                        return radio_wired_set(power);
                return host_usage();
        }

        if (string_equals(verb, "wifi"))
        {
                if (count < 3)
                        return radio_wifi_status();
                if (power > 0)
                        return radio_wifi_on(true);
                if (power == 0)
                        return radio_done(radio_wifi_off(true), "wifi off");
                if (string_equals(word, "add") && count >= 4 && count <= 5)
                {
                        p8 pass[256];
                        positive length;
                        b32 result;

                        /*      No password: asked for at a terminal, open
                                anywhere else, as it always was. "-": one
                                line of standard input. Either keeps it out
                                of argv, where ps shows it to everybody. */
                        if (count == 4 ||
                            string_equals(arguments[4], (string_address) "-"))
                        {
                                if (radio_password_read(pass, sizeof(pass), arguments[3],
                                                        count == 4) < 0)
                                {
                                        crypto_forget(pass, sizeof(pass));
                                        return host_refuse("nothing saved%s\n", "");
                                }
                        }
                        else
                        {
                                length = string_length(arguments[4]);
                                if (length >= sizeof(pass))
                                        return host_refuse("that password is too long%s\n",
                                                           "");
                                memory_copy(pass, arguments[4], length + 1);
                                crypto_forget(arguments[4], length);
                        }
                        result = radio_wifi_add(arguments[3], pass);
                        crypto_forget(pass, sizeof(pass));
                        radio_keeper_start();
                        return result;
                }
                if (string_equals(word, "remove") && count == 4)
                        return radio_wifi_remove(arguments[3]);
                return host_usage();
        }

        if (count < 3)
                return radio_bluetooth_status();
        if (power >= 0)
                return radio_done(radio_bluetooth_power(power, true),
                                  power ? "bluetooth on" : "bluetooth off");
        if ((string_equals(word, "add") || string_equals(word, "remove")) && count == 4)
                return radio_bluetooth_edit(arguments[3], string_equals(word, "add"));
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

static bool tune_path(p8 address_to into, positive room, string_address first,
                      string_address second, string_address third)
{
        return string_copy_bounded(into, first, room) < room &&
               string_append_bounded(into, second, room) < room &&
               string_append_bounded(into, third, room) < room;
}

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

        if (host_read_text(path, text, sizeof(text)) <= 0)
                return false;
        *value = string_digits_max(text, 18, address_of used);
        return used && !text[used];
}

static bool tune_word(string_address path, p8 address_to into, positive room)
{
        return host_read_text(path, into, room) > 0;
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
        bool wifi_off = radio_power(NET_WIFI_POWER) == 0;
        bool bluetooth_off = radio_power(NET_BLUETOOTH_POWER) == 0;
        p8 was[24];
        bool kept;
        bool on;
        b32 failed;
        bipolar sent;

        if (count == 2)
        {
                host_say(log, host_label "airplane %s\n",
                         wifi_off && bluetooth_off ? "on" : "off");
                return 0;
        }
        if (count != 3 || host_onoff(arguments[2]) < 0)
                return host_usage();
        host_need_root("moonwater airplane");
        on = host_onoff(arguments[2]) > 0;
        kept = tune_kept("airplane", was, sizeof(was));

        //      Type zero is every radio, the modem included.
        if (on)
        {
                //      Already in airplane mode is nothing to remember.
                failed = !wifi_off || !bluetooth_off
                             ? tune_remember("airplane", wifi_off ? "bluetooth"
                                                         : bluetooth_off ? "wifi" : "wifi bluetooth")
                             : 0;
                failed |= radio_wifi_off(true);
                failed |= radio_bluetooth_power(false, true);
                sent = radio_rfkill(0, true);
        }
        else
        {
                sent = radio_rfkill(0, false);
                failed = 0;
                if (!kept || tune_has(was, "wifi"))
                {
                        failed |= radio_switch(NET_WIFI_POWER, RADIO_RFKILL_WLAN, true, true);
                        (void)radio_wifi_on(false);
                }
                if (!kept || tune_has(was, "bluetooth"))
                        failed |= radio_bluetooth_power(true, true);
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
        positive percent;
        bool relative;
        bool lower;

        if (count > 3)
                return host_usage();
        tune_choice panel = {.rank = 0};

        host_each_entry(TUNE_SYS_BACKLIGHT, tune_backlight_visit, address_of panel);
        string_copy(name, panel.name);
        if (!panel.rank ||
            !tune_path(maximum_path, sizeof(maximum_path), TUNE_SYS_BACKLIGHT "/", (string_address)name,
                       "/max_brightness") ||
            !tune_path(current_path, sizeof(current_path), TUNE_SYS_BACKLIGHT "/", (string_address)name,
                       "/brightness") ||
            !tune_number(maximum_path, address_of maximum) || !maximum ||
            !tune_number(current_path, address_of current))
                return host_refuse("this machine has no backlight to set%s\n", "");

        if (count == 2)
        {
                host_say(log, host_label "brightness %p%% (%p of %p)\n",
                         (current * 100 + maximum / 2) / maximum, current, maximum);
                return 0;
        }
        if (!tune_percent(arguments[2], address_of relative, address_of lower,
                          address_of percent))
                return host_usage();
        host_need_root("moonwater brightness");

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
                if (failed < 0)
                        return host_fail("brightness", failed);
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
                bool limited = radio_sys_path(path, sizeof(path), directory, name,
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
        if (!tune_path(path, sizeof(path), TUNE_SYS_BATTERY "/", (string_address)name,
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

        if (!tune_battery(name, sizeof(name)))
                return host_refuse("this machine has no battery%s\n", "");

        if (count == 2)
        {
                have_limit = tune_path(path, sizeof(path), TUNE_SYS_BATTERY "/", (string_address)name,
                                       "/charge_control_end_threshold") &&
                             tune_number(path, address_of limit);
                if (tune_path(path, sizeof(path), TUNE_SYS_BATTERY "/", (string_address)name, "/capacity"))
                        (void)tune_number(path, address_of capacity);
                status[0] = end;
                if (tune_path(path, sizeof(path), TUNE_SYS_BATTERY "/", (string_address)name, "/status"))
                        (void)tune_word(path, status, sizeof(status));
                string_format(log, host_label "battery %p%%, %s", capacity, (string_address)status);
                if (have_limit)
                        string_format(log, "; charging stops at %p%%\n", limit);
                else
                        string_format(log, "; this battery has no charge limit\n");
                log_flush();
                return 0;
        }

        if (count == 4 && string_equals(arguments[2], "limit") &&
            (string_equals(arguments[3], "off") || byte_is_digit(arguments[3][0])))
        {
                positive number = 100;
                bipolar failed;

                host_need_root("moonwater charge");
                if (!string_equals(arguments[3], "off"))
                {
                        positive used;

                        number = string_digits_max(arguments[3], 4, address_of used);
                        if (arguments[3][used] || number < 20 || number > 100)
                                return host_refuse("a charge limit is 20 to 100 percent%s\n", "");
                }
                {
                        p8 text[8];

                        positive_into_string(text, number);
                        failed = tune_charge_apply((string_address)text);
                        if (failed < 0)
                                return failed == -ENODEV
                                           ? host_refuse("this machine has no battery%s\n", "")
                                           : failed == -ENOENT
                                                 ? host_refuse("this battery has no charge limit%s\n", "")
                                                 : host_fail("charge", failed);
                        //      A limit of a full charge is the default: nothing to bring back.
                        if (tune_remember("charge.limit", number == 100 ? (string_address)"" : (string_address)text))
                                return 1;
                }
                host_say(log, host_label "charging stops at %p%%\n", number);
                return 0;
        }
        return host_usage();
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
            !tune_path(policy, sizeof(policy), directory, "/", name) ||
            string_append_bounded(policy, "/cpufreq/", sizeof(policy)) >= sizeof(policy))
                return true;
        if (profile->governor && tune_path(path, sizeof(path), (string_address)policy,
                                           "scaling_available_governors", "") &&
            tune_lists(path, profile->governor) &&
            tune_path(path, sizeof(path), (string_address)policy, "scaling_governor", ""))
                profile->written += tune_write(path, profile->governor) >= 0;
        if (profile->preference && tune_path(path, sizeof(path), (string_address)policy,
                                             "energy_performance_available_preferences", "") &&
            tune_lists(path, profile->preference) &&
            tune_path(path, sizeof(path), (string_address)policy,
                      "energy_performance_preference", ""))
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
                        return host_refuse("this machine has no power profile to set%s\n", "");
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
        host_need_root("moonwater power");
        if (!tune_power_apply(arguments[2]))
                return host_refuse("this machine has no power profile to set%s\n", "");
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
                *(positive address_to)context += !tune_path(path, sizeof(path), directory, "/", name) ||
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

                host_need_root("moonwater cpu");
                failed = boost ? tune_cpu_boost(on) : tune_cpu_smt(on);
                if (failed == -ENOENT)
                        return host_refuse(boost ? "this machine has no boost switch%s\n"
                                                 : "this machine has no SMT switch%s\n", "");
                if (failed < 0)
                        return host_fail(boost ? "cpu boost" : "cpu smt", failed);
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

                host_need_root("moonwater cpu");
                number = string_digits_max(arguments[3], 5, address_of used);
                if (arguments[3][used])
                        return host_refuse("a cpu is a number of at most five digits%s\n", "");
                if (!number)
                        return host_refuse("cpu 0 stays%s\n", "");
                bipolar failed;

                positive_into_string(text, number);
                failed = tune_path(path, sizeof(path), TUNE_SYS_CPU "/cpu", (string_address)text, "/online")
                             ? tune_write(path, string_equals(arguments[2], "online") ? "1" : "0")
                             : -ENOENT;
                if (failed == -ENOENT)
                        return host_refuse("that cpu cannot be switched%s\n", "");
                if (failed < 0)
                        return host_fail("cpu", failed);
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

                if (!tune_path(tail, sizeof(tail), "/", name, ":1.") ||
                    string_append_bounded(tail, (string_address)number, sizeof(tail)) >= sizeof(tail) ||
                    !tune_path(path, sizeof(path), bus, (string_address)tail, "/bInterfaceClass") ||
                    !tune_word(path, word, sizeof(word)) || !string_equals((string_address)word, "03") ||
                    !tune_path(path, sizeof(path), bus, (string_address)tail, "/bInterfaceProtocol") ||
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
        if (!tune_path(path, sizeof(path), directory, "/", name) ||
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
        if (!radio_sys_path(path, sizeof(path), directory, name, "/event_count") ||
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
                if (!radio_sys_path(path, sizeof(path), TUNE_WAKE_CLASS, after.source[at].name, "/name") ||
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

                if (!tune_path(path, sizeof(path), TUNE_SYS_POWER "/suspend_stats/", names[at], "") ||
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
        if (!radio_sys_path(path, sizeof(path), directory, name, "/brightness") ||
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

                if (radio_sys_path(path, sizeof(path), TUNE_SYS_BACKLIGHT, name, "/bl_power") &&
                    tune_number(path, address_of blank) && blank &&
                    tune_write(path, "0") >= 0)
                        host_say(log, host_label "backlight %s was blanked, unblanked\n", name);
                if (lights->light[at].level == TUNE_NO_LEVEL ||
                    !radio_sys_path(path, sizeof(path), TUNE_SYS_BACKLIGHT, name, "/brightness"))
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
                                       ? "this kernel does not offer sleep%s\n"
                                       : "this kernel does not offer hibernate%s\n", "");
        host_need_root(string_equals(state, "mem") ? "moonwater sleep" : "moonwater hibernate");
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
            radio_power(NET_WIFI_POWER) == 0 && radio_power(NET_BLUETOOTH_POWER) == 0)
                (void)radio_rfkill(0, true);
}

static b32 host_tune(string_address address_to arguments, positive count)
{
        string_address verb = arguments[1];

        if (string_equals(verb, "airplane"))
                return tune_airplane(arguments, count);
        if (string_equals(verb, "brightness"))
                return tune_brightness(arguments, count);
        if (string_equals(verb, "charge"))
                return tune_charge(arguments, count);
        if (string_equals(verb, "power"))
                return tune_power(arguments, count);
        if (string_equals(verb, "cpu"))
                return tune_cpu(arguments, count);
        if (count != 2)
                return host_usage();
        if (string_equals(verb, "sleep"))
                return tune_suspend(verb, "mem");
        return tune_suspend(verb, "disk");
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

        NOTHING BELOW HAS BEEN SEEN TO SET A CLOCK

        Setting CLOCK_REALTIME needs a machine it is acceptable to
        disturb, and no machine in reach was one, so neither the step nor
        the slew has ever been run against a kernel that carried it out.
        What has been checked is what gets asked for: every ADJ_ and STA_
        value here against uapi/linux/timex.h, every timex word index
        against the struct's own offsets, and the choice between stepping
        and slewing against crafted offsets either side of the threshold
        in the machine lane. Take that as the request being right, not as
        the request having been granted.

        The reason to be careful about the difference is in the constants
        below. ADJ_SETOFFSET was 0x80, which is ADJ_TAI, for as long as
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
        clocks read but the date of this source: a clock set earlier than
        the code that set it is wrong by construction, which is systemd's
        TIME_EPOCH rule. Moving it forward with each release is what keeps
        an answer from taking the clock back to when a revoked or expired
        certificate was still good.

        SNTP_WALL_MOST ends that window at 2036-01-01, which is before the
        NTP era rolls over rather than because of it: sntp_load_stamp reads
        the era from the top of the seconds field and converts either one.
        Moving the window is therefore the single edit that date needs, and
        it is safe to move. The offset sum is two differences of four terms
        the window bounds, and carrying the window all the way to 2104, the
        end of era 1, leaves that sum at 8.47 of the 9.22 the type holds.

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
#define SNTP_DISCARD_MAX 64
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
#define SNTP_KISS_RATE 0x52415445u /* "RATE" */
#define SNTP_KISS_DENY 0x44454e59u /* "DENY" */
#define SNTP_KISS_RSTR 0x52535452u /* "RSTR" */
#define SNTP_DELAY_MOST_NS ((bipolar)2 * (bipolar)SNTP_NANOSECONDS)
#define SNTP_OFFSET_MOST_NS ((bipolar)24 * 3600 * (bipolar)SNTP_NANOSECONDS)
#define SNTP_OFFSET_SYNCED_NS ((bipolar)2 * (bipolar)SNTP_NANOSECONDS)
#define SNTP_WALL_LEAST 1788220800ll /* 2026-09-01 */
#define SNTP_WALL_MOST 2082758400ll  /* 2036-01-01 */
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
#define SNTP_TEST_NOW \
        ((bipolar)1800000000 * (bipolar)SNTP_NANOSECONDS)

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
        if (nanoseconds >= SNTP_NANOSECONDS)
                return -1;
        if (seconds > SNTP_TIMESPEC_SECONDS_MOST)
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
        if (t1 < 0 || t4 < t1 || t3 < t2)
                return false;
        if (!sntp_wall_ok(t2) || !sntp_wall_ok(t3))
                return false;
        if (delay_ns < 0 || delay_ns > SNTP_DELAY_MOST_NS)
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
        it is reached here by its own name and sntp_math_ok hands it
        buffers laid out by hand -- including ones whose length words lie.

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

//      A control message laid out by hand for sntp_math_ok: the header,
//      then a timespec where the payload starts.
static COLD fn sntp_test_message(p8 address_to at, positive size, b32 level,
                                 b32 type, p64 seconds, p64 nanoseconds)
{
        address_to(positive address_to)at = size;
        address_to(b32 address_to)(at + sizeof(positive)) = level;
        address_to(b32 address_to)(at + sizeof(positive) + 4) = type;
        address_to(p64 address_to)(at + SNTP_CONTROL_DATA) = seconds;
        address_to(p64 address_to)(at + SNTP_CONTROL_DATA + sizeof(p64)) =
            nanoseconds;
}

//      A stamp, as the wire has it, for a time before 2036.
static COLD fn sntp_test_stamp(p8 address_to at, bipolar ns)
{
        network_store_32(at, (p32)(ns / (bipolar)SNTP_NANOSECONDS + SNTP_UNIX));
        network_store_32(at + 4,
                         (p32)((p64)(ns % (bipolar)SNTP_NANOSECONDS) *
                               4294967296ull / SNTP_NANOSECONDS));
}

static COLD bool sntp_math_ok(void)
{
        bipolar offset = 0;
        bipolar delay = 0;
        bipolar target = 0;
        bipolar two_days = (bipolar)2 * 86400 * (bipolar)SNTP_NANOSECONDS;
        positive at;
        p8 request[SNTP_PACKET];
        p8 reply[SNTP_PACKET];
        positive words[12];
        p8 address_to control = (p8 address_to)words;
        p64 arrived[2];
        p32 sequence;
        static const struct
        {
                positive claimed; /* what the message says its length is */
                b32 level;
                b32 kind;
                positive held;    /* what the buffer actually holds */
                bool want;
        } control_case[] = {
            /* the message the kernel really sends */
            {SNTP_CONTROL_DATA + 16, SOL_SOCKET, SNTP_TIMESTAMPNS,
             SNTP_CONTROL_DATA + 16, true},
            /* some other control message, of which there are many */
            {SNTP_CONTROL_DATA + 16, SOL_SOCKET, SNTP_TIMESTAMPNS + 1,
             SNTP_CONTROL_DATA + 16, false},
            {SNTP_CONTROL_DATA + 16, 0, SNTP_TIMESTAMPNS,
             SNTP_CONTROL_DATA + 16, false},
            /* a length word smaller than the header it heads */
            {SNTP_CONTROL_DATA - 8, SOL_SOCKET, SNTP_TIMESTAMPNS,
             SNTP_CONTROL_DATA + 16, false},
            /* a length word reaching past the end of the buffer */
            {SNTP_CONTROL_DATA + 64, SOL_SOCKET, SNTP_TIMESTAMPNS,
             SNTP_CONTROL_DATA + 16, false},
            /* the right message with too little room for a timespec */
            {SNTP_CONTROL_DATA + 8, SOL_SOCKET, SNTP_TIMESTAMPNS,
             SNTP_CONTROL_DATA + 8, false},
            /* nothing at all, which is what a kernel without the option
               sends, and the case the caller falls back on */
            {SNTP_CONTROL_DATA + 16, SOL_SOCKET, SNTP_TIMESTAMPNS, 0, false},
        };
        static const struct
        {
                p8 first;
                p8 stratum;
                p32 root_delay;
                p32 root_dispersion;
                p32 reference_id;
                bool echoed;
                bipolar want;
        } reply_case[] = {
            /* a stratum 2 server answering the question we asked */
            {0x24, 2, 0, 0, 0, true, SNTP_OK},
            /* the same packet with the origin stamp not echoed: a forgery,
               and the one check standing between us and an off-path lie */
            {0x24, 2, 0, 0, 0, false, SNTP_NO_REPLY},
            /* mode 3 is a request, not a reply */
            {0x23, 2, 0, 0, 0, true, SNTP_BAD_SERVER},
            /* version 0 and version 7 are no version NTP has had */
            {0x04, 2, 0, 0, 0, true, SNTP_BAD_SERVER},
            {0x3c, 2, 0, 0, 0, true, SNTP_BAD_SERVER},
            /* stratum 0 carries a kiss code in the reference id */
            {0x24, 0, 0, 0, SNTP_KISS_RATE, true, SNTP_RATE_LIMITED},
            {0x24, 0, 0, 0, SNTP_KISS_DENY, true, SNTP_DENIED},
            {0x24, 0, 0, 0, SNTP_KISS_RSTR, true, SNTP_DENIED},
            {0x24, 0, 0, 0, 0, true, SNTP_BAD_SERVER},
            /* and a kiss that is none of the three tells us nothing */
            {0x24, 0, 0, 0, 0x58595a5au, true, SNTP_BAD_SERVER},
            /* RATE is still RATE when the alarm bit is set with it */
            {0xe4, 0, 0, 0, SNTP_KISS_RATE, true, SNTP_RATE_LIMITED},
            /* stratum 16 is unsynchronised, and the alarm says so too */
            {0x24, 16, 0, 0, 0, true, SNTP_BAD_SERVER},
            {0xe4, 2, 0, 0, 0, true, SNTP_BAD_SERVER},
            /* a second of root delay is the most we trust, and the sign
               bit of the fixed-point short is never legitimately set */
            {0x24, 2, SNTP_SHORT_SECOND, SNTP_SHORT_SECOND, 0, true, SNTP_OK},
            {0x24, 2, SNTP_SHORT_SECOND + 1, 0, 0, true, SNTP_BAD_SERVER},
            {0x24, 2, 0, SNTP_SHORT_SECOND + 1, 0, true, SNTP_BAD_SERVER},
            {0x24, 2, 0x80000000u, 0, 0, true, SNTP_BAD_SERVER},
            {0x24, 2, 0, 0x80000000u, 0, true, SNTP_BAD_SERVER},
        };
        sntp_sample row[5] = {
            {10000000, 20000000, 0, true}, {8000000, 80000000, 0, true},
            {2000000, 15000000, 0, true},  {4000000, 40000000, 0, true},
            {50000000, 200000000, 0, true},
        };
        static const bipolar delay_case[][6] = {
            {0, 1000000000, 1000000000, 2000000000, 0, 2000000000},
            {0, 1050000000, 1050000000, 2000000000, 50000000, 2000000000},
            {0, 100000000, 100000000, 1100000000, -450000000, 1100000000},
        };
        static const struct
        {
                p32 seconds;
                p32 fraction;
                bipolar want;
        } stamp_case[] = {
            /* era 0, the high bit set: 1968 through February 2036 */
            {SNTP_UNIX, 0, 0},
            {SNTP_UNIX + 1, 0, (bipolar)SNTP_NANOSECONDS},
            {SNTP_UNIX, 0x80000000u, 500000000},
            /* era 1, the high bit clear: February 2036 onward */
            {0, 0, (bipolar)2085978496 * (bipolar)SNTP_NANOSECONDS},
            {1, 0, (bipolar)2085978497 * (bipolar)SNTP_NANOSECONDS},
        };
        static const struct
        {
                bipolar t1;
                bipolar t2;
                bipolar t3;
                bipolar t4;
                bipolar off;
                bipolar del;
                bipolar most;
                bool want;
        } sane_case[] = {
            {SNTP_TEST_NOW, SNTP_TEST_NOW + 1000000000,
             SNTP_TEST_NOW + 1000000000, SNTP_TEST_NOW + 2000000000, 0,
             2000000000, SNTP_OFFSET_MOST_NS, true},
            {SNTP_TEST_NOW, SNTP_TEST_NOW + 1000000000,
             SNTP_TEST_NOW + 1000000000, SNTP_TEST_NOW + 2000000000, 0,
             2000000000, SNTP_OFFSET_SYNCED_NS, true},
            {SNTP_TEST_NOW, SNTP_TEST_NOW + 1000000000,
             SNTP_TEST_NOW + 1000000000, SNTP_TEST_NOW + 3000000001, 0,
             3000000001, SNTP_OFFSET_MOST_NS, false},
            {SNTP_TEST_NOW, SNTP_TEST_NOW + 1000000000,
             SNTP_TEST_NOW + 1000000000, SNTP_TEST_NOW + 500000000, 0,
             -500000000, SNTP_OFFSET_MOST_NS, false},
            {SNTP_TEST_NOW, SNTP_TEST_NOW + 3000000000,
             SNTP_TEST_NOW + 3000000000, SNTP_TEST_NOW + 50000000, 2975000000,
             50000000, SNTP_OFFSET_SYNCED_NS, false},
            {SNTP_TEST_NOW, SNTP_TEST_NOW + 2000000000,
             SNTP_TEST_NOW + 1000000000, SNTP_TEST_NOW + 50000000, 0, 50000000,
             SNTP_OFFSET_MOST_NS, false},
            /* a clock that is 2.5 s out is stepped by an unsynchronised
               clock and by a hand, and left to a synchronised one */
            {SNTP_TEST_NOW, SNTP_TEST_NOW + 2500000000,
             SNTP_TEST_NOW + 2500000000, SNTP_TEST_NOW + 2000000, 2499000000,
             2000000, SNTP_OFFSET_SYNCED_NS, false},
            {SNTP_TEST_NOW, SNTP_TEST_NOW + 2500000000,
             SNTP_TEST_NOW + 2500000000, SNTP_TEST_NOW + 2000000, 2499000000,
             2000000, SNTP_OFFSET_MOST_NS, true},
            /* and one 25 h out only by a hand, which the window bounds */
            {SNTP_TEST_NOW, SNTP_TEST_NOW + 90000000000000,
             SNTP_TEST_NOW + 90000000000000, SNTP_TEST_NOW + 2000000,
             89999999000000, 2000000, SNTP_OFFSET_MOST_NS, false},
            {SNTP_TEST_NOW, SNTP_TEST_NOW + 90000000000000,
             SNTP_TEST_NOW + 90000000000000, SNTP_TEST_NOW + 2000000,
             89999999000000, 2000000, SNTP_OFFSET_SYNCED_NS, false},
            {SNTP_TEST_NOW, SNTP_TEST_NOW + 90000000000000,
             SNTP_TEST_NOW + 90000000000000, SNTP_TEST_NOW + 2000000,
             89999999000000, 2000000, SNTP_OFFSET_ANY_NS, true},
        };

        for (at = 0; at < array_count(delay_case); at++)
        {
                sntp_offset_delay(delay_case[at][0], delay_case[at][1],
                                  delay_case[at][2], delay_case[at][3],
                                  address_of offset, address_of delay);
                if (offset != delay_case[at][4] || delay != delay_case[at][5])
                        return false;
        }

        for (at = 0; at < array_count(sane_case); at++)
                if (sntp_sample_sane(sane_case[at].t1, sane_case[at].t2,
                                     sane_case[at].t3, sane_case[at].t4,
                                     sane_case[at].off, sane_case[at].del,
                                     sane_case[at].most) != sane_case[at].want)
                        return false;

        sntp_offset_delay(0, SNTP_TEST_NOW + 1000000000,
                          SNTP_TEST_NOW + 1000000000, 100000000,
                          address_of offset, address_of delay);
        if (!sntp_sample_sane(0, SNTP_TEST_NOW + 1000000000,
                              SNTP_TEST_NOW + 1000000000, 100000000, offset,
                              delay, SNTP_OFFSET_MOST_NS) ||
            sntp_sample_sane(0, SNTP_TEST_NOW + 1000000000,
                             SNTP_TEST_NOW + 1000000000, 100000000, offset,
                             delay, SNTP_OFFSET_SYNCED_NS) ||
            !sntp_target_ok(0, offset, address_of target) ||
            target < SNTP_TEST_NOW || target > SNTP_TEST_NOW + 1000000000)
                return false;

        sntp_offset_delay(SNTP_TEST_NOW, SNTP_TEST_NOW + two_days,
                          SNTP_TEST_NOW + two_days,
                          SNTP_TEST_NOW + 50000000, address_of offset,
                          address_of delay);
        if (sntp_sample_sane(SNTP_TEST_NOW, SNTP_TEST_NOW + two_days,
                             SNTP_TEST_NOW + two_days,
                             SNTP_TEST_NOW + 50000000, offset, delay,
                             SNTP_OFFSET_MOST_NS))
                return false;

        if (!sntp_short_ok(0) || !sntp_short_ok(SNTP_SHORT_SECOND) ||
            sntp_short_ok(SNTP_SHORT_SECOND + 1) ||
            sntp_short_ok(0x80000000u))
                return false;

        /*
                The seconds bound is the one that has to hold exactly: a
                whole second short of it, with the largest fraction, is
                still a number, and one second past it is not.
        */
        if (sntp_timespec_ns(SNTP_TIMESPEC_SECONDS_MOST,
                             SNTP_NANOSECONDS - 1) < 0 ||
            sntp_timespec_ns(SNTP_TIMESPEC_SECONDS_MOST + 1, 0) >= 0 ||
            sntp_timespec_ns(0, SNTP_NANOSECONDS) >= 0)
                return false;

        if (!sntp_local_ok(0) || !sntp_local_ok(SNTP_WALL_MOST_NS) ||
            sntp_local_ok(SNTP_WALL_MOST_NS + 1) || sntp_local_ok(-1))
                return false;

        for (at = 0; at < array_count(stamp_case); at++)
        {
                p8 field[8];

                network_store_32(field, stamp_case[at].seconds);
                network_store_32(field + 4, stamp_case[at].fraction);
                if (sntp_load_stamp(field) != stamp_case[at].want)
                        return false;
        }

        memory_fill(request, 0, sizeof(request));
        request[0] = SNTP_LI_VN_MODE;
        network_store_32(request + 40, 0xc0ffee00u);
        network_store_32(request + 44, 0x0badf00du);
        for (at = 0; at < array_count(reply_case); at++)
        {
                memory_fill(reply, 0, sizeof(reply));
                reply[0] = reply_case[at].first;
                reply[1] = reply_case[at].stratum;
                network_store_32(reply + 4, reply_case[at].root_delay);
                network_store_32(reply + 8, reply_case[at].root_dispersion);
                network_store_32(reply + 12, reply_case[at].reference_id);
                if (reply_case[at].echoed)
                        memory_copy(reply + 24, request + 40, 8);
                if (sntp_reply_ok(reply, request) != reply_case[at].want)
                        return false;
        }

        /*
                The reference stamp: all zeros is a server that does not
                say, and is the same server for all that; one that says is
                held to the window and to what the server sent.
        */
        {
                static const struct
                {
                        bipolar reference; /* unix seconds; -1 for all zeros */
                        bipolar want;
                } reference_case[] = {
                    {-1, SNTP_OK},
                    {SNTP_TEST_NOW / (bipolar)SNTP_NANOSECONDS - 10, SNTP_OK},
                    {SNTP_TEST_NOW / (bipolar)SNTP_NANOSECONDS + 1, SNTP_OK},
                    {SNTP_TEST_NOW / (bipolar)SNTP_NANOSECONDS + 5,
                     SNTP_BAD_SERVER},
                    {SNTP_WALL_LEAST - 1, SNTP_BAD_SERVER},
                    {1, SNTP_BAD_SERVER},
                };
                sntp_sample taken;

                for (at = 0; at < array_count(reference_case); at++)
                {
                        memory_fill(reply, 0, sizeof(reply));
                        reply[0] = 0x24;
                        reply[1] = 2;
                        memory_copy(reply + 24, request + 40, 8);
                        sntp_test_stamp(reply + 32, SNTP_TEST_NOW + 1000000);
                        sntp_test_stamp(reply + 40, SNTP_TEST_NOW + 1000000);
                        if (reference_case[at].reference >= 0)
                                sntp_test_stamp(reply + 16,
                                                reference_case[at].reference *
                                                    (bipolar)SNTP_NANOSECONDS);
                        memory_zero(address_of taken, sizeof(taken));
                        if (sntp_reply_sample(reply, request, SNTP_TEST_NOW,
                                              SNTP_TEST_NOW + 2000000,
                                              SNTP_OFFSET_MOST_NS,
                                              address_of taken) !=
                            reference_case[at].want)
                                return false;
                }
        }

        /*
                A control buffer the kernel says it filled short is read
                for nothing, the error queue's own flag is not that, and
                the buffer takes the arrival stamp, the departure stamp and
                the error header with the address behind it at once.
        */
        if (sntp_control_held(0, 144) != 144 ||
            sntp_control_held(SNTP_MSG_CTRUNC, 144) ||
            sntp_control_held(SNTP_MSG_CTRUNC | SNTP_ERRQUEUE, 128) ||
            sntp_control_held(SNTP_ERRQUEUE, 96) != 96 ||
            SNTP_CONTROL_WORDS * sizeof(positive) < 32 + 64 + 48)
                return false;

        for (at = 0; at < array_count(control_case); at++)
        {
                memory_zero(words, sizeof(words));
                sntp_test_message(control, control_case[at].claimed,
                                  control_case[at].level, control_case[at].kind,
                                  1700000000ull, 250000000ull);
                arrived[0] = 0;
                arrived[1] = 0;
                if (sntp_control_stamp(control, control_case[at].held,
                                       SNTP_TIMESTAMPNS,
                                       arrived) != control_case[at].want)
                        return false;
                if (control_case[at].want &&
                    (arrived[0] != 1700000000ull || arrived[1] != 250000000ull))
                        return false;
        }

        /*
                The kernel puts its messages in the order it likes, so the
                one we want is not always first. A message of another kind
                in front of it must be stepped over, not stopped at.
        */
        memory_zero(words, sizeof(words));
        sntp_test_message(control, SNTP_CONTROL_DATA, SOL_SOCKET,
                          SNTP_TIMESTAMPNS + 7, 0, 0);
        sntp_test_message(control + SNTP_CONTROL_DATA, SNTP_CONTROL_DATA + 16,
                          SOL_SOCKET, SNTP_TIMESTAMPNS, 1700000001ull,
                          750000000ull);
        arrived[0] = 0;
        arrived[1] = 0;
        if (!sntp_control_stamp(control, 2 * SNTP_CONTROL_DATA + 16,
                                SNTP_TIMESTAMPNS, arrived) ||
            arrived[0] != 1700000001ull || arrived[1] != 750000000ull)
                return false;

        /*
                The departure stamp comes back under a different type and
                in a longer payload -- three timespecs, of which the
                software one is first -- so the walk has to take its two
                words from the front and let the rest alone, and has to
                tell the two types apart rather than taking whichever
                timestamp it meets first.
        */
        memory_zero(words, sizeof(words));
        sntp_test_message(control, SNTP_CONTROL_DATA + 48, SOL_SOCKET,
                          SNTP_TIMESTAMPING, 1700000002ull, 125000000ull);
        arrived[0] = 0;
        arrived[1] = 0;
        if (!sntp_control_stamp(control, SNTP_CONTROL_DATA + 48,
                                SNTP_TIMESTAMPING, arrived) ||
            arrived[0] != 1700000002ull || arrived[1] != 125000000ull)
                return false;
        if (sntp_control_stamp(control, SNTP_CONTROL_DATA + 48,
                               SNTP_TIMESTAMPNS, arrived))
                return false;

        /*
                The transmit stamp's number rides in the error header
                beside it, not in the stamp, and the same header says
                whether the entry is a timestamp at all. A real ICMP
                error carries a different origin and must not be read as
                a sequence number, or a refused port would start
                claiming to be the answer to a send.
        */
        memory_zero(words, sizeof(words));
        sntp_test_message(control, SNTP_CONTROL_DATA + SNTP_ERROR_BYTES,
                          SNTP_SOL_IP, SNTP_IP_RECVERR, 0, 0);
        control[SNTP_CONTROL_DATA + SNTP_ERROR_ORIGIN] =
            SNTP_ERROR_TIMESTAMPING;
        address_to(p32 address_to)(control + SNTP_CONTROL_DATA +
                                   SNTP_ERROR_SEQUENCE) = 4u;
        sequence = 0;
        if (!sntp_control_sequence(control, SNTP_CONTROL_DATA +
                                                SNTP_ERROR_BYTES,
                                   address_of sequence) ||
            sequence != 4u)
                return false;

        /* the same entry as an ICMP error rather than a timestamp */
        control[SNTP_CONTROL_DATA + SNTP_ERROR_ORIGIN] = 2; /* ICMP */
        sequence = 0;
        if (sntp_control_sequence(control, SNTP_CONTROL_DATA +
                                               SNTP_ERROR_BYTES,
                                  address_of sequence))
                return false;

        /* a header cut short of the field the number sits in */
        control[SNTP_CONTROL_DATA + SNTP_ERROR_ORIGIN] =
            SNTP_ERROR_TIMESTAMPING;
        address_to(positive address_to)control = SNTP_CONTROL_DATA + 8;
        sequence = 0;
        if (sntp_control_sequence(control, SNTP_CONTROL_DATA + 8,
                                  address_of sequence))
                return false;

        /* and no error header at all, which is the fallback case */
        memory_zero(words, sizeof(words));
        sntp_test_message(control, SNTP_CONTROL_DATA + 48, SOL_SOCKET,
                          SNTP_TIMESTAMPING, 0, 0);
        sequence = 0;
        if (sntp_control_sequence(control, SNTP_CONTROL_DATA + 48,
                                  address_of sequence))
                return false;

        /*
                One bit of the echoed stamp flipped is still a forgery.
        */
        memory_fill(reply, 0, sizeof(reply));
        reply[0] = 0x24;
        reply[1] = 2;
        memory_copy(reply + 24, request + 40, 8);
        /* Every bit of the full random nonce is reply identity. A parser
           that compares only the old random low half recreates the weak
           predictable-clock authenticator this test is meant to prevent. */
        for (positive byte = 0; byte < 8; byte++)
                for (p8 bit = 1; bit; bit <<= 1)
                {
                        reply[24 + byte] ^= bit;
                        if (sntp_reply_ok(reply, request) != SNTP_NO_REPLY)
                                return false;
                        reply[24 + byte] ^= bit;
                }
        {
                p8 later[SNTP_PACKET];

                memory_copy(later, request, sizeof later);
                later[40] ^= 1;
                /* A valid reply to the preceding exchange is a replay, not a
                   reply to this one, even when every server field is valid. */
                if (sntp_reply_ok(reply, later) != SNTP_NO_REPLY)
                        return false;
        }

        /*
                The root distance: half of a 20 ms round trip, half of a
                half-second root delay and a 1/64 s root dispersion.
        */
        if (sntp_distance_ns(20000000, 0x8000u, 0x400u) !=
            10000000 + 250000000 + 15625000)
                return false;

        /*
                Choosing among servers. The one with the least distance is a
                falseticker when neither other interval meets its own, and
                the next best is believed; two that disagree, or three that
                all do, cannot name the wrong one, so the least distance
                stands; and one answer is the answer.
        */
        {
                sntp_sample three[SNTP_SERVERS] = {
                    {0, 0, 5000000, true},
                    {1000000, 0, 2000000, true},
                    {50000000, 0, 1000000, true},
                };
                sntp_sample apart[SNTP_SERVERS] = {
                    {0, 0, 1000000, true},
                    {10000000, 0, 2000000, true},
                    {20000000, 0, 500000, true},
                };

                /* an answer 2 h out claiming half a millisecond, against
                   an honest 10 ms one: its own word is not enough, alone
                   or against that one, and two that agree are */
                sntp_sample far[2] = {
                    {(bipolar)7200 * 1000000000, 0, 500000, true},
                    {0, 0, 10000000, true},
                };

                for (positive server = 0; server < SNTP_SERVERS; server++)
                {
                        three[server].address = (p32)(server + 1);
                        apart[server].address = (p32)(server + 1);
                }
                far[0].address = 1;
                far[1].address = 2;
                if (sntp_choose(three, 3) != 1 || sntp_choose(three + 1, 2) != 1 ||
                    sntp_choose(three + 2, 1) != 0 || sntp_choose(apart, 3) != 2)
                        return false;
                three[1].ok = false;
                if (sntp_choose(three, 3) != 2)
                        return false;
                if (sntp_choose(far, 2) != 1 || sntp_choose(far, 1) != -1)
                        return false;
                far[1].offset_ns = far[0].offset_ns + 1000000;
                if (sntp_choose(far, 2) != 0)
                        return false;
                /* one server under three names is one answer: three that agree
                   from one address are not a majority, a second server that
                   agrees with them is, and a step within 2 s needs no word */
                sntp_sample same[3] = {
                    {(bipolar)7200 * 1000000000, 0, 500000, true, 9},
                    {(bipolar)7200 * 1000000000, 0, 600000, true, 9},
                    {(bipolar)7200 * 1000000000, 0, 700000, true, 9},
                };

                if (sntp_choose(same, 3) != -1)
                        return false;
                same[2].address = 10;
                if (sntp_choose(same, 3) != 0)
                        return false;
                same[0].offset_ns = same[1].offset_ns = same[2].offset_ns = 1000000;
                same[2].address = 9;
                if (sntp_choose(same, 3) != 0)
                        return false;
        }

        return sntp_pick(row, 5) == 2 && row[2].offset_ns == 2000000;
}

static HOT bipolar sntp_exchange(b32 handle,
                                 network_deadline address_to deadline,
                                 bipolar most, p32 address_to sequence,
                                 sntp_sample address_to into)
{
        p8 request[SNTP_PACKET];
        p8 reply[SNTP_REPLY_ROOM];
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
        memory_fill(request, 0, sizeof(request));
        request[0] = SNTP_LI_VN_MODE;

        if_rare (system_call_2(syscall(clock_gettime), CLOCK_REALTIME,
                               (positive)sent) < 0)
                return SNTP_NO_REPLY;
        if_rare (!sntp_put_stamp(request + 40))
                return SNTP_NO_REPLY;
        if_rare (socket_send(handle, request, SNTP_PACKET, 0, 0, 0) < 0)
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
                                                sizeof(reply), got,
                                                address_of stamped);
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
                        if (discarded++ == SNTP_DISCARD_MAX)
                                return SNTP_NO_REPLY;
                        continue;
                }
                if_rare (received < SNTP_PACKET || (MOONWATER_STRICT >= STRICT_TIGHT && received != SNTP_PACKET))
                {
                        if (discarded++ == SNTP_DISCARD_MAX)
                                return SNTP_NO_REPLY;
                        continue;
                }
                verdict = sntp_reply_sample(reply, request, t1,
                                            sntp_timespec_ns(got[0], got[1]),
                                            most, into);
                if_rare (verdict == SNTP_NO_REPLY)
                {
                        if (discarded++ == SNTP_DISCARD_MAX)
                                return SNTP_NO_REPLY;
                        continue;
                }
                return verdict;
        }
}

static COLD bipolar sntp_query_at(p32 server, bool filter, bipolar most,
                                  sntp_sample address_to answer)
{
        socket_address_internet where = {
            .family = AF_INET,
            .port = network_order_16(SNTP_PORT),
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
        if (socket_connect((b32)handle, address_of where, sizeof(where)) < 0)
        {
                socket_close((b32)handle);
                return SNTP_NO_SERVER;
        }
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
                                       address_of sequence, row + at);
                //      A server that is not there, or has said no, is not
                //      asked again for the rest of the samples.
                if (failed == SNTP_NO_SERVER || failed == SNTP_BAD_SERVER ||
                    failed == SNTP_RATE_LIMITED || failed == SNTP_DENIED)
                        break;
        }
        socket_close((b32)handle);

        best = sntp_pick(row, want);
        if (best < 0)
                return failed < 0 ? failed : SNTP_NO_REPLY;
        address_to answer = row[best];
        answer->address = server;
        return SNTP_OK;
}

static COLD bipolar sntp_query(string_address name, bool filter, bipolar most,
                               sntp_sample address_to answer)
{
        bipolar numeric;
        p32 host = 0;
        bipolar found;

        if (!name || !name[0])
                return SNTP_NO_SERVER;
        numeric = string_to_host(name);
        if (numeric >= 0)
                return sntp_query_at((p32)numeric, filter, most, answer);

        found = dns_resolve_any((string_address) "/etc/resolv.conf", name,
                                address_of host, SNTP_SECONDS);
        if (found != DNS_OK)
                return SNTP_NO_SERVER;
        return sntp_query_at(host, filter, most, answer);
}

#endif


#define LOCALE_ZONE_MODE_PATH "/root/timezone.mode"
#define LOCALE_ZONE_NETWORK_PATH HOST_STATE "/timezone.network"
#define LOCALE_NTP_PATH "/root/ntp"
#define LOCALE_NTP_SERVER_PATH "/root/ntp.server"
#define LOCALE_NTP_SAMPLING_PATH "/root/ntp.sampling"
#define LOCALE_KEYBOARD_PATH "/root/keyboard"
#define LOCALE_NTP_DEFAULT_SERVER "pool.ntp.org"
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
        bipolar pid;

        if (system_pipe(ends, O_CLOEXEC | O_NONBLOCK) < 0)
                return -1;
        pid = system_fork();
        if (pid < 0)
        {
                system_close(ends[0]);
                system_close(ends[1]);
                return pid;
        }
        if (!pid)
        {
                positive keep = (positive)ends[1];

                system_close(ends[0]);
                child->pid = 0;
                child->answer = ends[1];
                //      Nothing of the machine process stays open in it. Its
                //      descriptor on /dev/spark holds the machine's
                //      attachment until the last copy closes, and a machine
                //      restarted during a query found it taken.
                if (keep > 3)
                        system_call_3(syscall(close_range), 3, keep - 1, 0);
                system_call_3(syscall(close_range), keep < 3 ? 3 : keep + 1,
                              ~(p32)0, 0);
                return 0;
        }
        system_close(ends[1]);
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
        positive status = 0;

        if (child->pid <= 0)
                return;
        system_call_2(syscall(kill), (positive)child->pid, SIGKILL);
        system_close(child->answer);
        (void)system_wait4_retry(child->pid, address_of status, 0, null);
        child->pid = 0;
        child->answer = -1;
}

//      LOCALE_CHILD_IDLE with none running, LOCALE_CHILD_RUNNING while it
//      runs, then the byte it ended with -- once.
static bipolar locale_child_poll(locale_child address_to child)
{
        p8 code = 1;
        positive status = 0;
        bipolar got;

        if (child->pid <= 0)
                return LOCALE_CHILD_IDLE;
        got = system_read_once(child->answer, address_of code, 1);
        if (got == -EAGAIN || got == -EINTR)
                return LOCALE_CHILD_RUNNING;
        //      The byte or the end of file says it is done; any other
        //      answer of read says nothing, and is not waited for.
        if (got < 0)
                system_call_2(syscall(kill), (positive)child->pid, SIGKILL);
        system_close(child->answer);
        (void)system_wait4_retry(child->pid, address_of status, 0, null);
        child->pid = 0;
        child->answer = -1;
        return got == 1 ? code : 1;
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

static fn locale_ntp_keep(bool online);
static bipolar locale_ntp_apply(bool by_hand);

//      What the last answer moved the clock by, and who gave it, for the
//      person who asked by hand. The scheduled queries run in a child and
//      nobody reads these there.
static bipolar locale_ntp_moved_ns;
static p8 locale_ntp_answered[80];

//      A word from /root, read as state is: a FIFO or a link planted at the
//      name on the data partition is no word, and cannot stop the command.
static fn locale_word(string_address path, p8 address_to into, positive room)
{
        bipolar got = host_read_state(path, into, room);
        positive length;

        if (got < 0)
        {
                into[0] = end;
                return;
        }
        length = string_length(into);
        while (length && (into[length - 1] == '\n' || into[length - 1] == '\r'))
                into[--length] = end;
}

static bool locale_switch_on(string_address path)
{
        p8 word[16];

        locale_word(path, word, sizeof(word));
        if (!word[0])
                return true;
        return string_equals(word, "on");
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

        locale_word(LOCALE_ZONE_MODE_PATH, into, room);
        if (into[0])
                return;
        locale_word(CLOCK_ZONE_PATH, zone, sizeof(zone));
        if (zone[0])
                string_copy_bounded(into, "manual", room);
}

static bool locale_zone_manual(void)
{
        p8 mode[48];

        locale_zone_mode(mode, sizeof(mode));
        return host_starts(mode, "manual");
}

static fn locale_zone_how(p8 address_to into, positive room)
{
        p8 mode[48];

        locale_zone_mode(mode, sizeof(mode));
        if (host_starts(mode, "manual"))
                string_copy_bounded(into, "(manual)", room);
        else if (host_starts(mode, "auto cloudflare"))
                string_copy_bounded(into, "(auto, from Cloudflare)", room);
        else if (host_starts(mode, "auto country ") && mode[13])
        {
                string_copy_bounded(into, "(auto, from country ", room);
                string_append_bounded(into, mode + 13, room);
                string_append_bounded(into, ")", room);
        }
        else
                string_copy_bounded(into, "(auto, waiting for the network)",
                                    room);
}

static b32 locale_zone_status(void)
{
        p8 zone[80];
        p8 title[128];
        p8 how[48];
        p8 when[48];

        locale_word(CLOCK_ZONE_PATH, zone, sizeof(zone));
        locale_zone_title(zone, title, sizeof(title));
        locale_zone_how(how, sizeof(how));
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
        bipolar failed;

        if (lock < 0)
                return lock;
        if (!by_hand && locale_zone_manual())
                failed = -EBUSY;
        else if (host_starts(mode, "manual"))
        {
                failed = radio_write_word(LOCALE_ZONE_MODE_PATH, mode);
                if (failed >= 0)
                        failed = radio_write_word(CLOCK_ZONE_PATH, zone);
        }
        else
        {
                failed = radio_write_word(CLOCK_ZONE_PATH, zone);
                if (failed >= 0)
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

        locale_word(CLOCK_ZONE_PATH, was, sizeof(was));
        locale_zone_describe(was, old_shown, sizeof(old_shown));
        locale_zone_moment(before, sizeof(before));

        failed = locale_zone_write(zone, mode, true);
        if (failed < 0)
                return host_fail("timezone", failed);

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
#define LOCALE_AUTO_HOST "speed.cloudflare.com"
#define LOCALE_AUTO_PATH "/__down?bytes=0"
#define LOCALE_AUTO_TRACE_HOST "cloudflare.com"
#define LOCALE_AUTO_TRACE_PATH "/cdn-cgi/trace"
#define LOCALE_AUTO_SECONDS 10
#define LOCALE_AUTO_LEAST 180
#define LOCALE_AUTO_MOST 3600
#define LOCALE_AUTO_CLOCK_WAIT 60
#define LOCALE_AUTO_ROOM 4096
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
        got = host_read_text("/proc/net/route", table, sizeof(table));
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
                got = host_read_text("/proc/net/arp", table, sizeof(table));
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

static bipolar locale_auto_get(string_address host, string_address path,
                               p8 address_to into, positive room,
                               positive address_to used,
                               positive address_to header)
{
        http_link link;
        http_response response;
        network_deadline deadline;
        p32 ip = http_lookup(host);
        bipolar status;

        address_to used = 0;
        address_to header = 0;
        into[0] = end;
        if (!ip)
                return HTTP_NO_HOST;
        status = http_link_open(address_of link, ip, HTTP_HTTPS_PORT, host,
                                true, true);
        if (status)
                return status;
        status = http_send_get(address_of link, host, HTTP_HTTPS_PORT, path,
                               true, '1', (string_address) "Moonwater");
        if (!status)
                status = http_response_head(address_of link, into, room - 1,
                                            used, header, address_of response,
                                            LOCALE_AUTO_SECONDS, 0, false);
        if (!status && !http_response_is_success(response.code))
                status = HTTP_STATUS;
        //      A small body, read to its length or the server's close.
        if (!status &&
            network_deadline_begin(address_of deadline, LOCALE_AUTO_SECONDS, 0))
                while (address_to used + 1 < room &&
                       (response.body_kind != HTTP_BODY_LENGTH ||
                        address_to used - address_to header < response.body_length))
                {
                        positive got = 0;

                        if (http_link_read_until(address_of link,
                                                 into + address_to used,
                                                 room - 1 - address_to used,
                                                 address_of got,
                                                 address_of deadline) ||
                            !got)
                                break;
                        address_to used += got;
                }
        http_link_close(address_of link);
        into[address_to used] = end;
        return status;
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
        p8 reply[LOCALE_AUTO_ROOM];
        positive used = 0;
        positive header = 0;
        bipolar status;

        memory_zero(answer, sizeof(address_to answer));
        status = locale_auto_get((string_address)LOCALE_AUTO_HOST,
                                 (string_address)LOCALE_AUTO_PATH, reply,
                                 sizeof(reply), address_of used,
                                 address_of header);
        if (!status && locale_auto_from_headers(reply, header, answer))
                return 0;
        status = locale_auto_get((string_address)LOCALE_AUTO_TRACE_HOST,
                                 (string_address)LOCALE_AUTO_TRACE_PATH, reply,
                                 sizeof(reply), address_of used,
                                 address_of header);
        if (status)
                return status;
        return locale_auto_from_trace(reply + header, used - header, answer)
                   ? 0
                   : HTTP_MALFORMED;
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
                return (string_address) "TLS failed -- is the clock right?";
        case HTTP_STATUS:
                return (string_address) "it answered with an error";
        case HTTP_MALFORMED:
                return (string_address) "the answer named no zone";
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

        locale_word(CLOCK_ZONE_PATH, was, sizeof(was));
        locale_word(LOCALE_ZONE_MODE_PATH, mode, sizeof(mode));
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

        if (locale_zone_manual() &&
            radio_write_word(LOCALE_ZONE_MODE_PATH, "auto") < 0)
                return host_fail("timezone", -1);
        locale_network(network, sizeof(network));
        status = locale_auto_ask(address_of answer);
        if (status)
        {
                locale_auto_asked(null);
                locale_word(CLOCK_ZONE_PATH, zone, sizeof(zone));
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

static fn locale_auto_keep(bool ntp_wanted, string_address network)
{
        p64 now = system_clock_ns(HOST_CLOCK_BOOTTIME);
        bipolar ended = locale_child_poll(address_of locale_auto_child);
        p8 asked[LOCALE_NETWORK_ROOM];

        if (ended == LOCALE_CHILD_RUNNING)
                return;
        if (ended != LOCALE_CHILD_IDLE)
        {
                locale_auto_next = now + (p64)locale_auto_wait * 1000000000ull;
                locale_auto_wait = ended ? (locale_auto_wait * 2 > LOCALE_AUTO_MOST
                                                ? LOCALE_AUTO_MOST
                                                : locale_auto_wait * 2)
                                         : LOCALE_AUTO_LEAST;
                if (!ended)
                        locale_auto_next = now + (p64)LOCALE_AUTO_LEAST *
                                                     1000000000ull;
                return;
        }
        if (locale_zone_manual())
                return;
        //      A certificate is only as good as the clock that checks it:
        //      while NTP is on and has not set it, wait a minute for it.
        if (ntp_wanted && !locale_clock_synced() &&
            now < (p64)LOCALE_AUTO_CLOCK_WAIT * 1000000000ull)
                return;
        if (!network[0])
                return;
        locale_word(LOCALE_ZONE_NETWORK_PATH, asked, sizeof(asked));
        if (locale_network_same(network, asked))
        {
                //      The gateway's address arrived after the answer did.
                if (string_length(network) > string_length(asked))
                        locale_auto_asked(network);
                return;
        }
        if (locale_auto_next && now < locale_auto_next)
                return;

        if (!locale_child_fork(address_of locale_auto_child))
        {
                locale_auto_answer answer;
                bool took = !locale_auto_ask(address_of answer) &&
                            !locale_auto_take(address_of answer, false);

                if (took)
                        locale_auto_asked(network);
                locale_child_end(address_of locale_auto_child, took ? 0 : 1);
        }
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
        host_say(log, "  ntp %s, %s\n",
                 locale_ntp_wanted() ? "on" : "off",
                 locale_clock_synced() ? "synchronised" : "waiting");
        return 0;
}

static b32 locale_time_sync(void)
{
        bipolar failed;

        locale_ntp_moved_ns = 0;
        locale_ntp_answered[0] = end;
        failed = locale_ntp_apply(true);
        if (failed < 0 && locale_ntp_answered[0])
                string_format(log, host_label "time: %s answered, but the "
                                   "clock could not be set: %s\n",
                              locale_ntp_answered,
                              failed == SNTP_MALFORMED
                                  ? (string_address) "the time it gave is "
                                                     "outside the years this "
                                                     "machine accepts"
                                  : file_reason(failed));
        else if (failed == SNTP_UNCONFIRMED)
                string_format(log, host_label "time: no second server agreed "
                                   "with a step past 2 s, so the clock was "
                                   "left alone\n");
        else if (failed < 0)
                string_format(log, host_label "time: no server answered%s\n",
                              failed == SNTP_RATE_LIMITED
                                  ? " -- asked too often, try in a minute"
                              : failed == SNTP_DENIED
                                  ? " -- a server told this machine to stay away"
                                  : "");
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

/*
        Nothing unprivileged can ask the kernel which mode a bit means,
        and a wrong one returns success, so the decision is checked here
        instead: what goes in the request for a given offset, rather than
        what the kernel does with it.
*/
static COLD bool locale_discipline_ok(void)
{
        positive words[LOGGER_TIMEX_WORDS];
        positive at;
        static const struct
        {
                bipolar offset_ns;
                bool step;
                bipolar sec; /* what a step carries, floored */
                bipolar nsec;
        } discipline_case[] = {
            {0, false, 0, 0},
            {1000000, false, 0, 0},
            {-1000000, false, 0, 0},
            {LOCALE_NTP_STEP_NS - 1, false, 0, 0},
            {-(LOCALE_NTP_STEP_NS - 1), false, 0, 0},
            {LOCALE_NTP_STEP_NS, true, 0, LOCALE_NTP_STEP_NS},
            {-LOCALE_NTP_STEP_NS, true, -1,
             (bipolar)SNTP_NANOSECONDS - LOCALE_NTP_STEP_NS},
            {1500000000, true, 1, 500000000},
            {-1500000000, true, -2, 500000000},
            {(bipolar)86400 * 1000000000, true, 86400, 0},
            {-(bipolar)86400 * 1000000000, true, -86400, 0},
            {(bipolar)3 * 86400 * 1000000000 + 1, true, 259200, 1},
            {-((bipolar)3 * 86400 * 1000000000 + 1), true, -259201,
             (bipolar)SNTP_NANOSECONDS - 1},
        };

        for (at = 0; at < array_count(discipline_case); at++)
        {
                bipolar offset = discipline_case[at].offset_ns;

                /* a 20 ms round trip to a server at the root of its
                   tree is a 10 ms root distance */
                locale_ntp_discipline_words(offset,
                                            locale_ntp_error_us(
                                                sntp_distance_ns(20000000, 0, 0)),
                                            words);

                if (locale_ntp_wants_step(offset) != discipline_case[at].step)
                        return false;
                /* the loop is enabled either way, and the clock counts as
                   set either way, so STA_UNSYNC never survives a reply */
                if (words[LOGGER_TIMEX_STATUS] != (STA_PLL | STA_FREQHOLD))
                        return false;
                if (words[0] & ADJ_STATUS ? false : true)
                        return false;
                /* and it carries an error bound, or the kernel takes the
                   synchronisation back at the next second */
                if (!(words[0] & ADJ_MAXERROR) || !(words[0] & ADJ_ESTERROR) ||
                    words[LOGGER_TIMEX_MAXERROR] != 10001 ||
                    words[LOCALE_TIMEX_ESTERROR] != 10001)
                        return false;
                if (discipline_case[at].step)
                {
                        /* a step carries the time, cancels any slew, and
                           has no business setting a loop time constant */
                        if (!(words[0] & ADJ_SETOFFSET) ||
                            words[0] & ADJ_TIMECONST ||
                            words[LOCALE_TIMEX_OFFSET] != 0 ||
                            (bipolar)words[LOGGER_TIMEX_TIME_SEC] !=
                                discipline_case[at].sec ||
                            (bipolar)words[LOGGER_TIMEX_TIME_NSEC] !=
                                discipline_case[at].nsec)
                                return false;
                }
                else
                {
                        /* a slew hands the offset to the loop and never
                           steps, so time does not go backwards */
                        if (words[0] & ADJ_SETOFFSET ||
                            !(words[0] & ADJ_TIMECONST) ||
                            (bipolar)words[LOCALE_TIMEX_OFFSET] != offset ||
                            words[LOCALE_TIMEX_CONSTANT] !=
                                LOCALE_NTP_TIMECONST ||
                            words[LOGGER_TIMEX_TIME_SEC] ||
                            words[LOGGER_TIMEX_TIME_NSEC])
                                return false;
                }
                if (!(words[0] & ADJ_NANO) || !(words[0] & ADJ_OFFSET))
                        return false;
        }
        /* the first answer after boot steps from a millisecond out, and a
           later one only from 128 ms */
        locale_ntp_first = true;
        if (!locale_ntp_wants_step(1000000) || !locale_ntp_wants_step(-1000000) ||
            locale_ntp_wants_step(999999) || locale_ntp_wants_step(-999999))
                return false;
        locale_ntp_first = false;
        if (locale_ntp_wants_step(1000000) || locale_ntp_wants_step(-86000000))
                return false;
        /* a poll learns offset over elapsed, trusted as far as the offset
           stands above the noise: 144 ms slow after half an hour is -80
           ppm, nearly all of it learned; 150 us after 256 s is under a
           quarter of its 0.58 ppm; held within the kernel's 500, and under
           a minute, or past 1000 ppm, learns nothing */
        if (locale_ntp_learned(-144000000, (bipolar)1800 * 1000000000, 0) !=
                -5224738 ||
            locale_ntp_learned(36000000, (bipolar)1800 * 1000000000,
                               (bipolar)7 << 16) != 1751516 ||
            locale_ntp_learned(150000, (bipolar)256 * 1000000000, 0) !=
                (bipolar)150000 * 65536 / 256000 * 150000 / 650000 ||
            locale_ntp_learned(-144000000, (bipolar)1800 * 1000000000,
                               -((bipolar)490 << 16)) != -LOCALE_NTP_FREQ_MOST ||
            locale_ntp_learned(-144000000, (bipolar)30 * 1000000000, 5) != 5 ||
            locale_ntp_learned(-5000000000, (bipolar)1800 * 1000000000, 5) != 5 ||
            locale_ntp_learned((bipolar)199 * 1000000000,
                               (bipolar)200000 * 1000000000, 0) !=
                LOCALE_NTP_FREQ_MOST)
                return false;
        /* a 10 ms distance is 10 ms, rounded up a microsecond; a negative
           or absurd one still gives a bound the kernel accepts */
        return locale_ntp_error_us((bipolar)10 * 1000000) == 10001 &&
               locale_ntp_error_us(0) == 1 && locale_ntp_error_us(-5) == 1 &&
               locale_ntp_error_us((bipolar)60 * 1000000000) ==
                   LOCALE_NTP_ERROR_MOST_US;
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

static bipolar locale_ntp_ask(string_address server, bool filter, bipolar most,
                              sntp_sample address_to answer)
{
        if (!server || !server[0] ||
            !radio_text_plain(server, string_length(server)))
                return SNTP_NO_SERVER;
        return sntp_query(server, filter, most, answer);
}

static bipolar locale_ntp_take(string_address server,
                               const sntp_sample address_to answer)
{
        locale_ntp_moved_ns = answer->offset_ns;
        string_copy_bounded(locale_ntp_answered, server,
                            sizeof(locale_ntp_answered));
        return locale_ntp_apply_offset(answer->offset_ns, answer->distance_ns);
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
        sntp_choose picks which to believe; a server named in
        /root/ntp.server is believed on its own, as before, and sampling off
        takes the first answer -- unless it would step the clock more than
        two seconds, when a second is asked for, since sntp_choose believes
        no such step on one word. A clock nobody has set takes the first
        answer there is, with sampling or not: it has no time to be moved
        from, and waiting for the others is a minute on a machine whose
        network is slow to come.
*/
static bipolar locale_ntp_walk(bool by_hand)
{
        p8 server[80];
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

        locale_word(LOCALE_NTP_SERVER_PATH, server, sizeof(server));
        if (server[0])
        {
                failed = locale_ntp_ask((string_address)server, filter, most,
                                        heard);
                if (failed >= 0)
                        return locale_ntp_take((string_address)server, heard);
                rated = failed == SNTP_RATE_LIMITED;
                denied = failed == SNTP_DENIED;
        }

        wanted = filter && !server[0] && !unset ? SNTP_SERVERS : 1;
        for (at = 0; at < array_count(locale_ntp_fallback) &&
                     heard_count < wanted; at++)
        {
                if (server[0] &&
                    string_equals((string_address)server,
                                  (string_address)locale_ntp_fallback[at]))
                        continue;
                failed = locale_ntp_ask((string_address)locale_ntp_fallback[at],
                                        filter, most, heard + heard_count);
                if (failed >= 0)
                {
                        bool again = false;

                        //      Another name for a server already heard from
                        //      is that server saying it again: the network's
                        //      resolver can name every one of these the same
                        //      host. It is not counted, and the next name is
                        //      asked.
                        for (positive earlier = 0; earlier < heard_count; earlier++)
                                again |= heard[earlier].address ==
                                         heard[heard_count].address;
                        if (!again)
                                heard_from[heard_count++] = (b32)at;
                }
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
                    (string_address)locale_ntp_fallback[heard_from[chosen]],
                    heard + chosen);
        }
        return denied ? SNTP_DENIED : rated ? SNTP_RATE_LIMITED : failed;
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

static b32 locale_ntp_status(void)
{
        p8 server[80];
        bool wanted = locale_ntp_wanted();
        bool synced = locale_clock_synced();

        locale_word(LOCALE_NTP_SERVER_PATH, server, sizeof(server));
        if (!server[0])
                string_copy_bounded(server, LOCALE_NTP_DEFAULT_SERVER,
                                    sizeof(server));
        host_say(log, host_label "ntp %s, %s, sampling %s, %s\n",
                 wanted ? "on" : "off", server,
                 locale_ntp_sampling_wanted() ? "on" : "off",
                 synced ? "synchronised" : "waiting");
        return 0;
}

static b32 locale_ntp_sampling_status(void)
{
        host_say(log, host_label "ntp sampling %s\n",
                 locale_ntp_sampling_wanted() ? "on" : "off");
        return 0;
}

//      moonwater ntp on|off, and ntp sampling on|off; turning ntp on asks
//      at once.
static b32 locale_ntp_set(bool sampling, string_address word)
{
        if (!string_equals(word, "on") && !string_equals(word, "off"))
                return host_usage();
        if (radio_write_word(sampling ? LOCALE_NTP_SAMPLING_PATH : LOCALE_NTP_PATH,
                             word) < 0)
                return host_fail("ntp", -1);
        if (!sampling && string_equals(word, "on"))
        {
                locale_ntp_next = 0;
                if (locale_ntp_apply(true) < 0)
                        word = "on, waiting for a reply";
        }
        host_say(log, host_label "ntp %s%s\n", sampling ? "sampling " : "",
                 word);
        return 0;
}

static const struct
{
        char name[8];
} locale_keyboards[] = {
        {"us"}, {"uk"}, {"gb"}, {"de"}, {"se"}, {"sv"}, {"no"}, {"nb"},
        {"dk"}, {"fi"}, {"fr"}, {"es"}, {"it"},
};

static bool locale_keyboard_ok(string_address name)
{
        positive at;

        for (at = 0; at < array_count(locale_keyboards); at++)
                if (string_equals(name, (string_address)locale_keyboards[at].name))
                        return true;
        return false;
}

static b32 locale_keyboard_live(string_address name)
{
        struct canvas_control control;

        memory_zero(address_of control, sizeof(control));
        control.request = SPARK_CANVAS_LAYOUT;
        if (name)
        {
                positive length = string_length(name);

                if (length >= sizeof(control.master_command))
                        return -22;
                memory_copy(control.master_command, name, length);
        }
        return host_spark_once(SPARK_IOCTL_CANVAS, address_of control, FILE_READ);
}

static b32 locale_keyboard_status(void)
{
        p8 name[16];
        struct canvas_control control;

        locale_word(LOCALE_KEYBOARD_PATH, name, sizeof(name));
        if (!name[0])
                string_copy_bounded(name, "us", sizeof(name));
        memory_zero(address_of control, sizeof(control));
        control.request = SPARK_CANVAS_LAYOUT;
        if (host_spark_once(SPARK_IOCTL_CANVAS, address_of control, FILE_READ) >= 0 &&
            control.master_command[0])
                string_copy_bounded(name, control.master_command, sizeof(name));
        host_say(log, host_label "keyboard %s\n", name);
        return 0;
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
        if (radio_write_word(LOCALE_KEYBOARD_PATH, name) < 0)
                return host_fail("keyboard", -1);
        (void)locale_keyboard_live(name);
        host_say(log, host_label "keyboard %s\n", name);
        return 0;
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
                locale_ntp_next = now + (p64)locale_ntp_every * 1000000000ull;
                locale_ntp_every = locale_ntp_every * 2 < LOCALE_NTP_AGAIN
                                       ? locale_ntp_every * 2
                                       : LOCALE_NTP_AGAIN;
        }
        else if (ended == LOCALE_NTP_EXIT_RATE)
        {
                locale_ntp_retry = LOCALE_NTP_RETRY_MOST;
                locale_ntp_next = now + (p64)LOCALE_NTP_RATE_AGAIN * 1000000000ull;
        }
        else if (ended == LOCALE_NTP_EXIT_LONG)
                locale_ntp_next = now + (p64)LOCALE_NTP_AGAIN * 1000000000ull;
        else if (ended != LOCALE_CHILD_IDLE)
        {
                //      Doubling from a second to a quarter of an hour: a
                //      machine that cannot reach a server does not walk
                //      seven names every eight seconds for ever.
                locale_ntp_next = now + (p64)locale_ntp_retry * 1000000000ull;
                locale_ntp_retry = locale_ntp_retry * 2 < LOCALE_NTP_RETRY_MOST
                                       ? locale_ntp_retry * 2
                                       : LOCALE_NTP_RETRY_MOST;
        }
        else if (locale_ntp_synced && !synced &&
                 locale_ntp_retry == LOCALE_NTP_RETRY_LEAST &&
                 locale_ntp_next > now + LOCALE_NTP_RETRY_LEAST * 1000000000ull)
                locale_ntp_next = now + LOCALE_NTP_RETRY_LEAST * 1000000000ull;
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

static fn locale_ntp_keep(bool online)
{
        p64 now = system_clock_ns(HOST_CLOCK_BOOTTIME);
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
        locale_ntp_schedule(ended, locale_clock_synced(), now);
        if (ended != LOCALE_CHILD_IDLE || (locale_ntp_next && now < locale_ntp_next) ||
            !online)
                return;

        if (!locale_child_fork(address_of locale_ntp_child))
        {
                bipolar failed = locale_ntp_apply(false);

                locale_child_end(address_of locale_ntp_child,
                                 failed >= 0 ? 0
                                 : failed == SNTP_RATE_LIMITED
                                     ? LOCALE_NTP_EXIT_RATE
                                 : failed == SNTP_DENIED || failed > SNTP_ANSWER
                                     ? LOCALE_NTP_EXIT_LONG
                                     : 1);
        }
}

static fn locale_restore(void)
{
        p8 zone[80];
        p8 keyboard[16];

        locale_word(CLOCK_ZONE_PATH, zone, sizeof(zone));
        if (zone[0])
        {
                tzset();
                (void)locale_zone_kernel();
                (void)bowl_write_localtime_host();
                (void)bowl_write_localtime_all(null);
        }

        locale_word(LOCALE_KEYBOARD_PATH, keyboard, sizeof(keyboard));
        if (keyboard[0])
                locale_keyboard_live(keyboard);

        locale_ntp_next = 0;
        locale_ntp_retry = LOCALE_NTP_RETRY_LEAST;
        locale_ntp_synced = 0;
        locale_ntp_every = LOCALE_NTP_AGAIN_FIRST;
        locale_auto_next = 0;
        locale_auto_wait = LOCALE_AUTO_LEAST;
        //      Only the machine process asks the time. `moonwater boot` comes
        //      through here too when a disk is taken, and the machine
        //      process, released by the same verdict, asks as well: two
        //      queries that each measured an offset before either applied it
        //      stepped the clock twice.
        locale_ntp_on = locale_ntp_wanted();
        if (host_machine_self && locale_ntp_on)
                locale_ntp_keep(true);
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
        locale_ntp_keep(online);
        locale_auto_keep(locale_ntp_on, network);
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

/* The word at an index in a list of words one space apart. */
static fn name_word(const p8 address_to list, positive index, p8 address_to into,
                    positive room)
{
        positive length = 0;

        for (; index; list++)
                if (address_to list == ' ')
                        index--;

        while (address_to list && address_to list != ' ' && length + 1 < room)
                into[length++] = address_to list++;

        into[length] = end;
}

static fn name_roll(p8 address_to into, positive room)
{
        p8 noun[16];
        p32 bits;

        system_random_fill(address_of bits, sizeof(bits), 0);

        name_word(name_adjectives, bits & (NAME_WORDS - 1), into, room);
        string_append_bounded(into, "-", room);
        name_word(name_nouns, (bits >> 9) & (NAME_WORDS - 1), noun, sizeof(noun));
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

        return length && string_span_of_set(name, NAME_LETTERS) == length &&
               name[0] != '-' && name[length - 1] != '-' &&
               !string_equals(name, "random") && link_name_good(name);
}

static bipolar name_apply(string_address name)
{
        return system_call_2(syscall(sethostname), (positive)name,
                             string_length(name));
}

//      A roll that could not be saved, kept so that the next ask in this
//      process is the same name and not another roll.
static p8 name_unsaved[NAME_ROOM];

/* The saved name, rolled and saved first when there is none or it does not
   read. Says whether it had to roll in `rolled`, and whether the name is
   saved at the end: false when /root would not take it. */
static bool name_ensure(p8 address_to into, positive room, bool address_to rolled)
{
        locale_word(NAME_PATH, into, room);
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

static b32 host_name(string_address address_to arguments, positive count)
{
        p8 name[NAME_ROOM];
        bool rolled;

        if (count > 3)
                return host_usage();

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

                host_say(log, "%s\n", name);
                return 0;
        }

        host_need_root("moonwater name");

        if (string_equals(arguments[2], "random"))
                name_roll(name, sizeof(name));
        else
        {
                string_copy_bounded(name, arguments[2], sizeof(name));
                for (positive at = 0; name[at]; at++)
                        name[at] = byte_to_lower(name[at]);

                if (!name_valid(name))
                        return host_refuse("%s is not a machine name: lowercase "
                                           "letters, digits and hyphens, a "
                                           "letter or digit at each end, under "
                                           "32 characters, and not random or "
                                           "a word moonwater link takes\n",
                                           arguments[2]);
        }

        {
                bipolar done = radio_write_word(NAME_PATH, name);

                if (done < 0)
                        return host_fail(NAME_PATH, done);
                done = name_apply(name);
                if (done < 0)
                {
                        host_say(log_error, host_label "%s is saved, but the "
                                            "kernel did not take it: %s\n",
                                 name, file_reason(done));
                        return 1;
                }
        }

        host_say(log, "%s\n", name);
        return 0;
}

static b32 host_locale(string_address address_to arguments, positive count)
{
        string_address verb = arguments[1];
        string_address word = count > 2 ? arguments[2] : null;

        if (string_equals(verb, "time"))
        {
                if (count == 2)
                        return locale_time_status();
                if (count != 3 || !string_equals(word, "sync"))
                        return host_usage();
                host_need_root("moonwater");
                return locale_time_sync();
        }

        if (string_equals(verb, "timezone"))
        {
                if (count == 2)
                        return locale_zone_status();
                if (count != 3)
                        return host_usage();
                if (string_equals(word, "list"))
                        return locale_zone_list();
                host_need_root("moonwater");
                if (string_equals(word, "auto"))
                        return locale_zone_auto();
                return locale_zone_set(word);
        }

        if (string_equals(verb, "ntp"))
        {
                if (count == 2)
                        return locale_ntp_status();
                if (string_equals(word, "sampling"))
                {
                        if (count == 3)
                                return locale_ntp_sampling_status();
                        if (count != 4)
                                return host_usage();
                        host_need_root("moonwater");
                        return locale_ntp_set(true, arguments[3]);
                }
                if (count != 3)
                        return host_usage();
                host_need_root("moonwater");
                return locale_ntp_set(false, word);
        }

        if (count == 2)
                return locale_keyboard_status();
        if (count != 3)
                return host_usage();
        if (string_equals(word, "list"))
                return locale_keyboard_list();
        host_need_root("moonwater");
        return locale_keyboard_set(word);
}


/*
        Forget userspace, keep the machine.

        /home is emptied. /root is emptied except the overlay and the files
        an image update already leaves on the data partition. /bowls
        stays: that is the pre-installed software a kiosk starts after wipe.
        The builtin machine script calls this at every settled boot.
*/
static string_address host_wipe_keep[] = {
    "main.moonwater.sh",
    "wifi",
    "wifi.power",
    "wired.power",
    "bluetooth",
    "bluetooth.power",
    "internet",
    "tune",
    "timezone",
    "timezone.mode",
    "ntp",
    "ntp.server",
    "ntp.sampling",
    "keyboard",
    "name",
    "link",
    "link.key",
    "link.peers",
    "link.port",
    "link.groups",
    null,
};

static bipolar host_wipe_ensure(string_address path, positive mode)
{
        bipolar made = system_make_directory_at(AT_FDCWD, path, mode);

        return made < 0 && made != -ERROR_EXISTS ? made : 0;
}

static b32 host_wipe(void)
{
        bipolar failed;

        host_need_root("moonwater wipe");

        failed = bowl_reset_walk("/home", 0);
        if (failed < 0)
                return host_fail("/home", failed);

        failed = host_wipe_ensure("/home", 0755);
        if (failed < 0)
                return host_fail("/home", failed);

        failed = bowl_reset_walk_at(AT_FDCWD, "/root", 0, true, host_wipe_keep);
        if (failed < 0)
                return host_fail("/root", failed);

        failed = host_wipe_ensure("/root", 0700);
        if (failed < 0)
                return host_fail("/root", failed);

        host_say(log, host_label "userspace forgotten\n");
        return 0;
}

/*
        Bound events, and the init and exit lists, as one verb.

        A kernel event is one line. init and exit stay lists, because more
        than one thing runs at boot and at stop. An empty line puts the
        event's default back. What is not the default is kept in the image
        and put back at the next boot.
*/
static fn host_bind_say(string_address prefix, struct bind_control address_to control)
{
        p16 line = host_machine_event_line(control->event);
        string_address name = (string_address)control->name;
        bool said = false;

        if (line)
                string_format(log, "%s%s: %s:%p", prefix, name, host_machine_where(),
                              (positive)line);
        else if (control->command[0])
                string_format(log, "%s%s: %s", prefix, name,
                              host_plain((string_address)control->command));
        else if (control->flags & SPARK_BIND_DEFAULT)
                string_format(log, "%s%s", prefix, name);
        else
                //      The kernel keeps a line somebody set from anyone but
                //      root, and clears the DEFAULT flag so that this can say so.
                string_format(log, "%s%s: set, and root's to read", prefix, name);

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
                if (failed)
                        return host_settings_refused("bind", failed, address_of settings);
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

static bipolar host_bind_each(string_address prefix, bool required)
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
                host_bind_say(prefix, address_of control);
        }

        system_close(device);
        return 0;
}

static b32 host_bind_events(void)
{
        bipolar failed = host_bind_each("  ", true);

        if (failed < 0)
                return host_fail(SPARK_DEVICE, failed);

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

static b32 host_bind_tell(unsigned int event, string_address command)
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
                return host_refuse("that command is longer than the %s a bound event holds\n",
                                   "255 bytes");
        if (failed < 0)
                return host_fail(SPARK_DEVICE, failed);

        if (host_bind_keep(event, address_of control))
                return 1;
        host_bind_say(host_label, address_of control);
        log_flush();
        return 0;
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
                memory_copy(arguments + 2, arguments + 3,
                            (count - 3) * sizeof(*arguments));
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
                if (host_bind_request(SPARK_BIND_GET, 1, null, address_of probe) < 0)
                        return host_fail(SPARK_DEVICE, -ENODEV);

                if (second)
                        string_format(log_error,
                                      host_label "%s %s is not a bound event; the events are ",
                                      arguments[2], arguments[3]);
                else
                        string_format(log_error, host_label "%s is not a bound event; the events are ",
                                      arguments[2]);
                host_bind_names(log_error);
                host_say(log_error, "\n");
                return 1;
        }

        if (count == words)
        {
                struct bind_control control;
                bipolar failed = host_bind_request(SPARK_BIND_GET, event, null,
                                                   address_of control);

                if (failed < 0)
                        return host_fail(SPARK_DEVICE, failed);
                host_bind_say(host_label, address_of control);
                log_flush();
                return 0;
        }

        if (!host_settings_words(text, sizeof(text), arguments + words, count - words,
                                 address_of length))
                return host_refuse("that command is longer than the %s a bound event holds\n",
                                   "255 bytes");

        return host_bind_tell(event, text);
}

// The command ---------------------------------------------------

static fn host_title(writer out)
{
        string_format(out, TERM_BOLD "Moonwater" TERM_RESET "\n\n");
}

/*      One line of the list: the command in bold, then padding that brings
        the description to its column, then the description dimmed. */
#define HOST_ROW(command, padding, description) \
        TERM_BOLD "  " command TERM_RESET padding TERM_DIM description TERM_RESET "\n"

/*
        One list as it is typed, then what it does. Status, help and a
        wrong argument share it, so the picture and the usage read as the
        same page.
*/
static fn host_usage_write(writer out)
{
        host_say(out,
                 HOST_ROW("status", "                      ", "this picture")
                 HOST_ROW("setup", "                       ", "live or kept on a disk: install, update, use, live")
                 HOST_ROW("bind", "                        ", "what the machine's events run")
                 HOST_ROW("bind EVENT [COMMAND]", "        ", "one event; empty puts the default back")
                 HOST_ROW("bind init [add|remove ...]", "  ", "what runs at boot")
                 HOST_ROW("bind init mount [on|off]", "    ", "mount kept disks at boot")
                 HOST_ROW("bind exit [add|remove ...]", "  ", "what runs when the machine stops")
                 HOST_ROW("canvas [on|off]", "             ", "the desktop")
                 HOST_ROW("canvas log|terminal", "         ", "open the kernel log or a terminal")
                 HOST_ROW("airplane [on|off]", "           ", "every radio at once")
                 HOST_ROW("brightness [N%%|+N|-N]", "       ", "the screen backlight")
                 HOST_ROW("power [performance|balanced|powersave]", " ", "profile and CPU governor")
                 HOST_ROW("cpu [boost|smt on|off] [online|offline N]", " ", "turbo, SMT and hotplug")
                 HOST_ROW("charge [limit N|off]", "        ", "where the battery stops charging")
                 TERM_BOLD "  sleep" TERM_RESET " | " TERM_BOLD "hibernate" TERM_RESET
                 "           " TERM_DIM "suspend to RAM or to disk" TERM_RESET "\n"
                 HOST_ROW("bios [reboot]", "               ", "restart into the firmware's setup screen")
                 HOST_ROW("wired [on|off]", "              ", "the wired links: no lease is asked on one when off")
                 HOST_ROW("wifi [on|off]", "               ", "the wireless radio")
                 HOST_ROW("wifi add SSID [PASSWORD|-]", "  ", "remember a network and join it; asks for")
                 "                              " TERM_DIM "the password, - reads it from stdin" TERM_RESET "\n"
                 HOST_ROW("wifi remove SSID", "            ", "forget a saved network, and leave it")
                 HOST_ROW("bluetooth [on|off]", "          ", "the bluetooth radio")
                 HOST_ROW("bluetooth add NAME", "          ", "remember a bluetooth device")
                 HOST_ROW("bluetooth remove NAME", "       ", "forget a bluetooth device")
                 HOST_ROW("priority internet [wired|wifi]", " ", "which link when both are up [wired]")
                 HOST_ROW("time [sync]", "                 ", "the clock; sync sets it and the zone now")
                 HOST_ROW("timezone [ZONE|se|+1|list]", "  ", "the clock's zone; setting one makes it manual")
                 HOST_ROW("timezone auto", "               ", "from the network [auto]: one Cloudflare")
                 "                              " TERM_DIM "request per network joined" TERM_RESET "\n"
                 HOST_ROW("ntp [on|off]", "                ", "set the clock from the network [on]")
                 HOST_ROW("ntp sampling [on|off]", "       ", "keep the lowest-delay sample of five [on]")
                 HOST_ROW("link [pair|NAME|help]", "       ", "link machines by a code, then a terminal or a command on any, by name")
                 HOST_ROW("keyboard [LAYOUT|list]", "      ", "Canvas keys: us uk de se no dk fi fr es it")
                 HOST_ROW("name [NEW|random]", "           ", "what this machine is called; rolled at first boot")
                 HOST_ROW("wipe", "                        ", "forget /home and /root, keep the machine")
                 "\n"
                 TERM_DIM                       "  Settings stay in the image this session started from.\n"
                 "  install takes this session's; update keeps the disk's.\n"
                 "  The machine script overwrites bind, init and exit.\n"
                 "  " HOST_MACHINE_SCRIPT " overlays the kernel builtin.\n" TERM_RESET);
}

static b32 host_usage(void)
{
        host_title(log_error);
        host_usage_write(log_error);
        return 2;
}

/*
        What setup does. A session is live, with nothing kept after power off,
        or it keeps /bowls, /root and /home on an install's data partition;
        the verbs move between the two, or write this build onto a disk.
*/
static fn host_setup_usage_write(writer out)
{
        host_say(out,
                 HOST_ROW("setup", "                       ", "where this session runs, and the installs found")
                 HOST_ROW("setup install DISK [removable]", " ", "erase DISK and put Moonwater on it; removable")
                 "                              " TERM_DIM "takes a disk that says it is removable" TERM_RESET "\n"
                 HOST_ROW("setup update [DISK]", "         ", "write this build over an install, keeping its data")
                 HOST_ROW("setup use [DISK]", "            ", "run this build with an install's /bowls /root /home")
                 HOST_ROW("setup live", "                  ", "leave the disks alone this session")
                 "\n");
}

static b32 host_setup_usage(void)
{
        host_title(log_error);
        host_setup_usage_write(log_error);
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
        p8 last[RADIO_SSID_MOST + 1];
        p8 name[RADIO_SSID_MOST * 4 + 1];
        bipolar failed = 0;
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
        if (radio_last_get(last, sizeof(last), address_of failed))
        {
                radio_display(name, sizeof(name), last, string_length(last));
                string_format(log, "  wifi on, not joined: %s %s\n", name,
                              radio_join_words(failed));
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

        host_read_text(HOST_VERDICT, verdict, sizeof(verdict));
        if (host_starts(verdict, "disk "))
                string_format(log, "  kept on %s: %s /root /home\n",
                              verdict + 5, BOWL_ROOT_DIRECTORY);
        else if (host_starts(verdict, "ask "))
                string_format(log, "  waiting: %s has another build; "
                                   "moonwater setup use, update or live\n",
                              verdict + 4);
        else
                string_format(log, "  live session: nothing is kept after power "
                                   "off; moonwater setup install DISK keeps it\n");

        host_census_take(address_of census);
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
                host_canvas_write("  ", address_of canvas);

        {
                p8 zone[80];
                p8 keyboard[16];

                p8 shown[128];
                p8 how[48];

                locale_word(CLOCK_ZONE_PATH, zone, sizeof(zone));
                locale_word(LOCALE_KEYBOARD_PATH, keyboard, sizeof(keyboard));
                locale_zone_title(zone, shown, sizeof(shown));
                locale_zone_how(how, sizeof(how));
                string_format(log, "  timezone %s %s\n", shown, how);
                string_format(log, "  ntp %s, sampling %s\n",
                              locale_ntp_wanted() ? (string_address) "on"
                                                  : (string_address) "off",
                              locale_ntp_sampling_wanted()
                                  ? (string_address) "on"
                                  : (string_address) "off");
                string_format(log, "  keyboard %s\n",
                              keyboard[0] ? (string_address)keyboard
                                          : (string_address) "us");
        }

        {
                p8 name[NAME_ROOM];

                locale_word(NAME_PATH, name, sizeof(name));
                if (name_valid(name))
                        string_format(log, "  name %s\n", name);
        }

        host_status_wifi();

        (void)host_bind_each("  ", false);

        host_settings_session(address_of settings);
        host_settings_lines(address_of settings, 0, true);
        string_format(log, "  init mount is %s\n",
                      settings.flags & SPARK_SETTINGS_MOUNT_OFF ? "off" : "on");
        host_settings_lines(address_of settings, 1, true);

        string_format(log, "\n");
        host_usage_write(log);
        return 0;
}

/* use and update: the named install, the one boot asked about, or the only one. */
static b32 host_answer(bool update, string_address disk)
{
        p8 verdict[HOST_NAME_ROOM + 16];
        host_census census;
        host_install address_to install = null;

        host_read_text(HOST_VERDICT, verdict, sizeof(verdict));
        if (disk && host_starts(disk, "/dev/"))
                disk += 5;

        /*      The data this session already keeps. An update touches only the
                system partition, so it can go ahead underneath what is mounted;
                keeping a second disk's data on top of the first cannot. */
        if (host_starts(verdict, "disk "))
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
        else if (host_starts(verdict, "ask "))
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
                host_setup_usage_write(log);
                return 0;
        }

        if (string_equals(verb, "install"))
        {
                bool removable = count == 5 && string_equals(arguments[4], "removable");

                if (count < 4 || count > 5 || (count == 5 && !removable))
                        return host_setup_usage();
                host_need_root("moonwater setup");
                host_state_ready();
                return host_install_disk(arguments[3], removable);
        }

        if (string_equals(verb, "live") && count == 3)
        {
                host_need_root("moonwater setup");
                host_state_ready();
                system_remove_at(AT_FDCWD, HOST_QUESTION, 0);
                host_verdict_set("live", "");
                host_say(log, host_label "the disks are left alone this session\n");
                return 0;
        }

        if ((update || string_equals(verb, "use")) && count <= 4)
        {
                host_need_root("moonwater setup");
                host_state_ready();
                return host_answer(update, count == 4 ? arguments[3] : null);
        }

        return host_setup_usage();
}

static b32 host_main()
{
        string_address address_to arguments = program_argument_list();
        positive count = (positive)program_argument_count();
        string_address verb = count > 1 ? arguments[1] : null;

        if (!verb || string_equals(verb, "-h") || string_equals(verb, "--help"))
        {
                if (count > 2)
                        return host_usage();

                host_title(log);
                host_usage_write(log);
                return 0;
        }

        if (string_equals(verb, "status") && count <= 2)
                return host_status();

        if (string_equals(verb, "setup"))
                return host_setup(arguments, count);

        // Before the root check: reading needs nothing, and the kernel
        // decides who may set a bound event. init and exit still need root.
        if (string_equals(verb, "bind"))
                return host_bind(arguments, count);

        if (string_equals(verb, "canvas"))
                return host_canvas(arguments, count);

        if (string_equals(verb, "bios"))
                return host_bios(arguments, count);

        if (string_equals(verb, "airplane") || string_equals(verb, "brightness") ||
            string_equals(verb, "charge") || string_equals(verb, "power") ||
            string_equals(verb, "cpu") || string_equals(verb, "sleep") ||
            string_equals(verb, "hibernate"))
                return host_tune(arguments, count);

        if (string_equals(verb, "wifi") || string_equals(verb, "bluetooth") ||
            string_equals(verb, "priority") || string_equals(verb, "wired"))
                return host_radio(arguments, count);

        if (string_equals(verb, "timezone") || string_equals(verb, "ntp") ||
            string_equals(verb, "keyboard") || string_equals(verb, "time"))
                return host_locale(arguments, count);

        if (string_equals(verb, "name"))
                return host_name(arguments, count);

        if (string_equals(verb, "link"))
                return link_main(arguments, count);

        if (string_equals(verb, "wipe") && count == 2)
                return host_wipe();

        if (string_equals(verb, "machine") && count == 2)
                return host_machine_run();

        if (!string_equals(verb, "boot") && !string_equals(verb, "ask"))
                return host_usage();

        host_need_root("moonwater");

        host_state_ready();

        if (count > 2)
                return host_usage();

        if (string_equals(verb, "boot"))
                return host_boot();

        {
                p8 verdict[HOST_NAME_ROOM + 16];

                host_read_text(HOST_VERDICT, verdict, sizeof(verdict));
                if (!host_starts(verdict, "ask ") ||
                    system_rename_at(AT_FDCWD, HOST_QUESTION, AT_FDCWD,
                                     HOST_QUESTION_TAKEN, 0) < 0)
                        return host_refuse("there is nothing to ask%s\n", "");

                host_question(verdict + 4);
        }
        return 0;
}

#define MOONWATER_CLI
#include "../moonwater/moonwater.c"
