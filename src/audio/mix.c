/*
        Moonwater's audio mixing core: what turns the streams of programs into the one stream of a sound card.

        WHAT IT IS

        A voice is one program's playback stream: a ring of the bytes the program wrote, in the format it chose (any
        PCM format PulseAudio names, 1 to 8 channels, 1 to 384000 Hz). A sink is a card's worth of mixing: every period the
        service asks each voice for the next block of the sink's rate and adds it to the sink's bus, and the sink turns the
        bus into the card's format. Nothing here calls anything: no library, no allocator, no clock, no descriptor. A
        voice's ring is memory the caller gives it, and a call does a bounded amount of work for a bounded block, so the
        loop that drives it never waits on this file.

        THE BUS

        Two channels of float32, planar, one block (AUDIO_BLOCK frames) at a time. A stream's channels are remixed to the
        two by the positions of its channel map (a mono stream to both, 5.1 down by power-normalised coefficients), a
        device of more channels gets the pair on its first two, a mono device the mean. Float holds 24 bits, so a 16-bit
        stream at unity volume, the sink's rate and format comes out the bytes it went in, and the sum of two such streams
        is exact until the clip.

        VOLUME

        PulseAudio's curve: the gain of a volume v (65536 is 100%) is (v / 65536) cubed, so "50%" asked of any client
        means what it means everywhere. A gain is never a step: each channel's gain slews toward its target by at most
        1/AUDIO_RAMP (more for a gain above one) a frame, so a mute, an unmute, a cork or a new stream is a ramp of 96
        frames (2 ms at 48 kHz), and a stream that runs out of data fades to nothing over the last frames it has, so the
        stream does not end in a click. The peak of each voice and of the sink is the greatest sample after the gain.

        RESAMPLING

        A windowed-sinc polyphase filter (Kaiser window, 128 taps a phase at a ratio of one or less, the taps growing with
        the ratio when the rate goes down; 512 phases, fewer as the taps grow, with linear interpolation between phases)
        centred on 95% of the lower Nyquist rate: the response is flat (under 0.001 dB) to about 87% of it and the stopband,
        beyond 100%, is about 144 dB down (a Kaiser window of AUDIO_KAISER_BETA). Measured (test/checks.c, the quality rows):
        44.1 to 48 kHz, 48 to 44.1 kHz and 96 to 48 kHz each give 139 to 150 dB SINAD on a float client, and a tone just above
        the output's Nyquist rate folds down at more than 140 dB below it. The tables
        are made from the two rates by sin and a Bessel function written here (no libm), cached by rates, and shared by
        the voices that use them. The filter is centred: an output frame is the signal at its own time, not delayed, and
        the filter needs half its taps of the stream ahead of the frame it is for, which is the latency
        audio_voice_delay reports. Equal rates skip it altogether.
*/
#ifndef AUDIO_MIX_INCLUDED
#define AUDIO_MIX_INCLUDED

#define AUDIO_API static __attribute__((unused))

/* The formats are PulseAudio's numbers, which is what the wire carries. */
#define AUDIO_FORMAT_U8 0
#define AUDIO_FORMAT_ALAW 1
#define AUDIO_FORMAT_ULAW 2
#define AUDIO_FORMAT_S16LE 3
#define AUDIO_FORMAT_S16BE 4
#define AUDIO_FORMAT_F32LE 5
#define AUDIO_FORMAT_F32BE 6
#define AUDIO_FORMAT_S32LE 7
#define AUDIO_FORMAT_S32BE 8
#define AUDIO_FORMAT_S24LE 9      // three bytes a sample
#define AUDIO_FORMAT_S24BE 10
#define AUDIO_FORMAT_S24_32LE 11  // 24 bits in the low three bytes of four
#define AUDIO_FORMAT_S24_32BE 12
#define AUDIO_FORMATS 13

#define AUDIO_RATE_MAX 384000
#define AUDIO_CHANNELS_MAX 8
#define AUDIO_BLOCK 512            // the frames one mix call handles
#define AUDIO_RATIO_MAX 8          // input rate over output rate, rounded up, that is resampled
#define AUDIO_TAPS 128             // taps a phase at a ratio of one or less (the transition band is 0.1 of the lower Nyquist rate)
#define AUDIO_KAISER_BETA 14.8     // a Kaiser window of about 144 dB: the 24-bit floor, not the 16-bit one (9.84 gave 98 dB)
#define AUDIO_PHASES 512
#define AUDIO_TABLE_FLOATS (AUDIO_TAPS * AUDIO_PHASES + AUDIO_TAPS * AUDIO_RATIO_MAX)
#define AUDIO_TABLES 12
#define AUDIO_EXACT_MOST 4096      // the most places between two inputs an exact table has a row for
#define AUDIO_RAMP 96              // frames a gain of one takes to ramp
#define AUDIO_WORK (AUDIO_BLOCK * AUDIO_RATIO_MAX + 2 * AUDIO_TAPS * AUDIO_RATIO_MAX)
#define AUDIO_VOLUME_NORM 0x10000ul
#define AUDIO_VOLUME_MAX 0x20000ul // 200%: the most a volume is taken to be

/* Channel positions are PulseAudio's too. */
#define AUDIO_POSITION_MONO 0
#define AUDIO_POSITION_FRONT_LEFT 1
#define AUDIO_POSITION_FRONT_RIGHT 2
#define AUDIO_POSITION_FRONT_CENTER 3
#define AUDIO_POSITION_REAR_CENTER 4
#define AUDIO_POSITION_REAR_LEFT 5
#define AUDIO_POSITION_REAR_RIGHT 6
#define AUDIO_POSITION_LFE 7
#define AUDIO_POSITION_FRONT_LEFT_OF_CENTER 8
#define AUDIO_POSITION_FRONT_RIGHT_OF_CENTER 9
#define AUDIO_POSITION_SIDE_LEFT 10
#define AUDIO_POSITION_SIDE_RIGHT 11
#define AUDIO_POSITION_AUX0 12     // AUX0 to AUX31 are 12 to 43
#define AUDIO_POSITION_MAX 63

#define AUDIO_E_INVALID (-1)
#define AUDIO_E_NOMEM (-2)

/*
        THE LIBRARY'S LOOPS

        The loops that run over every sample of every stream every period are lib.c's: assembly on the three machines,
        one routine each, named for what they do. They are declared here only where lib.c is not (a check, a stand-alone
        compile of this file): the includer then supplies a routine of the same name, the check its own C. Where lib.c is
        in the translation unit its declaration stands.
*/
#ifndef STANDARD_MODERN_C
void resample_polyphase2_f32(float *out0, float *out1, const float *x0, const float *x1, const float *table, unsigned long taps,
                             unsigned long phases, unsigned long pos, unsigned long step, unsigned long count);
