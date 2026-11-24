/*
        moonwater disc [tracks DRIVE|play DRIVE [TRACK]|stop|mount DRIVE FOLDER|eject DRIVE]: the optical drives (/sys/block/sr*) and what
        is in them, through the kernel's CD-ROM interface (linux/cdrom.h), which every drive the sr driver takes answers: an internal SATA
        one or a USB one.

        - disc: a line a drive, "disc sr0 none" (no disc), "disc sr0 open" (its tray is out), "disc sr0 audio TRACKS SECONDS" (a CD of
          music), "disc sr0 data" (a disc with files: opened read-only as any drive's partition is), "disc sr0 mixed TRACKS SECONDS".
        - disc tracks DRIVE: "track N audio|data SECONDS" a track, in order, from its table of contents (the lead-out ends the last).
        - disc play DRIVE [TRACK]: the music from that track (the first, none said) to the disc's end, read as the drive gives it (2352
          bytes a frame, 75 a second, 44100 Hz of 16-bit stereo) and played through the sound server as "Audio CD" (the mixer shows it).
          One plays at a time: a play started stops the one before (its pid is kept in DISC_PLAY_PATH), as disc stop does.
        - disc mount DRIVE FOLDER: a CD of music as a folder of files at FOLDER, read-only: each track of music a WAV file ("Track
          01.wav"), its header and the drive's frames as they are, so a track is copied, dragged or opened in a browser as any file is.
          A server of its own answers the kernel's FUSE requests for it (disc_fs_answer) until the folder is unmounted.
        - disc eject DRIVE: the play stopped, the disc's file system let go of where device open opened it (a CD's folder of tracks too),
          and the tray out (or the disc out of a slot).

        A disc of music has no file system of its own: its tracks are read raw (CDROMREADAUDIO). device list says each drive too
        (disc_census): a drive line, the file system of a disc of data or a CD's folder of tracks ("fs cdda", which device open mounts
        as disc mount does), and what disc says of it.
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
#define DISC_EXTRA_GAP 11400     // a CD-Extra's music ends this many frames before its data session (its lead-out, lead-in and gap: the Blue Book)

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
        if (toc->count <= 0 || toc->lead_out <= toc->start[0])
                return false;
        for (int at = 0; at < toc->count; at++)
                if (toc->start[at] < 0 || (at > 0 && toc->start[at] < toc->start[at - 1]))
                        return false;
        return toc->lead_out >= toc->start[toc->count - 1];
}

static int disc_track_frames(const struct disc_toc address_to toc, int at) { return (at + 1 < toc->count ? toc->start[at + 1] : toc->lead_out) - toc->start[at]; }

// A track of music's frames: the last before a data track of a disc that starts with music (a CD-Extra) ends at its session's end.
static int disc_music_frames(const struct disc_toc address_to toc, int at)
{
        int frames = disc_track_frames(toc, at);
        return at + 1 < toc->count && toc->data[at + 1] && !toc->data[0] && frames > DISC_EXTRA_GAP ? frames - DISC_EXTRA_GAP : frames;
}

#define DISC_WAV_HEAD 44 // a track's WAV header: RIFF, its fmt chunk (PCM, 2 channels, 44100 Hz, 16 bits) and its data chunk's

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
struct disc_look { int state, audio, seconds; b32 data_first; p64 bytes; }; // bytes: a CD's folder of tracks (disc mount), each a WAV

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
                                look->audio++, look->seconds += disc_music_frames(&toc, at) / DISC_FRAMES_SECOND,
                                look->bytes += DISC_WAV_HEAD + (p64)disc_music_frames(&toc, at) * DISC_FRAME;
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
        its file system said as a disk's with no table is (one segment, the whole drive, its fs tail), which device open opens; a disc
        that starts with music has its folder of tracks so ("fs cdda - DRIVE", its bytes the WAV files'), which device open mounts as
        disc mount does; then "    disc none|open|audio TRACKS SECONDS|data|mixed TRACKS SECONDS", as disc says it. A disc of music is not
        read for a file system: its sectors are sound, and a read of one as data is an error a drive can take seconds over.
*/
static fn disc_census(string_address name, unsigned long long sectors)
{
        struct disc_look look;

        if (!disc_look_at(name, &look))
                return;
        if (sectors && look.data_first && (look.state == DISC_STATE_DATA || look.state == DISC_STATE_MIXED))
                usb_disk_strip(name, sectors);
        else if (look.audio && !look.data_first)
                host_say(log, "    part 0 %p unpartitioned - fs cdda - %s\n", (positive)look.bytes, name);
        if (look.state == DISC_STATE_AUDIO || look.state == DISC_STATE_MIXED)
                host_say(log, "    disc %s %b %b\n", disc_state_words[look.state], (bipolar)look.audio, (bipolar)look.seconds);
        else
                host_say(log, "    disc %s\n", disc_state_words[look.state]);
}

