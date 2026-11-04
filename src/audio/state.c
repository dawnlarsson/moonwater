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

        The input half of the page (version 2): the capture device the programs that record are given (its volume, mute and
        peak, which are the levels of the default source and are only live while something records from it), the list of
        capture devices, whether the person has let programs record at all, and the programs that are recording (a record
        stream is listed as a playback stream is, with the flag AUDIO_STATE_MONITOR when what it records is what the
        machine plays and not a microphone).

        Control goes the other way, over the PulseAudio socket (src/audio/pulse.c) or the settings files (/root/audio.*):
        a mixer is a client of the server like any other.
*/
#ifndef AUDIO_STATE_INCLUDED
#define AUDIO_STATE_INCLUDED

#define AUDIO_STATE_PATH "/run/moonwater/pulse/state"
#define AUDIO_STATE_MAGIC 0x5341574du // "MWAS"
#define AUDIO_STATE_VERSION 3u
#define AUDIO_STATE_STREAMS 32
#define AUDIO_STATE_DEVICES 8
#define AUDIO_STATE_INPUTS 8
#define AUDIO_STATE_RECORDS 16
#define AUDIO_STATE_BYTES 16384

#define AUDIO_STATE_MUTED 1u
#define AUDIO_STATE_CORKED 2u
#define AUDIO_STATE_PLAYING 4u
#define AUDIO_STATE_MONITOR 8u // a record stream: what it records is what a sink plays, not a capture device

// What the output is (output_state): a card the sound goes to, or a clock of the server's own because there was none to open, and
// what the server found of the card's own mixer (the switches and gains of the hardware, which start muted).
#define AUDIO_STATE_OUT_CARD 1u     // the sound is played on a sound card
#define AUDIO_STATE_OUT_CLOCK 2u    // there is no card: the sound goes to nothing, at the pace of a clock, and is not heard
#define AUDIO_STATE_OUT_HW_SET 4u   // the server set the card's output controls (unmuted, raised) when it opened it
#define AUDIO_STATE_OUT_HW_OFF 8u   // and some switch would not stay on: hw_note names the first
#define AUDIO_STATE_OUT_NO_MIXER 16u // the card has no mixer controls of the kinds that matter
#define AUDIO_STATE_OUT_SUSPENDED 32u // no stream has played for five seconds: the card (or the clock) is released; the first stream opens it again

#define AUDIO_STATE_DEVICE_ACTIVE 1u
#define AUDIO_STATE_DEVICE_MONITOR 2u // an input: the monitor of a sink (what the machine plays), not a microphone
#define AUDIO_STATE_DEVICE_UNPLUGGED 4u // an output whose card says nothing is plugged in (no speakers in its jack, no display on its pin)

// Why the output is the one that plays (output_why).
#define AUDIO_STATE_WHY_CHOSEN 1u // the person chose it (moonwater audio device)
#define AUDIO_STATE_WHY_NAMED 2u  // the server was started with -d
#define AUDIO_STATE_WHY_ANALOG 3u // an analog or USB output that can be heard: a speaker, something in a jack, a USB card, or no jack that could tell
#define AUDIO_STATE_WHY_DISPLAY 4u // a display is plugged into it and nothing is in the analog jacks
#define AUDIO_STATE_WHY_HDMI 5u   // an HDMI or DisplayPort output whose pin cannot say whether a display is there
#define AUDIO_STATE_WHY_NOTHING 6u // nothing is plugged in anywhere: the first output there is

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
        // The input half. An index here is a source output's, and a record stream's "client" is its program's connection.
        unsigned int input_volume;     // the default capture source's volume (65536 is 100%)
        unsigned int input_muted;
        unsigned int input_peak_left, input_peak_right;
        unsigned int capture;          // 1: programs may record; 0: the person has turned capture off, and none is
        unsigned int inputs, records;  // places used in the two lists below
        char input[96];                // the default capture device as the person is shown it; empty when there is none
        char input_name[64];           // and as programs name it
        struct audio_state_device input_list[AUDIO_STATE_INPUTS]; // AUDIO_STATE_DEVICE_ACTIVE is the default
        struct audio_state_stream record[AUDIO_STATE_RECORDS];
        // The output's own state (version 3).
        unsigned int output_state;     // AUDIO_STATE_OUT_*
        unsigned int hw_controls;      // output controls of the card's mixer the server looked at
        unsigned int hw_changed;       // and wrote, because they were not as a sound needs them
        unsigned int hw_off;           // and found still off after (the card would not take them)
        char hw_note[48];              // the first of those, as the card names it
        unsigned int output_why;       // AUDIO_STATE_WHY_*: why this output plays
};

