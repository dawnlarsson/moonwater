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

        An installed machine also takes a build from a stick without starting
        it. `moonwater setup` names what each stick plugged in carries beside
        the build running, `setup update usb` writes the one stick's build
        over the install, judged by its bytes and kept with the disk's data
        and settings, and `setup rollback` starts the build the install had
        before it again. The loader is only ever replaced by a rename from a
        copy beside it that was written, synced and read back, so a machine
        that loses power at any moment starts one build or the other.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater

        www.dawning.dev
*/

#define host_label TERM_BOLD "[Moonwater]" TERM_RESET " "

#define HOST_STATE "/run/moonwater"
#define HOST_VERDICT HOST_STATE "/verdict"
#define HOST_QUESTION HOST_STATE "/question"
#define HOST_QUESTION_TAKEN HOST_STATE "/question.taken"
#define HOST_DATA HOST_STATE "/data"
#define HOST_SYSTEM HOST_STATE "/system"
#define HOST_MEDIUM HOST_STATE "/medium"
#define HOST_LOOK HOST_STATE "/look"
//      The machine script there was, which ran as root at boot: still a name the link keeps from a peer's files
//      (src/waterlink/service.c), so one left on a disk is not a peer's to write.
#define HOST_MACHINE_SCRIPT "/root/main.moonwater.sh"

/*
        The commit this build was made from and its date, which build writes
        into artifacts/moonwater_config.h (build.c, build_commit_take): twelve
        hex digits and -dirty when the tree had changes, or unknown outside a
        git checkout. A compile that names no header says unknown.
*/
#ifndef MOONWATER_COMMIT
#define MOONWATER_COMMIT "unknown"
#endif
#ifndef MOONWATER_COMMIT_DATE
#define MOONWATER_COMMIT_DATE "unknown"
#endif
static const char host_commit[] = MOONWATER_COMMIT;
static const char host_commit_date[] = MOONWATER_COMMIT_DATE;

#include "../moonwater/moonwater.c"
#include "../audio/state.c" // the sound server's page, which bare `moonwater audio` reads
#include "../moonwater/splash.c" // the boot splash: its file, and the parser the loader checks it with too
#include "splash_mark.c"         // and the built-in picture an install writes

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
   firmware on a disk with no boot entry of its own starts this file. An update
   carried on a stick that is not to start the machine is the image under the
   name the build gives it in dist/, in a directory firmware does not look in,
   so copying that one file onto any FAT stick is enough. The machine is the
   one a PE header names, which an image for another one does not. */
#if X64
#define HOST_IMAGE "/EFI/BOOT/BOOTX64.EFI"
#define HOST_IMAGE_NEXT "/EFI/BOOT/BOOTX64.NEW"
#define HOST_UPDATE_FILE "/moonwater/bootx64.efi"
#define HOST_PE_MACHINE 0x8664
#elif ARM64
#define HOST_IMAGE "/EFI/BOOT/BOOTAA64.EFI"
#define HOST_IMAGE_NEXT "/EFI/BOOT/BOOTAA64.NEW"
#define HOST_UPDATE_FILE "/moonwater/bootaa64.efi"
#define HOST_PE_MACHINE 0xaa64
#elif RISCV64
#define HOST_IMAGE "/EFI/BOOT/BOOTRISCV64.EFI"
#define HOST_IMAGE_NEXT "/EFI/BOOT/BOOTRISCV64.NEW"
#define HOST_UPDATE_FILE "/moonwater/bootriscv64.efi"
#define HOST_PE_MACHINE 0x5064
#else
#error "no removable-media loader name for this architecture"
#endif
#define HOST_KEPT_DIRECTORY "/EFI/moonwater"
#define HOST_KEPT_IMAGE HOST_KEPT_DIRECTORY "/previous.efi"
#define HOST_KEPT_IMAGE_NEXT HOST_KEPT_DIRECTORY "/previous.new"
/* What a stick says the update's bytes are, beside it: sha256sum's line. */
#define HOST_DIGEST_SUFFIX ".sha256"
/* Where a stick is looked at for the build it carries, read-only. */
#define HOST_STICK HOST_STATE "/stick"
#define HOST_STICKS 8

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
#define HOST_DATA_MOUNT (MS_NODEV | MS_NOSUID | MS_NOATIME)
#else
#define HOST_DATA_MOUNT (MS_NODEV | MS_NOATIME)
#endif
#define HOST_LOCK HOST_STATE "/lock"
#define HOST_LOCK_WAIT_NS ((p64)30000000000)
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
static b32 host_image_fit(string_address path, p8 address_to build, positive room,
                          p16 address_to machine);
static string_address host_fit_said(b32 fit, p16 machine);
static bool host_settings_slots(bipolar handle, host_settings address_to slots,
                                p64 address_to offset);
static bool host_settings_booted(host_settings address_to into);
static bool host_settings_kept(host_settings address_to into);
static bool host_settings_session(host_settings address_to settings);
static bool host_settings_keep(host_settings address_to settings);
static bool host_settings_install(host_install address_to install,
                                  host_settings address_to into);
static fn host_events_boot(host_settings address_to settings);
static b32 host_settings_command(string_address address_to arguments, positive count);
static b32 host_usage(void);
static b32 host_setting_group(string_address address_to words, positive count);
static positive radio_display(p8 address_to into, positive room, p8 address_to name,
                              positive length);
static bool host_machine_stop(void);