unsigned long resample_polyphase2_f32_rows(float *out0, float *out1, const float *x0, const float *x1, const float *table, unsigned long taps,
                                           unsigned long phases, unsigned long pos, unsigned long step, unsigned long count);
float f32_scale_peak(float *dst, const float *x, unsigned long count, float gain);
float f32_scale_add_peak(float *dst, const float *x, unsigned long count, float gain);
void f32_planes_from_s16(float *const *planes, const unsigned char *src, unsigned long frames, unsigned long channels);
void f32_planes_from_s32(float *const *planes, const unsigned char *src, unsigned long frames, unsigned long channels);
void f32_planes_from_f32(float *const *planes, const unsigned char *src, unsigned long frames, unsigned long channels);
void f32_pair_to_s16(unsigned char *dst, const float *left, const float *right, unsigned long count);
void f32_pair_to_s32(unsigned char *dst, const float *left, const float *right, unsigned long count);
void f32_pair_to_s24_32(unsigned char *dst, const float *left, const float *right, unsigned long count);
void f32_pair_to_f32(unsigned char *dst, const float *left, const float *right, unsigned long count);
unsigned long f32_pair_clip(float *left, float *right, unsigned long count);
void f32_rows_mix2(float *out0, float *out1, const float *rows, unsigned long stride, const float *matrix, unsigned long count, unsigned long channels);
void pcm_widen(unsigned int *dst, const unsigned char *src, unsigned long samples, unsigned long kind);
void f32_planes_from_law(float *const *planes, const unsigned char *src, unsigned long frames, unsigned long channels, unsigned long law);
void pcm_pack24(unsigned char *dst, const unsigned int *src, unsigned long lanes);
#endif

/* ---- numbers: sin, sqrt and the Bessel function, in double, with no library ---- */

AUDIO_API double audio_sin(double x)
{
        const double pi = 3.14159265358979323846, two_pi = 6.28318530717958647692, half_pi = 1.57079632679489661923;
        double n = x / two_pi, x2, r;
        long k = (long)(n < 0 ? n - 0.5 : n + 0.5);

        x -= (double)k * two_pi;
        if (x > half_pi)
                x = pi - x;
        else if (x < -half_pi)
                x = -pi - x;
        x2 = x * x;
        r = -1.0 / 121645100408832000.0;
        r = r * x2 + 1.0 / 355687428096000.0;
        r = r * x2 - 1.0 / 1307674368000.0;
        r = r * x2 + 1.0 / 6227020800.0;
        r = r * x2 - 1.0 / 39916800.0;
        r = r * x2 + 1.0 / 362880.0;
        r = r * x2 - 1.0 / 5040.0;
        r = r * x2 + 1.0 / 120.0;
        r = r * x2 - 1.0 / 6.0;
        r = r * x2 + 1.0;
        return x * r;
}

AUDIO_API double audio_sqrt(double x)
{
        double r;

        if (!(x > 0))
                return 0;
        r = x > 1 ? x : 1;
        for (int step = 0; step < 100; step++)
        {
                double next = 0.5 * (r + x / r);

                if (next >= r && step > 4)
                        break;
                r = next;
        }
        return r;
}

// I0, the modified Bessel function of the first kind and order zero: the Kaiser window's shape.
AUDIO_API double audio_bessel0(double x)
{
        double term = 1, sum = 1, q = x * x / 4;

        for (int k = 1; k < 400; k++)
        {
                term *= q / ((double)k * (double)k);
                sum += term;
                if (term < sum * 1e-17)
                        break;
        }
        return sum;
}

/* ---- the volume curve ---- */

// The linear gain of a PulseAudio volume: the cube of its ratio to 100%, and nothing past AUDIO_VOLUME_MAX.
AUDIO_API float audio_volume_gain(unsigned long volume)
{
        float x;

        if (volume > AUDIO_VOLUME_MAX)
                volume = AUDIO_VOLUME_MAX;
        x = (float)volume * (1.0f / 65536.0f);
        return x * x * x;
}

// A gain asked for by a caller: not a number is silence, nothing is below silence, and nothing is past 200% (a gain of eight).
#define AUDIO_GAIN_MAX 8.0f
static inline float audio_gain_clean(float g)
{
        return g != g || g < 0.0f ? 0.0f : g > AUDIO_GAIN_MAX ? AUDIO_GAIN_MAX : g;
}

/* ---- the formats ---- */

AUDIO_API long audio_format_bytes(long format)
{
        static const unsigned char bytes[AUDIO_FORMATS] = {1, 1, 1, 2, 2, 4, 4, 4, 4, 3, 3, 4, 4};

        return format >= 0 && format < AUDIO_FORMATS ? bytes[format] : 0;
}

// The byte that is silence in a format.
AUDIO_API unsigned char audio_format_silence(long format)
{
        return format == AUDIO_FORMAT_U8 ? 0x80 : format == AUDIO_FORMAT_ALAW ? 0xd5 : format == AUDIO_FORMAT_ULAW ? 0xff : 0;
}

// Kept for the callers that name it: the two laws of G.711 are arithmetic in the library's pcm_widen now, and nothing is made here.
AUDIO_API void audio_init(void)
{
}

/*
        frames of the format, channels of them interleaved, into one planar float array a channel. dst[c] is the channel's
        array, written from its first element. channels is 1 to AUDIO_CHANNELS_MAX.

        Signed 16-bit, signed 32-bit and float samples in the machine's order are the library's decoders', and the two laws of
        G.711 are the library's table lookup (f32_planes_from_law). Every other format goes in two library passes over a run of
        frames at a time: pcm_widen turns the run's samples into 32-bit lanes (8-bit, the byte-swapped words, the 24-bit forms
        in three bytes and in four), and a decoder turns the lanes into
        floats, the exact ones the format's C made (a lane is the sample shifted up to the top of a 32-bit integer, so the
        integer decoder's scale gives the sample's own; a byte-swapped float is a float, held to [-16, 16] with a NaN made zero).
*/
#define AUDIO_WIDE 4096 // lanes a pass holds
#define AUDIO_PACK 1024 // frames a pass of the 24-bit encoder holds

