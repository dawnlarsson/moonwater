/*
        What the sound server shows the rest of the machine: one page of memory, /run/moonwater/pulse/state, that the server
        writes and the mixer, the tray and `moonwater audio` read. It is not a protocol: it is the server's state as numbers,
        small, fixed in size, and read without a call (a program maps the file read-only and copies it).

        The page is written about sixty times a second while any program is playing or any peak is above nothing, and about
        twice a second when all is quiet. A write is bracketed by the sequence number, odd while the server is in the middle
        of it, so a reader that finds it odd, or different at the end of its copy, copies again (audio_state_read).

        A peak is the greatest sample (15 bits, 0 to 32767) since the last write, after the volumes; a reader that draws a
        meter holds it and lets it fall at its own rate. A volume is PulseAudio's (65536 is 100%); the mean of a stream's
        channels. Text is what the program called itself, cleaned (control bytes and bad UTF-8 are question marks), and
        ends in a zero byte.

        The page is for the person's own session (mode 0600, root): what a program calls its stream is what it plays.

        Control goes the other way, over the PulseAudio socket (src/audio/pulse.c) or the settings files (/root/audio.*):
        a mixer is a client of the server like any other.
*/
#ifndef AUDIO_STATE_INCLUDED
#define AUDIO_STATE_INCLUDED

#define AUDIO_STATE_PATH "/run/moonwater/pulse/state"
#define AUDIO_STATE_MAGIC 0x5341574du // "MWAS"
#define AUDIO_STATE_VERSION 1u
#define AUDIO_STATE_STREAMS 32
#define AUDIO_STATE_DEVICES 8
#define AUDIO_STATE_BYTES 8192

#define AUDIO_STATE_MUTED 1u
#define AUDIO_STATE_CORKED 2u
#define AUDIO_STATE_PLAYING 4u

#define AUDIO_STATE_DEVICE_ACTIVE 1u

struct audio_state_stream
{
        unsigned int index;  // the sink input's number, which a volume or a mute names; 0xffffffff for a free place
        unsigned int client; // the program's connection
        unsigned int volume; // 65536 is 100%
        unsigned int flags;  // AUDIO_STATE_MUTED, _CORKED, _PLAYING
        unsigned int peak_left, peak_right;
        int pid;
        unsigned int uid;
        char app[48];   // application.name
        char media[48]; // media.name
        char icon[32];  // application.icon_name
};

struct audio_state_device
{
        char name[64];         // what a program or `moonwater audio device` names it
        char description[96];  // what the person is shown
        unsigned int flags;    // AUDIO_STATE_DEVICE_ACTIVE
        unsigned int reserved;
};

struct audio_state
{
        unsigned int magic, version, seq, bytes;
        unsigned int volume; // the sink's master volume (65536 is 100%)
        unsigned int muted;
        unsigned int peak_left, peak_right;
        unsigned int clips, underruns; // frames that went past full scale, and card underruns, since the server started
        unsigned int rate, period;     // the card's rate, and a period in frames
        unsigned int latency_us;       // what the card holds (the delay of the last write)
        unsigned int streams, devices; // places used
        char device[96];               // the active device as the person is shown it
        char name[64];                 // and as programs name it
        struct audio_state_device device_list[AUDIO_STATE_DEVICES];
        struct audio_state_stream stream[AUDIO_STATE_STREAMS];
};

_Static_assert(sizeof(struct audio_state) <= AUDIO_STATE_BYTES, "the state page fits its file");

/*
        A consistent copy of the page: the sequence number read before and after the copy, even and the same, else again
        (at most eight times: the writer is not stopped, and a page that cannot be copied whole in eight tries is not returned).
        Answers 1 with *copy filled, 0 for a page that is not the server's or is never still.
*/
static int audio_state_read(const volatile struct audio_state *page, struct audio_state *copy)
{
        for (int tries = 0; tries < 8; tries++)
        {
                unsigned int before = __atomic_load_n(&page->seq, __ATOMIC_ACQUIRE), after;

                if (before & 1u)
                        continue;
                __builtin_memcpy(copy, (const void *)page, sizeof(*copy));
                __atomic_thread_fence(__ATOMIC_ACQUIRE);
                after = __atomic_load_n(&page->seq, __ATOMIC_ACQUIRE);
                if (before == after)
                        return copy->magic == AUDIO_STATE_MAGIC && copy->version == AUDIO_STATE_VERSION && copy->bytes == sizeof(*copy);
        }
        return 0;
}

#endif // AUDIO_STATE_INCLUDED