// Whether a drive holds a disc that starts with music, and its folder of tracks' bytes (device open's look at it: usb_open_look).
static b32 disc_music(string_address name, p64 address_to bytes)
{
        struct disc_look look;
        b32 music = disc_look_at(name, &look) && look.audio && !look.data_first;
        address_to bytes = music ? look.bytes : 0;
        return music;
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
                         (bipolar)((toc.data[at] ? disc_track_frames(&toc, at) : disc_music_frames(&toc, at)) / DISC_FRAMES_SECOND));
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
                        play.stop = toc.start[k - 1] + disc_music_frames(&toc, k - 1);
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

/* ---- disc mount: a CD of music as a folder of WAV files, served to the kernel's FUSE (linux/fuse.h, protocol 7) ---- */

#define DISC_MOUNT_STATE "/run/moonwater/disc.mount." // and the drive's name: the server's pid and the folder it serves
#define DISC_FUSE_REQUEST 8192   // a read of /dev/fuse takes one request whole: FUSE_MIN_READ_BUFFER, a write's max_write and its headers
#define DISC_FUSE_WRITE 4096     // the max_write said at INIT, the least the kernel takes: nothing is written here
#define DISC_FUSE_ROOT 1
#define DISC_FUSE_VALID 3600     // seconds the kernel keeps a name and its attributes: a disc does not change while it is in
#define DISC_FUSE_KEEP 2         // FOPEN_KEEP_CACHE: what the kernel has read of a track stays read
#define DISC_READ_MOST 75        // the most CDROMREADAUDIO takes at once (CD_FRAMES)
#define DISC_UNMOUNT_WAIT_NS 2000000000ull // a server unmounted ends in this, and lets go of the drive (its eject waits)
enum { DISC_FUSE_LOOKUP = 1, DISC_FUSE_FORGET = 2, DISC_FUSE_GETATTR = 3, DISC_FUSE_OPEN = 14, DISC_FUSE_READ = 15, DISC_FUSE_STATFS = 17,
       DISC_FUSE_RELEASE = 18, DISC_FUSE_INIT = 26, DISC_FUSE_OPENDIR = 27, DISC_FUSE_READDIR = 28, DISC_FUSE_RELEASEDIR = 29,
       DISC_FUSE_INTERRUPT = 36, DISC_FUSE_DESTROY = 38, DISC_FUSE_NOTIFY_REPLY = 41, DISC_FUSE_BATCH_FORGET = 42 };

// linux/fuse.h's structures as the kernel lays them out (little or big endian alike: the kernel's own order).
struct disc_fuse_in { p32 length, opcode; p64 unique, node; p32 uid, gid, pid; p16 extensions, pad; };
struct disc_fuse_out { p32 length; b32 error; p64 unique; };
struct disc_fuse_attr { p64 inode, size, blocks, atime, mtime, ctime; p32 atime_ns, mtime_ns, ctime_ns, mode, links, uid, gid, rdev, block_size, flags; };
struct disc_fuse_entry { p64 node, generation, entry_valid, attr_valid; p32 entry_valid_ns, attr_valid_ns; struct disc_fuse_attr attr; };
struct disc_fuse_attr_out { p64 attr_valid; p32 attr_valid_ns, dummy; struct disc_fuse_attr attr; };
struct disc_fuse_read { p64 handle, offset; p32 size, read_flags; p64 lock_owner; p32 flags, pad; };
struct disc_fuse_open_out { p64 handle; p32 open_flags; b32 backing; };
struct disc_fuse_init_in { p32 major, minor, max_readahead, flags; };
struct disc_fuse_init_out { p32 major, minor, max_readahead, flags; p16 max_background, congestion; p32 max_write, time_gran; p16 max_pages, map_alignment;
                            p32 flags2, max_stack_depth; p16 request_timeout, unused[11]; };