//      builtin.c's, which stops the machine the way reboot does; it is
//      included after this file. The commands are shell.c's REBOOT_*.
fn shell_stop(writer write, positive command);
static b32 host_machine_run(void);
static b32 rover_kernel_verb(string_address address_to arguments, positive count);
static b32 host_version_verb(string_address address_to arguments, positive count);
static fn tune_restore(void);
static fn radio_restore(void);
static fn radio_recover(void);
static fn usb_restore(void);
static fn usb_recover(void);
static fn locale_restore(void);
static fn dns_restore(void);
static bipolar security_publish(host_settings address_to settings);
static b32 security_publish_answer(host_settings address_to settings);
//      Set by the machine process alone, in moonwater.c: only it asks the time.
static bool host_machine_self;
static fn name_restore(void);
//      The groups an image carries, joined (src/waterlink/command.c): the link's, and nothing else reads them.
static fn link_group_restore(void);
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

/* Four lowercase hexadecimal digits of a 16-bit value (e.g. USB/PCI vendor or device id). */
static fn host_hex4(p8 address_to into, p32 value)
{
        p8 wire[2];

        network_store_16(wire, (p16)value);
        memory_into_hex(into, wire, sizeof(wire));
        into[4] = end;
}

/* Build "/proc/<pid><suffix>" with a byte_cursor; false if it does not fit. */
static bool host_proc_path(p8 address_to into, positive room, positive pid,
                           string_address suffix)
{
        byte_cursor cursor = byte_cursor_open(into, room);

        byte_cursor_put_bytes(address_of cursor, "/proc/", 6);
        byte_cursor_put_u64(address_of cursor, (p64)pid);
        byte_cursor_put_string(address_of cursor, suffix);
        return byte_cursor_terminate(address_of cursor);
}

#include "host/disk.c" // the disk: the state directory and boot's verdict, the lock, the census of disks, images and their builds, the place steps an image is written by (host_place_*), the splash an install lays, a disk taken, boot's settle and its question, and setup install's work (host_install_locked); the setup verbs are page.c's
#include "host/image.c" // the settings an image carries and a disk keeps (host_settings_*): read, written, carried and applied; and spark's bind
#include "host/boot.c" // boot's service, its events and the shells it starts; the verbs canvas, latency and bios
#include "host/nl80211.c" // the radio's words to the kernel: nl80211, a station joined and left and the process that keeps it (the keeper), WPA's and SAE's handshakes; the wifi verb is radio.c's
#include "host/radio.c" // the verbs wifi, bluetooth, priority and wired: the switches, the saved networks, the air scanned, a join, its password and its state files (wifi.last), the status, the shared link
#include "host/tune.c" // the verbs airplane, brightness, charge, battery, power, cpu, sleep and hibernate: the switches the kernel keeps in sysfs
#include "host/device.c" // the verb device: which devices plugged in are let in, the rules, the census, the lock, the watch, the list
#include "host/locale.c" // the timezone and the clock (SNTP, NTS, the zone found and set), the keyboard's layout and the canvas's modes: the work under the verbs time, timezone, ntp and keyboard, which are debug.c's
#include "host/setting.c" // the verbs splash, taskbar, mouse, language and audio, and the canvas's settings; the clock kept and restored at boot, and the name
static long host_windows_map(bool writable);
#include "host/debug.c" // the verbs debug, notices, sensors, name, time, timezone, ntp and keyboard
#include "host/profile.c" // the verb profile: where a program's processor time goes, sampled by the kernel
#include "host/memory.c" // the verb memory: installed, usable, available and shared memory and the largest memfds, once or to the kernel log every minute
#include "host/desktop.c" // the verb desktop: the desktop a machine starts, and which one
#include "host/security.c" // the verbs dns, security and bind
#include "host/network.c" // the verb network: the interfaces, the name servers and discovery as one page, and discovery's switches
#include "host/page.c" // the verbs setup (install, update, update usb, rollback, use, live, removable), status, help, wipe, machine, boot and ask: the page of verbs and its rows, the sticks and an update from one, a rollback, a wipe
#include "host/install.c" // the verb install: the installer's drives, the one this started from, an update or an overwrite of one and what it wrote read back
#define MOONWATER_CLI_ROVER
#include "../rover/compile.c" // the verb rover: Moonwater's rules compiled to a table, printed back, drawn as a graph, tested against states; and the schema

/*
        moonwater canvas identify: each screen's number, as a black-on-white badge in that screen's top left corner for three seconds (the window
        is src/canvas/ui/identify.c). The numbers are windows_screen_numbers' (window.c), the ones the settings' Display map
        shows. One window is started per screen, with that screen's index, each in a session of its own as the settings
        window is (host_detach), and this returns at once. The desktop must be up, as `moonwater settings` needs: the
        compositor's pid alive and its table there.
*/
/* Whether the compositor is alive and its window table exists. */
static bool host_desktop_up(positive address_to pid_out)
{
        positive pid = desktop_pid_of((string_address)DESKTOP_COMPOSITOR);

        if (pid_out)
                *pid_out = pid;
        return desktop_alive(pid) &&
               system_access_at(AT_FDCWD, (string_address)WINDOWS_TABLE, 0) == 0;
}

