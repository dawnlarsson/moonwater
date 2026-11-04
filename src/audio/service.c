/*
        The sound server of Moonwater's desktop, `audio`: the process that listens on a socket for programs that play
        sound, mixes them (mix.c, pulse.c) and plays the mix on a sound card (alsa.c).

            audio [-S SOCKET] [-C DIRECTORY] [-d CARD:DEVICE] [-o FILE] [-n] [-r RATE]

        It is a process of its own and not a part of the compositor: the compositor holds the display and every input
        device, and a parser of what any program may send has no place in it; the compositor's frame can take longer
        than the period of a sound card; and sound should outlive a desktop that is restarted. The machine script
        starts it with the desktop and starts it again when it ends.

        The socket is /run/moonwater/pulse/native (mode 0666: what a program on the machine is allowed to do there is
        decided by who it is, pulse.c, and by the caps), and a program reaches it with PULSE_SERVER=unix:PATH. The card
        is the first with a playback device that is not an HDMI one, else the first there is; with none the server
        runs on a clock of its own and the programs play to nowhere, so they start and are never told there is no
        sound server. -d names a card and device, -o writes what is played to a file (raw 16-bit stereo) as it plays,
        -n takes no card, -r is the rate it asks of the card.

        One loop and one thread: it waits for the socket, the programs' sockets and the card (or the clock) in one
        poll, and a pass of it does a bounded amount of work, so a program that writes too much, reads too little or
        stops cannot make the sound late for the others. The card is kept filled to three periods of about 5 ms; the
        loop asks the system for a real-time priority and says so in its log when it does not get one.
*/
#ifndef MOONWATER_AUDIO_SERVICE
#define MOONWATER_AUDIO_SERVICE

#include "mix.c"
#include "alsa.c"
#include "pulse.c"
#include "state.c"

#define AUDIO_SOCKET "/run/moonwater/pulse/native"
#define AUDIO_RUN_DIRECTORY "/run/moonwater"
#define AUDIO_PULSE_DIRECTORY "/run/moonwater/pulse"
#define AUDIO_BACKLOG 16
#define AUDIO_READ 65536              // one read from a client; a client gets AUDIO_READS of them a pass
#define AUDIO_READS 4
#define AUDIO_ACCEPTS 4
#define AUDIO_PERIOD 256              // frames of a period at 48000 Hz
#define AUDIO_STALL_NS 10000000000ull // a client that has taken none of what it was sent for this long is dropped
#define AUDIO_RESCAN_NS 2000000000ull
#define AUDIO_SETTINGS "/root"
#define AUDIO_APPS 64
#define AUDIO_STATE_NS 16000000ull       // the page is written this often while anything plays
#define AUDIO_STATE_IDLE_NS 500000000ull // and this often when all is quiet
#define AUDIO_SETTINGS_NS 500000000ull   // the settings files are read again this often
#define AUDIO_DEVICES_NS 3000000000ull   // and the cards looked at again
#define AUDIO_SAVE_NS 1000000000ull      // a change is written to the settings this long after the last
#define AUDIO_POLLIN 1
#define AUDIO_POLLOUT 4
#define AUDIO_POLLERR 8
#define AUDIO_POLLHUP 16

struct audio_poll
{
        int handle;
        short events, returned;
};

static struct
{
        struct pulse pulse;
        int listen, signals, timer;
        int fd[PULSE_CLIENTS];
        positive stalled[PULSE_CLIENTS];
        struct audio_pcm pcm;
        long sink, rate;
        int file;
        int no_card;
        long card, device;
        const char *socket_path;
        const char *page_path;
        positive rescan_at, started_at;
        unsigned long long frames_played;
        const char *settings;           // where /root/audio.* are
        int page_fd;
        struct audio_state *page;
        positive state_at, settings_at, devices_at, save_at, dirty_since;
        int dirty;                      // a volume, a mute or a program's volume changed and is to be kept
        unsigned long applied_volume;   // what the settings files last said (or the server last wrote), so its own write is not read as a change
        int applied_mute;
        char applied_device[PULSE_NAME];
        char wanted_device[PULSE_NAME]; // the person's choice of card, "" for the first that is not HDMI
        int device_count;
        struct audio_state_device devices[AUDIO_STATE_DEVICES];
        struct { char name[PULSE_VALUE + 1]; unsigned long volume; int mute; } apps[AUDIO_APPS];
        int app_count;
        char active_name[PULSE_NAME], active_description[PULSE_DESCRIPTION];
        unsigned char block[AUDIO_BLOCK * 8 * 4];
        unsigned char read[AUDIO_READ];
        unsigned char file_block[AUDIO_BLOCK * 4];
} audio_service; // zeroes only: an initialiser with a -1 in it would put its 4 MiB in the file

static long audio_call(positive number, positive a, positive b, positive c, positive d, positive e, positive f)
{
        return (long)system_call_6(number, a, b, c, d, e, f);
}

/* The five kernel calls of alsa.c. */
static long audio_sys_open(const char *path, int flags)
{
        return audio_call(syscall(openat), (positive)AT_FDCWD, (positive)path, (positive)flags, 0, 0, 0);
}

static long audio_sys_close(int fd)
{
        return audio_call(syscall(close), (positive)fd, 0, 0, 0, 0, 0);
}

static long audio_sys_ioctl(int fd, unsigned long request, void *argument)
{
        return audio_call(syscall(ioctl), (positive)fd, request, (positive)argument, 0, 0, 0);
}

static long audio_sys_read(int fd, void *buffer, unsigned long bytes)
{
        return audio_call(syscall(read), (positive)fd, (positive)buffer, bytes, 0, 0, 0);
}

static long audio_sys_write(int fd, const void *buffer, unsigned long bytes)
{
        return audio_call(syscall(write), (positive)fd, (positive)buffer, bytes, 0, 0, 0);
}