AUDIO_API void audio_decode(long format, const unsigned char *src, long frames, long channels, float *const *dst)
{
        long bytes = audio_format_bytes(format), done = 0;
        unsigned long kind;
        static unsigned int wide[AUDIO_WIDE];

        if (frames <= 0 || channels <= 0 || channels > AUDIO_CHANNELS_MAX)
                return;
        if (format == AUDIO_FORMAT_S16LE || format == AUDIO_FORMAT_S32LE || format == AUDIO_FORMAT_F32LE)
        {
                if (format == AUDIO_FORMAT_S16LE)
                        f32_planes_from_s16(dst, src, (unsigned long)frames, (unsigned long)channels);
                else if (format == AUDIO_FORMAT_S32LE)
                        f32_planes_from_s32(dst, src, (unsigned long)frames, (unsigned long)channels);
                else
                        f32_planes_from_f32(dst, src, (unsigned long)frames, (unsigned long)channels);
                return;
        }
        if (format == AUDIO_FORMAT_ALAW || format == AUDIO_FORMAT_ULAW)
        {
                f32_planes_from_law(dst, src, (unsigned long)frames, (unsigned long)channels, format == AUDIO_FORMAT_ULAW);
                return;
        }
        kind = format == AUDIO_FORMAT_U8 ? 0 : format == AUDIO_FORMAT_S16BE ? 1 : format == AUDIO_FORMAT_S32BE || format == AUDIO_FORMAT_F32BE ? 2 :
               format == AUDIO_FORMAT_S24LE ? 3 : format == AUDIO_FORMAT_S24BE ? 4 : format == AUDIO_FORMAT_S24_32LE ? 5 : format == AUDIO_FORMAT_S24_32BE ? 6 : 7;
        if (kind > 6)
        {
                for (long c = 0; c < channels; c++)
                        memory_fill(dst[c], 0, (unsigned long)frames * sizeof(float));
                return;
        }
        while (done < frames)
        {
                long take = AUDIO_WIDE / channels, c;
                float *planes[AUDIO_CHANNELS_MAX];

                if (take > frames - done)
                        take = frames - done;
                for (c = 0; c < channels; c++)
                        planes[c] = dst[c] + done;
                pcm_widen(wide, src + done * channels * bytes, (unsigned long)(take * channels), kind);
                if (format == AUDIO_FORMAT_F32BE)
                        f32_planes_from_f32(planes, (const unsigned char *)wide, (unsigned long)take, (unsigned long)channels);
                else
                        f32_planes_from_s32(planes, (const unsigned char *)wide, (unsigned long)take, (unsigned long)channels);
                done += take;
        }
}

// A float in [-1, 1] as a rounded integer of the given scale, ties to even, clipped into the integer's range.
static inline int audio_round16(float v)
{
        float r;
        union { float f; int i; } bits;

        if (v != v)
                return 0;
        v *= 32768.0f;
        v = v > 32767.0f ? 32767.0f : v;
        v = v < -32768.0f ? -32768.0f : v;
        r = v + 12582912.0f; // 1.5 * 2^23: the sum's low bits are the rounded integer
        bits.f = r;
        return (bits.i & 0x7fffff) - 0x400000;
}

static inline int audio_round_wide(float v, double scale, double high)
{
        double d = (double)v * scale, r;

        if (v != v)
                return 0;
        d = d > high ? high : d;
        d = d < -high - 1.0 ? -high - 1.0 : d;
        r = d + 6755399441055744.0; // 1.5 * 2^52
        union { double d; long long i; } bits;

        bits.d = r;
        return (int)(bits.i & 0xffffffffll);
}

/*
        The bus into a card's bytes: format is one of S16LE, S24LE, S24_32LE, S32LE, F32LE. The pair goes to the first two
        channels of the card, a mono card gets the mean, and a channel past the second is silence. Answers the bytes written.
*/
AUDIO_API long audio_encode(long format, unsigned char *dst, long frames, long channels, const float *left, const float *right)
{
        long bytes = audio_format_bytes(format);

        // Only the card's formats are made: any other would be written four bytes a sample and answered as its own width.
        if (frames < 1 || channels < 1 || !(format == AUDIO_FORMAT_S16LE || format == AUDIO_FORMAT_S24LE || format == AUDIO_FORMAT_S24_32LE ||
                                             format == AUDIO_FORMAT_S32LE || format == AUDIO_FORMAT_F32LE))
                return 0;
        // A card of two channels, which is what is asked for whenever it can be, in the formats it is asked in: the library's.
        if (channels == 2 && frames > 0 && (format == AUDIO_FORMAT_S16LE || format == AUDIO_FORMAT_S32LE || format == AUDIO_FORMAT_S24_32LE || format == AUDIO_FORMAT_F32LE))
        {
                if (format == AUDIO_FORMAT_S16LE)
                        f32_pair_to_s16(dst, left, right, (unsigned long)frames);
                else if (format == AUDIO_FORMAT_S32LE)
                        f32_pair_to_s32(dst, left, right, (unsigned long)frames);
                else if (format == AUDIO_FORMAT_S24_32LE)
                        f32_pair_to_s24_32(dst, left, right, (unsigned long)frames);
                else
                        f32_pair_to_f32(dst, left, right, (unsigned long)frames);
                return frames * 2 * bytes;
        }
        // The 24-bit form in three bytes: the library's 24-in-32 pairs, packed to three bytes by its next routine, a pass of 1024
        // frames at a time.
        if (channels == 2 && format == AUDIO_FORMAT_S24LE)
        {
                static unsigned int lanes[2 * AUDIO_PACK];

                for (long done = 0; done < frames;)
                {
                        long take = frames - done < AUDIO_PACK ? frames - done : AUDIO_PACK;

                        f32_pair_to_s24_32((unsigned char *)lanes, left + done, right + done, (unsigned long)take);
                        pcm_pack24(dst + done * 6, lanes, (unsigned long)(2 * take));
                        done += take;
                }
                return frames * 2 * bytes;
        }
        for (long i = 0; i < frames; i++)
        {
                for (long c = 0; c < channels; c++)
                {
                        float v = channels == 1 ? (left[i] + right[i]) * 0.5f : c == 0 ? left[i] : c == 1 ? right[i] : 0.0f;
                        unsigned char *q = dst + (i * channels + c) * bytes;

                        switch (format)
                        {
                        case AUDIO_FORMAT_S16LE:
                        {
                                unsigned short x = (unsigned short)(short)audio_round16(v);

                                q[0] = (unsigned char)x;
                                q[1] = (unsigned char)(x >> 8);
                                break;
                        }
                        case AUDIO_FORMAT_S32LE:
                        {
                                unsigned int x = (unsigned int)audio_round_wide(v, 2147483648.0, 2147483647.0);

                                q[0] = (unsigned char)x;
                                q[1] = (unsigned char)(x >> 8);
                                q[2] = (unsigned char)(x >> 16);
                                q[3] = (unsigned char)(x >> 24);
                                break;
                        }
                        case AUDIO_FORMAT_S24_32LE:
                        {
                                unsigned int x = (unsigned int)audio_round_wide(v, 8388608.0, 8388607.0);

                                q[0] = (unsigned char)x;
                                q[1] = (unsigned char)(x >> 8);
                                q[2] = (unsigned char)(x >> 16);
                                q[3] = (unsigned char)(x >> 24);
                                break;
                        }
                        case AUDIO_FORMAT_S24LE:
                        {
                                int x = audio_round_wide(v, 8388608.0, 8388607.0);

                                q[0] = (unsigned char)x;
                                q[1] = (unsigned char)(x >> 8);
                                q[2] = (unsigned char)(x >> 16);
                                break;
                        }
                        default: // F32LE
                        {
                                union { float f; unsigned int i; } x;

                                x.f = v > 1.0f ? 1.0f : v < -1.0f ? -1.0f : v;
                                q[0] = (unsigned char)x.i;
                                q[1] = (unsigned char)(x.i >> 8);
                                q[2] = (unsigned char)(x.i >> 16);
                                q[3] = (unsigned char)(x.i >> 24);
                                break;
                        }
                        }
                }
        }
        return frames * channels * bytes;
}

