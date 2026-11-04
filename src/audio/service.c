/*
        The sound server of Moonwater's desktop, `audio`: the process that listens on a socket for programs that play
        sound, mixes them (mix.c, pulse.c) and plays the mix on a sound card (alsa.c).

            audio [-S SOCKET] [-d CARD:DEVICE] [-o FILE] [-n] [-r RATE]

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
        positive rescan_at, started_at;
        unsigned long long frames_played;
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

// The first card with a playback device that is not an HDMI one, else the first with any.
static int audio_card_pick(long *card_out, long *device_out, char *name, long name_room, char *description, long description_room)
{
        static struct audio_card cards[AUDIO_ALSA_CARDS];
        long found = audio_cards_scan(cards, AUDIO_ALSA_CARDS);
        long best = -1, best_device = -1, best_rank = -1;

        for (long i = 0; i < found; i++)
                for (long d = 0; d < cards[i].devices; d++)
                {
                        long rank;
                        int hdmi = 0;
                        const char *label = cards[i].device[d].name;

                        if (!cards[i].device[d].playback)
                                continue;
                        for (long k = 0; label[k]; k++)
                                hdmi |= (label[k] == 'H' || label[k] == 'h') && (label[k + 1] == 'D' || label[k + 1] == 'd') && (label[k + 2] == 'M' || label[k + 2] == 'm') && (label[k + 3] == 'I' || label[k + 3] == 'i');
                        rank = hdmi ? 1 : 2;
                        if (audio_service.card >= 0)
                                rank = cards[i].number == audio_service.card && (audio_service.device < 0 || cards[i].device[d].device == audio_service.device) ? 9 : 0;
                        if (rank > best_rank)
                        {
                                best_rank = rank;
                                best = i;
                                best_device = d;
                        }
                }
        if (best < 0 || best_rank <= 0)
                return 0;
        *card_out = cards[best].number;
        *device_out = cards[best].device[best_device].device;
        audio_sink_name(name, name_room, cards + best, *device_out);
        {
                long at = 0;
                const char *card_name = cards[best].name, *device_label = cards[best].device[best_device].name;

                for (; *card_name && at < description_room - 4; card_name++)
                        description[at++] = *card_name;
                if (*device_label && at < description_room - 6)
                {
                        description[at++] = ' ';
                        description[at++] = '-';
                        description[at++] = ' ';
                        for (; *device_label && at < description_room - 1; device_label++)
                                description[at++] = *device_label;
                }
                description[at] = 0;
        }
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
                        if (audio_service.sink < 0)
                                audio_service.sink = pulse_sink_add(&audio_service.pulse, name, description, pcm.rate);
                        audio_timer_stop();
                        audio_say_text("playing on", description);
                        return 1;
                }
                audio_say("the card could not be opened, error", result);
        }
        audio_service.pcm.fd = -1;
        audio_service.rate = asked_rate;
        if (audio_service.sink < 0)
                audio_service.sink = pulse_sink_add(&audio_service.pulse, "auto_null", "Dummy Output", asked_rate);
        audio_timer_set(audio_service.rate);
        return 0;
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
        for (positive at = 1; at < program_argument_count(); at++)
        {
                string_address option = program_argument(at);

                if (at + 1 < program_argument_count() && string_equals(option, "-S"))
                        audio_service.socket_path = (const char *)program_argument(++at);
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
        audio_output_open((long)rate);
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
                long awaited, wait[2] = {0, 50000000};

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
        audio_pcm_close(&audio_service.pcm);
        system_close(audio_service.listen);
        audio_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)audio_service.socket_path, 0, 0, 0, 0);
        string_format(log, "audio: ended after %p frames, %p underruns\n", (positive)audio_service.frames_played, (positive)audio_service.pcm.xruns);
        log_flush();
        return 0;
}

#endif // MOONWATER_AUDIO_SERVICE