/* The host of pulse.c. */
static void *audio_host_alloc(void *context, long bytes)
{
        long got = audio_call(syscall(mmap), 0, (positive)bytes, 3, 0x22, (positive)-1, 0);

        (void)context;
        return got < 0 && got > -4096 ? 0 : (void *)got;
}

static void audio_host_release(void *context, void *block, long bytes)
{
        (void)context;
        audio_call(syscall(munmap), (positive)block, (positive)bytes, 0, 0, 0, 0);
}

static long audio_host_now(void *context)
{
        (void)context;
        return (long)(clock_monotonic_nanoseconds() / 1000);
}

static void audio_host_real(void *context, long *seconds, long *microseconds)
{
        timespec at = {0, 0};

        (void)context;
        clock_gettime(CLOCK_REALTIME, address_of at);
        *seconds = (long)at.tv_sec;
        *microseconds = (long)(at.tv_nsec / 1000);
}

// SIGTERM, SIGINT and SIGHUP, as a descriptor that is read.
static int audio_signals(void)
{
        unsigned long mask = 1ul << (15 - 1) | 1ul << (2 - 1) | 1ul << (1 - 1);

        audio_call(syscall(rt_sigprocmask), 0, (positive)address_of mask, 0, 8, 0, 0);
        return (int)audio_call(syscall(signalfd4), (positive)-1, (positive)address_of mask, 8, 0x800 | 0x80000, 0, 0);
}

// A decimal number, else the fallback.
static positive audio_number(string_address text, positive fallback)
{
        positive value = 0;
        int digits = 0;

        for (; *text >= '0' && *text <= '9' && digits < 9; text++, digits++)
                value = value * 10 + (positive)(*text - '0');
        return digits ? value : fallback;
}

static void audio_say(string_address what, bipolar value)
{
        string_format(log, "audio: %s %b\n", what, value);
        log_flush();
}

static void audio_say_text(string_address what, string_address text)
{
        string_format(log, "audio: %s %s\n", what, text);
        log_flush();
}

/* ---- the card ---- */

// "alsa_output.<card id>.<device>" with the characters a name may have.
static void audio_sink_name(char *to, long room, const struct audio_card *card, long device)
{
        long at = 0;
        const char *prefix = "alsa_output.";

        for (; *prefix && at < room - 1; prefix++)
                to[at++] = *prefix;
        for (long i = 0; card->id[i] && at < room - 8; i++)
        {
                char c = card->id[i];

                to[at++] = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ? c : (c >= 'A' && c <= 'Z') ? (char)(c + 32) : '_';
        }
        to[at++] = '.';
        to[at++] = (char)('0' + device / 10 % 10);
        to[at++] = (char)('0' + device % 10);
        to[at] = 0;
}

static void audio_copy_text(char *to, long room, const char *from)
{
        long i = 0;

        for (; i < room - 1 && from[i]; i++)
                to[i] = from[i];
        to[i] = 0;
}

static int audio_text_equal(const char *a, const char *b)
{
        while (*a && *a == *b)
                a++, b++;
        return *a == *b;
}

// "Card - device" as the person is shown it (just the card's name when the device has none).
static void audio_describe(char *to, long room, const char *card, const char *device)
{
        long at = 0;

        for (; *card && at < room - 4; card++)
                to[at++] = *card;
        if (*device && at < room - 6)
        {
                to[at++] = ' ';
                to[at++] = '-';
                to[at++] = ' ';
                for (; *device && at < room - 1; device++)
                        to[at++] = *device;
        }
        to[at] = 0;
}

static int audio_is_hdmi(const char *label)
{
        for (long k = 0; label[k]; k++)
                if ((label[k] == 'H' || label[k] == 'h') && (label[k + 1] == 'D' || label[k + 1] == 'd') && (label[k + 2] == 'M' || label[k + 2] == 'm') &&
                    (label[k + 3] == 'I' || label[k + 3] == 'i'))
                        return 1;
        return 0;
}

/*
        Looks at the cards: the list of playback devices is made (what the page shows, and what a choice may name), and
        one is picked: the one the person chose by name, else the one -d names, else the first that is not an HDMI one,
        else the first. 0 when there is none.
*/
static int audio_card_pick(long *card_out, long *device_out, char *name, long name_room, char *description, long description_room)
{
        static struct audio_card cards[AUDIO_ALSA_CARDS];
        long found = audio_cards_scan(cards, AUDIO_ALSA_CARDS), best = -1, best_device = -1, best_rank = 0;

        audio_service.device_count = 0;
        for (long i = 0; i < found; i++)
                for (long d = 0; d < cards[i].devices; d++)
                {
                        char listed[PULSE_NAME];
                        long rank;
                        int at = audio_service.device_count;

                        if (!cards[i].device[d].playback)
                                continue;
                        audio_sink_name(listed, sizeof(listed), cards + i, cards[i].device[d].device);
                        if (at < AUDIO_STATE_DEVICES)
                        {
                                audio_copy_text(audio_service.devices[at].name, sizeof(audio_service.devices[at].name), listed);
                                audio_describe(audio_service.devices[at].description, sizeof(audio_service.devices[at].description), cards[i].name, cards[i].device[d].name);
                                audio_service.devices[at].flags = 0;
                                audio_service.device_count++;
                        }
                        rank = audio_is_hdmi(cards[i].device[d].name) ? 1 : 2;
                        if (audio_service.card >= 0 && cards[i].number == audio_service.card && (audio_service.device < 0 || cards[i].device[d].device == audio_service.device))
                                rank = 90;
                        if (audio_service.wanted_device[0] && audio_text_equal(audio_service.wanted_device, listed))
                                rank = 100;
                        if (rank > best_rank || (rank == best_rank && rank >= 90 && best < 0))
                        {
                                best_rank = rank;
                                best = i;
                                best_device = d;
                        }
                }
        if (best < 0)
                return 0;
        // A choice of the person's that is not there (the card is unplugged) is not a reason for no sound: the first is used.
        *card_out = cards[best].number;
        *device_out = cards[best].device[best_device].device;
        audio_sink_name(name, name_room, cards + best, *device_out);
        audio_describe(description, description_room, cards[best].name, cards[best].device[best_device].name);
        return 1;
}