/* ---- the ring a program's bytes wait in ---- */

/*
        data is size bytes, a multiple of the stream's frame; read, write and limit are byte counts from the stream's start.
        What can be played is [read, limit); write is where the program's next bytes go, and it is behind limit after a seek
        back (a program may rewrite what it wrote and was not played), or ahead of it after a seek forward (the gap is
        silence).
*/
struct audio_ring
{
        unsigned char *data;
        unsigned long size, frame;
        unsigned long read, write, limit;
        unsigned char silence;
};

AUDIO_API void audio_ring_open(struct audio_ring *ring, unsigned char *data, unsigned long size, unsigned long frame, unsigned char silence)
{
        ring->data = data;
        ring->frame = frame;
        ring->size = size - size % frame;
        ring->read = ring->write = ring->limit = 0;
        ring->silence = silence;
}

AUDIO_API unsigned long audio_ring_fill(const struct audio_ring *ring)
{
        return ring->limit - ring->read;
}

// What can be written at the cursor before it would pass the oldest byte not yet played, a ring's size on.
AUDIO_API unsigned long audio_ring_room(const struct audio_ring *ring)
{
        return ring->size - (ring->write - ring->read);
}

// Bytes to the cursor, as many as fit and whole frames; answers how many went in.
AUDIO_API unsigned long audio_ring_put(struct audio_ring *ring, const unsigned char *src, unsigned long n)
{
        unsigned long room = audio_ring_room(ring), from, first;

        if (n > room)
                n = room;
        n -= n % ring->frame;
        from = ring->write % ring->size;
        first = ring->size - from;
        if (first > n)
                first = n;
        memory_copy_apart(ring->data + from, src, first);
        if (n > first)
                memory_copy_apart(ring->data, src + first, n - first);
        ring->write += n;
        if (ring->write > ring->limit)
                ring->limit = ring->write;
        return n;
}

// n bytes of silence at the cursor, as put does.
AUDIO_API unsigned long audio_ring_put_silence(struct audio_ring *ring, unsigned long n)
{
        unsigned long room = audio_ring_room(ring), from, first;

        if (n > room)
                n = room;
        n -= n % ring->frame;
        from = ring->write % ring->size;
        first = ring->size - from;
        if (first > n)
                first = n;
        memory_fill(ring->data + from, ring->silence, first);
        if (n > first)
                memory_fill(ring->data, ring->silence, n - first);
        ring->write += n;
        if (ring->write > ring->limit)
                ring->limit = ring->write;
        return n;
}

/*
        Moves the cursor as PulseAudio's seek does: mode 0 is relative to the cursor, 1 absolute, 2 relative to the read
        count, 3 relative to the limit. Behind the read count, a ring's size past it, off a frame, or any overflow is refused
        (-1); past the limit the gap is filled with silence.
*/
AUDIO_API int audio_ring_seek(struct audio_ring *ring, long mode, long long offset)
{
        long long target;
        unsigned long base = mode == 0 ? ring->write : mode == 2 ? ring->read : mode == 3 ? ring->limit : 0;

        if (mode < 0 || mode > 3 || offset % (long long)ring->frame)
                return -1;
        // An absolute offset is a place in the stream; the others are a distance, and none is further than the ring is big.
        if (mode == 1 ? offset < 0 : (offset > (long long)ring->size * 4 || offset < -(long long)ring->size * 4))
                return -1;
        target = (long long)base + offset;
        if (target < (long long)ring->read || target - (long long)ring->read > (long long)ring->size)
                return -1;
        if ((unsigned long)target > ring->limit)
        {
                unsigned long gap = (unsigned long)target - ring->limit, was = ring->write;

                ring->write = ring->limit;
                if (audio_ring_put_silence(ring, gap) != gap)
                {
                        ring->write = was;
                        return -1;
                }
        }
        ring->write = (unsigned long)target;
        return 0;
}

// What was played is gone: the read count moves on, and the cursor never stays behind it.
AUDIO_API void audio_ring_drop(struct audio_ring *ring, unsigned long n)
{
        ring->read += n;
        if (ring->write < ring->read)
                ring->write = ring->read;
}

// Everything not played is thrown away.
AUDIO_API void audio_ring_flush(struct audio_ring *ring)
{
        ring->read = ring->write = ring->limit = ring->limit > ring->write ? ring->limit : ring->write;
}

/* ---- the channel map ---- */

