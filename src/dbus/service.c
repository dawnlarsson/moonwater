/*
        The session bus as a process: `sessionbus`, supervised by the machine script beside the compositor. It owns
        /run/moonwater/bus (open to every user, as the Wayland socket is; DBUS_SESSION_BUS_ADDRESS names it in the
        environment of a bowl's programs), and the Settings of the desktop's portal, which it keeps from the files in /root
        that Settings and `moonwater` write. It is a process of its own and not a part of the compositor on purpose: what it
        reads is whatever a bowl's program sends, and the compositor holds the card and every input device; and a text
        console, or a compositor that has just crashed, still has a bus and a portal.

        Beside the socket it keeps one small file, color-scheme, which says dark or light: what a launcher that starts a
        program in the scheme of the moment reads (a GTK 3 program takes GTK_THEME=Adwaita:dark; the portal is for
        programs that ask, and GTK 3 does not), the same word the portal gives, the clock's turn of auto included.

        What it serves, from which setting:
          org.freedesktop.appearance color-scheme    canvas theme (auto by the clock: light from 7 to 19)   1 dark, 2 light
          org.freedesktop.appearance accent-color    canvas theme accent, when one is kept (else out of range = none)
          org.freedesktop.appearance contrast        0 (no setting yet)
          org.gnome.desktop.interface color-scheme, gtk-theme, enable-animations (canvas motion), text-scaling-factor (1.0),
                                      cursor-size (canvas cursor size: 24 pixels times the percent over 100, 12 at least)
        and a change of any is told to the programs that listen (SettingChanged) within the time the loop takes to wake: at
        once for a file written, within 20 s for the clock turning auto from dark to light.

        The notifications programs post (org.freedesktop.Notifications, the portal's Notification) go on the desktop's
        board, where the notices popup and the tray's dot read them (sessionbus_notices).
*/
#ifndef SESSIONBUS_INCLUDED
#define SESSIONBUS_INCLUDED

#include "dbus.c"

#define SESSIONBUS_PATH "/run/moonwater/bus"
#define SESSIONBUS_BACKLOG 16
#define SESSIONBUS_ACCEPTS 8
#define SESSIONBUS_READ 65536
#define SESSIONBUS_READS 8
#define SESSIONBUS_TICK_MS 20000
#define SESSIONBUS_POLLIN 1
#define SESSIONBUS_POLLOUT 4
#define SESSIONBUS_POLLERR 8
#define SESSIONBUS_POLLHUP 16

struct sessionbus_poll
{
        int handle;
        short events, returned;
};

static struct
{
        int listen, inotify, signals;
        int fd[DBUS_CLIENTS];
        const char *root;       // where the settings are: /root, or what -r says
        char path[108];         // the socket
        int dirty;
        long last_settings;
        int auto_theme;
        // The settings' directory the watch is on, by its device and inode, and when it was last looked at (sessionbus_rewatch).
        unsigned long watched_ino;
        unsigned int watched_major, watched_minor;
        long last_rewatch;
} sessionbus = {-1, -1, -1, {0}, "/root", SESSIONBUS_PATH, 1, 0, 0, 0, 0, 0, 0};

static long sessionbus_call(positive number, positive a, positive b, positive c, positive d, positive e, positive f)
{
        return (long)system_call_6(number, a, b, c, d, e, f);
}

static int sessionbus_dup_fd(int fd)
{
        return (int)sessionbus_call(syscall(fcntl), (positive)fd, 1030, 0, 0, 0, 0); // F_DUPFD_CLOEXEC
}

static void sessionbus_close_fd(int fd)
{
        system_close(fd);
}

struct sessionbus_iovec
{
        void *base;
        unsigned long length;
};

struct sessionbus_msghdr
{
        void *name;
        unsigned int namelen;
        unsigned int pad0;
        struct sessionbus_iovec *iov;
        unsigned long iovlen;
        void *control;
        unsigned long controllen;
        int flags;
        int pad1;
};

struct sessionbus_cmsghdr
{
        unsigned long len;
        int level;
        int type;
};

static void *sessionbus_alloc(void *context, long bytes)
{
        long got;

        (void)context;
        if (bytes <= 0)
                return 0;
        got = sessionbus_call(syscall(mmap), 0, (positive)bytes, 3, 0x22, (positive)-1, 0);
        return got < 0 && got > -4096 ? 0 : (void *)got;
}

static void sessionbus_release(void *context, void *block, long bytes)
{
        (void)context;
        sessionbus_call(syscall(munmap), (positive)block, (positive)bytes, 0, 0, 0, 0);
}

static long sessionbus_now(void *context)
{
        (void)context;
        return (long)(clock_monotonic_nanoseconds() / 1000000ull);
}

static void sessionbus_random(void *context, unsigned char *bytes, long count)
{
        (void)context;
        if (sessionbus_call(syscall(getrandom), (positive)bytes, (positive)count, 0, 0, 0, 0) != count)
                for (long at = 0; at < count; at++)
                        bytes[at] = (unsigned char)(clock_monotonic_nanoseconds() >> (at & 7));
}

// -t: a line on standard error for each message routed, which is how to read what a program asks of the bus.
static void sessionbus_trace(void *context, const char *line)
{
        (void)context;
        string_format(log, (string_address) "%s\n", (string_address)line);
        log_flush();
}

/*
        The desktop's notifications (dbus.c's org.freedesktop.Notifications and the portal's) on the board, the compositor's table
        that the notices popup and the tray's dot read (window.c's windows_issue_set): one issue for each program's name, filed under
        that name, with the newest of its notifications as the detail, and cleared when the program has none left. Each is a
        warning, a critical one too: the tray's dot is lit by a notification as by any issue, and only its amber, so the red stays
        what the system says of itself (a program cannot make the desktop say it has an error). The code is
        WINDOWS_ISSUE_NOTIFICATION, a program's notification, whose message is its detail as it is.
        The table is root's (a memfd of the compositor's reached through its /proc, or /dev/spark's ioctl for a program of the
        desktop), and this process is root: a program of a bowl reaches the board only through the bus, and the bus's caps. The
        bus keeps DBUS_NOTICES, so the notifications take that many of the board's WINDOWS_ISSUES at most, whatever is posted. The
        table is made anew by a compositor that starts again, so it is mapped again when the file at its path is not the one
        mapped (by its inode), and nothing is written while there is none.
*/

