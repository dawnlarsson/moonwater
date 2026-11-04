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
                                      cursor-size (canvas cursor size: 24 pixels a step, 24 for auto)
        and a change of any is told to the programs that listen (SettingChanged) within the time the loop takes to wake: at
        once for a file written, within 20 s for the clock turning auto from dark to light.
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
} sessionbus = {-1, -1, -1, {0}, "/root", SESSIONBUS_PATH, 1, 0, 0};

static long sessionbus_call(positive number, positive a, positive b, positive c, positive d, positive e, positive f)
{
        return (long)system_call_6(number, a, b, c, d, e, f);
}

static void *sessionbus_alloc(void *context, long bytes)
{
        long got = sessionbus_call(syscall(mmap), 0, (positive)bytes, 3, 0x22, (positive)-1, 0);

        (void)context;
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

static struct dbus_host sessionbus_host = {0, sessionbus_alloc, sessionbus_release, sessionbus_now, sessionbus_random, 0};

/* ---- the settings ---- */

// The first word of a file of the settings root, or "" when there is none: the file is `/root/NAME` of the registry's paths.
static void sessionbus_word(const char *path, char *word, unsigned room)
{
        char full[160];
        positive length = 0;
        const char *name = path + sizeof("/root") - 1;
        bipolar handle, got = 0, at = 0;

        for (positive k = 0; sessionbus.root[k] && length + 1 < sizeof full; k++)
                full[length++] = sessionbus.root[k];
        for (positive k = 0; name[k] && length + 1 < sizeof full; k++)
                full[length++] = name[k];
        full[length] = 0;
        word[0] = 0;
        // not blocking: a FIFO (or a link to a device) where a setting is opens and reads at once with nothing, never holding the bus
        handle = system_open_at(AT_FDCWD, (string_address)full, FILE_READ | O_NONBLOCK | O_CLOEXEC);
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
        sessionbus_call(syscall(write), (positive)handle, (positive)(dark ? "dark\n" : "light\n"), dark ? 5 : 6, 0, 0, 0);
        system_close(handle);
        if (sessionbus_call(syscall(renameat2), (positive)AT_FDCWD, (positive)temporary, (positive)AT_FDCWD, (positive)path, 0, 0) >= 0)
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
                dark = !string_equals((string_address)word, (string_address) "light");
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
        // The pointer is the compositor's; this is the size a program that draws one of its own (a drag, a popup) takes.
        sessionbus_word(SETTING_CURSOR_SIZE_PATH, word, sizeof word);
        dbus_setting_set("org.gnome.desktop.interface", "cursor-size", 'i', word[0] >= '1' && word[0] <= '4' && !word[1] ? 24 * (word[0] - '0') : 24, 0, 0);
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
        long length = 0, got;

        memory_zero(address, sizeof address);
        address[0] = 1; // AF_UNIX
        while (sessionbus.path[length] && length < 100)
        {
                address[2 + length] = (unsigned char)sessionbus.path[length];
                length++;
        }
        sessionbus_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)sessionbus.path, 0, 0, 0, 0);
        got = sessionbus_call(syscall(socket), 1, 1 | 0x800 | 0x80000, 0, 0, 0, 0); // SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC
        if (got < 0)
                return -1;
        if (sessionbus_call(syscall(bind), (positive)got, (positive)address, (positive)(2 + length + 1), 0, 0, 0) < 0 ||
            sessionbus_call(syscall(listen), (positive)got, SESSIONBUS_BACKLOG, 0, 0, 0, 0) < 0)
        {
                system_close((int)got);
                return -1;
        }
        sessionbus_call(syscall(fchmodat), (positive)AT_FDCWD, (positive)sessionbus.path, 0666, 0, 0, 0);
        sessionbus.listen = (int)got;
        return 0;
}

