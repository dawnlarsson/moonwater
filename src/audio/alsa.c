/*
        Moonwater's sound card, as the kernel's ALSA device files give it: raw ioctls on /dev/snd, no libasound.

        WHAT IT DOES

        Finds the cards and their PCM devices (the control file of each card, /dev/snd/controlC<n>), opens one PCM
        (/dev/snd/pcmC<card>D<device>p or c), and negotiates what it will run in with the driver itself: the kernel's
        hw_params are a set of masks and intervals that a refine call narrows, so the file asks what the device can do
        (formats, channels, rates, period sizes), picks by its own preference (a rate near the one asked, two channels,
        32 bits before 24 before 16, a period of about 5 ms and four of them to a buffer), and sets it, falling back
        one step at a time when the driver says the set is empty. The access is the plain read/write one, so a
        write() is the transfer and nothing is mapped: no coherency to keep on any machine.

        The file keeps the device filled to a target of frames (a few periods) and not to its whole buffer, so the
        latency is the target whatever size of buffer the hardware wants; the poll wakes when a period's worth has
        been played (avail_min), and audio_pcm_avail says how much room there is. A driver that reports an underrun
        (EPIPE) or a resume (ESTRPIPE) is recovered by audio_pcm_recover, which counts it; a card that is gone (ENODEV)
        marks the PCM broken and the caller moves its sink to a clock of its own.

        WHAT IT NEEDS

        Five functions of the file that includes it, which are the kernel and nothing else, so a check can put a card
        of its own behind them: audio_sys_open(path, flags), audio_sys_close(fd), audio_sys_ioctl(fd, request,
        argument), audio_sys_read(fd, buffer, bytes) and audio_sys_write(fd, buffer, bytes), each answering what the
        system call does (a negative errno for a failure). The ioctl numbers and the structures are written here by
        their layout, since no kernel header is in reach of the freestanding build, and CHECK_audio_alsa compares
        every one with the header where the header is.
*/
#ifndef AUDIO_ALSA_INCLUDED
#define AUDIO_ALSA_INCLUDED

#ifndef AUDIO_MIX_INCLUDED
#error "alsa.c follows mix.c, whose formats and names it uses"
#endif

/* ---- the kernel's numbers ---- */

// _IOC as every machine of Moonwater's has it (x86-64, arm64, riscv64): two bits of direction, fourteen of size, then type and number.
#define AUDIO_IOC(direction, type, number, size) (((unsigned long)(direction) << 30) | ((unsigned long)(size) << 16) | ((unsigned long)(type) << 8) | (unsigned long)(number))
#define AUDIO_IOC_NONE 0u
#define AUDIO_IOC_WRITE 1u
#define AUDIO_IOC_READ 2u

struct audio_alsa_mask
{
        unsigned int bits[8];
};

struct audio_alsa_interval
{
        unsigned int min, max, flags; // flags: openmin 1, openmax 2, integer 4, empty 8
};

struct audio_alsa_hw
{
        unsigned int flags;
        struct audio_alsa_mask masks[3];     // access, format, subformat
        struct audio_alsa_mask mres[5];
        struct audio_alsa_interval intervals[12]; // sample bits, frame bits, channels, rate, period time, period size, period bytes, periods, buffer time, buffer size, buffer bytes, tick time
        struct audio_alsa_interval ires[9];
        unsigned int rmask, cmask, info, msbits, rate_num, rate_den;
        unsigned long fifo_size;
        unsigned char reserved[64];
};

struct audio_alsa_sw
{
        int tstamp_mode;
        unsigned int period_step, sleep_min;
        unsigned long avail_min, xfer_align, start_threshold, stop_threshold, silence_threshold, silence_size, boundary;
        unsigned int proto, tstamp_type;
        unsigned char reserved[56];
};

struct audio_alsa_status // the kernel's snd_pcm_mmap_status
{
        int state, pad;
        unsigned long hw_ptr;
        long tstamp[2];
        int suspended_state, pad2;
        long audio_tstamp[2];
};