static void audio_timer_set(long rate)
{
        long period_ns = (long)((long long)AUDIO_PERIOD * 1000000000ll / (rate ? rate : 48000));
        long spec[4] = {0, period_ns, 0, period_ns}; // interval, then first expiry

        if (audio_service.timer < 0)
                audio_service.timer = (int)audio_call(syscall(timerfd_create), 1, 0x800 | 0x80000, 0, 0, 0, 0); // CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC
        if (audio_service.timer >= 0)
                audio_call(syscall(timerfd_settime), (positive)audio_service.timer, 0, (positive)spec, 0, 0, 0);
}

static void audio_timer_stop(void)
{
        long spec[4] = {0, 0, 0, 0};

        if (audio_service.timer >= 0)
                audio_call(syscall(timerfd_settime), (positive)audio_service.timer, 0, (positive)spec, 0, 0, 0);
}

// The card, or the clock when there is none: opened, and the sink made or moved to its rate. Answers 1 when a card is open.
static int audio_output_open(long asked_rate)
{
        char name[PULSE_NAME], description[PULSE_DESCRIPTION];
        long card = -1, device = -1;

        if (!audio_service.no_card && audio_card_pick(&card, &device, name, sizeof(name), description, sizeof(description)))
        {
                struct audio_pcm pcm;
                long result = audio_pcm_open(&pcm, card, device, AUDIO_ALSA_PLAYBACK, asked_rate, AUDIO_PERIOD, -1);

                if (result == 0)
                {
                        audio_service.pcm = pcm;
                        audio_service.rate = pcm.rate;
                        audio_copy_text(audio_service.active_name, sizeof(audio_service.active_name), name);
                        audio_copy_text(audio_service.active_description, sizeof(audio_service.active_description), description);
                        if (audio_service.sink < 0)
                                audio_service.sink = pulse_sink_add(&audio_service.pulse, name, description, pcm.rate);
                        else
                                pulse_sink_reconfigure(&audio_service.pulse, audio_service.sink, name, description, pcm.rate);
                        audio_timer_stop();
                        audio_say_text("playing on", description);
                        return 1;
                }
                audio_say("the card could not be opened, error", result);
        }
        audio_service.pcm.fd = -1;
        audio_service.rate = asked_rate;
        audio_copy_text(audio_service.active_name, sizeof(audio_service.active_name), "auto_null");
        audio_copy_text(audio_service.active_description, sizeof(audio_service.active_description), "Dummy Output");
        if (audio_service.sink < 0)
                audio_service.sink = pulse_sink_add(&audio_service.pulse, "auto_null", "Dummy Output", asked_rate);
        else
                pulse_sink_reconfigure(&audio_service.pulse, audio_service.sink, "auto_null", "Dummy Output", asked_rate);
        audio_timer_set(audio_service.rate);
        return 0;
}

/* ---- what is kept: the settings files, the memory of programs' volumes, the page ---- */

static long audio_text_length(const char *s);

static void audio_path(char *to, long room, const char *name)
{
        long at = 0;

        for (const char *d = audio_service.settings; *d && at < room - 2; d++)
                to[at++] = *d;
        to[at++] = '/';
        for (; *name && at < room - 1; name++)
                to[at++] = *name;
        to[at] = 0;
}

// A small file's text (up to room - 1 bytes), or an empty string.
static long audio_file_read(const char *name, char *text, long room)
{
        char path[160];
        long fd, got;

        text[0] = 0;
        audio_path(path, sizeof(path), name);
        fd = audio_call(syscall(openat), (positive)AT_FDCWD, (positive)path, 0x80000, 0, 0, 0);
        if (fd < 0)
                return 0;
        got = audio_call(syscall(read), (positive)fd, (positive)text, (positive)(room - 1), 0, 0, 0);
        audio_call(syscall(close), (positive)fd, 0, 0, 0, 0, 0);
        if (got < 0)
                got = 0;
        text[got] = 0;
        return got;
}

// A small file replaced whole: written beside it and renamed over it.
static void audio_file_write(const char *name, const char *text, long length)
{
        char path[160], temporary[168];
        long fd;

        audio_path(path, sizeof(path), name);
        audio_copy_text(temporary, sizeof(temporary), path);
        audio_copy_text(temporary + audio_text_length(temporary), (long)sizeof(temporary) - audio_text_length(temporary), ".new");
        fd = audio_call(syscall(openat), (positive)AT_FDCWD, (positive)temporary, 0x1 | 0x40 | 0x200 | 0x80000, 0644, 0, 0);
        if (fd < 0)
                return;
        audio_call(syscall(write), (positive)fd, (positive)text, (positive)length, 0, 0, 0);
        audio_call(syscall(close), (positive)fd, 0, 0, 0, 0, 0);
        audio_call(syscall(renameat2), (positive)AT_FDCWD, (positive)temporary, (positive)AT_FDCWD, (positive)path, 0, 0);
}

static long audio_text_length(const char *s)
{
        long n = 0;

        while (s[n])
                n++;
        return n;
}

// "65" and "100%" and " 40\n": a percent from 0 to 200, or -1.
static long audio_percent(const char *text)
{
        long value = 0, digits = 0;

        while (*text == ' ' || *text == '\t')
                text++;
        for (; *text >= '0' && *text <= '9' && digits < 4; text++, digits++)
                value = value * 10 + (*text - '0');
        return digits && value <= 200 ? value : -1;
}