static void sessionbus_accept(void)
{
        for (int tries = 0; tries < SESSIONBUS_ACCEPTS; tries++)
        {
                int credentials[3] = {0, 0, 0};
                unsigned int size = sizeof credentials;
                long fd = sessionbus_call(syscall(accept4), (positive)sessionbus.listen, 0, 0, 0x800 | 0x80000, 0, 0), slot;

                if (fd < 0)
                        return;
                // SOL_SOCKET, SO_PEERCRED: the kernel's word for who is on the other end.
                if (sessionbus_call(syscall(getsockopt), (positive)fd, 1, 17, (positive)credentials, (positive)&size, 0) < 0)
                {
                        system_close((int)fd);
                        continue;
                }
                slot = dbus_connect(credentials[0], credentials[1]);
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

                if (!to || room <= 0)
                        return;
                asked = room > SESSIONBUS_READ ? SESSIONBUS_READ : room;
                got = sessionbus_call(syscall(read), (positive)sessionbus.fd[slot], (positive)to, (positive)asked, 0, 0, 0);
                if (got == -11)
                        return;
                if (got <= 0)
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
        long length, got;

        if (sessionbus.fd[slot] < 0)
                return;
        if (dbus_dead(slot))
        {
                sessionbus_drop(slot);
                return;
        }
        length = dbus_out(slot, &bytes);
        if (!length)
                return;
        // MSG_DONTWAIT | MSG_NOSIGNAL
        got = sessionbus_call(syscall(sendto), (positive)sessionbus.fd[slot], (positive)bytes, (positive)length, 0x40 | 0x4000, 0, 0);
        if (got > 0)
                dbus_sent(slot, got);
        else if (got != -11)
                sessionbus_drop(slot);
}

static void sessionbus_watch(void)
{
        sessionbus.inotify = (int)sessionbus_call(syscall(inotify_init1), 0x800 | 0x80000, 0, 0, 0, 0, 0);
        if (sessionbus.inotify >= 0 &&
            sessionbus_call(syscall(inotify_add_watch), (positive)sessionbus.inotify, (positive)sessionbus.root,
                            0x8u | 0x100u | 0x80u, 0, 0, 0) < 0)
        {
                system_close(sessionbus.inotify);
                sessionbus.inotify = -1;
        }
}

static void sessionbus_drain_watch(void)
{
        unsigned int buffer[256];
        long got;

        while ((got = sessionbus_call(syscall(read), (positive)sessionbus.inotify, (positive)buffer, sizeof buffer, 0, 0, 0)) > 0)
                sessionbus.dirty = 1;
}

static b32 sessionbus_main()
{
        unsigned long mask = 1ul << (15 - 1) | 1ul << (2 - 1) | 1ul << (1 - 1);
        const char *claims[8];
        int claimed = 0;

        for (positive at = 1; at < (positive)program_argument_count(); at++)
        {
                string_address option = program_argument(at);

                if (option[0] == '-' && option[1] == 's' && !option[2] && at + 1 < (positive)program_argument_count())
                        string_copy_bounded((p8 *)sessionbus.path, (string_address)program_argument(++at), sizeof sessionbus.path);
                else if (option[0] == '-' && option[1] == 'r' && !option[2] && at + 1 < (positive)program_argument_count())
                        sessionbus.root = (const char *)program_argument(++at);
                else if (option[0] == '-' && option[1] == 't' && !option[2])
                        sessionbus_host.trace = sessionbus_trace;
                else if (option[0] == '-' && option[1] == 'i' && !option[2] && at + 1 < (positive)program_argument_count())
                        claims[claimed++ & 7] = (const char *)program_argument(++at);
                else
                {
                        string_format(log, (string_address) "sessionbus: [-t] [-i INTERFACE=VERSION] [-s SOCKET] [-r SETTINGS-DIRECTORY]\n");
                        log_flush();
                        return 2;
                }
        }
        for (int at = 0; at < DBUS_CLIENTS; at++)
                sessionbus.fd[at] = -1;
        if (dbus_start(&sessionbus_host) < 0 || sessionbus_listen() < 0)
        {
                string_format(log, (string_address) "sessionbus: cannot listen on %s\n", (string_address)sessionbus.path);
                log_flush();
                return 1;
        }
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
        sessionbus_watch();
        sessionbus_settings();
        for (;;)
        {
                struct sessionbus_poll watch[3 + DBUS_CLIENTS];
                long owner[3 + DBUS_CLIENTS], count = 0;
                positive timeout[2] = {1, 0}; // a second
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
                for (long slot = 0; slot < DBUS_CLIENTS; slot++)
                        if (sessionbus.fd[slot] >= 0)
                        {
                                const unsigned char *bytes;

                                watch[count] = (struct sessionbus_poll){sessionbus.fd[slot],
                                                                         (short)(SESSIONBUS_POLLIN | (dbus_out(slot, &bytes) ? SESSIONBUS_POLLOUT : 0)), 0};
                                owner[count++] = slot;
                        }
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
                if (sessionbus.dirty || (sessionbus.auto_theme && now - sessionbus.last_settings > SESSIONBUS_TICK_MS) ||
                    (sessionbus.inotify < 0 && now - sessionbus.last_settings > 1000))
                        sessionbus_settings();
                dbus_tick();
                for (long slot = 0; slot < DBUS_CLIENTS; slot++)
                        sessionbus_flush(slot);
        }
stop:
        for (long slot = 0; slot < DBUS_CLIENTS; slot++)
                sessionbus_drop(slot);
        system_close(sessionbus.listen);
        sessionbus_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)sessionbus.path, 0, 0, 0, 0);
        dbus_stop();
        return 0;
}

#endif /* SESSIONBUS_INCLUDED */