/*
        The two bus channels' share of each stream channel. A stream of one channel is both; two channels on the front pair
        are as they are; any other, by position: left, right and side channels to their side, the front and rear centre to
        both at -3 dB (a rear centre a half), the rears and sides at -3 dB, the low-frequency channel to neither, and a
        position without a side by the parity of the channel's number. A row that adds to more than one is scaled to one,
        so no remix can clip what its inputs did not.
*/
AUDIO_API int audio_remix_build(float matrix[2][AUDIO_CHANNELS_MAX], long channels, const signed char *map)
{
        int identity = 0;

        for (int o = 0; o < 2; o++)
                for (int c = 0; c < AUDIO_CHANNELS_MAX; c++)
                        matrix[o][c] = 0;
        if (channels == 1)
        {
                matrix[0][0] = matrix[1][0] = 1.0f;
                return 0;
        }
        if (channels == 2 && map[0] == AUDIO_POSITION_FRONT_LEFT && map[1] == AUDIO_POSITION_FRONT_RIGHT)
        {
                matrix[0][0] = matrix[1][1] = 1.0f;
                return 1;
        }
        for (long c = 0; c < channels; c++)
        {
                int position = map[c], side = -1;
                float weight = 1.0f;

                switch (position)
                {
                case AUDIO_POSITION_MONO:
                        side = 2;
                        break;
                case AUDIO_POSITION_FRONT_LEFT:
                case AUDIO_POSITION_FRONT_LEFT_OF_CENTER:
                        side = 0;
                        break;
                case AUDIO_POSITION_FRONT_RIGHT:
                case AUDIO_POSITION_FRONT_RIGHT_OF_CENTER:
                        side = 1;
                        break;
                case AUDIO_POSITION_FRONT_CENTER:
                        side = 2;
                        weight = 0.70710678f;
                        break;
                case AUDIO_POSITION_REAR_CENTER:
                        side = 2;
                        weight = 0.5f;
                        break;
                case AUDIO_POSITION_REAR_LEFT:
                case AUDIO_POSITION_SIDE_LEFT:
                        side = 0;
                        weight = 0.70710678f;
                        break;
                case AUDIO_POSITION_REAR_RIGHT:
                case AUDIO_POSITION_SIDE_RIGHT:
                        side = 1;
                        weight = 0.70710678f;
                        break;
                case AUDIO_POSITION_LFE:
                        break;
                default:
                        side = (int)(c & 1);
                        break;
                }
                if (side == 2)
                        matrix[0][c] = matrix[1][c] = weight;
                else if (side >= 0)
                        matrix[side][c] = weight;
        }
        for (int o = 0; o < 2; o++)
        {
                float sum = 0;

                for (long c = 0; c < channels; c++)
                        sum += matrix[o][c];
                if (sum > 1.0f)
                        for (long c = 0; c < channels; c++)
                                matrix[o][c] /= sum;
        }
        return identity;
}

/* ---- the resampler's tables ---- */

struct audio_table
{
        long rin, rout;    // the rates it is for; 0 when the slot is free
        long taps, phases; // taps a phase, and phases (the rows are phases + 1; an exact table has exactly phases rows)
        long exact;        // set: rin / rout is num / den in lowest terms and the table has a row for each of den places an output can be at
        unsigned long num, den;
        long users;
        unsigned long used;
        float h[AUDIO_TABLE_FLOATS] __attribute__((aligned(64))); // the rows start on a cache line
};

static struct audio_table audio_tables[AUDIO_TABLES];
static unsigned long audio_table_clock;

/*
        The filter for a ratio. An output frame at input time t is the sum over taps k of h[phase][k] x[floor(t) + k],
        the centre between the tap taps/2 - 1 and taps/2. The kernel is the ideal low pass at the lower Nyquist rate times
        a Kaiser window of AUDIO_KAISER_BETA (about 144 dB), a row at a time normalised to a gain of one, so a constant stays one. The phases beyond
        half are the first half turned around.
*/
AUDIO_API int audio_table_build(struct audio_table *table, long rin, long rout)
{
        long m, taps, phases, rows, exact = 0;
        unsigned long a, b, num, den;
        double cutoff, beta = AUDIO_KAISER_BETA, base;
        const double pi = 3.14159265358979323846;

        if (rin <= 0 || rout <= 0 || rin > AUDIO_RATE_MAX || rout > AUDIO_RATE_MAX)
                return AUDIO_E_INVALID;
        m = rin > rout ? (rin + rout - 1) / rout : 1;
        if (m > AUDIO_RATIO_MAX)
                return AUDIO_E_INVALID;
        // The cutoff is the middle of the transition band (0.9 to 1.0 of the lower Nyquist rate), not its top: the stopband starts at
        // the Nyquist rate, so what folds into the passband is rejected (at the old cutoff, 22.5 kHz folded to 21.6 kHz at -19 dB).
        cutoff = 0.475 * (rin > rout ? (double)rout / (double)rin : 1.0);
        base = audio_bessel0(beta);
        taps = AUDIO_TAPS * m;
        phases = AUDIO_PHASES / m;
        // Rates that reduce to num / den: an output is at one of den places between two inputs, and if there is room for a row
        // for each the table has exactly those, the rows are not mixed (no weight between two), and the position is counted in
        // whole numbers, so it never drifts.
        for (a = (unsigned long)rin, b = (unsigned long)rout; b;)
        {
                unsigned long t = a % b;

                a = b;
                b = t;
        }
        num = (unsigned long)rin / a;
        den = (unsigned long)rout / a;
        if (den <= AUDIO_EXACT_MOST && den * (unsigned long)taps <= AUDIO_TABLE_FLOATS)
        {
                exact = 1;
                phases = (long)den;
        }
        rows = exact ? phases : phases + 1;
        for (long p = 0; p <= phases / 2 && p < rows; p++)
        {
                float *row = table->h + p * taps;
                double sum = 0, frac = (double)p / (double)phases;

                for (long k = 0; k < taps; k++)
                {
                        double tau = frac + (double)(taps / 2 - 1 - k), u = tau / (double)(taps / 2), x = 2.0 * cutoff * tau, sinc, window;

                        sinc = x == 0 ? 1.0 : audio_sin(pi * x) / (pi * x);
                        window = u >= 1.0 || u <= -1.0 ? 0.0 : audio_bessel0(beta * audio_sqrt(1.0 - u * u)) / base;
                        sum += 2.0 * cutoff * sinc * window;
                        row[k] = (float)(2.0 * cutoff * sinc * window);
                }
                for (long k = 0; k < taps; k++)
                        row[k] = (float)((double)row[k] / sum);
        }
        for (long p = phases / 2 + 1; p < rows; p++)
                for (long k = 0; k < taps; k++)
                        table->h[p * taps + k] = table->h[(phases - p) * taps + taps - 1 - k];
        table->rin = rin;
        table->rout = rout;
        table->taps = taps;
        table->phases = phases;
        table->exact = exact;
        table->num = num;
        table->den = den;
        return 0;
}