static long audio_number_text(char *to, unsigned long value)
{
        char digits[24];
        long count = 0, at = 0;

        do
                digits[count++] = (char)('0' + value % 10);
        while ((value /= 10) > 0 && count < 20);
        while (count)
                to[at++] = digits[--count];
        to[at] = 0;
        return at;
}

// The master volume and mute, and the choice of card, from the settings files: what is new in them is applied. The server's own writes are not read back as changes.
static void audio_settings_read(int first)
{
        char text[128];
        struct pulse_sink *sink = audio_service.sink >= 0 ? audio_service.pulse.sink + audio_service.sink : 0;
        long percent;

        // The choice of card is read before there is a sink; the volume and the mute need one.
        if (audio_file_read("audio.device", text, sizeof(text)))
        {
                char wanted[PULSE_NAME];
                long n = 0;

                while (text[n] && text[n] != '\n' && n < PULSE_NAME - 1)
                {
                        wanted[n] = text[n];
                        n++;
                }
                wanted[n] = 0;
                if (audio_text_equal(wanted, "default"))
                        wanted[0] = 0;
                if (!audio_text_equal(wanted, audio_service.applied_device))
                {
                        audio_copy_text(audio_service.applied_device, sizeof(audio_service.applied_device), wanted);
                        audio_copy_text(audio_service.wanted_device, sizeof(audio_service.wanted_device), wanted);
                        if (!first && !audio_service.no_card)
                                audio_service.devices_at = 1; // looked at again at once
                }
        }
        if (!sink)
                return;
        if (audio_file_read("audio.volume", text, sizeof(text)) && (percent = audio_percent(text)) >= 0)
        {
                unsigned long volume = (unsigned long)percent * AUDIO_VOLUME_NORM / 100;

                if (first || volume != audio_service.applied_volume)
                {
                        sink->volume[0] = sink->volume[1] = volume;
                        audio_service.applied_volume = volume;
                        if (!first)
                                pulse_event(&audio_service.pulse, PULSE_FACILITY_SINK, PULSE_EVENT_CHANGE, (unsigned long)audio_service.sink);
                }
        }
        if (audio_file_read("audio.mute", text, sizeof(text)))
        {
                int mute = text[0] == 'o' && text[1] == 'n';

                if (first || mute != audio_service.applied_mute)
                {
                        sink->muted = mute;
                        audio_service.applied_mute = mute;
                        if (!first)
                                pulse_event(&audio_service.pulse, PULSE_FACILITY_SINK, PULSE_EVENT_CHANGE, (unsigned long)audio_service.sink);
                }
        }
}

// What a program changed (the master volume, the mute) is written to the settings, a second after the last change.
static void audio_settings_write(void)
{
        struct pulse_sink *sink = audio_service.pulse.sink + audio_service.sink;
        char text[16];
        long length;

        audio_service.applied_volume = sink->volume[0];
        audio_service.applied_mute = sink->muted;
        length = audio_number_text(text, sink->volume[0] * 100 / AUDIO_VOLUME_NORM);
        text[length++] = '\n';
        audio_file_write("audio.volume", text, length);
        audio_file_write("audio.mute", sink->muted ? "on\n" : "off\n", sink->muted ? 3 : 4);
}

/* The memory of programs' volumes: a line a program, "name TAB volume TAB mute". */
static void audio_apps_load(void)
{
        static char text[AUDIO_APPS * (PULSE_VALUE + 24)];
        long length = audio_file_read("audio.apps", text, sizeof(text)), at = 0;

        audio_service.app_count = 0;
        while (at < length && audio_service.app_count < AUDIO_APPS)
        {
                long start = at, tab1 = -1, tab2 = -1;

                while (at < length && text[at] != '\n')
                {
                        if (text[at] == '\t')
                        {
                                if (tab1 < 0)
                                        tab1 = at;
                                else if (tab2 < 0)
                                        tab2 = at;
                        }
                        at++;
                }
                if (tab1 > start && tab2 > tab1 && tab1 - start <= PULSE_VALUE)
                {
                        long entry = audio_service.app_count++;

                        for (long i = start; i < tab1; i++)
                                audio_service.apps[entry].name[i - start] = text[i];
                        audio_service.apps[entry].name[tab1 - start] = 0;
                        audio_service.apps[entry].volume = (unsigned long)audio_number(text + tab1 + 1, AUDIO_VOLUME_NORM);
                        audio_service.apps[entry].mute = text[tab2 + 1] == '1';
                }
                at++;
        }
}

static void audio_apps_save(void)
{
        static char text[AUDIO_APPS * (PULSE_VALUE + 24)];
        long length = 0;

        for (int i = 0; i < audio_service.app_count; i++)
        {
                long n = audio_text_length(audio_service.apps[i].name);

                for (long k = 0; k < n; k++)
                        text[length++] = audio_service.apps[i].name[k];
                text[length++] = '\t';
                length += audio_number_text(text + length, audio_service.apps[i].volume);
                text[length++] = '\t';
                text[length++] = audio_service.apps[i].mute ? '1' : '0';
                text[length++] = '\n';
        }
        audio_file_write("audio.apps", text, length);
}

static int audio_host_recall(void *context, const char *name, unsigned long *volume, int *mute)
{
        (void)context;
        for (int i = 0; i < audio_service.app_count; i++)
                if (audio_text_equal(audio_service.apps[i].name, name))
                {
                        *volume = audio_service.apps[i].volume;
                        *mute = audio_service.apps[i].mute;
                        return 1;
                }
        return 0;
}

