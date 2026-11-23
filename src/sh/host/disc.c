/*
        moonwater disc [tracks DRIVE|play DRIVE [TRACK]|stop|eject DRIVE]: the optical drives (/sys/block/sr*) and what is in them, through
        the kernel's CD-ROM interface (linux/cdrom.h), which every drive the sr driver takes answers: an internal SATA one or a USB one.

        - disc: a line a drive, "disc sr0 none" (no disc), "disc sr0 open" (its tray is out), "disc sr0 audio TRACKS SECONDS" (a CD of
          music), "disc sr0 data" (a disc with files: opened read-only as any drive's partition is), "disc sr0 mixed TRACKS SECONDS".
        - disc tracks DRIVE: "track N audio|data SECONDS" a track, in order, from its table of contents (the lead-out ends the last).
        - disc play DRIVE [TRACK]: the music from that track (the first, none said) to the disc's end, read as the drive gives it (2352
          bytes a frame, 75 a second, 44100 Hz of 16-bit stereo) and played through the sound server as "Audio CD" (the mixer shows it).
          One plays at a time: a play started stops the one before (its pid is kept in DISC_PLAY_PATH), as disc stop does.
        - disc eject DRIVE: the play stopped, the disc's file system let go of where device open opened it, and the tray out (or the
          disc out of a slot).

        A disc of music has no file system to open: its tracks are read raw (CDROMREADAUDIO), so a file browser lists them from here.
        device list says each drive too (disc_census): a drive line, the file system of a disc of data, and what disc says of it.
*/
#define DISC_FRAME 2352          // a frame of a CD's sound: 588 stereo samples of 16 bits (the Red Book)
#define DISC_FRAMES_SECOND 75
#define DISC_READ_FRAMES 25      // read a third of a second at a time: what the stream asks for a tenth at a time is kept between reads
#define DISC_PLAY_PATH "/run/moonwater/disc.play"
#define DISC_LEAD_OUT 0xaa
#define DISC_CDROMREADTOCHDR 0x5305
#define DISC_CDROMREADTOCENTRY 0x5306
#define DISC_CDROMEJECT 0x5309
#define DISC_CDROMREADAUDIO 0x530e
#define DISC_CDROM_DRIVE_STATUS 0x5326
#define DISC_CDROM_DISC_STATUS 0x5327
#define DISC_CDS_NO_DISC 1
#define DISC_CDS_TRAY_OPEN 2
#define DISC_CDS_AUDIO 100
#define DISC_CDS_MIXED 105
#define DISC_CDROM_LBA 1
#define DISC_DATA_TRACK 4        // a track's control bits: it holds data, not sound

// linux/cdrom.h's cdrom_tochdr, cdrom_tocentry (its address an LBA) and cdrom_read_audio, as the kernel lays them out on a 64-bit machine.
struct disc_toc_header { unsigned char first, last; };
struct disc_toc_entry { unsigned char track, adr_ctrl, format, pad; int lba; unsigned char datamode, rest[3]; };
struct disc_read_audio { int lba; unsigned char format, pad[3]; int frames; int rest; unsigned char address_to buffer; };
_Static_assert(sizeof(struct disc_toc_entry) == 12 && sizeof(struct disc_read_audio) == 24, "the kernel's cdrom structures");

// Where a disc's tracks are: each one's first frame and whether it is sound, the lead-out after the last.
struct disc_toc
{
        int count, first;
        int start[100];
        b32 data[100];
        int lead_out;
};

// The kernel's own table of contents holds at most 99 tracks (a CD's numbers are 1 to 99), so this one is that bound, said by the medium.
static b32 disc_toc_read(bipolar handle, struct disc_toc address_to toc)
{
        struct disc_toc_header header;
        struct disc_toc_entry entry;

        memory_zero(toc, sizeof(*toc));
        if (system_control(handle, DISC_CDROMREADTOCHDR, address_of header) != 0 || header.first < 1 || header.last < header.first || header.last > 99)
                return false;
        toc->first = header.first;
        for (int track = header.first; track <= header.last + 1; track++)
        {
                memory_zero(&entry, sizeof(entry));
                entry.track = (unsigned char)(track > header.last ? DISC_LEAD_OUT : track), entry.format = DISC_CDROM_LBA;
                if (system_control(handle, DISC_CDROMREADTOCENTRY, address_of entry) != 0)
                        return false;
                if (track > header.last)
                        toc->lead_out = entry.lba;
                else
                        toc->start[toc->count] = entry.lba, toc->data[toc->count] = (entry.adr_ctrl >> 4 & DISC_DATA_TRACK) != 0, toc->count++;
        }
        return toc->count > 0 && toc->lead_out > toc->start[0];
}