static struct
{
        struct windows *table;
        long size;
        unsigned long inode;
        char key[DBUS_NOTICES][WINDOWS_ISSUE_KEY]; // the programs' names on the board now
} sessionbus_board;

// The board, mapped (again when the compositor's table there is another file than the one mapped), or null when there is none.
static struct windows *sessionbus_board_take(void)
{
        p8 record[256];
        unsigned long inode = 0, size = 0;
        long file = sessionbus_call(syscall(openat), (positive)AT_FDCWD, (positive)WINDOWS_TABLE, O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0, 0, 0), mapped;

        memory_zero(record, sizeof record);
        // statx of what was opened: its inode at 32 and its size at 40 of the kernel's fixed record
        if (file >= 0 && system_stat_at(file, (string_address) "", AT_EMPTY_PATH, 0x300, record) >= 0)
        {
                memory_copy(&inode, record + 32, sizeof inode);
                memory_copy(&size, record + 40, sizeof size);
        }
        if (sessionbus_board.table && (file < 0 || inode != sessionbus_board.inode))
        {
                sessionbus_call(syscall(munmap), (positive)sessionbus_board.table, (positive)sessionbus_board.size, 0, 0, 0, 0);
                sessionbus_board.table = null;
                memory_zero(sessionbus_board.key, sizeof sessionbus_board.key);
        }
        if (file >= 0 && !sessionbus_board.table && size >= sizeof(struct windows))
        {
                mapped = sessionbus_call(syscall(mmap), 0, (positive)size, 3, 1, (positive)file, 0); // PROT_READ | PROT_WRITE, MAP_SHARED
                if (mapped >= 0 || mapped <= -4096)
                        sessionbus_board.table = (struct windows *)mapped, sessionbus_board.size = (long)size, sessionbus_board.inode = inode;
        }
        if (file >= 0)
                system_close((int)file);
        return sessionbus_board.table;
}

static void sessionbus_notices(void *context, const struct dbus_notice *notice, long count)
{
        struct windows *table = sessionbus_board_take();
        char shown[DBUS_NOTICES][WINDOWS_ISSUE_KEY];

        (void)context;
        memory_zero(shown, sizeof shown);
        if (!table)
                return;
        for (long k = 0; k < count && k < DBUS_NOTICES; k++)
        {
                const struct dbus_notice *newest = &notice[k];
                unsigned int of = 0;
                int first = 1;

                if (!notice[k].id)
                        continue;
                for (long j = 0; j < count; j++)
                        if (notice[j].id && string_equals((string_address)notice[j].app, (string_address)notice[k].app))
                        {
                                of++;
                                first &= j >= k;
                                newest = notice[j].order > newest->order ? &notice[j] : newest;
                        }
                if (!first)
                        continue;
                string_copy_bounded((p8 *)shown[k], (string_address)notice[k].app, WINDOWS_ISSUE_KEY);
                windows_issue_set(table, shown[k], WINDOWS_ISSUE_WARNING, WINDOWS_ISSUE_NOTIFICATION, (unsigned int)newest->id, of, newest->text);
        }
        // a program that had notifications on the board and has none now
        for (long k = 0; k < DBUS_NOTICES; k++)
        {
                int still = 0;

                for (long j = 0; j < DBUS_NOTICES && sessionbus_board.key[k][0]; j++)
                        still |= string_equals((string_address)shown[j], (string_address)sessionbus_board.key[k]);
                if (sessionbus_board.key[k][0] && !still)
                        windows_issue_clear(table, sessionbus_board.key[k], WINDOWS_ISSUE_NOTIFICATION);
        }
        memory_copy(sessionbus_board.key, shown, sizeof shown);
}

/*
        The FileChooser portal's chooser (dbus.c's dbus_portal_choose): Files in pick mode, files -pick open|folder|save (src/canvas/ui/
        files.c), started on the desktop as the person's (MOONWATER_ASKED, so its window takes the keys), with the program's title, the
        name it suggests and the folder to start in. Its standard output is a pipe this reads until Files ends: what Files wrote there,
        each path ended by a NUL, is the request's answer (dbus_choice_done), and nothing is the chooser shut. One a client, by its slot;
        what Files writes is kept in memory that grows as it is written, so a folder's every file picked at once fits. A request let go
        (Request.Close, its program gone) ends its Files. Files runs as the desktop does, so what a program of a bowl is given is only what
        the person picked in it.
*/
#define SESSIONBUS_CHOOSER_ROOM 4096

static struct
{
        long pid, id;   // Files, and the id of the client it answers
        int out;        // the pipe's end Files writes to, -1 for none
        char *said;     // what it has written: used bytes of room
        long used, room;
} sessionbus_chooser[DBUS_CLIENTS];