struct audio_alsa_sync
{
        unsigned int flags, pad;
        union
        {
                struct audio_alsa_status status;
                unsigned char reserved[64];
        } s;
        union
        {
                struct
                {
                        unsigned long appl_ptr, avail_min;
                } control;
                unsigned char reserved[64];
        } c;
};

struct audio_alsa_xfer
{
        long result;
        void *buffer;
        unsigned long frames;
};

struct audio_alsa_pcm_info
{
        unsigned int device, subdevice;
        int stream, card;
        unsigned char id[64], name[80], subname[32];
        int dev_class, dev_subclass;
        unsigned int subdevices_count, subdevices_avail;
        unsigned char sync[16], reserved[64];
};

struct audio_alsa_card_info
{
        int card, pad;
        unsigned char id[16], driver[16], name[32], longname[80], reserved[16], mixername[80], components[128];
};

_Static_assert(sizeof(struct audio_alsa_hw) == 608, "snd_pcm_hw_params");
_Static_assert(sizeof(struct audio_alsa_sw) == 136, "snd_pcm_sw_params");
_Static_assert(sizeof(struct audio_alsa_sync) == 136, "snd_pcm_sync_ptr");
_Static_assert(sizeof(struct audio_alsa_xfer) == 24, "snd_xferi");
_Static_assert(sizeof(struct audio_alsa_pcm_info) == 288, "snd_pcm_info");
_Static_assert(sizeof(struct audio_alsa_card_info) == 376, "snd_ctl_card_info");

#define AUDIO_ALSA_HW_REFINE AUDIO_IOC(AUDIO_IOC_READ | AUDIO_IOC_WRITE, 'A', 0x10, sizeof(struct audio_alsa_hw))
#define AUDIO_ALSA_HW_PARAMS AUDIO_IOC(AUDIO_IOC_READ | AUDIO_IOC_WRITE, 'A', 0x11, sizeof(struct audio_alsa_hw))
#define AUDIO_ALSA_SW_PARAMS AUDIO_IOC(AUDIO_IOC_READ | AUDIO_IOC_WRITE, 'A', 0x13, sizeof(struct audio_alsa_sw))
#define AUDIO_ALSA_DELAY AUDIO_IOC(AUDIO_IOC_READ, 'A', 0x21, sizeof(long))
#define AUDIO_ALSA_SYNC_PTR AUDIO_IOC(AUDIO_IOC_READ | AUDIO_IOC_WRITE, 'A', 0x23, sizeof(struct audio_alsa_sync))
#define AUDIO_ALSA_PREPARE AUDIO_IOC(AUDIO_IOC_NONE, 'A', 0x40, 0)
#define AUDIO_ALSA_START AUDIO_IOC(AUDIO_IOC_NONE, 'A', 0x42, 0)
#define AUDIO_ALSA_DROP AUDIO_IOC(AUDIO_IOC_NONE, 'A', 0x43, 0)
#define AUDIO_ALSA_RESUME AUDIO_IOC(AUDIO_IOC_NONE, 'A', 0x47, 0)
#define AUDIO_ALSA_CARD_INFO AUDIO_IOC(AUDIO_IOC_READ, 'U', 0x01, sizeof(struct audio_alsa_card_info))
#define AUDIO_ALSA_PCM_NEXT_DEVICE AUDIO_IOC(AUDIO_IOC_READ, 'U', 0x30, sizeof(int))
#define AUDIO_ALSA_PCM_INFO AUDIO_IOC(AUDIO_IOC_READ | AUDIO_IOC_WRITE, 'U', 0x31, sizeof(struct audio_alsa_pcm_info))
#define AUDIO_ALSA_PCM_PREFER_SUBDEVICE AUDIO_IOC(AUDIO_IOC_WRITE, 'U', 0x32, sizeof(int))