// The table for two rates: the one cached, else one built in a slot nobody uses (the oldest). Counts a user.
AUDIO_API struct audio_table *audio_table_take(long rin, long rout)
{
        struct audio_table *pick = 0;

        for (int at = 0; at < AUDIO_TABLES; at++)
                if (audio_tables[at].rin == rin && audio_tables[at].rout == rout)
                {
                        pick = audio_tables + at;
                        break;
                }
        if (!pick)
        {
                for (int at = 0; at < AUDIO_TABLES; at++)
                        if (!audio_tables[at].users && (!pick || audio_tables[at].used < pick->used))
                                pick = audio_tables + at;
                if (!pick)
                        return 0;
                pick->rin = pick->rout = 0;
                if (audio_table_build(pick, rin, rout) < 0)
                        return 0;
        }
        pick->users++;
        pick->used = ++audio_table_clock;
        return pick;
}

AUDIO_API void audio_table_give(struct audio_table *table)
{
        if (table && table->users > 0)
                table->users--;
}

/* ---- the resampler ---- */

/*
        buf is the input as planar float: the history (taps - 1 frames, the signal's own before the first as zeros) and what
        the caller has appended; pos is the place of the next output in it, 32.32. A voice appends what it decoded, asks
        for the outputs, and what no output needs again is dropped from the front.
*/
struct audio_resampler
{
        const struct audio_table *table;
        unsigned long step; // input frames an output takes, 32.32 (rounded up on an exact table)
        unsigned long pos;
        unsigned long k;    // an exact table: the place within an input frame, in den-ths; pos's fraction is its ceiling
        long have;
        float buf[2][AUDIO_WORK];
};

AUDIO_API void audio_rs_reset(struct audio_resampler *rs)
{
        long lead = rs->table->taps / 2 - 1;

        memory_fill(rs->buf[0], 0, (unsigned long)lead * sizeof(float));
        memory_fill(rs->buf[1], 0, (unsigned long)lead * sizeof(float));
        rs->have = lead;
        rs->pos = 0;
        rs->k = 0;
}

AUDIO_API void audio_rs_open(struct audio_resampler *rs, const struct audio_table *table)
{
        rs->table = table;
        // Rounded up on an exact table: the walk then never lands under the row it means, which the rows-only kernel relies on.
        rs->step = table->exact ? (unsigned long)((((unsigned long long)table->num << 32) + table->den - 1) / table->den)
                                : (unsigned long)(((unsigned long long)table->rin << 32) / (unsigned long long)table->rout);
        audio_rs_reset(rs);
}

// The frames the caller should append so that frames outputs can be made.
AUDIO_API long audio_rs_need(const struct audio_resampler *rs, long frames)
{
        unsigned long last = (rs->pos + (unsigned long)(frames - 1) * rs->step) >> 32;
        long want = (long)last + rs->table->taps, need = want - rs->have;

        return need > 0 ? need : 0;
}

AUDIO_API long audio_rs_room(const struct audio_resampler *rs)
{
        return AUDIO_WORK - rs->have;
}

// How many outputs the appended frames allow.
AUDIO_API long audio_rs_ready(const struct audio_resampler *rs)
{
        unsigned long limit, count;

        if (rs->have < rs->table->taps)
                return 0;
        limit = (unsigned long)(rs->have - rs->table->taps + 1) << 32;
        if (rs->pos >= limit)
                return 0;
        count = (limit - rs->pos + rs->step - 1) / rs->step;
        return (long)count;
}

// Up to frames outputs into out[0] and out[1]; answers how many were made.
AUDIO_API long audio_rs_run(struct audio_resampler *rs, float *out0, float *out1, long frames)
{
        const struct audio_table *table = rs->table;
        long ready = audio_rs_ready(rs), made = ready < frames ? ready : frames, drop;
        unsigned long pos = rs->pos;

        // Each output is the signal at its own place: the row between the two nearest phases (a + f * (b - a)) dotted with
        // the taps of each plane. A float sum in one chain waits on every add; the library's body takes it in lanes.
        if (table->exact)
        {
                // The place counted in whole numbers: made outputs are made * num den-ths on, and the fraction handed to the kernel is rounded up so
                // that it lands on its row. Nothing is added up in the fixed point, so nothing drifts however long the stream plays.
                unsigned long advance = rs->k + (unsigned long)made * table->num;

                resample_polyphase2_f32_rows(out0, out1, rs->buf[0], rs->buf[1], table->h, (unsigned long)table->taps, (unsigned long)table->phases, pos,
                                             rs->step, (unsigned long)made);
                rs->k = advance % table->den;
                pos = ((pos >> 32) + advance / table->den) << 32 | (unsigned long)(((unsigned long long)rs->k << 32) + table->den - 1) / table->den;
        }
        else
        {
                resample_polyphase2_f32(out0, out1, rs->buf[0], rs->buf[1], table->h, (unsigned long)table->taps, (unsigned long)table->phases, pos,
                                        rs->step, (unsigned long)made);
                pos += (unsigned long)made * rs->step;
        }
        rs->pos = pos;
        drop = (long)(pos >> 32);
        if (drop > rs->have)
                drop = rs->have;
        if (drop > 0)
        {
                for (int c = 0; c < 2; c++)
                        memory_copy(rs->buf[c], rs->buf[c] + drop, (unsigned long)(rs->have - drop) * sizeof(float));
                rs->have -= drop;
                rs->pos -= (unsigned long)drop << 32;
        }
        return made;
}

/* ---- the voice ---- */

struct audio_voice
{
        long format, channels, rate, out_rate;
        unsigned long frame;            // bytes a frame
        signed char map[AUDIO_CHANNELS_MAX];
        float remix[2][AUDIO_CHANNELS_MAX];
        int identity;                   // two channels straight to the bus
        struct audio_ring ring;
        float target[2], now[2];        // the gain of each bus channel: where it is going, and where it is
        float peak[2];                  // the greatest sample since the last take
        struct audio_table *table;      // the resampler's, none at equal rates
        int flushed;                    // the filter's tail has been given zeros to come out on
        int flows;                      // set: a voice whose gain is nothing still plays on (muted), instead of standing still (corked)
        struct audio_resampler rs;
};

/* The sink: the bus of one card, and the room a block needs. */
struct audio_sink
{
        long rate, channels;
        float bus[2][AUDIO_BLOCK];
        float mix[2][AUDIO_BLOCK];
        float decoded[AUDIO_CHANNELS_MAX][AUDIO_BLOCK];
        float master_target, master_now;
        float peak[2];
        unsigned long clips;            // frames that went past full scale, since the start
};

AUDIO_API void audio_sink_open(struct audio_sink *sink, long rate, long channels)
{
        memory_fill(sink, 0, sizeof(*sink));
        sink->rate = rate;
        sink->channels = channels;
        sink->master_target = sink->master_now = 1.0f;
}