static long sessionbus_choose(void *context, const struct dbus_choice *choice)
{
        long slot = choice->slot, child;
        int ends[2];
        string_address argv[12];
        positive count = 0;

        (void)context;
        if (slot < 0 || slot >= DBUS_CLIENTS || sessionbus_chooser[slot].out >= 0 ||
            sessionbus_call(syscall(pipe2), (positive)ends, O_CLOEXEC, 0, 0, 0, 0) < 0)
                return -1;
        argv[count++] = (string_address) "/files", argv[count++] = (string_address) "-pick";
        argv[count++] = (string_address)(choice->kind == DBUS_CHOOSE_SAVE ? "save" : choice->directory ? "folder" : "open");
        if (choice->multiple)
                argv[count++] = (string_address) "-multiple";
        if (choice->title[0])
                argv[count++] = (string_address) "-title", argv[count++] = (string_address)choice->title;
        if (choice->name[0])
                argv[count++] = (string_address) "-name", argv[count++] = (string_address)choice->name;
        if (choice->folder[0] == '/')
                argv[count++] = (string_address)choice->folder;
        argv[count] = null;
        child = system_fork();
        if (child == 0)
        {
                string_address address_to inherited = file_environment_all();
                positive have = 0, used = 0;
                long quiet = sessionbus_call(syscall(openat), (positive)AT_FDCWD, (positive) "/dev/null", O_RDWR | O_CLOEXEC, 0, 0, 0);

                while (inherited && inherited[have])
                        have++;
                string_address environment[have + 2];
                environment[used++] = (string_address) "MOONWATER_ASKED=1";
                for (positive at = 0; at < have; at++)
                        if (!string_has_prefix(inherited[at], "MOONWATER_ASKED="))
                                environment[used++] = inherited[at];
                environment[used] = null;
                sessionbus_call(syscall(dup3), (positive)ends[1], 1, 0, 0, 0, 0);
                if (quiet >= 0)
                        sessionbus_call(syscall(dup3), (positive)quiet, 0, 0, 0, 0, 0);
                sessionbus_call(syscall(setsid), 0, 0, 0, 0, 0, 0);
                system_execute(argv[0], argv, environment);
                sessionbus_call(syscall(exit_group), 127, 0, 0, 0, 0, 0);
        }
        system_close(ends[1]);
        if (child < 0)
                return system_close(ends[0]), -1;
        sessionbus_call(syscall(fcntl), (positive)ends[0], 4, 04000, 0, 0, 0); // F_SETFL O_NONBLOCK
        sessionbus_chooser[slot].pid = child, sessionbus_chooser[slot].id = choice->id;
        sessionbus_chooser[slot].out = ends[0], sessionbus_chooser[slot].used = 0;
        return 0;
}

// A chooser's Files let go of: its pipe closed and Files ended (it is reaped with the others, sessionbus_reap).
static void sessionbus_unchoose(void *context, long slot)
{
        (void)context;
        if (slot < 0 || slot >= DBUS_CLIENTS || sessionbus_chooser[slot].out < 0)
                return;
        sessionbus_call(syscall(kill), (positive)sessionbus_chooser[slot].pid, 15, 0, 0, 0, 0); // SIGTERM
        system_close(sessionbus_chooser[slot].out);
        sessionbus_chooser[slot].out = -1, sessionbus_chooser[slot].used = 0;
}

// What a chooser's Files wrote, read; at its end (Files gone) the request is answered with it.
static void sessionbus_chooser_read(long slot)
{
        long got;

        for (;;)
        {
                if (sessionbus_chooser[slot].used == sessionbus_chooser[slot].room)
                {
                        long room = sessionbus_chooser[slot].room ? 2 * sessionbus_chooser[slot].room : SESSIONBUS_CHOOSER_ROOM;
                        char *more = (char *)sessionbus_alloc(0, room);

                        if (!more)
                        {
                                dbus_choice_done(slot, sessionbus_chooser[slot].id, "", 0);
                                sessionbus_unchoose(0, slot);
                                return;
                        }
                        if (sessionbus_chooser[slot].said)
                        {
                                memory_copy(more, sessionbus_chooser[slot].said, (positive)sessionbus_chooser[slot].used);
                                sessionbus_release(0, sessionbus_chooser[slot].said, sessionbus_chooser[slot].room);
                        }
                        sessionbus_chooser[slot].said = more, sessionbus_chooser[slot].room = room;
                }
                got = (long)system_read_once(sessionbus_chooser[slot].out, sessionbus_chooser[slot].said + sessionbus_chooser[slot].used,
                                             (positive)(sessionbus_chooser[slot].room - sessionbus_chooser[slot].used));
                if (got > 0)
                {
                        sessionbus_chooser[slot].used += got;
                        continue;
                }
                if (got == -11) // EAGAIN: more to come
                        return;
                break;
        }
        system_close(sessionbus_chooser[slot].out);
        sessionbus_chooser[slot].out = -1;
        // Only what ends a path: a path cut short by Files ending as it wrote is not one.
        while (sessionbus_chooser[slot].used > 0 && sessionbus_chooser[slot].said[sessionbus_chooser[slot].used - 1])
                sessionbus_chooser[slot].used--;
        dbus_choice_done(slot, sessionbus_chooser[slot].id, sessionbus_chooser[slot].said, sessionbus_chooser[slot].used);
        sessionbus_chooser[slot].used = 0;
}

// Every chooser's Files that has ended, reaped.
static void sessionbus_reap(void)
{
        positive status;

        while (sessionbus_call(syscall(wait4), (positive)-1, (positive)address_of status, 1, 0, 0, 0) > 0) // WNOHANG
                ;
}