static void audio_host_remember(void *context, const char *name, unsigned long volume, int mute)
{
        int at = -1;

        (void)context;
        if (audio_text_length(name) > PULSE_VALUE)
                return;
        for (int i = 0; i < audio_service.app_count; i++)
                if (audio_text_equal(audio_service.apps[i].name, name))
                        at = i;
        if (at < 0)
        {
                // The oldest is let go when the table is full: the table is a list in the order they were changed.
                if (audio_service.app_count == AUDIO_APPS)
                {
                        for (int i = 1; i < AUDIO_APPS; i++)
                                audio_service.apps[i - 1] = audio_service.apps[i];
                        audio_service.app_count--;
                }
                at = audio_service.app_count++;
                audio_copy_text(audio_service.apps[at].name, sizeof(audio_service.apps[at].name), name);
        }
        audio_service.apps[at].volume = volume;
        audio_service.apps[at].mute = mute;
        audio_service.dirty |= 2;
        if (!audio_service.dirty_since)
                audio_service.dirty_since = clock_monotonic_nanoseconds();
}

static void audio_host_changed(void *context, int what, long sink)
{
        (void)context;
        (void)sink;
        if (what == PULSE_CHANGED_SINK_VOLUME)
        {
                audio_service.dirty |= 1;
                if (!audio_service.dirty_since)
                        audio_service.dirty_since = clock_monotonic_nanoseconds();
        }
}

// The page: the sink's state, the devices and every stream, copied under the sequence number.
static void audio_page_write(void)
{
        struct audio_state *page = audio_service.page;
        struct pulse *p = &audio_service.pulse;
        struct pulse_sink *sink = p->sink + audio_service.sink;
        struct audio_state build;
        unsigned int seq;

        if (!page)
                return;
        __builtin_memset(&build, 0, sizeof(build));
        build.magic = AUDIO_STATE_MAGIC;
        build.version = AUDIO_STATE_VERSION;
        build.bytes = sizeof(build);
        build.volume = (unsigned int)sink->volume[0];
        build.muted = (unsigned int)sink->muted;
        build.peak_left = (unsigned int)(sink->mix.peak[0] * 32767.0f > 32767.0f ? 32767.0f : sink->mix.peak[0] * 32767.0f);
        build.peak_right = (unsigned int)(sink->mix.peak[1] * 32767.0f > 32767.0f ? 32767.0f : sink->mix.peak[1] * 32767.0f);
        sink->mix.peak[0] = sink->mix.peak[1] = 0;
        build.clips = (unsigned int)sink->mix.clips;
        build.underruns = (unsigned int)audio_service.pcm.xruns;
        build.rate = (unsigned int)audio_service.rate;
        build.period = audio_service.pcm.fd >= 0 ? (unsigned int)audio_service.pcm.period : AUDIO_PERIOD;
        build.latency_us = (unsigned int)(sink->delay_frames * 1000000l / (sink->rate ? sink->rate : 1));
        audio_copy_text(build.device, sizeof(build.device), audio_service.active_description);
        audio_copy_text(build.name, sizeof(build.name), audio_service.active_name);
        build.devices = (unsigned int)audio_service.device_count;
        for (int i = 0; i < audio_service.device_count; i++)
        {
                build.device_list[i] = audio_service.devices[i];
                if (audio_text_equal(build.device_list[i].name, audio_service.active_name))
                        build.device_list[i].flags |= AUDIO_STATE_DEVICE_ACTIVE;
        }
        for (int i = 0; i < AUDIO_STATE_STREAMS; i++)
                build.stream[i].index = 0xffffffffu;
        for (int i = 0; i < PULSE_STREAMS; i++)
        {
                struct pulse_stream *s = p->stream + i;
                struct pulse_client *c;
                struct audio_state_stream *out;
                unsigned long sum = 0;

                if (!s->used || s->dying || s->client < 0 || !(c = p->client[s->client]) || build.streams >= AUDIO_STATE_STREAMS)
                        continue;
                out = build.stream + build.streams++;
                out->index = (unsigned int)s->index;
                out->client = (unsigned int)c->index;
                for (long k = 0; k < s->map_channels; k++)
                        sum += s->volume[k];
                out->volume = (unsigned int)(s->map_channels ? sum / (unsigned long)s->map_channels : 0);
                out->flags = (s->muted ? AUDIO_STATE_MUTED : 0) | (s->corked ? AUDIO_STATE_CORKED : 0) | (!s->underrun && !s->corked ? AUDIO_STATE_PLAYING : 0);
                out->peak_left = (unsigned int)(s->voice.peak[0] * 32767.0f > 32767.0f ? 32767.0f : s->voice.peak[0] * 32767.0f);
                out->peak_right = (unsigned int)(s->voice.peak[1] * 32767.0f > 32767.0f ? 32767.0f : s->voice.peak[1] * 32767.0f);
                s->voice.peak[0] = s->voice.peak[1] = 0;
                out->pid = (int)c->pid;
                out->uid = (unsigned int)c->uid;
                pulse_props_text(&c->props, "application.name", out->app, sizeof(out->app));
                pulse_props_text(&s->props, "media.name", out->media, sizeof(out->media));
                pulse_props_text(&c->props, "application.icon_name", out->icon, sizeof(out->icon));
        }
        seq = __atomic_load_n(&page->seq, __ATOMIC_RELAXED);
        __atomic_store_n(&page->seq, seq + 1, __ATOMIC_RELEASE); // odd: being written
        __builtin_memcpy((char *)page + 16, (char *)&build + 16, sizeof(build) - 16);
        page->magic = build.magic;
        page->version = build.version;
        page->bytes = build.bytes;
        __atomic_store_n(&page->seq, seq + 2, __ATOMIC_RELEASE);
}