// The parameters of a hw_params, in the order of the kernel's: three masks, then the intervals from the sample bits.
#define AUDIO_ALSA_ACCESS 0
#define AUDIO_ALSA_FORMAT 1
#define AUDIO_ALSA_SUBFORMAT 2
#define AUDIO_ALSA_SAMPLE_BITS 8
#define AUDIO_ALSA_FRAME_BITS 9
#define AUDIO_ALSA_CHANNELS 10
#define AUDIO_ALSA_RATE 11
#define AUDIO_ALSA_PERIOD_TIME 12
#define AUDIO_ALSA_PERIOD_SIZE 13
#define AUDIO_ALSA_PERIOD_BYTES 14
#define AUDIO_ALSA_PERIODS 15
#define AUDIO_ALSA_BUFFER_TIME 16
#define AUDIO_ALSA_BUFFER_SIZE 17
#define AUDIO_ALSA_BUFFER_BYTES 18
#define AUDIO_ALSA_FIRST_INTERVAL 8

#define AUDIO_ALSA_ACCESS_RW_INTERLEAVED 3
#define AUDIO_ALSA_FORMAT_S16_LE 2
#define AUDIO_ALSA_FORMAT_S24_LE 6
#define AUDIO_ALSA_FORMAT_S32_LE 10
#define AUDIO_ALSA_FORMAT_FLOAT_LE 14
#define AUDIO_ALSA_FORMAT_S24_3LE 32

#define AUDIO_ALSA_STATE_PREPARED 2
#define AUDIO_ALSA_STATE_RUNNING 3
#define AUDIO_ALSA_STATE_XRUN 4
#define AUDIO_ALSA_STATE_SUSPENDED 7
#define AUDIO_ALSA_STATE_DISCONNECTED 8
#define AUDIO_ALSA_SYNC_HWSYNC 1u
#define AUDIO_ALSA_SYNC_APPL 2u       // read the application pointer from the driver (without it the one given is APPLIED)
#define AUDIO_ALSA_SYNC_AVAIL_MIN 4u  // and likewise avail_min
#define AUDIO_ALSA_VERSION 0x20012u

#define AUDIO_ALSA_PLAYBACK 0
#define AUDIO_ALSA_CAPTURE 1
#define AUDIO_ALSA_CARDS 32
#define AUDIO_ALSA_DEVICES 16

#define AUDIO_EPIPE (-32)
#define AUDIO_ENODEV (-19)
#define AUDIO_ESTRPIPE (-86)
#define AUDIO_EAGAIN (-11)
#define AUDIO_EINVAL (-22)

#define AUDIO_O_RDWR 02
#define AUDIO_O_NONBLOCK 04000
#define AUDIO_O_CLOEXEC 02000000

// What the includer is: the kernel's calls.
static long audio_sys_open(const char *path, int flags);
static long audio_sys_close(int fd);
static long audio_sys_ioctl(int fd, unsigned long request, void *argument);
static long audio_sys_read(int fd, void *buffer, unsigned long bytes);
static long audio_sys_write(int fd, const void *buffer, unsigned long bytes);

/* ---- masks and intervals ---- */

static inline int audio_alsa_has(const struct audio_alsa_mask *mask, unsigned int bit)
{
        return bit < 256 && (mask->bits[bit >> 5] >> (bit & 31)) & 1;
}

// Every parameter open: what hw_params_any is.
AUDIO_API void audio_alsa_any(struct audio_alsa_hw *hw)
{
        __builtin_memset(hw, 0, sizeof(*hw));
        for (int m = 0; m < 3; m++)
                for (int w = 0; w < 8; w++)
                        hw->masks[m].bits[w] = ~0u;
        for (int i = 0; i < 12; i++)
        {
                hw->intervals[i].min = 0;
                hw->intervals[i].max = ~0u;
        }
        hw->rmask = ~0u;
}

static inline void audio_alsa_only(struct audio_alsa_hw *hw, int param, unsigned int bit)
{
        __builtin_memset(&hw->masks[param], 0, sizeof(hw->masks[param]));
        hw->masks[param].bits[bit >> 5] = 1u << (bit & 31);
        hw->rmask = ~0u;
}

static inline void audio_alsa_between(struct audio_alsa_hw *hw, int param, unsigned int min, unsigned int max)
{
        struct audio_alsa_interval *interval = &hw->intervals[param - AUDIO_ALSA_FIRST_INTERVAL];

        interval->min = min;
        interval->max = max;
        interval->flags = 4;
        hw->rmask = ~0u;
}