static int disc_track_frames(const struct disc_toc address_to toc, int at) { return (at + 1 < toc->count ? toc->start[at + 1] : toc->lead_out) - toc->start[at]; }

// A drive's node, /dev/NAME, for a name that is a drive's (sr and digits).
static bipolar disc_open(string_address name)
{
        p8 path[32];

        if (!name || !host_starts(name, "sr") || string_length(name) > 6)
                return -ENODEV;
        for (positive at = 2; name[at]; at++)
                if (name[at] < '0' || name[at] > '9')
                        return -ENODEV;
        path[0] = end, string_append_bounded(path, "/dev/", sizeof(path)), string_append_bounded(path, name, sizeof(path));
        return system_open_at(AT_FDCWD, path, FILE_READ | O_NONBLOCK | O_CLOEXEC);
}

// What a drive holds, as disc says it (DISC_STATE_*), and of a disc with tracks of music how many and their seconds; whether its first
// track holds data (a file system is read from a disc that starts with one, and from no other).
enum { DISC_STATE_NONE, DISC_STATE_OPEN, DISC_STATE_AUDIO, DISC_STATE_DATA, DISC_STATE_MIXED };
static const string_address disc_state_words[] = {"none", "open", "audio", "data", "mixed"};
struct disc_look { int state, audio, seconds; b32 data_first; };

// Opened with O_NONBLOCK, which leaves the tray as it is: a blocking open of a drive closes an open tray.
static b32 disc_look_at(string_address name, struct disc_look address_to look)
{
        struct disc_toc toc;
        bipolar handle, drive, status;

        memory_zero(look, sizeof(*look));
        if (!host_starts(name, "sr") || (handle = disc_open(name)) < 0)
                return false;
        drive = system_control(handle, DISC_CDROM_DRIVE_STATUS, 0);
        status = system_control(handle, DISC_CDROM_DISC_STATUS, 0);
        if (drive == DISC_CDS_TRAY_OPEN)
                look->state = DISC_STATE_OPEN;
        else if (status == DISC_CDS_NO_DISC || drive == DISC_CDS_NO_DISC || status < 0)
                look->state = DISC_STATE_NONE;
        else if (disc_toc_read(handle, &toc))
        {
                for (int at = 0; at < toc.count; at++)
                        if (!toc.data[at])
                                look->audio++, look->seconds += disc_track_frames(&toc, at) / DISC_FRAMES_SECOND;
                look->data_first = toc.data[0];
                look->state = !look->audio ? DISC_STATE_DATA : look->audio < toc.count || status == DISC_CDS_MIXED ? DISC_STATE_MIXED : DISC_STATE_AUDIO;
        }
        else
                look->state = DISC_STATE_DATA, look->data_first = true;
        system_close(handle);
        return true;
}

static bool disc_list_one(string_address directory, string_address name, address_any context)
{
        struct disc_look look;

        (void)directory, (void)context;
        if (!disc_look_at(name, &look))
                return true;
        if (look.state == DISC_STATE_AUDIO || look.state == DISC_STATE_MIXED)
                host_say(log, host_label "disc %s %s %b %b\n", name, disc_state_words[look.state], (bipolar)look.audio, (bipolar)look.seconds);
        else
                host_say(log, host_label "disc %s %s\n", name, disc_state_words[look.state]);
        return true;
}