struct disc_fuse_statfs { p64 blocks, free, available, files, free_files; p32 block_size, name_most, fragment, pad, spare[6]; };
struct disc_fuse_dirent { p64 inode, next; p32 name_length, type; };
_Static_assert(sizeof(struct disc_fuse_in) == 40 && sizeof(struct disc_fuse_out) == 16 && sizeof(struct disc_fuse_attr) == 88 &&
                   sizeof(struct disc_fuse_entry) == 128 && sizeof(struct disc_fuse_attr_out) == 104 && sizeof(struct disc_fuse_read) == 40 &&
                   sizeof(struct disc_fuse_open_out) == 16 && sizeof(struct disc_fuse_init_out) == 64 && sizeof(struct disc_fuse_statfs) == 80 &&
                   sizeof(struct disc_fuse_dirent) == 24,
               "linux/fuse.h's structures");

/*
        The folder of a CD's tracks: each track of music a file (node 2 on, the root 1), its frames from read (the drive's CDROMREADAUDIO,
        or a check's frames), kept a second ahead of what is asked (the cache, as large as a read asks and a second more) for a copy's
        and a browser's reads, which go on in order. A frame read cannot give is silence.
*/
struct disc_fs
{
        int count;
        int number[100], start[100], frames[100];  // a track's number on the disc, its first frame and its frames (at most 99 tracks)
        p32 uid, gid;
        p64 seconds;                               // the files' times: when the folder was made
        b32 (*read)(address_any context, int lba, int frames, p8 address_to into);
        address_any context;
        p8 address_to cache, address_to reply;
        positive cache_room, reply_room;
        int cache_lba, cache_frames;
        b32 ended;                                 // DESTROY came: the folder is let go of
};

// The tracks of music of a disc's table of contents into fs (its data tracks are no file); whether it has any.
static b32 disc_fs_take(struct disc_fs address_to fs, const struct disc_toc address_to toc)
{
        fs->count = 0, fs->cache_frames = 0;
        for (int at = 0; at < toc->count; at++)
                if (!toc->data[at])
                {
                        int frames = disc_music_frames(toc, at);
                        if (frames <= 0)
                                continue;
                        fs->number[fs->count] = toc->first + at, fs->start[fs->count] = toc->start[at], fs->frames[fs->count++] = frames;
                }
        return fs->count > 0;
}

static inline p64 disc_fs_size(const struct disc_fs address_to fs, int track) { return DISC_WAV_HEAD + (p64)fs->frames[track] * DISC_FRAME; }

// A track's name, "Track 01.wav" (two digits: a disc has 99 tracks at most), into into (16 bytes); its length.
static positive disc_fs_name(const struct disc_fs address_to fs, int track, p8 address_to into)
{
        memory_copy(into, "Track 00.wav", 13);
        into[6] = (p8)('0' + fs->number[track] / 10 % 10), into[7] = (p8)('0' + fs->number[track] % 10);
        return 12;
}

static fn disc_le32(p8 address_to at, p32 value)
{
        for (int k = 0; k < 4; k++)
                at[k] = (p8)(value >> (8 * k));
}