static inline const struct audio_alsa_interval *audio_alsa_interval(const struct audio_alsa_hw *hw, int param)
{
        return &hw->intervals[param - AUDIO_ALSA_FIRST_INTERVAL];
}

// Narrowed by the driver, or an empty set (negative).
AUDIO_API long audio_alsa_refine(int fd, struct audio_alsa_hw *hw)
{
        long result;

        hw->rmask = ~0u;
        result = audio_sys_ioctl(fd, AUDIO_ALSA_HW_REFINE, hw);
        if (result < 0)
                return result;
        for (int i = 0; i < 12; i++)
                if (hw->intervals[i].flags & 8)
                        return AUDIO_EINVAL;
        return 0;
}

/* ---- the formats ---- */

// The device formats this file runs, in order of preference, as the ALSA number, the bytes a sample, and the mixer's own format.
static const struct
{
        int alsa, bytes, format;
} audio_alsa_formats[] = {{AUDIO_ALSA_FORMAT_S32_LE, 4, AUDIO_FORMAT_S32LE},
                          {AUDIO_ALSA_FORMAT_S24_LE, 4, AUDIO_FORMAT_S24_32LE},
                          {AUDIO_ALSA_FORMAT_S24_3LE, 3, AUDIO_FORMAT_S24LE},
                          {AUDIO_ALSA_FORMAT_S16_LE, 2, AUDIO_FORMAT_S16LE},
                          {AUDIO_ALSA_FORMAT_FLOAT_LE, 4, AUDIO_FORMAT_F32LE}};

/* ---- one PCM ---- */

struct audio_pcm
{
        int fd;                 // -1 when closed
        int stream;             // AUDIO_ALSA_PLAYBACK or AUDIO_ALSA_CAPTURE
        int broken;             // the card is gone
        long card, device;
        long format;            // the mixer's AUDIO_FORMAT_* the device is run in
        long channels, rate;
        long frame_bytes;
        long period, buffer, boundary;
        long target;            // playback: the frames the device is kept filled to
        unsigned long xruns, resumes;
};

// "/dev/snd/pcmC<card>D<device>p" into path (at least 32 bytes).
AUDIO_API void audio_alsa_path(char *path, long card, long device, int stream, int control)
{
        const char *prefix = control ? "/dev/snd/controlC" : "/dev/snd/pcmC";
        long at = 0;
        char digits[12];
        int count;

        while (*prefix)
                path[at++] = *prefix++;
        count = 0;
        do
                digits[count++] = (char)('0' + card % 10);
        while ((card /= 10) > 0);
        while (count)
                path[at++] = digits[--count];
        if (!control)
        {
                path[at++] = 'D';
                count = 0;
                do
                        digits[count++] = (char)('0' + device % 10);
                while ((device /= 10) > 0);
                while (count)
                        path[at++] = digits[--count];
                path[at++] = stream == AUDIO_ALSA_CAPTURE ? 'c' : 'p';
        }
        path[at] = 0;
}

static inline long audio_alsa_clamp(long value, long low, long high)
{
        return value < low ? low : value > high ? high : value;
}

