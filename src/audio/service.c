/*
        The sound server of Moonwater's desktop, `audio`: the process that listens on a socket for programs that play
        sound, mixes them (mix.c, pulse.c) and plays the mix on a sound card (alsa.c).

            audio [-S SOCKET] [-C DIRECTORY] [-d CARD:DEVICE] [-o FILE] [-n] [-r RATE] [-M]
            audio -t [-S SOCKET]

        It is a process of its own and not a part of the compositor: the compositor holds the display and every input
        device, and a parser of what any program may send has no place in it; the compositor's frame can take longer
        than the period of a sound card; and sound should outlive a desktop that is restarted. The machine script
        starts it with the desktop and starts it again when it ends.

        A sound card starts with its own mixer muted (the hardware does, and nothing else sets it here: no alsactl, no
        PulseAudio), so when the card is opened its output controls are put where a sound is heard (alsa.c,
        audio_mixer_prepare: the switches on, the gains at 0 dB, auto-mute on), and what was found and done is on the page
        and in the log. -M leaves the card's mixer as it is (to see what a card does without it). `audio -t` is a client: a
        second of 440 Hz played through the running server and what happened said in words (`moonwater audio test`).

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

        Programs that record are served by the same loop: every capture device of every card is a source (and the monitor
        of every sink is one), a capture device is opened while a program listens to it and for a second after, and what
        it reads is given to the engine a period at a time. The person's switch /root/audio.capture (on or off) decides
        whether any program may record; /root/audio.source names the default source, and /root/audio.source.volume and
        .mute are its level.
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
#define AUDIO_PERIOD 128              // frames of a period at 48000 Hz (2.67 ms)
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
#define AUDIO_INPUTS (PULSE_SOURCES - PULSE_SINKS) // capture devices at most
#define AUDIO_CAPTURE_LINGER_NS 1000000000ull      // a capture device is kept open this long after the last program stopped listening
#define AUDIO_CAPTURE_RETRY_NS 2000000000ull       // and one that could not be opened is tried again after this
#define AUDIO_CAPTURE_ROUNDS 4                     // blocks one wake of a capture device reads at most
#define AUDIO_JOB_OPEN 1
#define AUDIO_JOB_CLOSE 2

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
        // What the loop measures of itself: how long a pass takes, how late the clock's wake was, how little the card held when it was
        // refilled; in buckets of a power of two of microseconds (bucket n is under 2^n us), and the worst of each.
        struct
        {
                unsigned long wakes, slips;
                unsigned long pass[16], late[16];
                positive worst_pass, worst_late, due;
                long min_fill, target;
        } stats;
        long budget;                    // bytes of what clients sent that a pass may still read: a flood is taken over several passes, the sound between them
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
        // The capture devices: each is a source of the engine; its PCM is open while a program listens.
        struct
        {
                int used, failed, opening;
                long slot, card, device;
                char name[PULSE_NAME], description[PULSE_DESCRIPTION];
                struct audio_pcm pcm;
                positive idle_since, retry_at;
        } input[AUDIO_INPUTS];
        int inputs_open;
        // The opening and the closing of a capture device are slow on some hardware (a USB device takes 20 ms to prepare and 7 to close),
        // which is longer than the card holds of what is playing: a thread does them, one at a time, and tells the loop on a pipe.
        struct
        {
                int kind, k, running, done;
                struct audio_pcm pcm;
                long card, device, result;
                thread address_to handle;
                positive took;
                struct audio_mixer_report mix; // what the open did to the card's capture controls
        } job;
        int wake[2];
        struct audio_mixer_report hw;   // what was found and done to the output card's mixer when it was opened
        int hw_done, leave_mixer, test; // the report is of this open; -M leaves the mixer as the card has it; -t is the tone
        int input_internal;             // set while the service itself changes the sources, so the host's notice of it is not taken for the person's choice
        char wanted_source[PULSE_NAME]; // the person's choice of default source, "" for the best there is
        char applied_source[PULSE_NAME];
        unsigned long applied_in_volume; // the level of the input as the settings last said it (or the server last wrote it)
        int applied_in_mute, applied_capture;
        struct
        {
                unsigned long wakes, overruns, opens;
                unsigned long pass[16];
                positive worst_pass, worst_open;
                unsigned long long frames;
        } capture;
        float in_planes[AUDIO_CHANNELS_MAX][AUDIO_BLOCK];
        unsigned char in_block[AUDIO_BLOCK * AUDIO_CHANNELS_MAX * 4];
        unsigned char block[AUDIO_BLOCK * 8 * 4];
        unsigned char read[AUDIO_READ];
        unsigned char file_block[AUDIO_BLOCK * 4];
} audio_service; // zeroes only: an initialiser with a -1 in it would put its 4 MiB in the file

// Something plays or something records: the loop looks at the page and the clients sixty times a second, else twice.
static int audio_busy(void)
{
        return audio_service.pulse.streams_made != audio_service.pulse.streams_freed || audio_service.pulse.records_made != audio_service.pulse.records_freed;
}

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
        unsigned long mask = 1ul << (15 - 1) | 1ul << (2 - 1) | 1ul << (1 - 1) | 1ul << (10 - 1);

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

// "<prefix><card id>.<device>" with the characters a name may have.
static void audio_device_name(char *to, long room, const char *prefix, const struct audio_card *card, long device)
{
        long at = 0;

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

static void audio_sink_name(char *to, long room, const struct audio_card *card, long device)
{
        audio_device_name(to, room, "alsa_output.", card, device);
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
                        string_format(log, "audio: the card runs at %p Hz in %p bytes a frame, a period of %p frames, a buffer of %p, filled to %p\n", (positive)pcm.rate, (positive)pcm.frame_bytes,
                                      (positive)pcm.period, (positive)pcm.buffer, (positive)pcm.target);
                        // The card's own mixer starts muted: its outputs are put where a sound is heard.
                        audio_service.hw_done = 0;
                        if (!audio_service.leave_mixer)
                        {
                                long mixed = audio_mixer_prepare(card, 1, &audio_service.hw);

                                if (mixed == 0)
                                {
                                        audio_service.hw_done = 1;
                                        string_format(log, "audio: the card's mixer has %p controls, %p of its outputs; %p were not as a sound needs them and are set, %p would not be%s%s\n", (positive)audio_service.hw.controls,
                                                      (positive)audio_service.hw.output, (positive)audio_service.hw.changed, (positive)audio_service.hw.failed + (positive)audio_service.hw.left_off,
                                                      audio_service.hw.left_off ? ", the first still off is " : "", audio_service.hw.left_off ? audio_service.hw.first_off : "");
                                }
                                else
                                        string_format(log, "audio: the card's mixer could not be opened, error %b\n", (bipolar)mixed);
                        }
                        log_flush();
                        return 1;
                }
                audio_say("the card could not be opened, error", result);
        }
        audio_service.pcm.fd = -1;
        audio_service.hw_done = 0;
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
        // O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK: a link is not followed and a FIFO is not waited for (the server would wait for ever in a read of it).
        fd = audio_call(syscall(openat), (positive)AT_FDCWD, (positive)path, 0x80000 | 0x20000 | 0x800, 0, 0, 0);
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
        long fd, put;

        audio_path(path, sizeof(path), name);
        audio_copy_text(temporary, sizeof(temporary), path);
        audio_copy_text(temporary + audio_text_length(temporary), (long)sizeof(temporary) - audio_text_length(temporary), ".new");

        fd = audio_call(syscall(openat), (positive)AT_FDCWD, (positive)temporary, 0x1 | 0x40 | 0x200 | 0x80000 | 0x20000, 0644, 0, 0); // O_NOFOLLOW
        if (fd < 0)
                return;
        put = audio_call(syscall(write), (positive)fd, (positive)text, (positive)length, 0, 0, 0);
        audio_call(syscall(close), (positive)fd, 0, 0, 0, 0, 0);
        // A short or failed write is not put over the file that is there: the new one is taken away and the old one stays.
        if (put != length)
        {
                audio_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)temporary, 0, 0, 0, 0);
                return;
        }
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

static void audio_inputs_choose(void);
static void audio_inputs_levels(void);
static void audio_inputs_pass(int first);

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
        // The person's switch for capture: on unless the file says off (and a file taken away is on).
        {
                int on = 1;

                if (audio_file_read("audio.capture", text, sizeof(text)))
                        on = !(text[0] == 'o' && text[1] == 'f');
                if (first || on != audio_service.applied_capture)
                {
                        audio_service.applied_capture = on;
                        pulse_capture_allow(&audio_service.pulse, on);
                }
        }
        // The default source: the person's choice of it by name, and its level.
        if (audio_file_read("audio.source", text, sizeof(text)))
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
                if (!audio_text_equal(wanted, audio_service.applied_source))
                {
                        audio_copy_text(audio_service.applied_source, sizeof(audio_service.applied_source), wanted);
                        audio_copy_text(audio_service.wanted_source, sizeof(audio_service.wanted_source), wanted);
                        if (!first)
                                audio_inputs_choose();
                }
        }
        if (audio_file_read("audio.source.volume", text, sizeof(text)) && (percent = audio_percent(text)) >= 0)
        {
                unsigned long volume = (unsigned long)percent * AUDIO_VOLUME_NORM / 100;

                if (first || volume != audio_service.applied_in_volume)
                {
                        audio_service.applied_in_volume = volume;
                        audio_inputs_levels();
                        if (!first && audio_service.pulse.default_source >= 0)
                                pulse_event(&audio_service.pulse, PULSE_FACILITY_SOURCE, PULSE_EVENT_CHANGE, (unsigned long)audio_service.pulse.default_source);
                }
        }
        if (audio_file_read("audio.source.mute", text, sizeof(text)))
        {
                int mute = text[0] == 'o' && text[1] == 'n';

                if (first || mute != audio_service.applied_in_mute)
                {
                        audio_service.applied_in_mute = mute;
                        audio_inputs_levels();
                        if (!first && audio_service.pulse.default_source >= 0)
                                pulse_event(&audio_service.pulse, PULSE_FACILITY_SOURCE, PULSE_EVENT_CHANGE, (unsigned long)audio_service.pulse.default_source);
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
        unsigned long percent = sink->volume[0] * 100 / AUDIO_VOLUME_NORM;
        char text[16];
        long length;

        // The file keeps whole percents and the read takes them back as percent * NORM / 100: that is what the server last said, so the read is not a change.
        audio_service.applied_volume = percent * AUDIO_VOLUME_NORM / 100;
        audio_service.applied_mute = sink->muted;
        length = audio_number_text(text, percent);
        text[length++] = '\n';
        audio_file_write("audio.volume", text, length);
        audio_file_write("audio.mute", sink->muted ? "on\n" : "off\n", sink->muted ? 3 : 4);
}

// The default source's level, and which source it is, as a program left them (a program's change of either is kept; the service's own is not).
static void audio_inputs_save(int bits)
{
        struct pulse *p = &audio_service.pulse;
        char text[16];
        long length;

        if (p->default_source < 0)
                return;
        if (bits & 4)
        {
                unsigned long percent = p->source[p->default_source].volume[0] * 100 / AUDIO_VOLUME_NORM;

                audio_service.applied_in_volume = percent * AUDIO_VOLUME_NORM / 100; // what the file says, read back (as audio_settings_write)
                audio_service.applied_in_mute = p->source[p->default_source].muted;
                length = audio_number_text(text, percent);
                text[length++] = '\n';
                audio_file_write("audio.source.volume", text, length);
                audio_file_write("audio.source.mute", p->source[p->default_source].muted ? "on\n" : "off\n", p->source[p->default_source].muted ? 3 : 4);
        }
        if (bits & 8)
        {
                char line[PULSE_NAME + 16];
                long n = audio_text_length(p->source[p->default_source].name);

                audio_copy_text(audio_service.applied_source, sizeof(audio_service.applied_source), p->source[p->default_source].name);
                audio_copy_text(audio_service.wanted_source, sizeof(audio_service.wanted_source), p->source[p->default_source].name);
                audio_copy_text(line, sizeof(line), p->source[p->default_source].name);
                line[n] = '\n';
                line[n + 1] = 0;
                audio_file_write("audio.source", line, n + 1);
        }
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
        if (at >= 0)
        {
                // Moved to the end: the table is a list in the order the volumes were last changed, and the oldest is the one let go when it is full.
                __typeof__(audio_service.apps[0]) kept = audio_service.apps[at];

                for (int i = at + 1; i < audio_service.app_count; i++)
                        audio_service.apps[i - 1] = audio_service.apps[i];
                audio_service.apps[audio_service.app_count - 1] = kept;
                at = audio_service.app_count - 1;
        }
        else
        {
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
        if (what == PULSE_CHANGED_SINK_VOLUME)
                audio_service.dirty |= 1;
        // A program changed the default source's level, or which source is the default: kept. (What the service does to the sources itself is not.)
        else if (what == PULSE_CHANGED_SOURCE_VOLUME && sink == audio_service.pulse.default_source && !audio_service.input_internal)
                audio_service.dirty |= 4;
        else if (what == PULSE_CHANGED_DEFAULT_SOURCE && !audio_service.input_internal)
                audio_service.dirty |= 8;
        else
                return;
        if (!audio_service.dirty_since)
                audio_service.dirty_since = clock_monotonic_nanoseconds();
}

// The page: the sink's state, the devices and every stream, copied under the sequence number.
static void audio_page_write(void)
{
        struct audio_state *page = audio_service.page;
        struct pulse *p = &audio_service.pulse;
        struct pulse_sink *sink = p->sink + audio_service.sink;
        struct audio_state build;
        unsigned int seq;

        if (!page || audio_service.sink < 0) // no sink (the sink could not be made): nothing to show, and no sink to index
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
        // The output: a card or a clock, and what the card's mixer was found to be.
        build.output_state = audio_service.pcm.fd >= 0 ? AUDIO_STATE_OUT_CARD : AUDIO_STATE_OUT_CLOCK;
        if (audio_service.hw_done)
        {
                build.hw_controls = (unsigned int)audio_service.hw.output;
                build.hw_changed = (unsigned int)audio_service.hw.changed;
                build.hw_off = (unsigned int)audio_service.hw.left_off;
                audio_copy_text(build.hw_note, sizeof(build.hw_note), audio_service.hw.first_off);
                build.output_state |= (audio_service.hw.changed ? AUDIO_STATE_OUT_HW_SET : 0) | (audio_service.hw.left_off ? AUDIO_STATE_OUT_HW_OFF : 0) | (audio_service.hw.output ? 0 : AUDIO_STATE_OUT_NO_MIXER);
        }
        // The input half: the default source's level and peak, the capture devices, and the programs that record.
        {
                struct pulse_source *in = p->default_source >= 0 ? p->source + p->default_source : 0;

                build.capture = !p->capture_off;
                if (in)
                {
                        build.input_volume = (unsigned int)in->volume[0];
                        build.input_muted = (unsigned int)in->muted;
                        build.input_peak_left = (unsigned int)(in->peak[0] * 32767.0f > 32767.0f ? 32767.0f : in->peak[0] * 32767.0f);
                        build.input_peak_right = (unsigned int)(in->peak[1] * 32767.0f > 32767.0f ? 32767.0f : in->peak[1] * 32767.0f);
                        in->peak[0] = in->peak[1] = 0;
                        audio_copy_text(build.input, sizeof(build.input), in->description);
                        audio_copy_text(build.input_name, sizeof(build.input_name), in->name);
                }
                for (long i = 0; i < PULSE_SOURCES && build.inputs < AUDIO_STATE_INPUTS; i++)
                        if (p->source[i].used)
                        {
                                audio_copy_text(build.input_list[build.inputs].name, sizeof(build.input_list[0].name), p->source[i].name);
                                audio_copy_text(build.input_list[build.inputs].description, sizeof(build.input_list[0].description), p->source[i].description);
                                build.input_list[build.inputs].flags = (i == p->default_source ? AUDIO_STATE_DEVICE_ACTIVE : 0) | (p->source[i].capture ? 0 : AUDIO_STATE_DEVICE_MONITOR);
                                build.inputs++;
                        }
                for (int i = 0; i < AUDIO_STATE_RECORDS; i++)
                        build.record[i].index = 0xffffffffu;
                for (int i = 0; i < PULSE_RECORDS; i++)
                {
                        struct pulse_record *r = p->record + i;
                        struct pulse_client *c;
                        struct audio_state_stream *out;
                        unsigned long sum = 0;

                        if (!r->used || r->client < 0 || !(c = p->client[r->client]) || build.records >= AUDIO_STATE_RECORDS)
                                continue;
                        out = build.record + build.records++;
                        out->index = (unsigned int)r->index;
                        out->client = (unsigned int)c->index;
                        for (long k = 0; k < r->map_channels; k++)
                                sum += r->volume[k];
                        out->volume = (unsigned int)(r->map_channels ? sum / (unsigned long)r->map_channels : 0);
                        out->flags = (r->muted ? AUDIO_STATE_MUTED : 0) | (r->corked ? AUDIO_STATE_CORKED : AUDIO_STATE_PLAYING) | (!p->source[r->source].capture ? AUDIO_STATE_MONITOR : 0);
                        out->peak_left = (unsigned int)(r->peak[0] * 32767.0f > 32767.0f ? 32767.0f : r->peak[0] * 32767.0f);
                        out->peak_right = (unsigned int)(r->peak[1] * 32767.0f > 32767.0f ? 32767.0f : r->peak[1] * 32767.0f);
                        r->peak[0] = r->peak[1] = 0;
                        out->pid = (int)c->pid;
                        out->uid = (unsigned int)c->uid;
                        pulse_props_text(&c->props, "application.name", out->app, sizeof(out->app));
                        pulse_props_text(&r->props, "media.name", out->media, sizeof(out->media));
                        pulse_props_text(&c->props, "application.icon_name", out->icon, sizeof(out->icon));
                }
        }
        seq = __atomic_load_n(&page->seq, __ATOMIC_RELAXED);
        __atomic_store_n(&page->seq, seq + 1, __ATOMIC_RELAXED); // odd: being written
        // A release store orders what is before it, not what follows: the copy's stores could be moved above the odd mark (or land before it
        // on arm64), and a reader that copied a torn page would find the same even number at both ends. The fence keeps them after it.
        __atomic_thread_fence(__ATOMIC_RELEASE);
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
        audio_inputs_pass(0);
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

/* ---- the loop's own numbers ---- */