/*
        OpenURI's opener (dbus.c's dbus_portal_open): the open tool (src/canvas/ui/open.c) started on the desktop as the person's
        (MOONWATER_ASKED, so what it opens takes the keys), with -asked: what a program asks never starts a program file (an AppImage,
        an ELF, a desktop entry, a Windows program through Wine), which only the person opens. A descriptor's file is its path as this
        process reads it (/proc/self/fd), and OpenDirectory's the folder it is in, or itself where it is one.
*/
static long sessionbus_open(void *context, const char *target, long length, int fd, int directory)
{
        static char path[4096];
        string_address argv[5] = {(string_address) "/open", (string_address) "-asked", (string_address) "--", null, null};
        long child;

        (void)context, (void)length;
        if (!target)
        {
                char link[40] = "/proc/self/fd/";
                long got, n = 14, digits = 0;
                unsigned long value = (unsigned long)fd;
                char reversed[16];
                file_facts facts;

                do
                        reversed[digits++] = (char)('0' + value % 10), value /= 10;
                while (value && digits < (long)sizeof reversed);
                while (digits)
                        link[n++] = reversed[--digits];
                link[n] = 0;
                got = sessionbus_call(syscall(readlinkat), (positive)AT_FDCWD, (positive)link, (positive)path, sizeof path - 1, 0, 0);
                if (got <= 0 || path[0] != '/')
                        return -1;
                path[got] = 0;
                if (directory && (file_look_code(fd, (string_address) "", AT_EMPTY_PATH, &facts) < 0 || (facts.mode & 0170000u) != 0040000u))
                {
                        while (got > 1 && path[got - 1] != '/')
                                got--;
                        path[got > 1 ? got - 1 : 1] = 0;
                }
                target = path;
        }
        argv[3] = (string_address)target;
        child = system_fork();
        if (child == 0)
        {
                string_address address_to inherited = file_environment_all();
                positive have = 0, used = 0;
                long quiet = sessionbus_call(syscall(openat), (positive)AT_FDCWD, (positive) "/dev/null", O_RDWR | O_CLOEXEC, 0, 0, 0);

                while (inherited && inherited[have])
                        have++;
                string_address environment[have + 2];
                environment[used++] = (string_address) "MOONWATER_ASKED=1";
                for (positive at = 0; at < have; at++)
                        if (!string_has_prefix(inherited[at], "MOONWATER_ASKED="))
                                environment[used++] = inherited[at];
                environment[used] = null;
                if (quiet >= 0)
                        sessionbus_call(syscall(dup3), (positive)quiet, 0, 0, 0, 0, 0), sessionbus_call(syscall(dup3), (positive)quiet, 1, 0, 0, 0, 0);
                sessionbus_call(syscall(setsid), 0, 0, 0, 0, 0, 0);
                // This program's own binary as the open tool (argv[0] names the tool): no link of the image's is needed for it.
                system_execute((string_address) "/proc/self/exe", argv, environment);
                system_execute(argv[0], argv, environment);
                sessionbus_call(syscall(exit_group), 127, 0, 0, 0, 0, 0);
        }
        return child < 0 ? -1 : 0;
}

static struct dbus_host sessionbus_host = {0, sessionbus_alloc, sessionbus_release, sessionbus_now, sessionbus_random, 0, sessionbus_notices,
                                           sessionbus_choose, sessionbus_unchoose, sessionbus_open};

/* ---- the settings ---- */

// The first word of a file of the settings root, or "" when there is none: the file is `/root/NAME` of the registry's paths.
static void sessionbus_word(const char *path, char *word, unsigned room)
{
        char full[160];
        positive length = 0;
        const char *name = path + sizeof("/root") - 1;
        bipolar handle, got = 0, at = 0;

        if (room == 0)
                return;
        for (positive k = 0; sessionbus.root[k] && length + 1 < sizeof full; k++)
                full[length++] = sessionbus.root[k];
        for (positive k = 0; name[k] && length + 1 < sizeof full; k++)
                full[length++] = name[k];
        full[length] = 0;
        word[0] = 0;
        // not blocking: a FIFO (or a link to a device) where a setting is opens and reads at once with nothing, never holding the bus
        handle = system_open_at(AT_FDCWD, (string_address)full, FILE_READ | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
        if (handle >= 0)
        {
                got = system_read_once(handle, word, room - 1);
                system_close(handle);
        }
        while (at < got && word[at] != '\n' && word[at] != ' ' && word[at] != '\r')
                at++;
        word[at] = 0;
}

static unsigned long sessionbus_bits(double value)
{
        union
        {
                double real;
                unsigned long bits;
        } both;

        both.real = value;
        return both.bits;
}

// The word dark or light in color-scheme beside the socket, written whole under a name of its own and moved in, when it changed.
static void sessionbus_scheme_file(long dark)
{
        static long written = -1;
        char path[128], temporary[132];
        positive length = 0, slash = 0;
        bipolar handle;
        long put;

        if (written == dark)
                return;
        for (; sessionbus.path[length] && length < 100; length++)
                if (sessionbus.path[length] == '/')
                        slash = length + 1;
        for (positive k = 0; k < slash; k++)
                path[k] = sessionbus.path[k];
        for (positive k = 0; "color-scheme"[k]; k++)
                path[slash + k] = "color-scheme"[k];
        path[slash + 12] = 0;
        for (positive k = 0; k < slash + 13; k++)
                temporary[k] = path[k];
        temporary[slash + 12] = '.';
        temporary[slash + 13] = 0;
        handle = system_open_at_mode(AT_FDCWD, (string_address)temporary, FILE_WRITE | O_CLOEXEC | O_NOFOLLOW, 0644);
        if (handle < 0)
                return;
        //      A short or failed write is not renamed over the file: the file that is there stays as it was.
        put = (long)sessionbus_call(syscall(write), (positive)handle, (positive)(dark ? "dark\n" : "light\n"), dark ? 5 : 6, 0, 0, 0);
        system_close(handle);
        if (put != (dark ? 5 : 6) ||
            sessionbus_call(syscall(renameat2), (positive)AT_FDCWD, (positive)temporary, (positive)AT_FDCWD, (positive)path, 0, 0) < 0)
        {
                sessionbus_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)temporary, 0, 0, 0, 0);
                return;
        }
        written = dark;
}