/*
        Opens a PCM and sets it up: card, device, playback or capture, the rate asked for (the nearest the device takes
        is the one run), and the frames of a period asked for at 48000 Hz (scaled to the rate). The result is 0 with *pcm
        filled in, or a negative errno with nothing held. subdevice is the one asked for on a device that has several
        (a loopback card), -1 for the driver's choice.
*/
AUDIO_API long audio_pcm_open(struct audio_pcm *pcm, long card, long device, int stream, long rate, long period_at_48000, long subdevice)
{
        static const long fallback_rates[] = {48000, 44100, 96000, 192000, 32000, 22050, 16000, 24000, 88200, 176400, 8000, 11025, 384000, 352800};
        char path[40];
        struct audio_alsa_hw hw;
        struct audio_alsa_sw sw;
        long fd, result, format_at = -1, channels, got_rate = 0, period;
        unsigned int low, high;

        __builtin_memset(pcm, 0, sizeof(*pcm));
        pcm->fd = -1;
        if (card < 0 || card >= AUDIO_ALSA_CARDS || device < 0 || device >= 256 || rate < 1 || period_at_48000 < 1)
                return AUDIO_EINVAL;
        if (subdevice >= 0)
        {
                // The loopback card has eight subdevices and an open takes the first free one; asking for one is a control call.
                char control[40];
                long cfd;
                int wanted = (int)subdevice;

                audio_alsa_path(control, card, 0, 0, 1);
                cfd = audio_sys_open(control, AUDIO_O_RDWR | AUDIO_O_CLOEXEC);
                if (cfd >= 0)
                {
                        audio_sys_ioctl((int)cfd, AUDIO_ALSA_PCM_PREFER_SUBDEVICE, &wanted);
                        audio_sys_close((int)cfd);
                }
        }
        audio_alsa_path(path, card, device, stream, 0);
        fd = audio_sys_open(path, AUDIO_O_RDWR | AUDIO_O_NONBLOCK | AUDIO_O_CLOEXEC);
        if (fd < 0)
                return fd;

        // What the device can do, with the plain read/write access.
        audio_alsa_any(&hw);
        audio_alsa_only(&hw, AUDIO_ALSA_ACCESS, AUDIO_ALSA_ACCESS_RW_INTERLEAVED);
        audio_alsa_only(&hw, AUDIO_ALSA_SUBFORMAT, 0);
        result = audio_alsa_refine((int)fd, &hw);
        if (result < 0)
                goto fail;

        for (long f = 0; f < (long)(sizeof(audio_alsa_formats) / sizeof(audio_alsa_formats[0])); f++)
                if (audio_alsa_has(&hw.masks[AUDIO_ALSA_FORMAT], (unsigned int)audio_alsa_formats[f].alsa))
                {
                        format_at = f;
                        break;
                }
        if (format_at < 0)
        {
                result = AUDIO_EINVAL;
                goto fail;
        }
        audio_alsa_only(&hw, AUDIO_ALSA_FORMAT, (unsigned int)audio_alsa_formats[format_at].alsa);
        result = audio_alsa_refine((int)fd, &hw);
        if (result < 0)
                goto fail;

        // Two channels if the device has them, else the fewest it has (a pair is mixed to the first two).
        low = audio_alsa_interval(&hw, AUDIO_ALSA_CHANNELS)->min;
        high = audio_alsa_interval(&hw, AUDIO_ALSA_CHANNELS)->max;
        channels = low <= 2 && high >= 2 ? 2 : (long)low;
        if (channels < 1 || channels > 32 || (high < low))
        {
                result = AUDIO_EINVAL;
                goto fail;
        }
        audio_alsa_between(&hw, AUDIO_ALSA_CHANNELS, (unsigned int)channels, (unsigned int)channels);
        result = audio_alsa_refine((int)fd, &hw);
        if (result < 0)
                goto fail;

        // The rate asked for, then the usual ones; the first the device takes.
        for (long r = -1; r < (long)(sizeof(fallback_rates) / sizeof(fallback_rates[0])) && !got_rate; r++)
        {
                struct audio_alsa_hw attempt = hw;
                long want = r < 0 ? rate : fallback_rates[r];

                if (r >= 0 && want == rate)
                        continue;
                audio_alsa_between(&attempt, AUDIO_ALSA_RATE, (unsigned int)want, (unsigned int)want);
                if (audio_alsa_refine((int)fd, &attempt) == 0)
                {
                        hw = attempt;
                        got_rate = want;
                }
        }
        if (!got_rate)
        {
                result = AUDIO_EINVAL;
                goto fail;
        }

        // A period of about 5 ms and a buffer of four: the sizes asked for, then the card's own rounding of the period (the least it has at or
        // above the one asked, found by narrowing a range), then the driver's choice of everything left.
        period = audio_alsa_clamp(period_at_48000 * got_rate / 48000, 16, 1 << 16);
        {
                struct audio_alsa_hw wide = hw;
                long nearest = period;
                long wanted[4][2];

                audio_alsa_between(&wide, AUDIO_ALSA_PERIOD_SIZE, (unsigned int)period, (unsigned int)(period * 8));
                if (audio_alsa_refine((int)fd, &wide) == 0)
                        nearest = (long)audio_alsa_interval(&wide, AUDIO_ALSA_PERIOD_SIZE)->min;
                wanted[0][0] = period, wanted[0][1] = 4 * period;
                wanted[1][0] = nearest, wanted[1][1] = 4 * nearest;
                wanted[2][0] = nearest, wanted[2][1] = 0;
                wanted[3][0] = 0, wanted[3][1] = 0;
                result = AUDIO_EINVAL;
                for (int step = 0; step < 4 && result < 0; step++)
                {
                        struct audio_alsa_hw attempt = hw;

                        if (wanted[step][0])
                                audio_alsa_between(&attempt, AUDIO_ALSA_PERIOD_SIZE, (unsigned int)wanted[step][0], (unsigned int)wanted[step][0]);
                        if (wanted[step][1])
                                audio_alsa_between(&attempt, AUDIO_ALSA_BUFFER_SIZE, (unsigned int)wanted[step][1], (unsigned int)wanted[step][1]);
                        else
                                audio_alsa_between(&attempt, AUDIO_ALSA_PERIODS, 2, 16);
                        // The driver left to choose would take the smallest period it has: not under 2.5 ms.
                        if (!wanted[step][0])
                                audio_alsa_between(&attempt, AUDIO_ALSA_PERIOD_TIME, 2500, 100000);
                        attempt.rmask = ~0u;
                        result = audio_sys_ioctl((int)fd, AUDIO_ALSA_HW_PARAMS, &attempt);
                        if (result >= 0)
                                hw = attempt;
                }
                if (result < 0)
                        goto fail;
        }
        pcm->fd = (int)fd;
        pcm->stream = stream;
        pcm->card = card;
        pcm->device = device;
        pcm->format = audio_alsa_formats[format_at].format;
        pcm->channels = channels;
        pcm->rate = (long)audio_alsa_interval(&hw, AUDIO_ALSA_RATE)->min;
        pcm->frame_bytes = audio_alsa_formats[format_at].bytes * channels;
        pcm->period = (long)audio_alsa_interval(&hw, AUDIO_ALSA_PERIOD_SIZE)->min;
        pcm->buffer = (long)audio_alsa_interval(&hw, AUDIO_ALSA_BUFFER_SIZE)->min;
        if (pcm->rate < 1 || pcm->period < 1 || pcm->buffer < 2 * pcm->period || pcm->buffer > (1 << 24))
        {
                pcm->fd = -1;
                result = AUDIO_EINVAL;
                goto fail;
        }

        // Kept filled to three periods (the whole buffer when it is smaller); woken when a period has been played.
        pcm->target = pcm->buffer < 3 * pcm->period ? pcm->buffer : 3 * pcm->period;
        __builtin_memset(&sw, 0, sizeof(sw));
        sw.tstamp_mode = 0;
        sw.period_step = 1;
        sw.proto = AUDIO_ALSA_VERSION;
        sw.tstamp_type = 1;
        if (stream == AUDIO_ALSA_PLAYBACK)
        {
                sw.avail_min = (unsigned long)(pcm->buffer - pcm->target + pcm->period);
                sw.start_threshold = (unsigned long)pcm->target;
        }
        else
        {
                sw.avail_min = (unsigned long)pcm->period;
                sw.start_threshold = 1;
        }
        sw.stop_threshold = (unsigned long)pcm->buffer;
        sw.boundary = 0;
        result = audio_sys_ioctl((int)fd, AUDIO_ALSA_SW_PARAMS, &sw);
        if (result < 0)
        {
                pcm->fd = -1;
                goto fail;
        }
        pcm->boundary = (long)sw.boundary;
        if (pcm->boundary < pcm->buffer)
        {
                pcm->fd = -1;
                result = AUDIO_EINVAL;
                goto fail;
        }
        result = audio_sys_ioctl((int)fd, AUDIO_ALSA_PREPARE, 0);
        if (result < 0)
        {
                pcm->fd = -1;
                goto fail;
        }
        // A capture device runs from the first frame asked of it.
        if (stream == AUDIO_ALSA_CAPTURE)
                audio_sys_ioctl((int)fd, AUDIO_ALSA_START, 0);
        return 0;
fail:
        audio_sys_close((int)fd);
        return result < 0 ? result : AUDIO_EINVAL;
}