/*
        An optical drive in device list (usb_list_disk and usb_list_device_disk, after its drive line): a disc that starts with data has
        its file system said as a disk's with no table is (one segment, the whole drive, its fs tail), which device open opens; then
        "    disc none|open|audio TRACKS SECONDS|data|mixed TRACKS SECONDS", as disc says it. A disc of music is not read for a file system:
        its sectors are sound, and a read of one as data is an error a drive can take seconds over.
*/
static fn disc_census(string_address name, unsigned long long sectors)
{
        struct disc_look look;

        if (!disc_look_at(name, &look))
                return;
        if (sectors && look.data_first && (look.state == DISC_STATE_DATA || look.state == DISC_STATE_MIXED))
                usb_disk_strip(name, sectors);
        if (look.state == DISC_STATE_AUDIO || look.state == DISC_STATE_MIXED)
                host_say(log, "    disc %s %b %b\n", disc_state_words[look.state], (bipolar)look.audio, (bipolar)look.seconds);
        else
                host_say(log, "    disc %s\n", disc_state_words[look.state]);
}

static b32 disc_tracks(string_address name)
{
        struct disc_toc toc;
        bipolar handle = disc_open(name);

        return_if(handle < 0, host_refuse("%s is no optical drive here\n", name));
        if (!disc_toc_read(handle, &toc))
        {
                system_close(handle);
                return host_refuse("%s has no disc with tracks in it\n", name);
        }
        for (int at = 0; at < toc.count; at++)
                host_say(log, host_label "track %b %s %b\n", (bipolar)(toc.first + at), toc.data[at] ? (string_address) "data" : (string_address) "audio",
                         (bipolar)(disc_track_frames(&toc, at) / DISC_FRAMES_SECOND));
        system_close(handle);
        return 0;
}

// The player's place on the disc: the drive, the next frame to read and the last, and what was read and not yet given to the stream.
struct disc_play
{
        bipolar handle;
        int next, stop;
        p8 buffer[DISC_READ_FRAMES * DISC_FRAME];
        positive have, given;
        b32 failed;
};

/*
        What the stream asks for: the drive's frames as they are (the sound server takes 16-bit little-endian stereo, which is what a CD
        holds), read a third of a second at a time. A frame the drive cannot read (a scratch) is silence, so a bad patch skips, as a
        player's does, and does not stop the music.
*/
static long disc_fill(address_any context, short address_to into, long frames)
{
        struct disc_play address_to play = context;
        long written = 0;

        while (written < frames)
        {
                if (play->given == play->have)
                {
                        struct disc_read_audio ask;
                        int count = play->stop - play->next < DISC_READ_FRAMES ? play->stop - play->next : DISC_READ_FRAMES;

                        if (count <= 0)
                                break;
                        ask = (struct disc_read_audio){play->next, DISC_CDROM_LBA, {0}, count, 0, play->buffer};
                        if (system_control(play->handle, DISC_CDROMREADAUDIO, address_of ask) != 0)
                                memory_zero(play->buffer, (positive)count * DISC_FRAME);
                        play->next += count, play->have = (positive)count * DISC_FRAME, play->given = 0;
                }
                {
                        positive bytes = play->have - play->given, want = (positive)(frames - written) * 4;
                        bytes = bytes < want ? bytes : want;
                        memory_copy((p8 address_to)into + written * 4, play->buffer + play->given, bytes);
                        play->given += bytes, written += (long)(bytes / 4);
                }
        }
        return written;
}

static b32 audio_play_stream(const char address_to name, long rate, long (*fill)(address_any context, short address_to into, long frames), address_any context); // audio/service.c

// Whether pid is a player's: its command line is moonwater's (any path to it) and its words are disc play.
static b32 disc_player(positive pid)
{
        p8 path[48], text[96], digits[24];
        bipolar got;
        positive first;

        digits[positive_into_string(digits, pid)] = end;
        if (!host_join(path, sizeof(path), "/proc/", (string_address)digits, "/cmdline") ||
            (got = file_slurp_once_at(AT_FDCWD, (string_address)path, text, sizeof(text) - 1)) <= 0)
                return false;
        text[got] = end;
        first = string_length((string_address)text);
        return first + 11 <= (positive)got && !memory_compare(text + first + 1, "disc\0play\0", 10);
}