static b32 host_canvas_identify(positive count)
{
        const struct windows address_to table;
        unsigned int numbers[WINDOWS_OUTPUTS], screens;

        return_if(count != 3, host_usage());
        return_if(!host_desktop_up(null),
                  host_refuse("the compositor's desktop is not up, and the identify windows are its windows: "
                              "moonwater desktop on starts it\n"));
        table = host_windows_header();
        return_if(!table, host_refuse("the compositor's window table cannot be read\n"));
        // windows_screen_numbers counts a table with no screen as one, for the window it has to draw: none is asked for here.
        if (!__atomic_load_n(&table->outputs, __ATOMIC_ACQUIRE))
        {
                host_windows_unmap(table);
                return host_refuse("no screen is connected, so there is none to identify\n");
        }
        screens = windows_screen_numbers(table, numbers);
        host_windows_unmap(table);
        for (unsigned int at = 0; at < screens; at++)
        {
                p8 index[16];
                positive length = positive_into_string(index, (positive)at);
                string_address words[3] = {(string_address) "/identify", (string_address)index, null};

                index[length] = 0;
                if (host_detach(descriptors_none, descriptors_none))
                {
                        (void)shell_exec_file(words[0], words, 2, file_environment_all());
                        system_call_1(syscall(exit), 127);
                }
        }
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
        p8 path[48] = "/proc/", line[512], face[256], bold[272];
        string_address words[8];
        positive pid = 0, length, at = 0, used = 0;
        bipolar handle, got = 0;

        // moonwater settings gallery: the same window on the kit's gallery, every control of it, to see them move.
        return_if(count != 2 && (count != 3 || !string_equals(arguments[2], (string_address) "gallery")), host_usage());

        return_if(!host_desktop_up(address_of pid),
                  host_refuse("the compositor's desktop is not up, and the settings window is one of its "
                              "windows: moonwater desktop on starts it\n"));

        // Its -f, from its command line, words a zero apart.
        string_copy_bounded(face, (string_address)HOST_SETTINGS_FACE, sizeof(face));
        handle = host_proc_path((p8 address_to)path, sizeof(path), pid, (string_address) "/cmdline")
                     ? system_open_at(AT_FDCWD, path, FILE_READ | O_CLOEXEC)
                     : -1;
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
        {
                p8 address_to regular = (p8 address_to)memory_search(bold, length, "Regular", 7);

                if (regular && length + 1 < sizeof(bold))
                {
                        positive from = (positive)(regular - bold);

                        string_copy_bounded(bold + from, (string_address) "Bold", sizeof(bold) - from);
                        string_copy_bounded(bold + from + 4, face + from + 7, sizeof(bold) - from - 4);
                }
        }

        words[used++] = (string_address) "/settings";
        words[used++] = (string_address) "-f";
        words[used++] = face;
        if (string_compare(bold, face) && system_access_at(AT_FDCWD, bold, 4) == 0)
        {
                words[used++] = (string_address) "-b";
                words[used++] = bold;
        }
        if (count == 3)
                words[used++] = (string_address) "-g";
        words[used] = null;

        if (!host_detach(descriptors_none, descriptors_none))
                return 0;
        (void)shell_exec_file(words[0], words, used, file_environment_all());
        system_call_1(syscall(exit), 127);
        return 1;
}

/*
        moonwater window: the windows of the compositor's desktop, and a command started with where its first
        window goes.

        Bare, each window that has shown a picture: its name, its program's pid, where it is, the size it drew and
        its title, which is a program's to write and so is spelled and not obeyed. With words, the words of a
        placement (window.c's window_place_parse: center, at X,Y, maximized, fullscreen, screen N, size WxH, name
        NAME), then the command, which runs in this process's place as env runs one, with MOONWATER_PLACE in its
        environment: the first window it opens is put there, and the programs it starts after that are not. --
        ends the words, for a command named like one of them. move puts windows that are open (host_window_move).

        The words are judged first, then root is asked for, as the other verbs do: the window table and its doors
        are root's (WINDOWS_MODE), so no program that is not root opens a window to be placed. The placement goes
        only to the command, in its own environment, which the compositor never reads: it reads the placement
        out of the window's row, as it reads the rest of what a program writes there, and clamps it. A program
        that is no window of this compositor's (a bowl's, Wayland's, X's) is started all the same and never reads it.
*/
#define HOST_WINDOW_ENVIRONMENT 512

/*
        The window table's header and columns, mapped to be read, or with writable to be written too (a move): from
        the device, which hands root the compositor's, or at its path. A negative errno when there is none to map,
        and -ENOENT for a table shorter than the struct, which no build can read the fields of (a compositor still
        starting), as debug says it.
*/
static long host_windows_map(bool writable)
{
        long spark, file = -1, mapped;

        spark = window_call(WINDOW_SYS_OPENAT, -100, (long)"/dev/spark", WINDOW_OPEN_FLAGS, 0, 0, 0);
        if (spark >= 0)
        {
                file = window_call(WINDOW_SYS_IOCTL, spark, WINDOWS_IOCTL_TABLE_GET, 0, 0, 0, 0);
                window_call(WINDOW_SYS_CLOSE, spark, 0, 0, 0, 0, 0);
        }
        if (file < 0)
                file = window_call(WINDOW_SYS_OPENAT, -100, (long)WINDOWS_TABLE, writable ? WINDOW_OPEN_FLAGS : WINDOWS_O_READ, 0, 0, 0);
        if (file >= 0 && window_call(WINDOW_SYS_LSEEK, file, 0, 2, 0, 0, 0) < (long)sizeof(struct windows))
        {
                window_call(WINDOW_SYS_CLOSE, file, 0, 0, 0, 0, 0);
                file = -ENOENT;
        }
        if (file < 0)
                return file;
        // PROT_READ, with PROT_WRITE for a move; MAP_SHARED
        mapped = window_call(WINDOW_SYS_MMAP, 0, (long)sizeof(struct windows), writable ? 3 : 1, 1, file, 0);
        window_call(WINDOW_SYS_CLOSE, file, 0, 0, 0, 0, 0);
        return mapped;
}