static void audio_page_open(void)
{
        long fd = audio_call(syscall(openat), (positive)AT_FDCWD, (positive)audio_service.page_path, 0x2 | 0x40 | 0x80000, 0600, 0, 0), got;

        if (fd < 0)
                return;
        audio_call(syscall(ftruncate), (positive)fd, AUDIO_STATE_BYTES, 0, 0, 0, 0);
        got = audio_call(syscall(mmap), 0, AUDIO_STATE_BYTES, 3, 1, (positive)fd, 0); // PROT_READ | PROT_WRITE, MAP_SHARED
        audio_call(syscall(close), (positive)fd, 0, 0, 0, 0, 0);
        if (got < 0 && got > -4096)
                return;
        audio_service.page = (struct audio_state *)got;
}

// The choice of card changed, or one came or went: the list is made again, and the card that is the person's choice, else the best, is the one played on.
static void audio_devices_pass(void)
{
        char name[PULSE_NAME], description[PULSE_DESCRIPTION];
        long card = -1, device = -1;
        int have;

        if (audio_service.no_card)
                return;
        have = audio_card_pick(&card, &device, name, sizeof(name), description, sizeof(description));
        if (!have)
                return;
        // The one in use is still the one wanted: nothing to do (and a card that went is the clock's job, below).
        if (audio_service.pcm.fd >= 0 && audio_text_equal(name, audio_service.active_name))
                return;
        if (audio_service.pcm.fd >= 0)
        {
                audio_pcm_close(&audio_service.pcm);
                audio_service.pcm.fd = -1;
        }
        audio_output_open(audio_service.rate);
}

/* ---- the clients ---- */

static void audio_client_drop(long slot)
{
        if (audio_service.fd[slot] >= 0)
                system_close(audio_service.fd[slot]);
        audio_service.fd[slot] = -1;
        audio_service.stalled[slot] = 0;
        pulse_client_close(&audio_service.pulse, slot);
}

static void audio_accept(void)
{
        for (int tries = 0; tries < AUDIO_ACCEPTS; tries++)
        {
                int credentials[3] = {0, 0, 0};
                unsigned int size = sizeof credentials;
                long fd = audio_call(syscall(accept4), (positive)audio_service.listen, 0, 0, 0x800 | 0x80000, 0, 0), slot;

                if (fd < 0)
                        return;
                // SOL_SOCKET, SO_PEERCRED: the kernel's word for who is on the other limit.
                if (audio_call(syscall(getsockopt), (positive)fd, 1, 17, (positive)credentials, (positive)&size, 0) < 0)
                {
                        system_close((int)fd);
                        continue;
                }
                slot = pulse_client_open(&audio_service.pulse, credentials[1], credentials[0]);
                if (slot < 0)
                {
                        system_close((int)fd);
                        continue;
                }
                audio_service.fd[slot] = (int)fd;
                audio_service.stalled[slot] = 0;
        }
}

// What a client sent, up to a budget; false when it is to be dropped.
static int audio_client_read(long slot)
{
        for (int reads = 0; reads < AUDIO_READS; reads++)
        {
                long got = audio_call(syscall(recvfrom), (positive)audio_service.fd[slot], (positive)audio_service.read, AUDIO_READ, 0x40, 0, 0); // MSG_DONTWAIT

                if (got == 0)
                        return 0;
                if (got < 0)
                        return got == -11 || got == -4; // EAGAIN, EINTR
                if (pulse_client_feed(&audio_service.pulse, slot, audio_service.read, got) < 0)
                        return 0;
                if (got < AUDIO_READ)
                        break;
        }
        return 1;
}

// What is queued for a client, as far as its socket takes it. A client that takes nothing for too long is dropped.
static int audio_client_write(long slot, positive now)
{
        for (int rounds = 0; rounds < 8; rounds++)
        {
                const unsigned char *bytes;
                long count = pulse_client_out(&audio_service.pulse, slot, &bytes), put;

                if (count <= 0)
                {
                        audio_service.stalled[slot] = 0;
                        return 1;
                }
                // MSG_DONTWAIT | MSG_NOSIGNAL
                put = audio_call(syscall(sendto), (positive)audio_service.fd[slot], (positive)bytes, (positive)count, 0x40 | 0x4000, 0, 0);
                if (put < 0)
                {
                        if (put != -11 && put != -4)
                                return 0;
                        if (!audio_service.stalled[slot])
                                audio_service.stalled[slot] = now;
                        return now - audio_service.stalled[slot] < AUDIO_STALL_NS;
                }
                audio_service.stalled[slot] = 0;
                pulse_client_sent(&audio_service.pulse, slot, put);
        }
        return 1;
}

/* ---- the sound ---- */

// Mixes frames of the sink and plays them: to the card, or the clock's file.
static void audio_produce(long frames)
{
        struct audio_pcm *pcm = &audio_service.pcm;

        while (frames > 0)
        {
                long n = frames < AUDIO_BLOCK ? frames : AUDIO_BLOCK;

                if (pcm->fd >= 0)
                {
                        long written;

                        pulse_sink_mix(&audio_service.pulse, audio_service.sink, audio_service.block, pcm->format, pcm->channels, n);
                        written = audio_pcm_write(pcm, audio_service.block, n);
                        if (written < 0)
                                return;
                        audio_service.frames_played += (unsigned long long)written;
                        frames -= written;
                        if (written < n)
                                return;
                }
                else
                {
                        pulse_sink_mix(&audio_service.pulse, audio_service.sink, audio_service.file_block, AUDIO_FORMAT_S16LE, 2, n);
                        if (audio_service.file >= 0)
                                audio_call(syscall(write), (positive)audio_service.file, (positive)audio_service.file_block, (positive)n * 4, 0, 0, 0);
                        audio_service.frames_played += (unsigned long long)n;
                        frames -= n;
                }
        }
}