AUDIO_API void audio_pcm_close(struct audio_pcm *pcm)
{
        if (pcm->fd >= 0)
        {
                audio_sys_ioctl(pcm->fd, AUDIO_ALSA_DROP, 0);
                audio_sys_close(pcm->fd);
        }
        pcm->fd = -1;
}

/*
        The frames of room in a playback buffer (so many may be written) or of data in a capture one (so many may be
        read). A negative number is the state the driver is in: EPIPE for an underrun or overrun, ESTRPIPE for a
        suspend, ENODEV for a card that is gone (and the PCM is marked broken).
*/
AUDIO_API long audio_pcm_avail(struct audio_pcm *pcm)
{
        struct audio_alsa_sync sync;
        long result, avail;

        __builtin_memset(&sync, 0, sizeof(sync));
        sync.flags = AUDIO_ALSA_SYNC_HWSYNC | AUDIO_ALSA_SYNC_APPL | AUDIO_ALSA_SYNC_AVAIL_MIN;
        result = audio_sys_ioctl(pcm->fd, AUDIO_ALSA_SYNC_PTR, &sync);
        if (result < 0)
        {
                if (result == AUDIO_ENODEV)
                        pcm->broken = 1;
                return result;
        }
        if (sync.s.status.state == AUDIO_ALSA_STATE_XRUN)
                return AUDIO_EPIPE;
        if (sync.s.status.state == AUDIO_ALSA_STATE_SUSPENDED)
                return AUDIO_ESTRPIPE;
        if (sync.s.status.state == AUDIO_ALSA_STATE_DISCONNECTED)
        {
                pcm->broken = 1;
                return AUDIO_ENODEV;
        }
        avail = pcm->stream == AUDIO_ALSA_PLAYBACK ? (long)sync.s.status.hw_ptr + pcm->buffer - (long)sync.c.control.appl_ptr
                                                    : (long)sync.s.status.hw_ptr - (long)sync.c.control.appl_ptr;
        if (avail < 0)
                avail += pcm->boundary;
        else if (avail >= pcm->boundary)
                avail -= pcm->boundary;
        return audio_alsa_clamp(avail, 0, pcm->buffer);
}