// A track's WAV header: RIFF of the data's bytes and 36, PCM of 2 channels at 44100 Hz (176400 bytes a second, 4 a sample), 16 bits.
static fn disc_wav_head(p8 address_to into, int frames)
{
        static const p8 shape[DISC_WAV_HEAD] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 2, 0,
                                                0x44, 0xac, 0, 0, 0x10, 0xb1, 2, 0, 4, 0, 16, 0, 'd', 'a', 't', 'a', 0, 0, 0, 0};
        memory_copy(into, shape, DISC_WAV_HEAD);
        disc_le32(into + 4, (p32)frames * DISC_FRAME + 36), disc_le32(into + 40, (p32)frames * DISC_FRAME);
}

static fn disc_fs_attr(const struct disc_fs address_to fs, p64 node, struct disc_fuse_attr address_to attr)
{
        memory_zero(attr, sizeof(*attr));
        attr->inode = node, attr->atime = attr->mtime = attr->ctime = fs->seconds, attr->uid = fs->uid, attr->gid = fs->gid, attr->block_size = 4096;
        if (node == DISC_FUSE_ROOT)
                attr->mode = 0040555, attr->links = 2;
        else
                attr->mode = 0100444, attr->links = 1, attr->size = disc_fs_size(fs, (int)(node - 2)), attr->blocks = (attr->size + 511) / 512;
}

// Frames [lba, lba + frames) into into: a run the drive will not give is asked again a frame at a time, and a frame it will not give is silence.
static fn disc_fs_frames(struct disc_fs address_to fs, int lba, int frames, p8 address_to into)
{
        for (int at = 0; at < frames; at += DISC_READ_MOST)
        {
                int count = frames - at < DISC_READ_MOST ? frames - at : DISC_READ_MOST;
                if (!fs->read(fs->context, lba + at, count, into + (positive)at * DISC_FRAME))
                        for (int one = at; one < at + count; one++)
                                if (!fs->read(fs->context, lba + one, 1, into + (positive)one * DISC_FRAME))
                                        memory_zero(into + (positive)one * DISC_FRAME, DISC_FRAME);
        }
}

// Bytes [offset, offset + size) of a track's file into into: of its header, and of its frames through the cache. The bytes given, or
// -ENOMEM where the cache could not be as large as the read.
static bipolar disc_fs_read(struct disc_fs address_to fs, int track, p64 offset, p32 size, p8 address_to into)
{
        p64 whole = disc_fs_size(fs, track), stop = offset + size < whole ? offset + size : whole;
        positive given = 0;
        if (offset >= whole)
                return 0;
        if (offset < DISC_WAV_HEAD)
        {
                p8 head[DISC_WAV_HEAD];
                positive part = (positive)((stop < DISC_WAV_HEAD ? stop : DISC_WAV_HEAD) - offset);
                disc_wav_head(head, fs->frames[track]);
                memory_copy(into, head + offset, part);
                given = part, offset += part;
        }
        if (offset < stop)
        {
                p64 from = offset - DISC_WAV_HEAD, to = stop - DISC_WAV_HEAD;
                int first = (int)(from / DISC_FRAME), last = (int)((to + DISC_FRAME - 1) / DISC_FRAME), lba = fs->start[track] + first;
                if (lba < fs->cache_lba || lba + (last - first) > fs->cache_lba + fs->cache_frames)
                {
                        // What is asked and a second after it, to the track's end.
                        int frames = last - first + DISC_FRAMES_SECOND;
                        frames = frames < fs->frames[track] - first ? frames : fs->frames[track] - first;
                        if (!memory_reserve((address_any address_to)&fs->cache, &fs->cache_room, 0, (positive)frames * DISC_FRAME, 1, (positive)frames * DISC_FRAME))
                                return -ENOMEM;
                        disc_fs_frames(fs, lba, frames, fs->cache);
                        fs->cache_lba = lba, fs->cache_frames = frames;
                }
                memory_copy(into + given, fs->cache + (positive)(lba - fs->cache_lba) * DISC_FRAME + from % DISC_FRAME, (positive)(to - from));
                given += (positive)(to - from);
        }
        return (bipolar)given;
}