// The player before this one, stopped: the pid DISC_PLAY_PATH keeps, if it is still a player (one that ended leaves its pid, which
// another process may have now).
static fn disc_stop_before(void)
{
        p8 text[24];
        bipolar got = host_read_state((string_address)DISC_PLAY_PATH, text, sizeof(text) - 1);
        positive pid = 0;

        if (got <= 0)
                return;
        text[got] = end;
        for (bipolar at = 0; at < got && text[at] >= '0' && text[at] <= '9'; at++)
                pid = pid * 10 + (positive)(text[at] - '0');
        if (pid > 1 && pid != (positive)system_call(syscall(getpid)) && disc_player(pid))
                system_call_2(syscall(kill), pid, 15);
        system_remove_at(AT_FDCWD, (string_address)DISC_PLAY_PATH, 0);
}

static b32 disc_play_track(string_address name, string_address which)
{
        static struct disc_play play;
        struct disc_toc toc;
        p8 pid[24];
        positive track = 0, length;
        int at = -1;
        b32 failed;

        play.handle = disc_open(name);
        return_if(play.handle < 0, host_refuse("%s is no optical drive here\n", name));
        if (!disc_toc_read(play.handle, &toc))
        {
                system_close(play.handle);
                return host_refuse("%s has no disc with tracks in it\n", name);
        }
        for (positive k = 0; which && which[k]; k++)
                track = which[k] >= '0' && which[k] <= '9' ? track * 10 + (positive)(which[k] - '0') : 1000;
        // The track asked for, or the first of music.
        for (int k = 0; k < toc.count && at < 0; k++)
                if (which ? (positive)(toc.first + k) == track : !toc.data[k])
                        at = k;
        if (at < 0 || toc.data[at])
        {
                system_close(play.handle);
                return host_refuse("%s has no track of music %s\n", name, which ? which : (string_address) "on it");
        }
        // From the track to the disc's last track of music: a data track after the music (a mixed disc's) is not played.
        play.next = toc.start[at], play.stop = toc.lead_out, play.have = play.given = 0;
        for (int k = at; k < toc.count; k++)
                if (toc.data[k])
                {
                        play.stop = toc.start[k];
                        break;
                }
        disc_stop_before();
        length = positive_into_string(pid, (positive)system_call(syscall(getpid)));
        pid[length++] = '\n';
        host_write_file((string_address)DISC_PLAY_PATH, pid, length, 0644, false);
        host_say(log, host_label "disc %s plays from track %b\n", name, (bipolar)(toc.first + at));
        failed = audio_play_stream("Audio CD", 44100, disc_fill, &play);
        system_close(play.handle);
        system_remove_at(AT_FDCWD, (string_address)DISC_PLAY_PATH, 0);
        return failed;
}

/*
        The disc out: the play stopped, what device open opened from it let go of (the kernel refuses an eject while a mount holds the
        drive; one in use is said and stays), and the tray opened.
*/
static b32 disc_eject(string_address name)
{
        static usb_media media;
        bipolar handle = disc_open(name), lock, error = 0, got;
        string_address why;

        return_if(handle < 0, host_refuse("%s is no optical drive here\n", name));
        disc_stop_before();
        usb_media_read(USB_MEDIA_KEPT, address_of media);
        if (usb_media_find(address_of media, name) < media.count)
        {
                if ((lock = usb_lock_take(true)) < 0)
                {
                        system_close(handle);
                        return usb_lock_refused(lock);
                }
                why = usb_media_eject(USB_MEDIA_KEPT, name, address_of error);
                radio_unlock(lock);
                if (why)
                {
                        system_close(handle);
                        return error ? host_refuse("disc %s %s (%s)\n", name, why, file_reason(error)) : host_refuse("disc %s %s\n", name, why);
                }
        }
        got = system_control(handle, DISC_CDROMEJECT, 0);
        system_close(handle);
        return_if(got != 0, host_fail("disc eject", got));
        return 0;
}

static b32 host_disc(string_address address_to arguments, positive count)
{
        string_address verb = count > 2 ? arguments[2] : null;

        if (count == 2)
        {
                host_each_entry((string_address) "/sys/block", disc_list_one, null);
                return 0;
        }
        return_if(string_equals(verb, "tracks") && count == 4, disc_tracks(arguments[3]));
        return_if(string_equals(verb, "play") && (count == 4 || count == 5), disc_play_track(arguments[3], count == 5 ? arguments[4] : null));
        if (string_equals(verb, "stop") && count == 3)
                return disc_stop_before(), 0;
        return_if(string_equals(verb, "eject") && count == 4, disc_eject(arguments[3]));
        return host_usage();
}