/*
        Makes a voice of a stream: format, channels, rate and channel map as the program asked, the sink's rate it is played
        at, and the memory of its ring (a multiple of the frame is used). A rate that is not the sink's takes a resampler
        table. 0, or a negative number and nothing taken.
*/
AUDIO_API int audio_voice_open(struct audio_voice *voice, long format, long channels, long rate, const signed char *map, long out_rate, unsigned char *ring, unsigned long ring_bytes)
{
        long bytes = audio_format_bytes(format);

        audio_init();
        if (!bytes || channels < 1 || channels > AUDIO_CHANNELS_MAX || rate < 1 || rate > AUDIO_RATE_MAX || out_rate < 1 ||
            out_rate > AUDIO_RATE_MAX || ring_bytes < (unsigned long)(bytes * channels))
                return AUDIO_E_INVALID;
        memory_fill(voice, 0, offset_of(struct audio_voice, rs));
        voice->format = format;
        voice->channels = channels;
        voice->rate = rate;
        voice->out_rate = out_rate;
        voice->frame = (unsigned long)(bytes * channels);
        for (long c = 0; c < channels; c++)
                voice->map[c] = map ? map[c] : (channels == 1 ? AUDIO_POSITION_MONO : c == 0 ? AUDIO_POSITION_FRONT_LEFT : c == 1 ? AUDIO_POSITION_FRONT_RIGHT : (signed char)(AUDIO_POSITION_AUX0 + c));
        voice->identity = audio_remix_build(voice->remix, channels, voice->map);
        if (rate != out_rate)
        {
                voice->table = audio_table_take(rate, out_rate);
                if (!voice->table)
                        return AUDIO_E_NOMEM;
                audio_rs_open(&voice->rs, voice->table);
        }
        audio_ring_open(&voice->ring, ring, ring_bytes, voice->frame, audio_format_silence(format));
        voice->target[0] = voice->target[1] = 1.0f;
        return 0;
}

AUDIO_API void audio_voice_close(struct audio_voice *voice)
{
        audio_table_give(voice->table);
        voice->table = 0;
}

// The gain of the two bus channels the voice is going to (it gets there by a ramp).
AUDIO_API void audio_voice_gain(struct audio_voice *voice, float left, float right)
{
        voice->target[0] = audio_gain_clean(left);
        voice->target[1] = audio_gain_clean(right);
}

// Changes the rate the stream is read at, as a program's own rate update does: the same table for a small change, else a new one.
AUDIO_API int audio_voice_rate(struct audio_voice *voice, long rate)
{
        struct audio_table *table;
        long was = voice->rate;

        if (rate < 1 || rate > AUDIO_RATE_MAX)
                return AUDIO_E_INVALID;
        if (rate == voice->rate)
                return 0;
        if (rate == voice->out_rate)
        {
                audio_table_give(voice->table);
                voice->table = 0;
                voice->rate = rate;
                return 0;
        }
        if (voice->table)
        {
                long m_was = was > voice->out_rate ? (was + voice->out_rate - 1) / voice->out_rate : 1;
                long m_now = rate > voice->out_rate ? (rate + voice->out_rate - 1) / voice->out_rate : 1;
                long close = (rate > was ? rate - was : was - rate) * 20 <= was;

                if (m_was == m_now && close && !voice->table->exact)
                {
                        voice->rs.step = (unsigned long)(((unsigned long long)rate << 32) / (unsigned long long)voice->out_rate);
                        voice->rate = rate;
                        return 0;
                }
        }
        table = audio_table_take(rate, voice->out_rate);
        if (!table)
                return AUDIO_E_NOMEM;
        audio_table_give(voice->table);
        voice->table = table;
        voice->rate = rate;
        audio_rs_open(&voice->rs, table);
        return 0;
}

// The voice is played at another sink's rate (it was moved): a table for the new ratio, or none; its filter starts over.
AUDIO_API int audio_voice_retarget(struct audio_voice *voice, long out_rate)
{
        struct audio_table *table = 0;

        if (out_rate < 1 || out_rate > AUDIO_RATE_MAX)
                return AUDIO_E_INVALID;
        if (out_rate == voice->out_rate)
                return 0;
        if (voice->rate != out_rate)
        {
                table = audio_table_take(voice->rate, out_rate);
                if (!table)
                        return AUDIO_E_NOMEM;
        }
        audio_table_give(voice->table);
        voice->table = table;
        voice->out_rate = out_rate;
        if (table)
                audio_rs_open(&voice->rs, table);
        return 0;
}

// The input frames of the stream ahead of the frame a voice is playing that its filter needs: its latency, in the stream's rate.
AUDIO_API long audio_voice_delay(const struct audio_voice *voice)
{
        return voice->table ? voice->table->taps / 2 : 0;
}

/* ---- the gain ---- */

/*
        dst (+)= x * g, g moving from *now toward target by at most slope a frame and held at it; the greatest |sample|
        goes to *peak. When the gain is held at zero the result is silence and the work is the same.
*/
static inline __attribute__((always_inline)) void audio_gain_run(float *dst, const float *x, long n, float *now, float target, float slope, float *peak, int add)
{
        float g = *now, pk = *peak;
        long i = 0, ramp = 0, steps = 0;

        if (g != target)
        {
                float distance = target > g ? target - g : g - target;

                steps = (long)(distance / slope);
                if ((float)steps * slope < distance)
                        steps++;
                ramp = steps > n ? n : steps;
                for (; i < ramp; i++)
                {
                        float y, a;

                        // The last step of a ramp lands on the target itself, not on what the steps added up to.
                        g = i + 1 == steps ? target : target > g ? (g + slope < target ? g + slope : target) : (g - slope > target ? g - slope : target);
                        y = x[i] * g;
                        a = y < 0 ? -y : y;
                        pk = a > pk ? a : pk;
                        dst[i] = add ? dst[i] + y : y;
                }
        }
        // Held at its gain: a multiply and a sum rounded apart, and the greatest sample, in the library's loop.
        if (i < n)
        {
                float held = add ? f32_scale_add_peak(dst + i, x + i, (unsigned long)(n - i), g) : f32_scale_peak(dst + i, x + i, (unsigned long)(n - i), g);

                pk = held > pk ? held : pk;
        }
        *now = g;
        *peak = pk;
}

// The slope of a gain's ramp toward target from now.
static inline float audio_slope(float now, float target)
{
        float most = now > target ? now : target;

        return (most > 1.0f ? most : 1.0f) * (1.0f / (float)AUDIO_RAMP);
}

/* ---- mixing a voice ---- */