// The card wants more: filled to its target, or recovered, or given up on.
static void audio_card_pass(void)
{
        struct audio_pcm *pcm = &audio_service.pcm;
        long avail = audio_pcm_avail(pcm), fill, want;

        if (avail < 0)
        {
                long error = audio_pcm_recover(pcm, avail);

                if (error == 0)
                        return; // empty and ready: the next wake fills it
                if (pcm->broken)
                {
                        audio_say("the card is gone, error", error);
                        audio_pcm_close(pcm);
                        pcm->fd = -1;
                        audio_timer_set(audio_service.rate);
                        audio_service.rescan_at = clock_monotonic_nanoseconds() + AUDIO_RESCAN_NS;
                }
                return;
        }
        fill = pcm->buffer - avail;
        want = pcm->target - fill;
        if (want > 0)
                audio_produce(want);
        {
                long delay = audio_pcm_delay(pcm);

                if (delay >= 0)
                        pulse_sink_delay(&audio_service.pulse, audio_service.sink, delay);
        }
}

// The clock ticked: as many periods as passed since the last pass, within reason.
static void audio_clock_pass(void)
{
        unsigned long long expirations = 0;
        long got = audio_call(syscall(read), (positive)audio_service.timer, (positive)&expirations, 8, 0, 0, 0);

        if (got != 8 || !expirations)
                return;
        if (expirations > 16)
                expirations = 16;
        audio_produce((long)expirations * (AUDIO_PERIOD * audio_service.rate / 48000));
        pulse_sink_delay(&audio_service.pulse, audio_service.sink, 0);
}

static void audio_start(void)
{
        struct pulse_host host = {0};
        unsigned long long param = 2;
        char directory[2][24] = {AUDIO_RUN_DIRECTORY, AUDIO_PULSE_DIRECTORY};

        host.alloc = audio_host_alloc;
        host.release = audio_host_release;
        host.now = audio_host_now;
        host.real = audio_host_real;
        host.recall = audio_host_recall;
        host.remember = audio_host_remember;
        host.changed = audio_host_changed;
        pulse_open(&audio_service.pulse, &host);
        for (long at = 0; at < PULSE_CLIENTS; at++)
                audio_service.fd[at] = -1;
        audio_service.sink = -1;
        // The server's own scheduling: real time at the lowest priority, when allowed.
        if (audio_call(syscall(sched_setscheduler), 0, 1, (positive)&param, 0, 0, 0) < 0)
                audio_say_text("no real-time priority, playing at the ordinary one", "");
        audio_service.signals = audio_signals();
        // A client that has gone is an error of the write, not a signal that ends the server.
        system_signal_install(13, 1, 0, 0, null);
        for (int i = 0; i < 2; i++)
                audio_call(syscall(mkdirat), (positive)AT_FDCWD, (positive)directory[i], 0755, 0, 0, 0);
}

static void audio_listen(void)
{
        unsigned char address[110];
        long length = 0, got;

        memory_zero(address, sizeof address);
        address[0] = 1; // AF_UNIX
        while (audio_service.socket_path[length] && length < 100)
        {
                address[2 + length] = (unsigned char)audio_service.socket_path[length];
                length++;
        }
        audio_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)audio_service.socket_path, 0, 0, 0, 0);
        got = audio_call(syscall(socket), 1, 1 | 0x800 | 0x80000, 0, 0, 0, 0);
        if (got < 0)
                return;
        if (audio_call(syscall(bind), (positive)got, (positive)address, (positive)(2 + length + 1), 0, 0, 0) < 0 ||
            audio_call(syscall(listen), (positive)got, AUDIO_BACKLOG, 0, 0, 0, 0) < 0)
        {
                system_close((int)got);
                audio_say_text("cannot listen on", audio_service.socket_path);
                return;
        }
        audio_call(syscall(fchmodat), (positive)AT_FDCWD, (positive)audio_service.socket_path, 0666, 0, 0, 0);
        audio_service.listen = (int)got;
}