// A folder's entries from the one numbered at (".", "..", then each track), as many as fit in room: their bytes.
static positive disc_fs_list(const struct disc_fs address_to fs, p64 at, p8 address_to into, positive room)
{
        positive used = 0;
        for (; at < (p64)fs->count + 2; at++)
        {
                struct disc_fuse_dirent entry = {at < 2 ? DISC_FUSE_ROOT : at, at + 1, 0, at < 2 ? 4u : 8u}; // DT_DIR, DT_REG
                p8 name[16];
                positive whole;
                entry.name_length = at < 2 ? (p32)(at + 1) : (p32)disc_fs_name(fs, (int)(at - 2), name);
                if (at < 2)
                        name[0] = name[1] = '.';
                whole = (sizeof(entry) + entry.name_length + 7) & ~(positive)7;
                if (used + whole > room)
                        break;
                memory_zero(into + used, whole), memory_copy(into + used, &entry, sizeof(entry)), memory_copy(into + used + sizeof(entry), name, entry.name_length);
                used += whole;
        }
        return used;
}

/*
        One request of the kernel's answered into fs->reply: the reply's bytes, 0 for a request that has none (FORGET, BATCH_FORGET,
        INTERRUPT, a reply to a notice). The folder is read-only, its root node 1 and each track node 2 on; every request it has no
        answer for is ENOSYS, which the kernel takes as "not here" and does without.
*/
static positive disc_fs_answer(struct disc_fs address_to fs, const p8 address_to request, positive length)
{
        struct disc_fuse_in in;
        struct disc_fuse_out out = {0, 0, 0};
        struct disc_fuse_read ask = {0, 0, 0, 0, 0, 0, 0};
        const p8 address_to body = request + sizeof(in);
        positive body_length, given = 0, want;
        p64 node;
        p8 address_to into;

        if (length < sizeof(in))
                return 0;
        memory_copy(&in, request, sizeof(in));
        body_length = length - sizeof(in), node = in.node;
        if (in.opcode == DISC_FUSE_FORGET || in.opcode == DISC_FUSE_BATCH_FORGET || in.opcode == DISC_FUSE_INTERRUPT || in.opcode == DISC_FUSE_NOTIFY_REPLY)
                return 0;
        if ((in.opcode == DISC_FUSE_READ || in.opcode == DISC_FUSE_READDIR) && body_length >= sizeof(ask))
                memory_copy(&ask, body, sizeof(ask));
        // The reply's room: its header and what the request may be given (a read's size, or the largest of the structures).
        want = sizeof(out) + (ask.size > sizeof(struct disc_fuse_entry) ? ask.size : sizeof(struct disc_fuse_entry));
        if (!memory_reserve((address_any address_to)&fs->reply, &fs->reply_room, 0, want, 1, want))
                return 0;
        into = fs->reply + sizeof(out);
        out.unique = in.unique;
        switch (in.opcode)
        {
        case DISC_FUSE_INIT:
        {
                struct disc_fuse_init_in asked = {0, 0, 0, 0};
                struct disc_fuse_init_out said;
                memory_copy(&asked, body, body_length < sizeof(asked) ? body_length : sizeof(asked));
                memory_zero(&said, sizeof(said));
                said.major = 7, said.minor = asked.minor < 31 ? asked.minor : 31, said.max_readahead = asked.max_readahead;
                said.max_write = DISC_FUSE_WRITE, said.time_gran = 1;
                if (asked.major != 7)
                        out.error = -EPROTO;
                else
                        given = asked.minor < 23 ? 24 : sizeof(said), memory_copy(into, &said, given); // a kernel before 7.23 takes its 24 bytes
                break;
        }
        case DISC_FUSE_LOOKUP:
                out.error = -ENOENT;
                for (int at = 0; node == DISC_FUSE_ROOT && at < fs->count; at++)
                {
                        p8 name[16];
                        positive name_length = disc_fs_name(fs, at, name);
                        if (body_length > name_length && !memory_compare(body, name, name_length) && !body[name_length])
                        {
                                struct disc_fuse_entry entry;
                                memory_zero(&entry, sizeof(entry));
                                entry.node = (p64)at + 2, entry.entry_valid = entry.attr_valid = DISC_FUSE_VALID;
                                disc_fs_attr(fs, entry.node, &entry.attr);
                                memory_copy(into, &entry, sizeof(entry)), given = sizeof(entry), out.error = 0;
                        }
                }
                break;
        case DISC_FUSE_GETATTR:
                if (node == DISC_FUSE_ROOT || (node >= 2 && node < (p64)fs->count + 2))
                {
                        struct disc_fuse_attr_out attr;
                        memory_zero(&attr, sizeof(attr));
                        attr.attr_valid = DISC_FUSE_VALID, disc_fs_attr(fs, node, &attr.attr);
                        memory_copy(into, &attr, sizeof(attr)), given = sizeof(attr);
                }
                else
                        out.error = -ENOENT;
                break;
        case DISC_FUSE_OPEN:
        case DISC_FUSE_OPENDIR:
        {
                struct disc_fuse_open_out opened = {0, in.opcode == DISC_FUSE_OPEN ? DISC_FUSE_KEEP : 0, 0};
                p32 flags = 0;
                b32 folder = node == DISC_FUSE_ROOT, track = node >= 2 && node < (p64)fs->count + 2;
                memory_copy(&flags, body, body_length < sizeof(flags) ? body_length : sizeof(flags)); // fuse_open_in's flags: the open's own
                out.error = !folder && !track                                  ? -ENOENT
                            : (in.opcode == DISC_FUSE_OPENDIR) != folder         ? (folder ? -EISDIR : -ENOTDIR)
                            : (flags & 3) != 0                                   ? -EROFS
                                                                                 : 0;
                if (!out.error)
                        memory_copy(into, &opened, sizeof(opened)), given = sizeof(opened);
                break;
        }
        case DISC_FUSE_READ:
        {
                bipolar got = node >= 2 && node < (p64)fs->count + 2 ? disc_fs_read(fs, (int)(node - 2), ask.offset, ask.size, into) : -EISDIR;
                if (got < 0)
                        out.error = (b32)got;
                else
                        given = (positive)got;
                break;
        }
        case DISC_FUSE_READDIR:
                if (node == DISC_FUSE_ROOT)
                        given = disc_fs_list(fs, ask.offset, into, ask.size);
                else
                        out.error = -ENOTDIR;
                break;
        case DISC_FUSE_STATFS:
        {
                struct disc_fuse_statfs room;
                memory_zero(&room, sizeof(room));
                for (int at = 0; at < fs->count; at++)
                        room.blocks += (disc_fs_size(fs, at) + 2047) / 2048;
                room.files = (p64)fs->count + 1, room.block_size = room.fragment = 2048, room.name_most = 255;
                memory_copy(into, &room, sizeof(room)), given = sizeof(room);
                break;
        }
        case DISC_FUSE_RELEASE:
        case DISC_FUSE_RELEASEDIR:
                break;
        case DISC_FUSE_DESTROY:
                fs->ended = true;
                break;
        default:
                out.error = -ENOSYS;
        }
        if (out.error)
                given = 0;
        out.length = (p32)(sizeof(out) + given);
        memory_copy(fs->reply, &out, sizeof(out));
        return out.length;
}