// frames of the voice's ring, from its read count, decoded and remixed to the two bus channels at out.
static void audio_voice_pull(struct audio_sink *sink, struct audio_voice *voice, long frames, float *out0, float *out1)
{
        while (frames > 0)
        {
                long n = frames < AUDIO_BLOCK ? frames : AUDIO_BLOCK, done = 0;
                float *planes[AUDIO_CHANNELS_MAX];

                while (done < n)
                {
                        unsigned long from = voice->ring.read % voice->ring.size;
                        long span = (long)((voice->ring.size - from) / voice->frame), take = n - done;

                        if (take > span)
                                take = span;
                        for (long c = 0; c < voice->channels; c++)
                                planes[c] = sink->decoded[c] + done;
                        audio_decode(voice->format, voice->ring.data + from, take, voice->channels, planes);
                        voice->ring.read += (unsigned long)take * voice->frame;
                        done += take;
                }
                if (voice->identity)
                {
                        memory_copy_apart(out0, sink->decoded[0], (unsigned long)n * sizeof(float));
                        memory_copy_apart(out1, sink->decoded[1], (unsigned long)n * sizeof(float));
                }
                else
                        f32_rows_mix2(out0, out1, sink->decoded[0], AUDIO_BLOCK, voice->remix[0], (unsigned long)n, (unsigned long)voice->channels);
                out0 += n;
                out1 += n;
                frames -= n;
        }
        if (voice->ring.write < voice->ring.read)
                voice->ring.write = voice->ring.read;
}

/*
        Adds the next frames (at most AUDIO_BLOCK) of the voice to the sink's bus, at the voice's gain, and answers how many
        frames it played. Fewer than asked is the voice out of data: its last frames were faded to nothing so it ends
        without a click, and a resampling one is started over (the gap is the program's). With drain set the filter's tail
        is given zeros once the ring is empty, so the last of the data comes out. A voice whose gain is nothing and
        going nowhere plays nothing, and takes nothing from its ring (a corked stream's time stands still): the caller
        sets the gain to zero and waits for the ramp. A muted one is the same with flows set: it goes on being read, and
        adds nothing.
*/
AUDIO_API long audio_voice_mix(struct audio_sink *sink, struct audio_voice *voice, long frames, int drain)
{
        float *out0 = sink->mix[0], *out1 = sink->mix[1];
        long played, held = 0;

        if (frames > AUDIO_BLOCK)
                frames = AUDIO_BLOCK;
        if (frames < 1)
                return 0;
        if (!voice->flows && voice->now[0] == 0 && voice->now[1] == 0 && voice->target[0] == 0 && voice->target[1] == 0)
                return 0;

        if (!voice->table)
        {
                long available = (long)(audio_ring_fill(&voice->ring) / voice->frame);

                played = available < frames ? available : frames;
                audio_voice_pull(sink, voice, played, out0, out1);
        }
        else
        {
                struct audio_resampler *rs = &voice->rs;
                long need = audio_rs_need(rs, frames), available = (long)(audio_ring_fill(&voice->ring) / voice->frame), take = need < available ? need : available;
                long room = audio_rs_room(rs);

                if (take > room)
                        take = room;
                if (take > 0)
                {
                        // Decoded straight onto the end of the filter's input.
                        audio_voice_pull(sink, voice, take, rs->buf[0] + rs->have, rs->buf[1] + rs->have);
                        rs->have += take;
                        voice->flushed = 0;
                }
                if (drain && take < need && !audio_ring_fill(&voice->ring) && !voice->flushed)
                {
                        long zeros = rs->table->taps / 2;

                        if (zeros > audio_rs_room(rs))
                                zeros = audio_rs_room(rs);
                        memory_fill(rs->buf[0] + rs->have, 0, (unsigned long)zeros * sizeof(float));
                        memory_fill(rs->buf[1] + rs->have, 0, (unsigned long)zeros * sizeof(float));
                        rs->have += zeros;
                        voice->flushed = 1;
                }
                played = audio_rs_run(rs, out0, out1, frames);
        }

        // The gain: held to its target for the frames that had data, then, when data ran out, the last ones fade to nothing.
        if (played < frames)
                held = played < AUDIO_RAMP ? played : AUDIO_RAMP;
        for (int c = 0; c < 2; c++)
        {
                float *x = c ? out1 : out0, *dst = sink->bus[c];
                long steady = played - held;

                if (steady > 0)
                        audio_gain_run(dst, x, steady, voice->now + c, voice->target[c], audio_slope(voice->now[c], voice->target[c]), voice->peak + c, 1);
                if (held > 0)
                {
                        float slope = voice->now[c] / (float)held;

                        if (slope <= 0)
                                slope = 1.0f;
                        audio_gain_run(dst + steady, x + steady, held, voice->now + c, 0.0f, slope, voice->peak + c, 1);
                }
        }
        if (played < frames)
        {
                // Out of data: it starts again from nothing, with the filter's history gone.
                voice->now[0] = voice->now[1] = 0;
                if (voice->table && !(drain && voice->flushed))
                        audio_rs_reset(&voice->rs);
        }
        return played;
}

/* ---- the sink's output ---- */

AUDIO_API void audio_sink_begin(struct audio_sink *sink, long frames)
{
        if (frames > AUDIO_BLOCK)
                frames = AUDIO_BLOCK;
        if (frames > 0)
                for (int c = 0; c < 2; c++)
                        memory_fill(sink->bus[c], 0, (unsigned long)frames * sizeof(float));
}

AUDIO_API void audio_sink_gain(struct audio_sink *sink, float gain)
{
        sink->master_target = audio_gain_clean(gain);
}

/*
        The bus, at the master gain, as the card's bytes: format, channels, and how many frames (at most AUDIO_BLOCK).
        A frame whose sample passes full scale is counted in clips and held to it. Answers the bytes written.
*/
AUDIO_API long audio_sink_end(struct audio_sink *sink, unsigned char *dst, long format, long channels, long frames)
{
        float master = sink->master_now;

        if (frames > AUDIO_BLOCK)
                frames = AUDIO_BLOCK;
        for (int c = 0; c < 2; c++)
        {
                float now = master;

                audio_gain_run(sink->mix[c], sink->bus[c], frames, &now, sink->master_target, audio_slope(master, sink->master_target), sink->peak + c, 0);
                if (c)
                        sink->master_now = now;
        }
        if (frames > 0)
                sink->clips += f32_pair_clip(sink->mix[0], sink->mix[1], (unsigned long)frames);
        return audio_encode(format, dst, frames, channels, sink->mix[0], sink->mix[1]);
}

#endif // AUDIO_MIX_INCLUDED