// The portal's settings from the files, set (and so told) where they changed.
static void sessionbus_settings(void)
{
        char word[24];
        long dark, day = 1;
        unsigned long colour[3], scale[3] = {sessionbus_bits(1.0), 0, 0};
        long accent = -1;

        sessionbus_word(SETTING_THEME_PATH, word, sizeof word);
        sessionbus.auto_theme = string_equals((string_address)word, (string_address) "auto");
        if (sessionbus.auto_theme)
        {
                timespec real = {0, 0};
                tm broken;
                time_t seconds;

                clock_gettime(CLOCK_REALTIME, address_of real);
                seconds = real.tv_sec;
                if (localtime_r(&seconds, &broken))
                        day = broken.tm_hour >= SETTING_THEME_DAY_FROM && broken.tm_hour < SETTING_THEME_DAY_UNTIL;
                dark = !day;
        }
        else
                dark = string_equals((string_address)word, (string_address) "dark");
        sessionbus_word(SETTING_ACCENT_PATH, word, sizeof word);
        if (word[0] && !string_equals((string_address)word, (string_address) "reset") && !string_equals((string_address)word, (string_address) "default"))
                accent = window_colour_parse((string_address)word);
        // The accent, red green blue in 0 to 1; out of that range is none.
        colour[0] = sessionbus_bits(accent < 0 ? -1.0 : (double)((accent >> 16) & 255) / 255.0);
        colour[1] = sessionbus_bits(accent < 0 ? -1.0 : (double)((accent >> 8) & 255) / 255.0);
        colour[2] = sessionbus_bits(accent < 0 ? -1.0 : (double)(accent & 255) / 255.0);
        sessionbus_scheme_file(dark);
        dbus_setting_set("org.freedesktop.appearance", "color-scheme", 'u', dark ? 1 : 2, 0, 0);
        dbus_setting_set("org.freedesktop.appearance", "accent-color", 'c', 0, colour, 0);
        dbus_setting_set("org.freedesktop.appearance", "contrast", 'u', 0, 0, 0);
        dbus_setting_set("org.gnome.desktop.interface", "color-scheme", 's', 0, 0, dark ? "prefer-dark" : "prefer-light");
        dbus_setting_set("org.gnome.desktop.interface", "gtk-theme", 's', 0, 0, dark ? "Adwaita-dark" : "Adwaita");
        sessionbus_word(SETTING_MOTION_PATH, word, sizeof word);
        dbus_setting_set("org.gnome.desktop.interface", "enable-animations", 'b', !string_equals((string_address)word, (string_address) "reduced"), 0, 0);
        // The text's size, a percent from 50 to 300 in /root/canvas.text.scale: GNOME's factor is that over 100.
        sessionbus_word(SETTING_TEXT_SCALE_PATH, word, sizeof word);
        {
                long percent = 0;

                for (char *digit = word; *digit >= '0' && *digit <= '9' && percent < 1000; digit++)
                        percent = percent * 10 + (*digit - '0');
                scale[0] = sessionbus_bits(percent >= 50 && percent <= 300 ? (double)percent / 100.0 : 1.0);
        }
        dbus_setting_set("org.gnome.desktop.interface", "text-scaling-factor", 'd', 0, scale, 0);
        sessionbus_word(SETTING_CLOCK_FORMAT_PATH, word, sizeof word);
        dbus_setting_set("org.gnome.desktop.interface", "clock-format", 's', 0, 0, string_equals((string_address)word, (string_address) "12h") ? "12h" : "24h");
        sessionbus_word(SETTING_DATE_FORMAT_PATH, word, sizeof word);
        dbus_setting_set("org.gnome.desktop.interface", "clock-show-date", 'b', word[0] == 'y' || word[0] == 'd' || word[0] == 'm', 0, 0);
        // The pointer is the compositor's; this is the size a program that draws one of its own (a drag, a popup) takes: the 24 pixels
        // of a pointer at 100 percent times the percent the file says (80 when none is kept), to a whole pixel (setting_cursor_pixels).
        sessionbus_word(SETTING_CURSOR_SIZE_PATH, word, sizeof word);
        dbus_setting_set("org.gnome.desktop.interface", "cursor-size", 'i', setting_cursor_pixels((string_address)word), 0, 0);
        sessionbus.dirty = 0;
        sessionbus.last_settings = sessionbus_now(0);
}

/* ---- the socket ---- */

static void sessionbus_drop(long slot)
{
        if (dbus_dead(slot) && dbus_why(slot)[0])
        {
                string_format(log, (string_address) "sessionbus: a client was ended: %s\n", (string_address)dbus_why(slot));
                log_flush();
        }
        if (sessionbus.fd[slot] >= 0)
                system_close(sessionbus.fd[slot]);
        sessionbus.fd[slot] = -1;
        dbus_drop(slot);
}

static int sessionbus_listen(void)
{
        unsigned char address[110];
        long length = 0, got, old_mask, bound;

        memory_zero(address, sizeof address);
        address[0] = 1; // AF_UNIX
        while (sessionbus.path[length] && length < 100)
        {
                address[2 + length] = (unsigned char)sessionbus.path[length];
                length++;
        }
        if (sessionbus.path[length])
                return -1;
        sessionbus_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)sessionbus.path, 0, 0, 0, 0);
        got = sessionbus_call(syscall(socket), 1, 1 | 0x800 | 0x80000, 0, 0, 0, 0); // SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC
        if (got < 0)
                return -1;
        // Set the socket mode atomically at bind() via umask instead of a post-bind fchmodat() symlink race.
        old_mask = sessionbus_call(syscall(umask), 0111, 0, 0, 0, 0, 0);
        bound = sessionbus_call(syscall(bind), (positive)got, (positive)address, (positive)(2 + length + 1), 0, 0, 0);
        sessionbus_call(syscall(umask), (positive)old_mask, 0, 0, 0, 0, 0);
        if (bound < 0 || sessionbus_call(syscall(listen), (positive)got, SESSIONBUS_BACKLOG, 0, 0, 0, 0) < 0)
        {
                system_close((int)got);
                return -1;
        }
        sessionbus.listen = (int)got;
        return 0;
}