_Static_assert(sizeof(struct audio_state) <= AUDIO_STATE_BYTES, "the state page fits its file");

/*
        A consistent copy of the page: the sequence number read before and after the copy, even and the same, else again
        (at most eight times: the writer is not stopped, and a page that cannot be copied whole in eight tries is not returned).
        Answers 1 with *copy filled, 0 for a page that is not the server's or is never still.
*/
// A string of the copy ends inside its own array whatever the page said: the last byte is made a null.
static void audio_state_end(char *text, unsigned long size)
{
        text[size - 1] = 0;
}

/*
        The page is root's and mode 0600, but a reader treats what it holds as untrusted: every count is held within its array,
        every string of the copy ends inside its array (a string the server did not end is cut where its array ends), and an
        output_why that names no reason is 0. After this the copy can be read field by field without a bound of its own.
*/
static void audio_state_trust(struct audio_state *copy)
{
        copy->streams = copy->streams > AUDIO_STATE_STREAMS ? AUDIO_STATE_STREAMS : copy->streams;
        copy->devices = copy->devices > AUDIO_STATE_DEVICES ? AUDIO_STATE_DEVICES : copy->devices;
        copy->inputs = copy->inputs > AUDIO_STATE_INPUTS ? AUDIO_STATE_INPUTS : copy->inputs;
        copy->records = copy->records > AUDIO_STATE_RECORDS ? AUDIO_STATE_RECORDS : copy->records;
        audio_state_end(copy->device, sizeof(copy->device));
        audio_state_end(copy->name, sizeof(copy->name));
        audio_state_end(copy->input, sizeof(copy->input));
        audio_state_end(copy->input_name, sizeof(copy->input_name));
        audio_state_end(copy->hw_note, sizeof(copy->hw_note));
        for (unsigned int k = 0; k < AUDIO_STATE_DEVICES; k++)
        {
                audio_state_end(copy->device_list[k].name, sizeof(copy->device_list[k].name));
                audio_state_end(copy->device_list[k].description, sizeof(copy->device_list[k].description));
        }
        for (unsigned int k = 0; k < AUDIO_STATE_INPUTS; k++)
        {
                audio_state_end(copy->input_list[k].name, sizeof(copy->input_list[k].name));
                audio_state_end(copy->input_list[k].description, sizeof(copy->input_list[k].description));
        }
        for (unsigned int k = 0; k < AUDIO_STATE_STREAMS; k++)
        {
                audio_state_end(copy->stream[k].app, sizeof(copy->stream[k].app));
                audio_state_end(copy->stream[k].media, sizeof(copy->stream[k].media));
                audio_state_end(copy->stream[k].icon, sizeof(copy->stream[k].icon));
        }
        for (unsigned int k = 0; k < AUDIO_STATE_RECORDS; k++)
        {
                audio_state_end(copy->record[k].app, sizeof(copy->record[k].app));
                audio_state_end(copy->record[k].media, sizeof(copy->record[k].media));
                audio_state_end(copy->record[k].icon, sizeof(copy->record[k].icon));
        }
        if (copy->output_why > AUDIO_STATE_WHY_NOTHING)
                copy->output_why = 0;
}

static int audio_state_read(const volatile struct audio_state *page, struct audio_state *copy)
{
        for (int tries = 0; tries < 8; tries++)
        {
                unsigned int before = __atomic_load_n(&page->seq, __ATOMIC_ACQUIRE), after;

                if (before & 1u)
                        continue;
                memory_copy_apart(copy, (const void *)page, sizeof(*copy));
                __atomic_thread_fence(__ATOMIC_ACQUIRE);
                after = __atomic_load_n(&page->seq, __ATOMIC_ACQUIRE);
                if (before == after)
                {
                        audio_state_trust(copy);
                        return copy->magic == AUDIO_STATE_MAGIC && copy->version == AUDIO_STATE_VERSION && copy->bytes == sizeof(*copy);
                }
        }
        return 0;
}

#endif // AUDIO_STATE_INCLUDED