static b32 audio_main()
{
        positive rate = 48000;
        int running = 1;

        claim_standard_descriptors();
        audio_service.listen = audio_service.signals = audio_service.timer = audio_service.file = -1;
        audio_service.card = audio_service.device = -1;
        audio_service.socket_path = AUDIO_SOCKET;
        audio_service.settings = AUDIO_SETTINGS;
        audio_service.page_path = AUDIO_STATE_PATH;
        for (positive at = 1; at < program_argument_count(); at++)
        {
                string_address option = program_argument(at);

                if (at + 1 < program_argument_count() && string_equals(option, "-S"))
                        audio_service.socket_path = (const char *)program_argument(++at);
                else if (at + 1 < program_argument_count() && string_equals(option, "-C"))
                        audio_service.settings = (const char *)program_argument(++at);
                else if (at + 1 < program_argument_count() && string_equals(option, "-P"))
                        audio_service.page_path = (const char *)program_argument(++at);
                else if (at + 1 < program_argument_count() && string_equals(option, "-o"))
                        audio_service.file = (int)audio_call(syscall(openat), (positive)AT_FDCWD, (positive)program_argument(++at), 0x1 | 0x40 | 0x200 | 0x80000, 0644, 0, 0);
                else if (at + 1 < program_argument_count() && string_equals(option, "-r"))
                        rate = audio_number(program_argument(++at), 48000);
                else if (at + 1 < program_argument_count() && string_equals(option, "-d"))
                {
                        string_address spec = program_argument(++at);

                        audio_service.card = (long)audio_number(spec, 0);
                        for (; *spec && *spec != ':'; spec++)
                                ;
                        audio_service.device = *spec == ':' ? (long)audio_number(spec + 1, 0) : -1;
                }
                else if (string_equals(option, "-n"))
                        audio_service.no_card = 1;
        }
        if (rate < 8000 || rate > 384000)
                rate = 48000;
        audio_start();
        audio_listen();
        if (audio_service.listen < 0 || audio_service.signals < 0)
                return 1;
        audio_settings_read(1); // the person's choice of card first, so it is the one opened
        audio_output_open((long)rate);
        audio_settings_read(1); // and the volume and the mute, now that there is a sink to put them on
        audio_apps_load();
        audio_page_open();
        audio_service.started_at = clock_monotonic_nanoseconds();
        string_format(log, "audio: serving %s, sink %s at %p Hz\n", audio_service.socket_path, audio_service.pulse.sink[audio_service.sink].name,
                      (positive)audio_service.rate);
        log_flush();

        while (running)
        {
                struct audio_poll wanted[3 + PULSE_CLIENTS];
                long owner[3 + PULSE_CLIENTS];
                unsigned int count = 0;
                positive now;
                long awaited, wait[2] = {0, audio_service.pulse.streams_made != audio_service.pulse.streams_freed ? 16000000 : 250000000};

                wanted[count] = (struct audio_poll){audio_service.signals, AUDIO_POLLIN, 0};
                owner[count++] = -1;
                wanted[count] = (struct audio_poll){audio_service.listen, AUDIO_POLLIN, 0};
                owner[count++] = -2;
                for (long slot = 0; slot < PULSE_CLIENTS; slot++)
                        if (audio_service.fd[slot] >= 0)
                        {
                                wanted[count] = (struct audio_poll){audio_service.fd[slot], (short)(AUDIO_POLLIN | (pulse_client_pending(&audio_service.pulse, slot) ? AUDIO_POLLOUT : 0)), 0};
                                owner[count++] = slot;
                        }
                if (audio_service.pcm.fd >= 0)
                {
                        wanted[count] = (struct audio_poll){audio_service.pcm.fd, AUDIO_POLLOUT, 0};
                        owner[count++] = -3;
                }
                else if (audio_service.timer >= 0)
                {
                        wanted[count] = (struct audio_poll){audio_service.timer, AUDIO_POLLIN, 0};
                        owner[count++] = -4;
                }
                awaited = audio_call(syscall(ppoll), (positive)wanted, count, (positive)wait, 0, 0, 0);
                now = clock_monotonic_nanoseconds();
                if (awaited >= 0)
                        for (unsigned int at = 0; at < count; at++)
                        {
                                if (!wanted[at].returned)
                                        continue;
                                if (owner[at] == -1)
                                        running = 0;
                                else if (owner[at] == -2)
                                        audio_accept();
                                else if (owner[at] == -3)
                                        audio_card_pass();
                                else if (owner[at] == -4)
                                        audio_clock_pass();
                                else if (audio_service.fd[owner[at]] >= 0)
                                {
                                        long slot = owner[at];

                                        if ((wanted[at].returned & (AUDIO_POLLIN | AUDIO_POLLHUP | AUDIO_POLLERR)) && !audio_client_read(slot))
                                                audio_client_drop(slot);
                                }
                        }
                // Whatever the pass queued is sent, the drains are answered, and a client that never says who it is, or never reads, is dropped.
                pulse_tick(&audio_service.pulse);
                for (long slot = 0; slot < PULSE_CLIENTS; slot++)
                        if (audio_service.fd[slot] >= 0)
                        {
                                if (pulse_client_late(&audio_service.pulse, slot, (long)(now / 1000)) || !audio_client_write(slot, now))
                                        audio_client_drop(slot);
                        }
                // The page, the settings, the cards, and what a program changed that is kept.
                if (now - audio_service.state_at >= (audio_service.pulse.streams_made != audio_service.pulse.streams_freed ? AUDIO_STATE_NS : AUDIO_STATE_IDLE_NS))
                {
                        audio_service.state_at = now;
                        audio_page_write();
                }
                if (now - audio_service.settings_at >= AUDIO_SETTINGS_NS)
                {
                        audio_service.settings_at = now;
                        audio_settings_read(0);
                }
                if (audio_service.devices_at == 1 || now - audio_service.devices_at >= AUDIO_DEVICES_NS)
                {
                        audio_service.devices_at = now;
                        audio_devices_pass();
                }
                if (audio_service.dirty && audio_service.dirty_since && now - audio_service.dirty_since >= AUDIO_SAVE_NS)
                {
                        if (audio_service.dirty & 1)
                                audio_settings_write();
                        if (audio_service.dirty & 2)
                                audio_apps_save();
                        audio_service.dirty = 0;
                        audio_service.dirty_since = 0;
                }
                // A card that went is looked for again.
                if (audio_service.pcm.fd < 0 && !audio_service.no_card && audio_service.rescan_at && now >= audio_service.rescan_at)
                {
                        audio_service.rescan_at = now + AUDIO_RESCAN_NS;
                        if (audio_output_open(audio_service.rate))
                                audio_service.rescan_at = 0;
                }
        }
        for (long slot = 0; slot < PULSE_CLIENTS; slot++)
                if (audio_service.fd[slot] >= 0)
                        audio_client_drop(slot);
        if (audio_service.dirty & 1)
                audio_settings_write();
        if (audio_service.dirty & 2)
                audio_apps_save();
        if (audio_service.page)
                audio_call(syscall(munmap), (positive)audio_service.page, AUDIO_STATE_BYTES, 0, 0, 0, 0);
        audio_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)audio_service.page_path, 0, 0, 0, 0);
        audio_pcm_close(&audio_service.pcm);
        system_close(audio_service.listen);
        audio_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)audio_service.socket_path, 0, 0, 0, 0);
        string_format(log, "audio: ended after %p frames, %p underruns\n", (positive)audio_service.frames_played, (positive)audio_service.pcm.xruns);
        log_flush();
        return 0;
}

#endif // MOONWATER_AUDIO_SERVICE