static b32 host_window_list(void)
{
        const struct windows address_to table;
        long mapped = host_windows_map(false);
        positive shown = 0;

        if (mapped < 0 && mapped > -4096)
                return host_refuse("the compositor's desktop is not up: moonwater desktop on starts it\n");
        table = (const struct windows address_to)mapped;
        for (unsigned int row = 0; row < WINDOWS; row++)
        {
                unsigned int pid = __atomic_load_n(&table->pid[row], __ATOMIC_ACQUIRE);
                char name[WINDOWS_NAME], title[WINDOWS_TITLE];

                if (!pid || pid == WINDOWS_TAKING || !__atomic_load_n(&table->frame[row], __ATOMIC_ACQUIRE))
                        continue;
                memory_copy(name, table->name[row], sizeof(name));
                memory_copy(title, table->title[row], sizeof(title));
                name[sizeof(name) - 1] = title[sizeof(title) - 1] = 0;
                host_say(log, "  %s, pid %p, at %b,%b, %p by %p%s: %w\n",
                         window_name_valid(name) ? (string_address)name : (string_address) "-", (positive)pid,
                         (bipolar)table->x[row], (bipolar)table->y[row], (positive)table->width[row],
                         (positive)table->height[row],
                         table->state[row] & WINDOW_MINIMIZED ? (string_address) ", put away" : (string_address) "",
                         writer_terminal_name, (string_address)title);
                shown++;
        }
        if (!shown)
                host_say(log, "  no windows\n");
        window_call(WINDOW_SYS_MUNMAP, mapped, (long)sizeof(struct windows), 0, 0, 0, 0);
        return 0;
}