// The drive's frames, for a mounted disc's files (the context is its handle).
static b32 disc_drive_read(address_any context, int lba, int frames, p8 address_to into)
{
        struct disc_read_audio ask = {lba, DISC_CDROM_LBA, {0}, frames, 0, into};
        return system_control((bipolar)(positive)context, DISC_CDROMREADAUDIO, address_of ask) == 0;
}

// The server: requests read from /dev/fuse and answered until the folder is unmounted (ENODEV) or let go of (DESTROY). Its pid and
// the folder are DISC_MOUNT_STATE's while it serves.
static fn disc_fs_serve(struct disc_fs address_to fs, bipolar fuse, string_address name, string_address folder)
{
        static p8 request[DISC_FUSE_REQUEST];
        p8 state[64], said[HOST_PATH_ROOM + 32];
        positive length = positive_into_string(said, (positive)system_call(syscall(getpid)));

        said[length++] = ' ';
        string_copy_bounded(said + length, folder, sizeof(said) - length - 1);
        length = string_length((string_address)said), said[length++] = '\n';
        if (host_join(state, sizeof(state), DISC_MOUNT_STATE, name))
                host_write_file((string_address)state, said, length, 0644, false);
        while (!fs->ended)
        {
                bipolar got = system_read_once(fuse, request, sizeof(request)), answer;
                if (got == -EINTR || got == -EAGAIN || got == -ENOENT)
                        continue;
                if (got <= 0)
                        break;
                answer = (bipolar)disc_fs_answer(fs, request, (positive)got);
                if (answer > 0)
                        (void)system_write_once(fuse, fs->reply, (positive)answer); // ENOENT: the request was taken back (interrupted)
        }
        system_remove_at(AT_FDCWD, (string_address)state, 0);
}