static void audio_bucket(unsigned long *counts, positive ns)
{
        positive us = ns / 1000;
        int at = 0;

        while (us && at < 15)
        {
                us >>= 1;
                at++;
        }
        counts[at]++;
}

static void audio_stats_say(const char *why)
{
        static const char *names[16] = {"<1", "<2", "<4", "<8", "<16", "<32", "<64", "<128", "<256", "<512", "<1k", "<2k", "<4k", "<8k", "<16k", "more"};

        string_format(log, "audio: %s: %p wakes, the worst pass %p us, the worst lateness of the clock %p us, the least the card held at a wake %b of %b frames (a period is %b, %p wakes were more than half a period late), %p underruns\n", why,
                      (positive)audio_service.stats.wakes, (positive)(audio_service.stats.worst_pass / 1000), (positive)(audio_service.stats.worst_late / 1000),
                      (bipolar)audio_service.stats.min_fill, (bipolar)audio_service.stats.target, (bipolar)audio_service.pcm.period, (positive)audio_service.stats.slips,
                      (positive)audio_service.pcm.xruns);
        for (int kind = 0; kind < 2; kind++)
        {
                unsigned long *counts = kind ? audio_service.stats.late : audio_service.stats.pass;

                string_format(log, "audio:   %s (us):", kind ? "lateness" : "pass");
                for (int at = 0; at < 16; at++)
                        if (counts[at])
                                string_format(log, " %s=%p", names[at], (positive)counts[at]);
                string_format(log, "\n");
        }
        if (audio_service.capture.opens)
        {
                string_format(log, "audio: capture: %p opens (the longest %p us), %p wakes, the worst pass %p us, %p overruns, %p frames\n", (positive)audio_service.capture.opens,
                              (positive)(audio_service.capture.worst_open / 1000), (positive)audio_service.capture.wakes, (positive)(audio_service.capture.worst_pass / 1000),
                              (positive)audio_service.capture.overruns, (positive)audio_service.capture.frames);
                string_format(log, "audio:   capture pass (us):");
                for (int at = 0; at < 16; at++)
                        if (audio_service.capture.pass[at])
                                string_format(log, " %s=%p", names[at], (positive)audio_service.capture.pass[at]);
                string_format(log, "\n");
        }
        log_flush();
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
        for (int reads = 0; reads < AUDIO_READS && audio_service.budget > 0; reads++)
        {
                long got = audio_call(syscall(recvfrom), (positive)audio_service.fd[slot], (positive)audio_service.read, AUDIO_READ, 0x40, 0, 0); // MSG_DONTWAIT

                if (got == 0)
                        return 0;
                if (got < 0)
                        return got == -11 || got == -4; // EAGAIN, EINTR
                audio_service.budget -= got;
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
        positive began = clock_monotonic_nanoseconds();

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
        audio_service.stats.wakes++;
        audio_service.stats.target = pcm->target;
        if (audio_service.stats.wakes > 8 && (audio_service.stats.min_fill == 0 || fill < audio_service.stats.min_fill))
                audio_service.stats.min_fill = fill ? fill : 1;
        // A wake that came more than half a period after it was due: the card had played a period and a half or more since the last refill.
        if (audio_service.stats.wakes > 8 && fill + pcm->period + pcm->period / 2 < pcm->target)
                audio_service.stats.slips++;
        if (want > 0)
                audio_produce(want);
        {
                long delay = audio_pcm_delay(pcm);
                positive took = clock_monotonic_nanoseconds() - began;

                if (delay >= 0)
                        pulse_sink_delay(&audio_service.pulse, audio_service.sink, delay);
                audio_bucket(audio_service.stats.pass, took);
                if (took > audio_service.stats.worst_pass)
                        audio_service.stats.worst_pass = took;
        }
}

// The clock ticked: as many periods as passed since the last pass, within reason.
static void audio_clock_pass(void)
{
        unsigned long long expirations = 0;
        long got = audio_call(syscall(read), (positive)audio_service.timer, (positive)&expirations, 8, 0, 0, 0);
        positive began = clock_monotonic_nanoseconds(), period_ns = (positive)AUDIO_PERIOD * 1000000000ull / (positive)(audio_service.rate ? audio_service.rate : 48000), took;

        if (got != 8 || !expirations)
                return;
        // How late this wake is: the clock's first expiry of the ones it counted was due at the deadline before the ones it counts now.
        if (audio_service.stats.due)
        {
                positive due = audio_service.stats.due;
                positive late = began > due ? began - due : 0;

                audio_bucket(audio_service.stats.late, late);
                if (late > audio_service.stats.worst_late)
                        audio_service.stats.worst_late = late;
        }
        audio_service.stats.due = (audio_service.stats.due ? audio_service.stats.due : began) + (positive)expirations * period_ns;
        audio_service.stats.wakes++;
        if (expirations > 16)
                expirations = 16;
        // A tick is one period, AUDIO_PERIOD frames at the rate (period_ns above is that many frames of it): the frames are not scaled from 48000.
        audio_produce((long)expirations * AUDIO_PERIOD);
        pulse_sink_delay(&audio_service.pulse, audio_service.sink, 0);
        took = clock_monotonic_nanoseconds() - began;
        audio_bucket(audio_service.stats.pass, took);
        if (took > audio_service.stats.worst_pass)
                audio_service.stats.worst_pass = took;
}

/* ---- the capture devices ---- */

/*
        Every capture device of every card is a source of the engine (a program picks its microphone from the list), and its
        PCM is opened only while a program listens to it, and a second after: a microphone nobody records from is not
        running, powers nothing, and is free for another program of the machine that wants the card itself.
*/

struct audio_found
{
        long card, device;
        char name[PULSE_NAME], description[PULSE_DESCRIPTION];
};

// The capture devices there are, at most room of them, in the order of the cards.
static int audio_inputs_scan(struct audio_found *found, int room)
{
        static struct audio_card cards[AUDIO_ALSA_CARDS];
        long n = audio_cards_scan(cards, AUDIO_ALSA_CARDS);
        int count = 0;

        for (long i = 0; i < n; i++)
                for (long d = 0; d < cards[i].devices && count < room; d++)
                        if (cards[i].device[d].capture)
                        {
                                found[count].card = cards[i].number;
                                found[count].device = cards[i].device[d].device;
                                audio_device_name(found[count].name, sizeof(found[count].name), "alsa_input.", cards + i, cards[i].device[d].device);
                                audio_describe(found[count].description, sizeof(found[count].description), cards[i].name, cards[i].device[d].name);
                                count++;
                        }
        return count;
}

static void audio_input_close(int k)
{
        if (audio_service.input[k].pcm.fd >= 0)
        {
                audio_pcm_close(&audio_service.input[k].pcm);
                audio_service.inputs_open--;
        }
        audio_service.input[k].pcm.fd = -1;
}

// The person's levels for the input (the files /root/audio.source.volume and .mute), put on the default source.
static void audio_inputs_levels(void)
{
        struct pulse *p = &audio_service.pulse;

        if (p->default_source < 0)
                return;
        p->source[p->default_source].volume[0] = p->source[p->default_source].volume[1] = audio_service.applied_in_volume;
        p->source[p->default_source].muted = audio_service.applied_in_mute;
}

/*
        The default source: the one the person named (audio.source) if there is such a source, else the capture device on the
        card the sound is played on (a laptop's own microphone is on its own card), else the first. Programs that follow the
        default are moved to it.
*/
static void audio_inputs_choose(void)
{
        struct pulse *p = &audio_service.pulse;
        long pick = -1, rank = 0;

        audio_service.input_internal++;
        if (audio_service.wanted_source[0])
                pick = pulse_source_named(p, audio_service.wanted_source);
        for (int k = 0; k < AUDIO_INPUTS && pick < 0; k++)
                if (audio_service.input[k].used)
                {
                        long this = audio_service.pcm.fd >= 0 && audio_service.input[k].card == audio_service.pcm.card ? 2 : 1;

                        if (this > rank)
                        {
                                rank = this;
                                pick = audio_service.input[k].slot;
                        }
                }
        if (pick >= 0 && pick != p->default_source)
        {
                pulse_source_default(p, pick);
                audio_inputs_levels();
        }
        audio_service.input_internal--;
}

// The cards were looked at again: a capture device that came is a source, one that went is not, and the default is chosen if anything changed.
static void audio_inputs_pass(int first)
{
        struct audio_found found[AUDIO_INPUTS];
        int count, changed = first;

        if (audio_service.no_card)
                return;
        count = audio_inputs_scan(found, AUDIO_INPUTS);
        audio_service.input_internal++;
        for (int k = 0; k < AUDIO_INPUTS; k++)
        {
                int still = 0;

                if (!audio_service.input[k].used)
                        continue;
                for (int f = 0; f < count; f++)
                        still |= audio_text_equal(found[f].name, audio_service.input[k].name);
                if (!still)
                {
                        audio_say_text("a capture device is gone:", audio_service.input[k].description);
                        audio_input_close(k);
                        audio_service.input[k].opening = 0;
                        pulse_source_remove(&audio_service.pulse, audio_service.input[k].slot);
                        audio_service.input[k].used = 0;
                        changed = 1;
                }
        }
        for (int f = 0; f < count; f++)
        {
                int present = 0, free_at = -1;

                for (int k = 0; k < AUDIO_INPUTS; k++)
                {
                        present |= audio_service.input[k].used && audio_text_equal(found[f].name, audio_service.input[k].name);
                        if (!audio_service.input[k].used && free_at < 0)
                                free_at = k;
                }
                if (!present && free_at >= 0)
                {
                        long slot = pulse_source_add(&audio_service.pulse, found[f].name, found[f].description, 48000);

                        if (slot < 0)
                                continue;
                        __builtin_memset(&audio_service.input[free_at], 0, sizeof(audio_service.input[free_at]));
                        audio_service.input[free_at].used = 1;
                        audio_service.input[free_at].slot = slot;
                        audio_service.input[free_at].card = found[f].card;
                        audio_service.input[free_at].device = found[f].device;
                        audio_service.input[free_at].pcm.fd = -1;
                        audio_copy_text(audio_service.input[free_at].name, sizeof(audio_service.input[free_at].name), found[f].name);
                        audio_copy_text(audio_service.input[free_at].description, sizeof(audio_service.input[free_at].description), found[f].description);
                        string_format(log, "audio: a capture device: %s\n", found[f].description);
                        log_flush();
                        changed = 1;
                }
        }
        audio_service.input_internal--;
        if (changed)
                audio_inputs_choose();
}

// What the thread does: opens a capture device, or closes one, and says so on the pipe.
static fn audio_job_entry(address_any argument)
{
        positive began = clock_monotonic_nanoseconds();
        char byte = 'x';

        (void)argument;
        if (audio_service.job.kind == AUDIO_JOB_OPEN)
        {
                audio_service.job.result = audio_pcm_open(&audio_service.job.pcm, audio_service.job.card, audio_service.job.device, AUDIO_ALSA_CAPTURE, 48000, AUDIO_PERIOD, -1);
                __builtin_memset(&audio_service.job.mix, 0, sizeof(audio_service.job.mix));
                if (audio_service.job.result == 0 && !audio_service.leave_mixer)
                        audio_mixer_prepare(audio_service.job.card, 2, &audio_service.job.mix);
        }
        else
        {
                audio_pcm_close(&audio_service.job.pcm);
                audio_service.job.result = 0;
        }
        audio_service.job.took = clock_monotonic_nanoseconds() - began;
        __atomic_store_n(&audio_service.job.done, 1, __ATOMIC_RELEASE);
        audio_call(syscall(write), (positive)audio_service.wake[1], (positive)&byte, 1, 0, 0, 0);
}

// A job for the thread, if it has none; 1 when it was started. For a close the descriptor goes to the thread and the input has none.
static int audio_job_start(int kind, int k)
{
        if (audio_service.job.running)
                return 0;
        audio_service.job.kind = kind;
        audio_service.job.k = k;
        __atomic_store_n(&audio_service.job.done, 0, __ATOMIC_RELEASE);
        if (kind == AUDIO_JOB_OPEN)
        {
                audio_service.job.card = audio_service.input[k].card;
                audio_service.job.device = audio_service.input[k].device;
        }
        else
                audio_service.job.pcm = audio_service.input[k].pcm;
        audio_service.job.handle = thread_start(audio_job_entry, null);
        if (!audio_service.job.handle)
                return 0;
        audio_service.job.running = 1;
        if (kind == AUDIO_JOB_CLOSE)
        {
                audio_service.input[k].pcm.fd = -1;
                audio_service.inputs_open--;
        }
        else
                audio_service.input[k].opening = 1;
        return 1;
}

// The thread has done its job: joined, and what it opened is the input's.
static void audio_job_finish(void)
{
        char junk[16];
        int k;

        audio_call(syscall(read), (positive)audio_service.wake[0], (positive)junk, sizeof(junk), 0, 0, 0);
        if (!audio_service.job.running || !__atomic_load_n(&audio_service.job.done, __ATOMIC_ACQUIRE))
                return;
        thread_join(audio_service.job.handle);
        audio_service.job.running = 0;
        audio_service.job.handle = null;
        if (audio_service.job.kind != AUDIO_JOB_OPEN)
                return;
        k = audio_service.job.k;
        audio_service.input[k].opening = 0;
        if (audio_service.job.result == 0 && audio_service.job.pcm.channels > AUDIO_CHANNELS_MAX)
        {
                audio_pcm_close(&audio_service.job.pcm);
                audio_service.job.result = -22;
        }
        if (audio_service.job.result < 0)
        {
                audio_service.input[k].retry_at = clock_monotonic_nanoseconds() + AUDIO_CAPTURE_RETRY_NS;
                if (!audio_service.input[k].failed && audio_service.input[k].used)
                        audio_say("a capture device could not be opened, error", audio_service.job.result);
                audio_service.input[k].failed = 1;
                return;
        }
        if (!audio_service.input[k].used || audio_service.input[k].card != audio_service.job.card || audio_service.input[k].device != audio_service.job.device)
        {
                audio_pcm_close(&audio_service.job.pcm); // the device went while it was being opened (and another may have its place)
                return;
        }
        audio_service.input[k].failed = 0;
        audio_service.input[k].pcm = audio_service.job.pcm;
        audio_service.inputs_open++;
        audio_service.capture.opens++;
        if (audio_service.job.took > audio_service.capture.worst_open)
                audio_service.capture.worst_open = audio_service.job.took;
        string_format(log, "audio: capturing from %s at %p Hz, %p channels, a period of %p frames; opened in %p us; the card's capture controls: %p, %p set, %p still off\n", audio_service.input[k].description,
                      (positive)audio_service.job.pcm.rate, (positive)audio_service.job.pcm.channels, (positive)audio_service.job.pcm.period, (positive)(audio_service.job.took / 1000),
                      (positive)audio_service.job.mix.capture, (positive)audio_service.job.mix.changed, (positive)audio_service.job.mix.left_off);
        log_flush();
        if (audio_service.job.pcm.rate != audio_service.pulse.source[audio_service.input[k].slot].rate)
        {
                audio_service.input_internal++;
                pulse_source_reconfigure(&audio_service.pulse, audio_service.input[k].slot, audio_service.input[k].name, audio_service.input[k].description, audio_service.job.pcm.rate);
                audio_service.input_internal--;
        }
}

// A capture device opened here, when no thread could be made for it.
static void audio_input_open_now(int k, positive now)
{
        struct audio_pcm pcm;
        long result = audio_pcm_open(&pcm, audio_service.input[k].card, audio_service.input[k].device, AUDIO_ALSA_CAPTURE, 48000, AUDIO_PERIOD, -1);

        if (result == 0 && pcm.channels > AUDIO_CHANNELS_MAX)
        {
                audio_pcm_close(&pcm);
                result = -22;
        }
        if (result < 0)
        {
                audio_service.input[k].retry_at = now + AUDIO_CAPTURE_RETRY_NS;
                audio_service.input[k].failed = 1;
                return;
        }
        audio_service.input[k].pcm = pcm;
        audio_service.inputs_open++;
        audio_service.capture.opens++;
}

// A capture device that has data: read, decoded to the pair, and given to the engine, a few blocks at most so the sound waits for nothing.
static void audio_capture_pass(int k)
{
        struct audio_pcm *pcm = &audio_service.input[k].pcm;
        long slot = audio_service.input[k].slot;
        positive began = clock_monotonic_nanoseconds(), took;
        float *planes[AUDIO_CHANNELS_MAX];

        for (int rounds = 0; rounds < AUDIO_CAPTURE_ROUNDS; rounds++)
        {
                long avail = audio_pcm_avail(pcm), want, got;

                if (avail < 0)
                {
                        long error = audio_pcm_recover(pcm, avail);

                        if (pcm->broken)
                        {
                                audio_say("a capture device is gone, error", error);
                                audio_input_close(k);
                                audio_service.input[k].retry_at = began + AUDIO_CAPTURE_RETRY_NS;
                                return;
                        }
                        if (avail == AUDIO_EPIPE)
                                audio_service.capture.overruns++;
                        return;
                }
                if (avail == 0)
                        break;
                want = avail < AUDIO_BLOCK ? avail : AUDIO_BLOCK;
                got = audio_pcm_read(pcm, audio_service.in_block, want);
                if (got <= 0)
                {
                        if (pcm->broken)
                        {
                                audio_input_close(k);
                                audio_service.input[k].retry_at = began + AUDIO_CAPTURE_RETRY_NS;
                        }
                        break;
                }
                for (long c = 0; c < pcm->channels; c++)
                        planes[c] = audio_service.in_planes[c];
                audio_decode(pcm->format, audio_service.in_block, got, pcm->channels, planes);
                pulse_source_feed(&audio_service.pulse, slot, planes[0], pcm->channels > 1 ? planes[1] : planes[0], got);
                audio_service.capture.frames += (unsigned long long)got;
                if (got < want)
                        break;
        }
        if (pcm->fd >= 0)
        {
                long delay = audio_pcm_delay(pcm);

                if (delay >= 0)
                        pulse_source_delay(&audio_service.pulse, slot, delay);
        }
        took = clock_monotonic_nanoseconds() - began;
        audio_service.capture.wakes++;
        audio_bucket(audio_service.capture.pass, took);
        if (took > audio_service.capture.worst_pass)
                audio_service.capture.worst_pass = took;
}

// Every pass: a capture device that a program has begun to listen to is opened, one that none has listened to for a second is closed.
static void audio_inputs_run(positive now)
{
        struct pulse *p = &audio_service.pulse;

        if (p->records_made == p->records_freed && !audio_service.inputs_open)
                return;
        for (int k = 0; k < AUDIO_INPUTS; k++)
        {
                long wanted;

                if (!audio_service.input[k].used)
                        continue;
                wanted = pulse_source_wanted(p, audio_service.input[k].slot);
                if (wanted)
                {
                        audio_service.input[k].idle_since = 0;
                        if (audio_service.input[k].pcm.fd < 0 && !audio_service.input[k].opening && now >= audio_service.input[k].retry_at && !audio_job_start(AUDIO_JOB_OPEN, k) && !audio_service.job.running)
                                audio_input_open_now(k, now);
                }
                else if (audio_service.input[k].pcm.fd >= 0)
                {
                        if (!audio_service.input[k].idle_since)
                                audio_service.input[k].idle_since = now;
                        else if (now - audio_service.input[k].idle_since >= AUDIO_CAPTURE_LINGER_NS)
                        {
                                if (audio_job_start(AUDIO_JOB_CLOSE, k))
                                {
                                        audio_service.input[k].idle_since = 0;
                                        audio_say_text("stopped capturing from", audio_service.input[k].description);
                                }
                                else if (!audio_service.job.running)
                                {
                                        audio_input_close(k);
                                        audio_service.input[k].idle_since = 0;
                                }
                        }
                }
        }
}

/* ---- a tone, to hear ---- */

/*
        `audio -t` (and `moonwater audio test`): a second of 440 Hz played through the server as a program of the desktop plays
        its sound, and what happened said in words. It is a client like any other: it connects to the socket, makes a
        playback stream, sends the samples, drains, and reads the server's page for what the server says of the output. Its
        exit is 0 when a tone was sent and played out, and 1 when it could not be: the verdict is for the person, and says
        what to look at.
*/
static int audio_test_page(struct audio_state *copy)
{
        static unsigned char raw[AUDIO_STATE_BYTES];
        long fd = audio_call(syscall(openat), (positive)AT_FDCWD, (positive)audio_service.page_path, 0x80000, 0, 0, 0), got;

        if (fd < 0)
                return 0;
        got = audio_call(syscall(read), (positive)fd, (positive)raw, sizeof(raw), 0, 0, 0);
        audio_call(syscall(close), (positive)fd, 0, 0, 0, 0, 0);
        if (got < (long)sizeof(*copy))
                return 0;
        __builtin_memcpy(copy, raw, sizeof(*copy));
        return copy->magic == AUDIO_STATE_MAGIC && copy->version == AUDIO_STATE_VERSION && copy->bytes == sizeof(*copy) && !(copy->seq & 1u);
}

static b32 audio_test(void)
{
        static struct ctl c;
        static struct audio_state page;
        static unsigned char frame[20 + 65536];
        static short samples[48000 * 2];
        struct ctl_w w;
        struct ctl_r r;
        unsigned long channel;
        long got, sent = 0, total = 48000 * 4;
        short *tone = (short *)(frame + 20);
        int have_page;
        static const unsigned long both[2] = {0x10000, 0x10000};

        c.timeout_ms = 4000;
        if (ctl_connect(&c, audio_service.socket_path) < 0)
        {
                string_format(log, "audio: no sound server answers on %s: nothing can be heard. Start it with the desktop, or `moonwater audio server on`.\n", audio_service.socket_path);
                log_flush();
                return 1;
        }
        // The stream: 16 bits, two channels, 48000 Hz, a second of buffer which starts when it is full; the properties name it for the mixer.
        ctl_begin(&c, &w, 3);
        ctl_w_byte(&w, 'a');
        ctl_w_byte(&w, 3);
        ctl_w_byte(&w, 2);
        ctl_w_be32(&w, 48000);
        ctl_w_byte(&w, 'm');
        ctl_w_byte(&w, 2);
        ctl_w_byte(&w, 1);
        ctl_w_byte(&w, 2);
        ctl_w_u32(&w, CTL_NONE);
        ctl_w_string(&w, 0);
        ctl_w_u32(&w, CTL_NONE);
        ctl_w_bool(&w, 0);
        ctl_w_u32(&w, (unsigned long)total);
        ctl_w_u32(&w, (unsigned long)total);
        ctl_w_u32(&w, CTL_NONE);
        ctl_w_u32(&w, 0);
        ctl_w_cvolume(&w, both, 2);
        for (int i = 0; i < 7; i++)
                ctl_w_bool(&w, 0);
        ctl_w_bool(&w, 0);
        ctl_w_bool(&w, 0);
        ctl_w_byte(&w, 'P');
        ctl_w_string(&w, "media.name");
        ctl_w_u32(&w, 5);
        ctl_w_byte(&w, 'x');
        ctl_w_be32(&w, 5);
        ctl_w_raw(&w, "Test", 5);
        ctl_w_string(&w, "media.role");
        ctl_w_u32(&w, 5);
        ctl_w_byte(&w, 'x');
        ctl_w_be32(&w, 5);
        ctl_w_raw(&w, "test", 5);
        ctl_w_byte(&w, 'N');
        ctl_w_bool(&w, 1);
        ctl_w_bool(&w, 0);
        ctl_w_bool(&w, 0);
        ctl_w_bool(&w, 0);
        ctl_w_bool(&w, 0);
        ctl_w_bool(&w, 0);
        ctl_w_bool(&w, 0);
        ctl_w_byte(&w, 'B');
        ctl_w_byte(&w, 0);
        got = w.bad ? CTL_FAILED : ctl_exchange(&c, w.len);
        if (got < 10)
        {
                string_format(log, "audio: the sound server refused a stream (%s): nothing can be heard.\n", got == CTL_ACCESS ? "access denied" : got == CTL_INVALID ? "not understood" : "no answer");
                log_flush();
                ctl_disconnect(&c);
                return 1;
        }
        r = (struct ctl_r){c.frame + 20 + 10, got - 10, 0, 0};
        channel = ctl_r_u32(&r);
        // The tone: a quarter of full scale, with a short rise and fall so that it begins and ends without a click.
        for (long i = 0; i < 48000; i++)
        {
                double amplitude = 8192.0, edge = i < 480 ? (double)i / 480.0 : i > 48000 - 480 ? (double)(48000 - i) / 480.0 : 1.0;
                short v = (short)(amplitude * edge * audio_sin(2.0 * 3.14159265358979323846 * 440.0 * (double)i / 48000.0));

                samples[2 * i] = v;
                samples[2 * i + 1] = v;
        }
        ctl_budget(&c);
        while (sent < total)
        {
                long n = total - sent < 65536 ? total - sent : 65536;

                ctl_put_be(frame, (unsigned long)n);
                ctl_put_be(frame + 4, channel);
                ctl_put_be(frame + 8, 0), ctl_put_be(frame + 12, 0), ctl_put_be(frame + 16, 0);
                __builtin_memcpy(tone, (unsigned char *)samples + sent, (unsigned long)n);
                if (c.transport.send(c.transport.context, frame, 20 + n, ctl_left(&c)) < 0)
                {
                        string_format(log, "audio: the connection to the sound server broke while the tone was sent.\n");
                        log_flush();
                        ctl_disconnect(&c);
                        return 1;
                }
                sent += n;
        }
        // Wait until it has been played out (drained), at most the connection's timeout.
        ctl_begin(&c, &w, 12);
        ctl_w_u32(&w, channel);
        got = ctl_exchange(&c, w.len);
        have_page = audio_test_page(&page);
        ctl_begin(&c, &w, 4);
        ctl_w_u32(&w, channel);
        ctl_exchange(&c, w.len);
        ctl_disconnect(&c);
        if (got < 0)
        {
                string_format(log, "audio: the tone was sent but the server did not say it had played it (%s).\n", got == CTL_FAILED ? "no answer in time" : "refused");
                log_flush();
                return 1;
        }
        if (have_page)
        {
                if (page.output_state & AUDIO_STATE_OUT_CLOCK)
                {
                        string_format(log, "audio: the tone went to NOTHING: the server has no sound card open (it plays to a clock, \"%s\"), so there is no sound to hear. A card that is there but could not be opened is named in the server's log.\n",
                                      (string_address)page.device);
                        log_flush();
                        return 1;
                }
                if (page.output_state & AUDIO_STATE_OUT_HW_OFF)
                {
                        string_format(log, "audio: the tone was played on %s, but the card's own mixer has \"%s\" off and would not take it on: nothing may be heard.\n", (string_address)page.device,
                                      (string_address)page.hw_note);
                        log_flush();
                        return 1;
                }
                if (page.muted || page.volume == 0)
                {
                        string_format(log, "audio: the tone was played on %s, but its volume is %s: nothing can be heard until `moonwater audio %s`.\n", (string_address)page.device,
                                      page.muted ? "muted" : "0 percent", page.muted ? (string_address) "mute off" : (string_address) "volume 100");
                        log_flush();
                        return 1;
                }
                string_format(log, "audio: played a second of 440 Hz on %s (%s), at %p percent%s. If nothing was heard, look at the speakers or the monitor: their power, their cable and their own volume.\n",
                              (string_address)page.device, (string_address)page.name, ((positive)page.volume * 100 + 32768) / 65536,
                              (page.output_state & AUDIO_STATE_OUT_HW_SET) ? (string_address) "; the card's own switches were set on when it was opened" : (string_address) "");
                log_flush();
                return 0;
        }
        string_format(log, "audio: played a second of 440 Hz through the sound server (its page is not readable here, so what it plays on is not said: ask `moonwater audio` as root).\n");
        log_flush();
        return 0;
}

// `moonwater audio test`: the tone, to the server at its own socket and page.
static b32 audio_test_run(void)
{
        audio_service.socket_path = AUDIO_SOCKET;
        audio_service.page_path = AUDIO_STATE_PATH;
        return audio_test();
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
        audio_service.wake[0] = audio_service.wake[1] = -1;
        audio_call(syscall(pipe2), (positive)audio_service.wake, 0x800 | 0x80000, 0, 0, 0, 0); // O_NONBLOCK | O_CLOEXEC
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
        // A path that does not fit sun_path is not cut short: the socket would be bound under a name the server did not ask for.
        if (audio_service.socket_path[length])
        {
                audio_say_text("the socket path is too long", audio_service.socket_path);
                return;
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
        audio_service.applied_in_volume = AUDIO_VOLUME_NORM;
        audio_service.applied_capture = 1;
        for (int k = 0; k < AUDIO_INPUTS; k++)
                audio_service.input[k].pcm.fd = -1;
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
                else if (string_equals(option, "-M"))
                        audio_service.leave_mixer = 1;
                else if (string_equals(option, "-t"))
                        audio_service.test = 1;
        }
        if (rate < 8000 || rate > 384000)
                rate = 48000;
        if (audio_service.test)
                return audio_test();
        audio_start();
        audio_listen();
        if (audio_service.listen < 0 || audio_service.signals < 0)
                return 1;
        audio_settings_read(1); // the person's choice of card first, so it is the one opened
        audio_output_open((long)rate);
        audio_settings_read(1); // and the volume and the mute, now that there is a sink to put them on
        audio_inputs_pass(1);   // the capture devices, and which of them is the default
        audio_apps_load();
        audio_page_open();
        audio_service.started_at = clock_monotonic_nanoseconds();
        string_format(log, "audio: serving %s, sink %s at %p Hz\n", audio_service.socket_path, audio_service.pulse.sink[audio_service.sink].name,
                      (positive)audio_service.rate);
        log_flush();

        while (running)
        {
                struct audio_poll wanted[4 + AUDIO_INPUTS + PULSE_CLIENTS];
                long owner[4 + AUDIO_INPUTS + PULSE_CLIENTS];
                unsigned int count = 0;
                positive now;
                long awaited, wait[2] = {0, audio_busy() ? 16000000 : 250000000};

                // The card (or the clock) is first in the list and so first in the pass: whatever the clients have sent waits behind the sound.
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
                // A microphone that is listened to is next: what it has is as late as it is made to wait.
                for (int k = 0; k < AUDIO_INPUTS; k++)
                        if (audio_service.input[k].used && audio_service.input[k].pcm.fd >= 0)
                        {
                                wanted[count] = (struct audio_poll){audio_service.input[k].pcm.fd, AUDIO_POLLIN, 0};
                                owner[count++] = -10 - k;
                        }
                wanted[count] = (struct audio_poll){audio_service.signals, AUDIO_POLLIN, 0};
                owner[count++] = -1;
                if (audio_service.job.running && audio_service.wake[0] >= 0)
                {
                        wanted[count] = (struct audio_poll){audio_service.wake[0], AUDIO_POLLIN, 0};
                        owner[count++] = -5;
                }
                wanted[count] = (struct audio_poll){audio_service.listen, AUDIO_POLLIN, 0};
                owner[count++] = -2;
                for (long slot = 0; slot < PULSE_CLIENTS; slot++)
                        if (audio_service.fd[slot] >= 0)
                        {
                                wanted[count] = (struct audio_poll){audio_service.fd[slot], (short)(AUDIO_POLLIN | (pulse_client_pending(&audio_service.pulse, slot) ? AUDIO_POLLOUT : 0)), 0};
                                owner[count++] = slot;
                        }
                awaited = audio_call(syscall(ppoll), (positive)wanted, count, (positive)wait, 0, 0, 0);
                now = clock_monotonic_nanoseconds();
                audio_service.budget = 4 * AUDIO_READ;
                if (awaited >= 0)
                        for (unsigned int at = 0; at < count; at++)
                        {
                                if (!wanted[at].returned)
                                        continue;
                                if (owner[at] == -1)
                                {
                                        unsigned char info[128];

                                        // SIGUSR1 (10) says the numbers and goes on; the others end it.
                                        if (audio_call(syscall(read), (positive)audio_service.signals, (positive)info, sizeof(info), 0, 0, 0) >= 4 && info[0] == 10)
                                                audio_stats_say("numbers");
                                        else
                                                running = 0;
                                }
                                else if (owner[at] == -2)
                                        audio_accept();
                                else if (owner[at] == -3)
                                        audio_card_pass();
                                else if (owner[at] == -4)
                                        audio_clock_pass();
                                else if (owner[at] == -5)
                                        audio_job_finish();
                                else if (owner[at] <= -10)
                                        audio_capture_pass((int)(-10 - owner[at]));
                                else if (audio_service.fd[owner[at]] >= 0)
                                {
                                        long slot = owner[at];

                                        if ((wanted[at].returned & (AUDIO_POLLIN | AUDIO_POLLHUP | AUDIO_POLLERR)) && !audio_client_read(slot))
                                                audio_client_drop(slot);
                                }
                        }
                // Whatever the pass queued is sent, the drains are answered, and a client that never says who it is, or never reads, is dropped.
                pulse_tick(&audio_service.pulse);
                audio_inputs_run(now);
                for (long slot = 0; slot < PULSE_CLIENTS; slot++)
                        if (audio_service.fd[slot] >= 0)
                        {
                                if (pulse_client_late(&audio_service.pulse, slot, (long)(now / 1000)) || !audio_client_write(slot, now))
                                        audio_client_drop(slot);
                        }
                // The page, the settings, the cards, and what a program changed that is kept.
                if (now - audio_service.state_at >= (audio_busy() ? AUDIO_STATE_NS : AUDIO_STATE_IDLE_NS))
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
                        if (audio_service.dirty & 12)
                                audio_inputs_save(audio_service.dirty);
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
        if (audio_service.dirty & 12)
                audio_inputs_save(audio_service.dirty);
        if (audio_service.job.running)
        {
                thread_join(audio_service.job.handle);
                audio_service.job.running = 0;
                if (audio_service.job.kind == AUDIO_JOB_OPEN && audio_service.job.result == 0)
                        audio_pcm_close(&audio_service.job.pcm);
        }
        for (int k = 0; k < AUDIO_INPUTS; k++)
                audio_input_close(k);
        if (audio_service.page)
                audio_call(syscall(munmap), (positive)audio_service.page, AUDIO_STATE_BYTES, 0, 0, 0, 0);
        audio_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)audio_service.page_path, 0, 0, 0, 0);
        audio_pcm_close(&audio_service.pcm);
        system_close(audio_service.listen);
        audio_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)audio_service.socket_path, 0, 0, 0, 0);
        audio_stats_say("at the end");
        string_format(log, "audio: ended after %p frames, %p underruns\n", (positive)audio_service.frames_played, (positive)audio_service.pcm.xruns);
        log_flush();
        return 0;
}

#endif // MOONWATER_AUDIO_SERVICE