// The frames in the device, as the driver counts them (the hardware's own delay with them): the latency of what is written now.
AUDIO_API long audio_pcm_delay(struct audio_pcm *pcm)
{
        long delay = 0, result = audio_sys_ioctl(pcm->fd, AUDIO_ALSA_DELAY, &delay);

        return result < 0 ? result : delay;
}

// frames of the interleaved buffer to the device; the frames it took, or a negative errno (EAGAIN when it is full).
AUDIO_API long audio_pcm_write(struct audio_pcm *pcm, const void *frames, long count)
{
        long result = audio_sys_write(pcm->fd, frames, (unsigned long)(count * pcm->frame_bytes));

        if (result == AUDIO_ENODEV)
                pcm->broken = 1;
        return result < 0 ? result : result / pcm->frame_bytes;
}

AUDIO_API long audio_pcm_read(struct audio_pcm *pcm, void *frames, long count)
{
        long result = audio_sys_read(pcm->fd, frames, (unsigned long)(count * pcm->frame_bytes));

        if (result == AUDIO_ENODEV)
                pcm->broken = 1;
        return result < 0 ? result : result / pcm->frame_bytes;
}

/*
        After EPIPE or ESTRPIPE: puts the device back to a state a write starts it from. An underrun is counted. Answers
        0 when it is ready (and a playback one is empty, to be filled to its target), else the errno, ENODEV marking the
        PCM broken.
*/
AUDIO_API long audio_pcm_recover(struct audio_pcm *pcm, long error)
{
        long result;

        if (error == AUDIO_ENODEV)
        {
                pcm->broken = 1;
                return error;
        }
        if (error == AUDIO_ESTRPIPE)
        {
                pcm->resumes++;
                result = audio_sys_ioctl(pcm->fd, AUDIO_ALSA_RESUME, 0);
                if (result == AUDIO_EAGAIN)
                        return result;
        }
        else if (error == AUDIO_EPIPE)
                pcm->xruns++;
        else
                return error;
        result = audio_sys_ioctl(pcm->fd, AUDIO_ALSA_PREPARE, 0);
        if (result == AUDIO_ENODEV)
                pcm->broken = 1;
        if (result == 0 && pcm->stream == AUDIO_ALSA_CAPTURE)
                audio_sys_ioctl(pcm->fd, AUDIO_ALSA_START, 0);
        return result;
}