/*
        A folder of tracks served at folder, read-only: the kernel's FUSE (/dev/fuse, which every image but the locked one has) mounted
        there with source as its source (a drive's node, so the mount table says which drive it is, as a partition's says), allow_other
        (a browser in its bowl reads it) and default_permissions (the kernel judges who may by the modes, 0444 and 0555), and a server of
        its own detached to answer it, which keeps the drive's handle (none: -1) and DISC_MOUNT_STATE name. Null when it serves; else why
        not, error the mount's.
*/
static string_address disc_fs_mount(struct disc_fs address_to fs, bipolar handle, string_address name, string_address folder, bipolar address_to error)
{
        p8 options[192], source[32], digits[3][24];
        bipolar fuse, child, made = 0;
        positive status = 0;

        address_to error = 0;
        if (system_access_at(AT_FDCWD, "/dev/fuse", 0) < 0)
                return "is not served as a folder: this system has no /dev/fuse (its kernel was built with no FUSE, as the locked image is)";
        if ((fuse = system_open_at(AT_FDCWD, "/dev/fuse", FILE_READ_WRITE | O_CLOEXEC)) < 0)
                return address_to error = fuse, (string_address) "could not open /dev/fuse";
        digits[0][positive_into_string(digits[0], (positive)fuse)] = end;
        digits[1][positive_into_string(digits[1], fs->uid)] = end, digits[2][positive_into_string(digits[2], fs->gid)] = end;
        if (!string_join(options, sizeof(options), "fd=", (string_address)digits[0], ",rootmode=40000,user_id=", (string_address)digits[1],
                         ",group_id=", (string_address)digits[2], ",allow_other,default_permissions") ||
            !host_join(source, sizeof(source), "/dev/", name) ||
            (made = system_mount((string_address)source, folder, "fuse.moonwater-cd", MS_RDONLY | MS_NOSUID | MS_NODEV, (string_address)options)) < 0)
        {
                system_close(fuse);
                return address_to error = made < 0 ? made : -ERROR_INVALID, (string_address) "could not be mounted";
        }
        // The server: forked twice (init reaps it), a session of its own, /dev/null for its three descriptors, and /dev/fuse and the
        // drive alone besides. The first child says whether the second came.
        if ((child = system_fork()) == 0)
        {
                bipolar server = system_fork(), quiet;
                if (server != 0)
                        system_call_1(syscall(exit_group), server < 0 ? 1 : 0);
                system_call(syscall(setsid));
                quiet = system_open_at(AT_FDCWD, "/dev/null", FILE_READ_WRITE | O_CLOEXEC);
                for (positive at = 0; quiet >= 0 && at < 3; at++)
                        if ((positive)quiet != at)
                                system_call_3(syscall(dup3), (positive)quiet, at, 0);
                (void)descriptors_close_except(3, (p32)fuse, handle >= 0 ? (p32)handle : descriptors_none);
                disc_fs_serve(fs, fuse, name, folder);
                system_call_1(syscall(exit_group), 0);
        }
        if (child > 0)
                system_call_4(syscall(wait4), (positive)child, (positive)address_of status, 0, 0);
        system_close(fuse);
        if (child < 0 || status)
        {
                system_call_2(syscall(umount2), (positive)folder, MNT_DETACH);
                return address_to error = child < 0 ? child : -ERROR_INVALID, (string_address) "could not start its server";
        }
        return null;
}