/*
        moonwater window move PID|NAME PLACE...: every window of that program, or of that name, that shows is put
        where PLACE says, as a window started with it would be (no name: a window is not renamed by a move). The
        placement is written into the window's row and its moved counted up, and its door knocked on, as a second
        start lifts the first; the compositor reads the row and clamps it as it does any.
*/
static b32 host_window_move(string_address address_to arguments, positive count)
{
        struct windows address_to table;
        struct window_place place;
        string_address who = count > 3 ? arguments[3] : (string_address) "", stop = who;
        int words = count > 4 ? window_place_parse(&place, (const char *const *)(arguments + 4), (unsigned int)(count - 4)) : 0;
        positive pid = 0, moved = 0;
        b32 range = 0;
        long mapped;

        if (byte_is_digit(who[0]))
                pid = string_to_number_unsigned_checked(who, address_of stop, 10, address_of range);
        //      A pid is digits that fit a pid and nothing after them; anything else is a name, or nothing.
        return_if(words <= 0 || (positive)words != count - 4 || place.name[0] ||
                      (byte_is_digit(who[0]) ? *stop || range || !pid || pid > 0x7fffffff || (who[0] == '0')
                                             : !window_name_valid((const char *)who)),
                  host_usage());
        host_need_root();
        mapped = host_windows_map(true);
        return_if(mapped < 0 && mapped > -4096,
                  host_refuse("the compositor's desktop is not up, so there is no window to move: moonwater desktop on "
                              "starts it\n"));
        table = (struct windows address_to)mapped;
        for (unsigned int row = 0; row < WINDOWS; row++)
        {
                unsigned int owner = __atomic_load_n(&table->pid[row], __ATOMIC_ACQUIRE);
                char name[WINDOWS_NAME], door_name[48];
                bipolar door;

                memory_copy(name, table->name[row], sizeof(name));
                name[sizeof(name) - 1] = 0;
                if (!owner || owner == WINDOWS_TAKING || !__atomic_load_n(&table->frame[row], __ATOMIC_ACQUIRE) ||
                    (pid ? owner != pid : !string_equals((string_address)name, who)))
                        continue;
                windows_place_set(table, row, &place);
                __atomic_add_fetch(&table->moved[row], 1, __ATOMIC_RELEASE);
                windows_door_name(door_name, row, 0);
                door = system_open_at(AT_FDCWD, (string_address)door_name, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
                if (door >= 0)
                {
                        system_write_once((b32)door, "", 1);
                        system_close(door);
                }
                moved++;
        }
        window_call(WINDOW_SYS_MUNMAP, mapped, (long)sizeof(struct windows), 0, 0, 0, 0);
        return_if(!moved, host_refuse("no window is %w: moonwater window lists them\n", writer_terminal_name, who));
        host_say(log, host_label "%p moved\n", moved);
        return 0;
}

static b32 host_window(string_address address_to arguments, positive count)
{
        static string_address environment[HOST_WINDOW_ENVIRONMENT + 2];
        static p8 entry[sizeof(WINDOW_PLACE_VARIABLE) + WINDOW_PLACE_TEXT];
        string_address address_to inherited;
        struct window_place place;
        positive at, used = 0, length = sizeof(WINDOW_PLACE_VARIABLE);
        int words = count > 2 ? window_place_parse(&place, (const char *const *)(arguments + 2), (unsigned int)(count - 2)) : 0;

        if (count > 2 && string_equals(arguments[2], "move"))
                return host_window_move(arguments, count);
        at = 2 + (positive)(words > 0 ? words : 0);
        if (words > 0 && at < count && string_equals(arguments[at], "--"))
                at++;
        //      A word that is wrong, words and no command, or a command with no words: the verb's page.
        return_if(count > 2 && (words <= 0 || at >= count), host_usage());
        host_need_root();
        return_if(!host_desktop_up(null),
                  host_refuse("the compositor's desktop is not up, so there is no window to put: moonwater desktop "
                              "on starts it\n"));
        if (count == 2)
                return host_window_list();

        memory_copy(entry, WINDOW_PLACE_VARIABLE "=", length);
        window_place_say(&place, (char *)entry + length, WINDOW_PLACE_TEXT);
        environment[used++] = entry;
        //      What a typed command is: asked for, so its first window takes the keys (window.c's windows_asked_take).
        environment[used++] = WINDOW_ASKED_VARIABLE "=1";
        //      One placement: one the caller already had is this one's now, and so is the mark.
        inherited = file_environment_all();
        for (positive variable = 0; inherited && inherited[variable]; variable++)
        {
                if (string_has_prefix(inherited[variable], WINDOW_PLACE_VARIABLE "=") ||
                    string_has_prefix(inherited[variable], WINDOW_ASKED_VARIABLE "="))
                        continue;
                return_if(used == HOST_WINDOW_ENVIRONMENT,
                          host_refuse("the environment has more than %p variables, which is more than this hands on\n",
                                      (positive)(HOST_WINDOW_ENVIRONMENT - 1)));
                environment[used++] = inherited[variable];
        }
        environment[used] = null;
        return process_tool_exec_environment("moonwater window", arguments + at, environment,
                                             file_environment((string_address) "PATH"));
}

/*
        moonwater clipboard [get|set|clear|watch] [primary] [TEXT]: the compositor's clipboard from the command line,
        root's alone -- a script's door to what a person copied is a door to their passwords, and the table it goes
        through is root's. Alone it says what each selection holds (its length and who copied it, never the text); get
        writes the text (exactly, to a file or a pipe; to a terminal with its controls escaped); set copies TEXT, or
        standard input, however long, as a copy of one text the compositor takes; clear empties both and the history
        (canvas clipboard history N, which nothing reads back yet); watch says a
        line each time something is copied. What is printed to a terminal is escaped: C0 and C1 controls, DEL and the
        bidirectional overrides are \xNN or \u{N}, so copied text cannot move the cursor, retitle the window or reorder
        the line it is read on, and a line of watch is at most CLIP_LINE characters.
*/
#define HOST_CLIP_LINE 120
#define HOST_CLIP_WAIT_MS 2000

// Text into the log as a terminal may be shown it: whole lines kept (lines false: a line end is \n too), at most most bytes.
static fn host_clip_escaped(const p8 address_to bytes, positive length, b32 lines, positive most)
{
        static const char digits[] = "0123456789abcdef";
        positive at = 0;

        while (at < length && at < most)
        {
                p8 out[16];
                positive put = 0, size = 1;
                p32 lead = bytes[at], code = lead;

                if (lead >= 0xc2 && lead <= 0xf4)
                {
                        // A valid UTF-8 sequence: a C1 control or a bidirectional control is spelled; the rest is shown.
                        // Invalid or truncated sequences fall back to a single byte escaped as \xNN so a malformed lead
                        // byte can never swallow a following control byte (such as ESC) into raw terminal output.
                        positive want = lead >= 0xf0 ? 4 : lead >= 0xe0 ? 3 : 2;
                        bool valid = at + want <= length;

                        if (valid)
                        {
                                p32 decoded = lead & (0x7fu >> want);

                                for (positive k = 1; k < want; k++)
                                {
                                        if ((bytes[at + k] & 0xc0) != 0x80)
                                        {
                                                valid = false;
                                                break;
                                        }
                                        decoded = (decoded << 6) | (bytes[at + k] & 0x3f);
                                }
                                if (valid &&
                                    ((want == 3 && decoded < 0x800) ||
                                     (want == 4 && (decoded < 0x10000 || decoded > 0x10ffff)) ||
                                     (decoded >= 0xd800 && decoded <= 0xdfff)))
                                        valid = false;
                                if (valid)
                                {
                                        size = want;
                                        code = decoded;
                                }
                        }
                }
                if (lead == '\n' && lines)
                        out[put++] = '\n';
                else if (lead == '\n' || lead == '\t')
                        out[put++] = '\\', out[put++] = lead == '\n' ? 'n' : 't';
                else if (lead < 0x20 || lead == 0x7f || (lead >= 0x80 && size == 1))
                        out[put++] = '\\', out[put++] = 'x', out[put++] = (p8)digits[lead >> 4], out[put++] = (p8)digits[lead & 15];
                else if ((code >= 0x80 && code < 0xa0) || (code >= 0x202a && code <= 0x202e) || (code >= 0x2066 && code <= 0x2069) ||
                         code == 0x200e || code == 0x200f || code == 0x061c)
                {
                        out[put++] = '\\', out[put++] = 'u', out[put++] = '{';
                        for (int shift = 12; shift >= 0; shift -= 4)
                                if ((code >> shift) || !shift)
                                        out[put++] = (p8)digits[code >> shift & 15];
                        out[put++] = '}';
                }
                else
                        for (positive k = 0; k < size; k++)
                                out[put++] = bytes[at + k];
                log((address_any)out, put);
                at += size;
        }
        if (length > most)
                log((address_any)"...", 3);
}

// A knock on the clipboard's door, and a wait for the compositor to answer: until done says so, or the wait is up.
static b32 host_clip_knock(const struct windows address_to table, b32 (*done)(const struct windows address_to, unsigned int),
                           unsigned int want)
{
        bipolar door = system_open_at(AT_FDCWD, (string_address)WINDOWS_CLIP_DOOR, O_WRONLY | O_NONBLOCK | O_CLOEXEC);

        if (door < 0)
                return false;
        system_write_once((b32)door, "", 1);
        system_close(door);
        for (positive waited = 0; waited < HOST_CLIP_WAIT_MS; waited += 10)
        {
                if (done(table, want))
                        return true;
                host_pause(10000000);
        }
        return done(table, want);
}

static b32 host_clip_answered(const struct windows address_to table, unsigned int serial)
{
        return (__atomic_load_n(&table->cli_answer, __ATOMIC_ACQUIRE) & ~WINDOWS_CLIP_NO) == serial;
}

static b32 host_clip_cleared(const struct windows address_to table, unsigned int ignored)
{
        (void)ignored;
        return !__atomic_load_n(&table->cli_clear, __ATOMIC_ACQUIRE);
}

// How many types a selection's copy has (0 for none, or a copy that cannot be read).
static positive host_clip_types(const struct windows address_to table, unsigned int which)
{
        const p8 address_to file = windows_clip_map(table->clip_pid, which, table->clip_length[which]);
        struct windows_clip_type type;
        positive count = 0, cursor = 0;

        while (file && windows_clip_next(file, table->clip_length[which], (unsigned long address_to)&cursor, &type))
                count++;
        if (file)
                window_call(WINDOW_SYS_MUNMAP, (long)file, (long)table->clip_length[which], 0, 0, 0, 0);
        return count;
}

// Who copied a selection: a window's title, the command line, or nobody now.
static fn host_clip_owner(const struct windows address_to table, unsigned int which)
{
        int owner = table->clip_owner[which];
        char title[WINDOWS_TITLE];

        if (owner == -2)
        {
                string_format(log, "the command line");
                return;
        }
        if (owner < 0 || owner >= WINDOWS || !table->pid[owner])
        {
                string_format(log, "a window that has gone");
                return;
        }
        memory_copy(title, table->title[owner], sizeof(title));
        title[sizeof(title) - 1] = 0;
        host_clip_escaped((const p8 address_to)title, string_length((string_address)title), false, WINDOWS_TITLE);
}

static b32 host_clipboard(string_address address_to arguments, positive count)
{
        static const char address_to const names[2] = {"clipboard", "primary"};
        static const named_byte clip_verbs[] = {
            {(string_address) "", 0},
            {(string_address) "get", 1},
            {(string_address) "set", 2},
            {(string_address) "clear", 3},
            {(string_address) "watch", 4},
        };
        struct windows address_to table;
        string_address verb = count > 2 ? arguments[2] : (string_address) "";
        positive at = 3;
        positive verb_at = string_table_lookup(verb, clip_verbs);
        p8 action;
        unsigned int which = WINDOWS_CLIP_CLIPBOARD;
        long mapped;
        b32 status = 0;

        if (verb_at == array_count(clip_verbs) && count > 2)
        {
                if (!string_equals(verb, "primary"))
                        return host_usage();
                verb_at = 0;
                at = 2;
        }
        action = clip_verbs[verb_at].value;
        if (at < count && string_equals(arguments[at], "primary"))
                which = WINDOWS_CLIP_PRIMARY, at++;
        return_if(at < count && action != 2, host_usage());
        host_need_root();
        mapped = host_windows_map(true);
        return_if(mapped < 0 && mapped > -4096,
                  host_refuse("the compositor's desktop is not up, so there is no clipboard: moonwater desktop on starts it\n"));
        table = (struct windows address_to)mapped;
        if (!table->clip_pid)
        {
                window_call(WINDOW_SYS_MUNMAP, mapped, (long)sizeof(struct windows), 0, 0, 0, 0);
                return host_refuse("this compositor keeps no clipboard\n");
        }

        if (action == 0)
        {
                for (unsigned int each = 0; each < 2; each++)
                {
                        string_format(log, "  %s: ", (string_address)names[each]);
                        if (!table->clip_length[each])
                                string_format(log, "empty\n");
                        else
                        {
                                string_format(log, "%p bytes of %p types, copied by ", (positive)table->clip_length[each], host_clip_types(table, each));
                                host_clip_owner(table, each);
                                string_format(log, "\n");
                        }
                }
                host_say(log, "  service %s\n", table->clip_on ? (string_address) "on" : (string_address) "off");
        }
        else if (action == 1)
        {
                p8 address_to bytes;
                positive length;
                long got = windows_clip_fetch(table->clip_pid, which, table->clip_length[which], &bytes, (unsigned long address_to)&length);

                if (got < 0)
                        status = host_refuse("the %s is empty\n", names[which]);
                else
                {
                        // A terminal is shown the text escaped; a file or a pipe is given it exactly, as copied.
                        if (isatty(1))
                        {
                                host_clip_escaped(bytes, length, true, length);
                                host_say(log, "\n");
                        }
                        else
                                system_write_all(1, bytes, length);
                        windows_clip_free(bytes, length);
                }
        }
        else if (action == 2)
        {
                static p8 input[65536];
                positive length = 0;
                long fd = window_call(WINDOW_SYS_MEMFD, (long)"moonwater-copy", WINDOWS_MFD_CLOEXEC | WINDOWS_MFD_SEALING, 0, 0, 0, 0), index = -1, text;
                unsigned int serial;
                int kept = fd >= 0; // 1 while the copy is whole, 0 when the memory was not there, 2 for no text

                // The words, or standard input, into the copy's memfd as they come: as long as the memory holds.
                for (positive word = at; kept && word < count; word++)
                {
                        kept = (word == at || windows_clip_put(fd, " ", 1)) && windows_clip_put(fd, arguments[word], string_length(arguments[word]));
                        length += string_length(arguments[word]) + (word > at);
                }
                for (bipolar got; kept && at >= count && (got = system_read_once(0, input, sizeof(input))) > 0; length += (positive)got)
                        kept = windows_clip_put(fd, input, (positive)got);
                text = kept && length ? window_call(WINDOW_SYS_MMAP, 0, (long)length, 1, 1, fd, 0) : -22;
                if (!(text < 0 && text > -4096))
                {
                        kept = windows_clip_text((const p8 address_to)text, length) ? kept : 2;
                        window_call(WINDOW_SYS_MUNMAP, text, (long)length, 0, 0, 0, 0);
                }
                if (kept == 1)
                        index = window_call(WINDOW_SYS_MEMFD, (long)"moonwater-index", WINDOWS_MFD_CLOEXEC, 0, 0, 0, 0);
                if (kept == 1 && length && !(index >= 0 && windows_clip_record(index, 0, length, WINDOWS_CLIP_TEXT, sizeof(WINDOWS_CLIP_TEXT) - 1) &&
                                                 windows_clip_seal(fd, index, length, 1)))
                        kept = 0;
                if (index >= 0)
                        window_call(WINDOW_SYS_CLOSE, index, 0, 0, 0, 0, 0);
                if (kept == 2)
                        status = host_refuse("not text a clipboard holds: UTF-8 with no NUL\n");
                else if (kept && !length)
                        status = host_refuse("nothing to copy\n");
                else if (!kept)
                        status = host_refuse("no memory for the copy\n");
                else
                {
                        table->cli_pid = (unsigned int)window_call(WINDOW_SYS_GETPID, 0, 0, 0, 0, 0, 0);
                        table->cli_fd = (unsigned int)fd;
                        table->cli_which = which;
                        serial = __atomic_add_fetch(&table->cli_serial, 1, __ATOMIC_RELEASE);
                        if (!host_clip_knock(table, host_clip_answered, serial))
                                status = host_refuse("the compositor did not answer\n");
                        else if (table->cli_answer & WINDOWS_CLIP_NO)
                                status = host_refuse("the compositor refused it: the clipboard is off (moonwater canvas clipboard)\n");
                        else
                                host_say(log, host_label "%p bytes copied to the %s\n", length, names[which]);
                }
                if (fd >= 0)
                        window_call(WINDOW_SYS_CLOSE, fd, 0, 0, 0, 0, 0);
        }
        else if (action == 3)
        {
                __atomic_store_n(&table->cli_clear, 1u, __ATOMIC_RELEASE);
                if (!host_clip_knock(table, host_clip_cleared, 0))
                        status = host_refuse("the compositor did not answer\n");
                else
                        host_say(log, host_label "the clipboard is empty\n");
        }
        else
        {
                unsigned int seen = table->clip_serial[which];

                // A line a copy, until the line cannot be written (the reader went) or the desktop goes.
                for (;;)
                {
                        timespec nap = {0, 100000000};
                        unsigned int now = __atomic_load_n(&table->clip_serial[which], __ATOMIC_ACQUIRE);

                        if (now != seen)
                        {
                                p8 address_to bytes;
                                positive length;

                                seen = now;
                                if (windows_clip_fetch(table->clip_pid, which, table->clip_length[which], &bytes,
                                                       (unsigned long address_to)&length) < 0)
                                        string_format(log, "%s emptied", (string_address)names[which]);
                                else
                                {
                                        string_format(log, "%s: %p bytes from ", (string_address)names[which], length);
                                        host_clip_owner(table, which);
                                        string_format(log, ": ");
                                        host_clip_escaped(bytes, length, false, HOST_CLIP_LINE);
                                        windows_clip_free(bytes, length);
                                }
                                string_format(log, "\n");
                                log_flush();
                        }
                        if (!__atomic_load_n(&table->clip_pid, __ATOMIC_ACQUIRE))
                                break;
                        system_call_2(syscall(nanosleep), (positive)address_of nap, 0);
                }
        }
        window_call(WINDOW_SYS_MUNMAP, mapped, (long)sizeof(struct windows), 0, 0, 0, 0);
        return status;
}

/*
        Every word the command answers to, and who answers. A verb that says
        what it does and nothing else (quiet) has its output set aside from
        its answer: standard output closed or full or a pipe whose reader has
        gone is no reason for `moonwater name` to say it failed after the name
        was kept, or for `status | head` to die of SIGPIPE. A verb that changes
        something (finish) is not killed by it either: it says a line while it
        works and keeps what it changed after, the settings' list and the file
        TLS reads after the line that names them, the device watcher after the
        lock, so a pipe closed at the line left the half it had done (`security
        block add KEY | true` was kept in the image and never published). Its
        answer is still what a lost write makes it, a status of 1, since what it
        printed may be what was asked for; a quiet verb's is not. A verb that
        starts a program puts the signal back before it execs it (host_run,
        host_shell_start, link_serve_start), and one that streams or waits on a
        person keeps it as it found it. The table is host_main's own, where the
        lane that works out which applets can start a program reads the
        handlers it names.
*/

static b32 host_main()
{
        static const struct
        {
                string_address verb;
                b32 (*run)(string_address address_to arguments, positive count);
                bool quiet;
                bool finish;
        } host_verbs[] = {
            {"status", host_status_verb, true},
            {"help", host_help, true},
            {"setup", host_setup, false, true},
            {"install", host_install_verb, false, true},
            // Reading needs nothing, and the kernel decides who may set a bound
            // event. init and exit still need root.
            {"init", host_settings_command, false, true},
            {"exit", host_settings_command, false, true},
            {"canvas", host_canvas, false},
            {"splash", host_splash, false, true},
            {"desktop", host_desktop, false},
            {"settings", host_settings_window, false},
            {"window", host_window, false},
            {"clipboard", host_clipboard, false},
            {"latency", host_latency, false},
            {"debug", host_debug, true},
            {"notices", host_notices, false, true},
            {"sensors", host_sensors, false},
            {"profile", host_profile, false},
            {"memory", host_memory, false},
            {"bios", host_bios, false},
            {"airplane", tune_airplane, false, true},
            {"brightness", tune_brightness, false, true},
            {"charge", tune_charge, false, true},
            {"battery", tune_battery_verb, false, true},
            {"power", tune_power, false, true},
            {"cpu", tune_cpu, false, true},
            {"sleep", tune_sleep_verb, false, true},
            {"hibernate", tune_hibernate_verb, false, true},
            {"wifi", host_wifi, false, true},
            {"bluetooth", host_bluetooth, false, true},
            {"priority", host_priority, false, true},
            {"wired", host_wired, false, true},
            {"device", host_device, false, true},
            {"timezone", host_timezone, false, true},
            {"ntp", host_ntp, false, true},
            {"keyboard", host_keyboard, false, true},
            {"mouse", host_setting_verb, false, true},
            {"language", host_setting_verb, false, true},
            {"taskbar", host_taskbar, false, true},
            {"audio", host_audio, false, true},
            {"time", host_time, false, true},
            {"name", host_name, true},
            {"dns", host_dns, false, true},
            {"network", host_network, false, true},
            {"security", host_security, false, true},
            {"workarounds", host_workarounds, false, true},
            {"link", link_main, false},
            {"ssh", ssh_main, false},
            {"wipe", host_wipe_verb, true},
            {"machine", host_machine_verb, false},
            {"rover", rover_kernel_verb, false},
            {"version", host_version_verb, true},
            {"boot", host_boot_verb, false},
            {"ask", host_ask_verb, false},
        };

        string_address address_to arguments = program_argument_list();
        positive count = (positive)program_argument_count();
        positive previous[4];
        positive which;
        bool ignored = false;
        b32 answer;

        //      The three standard descriptors are open before anything else is: with
        //      one closed, the first file this opens takes its number and what the
        //      command says goes into it (standard output closed, `bind init add`
        //      wrote its line into the machine's lock). A closed one is /dev/null.
        for (positive descriptor = 0; descriptor < 3; descriptor++)
                if (system_call_2(syscall(fcntl), descriptor, 1) < 0)
                        (void)system_open_at(AT_FDCWD, "/dev/null", FILE_READ_WRITE);

        host_verb = count > 1 ? arguments[1] : null;
        memory_zero(plain_streams, sizeof(plain_streams));
        if (!host_verb)
        {
                host_title(log);
                host_page_write(log);
                return 0;
        }

        //      canvas wallpaper picture: a file and a screen, which the plain row canvas wallpaper below would take for its word.
        if (count >= 4 && string_equals(arguments[1], "canvas") && string_equals(arguments[2], "wallpaper") && string_equals(arguments[3], "picture"))
                return locale_canvas_wallpaper_picture(arguments, count);

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

        //      the verb is device now: usb is refused in one line, and answers nothing else.
        if (string_equals(host_verb, "usb"))
        {
                host_say(log_error, host_label "moonwater usb is now moonwater device (try: moonwater device ...)\n");
                log_flush();
                return 2;
        }
        which = string_table_find(host_verb, host_verbs, sizeof(host_verbs[0]),
                                  array_count(host_verbs));
        return_if(which == array_count(host_verbs), host_usage());

        host_state_once = host_verbs[which].run != host_machine_verb;
        if (host_verbs[which].quiet || host_verbs[which].finish)
                ignored = system_signal_install(13, 1, 0, 0, previous);
        answer = host_verbs[which].run(arguments, count);
        if (host_verbs[which].quiet)
        {
                log_flush();
                log_failure_reset();
        }
        else if (host_verbs[which].finish)
                //      What was lost to a reader that went is flushed here, with the signal
                //      still ignored, and stays lost: the answer is the verb's own, and the
                //      status is the one a write that failed has always given.
                log_flush();
        if (ignored)
                system_signal_action(13, previous, null, 8);
        return answer;
}

#include "../rover/load.c" // the verb rover's kernel half: load, unload, status, state and watch, and the machine as the ring's consumer
#define MOONWATER_CLI
#include "../moonwater/moonwater.c"

#undef log
#undef log_error