/* ---- the cards ---- */

struct audio_card
{
        long number;
        char id[17], name[33], longname[81];
        long devices;
        struct
        {
                long device;
                int playback, capture;
                char name[81];
        } device[AUDIO_ALSA_DEVICES];
};

static void audio_alsa_text(char *to, const unsigned char *from, long room)
{
        long at = 0;

        // A name is what the driver says; it is shown, so only printable bytes of it are kept.
        for (; at < room - 1 && from[at]; at++)
                to[at] = from[at] >= 32 && from[at] < 127 ? (char)from[at] : '?';
        to[at] = 0;
}

/*
        The cards that are there, up to room of them, with their PCM devices: for each card number whose control file
        opens, its names, and for each PCM device whether it plays and whether it records. Answers how many cards.
*/
AUDIO_API long audio_cards_scan(struct audio_card *cards, long room)
{
        long found = 0;

        for (long number = 0; number < AUDIO_ALSA_CARDS && found < room; number++)
        {
                char path[40];
                long fd;
                struct audio_alsa_card_info info;
                struct audio_card *card = cards + found;
                int device = -1;

                audio_alsa_path(path, number, 0, 0, 1);
                fd = audio_sys_open(path, AUDIO_O_RDWR | AUDIO_O_CLOEXEC);
                if (fd < 0)
                        continue;
                __builtin_memset(&info, 0, sizeof(info));
                if (audio_sys_ioctl((int)fd, AUDIO_ALSA_CARD_INFO, &info) < 0)
                {
                        audio_sys_close((int)fd);
                        continue;
                }
                __builtin_memset(card, 0, sizeof(*card));
                card->number = number;
                audio_alsa_text(card->id, info.id, sizeof(card->id));
                audio_alsa_text(card->name, info.name, sizeof(card->name));
                audio_alsa_text(card->longname, info.longname, sizeof(card->longname));
                while (card->devices < AUDIO_ALSA_DEVICES && audio_sys_ioctl((int)fd, AUDIO_ALSA_PCM_NEXT_DEVICE, &device) == 0 && device >= 0 && device < 256)
                {
                        struct audio_alsa_pcm_info pcm;
                        int any = 0;

                        card->device[card->devices].device = device;
                        for (int stream = 0; stream < 2; stream++)
                        {
                                __builtin_memset(&pcm, 0, sizeof(pcm));
                                pcm.device = (unsigned int)device;
                                pcm.subdevice = 0;
                                pcm.stream = stream;
                                if (audio_sys_ioctl((int)fd, AUDIO_ALSA_PCM_INFO, &pcm) == 0)
                                {
                                        if (stream == 0)
                                                card->device[card->devices].playback = 1;
                                        else
                                                card->device[card->devices].capture = 1;
                                        if (!any)
                                                audio_alsa_text(card->device[card->devices].name, pcm.name, sizeof(card->device[card->devices].name));
                                        any = 1;
                                }
                        }
                        if (any)
                                card->devices++;
                }
                audio_sys_close((int)fd);
                found++;
        }
        return found;
}

#endif // AUDIO_ALSA_INCLUDED