// A CD of music served at folder as its folder of tracks, owned by uid and gid (disc_fs_mount), its frames the drive's.
static string_address disc_mount_at(string_address name, string_address folder, positive uid, positive gid, bipolar address_to error)
{
        static struct disc_fs fs;
        struct disc_toc toc;
        bipolar handle;
        string_address why;

        address_to error = 0;
        if ((handle = disc_open(name)) < 0)
                return "is no optical drive here";
        memory_zero(&fs, sizeof(fs));
        if (!disc_toc_read(handle, &toc) || !disc_fs_take(&fs, &toc))
        {
                system_close(handle);
                return "has no track of music on it";
        }
        fs.uid = (p32)uid, fs.gid = (p32)gid, fs.seconds = system_clock_ns(HOST_CLOCK_REALTIME) / 1000000000ull;
        fs.read = disc_drive_read, fs.context = (address_any)(positive)handle;
        why = disc_fs_mount(&fs, handle, name, folder, error);
        system_close(handle);
        return why;
}

// A CD's folder that disc mount served by hand let go of, and its server waited for, which holds the drive (an eject waits for it).
static string_address disc_unmount(string_address name)
{
        p8 state[64], text[HOST_PATH_ROOM + 32];
        bipolar got;
        positive used = 0, pid;
        string_address folder;
        p64 until;

        if (!host_join(state, sizeof(state), DISC_MOUNT_STATE, name) || (got = host_read_state((string_address)state, text, sizeof(text) - 1)) <= 0)
                return null;
        text[got] = end;
        if (got && text[got - 1] == '\n')
                text[got - 1] = end;
        pid = string_digits_max((string_address)text, 10, address_of used);
        folder = used && text[used] == ' ' ? (string_address)text + used + 1 : null;
        if (folder && folder[0] == '/')
        {
                bipolar gone = (bipolar)system_call_2(syscall(umount2), (positive)folder, 0);
                if (gone < 0 && gone != -EINVAL && gone != -ENOENT)
                        return "is in use: close what has its tracks open, and eject it again";
        }
        for (until = system_clock_ns(HOST_CLOCK_BOOTTIME) + DISC_UNMOUNT_WAIT_NS; pid > 1 && (bipolar)system_call_2(syscall(kill), pid, 0) == 0 &&
                                                                                 system_clock_ns(HOST_CLOCK_BOOTTIME) < until;)
                host_pause(20000000);
        system_remove_at(AT_FDCWD, (string_address)state, 0);
        return null;
}

static b32 disc_mount_verb(string_address name, string_address folder)
{
        bipolar error;
        string_address why;

        host_need_root();
        why = disc_mount_at(name, folder, (positive)system_call(syscall(getuid)), (positive)system_call(syscall(getgid)), address_of error);
        if (why)
                return error ? host_refuse("disc %s %s: %s\n", name, why, file_reason(error)) : host_refuse("disc %s %s\n", name, why);
        host_say(log, host_label "disc %s is a folder of tracks at %s\n", name, folder);
        return 0;
}

/*
        The disc out: the play stopped, what device open opened from it let go of (the kernel refuses an eject while a mount holds the
        drive; one in use is said and stays), a folder of its tracks served by hand let go of too, and the tray opened.
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
        // A CD's folder of tracks: its server holds the drive, which the kernel will not eject while another has it open.
        if ((why = disc_unmount(name)))
        {
                system_close(handle);
                return host_refuse("disc %s %s\n", name, why);
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
        return_if(string_equals(verb, "mount") && count == 5, disc_mount_verb(arguments[3], arguments[4]));
        return_if(string_equals(verb, "eject") && count == 4, disc_eject(arguments[3]));
        return host_usage();
}