static long sessionbus_peer_uid(long pid, long uid)
{
        char path[48], text[4096];
        long pos = 0, got = 0;
        bipolar handle;

        if (pid <= 0)
                return uid;
        memory_copy_apart(path, "/proc/", 6);
        pos = 6;
        pos += (long)positive_into_string((p8 *)(path + pos), (positive)pid);
        memory_copy_apart(path + pos, "/uid_map", 9);
        handle = system_open_at(AT_FDCWD, (string_address)path, FILE_READ | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
        if (handle < 0)
                return uid;
        while (got < (long)sizeof text)
        {
                bipolar part = system_read_once(handle, text + got, (positive)((long)sizeof text - got));

                if (part <= 0)
                        break;
                got += (long)part;
        }
        system_close(handle);
        if (got <= 0)
                return uid;
        return dbus_uid_from_map(text, got, uid);
}

static void sessionbus_accept(void)
{
        for (int tries = 0; tries < SESSIONBUS_ACCEPTS; tries++)
        {
                int credentials[3] = {-1, -1, -1};
                unsigned int size = sizeof credentials;
                long fd = sessionbus_call(syscall(accept4), (positive)sessionbus.listen, 0, 0, 0x800 | 0x80000, 0, 0), slot;

                if (fd < 0)
                        return;
                // SOL_SOCKET, SO_PEERCRED: the kernel's word for who is on the other end.
                if (sessionbus_call(syscall(getsockopt), (positive)fd, 1, 17, (positive)credentials, (positive)&size, 0) < 0 ||
                    size != sizeof credentials || credentials[1] == -1)
                {
                        system_close((int)fd);
                        continue;
                }
                slot = dbus_connect(credentials[0], sessionbus_peer_uid(credentials[0], (unsigned int)credentials[1]));
                if (slot < 0)
                {
                        system_close((int)fd);
                        continue;
                }
                sessionbus.fd[slot] = (int)fd;
        }
}

static void sessionbus_read(long slot)
{
        for (int reads = 0; reads < SESSIONBUS_READS; reads++)
        {
                long room, got, asked;
                unsigned char *to = dbus_in_room(slot, &room);
                unsigned char ctrl[16 + sizeof(int) * DBUS_FDS + 16] __attribute__((aligned(8)));
                struct sessionbus_iovec iov;
                struct sessionbus_msghdr msg;

                if (!to || room <= 0)
                        return;
                asked = room > SESSIONBUS_READ ? SESSIONBUS_READ : room;
                iov.base = to;
                iov.length = (unsigned long)asked;
                msg.name = 0;
                msg.namelen = 0;
                msg.pad0 = 0;
                msg.iov = &iov;
                msg.iovlen = 1;
                msg.control = ctrl;
                msg.controllen = sizeof ctrl;
                msg.flags = 0;
                msg.pad1 = 0;
                // MSG_DONTWAIT | MSG_CMSG_CLOEXEC
                got = sessionbus_call(syscall(recvmsg), (positive)sessionbus.fd[slot], (positive)&msg, 0x40 | 0x40000000, 0, 0, 0);
                if (got == -11 || got == -4)
                        return;
                if (got <= 0)
                {
                        sessionbus_drop(slot);
                        return;
                }
                for (unsigned long off = 0; off + sizeof(struct sessionbus_cmsghdr) <= msg.controllen;)
                {
                        const struct sessionbus_cmsghdr *cm = (const struct sessionbus_cmsghdr *)(ctrl + off);

                        if (cm->len < sizeof *cm || cm->len > msg.controllen - off)
                                break;
                        if (cm->level == 1 && cm->type == 1)
                        {
                                long nfds = (long)(cm->len - sizeof *cm) / (long)sizeof(int);
                                const int *fds = (const int *)(ctrl + off + sizeof *cm);

                                dbus_in_fds(slot, fds, nfds);
                        }
                        off += (cm->len + 7ul) & ~7ul;
                }
                if (msg.flags & 0x8) // MSG_CTRUNC: control data (fds) was truncated by the kernel
                {
                        sessionbus_drop(slot);
                        return;
                }
                dbus_in_done(slot, got);
                // a read that came short has emptied the socket: asking again would only be told EAGAIN (the poll says when there is more)
                if (dbus_dead(slot) || got < asked)
                        return;
        }
}

// What the bus queued for a client, written; what the socket takes is taken off the queue.
static void sessionbus_flush(long slot)
{
        const unsigned char *bytes;
        const int *fds;
        long length, nfds, got;

        if (sessionbus.fd[slot] < 0)
                return;
        if (dbus_dead(slot))
        {
                sessionbus_drop(slot);
                return;
        }
        length = dbus_out_fds(slot, &bytes, &fds, &nfds);
        if (!length)
                return;
        if (nfds > 0)
        {
                unsigned char ctrl[16 + sizeof(int) * DBUS_FDS + 8] __attribute__((aligned(8)));
                struct sessionbus_cmsghdr *cm = (struct sessionbus_cmsghdr *)ctrl;
                struct sessionbus_iovec iov;
                struct sessionbus_msghdr msg;
                unsigned long clen;
                int *dst = (int *)(ctrl + sizeof(struct sessionbus_cmsghdr));

                if (nfds > DBUS_FDS)
                {
                        sessionbus_drop(slot);
                        return;
                }
                clen = sizeof(struct sessionbus_cmsghdr) + (unsigned long)nfds * sizeof(int);
                memory_zero(ctrl, sizeof ctrl);
                cm->len = clen;
                cm->level = 1;
                cm->type = 1;
                memory_copy_apart(dst, fds, (unsigned long)nfds * sizeof(int));
                iov.base = (void *)bytes;
                iov.length = (unsigned long)length;
                msg.name = 0;
                msg.namelen = 0;
                msg.pad0 = 0;
                msg.iov = &iov;
                msg.iovlen = 1;
                msg.control = ctrl;
                msg.controllen = (clen + 7ul) & ~7ul;
                msg.flags = 0;
                msg.pad1 = 0;
                // MSG_DONTWAIT | MSG_NOSIGNAL
                got = sessionbus_call(syscall(sendmsg), (positive)sessionbus.fd[slot], (positive)&msg, 0x40 | 0x4000, 0, 0, 0);
                if (got > 0)
                {
                        dbus_sent_fds(slot);
                        dbus_sent(slot, got);
                }
                else if (got != -11 && got != -4)
                        sessionbus_drop(slot);
                return;
        }
        // MSG_DONTWAIT | MSG_NOSIGNAL
        got = sessionbus_call(syscall(sendto), (positive)sessionbus.fd[slot], (positive)bytes, (positive)length, 0x40 | 0x4000, 0, 0);
        if (got > 0)
                dbus_sent(slot, got);
        else if (got != -11 && got != -4)
                sessionbus_drop(slot);
}

// The settings' directory as it is now: its inode and device (statx's stx_ino at 32, stx_dev_major at 136 and minor at 140), 0 when it is not.
static unsigned long sessionbus_root_identity(unsigned int *major, unsigned int *minor)
{
        p8 facts[256];
        unsigned long ino = 0;

        *major = *minor = 0;
        memory_zero(facts, sizeof facts);
        if (system_stat_at(AT_FDCWD, (string_address)sessionbus.root, 0, 0x100u, facts) < 0) // STATX_INO
                return 0;
        memory_copy(major, facts + 136, sizeof *major);
        memory_copy(minor, facts + 140, sizeof *minor);
        memory_copy(&ino, facts + 32, sizeof ino);
        return ino;
}

static void sessionbus_watch(void)
{
        sessionbus.watched_ino = sessionbus_root_identity(&sessionbus.watched_major, &sessionbus.watched_minor);
        sessionbus.inotify = (int)sessionbus_call(syscall(inotify_init1), 0x800 | 0x80000, 0, 0, 0, 0, 0);
        if (sessionbus.inotify >= 0 &&
            sessionbus_call(syscall(inotify_add_watch), (positive)sessionbus.inotify, (positive)sessionbus.root,
                            0x8u | 0x100u | 0x80u, 0, 0, 0) < 0)
        {
                system_close(sessionbus.inotify);
                sessionbus.inotify = -1;
        }
}

/*
        The watch made again where the settings' directory is another one than the one watched: an installed machine's /root is its data
        partition, mounted over the root file system's /root, and the bus that started before the mount watched the directory under it, read
        there no canvas.theme (so dark), and never heard a change of the files Settings writes on the mounted one: the portal said dark to every
        browser on a light desktop (box, image e4c4d0e2a: /root/canvas.theme light, the bus's color-scheme 1, its watch on device 3 where /root
        was the NVMe partition's). Looked at once a second; a directory that changed is watched anew and its settings read again, which tells
        the programs (SettingChanged) what changed.
*/
static void sessionbus_rewatch(long now)
{
        unsigned int major, minor;
        unsigned long ino;

        if (now - sessionbus.last_rewatch < 1000)
                return;
        sessionbus.last_rewatch = now;
        ino = sessionbus_root_identity(&major, &minor);
        if (ino == sessionbus.watched_ino && major == sessionbus.watched_major && minor == sessionbus.watched_minor)
                return;
        if (sessionbus.inotify >= 0)
                system_close(sessionbus.inotify);
        sessionbus.inotify = -1;
        sessionbus_watch();
        sessionbus.dirty = 1;
}

static void sessionbus_drain_watch(void)
{
        unsigned int buffer[256];
        long got;

        while ((got = sessionbus_call(syscall(read), (positive)sessionbus.inotify, (positive)buffer, sizeof buffer, 0, 0, 0)) > 0)
                sessionbus.dirty = 1;
}

#include "../cast/service.c" // the capture engine's half of this process: the ScreenCast portal's host and its PipeWire

static b32 sessionbus_main()
{
        unsigned long mask = 1ul << (15 - 1) | 1ul << (2 - 1) | 1ul << (1 - 1);
        const char *claims[8];
        int claimed = 0;

        for (positive at = 1; at < (positive)program_argument_count(); at++)
        {
                static const struct
                {
                        string_address name;
                        p8 kind, needs_arg;
                } bus_opts[] = {
                    {(string_address) "-s", 1, 1}, {(string_address) "-r", 2, 1},
                    {(string_address) "-t", 3, 0}, {(string_address) "-i", 4, 1},
                };
                string_address option = program_argument(at);
                positive idx = string_table_lookup(option, bus_opts);

                if (idx >= array_count(bus_opts) || (bus_opts[idx].needs_arg && at + 1 >= (positive)program_argument_count()))
                {
                        string_format(log, (string_address) "sessionbus: [-t] [-i INTERFACE=VERSION] [-s SOCKET] [-r SETTINGS-DIRECTORY]\n");
                        log_flush();
                        return 2;
                }
                switch (bus_opts[idx].kind)
                {
                case 1:
                        string_copy_bounded((p8 *)sessionbus.path, (string_address)program_argument(++at), sizeof sessionbus.path);
                        break;
                case 2:
                        sessionbus.root = (const char *)program_argument(++at);
                        break;
                case 3:
                        sessionbus_host.trace = sessionbus_trace;
                        break;
                case 4:
                        claims[claimed++ & 7] = (const char *)program_argument(++at);
                        break;
                }
        }
        for (int at = 0; at < DBUS_CLIENTS; at++)
                sessionbus.fd[at] = -1, sessionbus_chooser[at].out = -1;
        dbus_set_fd_ops(sessionbus_dup_fd, sessionbus_close_fd);
        if (dbus_start(&sessionbus_host) < 0 || sessionbus_listen() < 0)
        {
                string_format(log, (string_address) "sessionbus: cannot listen on %s\n", (string_address)sessionbus.path);
                log_flush();
                return 1;
        }
        castbus_start();
        sessionbus_call(syscall(rt_sigprocmask), 0, (positive)address_of mask, 0, 8, 0, 0);
        sessionbus.signals = (int)sessionbus_call(syscall(signalfd4), (positive)-1, (positive)address_of mask, 8, 0x800 | 0x80000, 0, 0);
        system_signal_install(13, 1, 0, 0, null); // SIGPIPE: a write to a program that went is an error to see, not an end
        for (int at = 0; at < claimed && at < 8; at++)
        {
                // -i NAME=VERSION: an interface of the portal answers its version too
                const char *spec = claims[at];
                char name[56];
                unsigned version = 0;
                int length = 0;

                while (spec[length] && spec[length] != '=' && length < 55)
                {
                        name[length] = spec[length];
                        length++;
                }
                name[length] = 0;
                for (const char *digit = spec[length] == '=' ? spec + length + 1 : spec + length; *digit >= '0' && *digit <= '9'; digit++)
                        version = version * 10 + (unsigned)(*digit - '0');
                dbus_interface_claim(name, version);
        }
        // Files is the chooser of FileChooser's requests, so the portal answers the version a browser asks for before it uses one (4:
        // OpenFile's directory and current_folder are read).
        dbus_interface_claim("org.freedesktop.portal.FileChooser", 4);
        // OpenURI is the open tool's: a page, a program's link, a file or its folder, opened with what opens it (version 4: OpenFile's
        // and OpenDirectory's descriptors, the activation token read and let be).
        dbus_interface_claim("org.freedesktop.portal.OpenURI", 4);
        sessionbus_watch();
        sessionbus_settings();
#ifdef STANDARD_MODERN_C_NET_CUPS_SHIM
        bipolar cups_listen = -1;
        cups_shim_state cups_state;
        if (string_equals((string_address)sessionbus.path, (string_address)"/run/moonwater/bus"))
        {
                cups_shim_init(address_of cups_state, (string_address)CUPS_PRINTERS_PATH, (string_address)CUPS_DEFAULT_PATH);
                cups_listen = cups_shim_listen((string_address)CUPS_SHIM_SOCKET);
        }
#endif
        for (;;)
        {
                struct sessionbus_poll watch[4 + 2 * DBUS_CLIENTS + CASTBUS_POLLS];
                long owner[4 + 2 * DBUS_CLIENTS + CASTBUS_POLLS], count = 0;
                long wait = castbus_wait(dbus_wait(1000)); // a second, or less when the bus has a deadline sooner (a portal Response it holds)
                positive timeout[2] = {(positive)(wait / 1000), (positive)(wait % 1000) * 1000000};
                long now;

                watch[count] = (struct sessionbus_poll){sessionbus.listen, SESSIONBUS_POLLIN, 0};
                owner[count++] = -1;
                if (sessionbus.signals >= 0)
                {
                        watch[count] = (struct sessionbus_poll){sessionbus.signals, SESSIONBUS_POLLIN, 0};
                        owner[count++] = -2;
                }
                if (sessionbus.inotify >= 0)
                {
                        watch[count] = (struct sessionbus_poll){sessionbus.inotify, SESSIONBUS_POLLIN, 0};
                        owner[count++] = -3;
                }
#ifdef STANDARD_MODERN_C_NET_CUPS_SHIM
                if (cups_listen >= 0)
                {
                        watch[count] = (struct sessionbus_poll){(int)cups_listen, SESSIONBUS_POLLIN, 0};
                        owner[count++] = -999;
                }
#endif
                for (long slot = 0; slot < DBUS_CLIENTS; slot++)
                        if (sessionbus.fd[slot] >= 0)
                        {
                                const unsigned char *bytes;

                                watch[count] = (struct sessionbus_poll){sessionbus.fd[slot],
                                                                         (short)(SESSIONBUS_POLLIN | (dbus_out(slot, &bytes) ? SESSIONBUS_POLLOUT : 0)), 0};
                                owner[count++] = slot;
                        }
                for (long slot = 0; slot < DBUS_CLIENTS; slot++)
                        if (sessionbus_chooser[slot].out >= 0)
                        {
                                watch[count] = (struct sessionbus_poll){sessionbus_chooser[slot].out, SESSIONBUS_POLLIN, 0};
                                owner[count++] = -4 - slot;
                        }
                count += castbus_watch(watch + count, owner + count, CASTBUS_OWNER);
                sessionbus_call(syscall(ppoll), (positive)watch, (positive)count, (positive)timeout, 0, 0, 0);
                for (long at = 0; at < count; at++)
                {
                        if (!watch[at].returned)
                                continue;
                        if (owner[at] == -1)
                                sessionbus_accept();
                        else if (owner[at] == -2)
                                goto stop;
                        else if (owner[at] == -3)
                                sessionbus_drain_watch();
#ifdef STANDARD_MODERN_C_NET_CUPS_SHIM
                        else if (owner[at] == -999)
                        {
                                bipolar cfd = socket_accept((b32)cups_listen, null, null, SOCK_CLOEXEC);

                                if (cfd >= 0)
                                {
                                        cups_shim_serve_client(address_of cups_state, (b32)cfd);
                                        socket_close((b32)cfd);
                                }
                        }
#endif
                        else if (owner[at] <= CASTBUS_OWNER)
                                castbus_event(CASTBUS_OWNER, owner[at], watch[at].returned);
                        else if (owner[at] <= -4)
                        {
                                if (sessionbus_chooser[-4 - owner[at]].out >= 0)
                                        sessionbus_chooser_read(-4 - owner[at]);
                        }
                        else if (sessionbus.fd[owner[at]] >= 0)
                        {
                                if (watch[at].returned & (SESSIONBUS_POLLIN | SESSIONBUS_POLLHUP | SESSIONBUS_POLLERR))
                                        sessionbus_read(owner[at]);
                                if (sessionbus.fd[owner[at]] >= 0 && (watch[at].returned & (SESSIONBUS_POLLERR | SESSIONBUS_POLLHUP)) &&
                                    !(watch[at].returned & SESSIONBUS_POLLIN))
                                        sessionbus_drop(owner[at]);
                        }
                }
                now = sessionbus_now(0);
                sessionbus_reap();
                sessionbus_rewatch(now);
                if (sessionbus.dirty || (sessionbus.auto_theme && now - sessionbus.last_settings > SESSIONBUS_TICK_MS) ||
                    (sessionbus.inotify < 0 && now - sessionbus.last_settings > 1000))
                        sessionbus_settings();
                dbus_tick();
                castbus_tick();
                for (long slot = 0; slot < DBUS_CLIENTS; slot++)
                        sessionbus_flush(slot);
        }
stop:
#ifdef STANDARD_MODERN_C_NET_CUPS_SHIM
        if (cups_listen >= 0)
        {
                socket_close((b32)cups_listen);
                sessionbus_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)CUPS_SHIM_SOCKET, 0, 0, 0, 0);
        }
#endif
        for (long slot = 0; slot < DBUS_CLIENTS; slot++)
                sessionbus_drop(slot);
        if (sessionbus.signals >= 0)
                system_close(sessionbus.signals);
        if (sessionbus.inotify >= 0)
                system_close(sessionbus.inotify);
        system_close(sessionbus.listen);
        sessionbus_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)sessionbus.path, 0, 0, 0, 0);
        dbus_stop();
        return 0;
}

#endif /* SESSIONBUS_INCLUDED */
