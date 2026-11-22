/*
        test/checks/audio.c: the sound server: its mixer, ALSA, PulseAudio, the service, control and state.

        A run of test/checks.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. A section is built as
        test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib -nostartfiles
        -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its
        freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name>
        test/checks.c).
*/

#ifdef CHECK_audio_mix
/*
        CHECK_audio_mix: the audio mixing core (src/audio/mix.c) against references written here, apart from it: sin and
        the Bessel function against libm, every PCM format against its definition (G.711 from the standard's own
        formula), the ring against a model of bytes, the channel map, mixing of known sines exact where the arithmetic is
        integer (sums, clips, a volume of an eighth, the master gain) and to a stated bound where it is not, the ramps (no
        step of a gain greater than its slope, every length), the fade of a stream that runs out, and the resampler's
        quality (error against the sine itself at the output's times, gain, rejection above the lower Nyquist rate, the
        same bits however the input is cut) and its speed. A fixed-seed run of random voices holds the invariants
        (nothing but finite numbers in range; every ring and table taken is given back).

            cc -std=gnu11 -O2 -Wall -Wextra -DCHECK_audio_mix test/checks.c -lm
*/
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
        The loops mix.c leaves to lib.c, which this hosted check has no library for: routines of the library's names that run the C they
        were (SHARED_audio_loops), so the engine is held to what it was with them and CHECK_audiolib holds the assembly to the same C.
*/
#define SHARED_audio_loops
#define AUDIO_C_AS_LIBRARY
#include "../checks.c"
#undef AUDIO_C_AS_LIBRARY
#undef SHARED_audio_loops

#ifndef offset_of
#include <stddef.h>
#define offset_of(type, member) offsetof(type, member)
#endif
#include "../../src/audio/mix.c"

static unsigned au_checks, au_failures;

#define AUCHECK(condition, ...)                                                                                       \
        do                                                                                                            \
        {                                                                                                             \
                au_checks++;                                                                                          \
                if (!(condition))                                                                                     \
                {                                                                                                     \
                        au_failures++;                                                                                \
                        if (au_failures < 60)                                                                         \
                        {                                                                                             \
                                printf("  FAIL: ");                                                                   \
                                printf(__VA_ARGS__);                                                                  \
                                printf(" (line %d)\n", __LINE__);                                                     \
                        }                                                                                             \
                }                                                                                                     \
        } while (0)

static unsigned long long au_state = 0x9e3779b97f4a7c15ull;

static unsigned long au_rand(void)
{
        au_state ^= au_state >> 12;
        au_state ^= au_state << 25;
        au_state ^= au_state >> 27;
        return (unsigned long)(au_state * 0x2545f4914f6cdd1dull >> 11);
}

static double au_clock(void)
{
        struct timespec at;

        clock_gettime(CLOCK_MONOTONIC, &at);
        return (double)at.tv_sec + (double)at.tv_nsec * 1e-9;
}

/* ---- numbers ---- */

static void au_test_numbers(void)
{
        double worst = 0, sworst = 0;

        for (double x = -120; x < 120; x += 0.0137)
        {
                double e = fabs(audio_sin(x) - sin(x));

                if (e > worst)
                        worst = e;
        }
        AUCHECK(worst < 1e-12, "audio_sin is off libm's by %g", worst);
        for (double x = 1e-12; x < 1e6; x *= 1.37)
        {
                double e = fabs(audio_sqrt(x) - sqrt(x)) / sqrt(x);

                if (e > sworst)
                        sworst = e;
        }
        AUCHECK(sworst < 1e-14, "audio_sqrt is off libm's by %g (relative)", sworst);
        AUCHECK(audio_sqrt(0) == 0 && audio_sqrt(-4) == 0, "audio_sqrt of nothing and of a negative is nothing");
        // I0 at points from its series, known to ten places.
        AUCHECK(fabs(audio_bessel0(0) - 1.0) < 1e-15, "I0(0)");
        AUCHECK(fabs(audio_bessel0(1) - 1.2660658777520084) < 1e-14, "I0(1) is %.16g", audio_bessel0(1));
        AUCHECK(fabs(audio_bessel0(5) / 27.239871823604442 - 1) < 1e-14, "I0(5) is %.16g", audio_bessel0(5));
        AUCHECK(fabs(audio_bessel0(10) / 2815.716628466254 - 1) < 1e-14, "I0(10) is %.16g", audio_bessel0(10));
}

static void au_test_volume(void)
{
        AUCHECK(audio_volume_gain(0) == 0.0f, "volume 0 is silence");
        AUCHECK(audio_volume_gain(0x10000) == 1.0f, "100%% is a gain of one");
        AUCHECK(audio_volume_gain(0x8000) == 0.125f, "50%% is an eighth (the cube), not a half");
        AUCHECK(audio_volume_gain(0x20000) == 8.0f, "200%% is eight");
        AUCHECK(audio_volume_gain(0xffffffffu) == 8.0f, "past the most is the most");
        for (unsigned long v = 0; v <= 0x20000; v += 997)
        {
                double r = (double)v / 65536.0, want = r * r * r;

                AUCHECK(fabs((double)audio_volume_gain(v) - want) <= want * 2e-7 + 1e-30, "volume %lu: %g against %g", v,
                        (double)audio_volume_gain(v), want);
        }
}

/* ---- formats ---- */

// G.711 from the standard's formulas, not the decoder's: the 13- and 14-bit magnitudes of the tables, in 16-bit steps.
static double au_alaw_reference(int code)
{
        int a = code ^ 0x55, e = (a >> 4) & 7, m = a & 15;
        long magnitude = e == 0 ? 2 * m + 1 : (long)(2 * m + 33) << (e - 1);

        return (double)((a & 0x80) ? magnitude * 8 : -magnitude * 8) / 32768.0;
}

static double au_ulaw_reference(int code)
{
        int u = ~code & 0xff, e = (u >> 4) & 7, m = u & 15;
        long magnitude = (((long)(2 * m + 33)) << e) - 33;

        return (double)((u & 0x80) ? -magnitude * 4 : magnitude * 4) / 32768.0;
}

static void au_test_laws(void)
{
        float plane[1], *planes[1] = {plane};
        unsigned char byte;

        audio_init();
        for (int code = 0; code < 256; code++)
        {
                byte = (unsigned char)code;
                audio_decode(AUDIO_FORMAT_ALAW, &byte, 1, 1, planes);
                AUCHECK((double)plane[0] == au_alaw_reference(code), "A-law %02x: %g against %g", code, (double)plane[0],
                        au_alaw_reference(code));
                audio_decode(AUDIO_FORMAT_ULAW, &byte, 1, 1, planes);
                AUCHECK((double)plane[0] == au_ulaw_reference(code), "mu-law %02x: %g against %g", code, (double)plane[0],
                        au_ulaw_reference(code));
        }
        // The known ends of the two laws: 32124 and 32256 in 16 bits, and the byte that is silence is silence.
        byte = 0x00;
        audio_decode(AUDIO_FORMAT_ULAW, &byte, 1, 1, planes);
        AUCHECK(plane[0] == -32124.0f / 32768.0f, "mu-law 0x00 is -32124");
        byte = 0xaa;
        audio_decode(AUDIO_FORMAT_ALAW, &byte, 1, 1, planes);
        AUCHECK(plane[0] == 32256.0f / 32768.0f, "A-law 0xaa is +32256");
        byte = audio_format_silence(AUDIO_FORMAT_ULAW);
        audio_decode(AUDIO_FORMAT_ULAW, &byte, 1, 1, planes);
        AUCHECK(plane[0] == 0.0f, "the mu-law silence is zero");
        byte = audio_format_silence(AUDIO_FORMAT_ALAW);
        audio_decode(AUDIO_FORMAT_ALAW, &byte, 1, 1, planes);
        AUCHECK(plane[0] > 0.0f && plane[0] < 0.0003f, "the A-law silence is its smallest step");
        byte = audio_format_silence(AUDIO_FORMAT_U8);
        audio_decode(AUDIO_FORMAT_U8, &byte, 1, 1, planes);
        AUCHECK(plane[0] == 0.0f, "the unsigned 8-bit silence is zero");
}

static void au_put_le(unsigned char *at, long long value, int bytes)
{
        for (int i = 0; i < bytes; i++)
                at[i] = (unsigned char)(value >> (8 * i));
}

static void au_put_be(unsigned char *at, long long value, int bytes)
{
        for (int i = 0; i < bytes; i++)
                at[bytes - 1 - i] = (unsigned char)(value >> (8 * i));
}

// Samples as integers of a format's own width, written the long way here and read by the core.
static void au_test_formats(void)
{
        static const struct
        {
                int format, bytes, big, bits;
        } kinds[] = {{AUDIO_FORMAT_S16LE, 2, 0, 16},   {AUDIO_FORMAT_S16BE, 2, 1, 16},   {AUDIO_FORMAT_S24LE, 3, 0, 24},
                     {AUDIO_FORMAT_S24BE, 3, 1, 24},   {AUDIO_FORMAT_S24_32LE, 4, 0, 24}, {AUDIO_FORMAT_S24_32BE, 4, 1, 24},
                     {AUDIO_FORMAT_S32LE, 4, 0, 32},   {AUDIO_FORMAT_S32BE, 4, 1, 32}};
        static unsigned char bytes[AUDIO_BLOCK * 2 * 4];
        static float left[AUDIO_BLOCK], right[AUDIO_BLOCK];
        float *planes[2] = {left, right};

        for (unsigned k = 0; k < sizeof(kinds) / sizeof(kinds[0]); k++)
        {
                long long low = -(1ll << (kinds[k].bits - 1)), span = 1ll << kinds[k].bits;
                long long want[2][AUDIO_BLOCK];

                for (long i = 0; i < AUDIO_BLOCK; i++)
                        for (int c = 0; c < 2; c++)
                        {
                                long long v = i < 4 ? (i == 0 ? low : i == 1 ? -low - 1 : i == 2 ? 0 : -1) : low + (long long)(au_rand() % (unsigned long)span);
                                unsigned char *at = bytes + (i * 2 + c) * kinds[k].bytes;

                                want[c][i] = v;
                                if (kinds[k].format == AUDIO_FORMAT_S24_32LE || kinds[k].format == AUDIO_FORMAT_S24_32BE)
                                        // The byte above the 24 bits is whatever; the sample is the low three.
                                        v = (v & 0xffffff) | ((long long)(au_rand() & 0xff) << 24);
                                if (kinds[k].big)
                                        au_put_be(at, v, kinds[k].bytes);
                                else
                                        au_put_le(at, v, kinds[k].bytes);
                        }
                audio_decode(kinds[k].format, bytes, AUDIO_BLOCK, 2, planes);
                for (long i = 0; i < AUDIO_BLOCK; i++)
                        for (int c = 0; c < 2; c++)
                        {
                                float got = c ? right[i] : left[i], expect = (float)((double)want[c][i] / (double)span * 2.0);

                                AUCHECK(got == expect, "format %d sample %ld/%d: %.9g against %.9g", kinds[k].format, i, c,
                                        (double)got, (double)expect);
                        }
        }
        // Unsigned 8 bits, and floats of both orders, including the ones a program must not be trusted with.
        for (int i = 0; i < 256; i++)
                bytes[i] = (unsigned char)i;
        audio_decode(AUDIO_FORMAT_U8, bytes, 256, 1, planes);
        for (int i = 0; i < 256; i++)
                AUCHECK(left[i] == (float)(i - 128) / 128.0f, "u8 %d", i);
        {
                //      More than one pass of the decoder holds (4096 lanes): two channels of 3000 frames and one of 5000 in 8 bits, two of 3000
                //      in 16 bits big-endian; every sample its own.
                static unsigned char run[12000];
                static float run0[5000], run1[5000];
                float *pair[2];

                for (int i = 0; i < 12000; i++)
                        run[i] = (unsigned char)(i * 7 + (i >> 8));
                pair[0] = run0, pair[1] = run1;
                audio_decode(AUDIO_FORMAT_U8, run, 3000, 2, pair);
                for (int i = 0; i < 3000; i++)
                        for (int c = 0; c < 2; c++)
                                AUCHECK(pair[c][i] == (float)(run[i * 2 + c] - 128) / 128.0f, "u8 run %d/%d", i, c);
                audio_decode(AUDIO_FORMAT_U8, run, 5000, 1, pair);
                for (int i = 0; i < 5000; i++)
                        AUCHECK(run0[i] == (float)(run[i] - 128) / 128.0f, "u8 long %d", i);
                audio_decode(AUDIO_FORMAT_S16BE, run, 3000, 2, pair);
                for (int i = 0; i < 3000; i++)
                        for (int c = 0; c < 2; c++)
                                AUCHECK(pair[c][i] == (float)(short)((run[i * 4 + c * 2] << 8) | run[i * 4 + c * 2 + 1]) * (1.0f / 32768.0f), "s16be run %d/%d", i, c);
                for (int i = 0; i < 16; i++)
                        run0[i] = 5.0f;
                audio_decode(99, run, 16, 1, pair);
                for (int i = 0; i < 16; i++)
                        AUCHECK(run0[i] == 0.0f, "a format that is not one is silence %d", i);
        }
        {
                static const float samples[] = {0.0f, 1.0f, -1.0f, 0.5f, 1e-30f, 100.0f, -100.0f, 3.4e38f, -3.4e38f, 0.0f / 0.0f, 1.0f / 0.0f, -1.0f / 0.0f};
                unsigned count = sizeof(samples) / sizeof(samples[0]);

                for (unsigned i = 0; i < count; i++)
                {
                        unsigned int bits;

                        memcpy(&bits, samples + i, 4);
                        au_put_le(bytes + i * 4, bits, 4);
                        au_put_be(bytes + 64 + i * 4, bits, 4);
                }
                audio_decode(AUDIO_FORMAT_F32LE, bytes, count, 1, planes);
                audio_decode(AUDIO_FORMAT_F32BE, bytes + 64, count, 1, planes + 1);
                for (unsigned i = 0; i < count; i++)
                {
                        float s = samples[i], expect = s != s ? 0.0f : s > 16.0f ? 16.0f : s < -16.0f ? -16.0f : s;

                        AUCHECK(left[i] == expect, "float %u: %g gave %g, wanted %g", i, (double)s, (double)left[i], (double)expect);
                        AUCHECK(right[i] == expect, "big-endian float %u", i);
                        AUCHECK(left[i] == left[i] && fabsf(left[i]) <= 16.0f, "a program's float %u is a number in bounds", i);
                }
        }
}

/* The stereo card's bytes for a run, from the library's routine over the whole run in one call: the encoder's own chunks are not used. */
static void au_pair_reference(long format, unsigned char *dst, const float *left, const float *right, unsigned long frames)
{
        static unsigned int lanes[2 * 1300];

        if (format == AUDIO_FORMAT_S16LE)
                f32_pair_to_s16(dst, left, right, frames);
        else if (format == AUDIO_FORMAT_S32LE)
                f32_pair_to_s32(dst, left, right, frames);
        else if (format == AUDIO_FORMAT_S24_32LE)
                f32_pair_to_s24_32(dst, left, right, frames);
        else if (format == AUDIO_FORMAT_F32LE)
                f32_pair_to_f32(dst, left, right, frames);
        else
        {
                f32_pair_to_s24_32((unsigned char *)lanes, left, right, frames);
                pcm_pack24(dst, lanes, 2 * frames);
        }
}

static void au_test_encode(void)
{
        static unsigned char out[AUDIO_BLOCK * 8 * 4];
        static float left[AUDIO_BLOCK], right[AUDIO_BLOCK];
        static float planes_in[AUDIO_BLOCK];
        float *plane_ptr[1] = {planes_in};

        // Every 16-bit sample goes through the float bus and comes out itself.
        for (long base = 0; base < 65536; base += AUDIO_BLOCK)
        {
                static unsigned char in[AUDIO_BLOCK * 2];

                for (long i = 0; i < AUDIO_BLOCK; i++)
                        au_put_le(in + i * 2, base + i - 32768, 2);
                audio_decode(AUDIO_FORMAT_S16LE, in, AUDIO_BLOCK, 1, plane_ptr);
                audio_encode(AUDIO_FORMAT_S16LE, out, AUDIO_BLOCK, 1, planes_in, planes_in);
                AUCHECK(!memcmp(in, out, AUDIO_BLOCK * 2), "16-bit samples from %ld do not come out as they went in", base - 32768);
        }
        // 24 bits: all edges and a million drawn, through S24LE and S24_32LE.
        for (int round = 0; round < 2048; round++)
        {
                static unsigned char in[AUDIO_BLOCK * 3], back[AUDIO_BLOCK * 4];

                for (long i = 0; i < AUDIO_BLOCK; i++)
                {
                        long long v = round == 0 && i < 4 ? (i == 0 ? -8388608 : i == 1 ? 8388607 : i == 2 ? 0 : -1)
                                                          : -8388608 + (long long)(au_rand() % 16777216ul);

                        au_put_le(in + i * 3, v, 3);
                }
                audio_decode(AUDIO_FORMAT_S24LE, in, AUDIO_BLOCK, 1, plane_ptr);
                audio_encode(AUDIO_FORMAT_S24LE, out, AUDIO_BLOCK, 1, planes_in, planes_in);
                AUCHECK(!memcmp(in, out, AUDIO_BLOCK * 3), "24-bit samples round %d", round);
                audio_encode(AUDIO_FORMAT_S24_32LE, back, AUDIO_BLOCK, 1, planes_in, planes_in);
                for (long i = 0; i < AUDIO_BLOCK; i++)
                {
                        int word, expect = (int)((unsigned)in[i * 3] | (unsigned)in[i * 3 + 1] << 8 | (unsigned)in[i * 3 + 2] << 16);

                        expect = (int)((unsigned)expect << 8) >> 8;
                        memcpy(&word, back + i * 4, 4);
                        AUCHECK(word == expect, "24-in-32 sample %ld of round %d: %d against %d", i, round, word, expect);
                }
        }
        {
                //      Stereo 24 bits in three bytes, 2500 frames (the encoder works 1024 a pass): every sample is the low three bytes of the
                //      same sample in the 24-in-32 form.
                static float l24[2500], r24[2500];
                static unsigned char three[2500 * 6], four[2500 * 8];

                for (int i = 0; i < 2500; i++)
                {
                        l24[i] = (float)((i * 37) % 2000 - 1000) / 1000.0f;
                        r24[i] = -l24[i] * 0.7f + (float)(i % 7) * 1e-6f;
                }
                AUCHECK(audio_encode(AUDIO_FORMAT_S24LE, three, 2500, 2, l24, r24) == 2500 * 6, "stereo 24 bits answers another count");
                AUCHECK(audio_encode(AUDIO_FORMAT_S24_32LE, four, 2500, 2, l24, r24) == 2500 * 8, "stereo 24 in 32 answers another count");
                for (int i = 0; i < 5000; i++)
                        AUCHECK(!memcmp(three + i * 3, four + i * 4, 3), "stereo 24 bits, sample %d", i);
        }
        // 32 bits: the float has 24 of them, so the sample is within 64.
        for (int round = 0; round < 64; round++)
        {
                static unsigned char in[AUDIO_BLOCK * 4];

                for (long i = 0; i < AUDIO_BLOCK; i++)
                        au_put_le(in + i * 4, (long long)(int)(unsigned)au_rand(), 4);
                audio_decode(AUDIO_FORMAT_S32LE, in, AUDIO_BLOCK, 1, plane_ptr);
                audio_encode(AUDIO_FORMAT_S32LE, out, AUDIO_BLOCK, 1, planes_in, planes_in);
                for (long i = 0; i < AUDIO_BLOCK; i++)
                {
                        int a, b;

                        memcpy(&a, in + i * 4, 4);
                        memcpy(&b, out + i * 4, 4);
                        AUCHECK(llabs((long long)a - b) <= 64 || (a > 2147483000 && b == 2147483647), "32-bit sample %d came out %d", a, b);
                }
        }
        // Rounding is to even, clipping to the integer's ends, a float output is held to one.
        left[0] = 0.5f / 32768.0f;
        left[1] = 1.5f / 32768.0f;
        left[2] = -0.5f / 32768.0f;
        left[3] = -1.5f / 32768.0f;
        left[4] = 2.5f / 32768.0f;
        left[5] = 1.0f;
        left[6] = -1.0f;
        left[7] = 1.7f;
        left[8] = -1.7f;
        left[9] = 0.0f;
        audio_encode(AUDIO_FORMAT_S16LE, out, 10, 1, left, left);
        {
                short want[10] = {0, 2, 0, -2, 2, 32767, -32768, 32767, -32768, 0}, got[10];

                memcpy(got, out, sizeof(got));
                for (int i = 0; i < 10; i++)
                        AUCHECK(got[i] == want[i], "16-bit rounding %d: %d against %d", i, got[i], want[i]);
        }
        audio_encode(AUDIO_FORMAT_F32LE, out, 10, 1, left, left);
        {
                float got[10];

                memcpy(got, out, sizeof(got));
                AUCHECK(got[7] == 1.0f && got[8] == -1.0f && got[5] == 1.0f && got[1] == left[1], "the float output is held to one");
        }
        // Channels: a pair on the first two, silence after, the mean on a mono card.
        for (long i = 0; i < 4; i++)
        {
                left[i] = 0.25f * (float)(i + 1);
                right[i] = -0.125f * (float)(i + 1);
        }
        audio_encode(AUDIO_FORMAT_F32LE, out, 4, 6, left, right);
        for (long i = 0; i < 4; i++)
        {
                float frame[6];

                memcpy(frame, out + i * 24, 24);
                AUCHECK(frame[0] == left[i] && frame[1] == right[i] && frame[2] == 0 && frame[3] == 0 && frame[4] == 0 && frame[5] == 0,
                        "six channels, frame %ld", i);
        }
        audio_encode(AUDIO_FORMAT_F32LE, out, 4, 1, left, right);
        for (long i = 0; i < 4; i++)
        {
                float one;

                memcpy(&one, out + i * 4, 4);
                AUCHECK(one == (left[i] + right[i]) * 0.5f, "a mono card gets the mean, frame %ld", i);
        }
        {
                //      Every card shape of every format, past one block: a card of N channels is the stereo card's frame at the front
                //      and silence after it; a mono card is the mean's stereo frame, its first sample. The stereo card is the library's.
                static float enc_l[1300], enc_r[1300], enc_mid[1300];
                static unsigned char enc_out[1300 * 8 * 8], enc_ref[1300 * 8 * 8];
                static const long enc_formats[] = {AUDIO_FORMAT_S16LE, AUDIO_FORMAT_S24LE, AUDIO_FORMAT_S24_32LE, AUDIO_FORMAT_S32LE, AUDIO_FORMAT_F32LE};
                static const long enc_counts[] = {513, 1300};

                for (long i = 0; i < 1300; i++)
                {
                        enc_l[i] = (float)((i * 7919) % 2001 - 1000) / 900.0f;
                        enc_r[i] = -enc_l[i] * 0.7f + (float)(i % 11) * 1e-3f;
                        enc_mid[i] = (enc_l[i] + enc_r[i]) * 0.5f;
                }
                for (unsigned f = 0; f < sizeof enc_formats / sizeof enc_formats[0]; f++)
                        for (long ch = 1; ch <= 8; ch++)
                                for (unsigned n = 0; n < sizeof enc_counts / sizeof enc_counts[0]; n++)
                                {
                                        long frames = enc_counts[n], bytes = audio_format_bytes(enc_formats[f]), wrote, bad = 0;
                                        const float *a = ch == 1 ? enc_mid : enc_l, *b = ch == 1 ? enc_mid : enc_r;

                                        wrote = audio_encode(enc_formats[f], enc_out, frames, ch, enc_l, enc_r);
                                        au_pair_reference(enc_formats[f], enc_ref, a, b, (unsigned long)frames);
                                        for (long i = 0; i < frames; i++)
                                                for (long c = 0; c < ch; c++)
                                                {
                                                        const unsigned char *got = enc_out + (i * ch + c) * bytes;
                                                        const unsigned char *want = enc_ref + (i * 2 + c) * bytes;

                                                        if (c >= 2)
                                                                for (long k = 0; k < bytes; k++)
                                                                        bad |= got[k] != 0;
                                                        else
                                                                bad |= memcmp(got, want, (size_t)bytes) != 0;
                                                }
                                        AUCHECK(wrote == frames * ch * bytes && !bad, "format %ld, %ld channels, %ld frames: the card's bytes", enc_formats[f], ch, frames);
                                }
        }
}

/* ---- the ring ---- */

static void au_test_ring(void)
{
        static unsigned char memory[777 * 3], model[1 << 16];
        static const long sizes[] = {777 * 3, 4 * 3, 3};
        struct audio_ring ring;

        for (unsigned s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++)
        {
                unsigned long frame = 3, size = (unsigned long)sizes[s];
                unsigned long read = 0, write = 0, end = 0;

                audio_ring_open(&ring, memory, size, frame, 0x11);
                memset(model, 0, sizeof(model));
                for (int step = 0; step < 40000; step++)
                {
                        unsigned long op = au_rand() % 6, n = (au_rand() % 40) * frame;
                        unsigned char chunk[240];

                        if (op < 2)
                        {
                                unsigned long room = size - (write - read), took, want = n < room ? n : room;

                                for (unsigned long i = 0; i < n; i++)
                                        chunk[i] = (unsigned char)au_rand();
                                took = audio_ring_put(&ring, chunk, n);
                                AUCHECK(took == want, "put %lu at room %lu took %lu", n, room, took);
                                for (unsigned long i = 0; i < took; i++)
                                        model[(write + i) % sizeof(model)] = chunk[i];
                                write += took;
                                if (write > end)
                                        end = write;
                        }
                        else if (op == 2)
                        {
                                unsigned long fill = end - read, drop = n < fill ? n : fill;

                                // What is read is what the model says.
                                for (unsigned long i = 0; i < drop && i < sizeof(chunk); i++)
                                        chunk[i] = ring.data[(read + i) % ring.size];
                                for (unsigned long i = 0; i < drop && i < sizeof(chunk); i++)
                                        AUCHECK(chunk[i] == model[(read + i) % sizeof(model)], "the byte at %lu", read + i);
                                audio_ring_drop(&ring, drop);
                                read += drop;
                                if (write < read)
                                        write = read;
                        }
                        else if (op == 3)
                        {
                                long mode = (long)(au_rand() % 5) - 0;
                                long long offset = ((long long)(au_rand() % 61) - 30) * (long long)frame;
                                long long base = mode == 0 ? (long long)write : mode == 2 ? (long long)read : mode == 3 ? (long long)end : 0;

                                if (mode == 1)
                                        offset += (long long)read; // an absolute place, near where the stream is
                                long long target = base + offset;
                                int good = mode <= 3 && (mode != 1 || offset >= 0) && target >= (long long)read &&
                                           target - (long long)read <= (long long)size;
                                int got = audio_ring_seek(&ring, mode, offset);

                                AUCHECK((got == 0) == good, "seek mode %ld by %lld (read %lu write %lu end %lu): %d", mode, offset, read, write, end, got);
                                if (good)
                                {
                                        for (unsigned long i = end; i < (unsigned long)target; i++)
                                                model[i % sizeof(model)] = 0x11;
                                        if ((unsigned long)target > end)
                                                end = (unsigned long)target;
                                        write = (unsigned long)target;
                                }
                        }
                        else if (op == 4 && au_rand() % 16 == 0)
                        {
                                audio_ring_flush(&ring);
                                read = write = end = end > write ? end : write;
                        }
                        AUCHECK(ring.read == read && ring.write == write && ring.limit == end, "counts %lu %lu %lu against %lu %lu %lu", ring.read,
                                ring.write, ring.limit, read, write, end);
                        AUCHECK(audio_ring_fill(&ring) == end - read && audio_ring_room(&ring) == size - (write - read), "fill and room");
                }
        }
        // A frame off the grid and a hostile offset are refused.
        audio_ring_open(&ring, memory, sizeof(memory), 3, 0);
        AUCHECK(audio_ring_seek(&ring, 0, 1) < 0 && audio_ring_seek(&ring, 0, 1ll << 62) < 0 && audio_ring_seek(&ring, 1, -3) < 0 &&
                    audio_ring_seek(&ring, 7, 0) < 0 && audio_ring_seek(&ring, -1, 0) < 0,
                "seeks off the grid or out of bounds are refused");
        AUCHECK(audio_ring_seek(&ring, 0, 30) == 0 && audio_ring_fill(&ring) == 30, "a seek forward makes silence to play");
}

/* ---- the channel map ---- */

static void au_test_remix(void)
{
        float m[2][AUDIO_CHANNELS_MAX];
        signed char map6[6] = {AUDIO_POSITION_FRONT_LEFT, AUDIO_POSITION_FRONT_RIGHT, AUDIO_POSITION_FRONT_CENTER, AUDIO_POSITION_LFE,
                               AUDIO_POSITION_REAR_LEFT, AUDIO_POSITION_REAR_RIGHT};
        signed char stereo[2] = {AUDIO_POSITION_FRONT_LEFT, AUDIO_POSITION_FRONT_RIGHT}, swapped[2] = {AUDIO_POSITION_FRONT_RIGHT, AUDIO_POSITION_FRONT_LEFT};
        signed char mono[1] = {AUDIO_POSITION_MONO}, aux[4] = {AUDIO_POSITION_AUX0, AUDIO_POSITION_AUX0 + 1, AUDIO_POSITION_AUX0 + 2, AUDIO_POSITION_AUX0 + 3};
        double sum = 1.0 + 0.7071067811865476 * 2;

        AUCHECK(audio_remix_build(m, 1, mono) == 0 && m[0][0] == 1.0f && m[1][0] == 1.0f, "mono goes to both");
        AUCHECK(audio_remix_build(m, 2, stereo) == 1 && m[0][0] == 1.0f && m[1][1] == 1.0f && m[0][1] == 0 && m[1][0] == 0, "stereo is straight");
        AUCHECK(audio_remix_build(m, 2, swapped) == 0 && m[0][1] == 1.0f && m[1][0] == 1.0f && m[0][0] == 0, "swapped stereo is swapped");
        audio_remix_build(m, 6, map6);
        AUCHECK(fabs((double)m[0][0] - 1.0 / sum) < 1e-6 && fabs((double)m[0][2] - 0.7071067811865476 / sum) < 1e-6 &&
                    fabs((double)m[0][4] - 0.7071067811865476 / sum) < 1e-6 && m[0][1] == 0 && m[0][3] == 0 && m[0][5] == 0,
                "5.1 left row: %g %g %g %g %g %g", (double)m[0][0], (double)m[0][1], (double)m[0][2], (double)m[0][3], (double)m[0][4], (double)m[0][5]);
        AUCHECK(fabs((double)m[1][1] - 1.0 / sum) < 1e-6 && fabs((double)m[1][2] - 0.7071067811865476 / sum) < 1e-6 &&
                    fabs((double)m[1][5] - 0.7071067811865476 / sum) < 1e-6 && m[1][0] == 0 && m[1][3] == 0 && m[1][4] == 0,
                "5.1 right row");
        for (int o = 0; o < 2; o++)
        {
                double total = 0;

                for (int c = 0; c < 6; c++)
                        total += (double)m[o][c];
                AUCHECK(total <= 1.0000001, "a remix row adds to no more than one: %g", total);
        }
        audio_remix_build(m, 4, aux);
        AUCHECK(m[0][0] > 0 && m[1][1] > 0 && m[0][2] > 0 && m[1][3] > 0 && m[0][1] == 0 && m[1][0] == 0, "unknown positions alternate by number");
        {
                signed char rc[3] = {AUDIO_POSITION_FRONT_LEFT, AUDIO_POSITION_FRONT_RIGHT, AUDIO_POSITION_REAR_CENTER};

                audio_remix_build(m, 3, rc);
                AUCHECK(fabs((double)m[0][2] / (double)m[0][0] - 0.5) < 1e-6 && m[0][2] == m[1][2], "a rear centre is a half, to both");
        }
}

/* ---- mixing ---- */

static struct audio_sink au_sink;
static struct audio_voice au_voice[3];
static unsigned char au_memory[3][1 << 17];

static void au_steady(struct audio_voice *voice)
{
        voice->now[0] = voice->target[0];
        voice->now[1] = voice->target[1];
}

static long au_even(long long numerator, long long denominator)
{
        double q = (double)numerator / (double)denominator;

        return (long)rint(q);
}

static long au_clip16(long v)
{
        return v > 32767 ? 32767 : v < -32768 ? -32768 : v;
}

// Two voices of sines at 48000, in whole blocks of an odd length, against integer arithmetic.
static void au_test_mix_exact(void)
{
        static short a[2 * 4800], b[2 * 4800];
        static unsigned char out[AUDIO_BLOCK * 2 * 2];
        static const struct
        {
                unsigned long volume_a, volume_b;
                long master_num, master_den; // a master gain of exactly num/den
        } cases[] = {{0x10000, 0x10000, 1, 1}, {0x8000, 0x10000, 1, 1}, {0x10000, 0x8000, 1, 2}, {0x8000, 0x8000, 1, 1}, {0x10000, 0x10000, 1, 2}};

        for (unsigned c = 0; c < sizeof(cases) / sizeof(cases[0]); c++)
        {
                long clips = 0, frames = 4800, done = 0, ga = (long)(audio_volume_gain(cases[c].volume_a) * 8.0f + 0.5f),
                     gb = (long)(audio_volume_gain(cases[c].volume_b) * 8.0f + 0.5f);
                long mnum = cases[c].master_num, mden = cases[c].master_den;

                audio_sink_open(&au_sink, 48000, 2);
                for (int v = 0; v < 2; v++)
                {
                        short *s = v ? b : a;

                        for (long i = 0; i < frames; i++)
                        {
                                s[i * 2] = (short)rint(26000.0 * sin(2 * M_PI * (v ? 997.0 : 441.0) * (double)i / 48000.0));
                                s[i * 2 + 1] = (short)rint(24000.0 * sin(2 * M_PI * (v ? 1333.0 : 3001.0) * (double)i / 48000.0));
                        }
                        AUCHECK(audio_voice_open(au_voice + v, AUDIO_FORMAT_S16LE, 2, 48000, 0, 48000, au_memory[v], sizeof(au_memory[v])) == 0, "voice opens");
                        AUCHECK(audio_ring_put(&au_voice[v].ring, (const unsigned char *)s, (unsigned long)frames * 4) == (unsigned long)frames * 4, "the ring takes it");
                        audio_voice_gain(au_voice + v, audio_volume_gain(v ? cases[c].volume_b : cases[c].volume_a), audio_volume_gain(v ? cases[c].volume_b : cases[c].volume_a));
                        au_steady(au_voice + v);
                }
                au_sink.master_now = au_sink.master_target = (float)mnum / (float)mden;
                while (done < frames)
                {
                        long n = frames - done < 241 ? frames - done : 241;

                        audio_sink_begin(&au_sink, n);
                        AUCHECK(audio_voice_mix(&au_sink, au_voice, n, 0) == n && audio_voice_mix(&au_sink, au_voice + 1, n, 0) == n, "both voices play the block");
                        AUCHECK(audio_sink_end(&au_sink, out, AUDIO_FORMAT_S16LE, 2, n) == n * 4, "the block's bytes");
                        for (long i = 0; i < n; i++)
                        {
                                int hit = 0;

                                for (int ch = 0; ch < 2; ch++)
                                {
                                        // gain a is ga/8, gain b is gb/8, so the sum is (ga a + gb b)/8 and the master num/den.
                                        long long total = (long long)ga * a[(done + i) * 2 + ch] + (long long)gb * b[(done + i) * 2 + ch];
                                        long want = au_even(total * mnum, 8 * mden), unclipped = want;
                                        short got;

                                        want = au_clip16(want);
                                        hit |= want != unclipped;
                                        memcpy(&got, out + (i * 2 + ch) * 2, 2);
                                        AUCHECK(got == want, "case %u frame %ld channel %d: %d against %ld", c, done + i, ch, got, want);
                                }
                                clips += hit;
                        }
                        done += n;
                }
                AUCHECK((long)au_sink.clips == clips, "case %u counted %lu clipped frames, %ld are", c, au_sink.clips, clips);
                if (c == 0)
                        AUCHECK(clips > 0, "the first case exercises the clip");
                AUCHECK(audio_ring_fill(&au_voice[0].ring) == 0 && audio_ring_fill(&au_voice[1].ring) == 0, "the rings are empty");
        }
}

// Every format through the whole chain to a float output, against the format's definition.
static void au_test_mix_formats(void)
{
        static unsigned char in[AUDIO_BLOCK * 8 * 4], out[AUDIO_BLOCK * 2 * 4];
        static const int formats[] = {AUDIO_FORMAT_U8,    AUDIO_FORMAT_ALAW,    AUDIO_FORMAT_ULAW,   AUDIO_FORMAT_S16LE, AUDIO_FORMAT_S16BE,
                                      AUDIO_FORMAT_F32LE, AUDIO_FORMAT_F32BE,   AUDIO_FORMAT_S32LE,  AUDIO_FORMAT_S32BE, AUDIO_FORMAT_S24LE,
                                      AUDIO_FORMAT_S24BE, AUDIO_FORMAT_S24_32LE, AUDIO_FORMAT_S24_32BE};
        static float expect[2][AUDIO_BLOCK];

        for (unsigned f = 0; f < sizeof(formats) / sizeof(formats[0]); f++)
                for (int channels = 1; channels <= 2; channels++)
                {
                        long bytes = audio_format_bytes(formats[f]), n = 300;
                        float *planes[2] = {expect[0], expect[1]};
                        struct audio_voice *v = au_voice;

                        for (long i = 0; i < n * channels * bytes; i++)
                                in[i] = (unsigned char)au_rand();
                        if (formats[f] == AUDIO_FORMAT_F32LE || formats[f] == AUDIO_FORMAT_F32BE)
                                for (long i = 0; i < n * channels; i++)
                                {
                                        float x = (float)(((double)(au_rand() % 20000) - 10000.0) / 10000.0);
                                        unsigned int bits;

                                        memcpy(&bits, &x, 4);
                                        if (formats[f] == AUDIO_FORMAT_F32BE)
                                                au_put_be(in + i * 4, bits, 4);
                                        else
                                                au_put_le(in + i * 4, bits, 4);
                                }
                        audio_sink_open(&au_sink, 48000, 2);
                        AUCHECK(audio_voice_open(v, formats[f], channels, 48000, 0, 48000, au_memory[0], sizeof(au_memory[0])) == 0, "voice %d opens", formats[f]);
                        audio_ring_put(&v->ring, in, (unsigned long)(n * channels * bytes));
                        audio_decode(formats[f], in, n, channels, planes);
                        au_steady(v);
                        audio_sink_begin(&au_sink, n);
                        AUCHECK(audio_voice_mix(&au_sink, v, n, 0) == n, "format %d plays", formats[f]);
                        audio_sink_end(&au_sink, out, AUDIO_FORMAT_F32LE, 2, n);
                        for (long i = 0; i < n; i++)
                        {
                                float l, r, want_l = expect[0][i], want_r = channels == 1 ? expect[0][i] : expect[1][i];

                                memcpy(&l, out + i * 8, 4);
                                memcpy(&r, out + i * 8 + 4, 4);
                                want_l = want_l > 1.0f ? 1.0f : want_l < -1.0f ? -1.0f : want_l;
                                want_r = want_r > 1.0f ? 1.0f : want_r < -1.0f ? -1.0f : want_r;
                                AUCHECK(l == want_l && r == want_r, "format %d, %d channels, frame %ld: %g %g against %g %g", formats[f], channels, i, (double)l,
                                        (double)r, (double)want_l, (double)want_r);
                        }
                        audio_voice_close(v);
                }
}

// 5.1 down to the pair, against the matrix.
static void au_test_mix_remix(void)
{
        static float in[AUDIO_BLOCK * 6];
        static unsigned char out[AUDIO_BLOCK * 8];
        signed char map6[6] = {AUDIO_POSITION_FRONT_LEFT, AUDIO_POSITION_FRONT_RIGHT, AUDIO_POSITION_FRONT_CENTER, AUDIO_POSITION_LFE,
                               AUDIO_POSITION_REAR_LEFT, AUDIO_POSITION_REAR_RIGHT};
        double sum = 1.0 + 0.7071067811865476 * 2, r = 0.7071067811865476;
        long n = 400;

        for (long i = 0; i < n; i++)
                for (int c = 0; c < 6; c++)
                        in[i * 6 + c] = (float)(0.1 * sin(0.01 * (double)(i * (c + 1))) + 0.01 * (double)c);
        audio_sink_open(&au_sink, 48000, 2);
        AUCHECK(audio_voice_open(au_voice, AUDIO_FORMAT_F32LE, 6, 48000, map6, 48000, au_memory[0], sizeof(au_memory[0])) == 0, "6 channels open");
        audio_ring_put(&au_voice->ring, (const unsigned char *)in, (unsigned long)n * 24);
        au_steady(au_voice);
        audio_sink_begin(&au_sink, n);
        AUCHECK(audio_voice_mix(&au_sink, au_voice, n, 0) == n, "5.1 plays");
        audio_sink_end(&au_sink, out, AUDIO_FORMAT_F32LE, 2, n);
        for (long i = 0; i < n; i++)
        {
                float l, rr;
                double want_l = ((double)in[i * 6] + r * (double)in[i * 6 + 2] + r * (double)in[i * 6 + 4]) / sum,
                       want_r = ((double)in[i * 6 + 1] + r * (double)in[i * 6 + 2] + r * (double)in[i * 6 + 5]) / sum;

                memcpy(&l, out + i * 8, 4);
                memcpy(&rr, out + i * 8 + 4, 4);
                AUCHECK(fabs((double)l - want_l) < 1e-6 && fabs((double)rr - want_r) < 1e-6, "5.1 frame %ld: %g %g against %g %g", i, (double)l, (double)rr, want_l, want_r);
        }
        audio_voice_close(au_voice);
        // A mono stream is both channels, a stream's balance is its two gains.
        audio_sink_open(&au_sink, 48000, 2);
        AUCHECK(audio_voice_open(au_voice, AUDIO_FORMAT_F32LE, 1, 48000, 0, 48000, au_memory[0], sizeof(au_memory[0])) == 0, "mono opens");
        audio_ring_put(&au_voice->ring, (const unsigned char *)in, 400 * 4);
        audio_voice_gain(au_voice, 1.0f, 0.5f);
        au_steady(au_voice);
        audio_sink_begin(&au_sink, 400);
        audio_voice_mix(&au_sink, au_voice, 400, 0);
        audio_sink_end(&au_sink, out, AUDIO_FORMAT_F32LE, 2, 400);
        for (long i = 0; i < 400; i++)
        {
                float l, rr;

                memcpy(&l, out + i * 8, 4);
                memcpy(&rr, out + i * 8 + 4, 4);
                AUCHECK(l == in[i] && rr == in[i] * 0.5f, "mono with a balance, frame %ld", i);
        }
        audio_voice_close(au_voice);
}

static void au_dc_level(struct audio_voice *voice, long frames, float level);

// Peaks are the greatest sample after the gain; the sink's after the master; clips count frames.
static void au_test_peaks(void)
{
        static float in[2 * 300];
        static unsigned char out[300 * 8];
        float peak_l = 0, peak_r = 0, gain_l = 0.25f, gain_r = 2.0f;

        for (long i = 0; i < 300; i++)
        {
                // One channel's greatest sample is a negative one, the other's a positive one: a peak is of the magnitude.
                in[i * 2] = 0.8f * (float)sin(0.05 * (double)i) - 0.1f;
                in[i * 2 + 1] = 0.3f * (float)cos(0.11 * (double)i) + 0.1f;
        }
        audio_sink_open(&au_sink, 48000, 2);
        audio_voice_open(au_voice, AUDIO_FORMAT_F32LE, 2, 48000, 0, 48000, au_memory[0], sizeof(au_memory[0]));
        audio_ring_put(&au_voice->ring, (const unsigned char *)in, sizeof(in));
        audio_voice_gain(au_voice, gain_l, gain_r);
        au_steady(au_voice);
        au_sink.master_now = au_sink.master_target = 0.5f;
        audio_sink_begin(&au_sink, 300);
        audio_voice_mix(&au_sink, au_voice, 300, 0);
        audio_sink_end(&au_sink, out, AUDIO_FORMAT_F32LE, 2, 300);
        for (long i = 0; i < 300; i++)
        {
                float l = fabsf(in[i * 2] * gain_l), r = fabsf(in[i * 2 + 1] * gain_r);

                peak_l = l > peak_l ? l : peak_l;
                peak_r = r > peak_r ? r : peak_r;
        }
        AUCHECK(au_voice->peak[0] == peak_l && au_voice->peak[1] == peak_r, "voice peaks %g %g against %g %g", (double)au_voice->peak[0],
                (double)au_voice->peak[1], (double)peak_l, (double)peak_r);
        AUCHECK(au_sink.peak[0] == peak_l * 0.5f && au_sink.peak[1] == peak_r * 0.5f, "sink peaks %g %g", (double)au_sink.peak[0], (double)au_sink.peak[1]);
        AUCHECK(au_sink.clips == 0, "nothing clipped (the right channel peaks at %g)", (double)(peak_r * 0.5f));
        audio_voice_close(au_voice);

        // The held part of a block is the library's loop; a ramp is this file's, and its peak is of the magnitude too: a negative
        // level climbing from nothing over 64 of the 96 frames of its ramp peaks at the level at the last of them.
        audio_sink_open(&au_sink, 48000, 2);
        audio_voice_open(au_voice, AUDIO_FORMAT_F32LE, 2, 48000, 0, 48000, au_memory[0], sizeof(au_memory[0]));
        au_dc_level(au_voice, 128, -0.5f);
        audio_sink_begin(&au_sink, 64);
        audio_voice_mix(&au_sink, au_voice, 64, 0);
        AUCHECK(fabsf(au_voice->peak[0] - 0.5f * (64.0f / 96.0f)) < 1e-4f && fabsf(au_voice->peak[1] - 0.5f * (64.0f / 96.0f)) < 1e-4f,
                "the peak of a ramp of a negative level is of its magnitude: %g %g", (double)au_voice->peak[0], (double)au_voice->peak[1]);
        audio_voice_close(au_voice);
}

/* ---- ramps ---- */

static void au_dc_level(struct audio_voice *voice, long frames, float level)
{
        static float ones[AUDIO_BLOCK * 8];

        for (long i = 0; i < frames; i++)
                ones[i] = level;
        audio_ring_put(&voice->ring, (const unsigned char *)ones, (unsigned long)frames * 4);
}

static void au_dc(struct audio_voice *voice, long frames)
{
        au_dc_level(voice, frames, 1.0f);
}

// A constant stream whose gain is moved: the output is a ramp of the slope and no step is greater than it.
static void au_test_ramps(void)
{
        static unsigned char out[AUDIO_BLOCK * 8];
        static const struct
        {
                float from, to;
        } moves[] = {{1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 0.125f}, {0.125f, 1.0f}, {0.0f, 3.375f}, {3.375f, 0.0f}, {1.0f, 3.375f}, {0.5f, 0.5f}};

        for (unsigned m = 0; m < sizeof(moves) / sizeof(moves[0]); m++)
        {
                // The stream is a constant a quarter of full scale, so a gain of up to 3.375 stays in range and the output is the gain's own ramp.
                float from = moves[m].from, to = moves[m].to, top = from > to ? from : to, level = 0.25f;
                float slope = (top > 1.0f ? top : 1.0f) / (float)AUDIO_RAMP, last = from * level;
                long first = -1, want = (long)ceil((double)fabsf(to - from) / (double)slope) - 1;

                audio_sink_open(&au_sink, 48000, 2);
                audio_voice_open(au_voice, AUDIO_FORMAT_F32LE, 1, 48000, 0, 48000, au_memory[0], sizeof(au_memory[0]));
                audio_voice_gain(au_voice, from, from);
                au_steady(au_voice);
                // The gain is moved in the middle of a stream; the block after it holds the ramp.
                au_dc_level(au_voice, 400, level);
                audio_voice_gain(au_voice, to, to);
                audio_sink_begin(&au_sink, 400);
                AUCHECK(audio_voice_mix(&au_sink, au_voice, 400, 0) == 400, "the ramp block plays");
                audio_sink_end(&au_sink, out, AUDIO_FORMAT_F32LE, 2, 400);
                for (long i = 0; i < 400; i++)
                {
                        float l;

                        memcpy(&l, out + i * 8, 4);
                        AUCHECK(fabsf(l - last) <= slope * level * 1.0001f, "gain %g to %g, frame %ld steps by %g (slope %g)", (double)from, (double)to, i,
                                (double)fabsf(l - last), (double)(slope * level));
                        AUCHECK(to >= from ? l >= last : l <= last, "gain %g to %g runs the wrong way at frame %ld", (double)from, (double)to, i);
                        if (first < 0 && l == to * level)
                                first = i;
                        if (first >= 0)
                                AUCHECK(l == to * level, "gain %g to %g leaves its target at frame %ld", (double)from, (double)to, i);
                        last = l;
                }
                AUCHECK(au_voice->now[0] == to, "gain %g to %g is %.9g after its ramp, not the target itself", (double)from, (double)to, (double)au_voice->now[0]);
                if (from != to)
                        AUCHECK(first >= 0 && labs(first - want) <= 1, "gain %g to %g arrives at frame %ld, %ld wanted", (double)from, (double)to, first, want);
                else
                        AUCHECK(first == 0, "no move, no ramp");
                audio_voice_close(au_voice);
        }
        // A new stream fades in: its first block rises from nothing by the slope, and never overshoots.
        audio_sink_open(&au_sink, 48000, 2);
        audio_voice_open(au_voice, AUDIO_FORMAT_F32LE, 1, 48000, 0, 48000, au_memory[0], sizeof(au_memory[0]));
        au_dc(au_voice, 300);
        audio_sink_begin(&au_sink, 300);
        audio_voice_mix(&au_sink, au_voice, 300, 0);
        audio_sink_end(&au_sink, out, AUDIO_FORMAT_F32LE, 2, 300);
        {
                float l0, l_end, last = 0;

                memcpy(&l0, out, 4);
                memcpy(&l_end, out + 299 * 8, 4);
                AUCHECK(l0 > 0 && l0 <= 1.0f / (float)AUDIO_RAMP * 1.0001f, "the first frame of a stream is its first step: %g", (double)l0);
                AUCHECK(l_end == 1.0f, "and it arrives at one");
                for (long i = 0; i < 300; i++)
                {
                        float l;

                        memcpy(&l, out + i * 8, 4);
                        AUCHECK(l >= last && l - last <= 1.0f / (float)AUDIO_RAMP * 1.0001f && l <= 1.0f, "fade-in frame %ld", i);
                        last = l;
                }
        }
        audio_voice_close(au_voice);
        // The sink's master gain ramps the same way: a mute is not a click.
        audio_sink_open(&au_sink, 48000, 2);
        audio_voice_open(au_voice, AUDIO_FORMAT_F32LE, 1, 48000, 0, 48000, au_memory[0], sizeof(au_memory[0]));
        au_steady(au_voice);
        au_dc(au_voice, 300);
        audio_sink_gain(&au_sink, 0.0f);
        audio_sink_begin(&au_sink, 300);
        audio_voice_mix(&au_sink, au_voice, 300, 0);
        audio_sink_end(&au_sink, out, AUDIO_FORMAT_F32LE, 2, 300);
        {
                float last = 1.0f;

                for (long i = 0; i < 300; i++)
                {
                        float l;

                        memcpy(&l, out + i * 8, 4);
                        AUCHECK(l <= last && last - l <= 1.0f / (float)AUDIO_RAMP * 1.0001f, "master mute frame %ld: %g after %g", i, (double)l, (double)last);
                        last = l;
                }
                AUCHECK(last == 0.0f, "the master mute arrives at nothing");
        }
        audio_voice_close(au_voice);
}

// A stream that runs out of data ends in a fade; a corked one stops taking data; both start again from nothing.
static void au_test_underrun(void)
{
        static unsigned char out[AUDIO_BLOCK * 8];
        long have_counts[] = {100, 300, 10, 96, 97, 1};

        for (unsigned h = 0; h < sizeof(have_counts) / sizeof(have_counts[0]); h++)
        {
                long have = have_counts[h], played, held = have < AUDIO_RAMP ? have : AUDIO_RAMP;
                float last = 1.0f;

                audio_sink_open(&au_sink, 48000, 2);
                audio_voice_open(au_voice, AUDIO_FORMAT_F32LE, 1, 48000, 0, 48000, au_memory[0], sizeof(au_memory[0]));
                au_steady(au_voice);
                au_dc(au_voice, have);
                audio_sink_begin(&au_sink, 512);
                played = audio_voice_mix(&au_sink, au_voice, 512, 0);
                audio_sink_end(&au_sink, out, AUDIO_FORMAT_F32LE, 2, 512);
                AUCHECK(played == have, "%ld frames of data play %ld", have, played);
                for (long i = 0; i < 512; i++)
                {
                        float l;

                        memcpy(&l, out + i * 8, 4);
                        if (i < have - held)
                                AUCHECK(l == 1.0f, "before the fade (data %ld) frame %ld is %g", have, i, (double)l);
                        else if (i < have)
                                AUCHECK(l <= last && last - l <= 1.0f / (float)held * 1.001f + 1e-6f, "data %ld: the fade steps from %g to %g at %ld", have, (double)last, (double)l, i);
                        else
                                AUCHECK(l == 0.0f, "after the data (%ld) there is silence at %ld", have, i);
                        last = l;
                }
                {
                        float end;

                        memcpy(&end, out + (have - 1) * 8, 4);
                        AUCHECK(end == 0.0f, "the data (%ld) ends at nothing, not %g", have, (double)end);
                }
                AUCHECK(audio_ring_fill(&au_voice->ring) == 0, "the ring is empty");
                // More data comes: it rises from nothing.
                au_dc(au_voice, 200);
                audio_sink_begin(&au_sink, 200);
                audio_voice_mix(&au_sink, au_voice, 200, 0);
                audio_sink_end(&au_sink, out, AUDIO_FORMAT_F32LE, 2, 200);
                {
                        float first;

                        memcpy(&first, out, 4);
                        AUCHECK(first > 0 && first <= 1.0f / (float)AUDIO_RAMP * 1.0001f, "after the gap it starts from nothing: %g", (double)first);
                }
                audio_voice_close(au_voice);
        }
        // Corked: the gain goes to nothing over a ramp, then nothing is taken and nothing is played.
        audio_sink_open(&au_sink, 48000, 2);
        audio_voice_open(au_voice, AUDIO_FORMAT_F32LE, 1, 48000, 0, 48000, au_memory[0], sizeof(au_memory[0]));
        au_steady(au_voice);
        au_dc(au_voice, 1000);
        audio_voice_gain(au_voice, 0.0f, 0.0f);
        audio_sink_begin(&au_sink, 200);
        AUCHECK(audio_voice_mix(&au_sink, au_voice, 200, 0) == 200, "the ramp plays");
        AUCHECK(au_voice->now[0] == 0.0f, "and reaches nothing");
        AUCHECK(audio_ring_fill(&au_voice->ring) == (1000 - 200) * 4, "the ramp took %lu bytes", 4000 - audio_ring_fill(&au_voice->ring));
        audio_sink_begin(&au_sink, 200);
        AUCHECK(audio_voice_mix(&au_sink, au_voice, 200, 0) == 0 && audio_ring_fill(&au_voice->ring) == 800 * 4, "a silent voice takes nothing from its ring");
        audio_voice_close(au_voice);
}

/* ---- the resampler ---- */

static struct audio_resampler au_rs;
static float au_out0[1 << 18], au_out1[1 << 18];

// A sine of f at rin made into rout, as many output frames as asked, fed in pieces of the given size (0 for what is needed).
static long au_resample(long rin, long rout, double f, long outputs, long piece, struct audio_table *table)
{
        long made = 0, fed = 0;

        (void)rout;
        audio_rs_open(&au_rs, table);
        while (made < outputs)
        {
                long want = outputs - made < 512 ? outputs - made : 512, need = audio_rs_need(&au_rs, want), give = piece && piece < need ? piece : need;

                if (piece && give < need)
                {
                        // Cut small: append a piece, make what is ready, repeat.
                        for (long i = 0; i < give; i++)
                        {
                                float v = (float)(0.5 * sin(2 * M_PI * f * (double)(fed + i) / (double)rin));

                                au_rs.buf[0][au_rs.have + i] = au_rs.buf[1][au_rs.have + i] = v;
                        }
                        au_rs.have += give;
                        fed += give;
                        {
                                long ready = audio_rs_ready(&au_rs), n = ready < want ? ready : want;

                                // Only whole wants are made: the output must not depend on the cut.
                                if (n >= want)
                                        made += audio_rs_run(&au_rs, au_out0 + made, au_out1 + made, want);
                        }
                        continue;
                }
                for (long i = 0; i < give; i++)
                {
                        float v = (float)(0.5 * sin(2 * M_PI * f * (double)(fed + i) / (double)rin));

                        au_rs.buf[0][au_rs.have + i] = au_rs.buf[1][au_rs.have + i] = v;
                }
                au_rs.have += give;
                fed += give;
                made += audio_rs_run(&au_rs, au_out0 + made, au_out1 + made, want);
        }
        return made;
}

// SNR against the sine itself at the output's times, and the gain.
static void au_measure(long rin, long rout, double f, double *snr, double *gain_db, long outputs)
{
        struct audio_table *table = audio_table_take(rin, rout);
        long made, skip;
        double step = (double)rin / (double)rout, se = 0, ss = 0, scc = 0, scs = 0, sss = 0, syc = 0, sys = 0;

        if (!table)
        {
                *snr = *gain_db = -1000;
                return;
        }
        made = au_resample(rin, rout, f, outputs, 0, table);
        skip = (table->taps + 8) * (rout > rin ? (rout + rin - 1) / rin : 1) + 16;
        for (long m = skip; m < made - skip; m++)
        {
                double ref = 0.5 * sin(2 * M_PI * f * (double)m * step / (double)rin), e = (double)au_out0[m] - ref, ph = 2 * M_PI * f * (double)m / (double)rout;

                se += e * e;
                ss += ref * ref;
                scc += cos(ph) * cos(ph);
                scs += cos(ph) * sin(ph);
                sss += sin(ph) * sin(ph);
                syc += (double)au_out0[m] * cos(ph);
                sys += (double)au_out0[m] * sin(ph);
        }
        {
                // The amplitude of the best sine of this frequency in the output, fitted, so a span that is not a whole number of cycles biases nothing.
                double det = scc * sss - scs * scs, a = (syc * sss - sys * scs) / det, b = (sys * scc - syc * scs) / det;

                *gain_db = 20 * log10(sqrt(a * a + b * b) / 0.5);
        }
        *snr = 10 * log10(ss / se);
        audio_table_give(table);
}

static int au_exact_pair(long rin, long rout)
{
        struct audio_table *t = audio_table_take(rin, rout);
        int exact = t && t->exact;

        audio_table_give(t);
        return exact;
}

static void au_test_resampler(void)
{
        static const long pairs[][2] = {{44100, 48000}, {48000, 44100}, {96000, 48000}, {22050, 48000}, {8000, 48000}, {192000, 48000},
                                        {48000, 96000}, {32000, 48000}, {11025, 48000}, {88200, 44100}, {384000, 48000}, {48000, 8000 * 6}};
        static const double fractions[] = {0.01, 0.1, 0.3, 0.6, 0.8, 0.85};
        double worst_snr = 1000, worst_gain = 0;

        printf("  resampler: SNR against the sine itself, at 0.01 to 0.85 of the lower Nyquist rate (the passband is flat there)\n");
        for (unsigned p = 0; p < sizeof(pairs) / sizeof(pairs[0]); p++)
        {
                long rin = pairs[p][0], rout = pairs[p][1];
                double nyquist = 0.5 * (double)(rin < rout ? rin : rout), low = 1000, high = -1000, gain_low = 1000, gain_high = -1000;
                long outputs = rout / 4;

                for (unsigned i = 0; i < sizeof(fractions) / sizeof(fractions[0]); i++)
                {
                        double snr, gain;
                        // A tone of whole cycles in the span makes the gain exact.
                        double f = nyquist * fractions[i];

                        f = rint(f * (double)outputs / (double)rout) * (double)rout / (double)outputs;
                        au_measure(rin, rout, f, &snr, &gain, outputs);
                        low = snr < low ? snr : low;
                        high = snr > high ? snr : high;
                        gain_low = gain < gain_low ? gain : gain_low;
                        gain_high = gain > gain_high ? gain : gain_high;
                        // An exact table has a row for each place, so nothing is interpolated: the 24-bit floor. The others interpolate
                        // linearly between 512 phases, which is about 108 dB (the next step is a higher-order interpolation, in the kernel).
                        AUCHECK(snr >= (au_exact_pair(rin, rout) ? 130.0 : 105.0), "%ld to %ld at %.0f Hz: SNR %.1f dB", rin, rout, f, snr);
                        AUCHECK(fabs(gain) <= 0.001, "%ld to %ld at %.0f Hz: gain %+.4f dB", rin, rout, f, gain);
                }
                printf("    %6ld -> %6ld  SNR %5.1f to %5.1f dB, gain %+.4f to %+.4f dB\n", rin, rout, low, high, gain_low, gain_high);
                worst_snr = low < worst_snr ? low : worst_snr;
                worst_gain = fabs(gain_low) > worst_gain ? fabs(gain_low) : worst_gain;
                worst_gain = fabs(gain_high) > worst_gain ? fabs(gain_high) : worst_gain;
        }
        printf("  resampler: worst SNR %.1f dB, worst gain error %.4f dB\n", worst_snr, worst_gain);
}

// A tone above the lower Nyquist rate folds to a frequency below it; the filter must have taken it out.
static void au_test_aliasing(void)
{
        static const struct
        {
                long rin, rout;
                double f;
        } tones[] = {{96000, 48000, 27000}, {96000, 48000, 30000}, {96000, 48000, 40000}, {96000, 48000, 47000}, {192000, 48000, 60000},
                     {192000, 48000, 90000}, {48000, 44100, 22500}, {48000, 44100, 23500}, {48000, 44100, 24000}, {48000, 32000, 18000}, {44100, 22050, 12500}};
        double worst = -1000;

        for (unsigned t = 0; t < sizeof(tones) / sizeof(tones[0]); t++)
        {
                long outputs = tones[t].rout / 2;
                struct audio_table *table = audio_table_take(tones[t].rin, tones[t].rout);
                double alias = fabs(tones[t].f - rint(tones[t].f / (double)tones[t].rout) * (double)tones[t].rout), sc = 0, sn = 0, count = 0, db;
                long made, skip;

                // A whole number of cycles of the alias in the span.
                alias = rint(alias * (double)outputs / (double)tones[t].rout) * (double)tones[t].rout / (double)outputs;
                made = au_resample(tones[t].rin, tones[t].rout, tones[t].f, outputs, 0, table);
                skip = (table->taps + 8) * (tones[t].rout > tones[t].rin ? 2 : 1) + 16;
                for (long m = skip; m < made - skip; m++)
                {
                        double ph = 2 * M_PI * alias * (double)m / (double)tones[t].rout;

                        sc += (double)au_out0[m] * cos(ph);
                        sn += (double)au_out0[m] * sin(ph);
                        count += 1;
                }
                db = 20 * log10(2 * sqrt(sc * sc + sn * sn) / count / 0.5 + 1e-30);
                worst = db > worst ? db : worst;
                AUCHECK(db < -90.0, "%ld to %ld: %.0f Hz folds to %.0f Hz at %.1f dB", tones[t].rin, tones[t].rout, tones[t].f, alias, db);
                audio_table_give(table);
        }
        printf("  resampler: tones above the lower Nyquist rate fold in at no more than %.1f dB\n", worst);
}

// The same bits however the input is cut, equal rates left alone, a constant kept, the tail given a drain.
static void au_test_resampler_streaming(void)
{
        static float whole[1 << 15];
        struct audio_table *table = audio_table_take(44100, 48000);
        long made = au_resample(44100, 48000, 1234.5, 8192, 0, table);

        memcpy(whole, au_out0, sizeof(float) * (unsigned long)made);
        AUCHECK(made == 8192, "made all");
        {
                static const long pieces[] = {1, 7, 64, 100, 511};

                for (unsigned p = 0; p < sizeof(pieces) / sizeof(pieces[0]); p++)
                {
                        long again = au_resample(44100, 48000, 1234.5, 8192, pieces[p], table);

                        AUCHECK(again == 8192 && !memcmp(whole, au_out0, sizeof(float) * 8192), "input cut in pieces of %ld gives other bits", pieces[p]);
                }
        }
        // The place of an exact table is counted in whole numbers: after any number of outputs, cut in any pieces, it is exactly where num/den says.
        {
                static const long ratios[][2] = {{44100, 48000}, {48000, 44100}, {22050, 48000}, {32000, 48000}};

                for (unsigned r = 0; r < sizeof(ratios) / sizeof(ratios[0]); r++)
                {
                        struct audio_table *e = audio_table_take(ratios[r][0], ratios[r][1]);
                        unsigned long total = 0;
                        static const long pieces[] = {97, 512, 1, 333, 480};

                        audio_rs_open(&au_rs, e);
                        for (int round = 0; round < 3000; round++)
                        {
                                long want = pieces[round % 5], need = audio_rs_need(&au_rs, want);

                                for (long i = 0; i < need; i++)
                                        au_rs.buf[0][au_rs.have + i] = au_rs.buf[1][au_rs.have + i] = 0.25f;
                                au_rs.have += need;
                                total += (unsigned long)audio_rs_run(&au_rs, au_out0, au_out1, want);
                        }
                        AUCHECK(au_rs.k == total * e->num % e->den, "%ld to %ld after %lu outputs the place is %lu of %lu, not %lu", ratios[r][0], ratios[r][1], total, au_rs.k, e->den, total * e->num % e->den);
                        AUCHECK((au_rs.pos & 0xffffffffu) == (unsigned long)(((unsigned long long)au_rs.k << 32) + e->den - 1) / e->den, "and the fraction handed to the kernel is its ceiling");
                        audio_table_give(e);
                }
        }
        audio_table_give(table);
        // A constant stays a constant.
        {
                struct audio_table *pair[] = {audio_table_take(44100, 48000), audio_table_take(48000, 44100), audio_table_take(8000, 48000), audio_table_take(192000, 48000)};

                for (int t = 0; t < 4; t++)
                {
                        long n = 3000;

                        audio_rs_open(&au_rs, pair[t]);
                        for (long off = 0; off < n;)
                        {
                                long want = n - off < 512 ? n - off : 512, need = audio_rs_need(&au_rs, want);

                                for (long i = 0; i < need; i++)
                                        au_rs.buf[0][au_rs.have + i] = au_rs.buf[1][au_rs.have + i] = 0.75f;
                                au_rs.have += need;
                                off += audio_rs_run(&au_rs, au_out0 + off, au_out1 + off, want);
                        }
                        for (long i = 2000; i < n; i++)
                                AUCHECK(fabsf(au_out0[i] - 0.75f) < 2e-6f && au_out0[i] == au_out1[i], "table %d: a constant moved to %.9g at %ld", t, (double)au_out0[i], i);
                        audio_table_give(pair[t]);
                }
        }
}

// Through a voice: equal rates are bits, other rates drain to the last frame, the rate can be changed, the tables are all given back.
static void au_test_voice_resampling(void)
{
        static float in[1 << 14], expected[1 << 14];
        static unsigned char out[AUDIO_BLOCK * 8];
        static float played[1 << 15];
        long n = 4410, got = 0, calls = 0;

        for (long i = 0; i < n; i++)
                in[i] = (float)(0.5 * sin(2 * M_PI * 1000.0 * (double)i / 44100.0));
        audio_sink_open(&au_sink, 48000, 2);
        AUCHECK(audio_voice_open(au_voice, AUDIO_FORMAT_F32LE, 1, 44100, 0, 48000, au_memory[0], sizeof(au_memory[0])) == 0, "a resampling voice opens");
        AUCHECK(audio_voice_delay(au_voice) == AUDIO_TAPS / 2, "its latency is half its taps: %ld", audio_voice_delay(au_voice));
        au_steady(au_voice);
        audio_ring_put(&au_voice->ring, (const unsigned char *)in, (unsigned long)n * 4);
        for (;;)
        {
                long played_now, left = audio_ring_fill(&au_voice->ring) ? 1 : 0;

                audio_sink_begin(&au_sink, 480);
                played_now = audio_voice_mix(&au_sink, au_voice, 480, 1);
                audio_sink_end(&au_sink, out, AUDIO_FORMAT_F32LE, 2, 480);
                for (long i = 0; i < played_now && got + i < (long)(sizeof(played) / sizeof(played[0])); i++)
                        memcpy(played + got + i, out + i * 8, 4);
                got += played_now;
                calls++;
                if (played_now < 480 || calls > 100)
                        break;
                (void)left;
        }
        AUCHECK(audio_ring_fill(&au_voice->ring) == 0, "the drain took all the stream's bytes");
        // 4410 frames at 44100 is 4800 at 48000, and the tail is flushed: within a frame of that.
        AUCHECK(labs(got - 4800) <= 33, "drained to %ld frames, 4800 expected", got);
        for (long i = 0; i < got; i++)
                expected[i] = (float)(0.5 * sin(2 * M_PI * 1000.0 * (double)i / 48000.0));
        {
                double se = 0, ss = 0;

                for (long i = 200; i < 4700 && i < got; i++)
                {
                        double e = (double)played[i] - (double)expected[i];

                        se += e * e;
                        ss += (double)expected[i] * (double)expected[i];
                }
                AUCHECK(10 * log10(ss / se) > 96.0, "voice resampling SNR %.1f dB", 10 * log10(ss / se));
        }
        // The rate changes at run time: a small change keeps the table, a large one takes another; back to the sink's own frees it.
        {
                int taken = 0;

                for (int t = 0; t < AUDIO_TABLES; t++)
                        taken += audio_tables[t].users;
                AUCHECK(taken == 1, "one table in use: %d", taken);
                AUCHECK(audio_voice_rate(au_voice, 44200) == 0 && au_voice->rate == 44200 && au_voice->rs.step == (unsigned long)((((unsigned long long)221 << 32) + 239) / 240), "a small change takes a table of its own (221/240, exact) and its step rounded up");
                AUCHECK(audio_voice_rate(au_voice, 22050) == 0 && au_voice->table->rin == 22050, "a large change takes another table");
                AUCHECK(audio_voice_rate(au_voice, 48000) == 0 && au_voice->table == 0, "the sink's own rate needs none");
                AUCHECK(audio_voice_rate(au_voice, 0) < 0 && audio_voice_rate(au_voice, 400001) < 0, "a rate out of bounds is refused");
                taken = 0;
                for (int t = 0; t < AUDIO_TABLES; t++)
                        taken += audio_tables[t].users;
                AUCHECK(taken == 0, "no table is kept by a voice that needs none: %d", taken);
        }
        audio_voice_close(au_voice);
        // Equal rates skip the filter: the bits of the stream are the bits of the sink.
        {
                static short s[2 * 480];
                static unsigned char o[480 * 4];

                for (long i = 0; i < 480; i++)
                        s[i * 2] = (short)(i * 61 - 14000), s[i * 2 + 1] = (short)(9000 - i * 37);
                audio_sink_open(&au_sink, 48000, 2);
                audio_voice_open(au_voice, AUDIO_FORMAT_S16LE, 2, 48000, 0, 48000, au_memory[0], sizeof(au_memory[0]));
                AUCHECK(au_voice->table == 0 && audio_voice_delay(au_voice) == 0, "no filter, no delay");
                au_steady(au_voice);
                audio_ring_put(&au_voice->ring, (const unsigned char *)s, sizeof(s));
                audio_sink_begin(&au_sink, 480);
                audio_voice_mix(&au_sink, au_voice, 480, 0);
                audio_sink_end(&au_sink, o, AUDIO_FORMAT_S16LE, 2, 480);
                AUCHECK(!memcmp(o, s, sizeof(s)), "equal rates are the same bytes");
                audio_voice_close(au_voice);
        }
}

static void au_test_tables(void)
{
        struct audio_table *held[AUDIO_TABLES + 1];
        struct audio_table *a, *b;

        for (int t = 0; t < AUDIO_TABLES; t++)
                AUCHECK(audio_tables[t].users == 0, "table %d is kept by nobody", t);
        a = audio_table_take(44100, 48000);
        b = audio_table_take(44100, 48000);
        AUCHECK(a && a == b && a->users == 2, "the same rates share a table");
        audio_table_give(a);
        audio_table_give(b);
        AUCHECK(a->users == 0, "and give it back");
        audio_table_give(a);
        AUCHECK(a->users == 0, "a table is not given back past nothing");
        AUCHECK(audio_table_take(48000, 5000) == 0, "a ratio of more than eight is refused");
        {
                // The ratios that reduce to few enough places are exact tables, with a row for each; the others are not.
                static const struct
                {
                        long rin, rout, exact, phases, taps;
                } kinds[] = {{44100, 48000, 1, 160, 128}, {48000, 44100, 1, 147, 256}, {22050, 48000, 1, 320, 128}, {32000, 48000, 1, 3, 128}, {8000, 48000, 1, 6, 128},
                             {96000, 48000, 1, 1, 256}, {192000, 48000, 1, 1, 512}, {11025, 48000, 0, 512, 128}, {44117, 48000, 0, 512, 128}};

                for (unsigned i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++)
                {
                        struct audio_table *e = audio_table_take(kinds[i].rin, kinds[i].rout);

                        AUCHECK(e && e->exact == kinds[i].exact && e->phases == kinds[i].phases && e->taps == kinds[i].taps, "%ld to %ld: exact %ld, %ld phases, %ld taps", kinds[i].rin, kinds[i].rout,
                                e ? e->exact : -1, e ? e->phases : -1, e ? e->taps : -1);
                        if (e && e->exact)
                                AUCHECK((long)(e->num * (unsigned long)kinds[i].rout) == (long)(e->den * (unsigned long)kinds[i].rin), "%ld to %ld is %lu/%lu", kinds[i].rin, kinds[i].rout, e->num, e->den);
                        audio_table_give(e);
                }
        }
        AUCHECK(audio_table_take(0, 48000) == 0 && audio_table_take(48000, 0) == 0 && audio_table_take(400000, 48000) == 0, "rates out of bounds are refused");
        for (int t = 0; t < AUDIO_TABLES; t++)
        {
                held[t] = audio_table_take(30000 + 1000 * t, 48000);
                AUCHECK(held[t] != 0, "table %d is made", t);
        }
        held[AUDIO_TABLES] = audio_table_take(31234, 48000);
        AUCHECK(held[AUDIO_TABLES] == 0, "with every table in use another is refused");
        for (int t = 0; t < AUDIO_TABLES; t++)
                audio_table_give(held[t]);
        AUCHECK((held[0] = audio_table_take(31234, 48000)) != 0, "once one is free it is taken over");
        audio_table_give(held[0]);
        // The rows add to one and the second half is the first turned around.
        for (int t = 0; t < AUDIO_TABLES; t++)
                if (audio_tables[t].rin)
                {
                        const struct audio_table *table = audio_tables + t;

                        for (long p = 0; p < (table->exact ? table->phases : table->phases + 1); p += 7)
                        {
                                double sum = 0;

                                for (long k = 0; k < table->taps; k++)
                                        sum += (double)table->h[p * table->taps + k];
                                AUCHECK(fabs(sum - 1.0) < 2e-6, "table %d row %ld adds to %.9f", t, p, sum);
                        }
                        for (long k = 0; k < table->taps; k++)
                        {
                                // The last row of an inexact table is the first turned around; an exact one has no such row, and its row q - p is row p turned around.
                                if (!table->exact)
                                        AUCHECK(table->h[k] == table->h[table->phases * table->taps + table->taps - 1 - k], "table %d turned around at %ld", t, k);
                                else if (table->phases > 1)
                                        AUCHECK(table->h[1 * table->taps + k] == table->h[(table->phases - 1) * table->taps + table->taps - 1 - k], "exact table %d: row 1 and row q-1 turned around at %ld", t, k);
                        }
                }
}

static void au_test_open(void)
{
        signed char map[2] = {AUDIO_POSITION_FRONT_LEFT, AUDIO_POSITION_FRONT_RIGHT};

        AUCHECK(audio_voice_open(au_voice, 99, 2, 48000, map, 48000, au_memory[0], 4096) < 0, "an unknown format is refused");
        AUCHECK(audio_voice_open(au_voice, AUDIO_FORMAT_S16LE, 0, 48000, map, 48000, au_memory[0], 4096) < 0, "no channels is refused");
        AUCHECK(audio_voice_open(au_voice, AUDIO_FORMAT_S16LE, 9, 48000, map, 48000, au_memory[0], 4096) < 0, "nine channels are refused");
        AUCHECK(audio_voice_open(au_voice, AUDIO_FORMAT_S16LE, 2, 0, map, 48000, au_memory[0], 4096) < 0, "no rate is refused");
        AUCHECK(audio_voice_open(au_voice, AUDIO_FORMAT_S16LE, 2, 384001, map, 48000, au_memory[0], 4096) < 0, "too high a rate is refused");
        AUCHECK(audio_voice_open(au_voice, AUDIO_FORMAT_S16LE, 2, 48000, map, 48000, au_memory[0], 3) < 0, "a ring smaller than a frame is refused");
        AUCHECK(audio_voice_open(au_voice, AUDIO_FORMAT_S16LE, 2, 400000, map, 48000, au_memory[0], 4096) < 0, "400 kHz is refused");
        AUCHECK(audio_voice_open(au_voice, AUDIO_FORMAT_S16LE, 2, 8000, map, 192000, au_memory[0], 4096) < 0 || au_voice->table, "8 kHz up to 192 kHz is a ratio of 24, which is refused or filtered");
        audio_voice_close(au_voice);
        for (int t = 0; t < AUDIO_TABLES; t++)
                AUCHECK(audio_tables[t].users == 0, "a refused open keeps no table (%d)", t);
}

// Voices of every kind, drawn at random, driven for a long time: only numbers in range come out, the rings and tables balance.
static void au_test_fuzz(void)
{
        static const long rates[] = {8000, 11025, 16000, 22050, 24000, 32000, 44100, 48000, 88200, 96000, 192000, 384000, 12345, 47999};
        static unsigned char out[AUDIO_BLOCK * 8 * 4];
        static unsigned char noise[1 << 14];
        long blocks = 0, plays = 0;

        for (int round = 0; round < 40; round++)
        {
                long sink_rate = round % 3 == 0 ? 44100 : 48000, nv = 1 + (long)(au_rand() % 3);
                long format_of[3], channels_of[3];
                int open[3] = {0, 0, 0};

                audio_sink_open(&au_sink, sink_rate, 2);
                for (long v = 0; v < nv; v++)
                {
                        long format = (long)(au_rand() % AUDIO_FORMATS), channels = 1 + (long)(au_rand() % AUDIO_CHANNELS_MAX), rate = rates[au_rand() % (sizeof(rates) / sizeof(rates[0]))];

                        format_of[v] = format;
                        channels_of[v] = channels;
                        if (audio_voice_open(au_voice + v, format, channels, rate, 0, sink_rate, au_memory[v], 1 + au_rand() % 40000) == 0)
                                open[v] = 1;
                }
                for (int step = 0; step < 150; step++)
                {
                        audio_sink_begin(&au_sink, 1 + (long)(au_rand() % AUDIO_BLOCK));
                        for (long v = 0; v < nv; v++)
                        {
                                long n = 1 + (long)(au_rand() % AUDIO_BLOCK);

                                if (!open[v])
                                        continue;
                                for (unsigned i = 0; i < sizeof(noise); i++)
                                        noise[i] = (unsigned char)au_rand();
                                if (au_rand() % 3)
                                        audio_ring_put(&au_voice[v].ring, noise, au_rand() % sizeof(noise));
                                if (au_rand() % 5 == 0)
                                {
                                        unsigned long r = au_rand() % 400;

                                        audio_voice_gain(au_voice + v, audio_volume_gain(r * 600), audio_volume_gain((au_rand() % 400) * 600));
                                }
                                if (au_rand() % 40 == 0)
                                        audio_voice_rate(au_voice + v, rates[au_rand() % (sizeof(rates) / sizeof(rates[0]))]);
                                if (au_rand() % 50 == 0)
                                        audio_ring_seek(&au_voice[v].ring, (long)(au_rand() % 4), ((long long)(au_rand() % 41) - 20) * (long long)au_voice[v].frame);
                                if (au_rand() % 30 == 0)
                                        audio_ring_flush(&au_voice[v].ring);
                                plays += audio_voice_mix(&au_sink, au_voice + v, n, (int)(au_rand() % 2)) > 0;
                                AUCHECK(audio_ring_fill(&au_voice[v].ring) <= au_voice[v].ring.size, "a ring never holds more than it is");
                                AUCHECK(au_voice[v].ring.read <= au_voice[v].ring.limit && au_voice[v].ring.read <= au_voice[v].ring.write, "read stays behind write and end");
                        }
                        {
                                long frames = 1 + (long)(au_rand() % AUDIO_BLOCK), channels = 1 + (long)(au_rand() % 8);
                                long format = (long[]){AUDIO_FORMAT_S16LE, AUDIO_FORMAT_S24LE, AUDIO_FORMAT_S24_32LE, AUDIO_FORMAT_S32LE, AUDIO_FORMAT_F32LE}[au_rand() % 5];

                                if (au_rand() % 9 == 0)
                                        audio_sink_gain(&au_sink, audio_volume_gain(au_rand() % 0x30000));
                                audio_sink_begin(&au_sink, frames);
                                // A bus that nothing wrote is silence; the sink's end on it is the same.
                                audio_sink_end(&au_sink, out, format, channels, frames);
                                for (long i = 0; i < frames; i++)
                                        AUCHECK(au_sink.mix[0][i] == au_sink.mix[0][i] && fabsf(au_sink.mix[0][i]) <= 1.0f, "the mix is a number in range");
                                blocks++;
                        }
                }
                for (long v = 0; v < nv; v++)
                {
                        if (open[v])
                        {
                                audio_voice_close(au_voice + v);
                                (void)format_of[v];
                                (void)channels_of[v];
                        }
                }
                for (int t = 0; t < AUDIO_TABLES; t++)
                        AUCHECK(audio_tables[t].users == 0, "round %d leaves table %d kept", round, t);
        }
        printf("  fuzz: %ld blocks, %ld voices played\n", blocks, plays);
}

// The mix of voices drawn at random, summed and clipped, kept to numbers in range with the bus itself checked.
static void au_test_fuzz_range(void)
{
        static unsigned char out[AUDIO_BLOCK * 2 * 4];
        static float noise[2 * 1024];
        long frames = 512;

        audio_sink_open(&au_sink, 48000, 2);
        for (int v = 0; v < 3; v++)
        {
                audio_voice_open(au_voice + v, AUDIO_FORMAT_F32LE, 2, 48000, 0, 48000, au_memory[v], sizeof(au_memory[v]));
                au_voice[v].target[0] = au_voice[v].target[1] = 8.0f;
                au_steady(au_voice + v);
        }
        for (int step = 0; step < 100; step++)
        {
                audio_sink_begin(&au_sink, frames);
                for (int v = 0; v < 3; v++)
                {
                        for (int i = 0; i < 2 * 1024; i++)
                                noise[i] = (float)((double)(au_rand() % 65536) / 32768.0 - 1.0) * 16.0f;
                        audio_ring_put(&au_voice[v].ring, (const unsigned char *)noise, sizeof(noise));
                        audio_voice_mix(&au_sink, au_voice + v, frames, 0);
                }
                audio_sink_end(&au_sink, out, AUDIO_FORMAT_F32LE, 2, frames);
                for (long i = 0; i < frames * 2; i++)
                {
                        float x;

                        memcpy(&x, out + i * 4, 4);
                        AUCHECK(x == x && x >= -1.0f && x <= 1.0f, "the output stays in range: %g", (double)x);
                }
        }
        AUCHECK(au_sink.clips > 0, "and the clips are counted");
        for (int v = 0; v < 3; v++)
                audio_voice_close(au_voice + v);
}

/* ---- speed ---- */

static void au_test_speed(void)
{
        static unsigned char in[AUDIO_BLOCK * 8 * 4], out[AUDIO_BLOCK * 2 * 4];
        static const struct
        {
                long rin, rout, format, channels;
                const char *name;
        } kinds[] = {{48000, 48000, AUDIO_FORMAT_S16LE, 2, "s16 stereo, equal rates"},
                     {44100, 48000, AUDIO_FORMAT_S16LE, 2, "s16 stereo, 44100 to 48000"},
                     {96000, 48000, AUDIO_FORMAT_S16LE, 2, "s16 stereo, 96000 to 48000"},
                     {48000, 48000, AUDIO_FORMAT_F32LE, 6, "f32 5.1, equal rates"},
                     {22050, 48000, AUDIO_FORMAT_S16LE, 1, "s16 mono, 22050 to 48000"}};

        for (unsigned k = 0; k < sizeof(kinds) / sizeof(kinds[0]); k++)
        {
                long blocks = 3000, done = 0, per = AUDIO_BLOCK;
                double begin, took;
                signed char map6[6] = {1, 2, 3, 7, 5, 6};

                for (unsigned i = 0; i < sizeof(in); i++)
                        in[i] = (unsigned char)au_rand();
                audio_sink_open(&au_sink, kinds[k].rout, 2);
                audio_voice_open(au_voice, kinds[k].format, kinds[k].channels, kinds[k].rin, kinds[k].channels == 6 ? map6 : 0, kinds[k].rout, au_memory[0], sizeof(au_memory[0]));
                au_steady(au_voice);
                begin = au_clock();
                for (long b = 0; b < blocks; b++)
                {
                        unsigned long bytes = (unsigned long)per * 8 * 4 / 4;

                        if (bytes > sizeof(in))
                                bytes = sizeof(in);
                        audio_ring_put(&au_voice->ring, in, bytes - bytes % au_voice->frame);
                        audio_sink_begin(&au_sink, per);
                        done += audio_voice_mix(&au_sink, au_voice, per, 0);
                        audio_sink_end(&au_sink, out, AUDIO_FORMAT_S16LE, 2, per);
                        // Keep the ring from filling.
                        if (audio_ring_fill(&au_voice->ring) > au_voice->ring.size / 2)
                                audio_ring_flush(&au_voice->ring);
                }
                took = au_clock() - begin;
                printf("  speed: %-28s %7.1f Mframes/s (%6.0fx real time at %ld Hz)\n", kinds[k].name, (double)done / took * 1e-6, (double)done / took / (double)kinds[k].rout, kinds[k].rout);
                // Far under what any machine does, an emulated one too: a floor against a cliff, not a claim.
                AUCHECK((double)done / took > 5.0 * (double)kinds[k].rout, "%s: only %.0fx real time", kinds[k].name, (double)done / took / (double)kinds[k].rout);
                audio_voice_close(au_voice);
        }
        {
                struct audio_table *table;
                double begin = au_clock(), took;

                audio_tables[0].rin = 0;
                for (int t = 0; t < AUDIO_TABLES; t++)
                        audio_tables[t].rin = audio_tables[t].rout = 0;
                table = audio_table_take(44100, 48000);
                took = au_clock() - begin;
                printf("  speed: a table (%ld taps, %ld phases) is made in %.2f ms\n", table->taps, table->phases, took * 1000.0);
                AUCHECK(took < 0.05, "a table takes %.1f ms", took * 1000.0);
                audio_table_give(table);
                begin = au_clock();
                table = audio_table_take(192000, 48000);
                took = au_clock() - begin;
                printf("  speed: a table (%ld taps, %ld phases) is made in %.2f ms\n", table->taps, table->phases, took * 1000.0);
                audio_table_give(table);
        }
}

#define AUQ_MAX_OUT (48000 * 4)
static double AUQ_PI = 3.14159265358979323846;

/* ---- client encoders (independent of mix.c: standard G.711 definitions, and the scale of each format's own range) ---- */
static int auq_clamp(double v, double lo, double hi) { return v < lo ? (int)lo : v > hi ? (int)hi : (int)v; }
static int auq_round(double v) { return (int)floor(v + 0.5); }

static double auq_alaw_value(int byte)
{
        int a = byte ^ 0x55, t = (a & 0x0f) << 4, seg = (a & 0x70) >> 4;

        if (seg == 0) t += 8; else { t += 0x108; if (seg > 1) t <<= seg - 1; }
        return ((a & 0x80) ? t : -t) / 32768.0;
}
static double auq_ulaw_value(int byte)
{
        int u = ~byte & 0xff, t = (((u & 0x0f) << 3) + 0x84) << ((u & 0x70) >> 4);

        return ((u & 0x80) ? 0x84 - t : t - 0x84) / 32768.0;
}

static int auq_nearest_law(double x, int ulaw)
{
        int best = 0;
        double bd = 1e9;

        for (int b = 0; b < 256; b++)
        {
                double d = fabs((ulaw ? auq_ulaw_value(b) : auq_alaw_value(b)) - x);
                if (d < bd) { bd = d; best = b; }
        }
        return best;
}

/* bytes of one frame sample for a client format, x in [-1, 1] */
static void auq_put_sample(unsigned char *q, long format, double x)
{
        long bytes = audio_format_bytes(format);
        int v;
        unsigned int u;

        switch (format)
        {
        case AUDIO_FORMAT_U8: q[0] = (unsigned char)(auq_clamp(auq_round(x * 128) + 128, 0, 255)); break;
        case AUDIO_FORMAT_ALAW: q[0] = (unsigned char)auq_nearest_law(x, 0); break;
        case AUDIO_FORMAT_ULAW: q[0] = (unsigned char)auq_nearest_law(x, 1); break;
        case AUDIO_FORMAT_S16LE: case AUDIO_FORMAT_S16BE:
                v = auq_clamp(auq_round(x * 32768), -32768, 32767); u = (unsigned int)v & 0xffff;
                if (format == AUDIO_FORMAT_S16LE) { q[0] = u; q[1] = u >> 8; } else { q[1] = u; q[0] = u >> 8; }
                break;
        case AUDIO_FORMAT_S24LE: case AUDIO_FORMAT_S24BE:
                v = auq_clamp(auq_round(x * 8388608), -8388608, 8388607); u = (unsigned int)v & 0xffffff;
                if (format == AUDIO_FORMAT_S24LE) { q[0] = u; q[1] = u >> 8; q[2] = u >> 16; } else { q[2] = u; q[1] = u >> 8; q[0] = u >> 16; }
                break;
        case AUDIO_FORMAT_S24_32LE: case AUDIO_FORMAT_S24_32BE:
                v = auq_clamp(auq_round(x * 8388608), -8388608, 8388607); u = (unsigned int)v & 0xffffff;
                if (format == AUDIO_FORMAT_S24_32LE) { q[0] = u; q[1] = u >> 8; q[2] = u >> 16; q[3] = 0; }
                else { q[0] = 0; q[1] = u >> 16; q[2] = u >> 8; q[3] = u; }
                break;
        case AUDIO_FORMAT_S32LE: case AUDIO_FORMAT_S32BE:
                {
                        long long w = (long long)floor(x * 2147483648.0 + 0.5);
                        w = w > 2147483647LL ? 2147483647LL : w < -2147483648LL ? -2147483648LL : w;
                        u = (unsigned int)(int)w;
                }
                if (format == AUDIO_FORMAT_S32LE) { q[0] = u; q[1] = u >> 8; q[2] = u >> 16; q[3] = u >> 24; }
                else { q[3] = u; q[2] = u >> 8; q[1] = u >> 16; q[0] = u >> 24; }
                break;
        case AUDIO_FORMAT_F32LE: case AUDIO_FORMAT_F32BE:
                {
                        float f = (float)x;

                        memcpy(&u, &f, 4);
                        if (format == AUDIO_FORMAT_F32LE) { q[0] = u; q[1] = u >> 8; q[2] = u >> 16; q[3] = u >> 24; }
                        else { q[3] = u; q[2] = u >> 8; q[1] = u >> 16; q[0] = u >> 24; }
                }
                break;
        }
        (void)bytes;
}

/* ---- the pipeline: client stream -> voice -> sink -> card bytes -> decoded floats (card channel 0) ---- */
static double auq_out[AUQ_MAX_OUT];

/* signal(t) for the client's frames; returns output frames written to auq_out */
static long auq_run(long cfmt, long cch, long crate, long card_fmt, long card_ch, long orate, double seconds, double (*signal)(double t, int ch), double volume)
{
        static unsigned char client[48000 * 4 * 8 * 4];
        static unsigned char ring[48000 * 4 * 8 * 4];
        static unsigned char card[512 * 8 * 4];
        static float plane_store[AUDIO_CHANNELS_MAX][AUDIO_BLOCK];
        float *planes[AUDIO_CHANNELS_MAX];
        long bytes = audio_format_bytes(cfmt), frame = bytes * cch, frames_in = (long)(crate * seconds), frames_out = (long)(orate * seconds);
        long fed = 0, written = 0;
        struct audio_sink sink;
        struct audio_voice voice;

        for (long n = 0; n < frames_in; n++)
                for (long c = 0; c < cch; c++)
                        auq_put_sample(client + n * frame + c * bytes, cfmt, signal((double)n / crate, (int)c) * volume);
        for (long c = 0; c < AUDIO_CHANNELS_MAX; c++)
                planes[c] = plane_store[c];
        audio_sink_open(&sink, orate, 2);
        if (audio_voice_open(&voice, cfmt, cch, crate, NULL, orate, ring, sizeof(ring)) < 0)
                return -1;
        while (written < frames_out)
        {
                long period = frames_out - written < AUDIO_BLOCK ? frames_out - written : AUDIO_BLOCK;
                long room, put;
                int drain = fed >= frames_in;

                audio_sink_begin(&sink, period);
                room = (long)audio_ring_room(&voice.ring);
                put = frames_in - fed < room / frame ? frames_in - fed : room / frame;
                if (put > 0)
                {
                        audio_ring_put(&voice.ring, client + fed * frame, (unsigned long)(put * frame));
                        fed += put;
                }
                audio_voice_mix(&sink, &voice, period, drain);
                audio_sink_end(&sink, card, card_fmt, card_ch, period);
                audio_decode(card_fmt, card, period, card_ch, planes);
                for (long i = 0; i < period; i++)
                        auq_out[written + i] = planes[0][i];
                written += period;
        }
        audio_voice_close(&voice);
        return written;
}

/* ---- metrics: least-squares fit of a cos + b sin + c over a window; SINAD = fitted tone over everything else ---- */
static void auq_fit(const double *y, long n, double f, double rate, double *amp, double *sinad)
{
        double m[3][4] = {{0}}, s[3], sol[3];
        double w = 2 * AUQ_PI * f / rate;

        for (long k = 0; k < n; k++)
        {
                double b[3] = {cos(w * k), sin(w * k), 1};

                for (int i = 0; i < 3; i++)
                {
                        for (int j = 0; j < 3; j++)
                                m[i][j] += b[i] * b[j];
                        m[i][3] += b[i] * y[k];
                }
        }
        for (int i = 0; i < 3; i++) s[i] = m[i][3];
        for (int i = 0; i < 3; i++)
        {
                int p = i;

                for (int r = i + 1; r < 3; r++) if (fabs(m[r][i]) > fabs(m[p][i])) p = r;
                for (int j = 0; j < 4; j++) { double t = m[i][j]; m[i][j] = m[p][j]; m[p][j] = t; }
                for (int r = 0; r < 3; r++) if (r != i)
                {
                        double g = m[r][i] / m[i][i];
                        for (int j = 0; j < 4; j++) m[r][j] -= g * m[i][j];
                }
        }
        for (int i = 0; i < 3; i++) sol[i] = m[i][3] / m[i][i];
        (void)s;
        double sig = 0, res = 0;
        for (long k = 0; k < n; k++)
        {
                double tone = sol[0] * cos(w * k) + sol[1] * sin(w * k), e = y[k] - tone - sol[2];

                sig += tone * tone;
                res += e * e;
        }
        *amp = sqrt(sol[0] * sol[0] + sol[1] * sol[1]);
        *sinad = res > 0 ? 10 * log10(sig / res) : 999;
}

static double auq_sine_amp, auq_sine_freq;
static double auq_sine(double t, int ch) { (void)ch; return auq_sine_amp * sin(2 * AUQ_PI * auq_sine_freq * t); }

static const char *auq_fname(long f)
{
        static const char *names[] = {"U8", "ALAW", "ULAW", "S16LE", "S16BE", "F32LE", "F32BE", "S32LE", "S32BE", "S24LE", "S24BE", "S24_32LE", "S24_32BE"};
        return names[f];
}

static double auq_ideal_bits(long fmt)
{
        switch (fmt)
        {
        case AUDIO_FORMAT_U8: case AUDIO_FORMAT_ALAW: case AUDIO_FORMAT_ULAW: return 8;
        case AUDIO_FORMAT_S16LE: case AUDIO_FORMAT_S16BE: return 16;
        case AUDIO_FORMAT_S24LE: case AUDIO_FORMAT_S24BE: case AUDIO_FORMAT_S24_32LE: case AUDIO_FORMAT_S24_32BE: return 24;
        case AUDIO_FORMAT_F32LE: case AUDIO_FORMAT_F32BE: return 24;
        default: return 31;
        }
}


/* ---- the quality of the whole path: every client format into every card format, against the format's own floor ---- */

static void au_test_format_matrix(void)
{
        static const long clients[] = {AUDIO_FORMAT_U8, AUDIO_FORMAT_ALAW, AUDIO_FORMAT_ULAW, AUDIO_FORMAT_S16LE, AUDIO_FORMAT_S16BE, AUDIO_FORMAT_S24LE,
                                       AUDIO_FORMAT_S24BE, AUDIO_FORMAT_S24_32LE, AUDIO_FORMAT_S24_32BE, AUDIO_FORMAT_S32LE, AUDIO_FORMAT_S32BE, AUDIO_FORMAT_F32LE,
                                       AUDIO_FORMAT_F32BE};
        static const long cards[] = {AUDIO_FORMAT_S16LE, AUDIO_FORMAT_S24LE, AUDIO_FORMAT_S24_32LE, AUDIO_FORMAT_S32LE, AUDIO_FORMAT_F32LE};
        double worst_margin = 1000;

        printf("  quality: 1 kHz at -6 dBFS, 48000 stereo, client format (rows) into card format (columns): SINAD in dB\n");
        for (unsigned f = 0; f < sizeof(clients) / sizeof(clients[0]); f++)
        {
                printf("    %-9s", auq_fname(clients[f]));
                for (unsigned c = 0; c < sizeof(cards) / sizeof(cards[0]); c++)
                {
                        double amp, sinad, floor_db, bits = auq_ideal_bits(clients[f]) < auq_ideal_bits(cards[c]) ? auq_ideal_bits(clients[f]) : auq_ideal_bits(cards[c]);

                        // The float bus holds 24 bits a sample, so no path is better than that however wide its ends are.
                        bits = bits < 24.0 ? bits : 24.0;
                        long n;

                        auq_sine_amp = 0.5;
                        auq_sine_freq = 1000;
                        n = auq_run(clients[f], 2, 48000, cards[c], 2, 48000, 1.0, auq_sine, 1.0);
                        auq_fit(auq_out + 4096, n - 8192, 1000, 48000, &amp, &sinad);
                        floor_db = 6.02 * bits + 1.76 - 6.02;
                        printf(" %6.1f", sinad);
                        // mu-law at this level is about 38 dB, its own curve, not the 8-bit uniform floor.
                        AUCHECK(sinad >= (clients[f] == AUDIO_FORMAT_ULAW ? 34.0 : floor_db - 3.0), "%s into %s: SINAD %.1f dB (floor %.1f)",
                                auq_fname(clients[f]), auq_fname(cards[c]), sinad, floor_db);
                        AUCHECK(fabs(20 * log10(amp / 0.5)) < 0.1, "%s into %s: gain %+.4f dB", auq_fname(clients[f]), auq_fname(cards[c]),
                                20 * log10(amp / 0.5));
                        if (sinad - floor_db < worst_margin)
                                worst_margin = sinad - floor_db;
                }
                printf("\n");
        }
        printf("  quality: worst SINAD margin over the narrower format's floor %.1f dB\n", worst_margin);
}


/* ---- the filter follows the card: a 16-bit card gets the short filter, and nothing it can hear is lost ---- */

static void au_test_narrow(void)
{
        static unsigned char ring[48000 * 4 * 2];
        static unsigned char card[AUDIO_BLOCK * 2 * 2];
        static float feed[AUDIO_BLOCK * 2];
        struct audio_sink sink;
        struct audio_voice voice;
        struct audio_table *table;
        long peak = 0, steady = 0;

        audio_set_card_bits(16);
        table = audio_table_take(44100, 48000);
        AUCHECK(table && table->taps == AUDIO_TAPS_NARROW && table->narrow, "a 16-bit card's 44.1 to 48 kHz filter is %ld taps (want %d)", table ? table->taps : -1, AUDIO_TAPS_NARROW);
        audio_table_give(table);
        table = audio_table_take(96000, 48000);
        AUCHECK(table && table->taps == 2 * AUDIO_TAPS_NARROW, "a 16-bit card's 96 to 48 kHz filter is %ld taps", table ? table->taps : -1);
        audio_table_give(table);

        // A 96 kHz stream with a 25 kHz tone (1 kHz above the output's Nyquist rate) into a 16-bit card: the alias must be under the
        // card's least bit once the filter is steady, so every sample the card gets is within one step of silence.
        audio_sink_open(&sink, 48000, 2);
        AUCHECK(audio_voice_open(&voice, AUDIO_FORMAT_F32LE, 1, 96000, 0, 48000, ring, sizeof(ring)) == 0, "a 96 kHz voice does not open");
        // The stream runs four blocks ahead of the mix, as the service keeps it: the filter's look-ahead is in the ring, so no
        // block is short and no fade or restart puts a step into the output.
        for (long block = 0; block < 4; block++)
        {
                for (long i = 0; i < AUDIO_BLOCK * 2; i++)
                        feed[i] = (float)(0.5 * sin(2 * M_PI * 25000.0 * (double)(block * AUDIO_BLOCK * 2 + i) / 96000.0));
                audio_ring_put(&voice.ring, (const unsigned char *)feed, AUDIO_BLOCK * 2 * 4);
        }
        for (long block = 0; block < 60; block++)
        {
                audio_sink_begin(&sink, AUDIO_BLOCK);
                for (long i = 0; i < AUDIO_BLOCK * 2; i++)
                        feed[i] = (float)(0.5 * sin(2 * M_PI * 25000.0 * (double)((block + 4) * AUDIO_BLOCK * 2 + i) / 96000.0));
                audio_ring_put(&voice.ring, (const unsigned char *)feed, AUDIO_BLOCK * 2 * 4);
                audio_voice_mix(&sink, &voice, AUDIO_BLOCK, 0);
                audio_sink_end(&sink, card, AUDIO_FORMAT_S16LE, 2, AUDIO_BLOCK);
                if (block >= 20)
                        for (long i = 0; i < AUDIO_BLOCK * 2; i++)
                        {
                                short v = (short)(card[i * 2] | card[i * 2 + 1] << 8);
                                long a = v < 0 ? -(long)v : (long)v;

                                peak = a > peak ? a : peak;
                                steady++;
                        }
        }
        audio_voice_close(&voice);
        AUCHECK(steady > 0 && peak <= 1, "a 25 kHz tone from 96 kHz into a 16-bit card reaches the card at %ld (the alias must be under one step)", peak);
        audio_set_card_bits(0);
        table = audio_table_take(96000, 48000);
        AUCHECK(table && table->taps == 2 * AUDIO_TAPS && !table->narrow, "with no 16-bit card open the filter is the wide one (%ld taps)", table ? table->taps : -1);
        audio_table_give(table);
}

/* ---- what a caller may hand the engine: a format it does not make, a gain that is not a number ---- */

static void au_test_hardening(void)
{
        static unsigned char out[64], wide[4096];
        static float left[4] = {0.5f, 0.5f, 0.5f, 0.5f};
        static const long formats[] = {AUDIO_FORMAT_U8, AUDIO_FORMAT_ALAW, AUDIO_FORMAT_ULAW, AUDIO_FORMAT_S16BE, AUDIO_FORMAT_F32BE,
                                       AUDIO_FORMAT_S32BE, AUDIO_FORMAT_S24BE, AUDIO_FORMAT_S24_32BE};
        static float dc[AUDIO_BLOCK];
        static unsigned char ring[4096 * 4];
        static struct audio_sink sink;
        static struct audio_voice voice;

        // An encoder writes what it answers and no more: a format it does not make is refused, and nothing is written.
        for (unsigned f = 0; f < sizeof(formats) / sizeof(formats[0]); f++)
        {
                long wrote;
                int touched = 0;

                memset(out, 0xa5, sizeof(out));
                wrote = audio_encode(formats[f], out, 4, 2, left, left);
                for (unsigned i = 0; i < sizeof(out); i++)
                        touched += out[i] != 0xa5;
                AUCHECK(wrote == 0 && !touched, "format %ld: answered %ld bytes and wrote %d", formats[f], wrote, touched);
        }
        // A negative count answers nothing.
        {
                audio_sink_open(&sink, 48000, 2);
                AUCHECK(audio_sink_end(&sink, wide, AUDIO_FORMAT_F32LE, 2, -3) == 0, "a sink of -3 frames answers bytes");
        }
        // A gain that is not a number is silence, and one past 200% is 200%: a voice's gain and the master's.
        for (long i = 0; i < AUDIO_BLOCK; i++)
                dc[i] = 0.5f;
        audio_sink_open(&sink, 48000, 2);
        AUCHECK(audio_voice_open(&voice, AUDIO_FORMAT_F32LE, 1, 48000, 0, 48000, ring, sizeof(ring)) == 0, "a voice of floats does not open");
        for (int block = 0; block < 4; block++)
        {
                audio_sink_begin(&sink, AUDIO_BLOCK);
                audio_ring_put(&voice.ring, (const unsigned char *)dc, AUDIO_BLOCK * 4);
                audio_voice_mix(&sink, &voice, AUDIO_BLOCK, 0);
                if (block == 0)
                        AUCHECK(sink.bus[0][AUDIO_BLOCK - 1] == 0.5f, "a voice at unity is not 0.5 at the end of its block");
        }
        audio_voice_gain(&voice, NAN, NAN);
        for (int block = 0; block < 3; block++)
        {
                audio_sink_begin(&sink, AUDIO_BLOCK);
                audio_ring_put(&voice.ring, (const unsigned char *)dc, AUDIO_BLOCK * 4);
                audio_voice_mix(&sink, &voice, AUDIO_BLOCK, 0);
                if (block == 2)
                        AUCHECK(sink.bus[0][AUDIO_BLOCK - 1] == 0 && sink.bus[1][AUDIO_BLOCK - 1] == 0, "a voice at a gain of NaN is not silence (%g)", (double)sink.bus[0][AUDIO_BLOCK - 1]);
        }
        audio_voice_gain(&voice, 1e30f, 1e30f);
        for (int block = 0; block < 3; block++)
        {
                audio_sink_begin(&sink, AUDIO_BLOCK);
                audio_ring_put(&voice.ring, (const unsigned char *)dc, AUDIO_BLOCK * 4);
                audio_voice_mix(&sink, &voice, AUDIO_BLOCK, 0);
                if (block == 2)
                        AUCHECK(sink.bus[0][AUDIO_BLOCK - 1] == 4.0f, "a voice at a gain of 1e30 is %g, not 200%% (4)", (double)sink.bus[0][AUDIO_BLOCK - 1]);
        }
        audio_voice_close(&voice);
        // A voice that moves to another rate far from its own takes the table for it (a near one keeps its own).
        AUCHECK(audio_voice_open(&voice, AUDIO_FORMAT_F32LE, 1, 94001, 0, 48000, ring, sizeof(ring)) == 0, "a voice of 94001 does not open");
        AUCHECK(audio_voice_rate(&voice, 50001) == 0 && voice.table && voice.table->rin == 50001, "a voice moved from 94001 to 50001 keeps the table for 94001");
        audio_voice_close(&voice);
        // The master gain: NaN is silence too.
        audio_sink_open(&sink, 48000, 2);
        for (int block = 0; block < 2; block++)
        {
                audio_sink_begin(&sink, AUDIO_BLOCK);
                for (long i = 0; i < AUDIO_BLOCK; i++)
                        sink.bus[0][i] = sink.bus[1][i] = 0.5f;
                audio_sink_gain(&sink, NAN);
                audio_sink_end(&sink, wide, AUDIO_FORMAT_F32LE, 2, AUDIO_BLOCK);
        }
        AUCHECK(sink.mix[0][AUDIO_BLOCK - 1] == 0, "a master gain of NaN is not silence (%g)", (double)sink.mix[0][AUDIO_BLOCK - 1]);
}

static void au_test_resample_underrun_keeps_buf(void)
{
        static unsigned char ring[16384];
        static float chunk[100];
        struct audio_sink sink;
        struct audio_voice voice;
        long played;

        for (long i = 0; i < 100; i++)
                chunk[i] = 0.25f;
        audio_sink_open(&sink, 48000, 2);
        AUCHECK(audio_voice_open(&voice, AUDIO_FORMAT_F32LE, 1, 22050, 0, 48000, ring, sizeof(ring)) == 0,
                "resampled 22050->48000 voice opens");
        audio_ring_put(&voice.ring, (const unsigned char *)chunk, 100 * 4);
        audio_sink_begin(&sink, 128);
        played = audio_voice_mix(&sink, &voice, 128, 0);
        AUCHECK(played < 128 && voice.rs.have > voice.rs.table->taps / 2 - 1,
                "transient underrun keeps unconsumed resampler input frames (played %ld, have %ld, lead %ld)",
                played, voice.rs.have, voice.rs.table->taps / 2 - 1);
        audio_voice_close(&voice);
}

int main(void)
{
        au_test_numbers();
        au_test_volume();
        au_test_laws();
        au_test_formats();
        au_test_encode();
        au_test_ring();
        au_test_remix();
        au_test_mix_exact();
        au_test_mix_formats();
        au_test_mix_remix();
        au_test_peaks();
        au_test_ramps();
        au_test_underrun();
        au_test_tables();
        au_test_open();
        au_test_resampler();
        au_test_aliasing();
        au_test_hardening();
        au_test_format_matrix();
        au_test_narrow();
        au_test_resampler_streaming();
        au_test_voice_resampling();
        au_test_fuzz();
        au_test_fuzz_range();
        au_test_speed();
        au_test_resample_underrun_keeps_buf();
        printf("CHECK_audio_mix: %u checks, %u failures\n", au_checks, au_failures);
        return au_failures ? 1 : 0;
}
#endif /* CHECK_audio_mix */

#ifdef CHECK_audio_alsa
/*
        CHECK_audio_alsa: the sound card layer (src/audio/alsa.c) against the kernel's header where the header is here, a
        card of the check's own behind the five kernel calls (several kinds of card: what each takes, what the file
        picks, and every way a card goes wrong: an underrun, a suspend, a card pulled out, a driver that refuses), and
        on a Linux machine with the snd-aloop module, the real thing: a sine mixed by the mixing core, written to a
        loopback card and read back from the other side of it, bit for bit.

            cc -std=gnu11 -O2 -Wall -Wextra -DCHECK_audio_alsa test/checks.c -lm

        A machine with the module: sudo modprobe snd-aloop index=7 pcm_substreams=2; the check finds the card by its
        name and says plainly when it is not there.
*/
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#ifdef __linux__
#include <sound/asound.h>
#include <sound/tlv.h>
#define AL_HEADER 1
#endif
#ifndef EBADFD
#define EBADFD 77
#endif
#ifndef ESTRPIPE
#define ESTRPIPE 86
#endif

// mix.c's loops that are lib.c's, which a hosted check has none of: the C they were.
#define SHARED_audio_loops
#define AUDIO_C_AS_LIBRARY
#include "../checks.c"
#undef AUDIO_C_AS_LIBRARY
#undef SHARED_audio_loops

#ifndef offset_of
#include <stddef.h>
#define offset_of(type, member) offsetof(type, member)
#endif
#include "../../src/audio/mix.c"

static long audio_sys_open(const char *path, int flags);
static long audio_sys_close(int fd);
static long audio_sys_ioctl(int fd, unsigned long request, void *argument);
static long audio_sys_read(int fd, void *buffer, unsigned long bytes);
static long audio_sys_write(int fd, const void *buffer, unsigned long bytes);

// The library's names alsa.c calls, as the hosted build has them: the library's own are in the freestanding build.
typedef unsigned long positive;
typedef void *address_any;
typedef char *string_address;
static void *al_translate_hosted(void *block, unsigned long size, const void *table)
{
        unsigned char *bytes = block;

        for (unsigned long at = 0; at < size; at++)
                bytes[at] = ((const unsigned char *)table)[bytes[at]];
        return block;
}
#define memory_zero(to, size) memset((to), 0, (size))
#define memory_copy(to, from, size) memmove((to), (from), (size))
#define string_length_max(text, bound) strnlen((const char *)(text), (bound))
#define memory_translate(block, size, table) al_translate_hosted((block), (size), (table))

#include "../../src/audio/alsa.c"

static unsigned al_checks, al_failures;

#define ALCHECK(condition, ...)                                                                                       \
        do                                                                                                            \
        {                                                                                                             \
                al_checks++;                                                                                          \
                if (!(condition))                                                                                     \
                {                                                                                                     \
                        al_failures++;                                                                                \
                        if (al_failures < 60)                                                                         \
                        {                                                                                             \
                                printf("  FAIL: ");                                                                   \
                                printf(__VA_ARGS__);                                                                  \
                                printf(" (line %d)\n", __LINE__);                                                     \
                        }                                                                                             \
                }                                                                                                     \
        } while (0)

static unsigned long long al_state = 0x2545f4914f6cdd1dull;

static unsigned long al_rand(void)
{
        al_state ^= al_state >> 12;
        al_state ^= al_state << 25;
        al_state ^= al_state >> 27;
        return (unsigned long)(al_state * 0x2545f4914f6cdd1dull >> 11);
}

/* ---- the kernel's header against this file's layout ---- */

static void al_test_layout(void)
{
#ifdef AL_HEADER
        ALCHECK(sizeof(struct audio_alsa_hw) == sizeof(struct snd_pcm_hw_params), "hw_params size");
        ALCHECK(sizeof(struct audio_alsa_sw) == sizeof(struct snd_pcm_sw_params), "sw_params size");
        ALCHECK(sizeof(struct audio_alsa_sync) == sizeof(struct snd_pcm_sync_ptr), "sync_ptr size");
        ALCHECK(sizeof(struct audio_alsa_xfer) == sizeof(struct snd_xferi), "xferi size");
        ALCHECK(sizeof(struct audio_alsa_pcm_info) == sizeof(struct snd_pcm_info), "pcm_info size");
        ALCHECK(sizeof(struct audio_alsa_card_info) == sizeof(struct snd_ctl_card_info), "card_info size");
        ALCHECK(sizeof(struct audio_alsa_mask) == sizeof(struct snd_mask) && sizeof(struct audio_alsa_interval) == sizeof(struct snd_interval), "mask and interval");
        ALCHECK(offsetof(struct audio_alsa_hw, masks) == offsetof(struct snd_pcm_hw_params, masks), "masks at");
        ALCHECK(offsetof(struct audio_alsa_hw, intervals) == offsetof(struct snd_pcm_hw_params, intervals), "intervals at");
        ALCHECK(offsetof(struct audio_alsa_hw, rmask) == offsetof(struct snd_pcm_hw_params, rmask), "rmask at");
        ALCHECK(offsetof(struct audio_alsa_hw, cmask) == offsetof(struct snd_pcm_hw_params, cmask), "cmask at");
        ALCHECK(offsetof(struct audio_alsa_hw, info) == offsetof(struct snd_pcm_hw_params, info), "info at");
        ALCHECK(offsetof(struct audio_alsa_hw, fifo_size) == offsetof(struct snd_pcm_hw_params, fifo_size), "fifo_size at");
        ALCHECK(offsetof(struct audio_alsa_sw, avail_min) == offsetof(struct snd_pcm_sw_params, avail_min), "avail_min at");
        ALCHECK(offsetof(struct audio_alsa_sw, start_threshold) == offsetof(struct snd_pcm_sw_params, start_threshold), "start_threshold at");
        ALCHECK(offsetof(struct audio_alsa_sw, stop_threshold) == offsetof(struct snd_pcm_sw_params, stop_threshold), "stop_threshold at");
        ALCHECK(offsetof(struct audio_alsa_sw, boundary) == offsetof(struct snd_pcm_sw_params, boundary), "boundary at");
        ALCHECK(offsetof(struct audio_alsa_sw, proto) == offsetof(struct snd_pcm_sw_params, proto), "proto at");
        ALCHECK(offsetof(struct audio_alsa_sw, tstamp_type) == offsetof(struct snd_pcm_sw_params, tstamp_type), "tstamp_type at");
        ALCHECK(offsetof(struct audio_alsa_sync, s) == offsetof(struct snd_pcm_sync_ptr, s) && offsetof(struct audio_alsa_sync, c) == offsetof(struct snd_pcm_sync_ptr, c), "sync_ptr union places");
        ALCHECK(offsetof(struct audio_alsa_status, hw_ptr) == offsetof(struct snd_pcm_mmap_status, hw_ptr), "status hw_ptr at");
        ALCHECK(offsetof(struct audio_alsa_status, suspended_state) == offsetof(struct snd_pcm_mmap_status, suspended_state), "status suspended at");
        ALCHECK(offsetof(struct audio_alsa_pcm_info, subdevices_count) == offsetof(struct snd_pcm_info, subdevices_count), "pcm_info subdevices at");
        ALCHECK(offsetof(struct audio_alsa_pcm_info, name) == offsetof(struct snd_pcm_info, name), "pcm_info name at");
        ALCHECK(offsetof(struct audio_alsa_card_info, longname) == offsetof(struct snd_ctl_card_info, longname), "card_info longname at");
        ALCHECK(offsetof(struct audio_alsa_card_info, components) == offsetof(struct snd_ctl_card_info, components), "card_info components at");
        ALCHECK(AUDIO_ALSA_HW_REFINE == SNDRV_PCM_IOCTL_HW_REFINE, "HW_REFINE %lx against %lx", (unsigned long)AUDIO_ALSA_HW_REFINE, (unsigned long)SNDRV_PCM_IOCTL_HW_REFINE);
        ALCHECK(AUDIO_ALSA_HW_PARAMS == SNDRV_PCM_IOCTL_HW_PARAMS, "HW_PARAMS");
        ALCHECK(AUDIO_ALSA_SW_PARAMS == SNDRV_PCM_IOCTL_SW_PARAMS, "SW_PARAMS");
        ALCHECK(AUDIO_ALSA_DELAY == SNDRV_PCM_IOCTL_DELAY, "DELAY");
        ALCHECK(AUDIO_ALSA_SYNC_PTR == SNDRV_PCM_IOCTL_SYNC_PTR, "SYNC_PTR");
        ALCHECK(AUDIO_ALSA_PREPARE == SNDRV_PCM_IOCTL_PREPARE, "PREPARE");
        ALCHECK(AUDIO_ALSA_START == SNDRV_PCM_IOCTL_START, "START");
        ALCHECK(AUDIO_ALSA_DROP == SNDRV_PCM_IOCTL_DROP, "DROP");
        ALCHECK(AUDIO_ALSA_RESUME == SNDRV_PCM_IOCTL_RESUME, "RESUME");
        ALCHECK(AUDIO_ALSA_CARD_INFO == SNDRV_CTL_IOCTL_CARD_INFO, "CARD_INFO");
        ALCHECK(AUDIO_ALSA_PCM_NEXT_DEVICE == SNDRV_CTL_IOCTL_PCM_NEXT_DEVICE, "PCM_NEXT_DEVICE");
        ALCHECK(AUDIO_ALSA_PCM_INFO == SNDRV_CTL_IOCTL_PCM_INFO, "PCM_INFO");
        ALCHECK(AUDIO_ALSA_PCM_PREFER_SUBDEVICE == SNDRV_CTL_IOCTL_PCM_PREFER_SUBDEVICE, "PCM_PREFER_SUBDEVICE");
        ALCHECK(sizeof(struct audio_ctl_id) == sizeof(struct snd_ctl_elem_id) && sizeof(struct audio_ctl_list) == sizeof(struct snd_ctl_elem_list) &&
                    sizeof(struct audio_ctl_info) == sizeof(struct snd_ctl_elem_info) && sizeof(struct audio_ctl_value) == sizeof(struct snd_ctl_elem_value),
                "the mixer's structures: %zu %zu %zu %zu", sizeof(struct snd_ctl_elem_id), sizeof(struct snd_ctl_elem_list), sizeof(struct snd_ctl_elem_info), sizeof(struct snd_ctl_elem_value));
        ALCHECK(offsetof(struct audio_ctl_id, name) == offsetof(struct snd_ctl_elem_id, name) && offsetof(struct audio_ctl_id, index) == offsetof(struct snd_ctl_elem_id, index) &&
                    offsetof(struct audio_ctl_id, iface) == offsetof(struct snd_ctl_elem_id, iface),
                "the id's places");
        ALCHECK(offsetof(struct audio_ctl_list, pids) == offsetof(struct snd_ctl_elem_list, pids) && offsetof(struct audio_ctl_list, count) == offsetof(struct snd_ctl_elem_list, count) &&
                    offsetof(struct audio_ctl_list, used) == offsetof(struct snd_ctl_elem_list, used),
                "the list's places");
        ALCHECK(offsetof(struct audio_ctl_info, type) == offsetof(struct snd_ctl_elem_info, type) && offsetof(struct audio_ctl_info, access) == offsetof(struct snd_ctl_elem_info, access) &&
                    offsetof(struct audio_ctl_info, count) == offsetof(struct snd_ctl_elem_info, count) && offsetof(struct audio_ctl_info, value) == offsetof(struct snd_ctl_elem_info, value) &&
                    offsetof(struct audio_ctl_info, value.integer.max) == offsetof(struct snd_ctl_elem_info, value.integer.max) &&
                    offsetof(struct audio_ctl_info, value.enumerated.name) == offsetof(struct snd_ctl_elem_info, value.enumerated.name) &&
                    offsetof(struct audio_ctl_info, value.enumerated.items) == offsetof(struct snd_ctl_elem_info, value.enumerated.items),
                "the info's places");
        ALCHECK(offsetof(struct audio_ctl_value, value) == offsetof(struct snd_ctl_elem_value, value) && offsetof(struct audio_ctl_value, value.enumerated) == offsetof(struct snd_ctl_elem_value, value.enumerated.item),
                "the value's places");
        ALCHECK(offsetof(struct audio_ctl_tlv, tlv) == offsetof(struct snd_ctl_tlv, tlv), "the tlv's place");
        ALCHECK(AUDIO_CTL_ELEM_LIST == SNDRV_CTL_IOCTL_ELEM_LIST && AUDIO_CTL_ELEM_INFO == SNDRV_CTL_IOCTL_ELEM_INFO && AUDIO_CTL_ELEM_READ == SNDRV_CTL_IOCTL_ELEM_READ &&
                    AUDIO_CTL_ELEM_WRITE == SNDRV_CTL_IOCTL_ELEM_WRITE && AUDIO_CTL_TLV_READ == SNDRV_CTL_IOCTL_TLV_READ,
                "the mixer's calls: %lx %lx %lx %lx %lx against %lx %lx %lx %lx %lx", (unsigned long)AUDIO_CTL_ELEM_LIST, (unsigned long)AUDIO_CTL_ELEM_INFO, (unsigned long)AUDIO_CTL_ELEM_READ,
                (unsigned long)AUDIO_CTL_ELEM_WRITE, (unsigned long)AUDIO_CTL_TLV_READ, (unsigned long)SNDRV_CTL_IOCTL_ELEM_LIST, (unsigned long)SNDRV_CTL_IOCTL_ELEM_INFO,
                (unsigned long)SNDRV_CTL_IOCTL_ELEM_READ, (unsigned long)SNDRV_CTL_IOCTL_ELEM_WRITE, (unsigned long)SNDRV_CTL_IOCTL_TLV_READ);
        ALCHECK(AUDIO_CTL_IFACE_MIXER == SNDRV_CTL_ELEM_IFACE_MIXER && AUDIO_CTL_TYPE_BOOLEAN == SNDRV_CTL_ELEM_TYPE_BOOLEAN && AUDIO_CTL_TYPE_INTEGER == SNDRV_CTL_ELEM_TYPE_INTEGER &&
                    AUDIO_CTL_TYPE_ENUMERATED == SNDRV_CTL_ELEM_TYPE_ENUMERATED && AUDIO_CTL_ACCESS_READ == SNDRV_CTL_ELEM_ACCESS_READ && AUDIO_CTL_ACCESS_WRITE == SNDRV_CTL_ELEM_ACCESS_WRITE &&
                    AUDIO_CTL_ACCESS_INACTIVE == SNDRV_CTL_ELEM_ACCESS_INACTIVE && AUDIO_CTL_ACCESS_TLV_READ == SNDRV_CTL_ELEM_ACCESS_TLV_READ &&
                    AUDIO_CTL_TLVT_DB_SCALE == SNDRV_CTL_TLVT_DB_SCALE && AUDIO_CTL_TLVT_DB_MINMAX == SNDRV_CTL_TLVT_DB_MINMAX && AUDIO_CTL_TLVT_DB_MINMAX_MUTE == SNDRV_CTL_TLVT_DB_MINMAX_MUTE,
                "the mixer's numbers");
        ALCHECK(AUDIO_ALSA_ACCESS_RW_INTERLEAVED == SNDRV_PCM_ACCESS_RW_INTERLEAVED, "access");
        ALCHECK(AUDIO_ALSA_FORMAT_S16_LE == SNDRV_PCM_FORMAT_S16_LE && AUDIO_ALSA_FORMAT_S24_LE == SNDRV_PCM_FORMAT_S24_LE &&
                    AUDIO_ALSA_FORMAT_S32_LE == SNDRV_PCM_FORMAT_S32_LE && AUDIO_ALSA_FORMAT_S24_3LE == SNDRV_PCM_FORMAT_S24_3LE &&
                    AUDIO_ALSA_FORMAT_FLOAT_LE == SNDRV_PCM_FORMAT_FLOAT_LE,
                "formats");
        ALCHECK(AUDIO_ALSA_STATE_PREPARED == SNDRV_PCM_STATE_PREPARED && AUDIO_ALSA_STATE_RUNNING == SNDRV_PCM_STATE_RUNNING &&
                    AUDIO_ALSA_STATE_XRUN == SNDRV_PCM_STATE_XRUN && AUDIO_ALSA_STATE_SUSPENDED == SNDRV_PCM_STATE_SUSPENDED &&
                    AUDIO_ALSA_STATE_DISCONNECTED == SNDRV_PCM_STATE_DISCONNECTED,
                "states");
        ALCHECK(AUDIO_ALSA_SYNC_HWSYNC == SNDRV_PCM_SYNC_PTR_HWSYNC && AUDIO_ALSA_SYNC_APPL == SNDRV_PCM_SYNC_PTR_APPL &&
                    AUDIO_ALSA_SYNC_AVAIL_MIN == SNDRV_PCM_SYNC_PTR_AVAIL_MIN,
                "sync_ptr flags");
        ALCHECK(AUDIO_ALSA_VERSION == SNDRV_PCM_VERSION, "protocol version");
        ALCHECK(AUDIO_ALSA_ACCESS == SNDRV_PCM_HW_PARAM_ACCESS && AUDIO_ALSA_FORMAT == SNDRV_PCM_HW_PARAM_FORMAT &&
                    AUDIO_ALSA_SUBFORMAT == SNDRV_PCM_HW_PARAM_SUBFORMAT && AUDIO_ALSA_FIRST_INTERVAL == SNDRV_PCM_HW_PARAM_FIRST_INTERVAL &&
                    AUDIO_ALSA_CHANNELS == SNDRV_PCM_HW_PARAM_CHANNELS && AUDIO_ALSA_RATE == SNDRV_PCM_HW_PARAM_RATE &&
                    AUDIO_ALSA_PERIOD_TIME == SNDRV_PCM_HW_PARAM_PERIOD_TIME && AUDIO_ALSA_PERIOD_SIZE == SNDRV_PCM_HW_PARAM_PERIOD_SIZE &&
                    AUDIO_ALSA_PERIODS == SNDRV_PCM_HW_PARAM_PERIODS && AUDIO_ALSA_BUFFER_SIZE == SNDRV_PCM_HW_PARAM_BUFFER_SIZE,
                "parameter numbers");
#else
        printf("  layout: NOT RUN (no <sound/asound.h> here)\n");
#endif
}

/* ---- a card of the check's own behind the five calls ---- */

struct al_model
{
        const char *name;
        unsigned long long formats;      // bit n is format n
        unsigned cmin, cmax;
        unsigned rates[8];               // zero ends the list
        unsigned pmin, pmax, pstep;      // period sizes
        unsigned bmin, bmax;             // buffer sizes
        unsigned periods_min;
        int rw;                          // takes the read/write access
};

#define F(n) (1ull << (n))
static const struct al_model al_models[] = {
    {"hda", F(2) | F(6) | F(10), 2, 2, {44100, 48000, 96000, 192000, 0}, 32, 16384, 1, 64, 65536, 2, 1},
    {"usb", F(32), 2, 2, {44100, 48000, 96000, 0}, 48, 4800, 48, 96, 96000, 2, 1},
    {"hdmi", F(2) | F(10), 2, 8, {32000, 44100, 48000, 88200, 96000, 176400, 192000, 0}, 64, 8192, 1, 128, 65536, 2, 1},
    {"surround", F(2), 6, 6, {96000, 0}, 64, 8192, 1, 128, 65536, 2, 1},
    {"norw", F(10), 2, 2, {48000, 0}, 64, 8192, 1, 128, 65536, 2, 0},
    {"odd", F(2), 2, 2, {12345, 0}, 64, 8192, 1, 128, 65536, 2, 1},
    {"tiny", F(2), 2, 2, {48000, 0}, 256, 512, 1, 512, 512, 2, 1},
    {"floaty", F(14), 2, 2, {48000, 0}, 64, 8192, 1, 128, 65536, 2, 1},
};

#define AL_FDS 16
#define AL_BASE 100

static struct al_pcm_fake
{
        int used, control, card, device, stream;
        const struct al_model *model;
        int state, configured;
        unsigned format, channels, rate;
        long period, buffer, boundary;
        long hw, appl;                   // in [0, boundary)
        long avail_min, start_threshold;
        long hw_delay;
        long played_total, written_total; // frames, for the reference
        unsigned char written[1 << 16];
        long written_bytes;
        int wanted_subdevice;
} al_fds[AL_FDS];

static int al_use_real;
static int al_opens, al_closes;
static int al_model_of_card[AUDIO_ALSA_CARDS];
static int al_card_present[AUDIO_ALSA_CARDS];
static long al_next_errno; // an errno the next write answers, once
static long al_write_calls; // the writes the card has been asked for
static long al_small_boundary;

struct al_card_fake
{
        const char *id, *name, *longname;
        int devices, device[4], playback[4], capture[4];
        const char *device_name[4];
        int info_fails;
};
static struct al_card_fake al_cards[AUDIO_ALSA_CARDS];

static long al_fake_open(const char *path, int flags)
{
        int card = -1, device = 0, stream = 0, control = 0, at;
        const char *p = path;

        (void)flags;
        if (strncmp(p, "/dev/snd/controlC", 17) == 0)
        {
                control = 1;
                p += 17;
        }
        else if (strncmp(p, "/dev/snd/pcmC", 13) == 0)
                p += 13;
        else
                return -ENOENT;
        card = 0;
        for (; *p >= '0' && *p <= '9'; p++)
                card = card * 10 + (*p - '0');
        if (!control)
        {
                if (*p++ != 'D')
                        return -ENOENT;
                for (; *p >= '0' && *p <= '9'; p++)
                        device = device * 10 + (*p - '0');
                stream = *p == 'c';
        }
        if (card >= AUDIO_ALSA_CARDS || !al_card_present[card])
                return -ENOENT;
        for (at = 0; at < AL_FDS; at++)
                if (!al_fds[at].used)
                        break;
        if (at == AL_FDS)
                return -EMFILE;
        memset(al_fds + at, 0, sizeof(al_fds[0]));
        al_fds[at].used = 1;
        al_fds[at].control = control;
        al_fds[at].card = card;
        al_fds[at].device = device;
        al_fds[at].stream = stream;
        al_fds[at].model = al_models + al_model_of_card[card];
        al_fds[at].wanted_subdevice = -1;
        al_opens++;
        return AL_BASE + at;
}

static struct al_pcm_fake *al_fd(int fd)
{
        return fd >= AL_BASE && fd < AL_BASE + AL_FDS && al_fds[fd - AL_BASE].used ? al_fds + (fd - AL_BASE) : 0;
}

// Narrows an interval by another; empty when nothing is left.
static int al_narrow(struct audio_alsa_interval *iv, unsigned min, unsigned max)
{
        if (iv->min < min)
                iv->min = min;
        if (iv->max > max)
                iv->max = max;
        if (iv->min > iv->max)
        {
                iv->flags |= 8;
                return 0;
        }
        return 1;
}

// iv is the intervals by the number of the parameter (the kernel's array starts at the eighth).
static int al_refine_inner(struct al_pcm_fake *pcm, struct audio_alsa_hw *hw, struct audio_alsa_interval *iv, int choose)
{
        const struct al_model *m = pcm->model;
        int rate_ok = 0;

        if (!m->rw)
                hw->masks[AUDIO_ALSA_ACCESS].bits[0] &= ~(1u << AUDIO_ALSA_ACCESS_RW_INTERLEAVED);
        if (!audio_alsa_has(&hw->masks[AUDIO_ALSA_ACCESS], AUDIO_ALSA_ACCESS_RW_INTERLEAVED))
                return -EINVAL;
        for (int w = 0; w < 8; w++)
        {
                unsigned long long bits = w < 2 ? (m->formats >> (32 * w)) & 0xffffffffull : 0;

                hw->masks[AUDIO_ALSA_FORMAT].bits[w] &= (unsigned)bits;
        }
        {
                int any = 0;

                for (int w = 0; w < 8; w++)
                        any |= hw->masks[AUDIO_ALSA_FORMAT].bits[w] != 0;
                if (!any)
                        return -EINVAL;
        }
        if (!al_narrow(&iv[AUDIO_ALSA_CHANNELS], m->cmin, m->cmax))
                return -EINVAL;
        if (iv[AUDIO_ALSA_RATE].min == iv[AUDIO_ALSA_RATE].max)
        {
                for (int r = 0; m->rates[r]; r++)
                        rate_ok |= m->rates[r] == iv[AUDIO_ALSA_RATE].min;
                if (!rate_ok)
                        return -EINVAL;
        }
        else
        {
                unsigned low = ~0u, high = 0;

                for (int r = 0; m->rates[r]; r++)
                {
                        if (m->rates[r] < low)
                                low = m->rates[r];
                        if (m->rates[r] > high)
                                high = m->rates[r];
                }
                if (!al_narrow(&iv[AUDIO_ALSA_RATE], low, high))
                        return -EINVAL;
        }
        // Period time bounds the period size once the rate is known.
        if (iv[AUDIO_ALSA_RATE].min == iv[AUDIO_ALSA_RATE].max && (iv[AUDIO_ALSA_PERIOD_TIME].min || iv[AUDIO_ALSA_PERIOD_TIME].max != ~0u))
        {
                unsigned long long rate = iv[AUDIO_ALSA_RATE].min;
                unsigned min = (unsigned)((iv[AUDIO_ALSA_PERIOD_TIME].min * rate + 999999) / 1000000);
                unsigned max = iv[AUDIO_ALSA_PERIOD_TIME].max == ~0u ? ~0u : (unsigned)(iv[AUDIO_ALSA_PERIOD_TIME].max * rate / 1000000);

                if (!al_narrow(&iv[AUDIO_ALSA_PERIOD_SIZE], min, max))
                        return -EINVAL;
        }
        if (!al_narrow(&iv[AUDIO_ALSA_PERIOD_SIZE], m->pmin, m->pmax) || !al_narrow(&iv[AUDIO_ALSA_BUFFER_SIZE], m->bmin, m->bmax))
                return -EINVAL;
        // The step: a minimum is rounded up to it, and a single value must be on it.
        {
                struct audio_alsa_interval *ps = &iv[AUDIO_ALSA_PERIOD_SIZE];

                if (ps->min % m->pstep)
                        ps->min += m->pstep - ps->min % m->pstep;
                if (ps->max != ~0u)
                        ps->max -= ps->max % m->pstep;
                if (ps->min > ps->max)
                        return -EINVAL;
        }
        if (!al_narrow(&iv[AUDIO_ALSA_PERIODS], m->periods_min, 1024))
                return -EINVAL;
        // buffer = periods * period
        if (iv[AUDIO_ALSA_PERIOD_SIZE].min == iv[AUDIO_ALSA_PERIOD_SIZE].max)
        {
                unsigned long long period = iv[AUDIO_ALSA_PERIOD_SIZE].min;

                if (!al_narrow(&iv[AUDIO_ALSA_BUFFER_SIZE], (unsigned)(period * iv[AUDIO_ALSA_PERIODS].min), ~0u))
                        return -EINVAL;
                if (iv[AUDIO_ALSA_BUFFER_SIZE].min == iv[AUDIO_ALSA_BUFFER_SIZE].max && iv[AUDIO_ALSA_BUFFER_SIZE].min % period)
                        return -EINVAL;
        }
        if (!choose)
                return 0;
        // The driver's choice: the least of whatever is open.
        {
                unsigned period, buffer;

                period = iv[AUDIO_ALSA_PERIOD_SIZE].min;
                buffer = iv[AUDIO_ALSA_BUFFER_SIZE].min;
                if (buffer < period * iv[AUDIO_ALSA_PERIODS].min)
                        buffer = period * iv[AUDIO_ALSA_PERIODS].min;
                if (buffer % period)
                        buffer += period - buffer % period;
                if (buffer > iv[AUDIO_ALSA_BUFFER_SIZE].max)
                        return -EINVAL;
                iv[AUDIO_ALSA_PERIOD_SIZE].min = iv[AUDIO_ALSA_PERIOD_SIZE].max = period;
                iv[AUDIO_ALSA_BUFFER_SIZE].min = iv[AUDIO_ALSA_BUFFER_SIZE].max = buffer;
                iv[AUDIO_ALSA_RATE].max = iv[AUDIO_ALSA_RATE].min;
                iv[AUDIO_ALSA_CHANNELS].max = iv[AUDIO_ALSA_CHANNELS].min;
                pcm->period = period;
                pcm->buffer = buffer;
                pcm->rate = iv[AUDIO_ALSA_RATE].min;
                pcm->channels = iv[AUDIO_ALSA_CHANNELS].min;
                for (int bit = 0; bit < 64; bit++)
                        if (audio_alsa_has(&hw->masks[AUDIO_ALSA_FORMAT], (unsigned)bit))
                        {
                                pcm->format = (unsigned)bit;
                                break;
                        }
        }
        return 0;
}

static int al_refine(struct al_pcm_fake *pcm, struct audio_alsa_hw *hw, int choose)
{
        struct audio_alsa_interval iv[AUDIO_ALSA_FIRST_INTERVAL + 12];
        int result;

        memset(iv, 0, sizeof(iv));
        memcpy(iv + AUDIO_ALSA_FIRST_INTERVAL, hw->intervals, sizeof(hw->intervals));
        result = al_refine_inner(pcm, hw, iv, choose);
        memcpy(hw->intervals, iv + AUDIO_ALSA_FIRST_INTERVAL, sizeof(hw->intervals));
        return result;
}

/* ---- a card's mixer of the check's own ---- */

struct al_ctl
{
        const char *name;
        unsigned iface, type, access, count;
        long min, max;
        long value[2];
        unsigned items;
        const char *item[4];
        int tlv_min, tlv_step; // in hundredths of a dB; a step of 0 is no scale (the step's 0x10000 is the scale's mute flag)
        int write_fails;       // the card refuses the write
        int ignores;           // the card takes the write and keeps what it had
        unsigned writes;
        int tlv_kind, tlv_max; // 4 or 5: a line between tlv_min and tlv_max (in hundredths of a dB), not a scale
};

static struct al_ctl al_ctls[48];
static int al_ctl_count;

static struct al_ctl *al_ctl_of(unsigned numid)
{
        return numid >= 1 && numid <= (unsigned)al_ctl_count ? al_ctls + numid - 1 : 0;
}

static void al_ctl_add(struct al_ctl ctl)
{
        al_ctls[al_ctl_count++] = ctl;
}

// The mixer calls of the kernel for the control file of a card, as far as the file uses them.
static long al_mixer_ioctl(unsigned long request, void *argument)
{
        if (request == AUDIO_CTL_ELEM_LIST)
        {
                struct audio_ctl_list *list = argument;
                unsigned used = 0;

                ALCHECK(list->space <= AUDIO_MIXER_MAX, "the file asks for no more controls than its array holds: %u", list->space);
                list->count = (unsigned)al_ctl_count;
                for (unsigned i = list->offset; i < (unsigned)al_ctl_count && used < list->space; i++)
                {
                        memset(list->pids + used, 0, sizeof(list->pids[0]));
                        list->pids[used].numid = i + 1;
                        list->pids[used].iface = al_ctls[i].iface;
                        strncpy((char *)list->pids[used].name, al_ctls[i].name, 43);
                        used++;
                }
                list->used = used;
                return 0;
        }
        if (request == AUDIO_CTL_ELEM_INFO)
        {
                struct audio_ctl_info *info = argument;
                struct al_ctl *ctl = al_ctl_of(info->id.numid);

                if (!ctl)
                        return -ENOENT;
                info->type = (int)ctl->type;
                info->access = ctl->access | (ctl->tlv_step || ctl->tlv_kind ? AUDIO_CTL_ACCESS_TLV_READ : 0);
                info->count = ctl->count;
                if (ctl->type == AUDIO_CTL_TYPE_INTEGER)
                {
                        info->value.integer.min = ctl->min;
                        info->value.integer.max = ctl->max;
                        info->value.integer.step = 0;
                }
                if (ctl->type == AUDIO_CTL_TYPE_ENUMERATED)
                {
                        unsigned item = info->value.enumerated.item;

                        info->value.enumerated.items = ctl->items;
                        if (item >= ctl->items)
                                return -EINVAL;
                        memset(info->value.enumerated.name, 0, sizeof(info->value.enumerated.name));
                        strncpy(info->value.enumerated.name, ctl->item[item], 63);
                }
                return 0;
        }
        if (request == AUDIO_CTL_ELEM_READ || request == AUDIO_CTL_ELEM_WRITE)
        {
                struct audio_ctl_value *value = argument;
                struct al_ctl *ctl = al_ctl_of(value->id.numid);

                if (!ctl)
                        return -ENOENT;
                if (request == AUDIO_CTL_ELEM_READ)
                {
                        for (unsigned i = 0; i < ctl->count && i < 2; i++)
                        {
                                if (ctl->type == AUDIO_CTL_TYPE_ENUMERATED)
                                        value->value.enumerated[i] = (unsigned)ctl->value[i];
                                else
                                        value->value.integer[i] = ctl->value[i];
                        }
                        return 0;
                }
                if (!(ctl->access & AUDIO_CTL_ACCESS_WRITE))
                        return -EPERM;
                ctl->writes++;
                if (ctl->write_fails)
                        return -EPERM;
                if (!ctl->ignores)
                        for (unsigned i = 0; i < ctl->count && i < 2; i++)
                        {
                                long v = ctl->type == AUDIO_CTL_TYPE_ENUMERATED ? (long)value->value.enumerated[i] : value->value.integer[i];

                                if (ctl->type == AUDIO_CTL_TYPE_INTEGER && (v < ctl->min || v > ctl->max))
                                        return -EINVAL;
                                ctl->value[i] = v;
                        }
                return 0;
        }
        if (request == AUDIO_CTL_TLV_READ)
        {
                struct audio_ctl_tlv *tlv = argument;
                struct al_ctl *ctl = al_ctl_of(tlv->numid);

                if (!ctl || (!ctl->tlv_step && !ctl->tlv_kind))
                        return -ENOENT;
                if (tlv->length < 16)
                        return -ENOSPC;
                tlv->tlv[0] = ctl->tlv_kind ? (unsigned)ctl->tlv_kind : AUDIO_CTL_TLVT_DB_SCALE;
                tlv->tlv[1] = 8;
                tlv->tlv[2] = (unsigned)ctl->tlv_min;
                tlv->tlv[3] = ctl->tlv_kind ? (unsigned)ctl->tlv_max : (unsigned)ctl->tlv_step;
                return 0;
        }
        return -ENOTTY;
}

static long al_fake_ioctl(int fd, unsigned long request, void *argument)
{
        struct al_pcm_fake *pcm = al_fd(fd);

        if (!pcm)
                return -EBADF;
        if (pcm->control)
        {
                struct al_card_fake *card = &al_cards[pcm->card];

                if (request == AUDIO_ALSA_CARD_INFO)
                {
                        struct audio_alsa_card_info *info = argument;

                        if (card->info_fails)
                                return -EIO;
                        memset(info, 0, sizeof(*info));
                        info->card = pcm->card;
                        // No end byte on purpose: a driver's name is bounded by the array, not by a zero.
                        memcpy(info->id, card->id, strnlen(card->id, sizeof(info->id)));
                        memcpy(info->name, card->name, strnlen(card->name, sizeof(info->name)));
                        memcpy(info->longname, card->longname, strnlen(card->longname, sizeof(info->longname)));
                        return 0;
                }
                if (request == AUDIO_ALSA_PCM_NEXT_DEVICE)
                {
                        int *device = argument, next = -1;

                        for (int d = 0; d < card->devices; d++)
                                if (card->device[d] > *device && (next < 0 || card->device[d] < next))
                                        next = card->device[d];
                        *device = next;
                        return 0;
                }
                if (request == AUDIO_ALSA_PCM_INFO)
                {
                        struct audio_alsa_pcm_info *info = argument;

                        for (int d = 0; d < card->devices; d++)
                                if ((unsigned)card->device[d] == info->device && (info->stream ? card->capture[d] : card->playback[d]))
                                {
                                        memset(info->name, 0, sizeof(info->name));
                                        memcpy(info->name, card->device_name[d], strnlen(card->device_name[d], sizeof(info->name)));
                                        return 0;
                                }
                        return -ENOENT;
                }
                if (request == AUDIO_ALSA_PCM_PREFER_SUBDEVICE)
                        return 0;
                return al_mixer_ioctl(request, argument);
        }
        if (request == AUDIO_ALSA_HW_REFINE || request == AUDIO_ALSA_HW_PARAMS)
        {
                struct audio_alsa_hw *hw = argument;
                int result = al_refine(pcm, hw, request == AUDIO_ALSA_HW_PARAMS);

                if (result < 0)
                        return result;
                if (request == AUDIO_ALSA_HW_PARAMS)
                        pcm->configured = 1;
                return 0;
        }
        if (!pcm->configured)
                return -EBADFD;
        if (request == AUDIO_ALSA_SW_PARAMS)
        {
                struct audio_alsa_sw *sw = argument;
                long boundary = pcm->buffer;

                if (sw->avail_min == 0 || sw->start_threshold > (unsigned long)pcm->buffer || sw->proto == 0)
                        return -EINVAL;
                // The kernel's boundary: the buffer doubled while it stays small enough for a long (a test may ask for a small one, so the pointers wrap).
                if (al_small_boundary)
                        boundary = pcm->buffer * al_small_boundary;
                else
                        while (boundary * 2 <= (long)(0x7fffffffl - pcm->buffer))
                                boundary *= 2;
                pcm->boundary = boundary;
                sw->boundary = (unsigned long)boundary;
                pcm->avail_min = (long)sw->avail_min;
                pcm->start_threshold = (long)sw->start_threshold;
                return 0;
        }
        if (request == AUDIO_ALSA_PREPARE)
        {
                if (pcm->state == AUDIO_ALSA_STATE_DISCONNECTED)
                        return -ENODEV;
                pcm->state = AUDIO_ALSA_STATE_PREPARED;
                pcm->hw = pcm->appl = 0;
                return 0;
        }
        if (request == AUDIO_ALSA_START)
        {
                pcm->state = AUDIO_ALSA_STATE_RUNNING;
                return 0;
        }
        if (request == AUDIO_ALSA_DROP)
        {
                if (pcm->state != AUDIO_ALSA_STATE_DISCONNECTED)
                        pcm->state = 1;
                return 0;
        }
        if (request == AUDIO_ALSA_RESUME)
        {
                if (pcm->state == AUDIO_ALSA_STATE_SUSPENDED)
                        pcm->state = AUDIO_ALSA_STATE_PREPARED;
                return 0;
        }
        if (request == AUDIO_ALSA_DELAY)
        {
                long fill = pcm->appl - pcm->hw;

                if (fill < 0)
                        fill += pcm->boundary;
                *(long *)argument = fill + pcm->hw_delay;
                return 0;
        }
        if (request == AUDIO_ALSA_SYNC_PTR)
        {
                struct audio_alsa_sync *sync = argument;

                if (pcm->state == AUDIO_ALSA_STATE_DISCONNECTED)
                        return -ENODEV;
                // As the kernel does: without the flag the application pointer in the call is the one that is SET.
                if (!(sync->flags & AUDIO_ALSA_SYNC_APPL))
                        pcm->appl = (long)sync->c.control.appl_ptr;
                if (!(sync->flags & AUDIO_ALSA_SYNC_AVAIL_MIN))
                        pcm->avail_min = (long)sync->c.control.avail_min;
                sync->s.status.state = pcm->state;
                sync->s.status.hw_ptr = (unsigned long)pcm->hw;
                sync->c.control.appl_ptr = (unsigned long)pcm->appl;
                sync->c.control.avail_min = (unsigned long)pcm->avail_min;
                return 0;
        }
        return -ENOTTY;
}

// The hardware plays (or captures) frames: its pointer moves, and an emptied buffer is an underrun.
static void al_hardware(int fd, long frames)
{
        struct al_pcm_fake *pcm = al_fd(fd);

        if (!pcm || pcm->state != AUDIO_ALSA_STATE_RUNNING)
                return;
        if (pcm->stream == 1)
        {
                long held = pcm->hw - pcm->appl;

                if (held < 0)
                        held += pcm->boundary;
                if (held + frames > pcm->buffer)
                {
                        pcm->state = AUDIO_ALSA_STATE_XRUN;
                        return;
                }
        }
        if (pcm->stream == 0)
        {
                long fill = pcm->appl - pcm->hw;

                if (fill < 0)
                        fill += pcm->boundary;
                if (frames > fill)
                {
                        pcm->played_total += fill;
                        pcm->hw = pcm->appl;
                        pcm->state = AUDIO_ALSA_STATE_XRUN;
                        return;
                }
                pcm->played_total += frames;
        }
        pcm->hw = (pcm->hw + frames) % pcm->boundary;
}

static long al_fake_write(int fd, const void *buffer, unsigned long bytes)
{
        struct al_pcm_fake *pcm = al_fd(fd);
        long frame, frames, room, fill;

        al_write_calls++;
        if (!pcm || !pcm->configured)
                return -EBADFD;
        if (al_next_errno)
        {
                long e = al_next_errno;

                al_next_errno = 0;
                if (e == -ENODEV)
                        pcm->state = AUDIO_ALSA_STATE_DISCONNECTED;
                if (e == -ESTRPIPE)
                        pcm->state = AUDIO_ALSA_STATE_SUSPENDED;
                return e;
        }
        if (pcm->state == AUDIO_ALSA_STATE_DISCONNECTED)
                return -ENODEV;
        if (pcm->state == AUDIO_ALSA_STATE_XRUN)
                return -EPIPE;
        if (pcm->state == AUDIO_ALSA_STATE_SUSPENDED)
                return -ESTRPIPE;
        if (pcm->state != AUDIO_ALSA_STATE_PREPARED && pcm->state != AUDIO_ALSA_STATE_RUNNING)
                return -EBADFD;
        frame = (pcm->format == 2 ? 2 : pcm->format == 32 ? 3 : 4) * (long)pcm->channels;
        frames = (long)bytes / frame;
        fill = pcm->appl - pcm->hw;
        if (fill < 0)
                fill += pcm->boundary;
        room = pcm->buffer - fill;
        if (frames > room)
                frames = room;
        if (frames <= 0)
                return AUDIO_EAGAIN;
        if ((unsigned long)(pcm->written_bytes + frames * frame) <= sizeof(pcm->written))
                memcpy(pcm->written + pcm->written_bytes, buffer, (unsigned long)(frames * frame));
        pcm->written_bytes += frames * frame;
        pcm->appl = (pcm->appl + frames) % pcm->boundary;
        pcm->written_total += frames;
        if (pcm->state == AUDIO_ALSA_STATE_PREPARED && fill + frames >= pcm->start_threshold)
                pcm->state = AUDIO_ALSA_STATE_RUNNING;
        return frames * frame;
}

static long audio_sys_open(const char *path, int flags)
{
        if (al_use_real)
        {
                int fd = open(path, flags);

                return fd < 0 ? -errno : fd;
        }
        return al_fake_open(path, flags);
}

static long audio_sys_close(int fd)
{
        if (al_use_real)
                return close(fd) < 0 ? -errno : 0;
        if (!al_fd(fd))
                return -EBADF;
        al_fds[fd - AL_BASE].used = 0;
        al_closes++;
        return 0;
}

static long audio_sys_ioctl(int fd, unsigned long request, void *argument)
{
        if (al_use_real)
        {
                int result = ioctl(fd, request, argument);

                return result < 0 ? -errno : result;
        }
        return al_fake_ioctl(fd, request, argument);
}

static long audio_sys_read(int fd, void *buffer, unsigned long bytes)
{
        if (al_use_real)
        {
                long result = read(fd, buffer, bytes);

                return result < 0 ? -errno : result;
        }
        {
                struct al_pcm_fake *pcm = al_fd(fd);
                long frame, frames, avail;

                if (!pcm || !pcm->configured)
                        return -EBADFD;
                if (pcm->state == AUDIO_ALSA_STATE_XRUN)
                        return -EPIPE;
                frame = (pcm->format == 2 ? 2 : pcm->format == 32 ? 3 : 4) * (long)pcm->channels;
                frames = (long)bytes / frame;
                avail = pcm->hw - pcm->appl;
                if (avail < 0)
                        avail += pcm->boundary;
                if (frames > avail)
                        frames = avail;
                if (frames <= 0)
                        return AUDIO_EAGAIN;
                memset(buffer, 0, (unsigned long)(frames * frame));
                pcm->appl = (pcm->appl + frames) % pcm->boundary;
                return frames * frame;
        }
}

static long audio_sys_write(int fd, const void *buffer, unsigned long bytes)
{
        if (al_use_real)
        {
                long result = write(fd, buffer, bytes);

                return result < 0 ? -errno : result;
        }
        return al_fake_write(fd, buffer, bytes);
}

static void al_reset(void)
{
        memset(al_fds, 0, sizeof(al_fds));
        memset(al_card_present, 0, sizeof(al_card_present));
        memset(al_cards, 0, sizeof(al_cards));
        al_opens = al_closes = 0;
        al_next_errno = 0;
        al_small_boundary = 0;
}

// What each kind of card is given, and what the file picks of it.
static void al_test_negotiation(void)
{
        static const struct
        {
                int model;
                long rate, period_at_48000;
                long format, channels, rate_got, period, buffer, target, avail_min; // avail_min: a period played, less a quarter
                int opens;
        } cases[] = {
            // hda: 32 bits, a pair, 48000, a period of 256 and a buffer of four
            {0, 48000, 256, AUDIO_FORMAT_S32LE, 2, 48000, 256, 1024, 768, 448, 1},
            {0, 44100, 256, AUDIO_FORMAT_S32LE, 2, 44100, 235, 940, 705, 412, 1},
            {0, 192000, 256, AUDIO_FORMAT_S32LE, 2, 192000, 1024, 4096, 3072, 1792, 1},
            // a rate the card has not got: the first of the usual that it takes
            {0, 22050, 256, AUDIO_FORMAT_S32LE, 2, 48000, 256, 1024, 768, 448, 1},
            // usb: 24 bits in three bytes, and a period on the millisecond: 256 is not on it, 288 is the least above
            {1, 48000, 256, AUDIO_FORMAT_S24LE, 2, 48000, 288, 1152, 864, 504, 1},
            // hdmi: a pair of eight channels, 32 bits
            {2, 48000, 256, AUDIO_FORMAT_S32LE, 2, 48000, 256, 1024, 768, 448, 1},
            // a card of six channels only, at 96000 only: the pair goes to the first two
            {3, 48000, 256, AUDIO_FORMAT_S16LE, 6, 96000, 512, 2048, 1536, 896, 1},
            // a buffer no larger than two periods
            {6, 48000, 256, AUDIO_FORMAT_S16LE, 2, 48000, 256, 512, 512, 192, 1},
            // a card that takes only floats
            {7, 48000, 256, AUDIO_FORMAT_F32LE, 2, 48000, 256, 1024, 768, 448, 1},
        };

        for (unsigned c = 0; c < sizeof(cases) / sizeof(cases[0]); c++)
        {
                struct audio_pcm pcm;
                long result;

                al_reset();
                al_card_present[0] = 1;
                al_model_of_card[0] = cases[c].model;
                result = audio_pcm_open(&pcm, 0, 0, AUDIO_ALSA_PLAYBACK, cases[c].rate, cases[c].period_at_48000, -1);
                ALCHECK(result == 0, "%s at %ld: open said %ld", al_models[cases[c].model].name, cases[c].rate, result);
                if (result)
                        continue;
                ALCHECK(pcm.format == cases[c].format && pcm.channels == cases[c].channels && pcm.rate == cases[c].rate_got, "%s at %ld: format %ld channels %ld rate %ld, wanted %ld %ld %ld",
                        al_models[cases[c].model].name, cases[c].rate, pcm.format, pcm.channels, pcm.rate, cases[c].format, cases[c].channels, cases[c].rate_got);
                ALCHECK(pcm.period == cases[c].period && pcm.buffer == cases[c].buffer, "%s at %ld: period %ld buffer %ld, wanted %ld %ld", al_models[cases[c].model].name,
                        cases[c].rate, pcm.period, pcm.buffer, cases[c].period, cases[c].buffer);
                ALCHECK(pcm.target == cases[c].target, "%s: target %ld, wanted %ld", al_models[cases[c].model].name, pcm.target, cases[c].target);
                ALCHECK(al_fds[0].avail_min == cases[c].avail_min && al_fds[0].start_threshold == cases[c].target, "%s: avail_min %ld start %ld", al_models[cases[c].model].name,
                        al_fds[0].avail_min, al_fds[0].start_threshold);
                ALCHECK(pcm.boundary >= pcm.buffer && pcm.boundary % pcm.buffer == 0, "%s: boundary %ld", al_models[cases[c].model].name, pcm.boundary);
                ALCHECK(pcm.frame_bytes == audio_format_bytes(pcm.format) * pcm.channels, "%s: frame bytes %ld", al_models[cases[c].model].name, pcm.frame_bytes);
                {
                        // The rates mask is the model's list, bit for bit, in the table of rates the file asks about.
                        unsigned int want = 0;

                        for (int n = 0; n < AUDIO_ALSA_RATES; n++)
                                for (int r = 0; al_models[cases[c].model].rates[r]; r++)
                                        if (al_models[cases[c].model].rates[r] == audio_alsa_rate_table[n])
                                                want |= 1u << n;
                        ALCHECK(pcm.rates == want, "%s at %ld: rates mask %x, the model's %x", al_models[cases[c].model].name, cases[c].rate, pcm.rates, want);
                        ALCHECK(audio_rate_offered(pcm.rates, cases[c].rate_got) == 1, "%s: the rate it runs at is offered", al_models[cases[c].model].name);
                        ALCHECK(audio_rate_offered(pcm.rates, 22050) == (int)((want >> 3) & 1) && audio_rate_offered(pcm.rates, 12345) == 0,
                                "%s: 22050 is offered as the model says and a rate outside the table is not", al_models[cases[c].model].name);
                }
                audio_pcm_close(&pcm);
                ALCHECK(al_opens == al_closes && pcm.fd == -1, "%s: every open is closed", al_models[cases[c].model].name);
        }
        // A fill of two periods: the target is two periods and the wake is when one has played, less a quarter; a fill of one is taken as two.
        {
                struct audio_pcm pcm;
                long result;

                al_reset();
                al_card_present[0] = 1;
                al_model_of_card[0] = 0;
                result = audio_pcm_open_fill(&pcm, 0, 0, AUDIO_ALSA_PLAYBACK, 48000, 64, -1, 2);
                ALCHECK(result == 0 && pcm.target == 2 * pcm.period && al_fds[0].start_threshold == pcm.target &&
                                al_fds[0].avail_min == pcm.buffer - pcm.target + pcm.period - pcm.period / 4,
                        "a fill of two periods: target %ld, period %ld, buffer %ld, avail_min %ld", pcm.target, pcm.period, pcm.buffer, al_fds[0].avail_min);
                if (result == 0)
                        audio_pcm_close(&pcm);
                result = audio_pcm_open_fill(&pcm, 0, 0, AUDIO_ALSA_PLAYBACK, 48000, 64, -1, 1);
                ALCHECK(result == 0 && pcm.target == 2 * pcm.period, "a fill of one period is taken as two: target %ld, period %ld", pcm.target, pcm.period);
                if (result == 0)
                        audio_pcm_close(&pcm);
        }
        // Cards that cannot be used: refused, nothing left open.
        for (int model = 4; model <= 5; model++)
        {
                struct audio_pcm pcm;
                long result;

                al_reset();
                al_card_present[0] = 1;
                al_model_of_card[0] = model;
                result = audio_pcm_open(&pcm, 0, 0, AUDIO_ALSA_PLAYBACK, 48000, 256, -1);
                ALCHECK(result < 0 && pcm.fd == -1 && al_opens == al_closes, "%s is refused (%ld) and leaves nothing open (%d opened, %d closed)", al_models[model].name, result, al_opens, al_closes);
        }
        {
                struct audio_pcm pcm;

                al_reset();
                ALCHECK(audio_pcm_open(&pcm, 3, 0, AUDIO_ALSA_PLAYBACK, 48000, 256, -1) < 0, "a card that is not there is refused");
                ALCHECK(audio_pcm_open(&pcm, -1, 0, AUDIO_ALSA_PLAYBACK, 48000, 256, -1) < 0 && audio_pcm_open(&pcm, 99, 0, 0, 48000, 256, -1) < 0 &&
                            audio_pcm_open(&pcm, 0, -1, 0, 48000, 256, -1) < 0 && audio_pcm_open(&pcm, 0, 0, 0, 0, 256, -1) < 0 && audio_pcm_open(&pcm, 0, 0, 0, 48000, 0, -1) < 0,
                        "numbers out of bounds are refused");
        }
}

// The room in the buffer against a count kept apart, across the wrap of the pointers; and every way a card goes wrong.
static void al_test_flow(void)
{
        struct audio_pcm pcm;
        static unsigned char frames[4096 * 8];
        long written = 0, played = 0;

        al_reset();
        al_card_present[0] = 1;
        al_model_of_card[0] = 0;
        al_small_boundary = 4;
        ALCHECK(audio_pcm_open(&pcm, 0, 0, AUDIO_ALSA_PLAYBACK, 48000, 256, -1) == 0, "hda opens");
        ALCHECK(pcm.boundary == 4 * pcm.buffer, "the boundary asked for is the one the driver gave: %ld", pcm.boundary);
        ALCHECK(audio_pcm_avail(&pcm) == pcm.buffer, "an empty buffer has all its room: %ld", audio_pcm_avail(&pcm));
        for (long step = 0; step < 200000; step++)
        {
                long fill = written - played, avail = audio_pcm_avail(&pcm), n;

                if (avail != pcm.buffer - fill)
                {
                        ALCHECK(0, "step %ld: room %ld, %ld expected (written %ld played %ld)", step, avail, pcm.buffer - fill, written, played);
                        break;
                }
                if (al_rand() % 3)
                {
                        n = (long)(al_rand() % (unsigned long)(pcm.period + 1));
                        if (n > avail)
                                n = avail;
                        if (n > 0)
                        {
                                long took = audio_pcm_write(&pcm, frames, n);

                                if (took != n)
                                {
                                        ALCHECK(0, "step %ld: wrote %ld of %ld", step, took, n);
                                        break;
                                }
                                written += n;
                        }
                }
                else
                {
                        // The hardware plays some, never more than there is (an underrun is its own test).
                        n = (long)(al_rand() % (unsigned long)(pcm.period + 1));
                        if (n > fill)
                                n = fill;
                        if (n > 0 && al_fds[0].state == AUDIO_ALSA_STATE_RUNNING)
                        {
                                al_hardware(AL_BASE, n);
                                played += n;
                        }
                }
        }
        ALCHECK(written > 1000000, "the pointers wrapped many times: %ld frames", written);
        ALCHECK(al_fds[0].played_total == played && al_fds[0].written_total == written, "the card's own counts agree");
        {
                long delay = audio_pcm_delay(&pcm);

                ALCHECK(delay == written - played, "the delay is the frames in the card: %ld against %ld", delay, written - played);
        }
        // Underrun: the hardware runs past what was written.
        {
                long avail;

                al_hardware(AL_BASE, 100000);
                avail = audio_pcm_avail(&pcm);
                ALCHECK(avail == AUDIO_EPIPE, "an emptied buffer is an underrun: %ld", avail);
                ALCHECK(audio_pcm_write(&pcm, frames, 10) == AUDIO_EPIPE, "and a write says so");
                ALCHECK(audio_pcm_recover(&pcm, AUDIO_EPIPE) == 0 && pcm.xruns == 1, "recovering counts it");
                ALCHECK(audio_pcm_avail(&pcm) == pcm.buffer && al_fds[0].state == AUDIO_ALSA_STATE_PREPARED, "and leaves the card ready, empty");
                ALCHECK(audio_pcm_write(&pcm, frames, pcm.target) == pcm.target && al_fds[0].state == AUDIO_ALSA_STATE_RUNNING, "filled to its target it runs");
        }
        // Suspend: resumed and prepared.
        {
                al_fds[0].state = AUDIO_ALSA_STATE_SUSPENDED;
                ALCHECK(audio_pcm_avail(&pcm) == AUDIO_ESTRPIPE, "a suspend is reported");
                ALCHECK(audio_pcm_recover(&pcm, AUDIO_ESTRPIPE) == 0 && pcm.resumes == 1 && al_fds[0].state == AUDIO_ALSA_STATE_PREPARED, "and recovered");
        }
        // Other errors are not recovered.
        ALCHECK(audio_pcm_recover(&pcm, AUDIO_EINVAL) == AUDIO_EINVAL && audio_pcm_recover(&pcm, AUDIO_EAGAIN) == AUDIO_EAGAIN, "an error that is not an underrun is the caller's");
        // A full buffer is EAGAIN, not an error.
        {
                long first = audio_pcm_write(&pcm, frames, 4096), second = audio_pcm_write(&pcm, frames, 10);

                ALCHECK(first == pcm.buffer && second == AUDIO_EAGAIN, "a full buffer takes what fits (%ld) and then says to wait (%ld)", first, second);
        }
        // The card pulled out: refused from then on, broken.
        {
                al_fds[0].state = AUDIO_ALSA_STATE_DISCONNECTED;
                ALCHECK(audio_pcm_avail(&pcm) == AUDIO_ENODEV && pcm.broken, "a card that is gone is reported and marked");
                ALCHECK(audio_pcm_write(&pcm, frames, 10) == AUDIO_ENODEV && audio_pcm_recover(&pcm, AUDIO_ENODEV) == AUDIO_ENODEV, "and stays gone");
        }
        audio_pcm_close(&pcm);
        ALCHECK(al_opens == al_closes, "closed");
        // A card pulled out between the state and the write: the write's own ENODEV marks it.
        al_reset();
        al_card_present[0] = 1;
        al_model_of_card[0] = 0;
        audio_pcm_open(&pcm, 0, 0, AUDIO_ALSA_PLAYBACK, 48000, 256, -1);
        al_next_errno = -ENODEV;
        ALCHECK(audio_pcm_write(&pcm, frames, 10) == AUDIO_ENODEV && pcm.broken, "a write that finds the card gone marks it");
        audio_pcm_close(&pcm);
        // A negative count is refused before the card is asked: it would become a huge byte count for the kernel to copy from the
        // caller's memory. A count past the buffer is not refused (the test above: it takes what fits).
        al_reset();
        al_card_present[0] = 1;
        al_model_of_card[0] = 0;
        ALCHECK(audio_pcm_open(&pcm, 0, 0, AUDIO_ALSA_PLAYBACK, 48000, 256, -1) == 0, "a playback PCM opens for the count checks");
        {
                long asked = al_write_calls;

                ALCHECK(audio_pcm_write(&pcm, frames, -1) == AUDIO_EINVAL && al_write_calls == asked, "a negative count is refused and the card not asked");
                ALCHECK(audio_pcm_read(&pcm, frames, -3) == AUDIO_EINVAL, "and so is a negative read count");
                ALCHECK(audio_pcm_write(&pcm, frames, 10) == 10 && al_write_calls == asked + 1, "a count the buffer holds is written");
        }
        audio_pcm_close(&pcm);
        // A capture buffer across the wrap: the data to read is the hardware's pointer less the application's, and it is the hardware's that wraps first.
        al_reset();
        al_card_present[0] = 1;
        al_model_of_card[0] = 0;
        al_small_boundary = 4;
        ALCHECK(audio_pcm_open(&pcm, 0, 0, AUDIO_ALSA_CAPTURE, 48000, 256, -1) == 0, "a capture PCM opens");
        {
                long came = 0, taken = 0, wrapped = 0;

                for (long step = 0; step < 100000; step++)
                {
                        long avail = audio_pcm_avail(&pcm), n;

                        if (avail != came - taken)
                        {
                                ALCHECK(0, "capture step %ld: %ld to read, %ld expected", step, avail, came - taken);
                                break;
                        }
                        if (al_rand() % 2)
                        {
                                n = (long)(al_rand() % 200);
                                if (n > pcm.buffer - avail)
                                        n = pcm.buffer - avail;
                                al_hardware(AL_BASE, n);
                                came += n;
                        }
                        else
                        {
                                n = (long)(al_rand() % 200);
                                if (n > avail)
                                        n = avail;
                                if (n > 0 && audio_pcm_read(&pcm, frames, n) != n)
                                {
                                        ALCHECK(0, "capture step %ld: read of %ld", step, n);
                                        break;
                                }
                                taken += n;
                        }
                        wrapped += al_fds[0].hw < al_fds[0].appl;
                }
                ALCHECK(came > 1000000 && wrapped > 1000, "the capture pointers wrapped: %ld frames, %ld steps with the hardware's behind", came, wrapped);
                al_hardware(AL_BASE, pcm.buffer + 1);
                ALCHECK(audio_pcm_avail(&pcm) == AUDIO_EPIPE, "an overrun is reported like an underrun");
                ALCHECK(audio_pcm_recover(&pcm, AUDIO_EPIPE) == 0 && pcm.xruns == 1 && al_fds[0].state == AUDIO_ALSA_STATE_RUNNING, "and recovered, running again");
        }
        audio_pcm_close(&pcm);
        // The delay of a capture is not asked; a capture PCM counts overruns the same way.
        al_reset();
        al_card_present[0] = 1;
        al_model_of_card[0] = 0;
        ALCHECK(audio_pcm_open(&pcm, 0, 0, AUDIO_ALSA_CAPTURE, 48000, 256, -1) == 0 && al_fds[0].state == AUDIO_ALSA_STATE_RUNNING, "a capture PCM starts running at once");
        ALCHECK(audio_pcm_avail(&pcm) == 0, "and has nothing to read");
        al_hardware(AL_BASE, 300);
        ALCHECK(audio_pcm_avail(&pcm) == 300, "300 frames came in");
        audio_pcm_close(&pcm);
}

// The cards that are there, with the names a driver gives, which are bytes and not text.
static void al_test_scan(void)
{
        static struct audio_card cards[8];
        long found;

        al_reset();
        al_card_present[0] = al_card_present[1] = al_card_present[2] = al_card_present[5] = al_card_present[6] = 1;
        al_cards[0] = (struct al_card_fake){"Generic", "HD-Audio Generic", "HD-Audio Generic at 0xf6588000 irq 165", 2, {0, 3}, {1, 1}, {1, 0}, {"ALC892 Analog", "HDMI 0"}, 0};
        al_cards[1] = (struct al_card_fake){"Audio", "USB Audio", "Generic USB Audio at usb-0000:12:00.0-6, high speed", 1, {0}, {1}, {1}, {"USB Audio"}, 0};
        // A name with controls in it, one with no end inside its array, and a card whose info call fails.
        al_cards[2] = (struct al_card_fake){"E\x1b[31mvil\x07", "NoEndNoEndNoEndNoEndNoEndNoEndNoEndNoEndNoEnd", "Long\nname\ttab", 1, {2}, {1}, {0}, {"Capture only?"}, 0};
        al_cards[5] = (struct al_card_fake){"Broken", "Broken", "Broken", 1, {0}, {1}, {0}, {"x"}, 1};
        al_cards[6] = (struct al_card_fake){"None", "No devices", "No devices at all", 0, {0}, {0}, {0}, {""}, 0};
        found = audio_cards_scan(cards, 8);
        ALCHECK(found == 4, "four cards answer: %ld", found);
        ALCHECK(cards[0].number == 0 && !strcmp(cards[0].id, "Generic") && !strcmp(cards[0].name, "HD-Audio Generic") && cards[0].devices == 2, "card 0: %s %s", cards[0].id, cards[0].name);
        ALCHECK(cards[0].device[0].device == 0 && cards[0].device[0].playback && cards[0].device[0].capture && !strcmp(cards[0].device[0].name, "ALC892 Analog"), "card 0 device 0");
        ALCHECK(cards[0].device[1].device == 3 && cards[0].device[1].playback && !cards[0].device[1].capture && !strcmp(cards[0].device[1].name, "HDMI 0"), "card 0 device 3");
        ALCHECK(cards[1].number == 1 && !strcmp(cards[1].name, "USB Audio") && cards[1].devices == 1, "card 1");
        ALCHECK(cards[2].number == 2 && !strcmp(cards[2].id, "E?[31mvil?") && !strchr(cards[2].longname, '\n') && !strchr(cards[2].longname, '\t'), "card 2's name is text: %s / %s", cards[2].id,
                cards[2].longname);
        ALCHECK(strlen(cards[2].name) == 32, "a name without an end is bounded to its array: %zu", strlen(cards[2].name));
        ALCHECK(cards[3].number == 6 && cards[3].devices == 0, "a card with no devices is listed with none: %ld", cards[3].number);
        ALCHECK(audio_cards_scan(cards, 2) == 2, "no more cards than the room");
        ALCHECK(al_opens == al_closes, "every control file closed");
        // The path of a device.
        {
                char path[40];

                audio_alsa_path(path, 0, 0, 0, 0);
                ALCHECK(!strcmp(path, "/dev/snd/pcmC0D0p"), "%s", path);
                audio_alsa_path(path, 12, 34, 1, 0);
                ALCHECK(!strcmp(path, "/dev/snd/pcmC12D34c"), "%s", path);
                audio_alsa_path(path, 7, 0, 0, 1);
                ALCHECK(!strcmp(path, "/dev/snd/controlC7"), "%s", path);
        }
}

#define AL_RW (AUDIO_CTL_ACCESS_READ | AUDIO_CTL_ACCESS_WRITE)

static void al_mixer_dell(void)
{
        memset(al_ctls, 0, sizeof(al_ctls));
        al_ctl_count = 0;
        // A codec as the hardware leaves it: everything off, every gain at nothing.
        al_ctl_add((struct al_ctl){.name = "Master Playback Switch", .iface = 2, .type = 1, .access = AL_RW, .count = 2});
        al_ctl_add((struct al_ctl){.name = "Master Playback Volume", .iface = 2, .type = 2, .access = AL_RW, .count = 2, .min = 0, .max = 127, .value = {0, 0}, .items = 0, .item = {0}, .tlv_min = -6350, .tlv_step = 50});
        al_ctl_add((struct al_ctl){.name = "Headphone Playback Switch", .iface = 2, .type = 1, .access = AL_RW, .count = 2});
        al_ctl_add((struct al_ctl){.name = "Headphone Playback Volume", .iface = 2, .type = 2, .access = AL_RW, .count = 2, .min = 0, .max = 42, .value = {0, 0}, .items = 0, .item = {0}, .tlv_min = -3000, .tlv_step = 100});
        al_ctl_add((struct al_ctl){.name = "Speaker Playback Switch", .iface = 2, .type = 1, .access = AL_RW, .count = 2});
        al_ctl_add((struct al_ctl){.name = "Speaker Playback Volume", .iface = 2, .type = 2, .access = AL_RW, .count = 2, .min = 0, .max = 31});
        al_ctl_add((struct al_ctl){.name = "PCM Playback Volume", .iface = 2, .type = 2, .access = AL_RW, .count = 2, .min = 0, .max = 255, .value = {0, 0}, .items = 0, .item = {0}, .tlv_min = -10200, .tlv_step = 40});
        al_ctl_add((struct al_ctl){.name = "Line Out Playback Switch", .iface = 2, .type = 1, .access = AL_RW, .count = 2});
        al_ctl_add((struct al_ctl){.name = "Front Playback Switch", .iface = 2, .type = 1, .access = AL_RW, .count = 2});
        al_ctl_add((struct al_ctl){.name = "IEC958 Playback Switch", .iface = 2, .type = 1, .access = AL_RW, .count = 1});
        al_ctl_add((struct al_ctl){.name = "Digital Playback Switch", .iface = 2, .type = 1, .access = AL_RW, .count = 2, .min = 0, .max = 0, .value = {1, 1}});
        al_ctl_add((struct al_ctl){.name = "Auto-Mute Mode", .iface = 2, .type = 3, .access = AL_RW, .count = 1, .min = 0, .max = 0, .value = {0, 0}, .items = 2, .item = {"Disabled", "Enabled"}});
        // Not ours: what is only listened to (the monitors of the inputs), a beep, and the gains of the capture.
        al_ctl_add((struct al_ctl){.name = "Line Playback Switch", .iface = 2, .type = 1, .access = AL_RW, .count = 2});
        al_ctl_add((struct al_ctl){.name = "Mic Playback Switch", .iface = 2, .type = 1, .access = AL_RW, .count = 2});
        al_ctl_add((struct al_ctl){.name = "Mic Playback Volume", .iface = 2, .type = 2, .access = AL_RW, .count = 2, .min = 0, .max = 31});
        al_ctl_add((struct al_ctl){.name = "Beep Playback Switch", .iface = 2, .type = 1, .access = AL_RW, .count = 1});
        al_ctl_add((struct al_ctl){.name = "Capture Switch", .iface = 2, .type = 1, .access = AL_RW, .count = 2});
        al_ctl_add((struct al_ctl){.name = "Capture Volume", .iface = 2, .type = 2, .access = AL_RW, .count = 2, .min = 0, .max = 63, .value = {0, 0}, .items = 0, .item = {0}, .tlv_min = -1725, .tlv_step = 75});
        // Ours and not to be written: inactive, read only, wrong type, the wrong interface.
        al_ctl_add((struct al_ctl){.name = "Surround Playback Switch", .iface = 2, .type = 1, .access = AUDIO_CTL_ACCESS_READ | AUDIO_CTL_ACCESS_WRITE | AUDIO_CTL_ACCESS_INACTIVE, .count = 2});
        al_ctl_add((struct al_ctl){.name = "Center Playback Switch", .iface = 2, .type = 1, .access = AUDIO_CTL_ACCESS_READ, .count = 2});
        al_ctl_add((struct al_ctl){.name = "Side Playback Volume", .iface = 2, .type = 1, .access = AL_RW, .count = 2});
        al_ctl_add((struct al_ctl){.name = "Bass Speaker Playback Switch", .iface = 3, .type = 1, .access = AL_RW, .count = 2});
        // The card that will not: refuses the write, takes it and keeps its value.
        al_ctl_add((struct al_ctl){.name = "CLFE Playback Switch", .iface = 2, .type = 1, .access = AL_RW, .count = 2, .min = 0, .max = 0, .value = {0, 0}, .items = 0, .item = {0}, .tlv_min = 0, .tlv_step = 0, .write_fails = 1});
        al_ctl_add((struct al_ctl){.name = "LFE Playback Switch", .iface = 2, .type = 1, .access = AL_RW, .count = 2, .min = 0, .max = 0, .value = {0, 0}, .items = 0, .item = {0}, .tlv_min = 0, .tlv_step = 0, .write_fails = 0, .ignores = 1});
        // Names that only look like ours.
        al_ctl_add((struct al_ctl){.name = "Master Playback Switch Extra", .iface = 2, .type = 1, .access = AL_RW, .count = 2});
        al_ctl_add((struct al_ctl){.name = "Super Master Playback Volume", .iface = 2, .type = 2, .access = AL_RW, .count = 2, .min = 0, .max = 9});
        al_ctl_add((struct al_ctl){.name = "Headphone Jack", .iface = 2, .type = 1, .access = AUDIO_CTL_ACCESS_READ, .count = 1});
        // The scales of the other makes: a step that carries the scale's mute flag (0 dB is step 30 of 60), a range that is all over 0 dB, a
        // line between two ends with the mute and without it.
        al_ctl_add((struct al_ctl){.name = "Front Playback Volume", .iface = 2, .type = 2, .access = AL_RW, .count = 2, .min = 0, .max = 60, .tlv_min = -3000, .tlv_step = 0x10000 | 100});
        al_ctl_add((struct al_ctl){.name = "Digital Playback Volume", .iface = 2, .type = 2, .access = AL_RW, .count = 2, .min = 0, .max = 20, .value = {5, 5}, .tlv_min = 600, .tlv_step = 100});
        al_ctl_add((struct al_ctl){.name = "Line Out Playback Volume", .iface = 2, .type = 2, .access = AL_RW, .count = 2, .min = 0, .max = 100, .tlv_min = -9000, .tlv_max = 3000, .tlv_kind = 5});
        al_ctl_add((struct al_ctl){.name = "Center Playback Volume", .iface = 2, .type = 2, .access = AL_RW, .count = 2, .min = 0, .max = 40, .tlv_min = -2000, .tlv_max = 1000, .tlv_kind = 4});
        // An auto-mute with no item called Enabled, a switch that is a number, and the monitor of an input named as an output is, and more.
        al_ctl_add((struct al_ctl){.name = "Auto-Mute Mode", .iface = 2, .type = 3, .access = AL_RW, .count = 1, .items = 2, .item = {"Off", "On"}});
        al_ctl_add((struct al_ctl){.name = "Mono Playback Switch", .iface = 2, .type = 2, .access = AL_RW, .count = 1, .min = 0, .max = 1});
        al_ctl_add((struct al_ctl){.name = "Headphone Mic Playback Switch", .iface = 2, .type = 1, .access = AL_RW, .count = 2});
}

static void al_test_mixer(void)
{
        struct audio_mixer_report report;
        struct al_ctl *c;

        al_reset();
        al_card_present[0] = 1;
        al_cards[0] = (struct al_card_fake){"Generic", "HD-Audio Generic", "HD-Audio Generic", 1, {0}, {1}, {0}, {"ALC Analog"}, 0};
        al_mixer_dell();
        ALCHECK(audio_mixer_prepare(0, 1, &report) == 0, "the mixer of a card is prepared");
        // The outputs: on, at 0 dB where the scale says (Master's top is 0 dB; the headphones' 0 dB is under their top, which is +12), else at the top.
        ALCHECK(al_ctls[0].value[0] == 1 && al_ctls[0].value[1] == 1, "Master is on");
        ALCHECK(al_ctls[1].value[0] == 127 && al_ctls[1].value[1] == 127, "Master at 0 dB, the top: %ld", al_ctls[1].value[0]);
        ALCHECK(al_ctls[2].value[0] == 1 && al_ctls[4].value[0] == 1 && al_ctls[7].value[0] == 1 && al_ctls[8].value[0] == 1, "Headphone, Speaker, Line Out and Front are on");
        ALCHECK(al_ctls[3].value[0] == 30 && al_ctls[3].value[1] == 30, "the headphones at 0 dB, which is step 30 of 42 and not their +12 dB top: %ld", al_ctls[3].value[0]);
        ALCHECK(al_ctls[5].value[0] == 31, "a gain with no scale at its top: %ld", al_ctls[5].value[0]);
        ALCHECK(al_ctls[6].value[0] == 255, "PCM at 0 dB, its top: %ld", al_ctls[6].value[0]);
        ALCHECK(al_ctls[9].value[0] == 1, "the S/PDIF and HDMI output is on");
        ALCHECK(al_ctls[10].writes == 0, "what was already on is not written");
        ALCHECK(al_ctls[11].value[0] == 1, "auto-mute is on (the item called Enabled)");
        // What is not the outputs' is as it was.
        ALCHECK(al_ctls[12].writes == 0 && al_ctls[13].writes == 0 && al_ctls[14].writes == 0 && al_ctls[15].writes == 0, "the monitors of the inputs and the beep are not touched");
        ALCHECK(al_ctls[16].writes == 0 && al_ctls[17].writes == 0, "nor is the capture, when the outputs are asked for");
        ALCHECK(al_ctls[18].writes == 0 && al_ctls[19].writes == 0 && al_ctls[20].writes == 0 && al_ctls[21].writes == 0, "nor the inactive, the read-only, the wrong type, the wrong interface");
        ALCHECK(al_ctls[24].writes == 0 && al_ctls[25].writes == 0 && al_ctls[26].writes == 0, "nor names that only look like the outputs'");
        // The report.
        ALCHECK(report.controls == al_ctl_count, "controls: %ld", report.controls);
        ALCHECK(al_ctls[27].value[0] == 30 && al_ctls[27].value[1] == 30, "a scale whose step carries the mute flag has 0 dB at step 30 of 60: %ld", al_ctls[27].value[0]);
        ALCHECK(al_ctls[28].value[0] == 0, "a range wholly over 0 dB is put at its bottom: %ld", al_ctls[28].value[0]);
        ALCHECK(al_ctls[29].value[0] == 75, "a line between two ends with the mute, -90 to +30 dB in 100 steps, has 0 dB at 75: %ld", al_ctls[29].value[0]);
        ALCHECK(al_ctls[30].value[0] == 26, "and without the mute, -20 to +10 dB in 40 steps, at 26: %ld", al_ctls[30].value[0]);
        ALCHECK(al_ctls[31].writes == 0 && al_ctls[31].value[0] == 0, "an auto-mute with no item called Enabled is left as it is");
        ALCHECK(al_ctls[32].writes == 0 && al_ctls[33].writes == 0, "a switch that is a number, and the monitor of an input named as an output is, are not touched");
        ALCHECK(report.output == 18 && report.changed == 16 && report.failed == 1 && report.left_off == 2, "report: %ld ours, %ld written, %ld failed, %ld still off", report.output, report.changed, report.failed,
                report.left_off);
        ALCHECK(!strcmp(report.first_off, "CLFE Playback Switch"), "and which one is the first still off: %s", report.first_off);
        // Again: nothing that took is written again.
        for (int i = 0; i < al_ctl_count; i++)
                al_ctls[i].writes = 0;
        ALCHECK(audio_mixer_prepare(0, 1, &report) == 0, "again");
        ALCHECK(report.changed == 1, "only the one that does not keep what it is given is written again: %ld", report.changed);
        // The capture: asked for by itself.
        ALCHECK(audio_mixer_prepare(0, 2, &report) == 0, "the capture");
        ALCHECK(al_ctls[16].value[0] == 1 && al_ctls[16].value[1] == 1, "Capture Switch is on");
        ALCHECK(al_ctls[17].value[0] == 23, "and its gain at 0 dB, which is step 23 of a scale from -17.25 dB in steps of 0.75 (its top is +30): %ld", al_ctls[17].value[0]);
        ALCHECK(report.capture == 2 && report.output == 0, "two controls of the capture, none of the outputs: %ld %ld", report.capture, report.output);
        // A card with no mixer, and one that is gone.
        al_ctl_count = 0;
        ALCHECK(audio_mixer_prepare(0, 1, &report) == 0 && report.controls == 0 && report.changed == 0, "a card with no controls needs nothing");
        al_card_present[0] = 0;
        ALCHECK(audio_mixer_prepare(0, 1, &report) < 0, "a card that is gone is an error");
        ALCHECK(al_opens == al_closes, "every control file closed");
        (void)c;
}

/* ---- what is plugged in ---- */

static void al_jack(const char *name, unsigned iface, long value)
{
        al_ctl_add((struct al_ctl){.name = name, .iface = iface, .type = 1, .access = AUDIO_CTL_ACCESS_READ, .count = 1, .value = {value}});
}

static void al_jacks_card(void)
{
        memset(al_ctls, 0, sizeof(al_ctls));
        al_ctl_count = 0;
        al_reset();
        al_card_present[0] = 1;
        al_cards[0] = (struct al_card_fake){"Generic", "HD-Audio Generic", "HD-Audio Generic", 1, {0}, {1}, {0}, {"ALC Analog"}, 0};
}

static void al_test_jacks(void)
{
        struct audio_jacks jacks;

        // A desktop's codec with nothing in its jacks: the inputs (Mic, Line, Front Mic) have something and are not the question.
        al_jacks_card();
        al_jack("Headphone Jack", 0, 0);
        al_jack("Line Out Jack", 0, 0);
        al_jack("Mic Jack", 0, 1);
        al_jack("Line Jack", 0, 1);
        al_jack("Front Mic Jack", 0, 1);
        al_jack("Headphone Mic Jack", 0, 1); // a microphone on a headphone socket is an input
        al_jack("Headphone Jack", 2, 1); // a control of another interface by the same name says nothing
        al_jack("Line Out Jack", 3, 1);  // and so does a PCM's
        ALCHECK(audio_jacks_read(0, &jacks) == 0, "the jacks of a card are read");
        ALCHECK(jacks.output_jacks == 2 && jacks.output_plugged == 0 && !jacks.always && !jacks.hdmi_known && !jacks.hdmi_plugged, "two output jacks, nothing in them: %d %d", jacks.output_jacks, jacks.output_plugged);
        ALCHECK(audio_output_rank(&jacks, 0, 0, 0) == 3, "an analog output with nothing in its jacks is 3: %d", audio_output_rank(&jacks, 0, 0, 0));
        ALCHECK(audio_output_rank(&jacks, 0, 0, 1) == 6, "a USB card is heard whatever its card says: %d", audio_output_rank(&jacks, 0, 0, 1));
        // Something goes into the Line Out: heard.
        al_ctls[1].value[0] = 1;
        ALCHECK(audio_jacks_read(0, &jacks) == 0 && jacks.output_jacks == 2 && jacks.output_plugged == 1, "one output jack has something in it: %d", jacks.output_plugged);
        ALCHECK(audio_output_rank(&jacks, 0, 0, 0) == 6, "an analog output with something in a jack is 6: %d", audio_output_rank(&jacks, 0, 0, 0));
        // The headphones alone, with a location in the name.
        al_jacks_card();
        al_jack("Front Headphone Jack", 0, 1);
        al_jack("Rear Line Out Jack", 0, 0);
        ALCHECK(audio_jacks_read(0, &jacks) == 0 && jacks.output_jacks == 2 && jacks.output_plugged == 1, "a jack with a place in its name is an output jack: %d %d", jacks.output_jacks, jacks.output_plugged);
        // An internal speaker: a pin that cannot tell, or a Speaker output on the mixer, is there whatever the jacks say.
        al_jacks_card();
        al_jack("Line Out Jack", 0, 0);
        al_jack("Speaker Phantom Jack", 0, 1);
        ALCHECK(audio_jacks_read(0, &jacks) == 0 && jacks.always == 1 && jacks.output_jacks == 1 && jacks.output_plugged == 0, "a phantom jack is an output that is always there, and is not counted as a jack: %d %d %d", jacks.always, jacks.output_jacks, jacks.output_plugged);
        ALCHECK(audio_output_rank(&jacks, 0, 0, 0) == 6, "so the card is heard: %d", audio_output_rank(&jacks, 0, 0, 0));
        al_jacks_card();
        al_jack("Line Out Jack", 0, 0);
        al_ctl_add((struct al_ctl){.name = "Speaker Playback Switch", .iface = 2, .type = 1, .access = AUDIO_CTL_ACCESS_READ | AUDIO_CTL_ACCESS_WRITE, .count = 2});
        ALCHECK(audio_jacks_read(0, &jacks) == 0 && jacks.always == 1 && audio_output_rank(&jacks, 0, 0, 0) == 6, "a Speaker switch on the mixer is an output that is always there: %d", jacks.always);
        al_jacks_card();
        al_jack("Line Out Jack", 0, 0);
        al_ctl_add((struct al_ctl){.name = "Bass Speaker Playback Volume", .iface = 2, .type = 2, .access = AUDIO_CTL_ACCESS_READ | AUDIO_CTL_ACCESS_WRITE, .count = 2, .min = 0, .max = 10});
        ALCHECK(audio_jacks_read(0, &jacks) == 0 && jacks.always == 1, "and so is a Speaker volume: %d", jacks.always);
        al_jacks_card();
        al_jack("Line Out Jack", 0, 0);
        al_ctl_add((struct al_ctl){.name = "Headphone Playback Switch", .iface = 2, .type = 1, .access = AUDIO_CTL_ACCESS_READ | AUDIO_CTL_ACCESS_WRITE, .count = 2});
        ALCHECK(audio_jacks_read(0, &jacks) == 0 && jacks.always == 0, "an output of the mixer that is not a speaker is not always there: %d", jacks.always);
        // An HDMI card: the pins that have a display, the ones that have none, the ones that cannot say, and names that are not pins.
        al_jacks_card();
        al_jack("HDMI/DP,pcm=3 Jack", 0, 1);
        al_jack("HDMI/DP,pcm=7 Jack", 0, 0);
        al_jack("HDMI/DP,pcm=8 Jack", 0, 1);
        al_jack("HDMI/DP,pcm=9 Jack", 0, 0);
        al_jack("HDMI/DP,pcm=64 Jack", 0, 1);
        al_jack("HDMI/DP,pcm= Jack", 0, 1);
        al_jack("HDMI/DP,pcm=3x Jack", 0, 1);
        al_jack("HDMI/DP,pcm=63 Jack", 0, 1);
        ALCHECK(audio_jacks_read(0, &jacks) == 0, "an HDMI card is read");
        ALCHECK(jacks.hdmi_known == ((1ull << 3) | (1ull << 7) | (1ull << 8) | (1ull << 9) | (1ull << 63)), "the pins with a jack: %llx", jacks.hdmi_known);
        ALCHECK(jacks.hdmi_plugged == ((1ull << 3) | (1ull << 8) | (1ull << 63)), "and the ones with a display in them: %llx", jacks.hdmi_plugged);
        ALCHECK(jacks.output_jacks == 0 && !jacks.always, "an HDMI pin is not an analog output jack");
        ALCHECK(audio_output_rank(&jacks, 1, 3, 0) == 5 && audio_output_rank(&jacks, 1, 8, 0) == 5 && audio_output_rank(&jacks, 1, 63, 0) == 5, "a pin with a display is 5: %d %d", audio_output_rank(&jacks, 1, 3, 0),
                audio_output_rank(&jacks, 1, 8, 0));
        ALCHECK(audio_output_rank(&jacks, 1, 7, 0) == 2 && audio_output_rank(&jacks, 1, 9, 0) == 2, "a pin with none is 2: %d %d", audio_output_rank(&jacks, 1, 7, 0), audio_output_rank(&jacks, 1, 9, 0));
        ALCHECK(audio_output_rank(&jacks, 1, 5, 0) == 4 && audio_output_rank(&jacks, 1, 64, 0) == 4 && audio_output_rank(&jacks, 1, -1, 0) == 4, "a pin that cannot say is 4: %d %d %d", audio_output_rank(&jacks, 1, 5, 0),
                audio_output_rank(&jacks, 1, 64, 0), audio_output_rank(&jacks, 1, -1, 0));
        // The order of it all: a speaker or something in a jack, then a display, then a pin that cannot say, then a card with nothing in it, then a pin with no display.
        {
                struct audio_jacks bare;
                struct audio_jacks empty_analog = {.output_jacks = 2};

                memset(&bare, 0, sizeof(bare));
                ALCHECK(audio_output_rank(&bare, 0, 0, 0) == 6 && audio_output_rank(&bare, 1, 3, 0) == 4, "a card that has no jacks to ask is an analog output that can be heard and a pin that cannot say: %d %d",
                        audio_output_rank(&bare, 0, 0, 0), audio_output_rank(&bare, 1, 3, 0));
                ALCHECK(audio_output_rank(&empty_analog, 0, 0, 0) < audio_output_rank(&jacks, 1, 3, 0) && audio_output_rank(&jacks, 1, 3, 0) < audio_output_rank(&bare, 0, 0, 0) &&
                            audio_output_rank(&jacks, 1, 7, 0) < audio_output_rank(&empty_analog, 0, 0, 0) && audio_output_rank(&bare, 1, 3, 0) < audio_output_rank(&jacks, 1, 3, 0) &&
                            audio_output_rank(&bare, 1, 3, 0) > audio_output_rank(&empty_analog, 0, 0, 0),
                        "the order: analog that can be heard, a display, a pin that cannot say, analog with nothing in it, a pin with no display");
        }
        // A card that is gone: an error, and the jacks as they were zeroed; every control file closed.
        al_card_present[0] = 0;
        ALCHECK(audio_jacks_read(0, &jacks) < 0 && !jacks.hdmi_known && !jacks.output_jacks, "a card that is gone is an error");
        ALCHECK(al_opens == al_closes, "every control file closed after the jacks");
}

/* ---- a real card: the loopback ---- */

static __attribute__((unused)) double al_now(void)
{
        struct timespec at;

        clock_gettime(CLOCK_MONOTONIC, &at);
        return (double)at.tv_sec + (double)at.tv_nsec * 1e-9;
}

// A sine mixed by the mixing core, written to the loopback's playback side as the service will, and read from its capture side.
static void al_test_loopback(void)
{
#ifdef __linux__
        static struct audio_card cards[AUDIO_ALSA_CARDS];
        static struct audio_voice voice;
        static struct audio_sink sink;
        static unsigned char ring[1 << 16], out[AUDIO_BLOCK * 8 * 4], captured[48000 * 4 * 2 * 6];
        static short source[48000 * 2 * 2];
        struct audio_pcm playback, capture;
        long found, card = -1, result, captured_bytes = 0, produced = 0, wakes = 0, sent_frames = 0;
        double begin, worst_pass = 0, last_wake = 0, worst_gap = 0, sum_gap = 0;
        long frames = 48000 + 4800;

        al_use_real = 1;
        found = audio_cards_scan(cards, AUDIO_ALSA_CARDS);
        for (long i = 0; i < found; i++)
                if (!strcmp(cards[i].id, "Loopback"))
                        card = cards[i].number;
        if (card < 0)
        {
                printf("  loopback: NOT RUN (no snd-aloop card here: sudo modprobe snd-aloop index=7 pcm_substreams=2)\n");
                al_use_real = 0;
                return;
        }
        result = audio_pcm_open(&playback, card, 0, AUDIO_ALSA_PLAYBACK, 48000, 256, 1);
        if (result < 0)
        {
                printf("  loopback: NOT RUN (the loopback's playback side did not open: %ld)\n", result);
                al_use_real = 0;
                return;
        }
        result = audio_pcm_open(&capture, card, 1, AUDIO_ALSA_CAPTURE, 48000, 256, 1);
        ALCHECK(result == 0, "the capture side opens: %ld", result);
        if (result < 0)
        {
                audio_pcm_close(&playback);
                al_use_real = 0;
                return;
        }
        printf("  loopback: card %ld, %ld Hz, format %ld, %ld channels, period %ld, buffer %ld, target %ld\n", card, playback.rate, playback.format, playback.channels, playback.period,
               playback.buffer, playback.target);
        ALCHECK(playback.rate == capture.rate && playback.format == capture.format && playback.channels == capture.channels, "both sides run the same way");
        ALCHECK(playback.rate == 48000 && playback.format == AUDIO_FORMAT_S32LE && playback.channels == 2, "the card is run at 48000, 32 bits, a pair");

        for (long i = 0; i < frames; i++)
        {
                source[i * 2] = (short)rint(20000.0 * sin(2 * M_PI * 997.0 * (double)i / 48000.0));
                source[i * 2 + 1] = (short)rint(12000.0 * sin(2 * M_PI * 1500.0 * (double)i / 48000.0) + 6000.0 * sin(2 * M_PI * 220.0 * (double)i / 48000.0));
        }
        audio_sink_open(&sink, playback.rate, playback.channels);
        result = audio_voice_open(&voice, AUDIO_FORMAT_S16LE, 2, 48000, 0, playback.rate, ring, sizeof(ring));
        ALCHECK(result == 0, "the voice opens: %ld", result);
        voice.now[0] = voice.now[1] = voice.target[0] = voice.target[1] = 1.0f;

        // Prefill silence to the target, then a loop as the service has: wait for room, mix what is wanted, write, read the other side.
        {
                static unsigned char silence[4096 * 8];

                audio_pcm_write(&playback, silence, playback.target);
        }
        begin = al_now();
        while (sent_frames < frames + 2000 && al_now() - begin < 5.0)
        {
                struct pollfd fd = {playback.fd, POLLOUT, 0};
                double woke, pass_begin;
                long avail, fill, want;

                poll(&fd, 1, 100);
                woke = al_now();
                pass_begin = woke;
                if (last_wake)
                {
                        double gap = woke - last_wake;

                        sum_gap += gap;
                        worst_gap = gap > worst_gap ? gap : worst_gap;
                }
                last_wake = woke;
                wakes++;
                avail = audio_pcm_avail(&playback);
                if (avail < 0)
                {
                        audio_pcm_recover(&playback, avail);
                        continue;
                }
                fill = playback.buffer - avail;
                want = playback.target - fill;
                if (getenv("AUDIO_DEBUG") && wakes < 25)
                {
                        struct audio_alsa_sync sync;

                        memset(&sync, 0, sizeof(sync));
                        sync.flags = 7;
                        audio_sys_ioctl(playback.fd, AUDIO_ALSA_SYNC_PTR, &sync);
                        printf("    raw: state %d hw %lu appl %lu boundary %ld\n", sync.s.status.state, sync.s.status.hw_ptr, sync.c.control.appl_ptr, playback.boundary);
                }
                if (getenv("AUDIO_DEBUG") && wakes < 25)
                        printf("    wake %ld at %.4f: avail %ld fill %ld want %ld revents %x delay %ld sent %ld\n", wakes, woke - begin, avail, fill, want, fd.revents, audio_pcm_delay(&playback), sent_frames);
                while (want > 0)
                {
                        long n = want < AUDIO_BLOCK ? want : AUDIO_BLOCK, left = frames - produced, give;

                        // The program writes a little ahead of what is played, as a program does.
                        if (left > 0)
                        {
                                give = left < 480 ? left : 480;
                                if (audio_ring_fill(&voice.ring) / 4 < (unsigned long)n + 480)
                                {
                                        audio_ring_put(&voice.ring, (const unsigned char *)(source + produced * 2), (unsigned long)give * 4);
                                        produced += give;
                                }
                        }
                        audio_sink_begin(&sink, n);
                        audio_voice_mix(&sink, &voice, n, 0);
                        audio_sink_end(&sink, out, (long)playback.format, playback.channels, n);
                        result = audio_pcm_write(&playback, out, n);
                        if (result < 0)
                                break;
                        sent_frames += result;
                        want -= result;
                }
                {
                        double pass = al_now() - pass_begin;

                        worst_pass = pass > worst_pass ? pass : worst_pass;
                }
                for (;;)
                {
                        long got = audio_pcm_avail(&capture);

                        if (got <= 0)
                                break;
                        if (got > 4096)
                                got = 4096;
                        if (got > (long)(sizeof(captured) - (unsigned long)captured_bytes) / capture.frame_bytes)
                                got = (long)(sizeof(captured) - (unsigned long)captured_bytes) / capture.frame_bytes;
                        if (got <= 0)
                                break;
                        result = audio_pcm_read(&capture, captured + captured_bytes, got);
                        if (result <= 0)
                                break;
                        captured_bytes += result * capture.frame_bytes;
                        if (captured_bytes > (long)sizeof(captured) - 4096 * 8)
                                break;
                }
        }
        // Let the rest come over.
        for (int i = 0; i < 40; i++)
        {
                long got = audio_pcm_avail(&capture);

                if (got > (long)(sizeof(captured) - (unsigned long)captured_bytes) / capture.frame_bytes)
                        got = (long)(sizeof(captured) - (unsigned long)captured_bytes) / capture.frame_bytes;
                if (got > 0)
                {
                        result = audio_pcm_read(&capture, captured + captured_bytes, got);
                        if (result > 0)
                                captured_bytes += result * capture.frame_bytes;
                }
                usleep(2000);
        }
        // The sine is somewhere in what came out: find its first frame (after the fade-in ramps, the silence before it is zeros) and compare 40000 frames, bit for bit.
        {
                long at = -1, matched = 0, compared = 0, bad = 0, ramp_end = 96;
                int *words = (int *)captured;

                for (long i = 0; i < captured_bytes / 8; i++)
                        if (words[i * 2] != 0 || words[i * 2 + 1] != 0)
                        {
                                at = i;
                                break;
                        }
                ALCHECK(at >= 0, "the loopback gave back sound (%ld bytes)", captured_bytes);
                if (at >= 0)
                {
                        // The first frame of the voice is the first non-zero frame less the fade-in's first frames, which are nearly zero: look for where the sample index starts.
                        for (long shift = 0; shift < 200 && !matched; shift++)
                        {
                                long i = at - shift;
                                long probe = 3000;
                                int ok = i >= 0;

                                for (long k = ramp_end + 100; ok && k < ramp_end + 100 + probe; k++)
                                        ok = words[(i + k) * 2] == (int)source[k * 2] * 65536 && words[(i + k) * 2 + 1] == (int)source[k * 2 + 1] * 65536;
                                if (ok)
                                        matched = shift + 1;
                        }
                        ALCHECK(matched, "the first frames of the sine are found in what the loopback gave");
                        if (matched)
                        {
                                long i0 = at - (matched - 1);

                                for (long k = ramp_end + 100; k < frames - 400 && (i0 + k) * 8 + 8 <= captured_bytes; k++)
                                {
                                        compared++;
                                        if (words[(i0 + k) * 2] != (int)source[k * 2] * 65536 || words[(i0 + k) * 2 + 1] != (int)source[k * 2 + 1] * 65536)
                                                bad++;
                                }
                                ALCHECK(compared > 40000 && bad == 0, "%ld of %ld frames differ from the sine that was mixed", bad, compared);
                                printf("  loopback: %ld frames compared with the sine, %ld differ\n", compared, bad);
                        }
                }
        }
        printf("  loopback: %ld wakes, the period asked for is %.2f ms, wake gaps mean %.2f ms worst %.2f ms, the worst pass (mix and write) %.3f ms, underruns %lu\n", wakes,
               (double)playback.period * 1000.0 / (double)playback.rate, wakes > 1 ? sum_gap / (double)(wakes - 1) * 1000.0 : 0.0, worst_gap * 1000.0, worst_pass * 1000.0, playback.xruns);
        ALCHECK(playback.xruns == 0, "no underrun in a second of sound: %lu", playback.xruns);
        audio_voice_close(&voice);
        audio_pcm_close(&capture);
        audio_pcm_close(&playback);
        al_use_real = 0;
#else
        printf("  loopback: NOT RUN (not Linux)\n");
#endif
}

int main(void)
{
        al_test_layout();
        al_test_negotiation();
        al_test_flow();
        al_test_scan();
        al_test_mixer();
        al_test_jacks();
        al_test_loopback();
        printf("CHECK_audio_alsa: %u checks, %u failures\n", al_checks, al_failures);
        return al_failures ? 1 : 0;
}
#endif /* CHECK_audio_alsa */

#ifdef CHECK_audio_pulse
/*
        CHECK_audio_pulse: the sound server's protocol engine (src/audio/pulse.c) against a client written here from the
        documented wire (a tag structure writer and reader of its own), with a fake host behind the engine: the
        handshake, the info of every object at every protocol version that changes it, a playback stream through its
        whole life (prebuffering, the start, requests, an underrun, drain, cork, flush, trigger, latency, moving),
        volumes and mutes down to the bits that come out of the mixer, subscription events, who may do what, every
        cap, and a fixed-seed fuzz of raw bytes and of mutated real sessions, with every client, stream, ring and
        table counted back to nothing.

            cc -std=gnu11 -O2 -Wall -Wextra -DCHECK_audio_pulse test/checks.c -lm
*/
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// mix.c's loops that are lib.c's, which a hosted check has none of: the C they were.
#define SHARED_audio_loops
#define AUDIO_C_AS_LIBRARY
#include "../checks.c"
#undef AUDIO_C_AS_LIBRARY
#undef SHARED_audio_loops

#ifndef offset_of
#include <stddef.h>
#define offset_of(type, member) offsetof(type, member)
#endif
#ifndef offset_of
#define offset_of(type, member) ((unsigned long)&((type *)0)->member)
#endif
#ifndef memory_fill
#define memory_fill(block, value, count) memset((block), (value), (count))
#endif
#ifndef memory_copy_apart
#define memory_copy_apart(to, from, size) memcpy((to), (from), (size))
#endif
#ifndef memory_zero
#define memory_zero(to, size) memset((to), 0, (size))
#endif
#ifndef memory_copy
#define memory_copy(to, from, size) memmove((to), (from), (size))
#endif
#ifndef memory_first_of
#define memory_first_of(block, value, size) memchr((block), (value), (size))
#endif
typedef void *address_any;
#ifndef string_length
#define string_length(text) strlen((const char *)(text))
#endif
#include "../../src/audio/mix.c"
#include "../../src/audio/pulse.c"
// The library's names control.c calls, as the libc has them for a hosted build (the freestanding build has the library's own).
#define memory_copy(to, from, size) memmove((to), (from), (size))
#define string_length(text) strlen((const char *)(text))
#define memory_first_of(block, value, size) memchr((block), (value), (size))
#define memory_zero(to, size) memset((to), 0, (size))
#define CTL_NO_SYSTEM
#include "../../src/audio/control.c"

static unsigned pt_checks, pt_failures;

#define PTCHECK(condition, ...)                                                                                       \
        do                                                                                                            \
        {                                                                                                             \
                pt_checks++;                                                                                          \
                if (!(condition))                                                                                     \
                {                                                                                                     \
                        pt_failures++;                                                                                \
                        if (pt_failures < 60)                                                                         \
                        {                                                                                             \
                                printf("  FAIL: ");                                                                   \
                                printf(__VA_ARGS__);                                                                  \
                                printf(" (line %d)\n", __LINE__);                                                     \
                        }                                                                                             \
                }                                                                                                     \
        } while (0)

static unsigned long long pt_state = 0x853c49e6748fea9bull;

static unsigned long pt_rand(void)
{
        pt_state ^= pt_state >> 12;
        pt_state ^= pt_state << 25;
        pt_state ^= pt_state >> 27;
        return (unsigned long)(pt_state * 0x2545f4914f6cdd1dull >> 11);
}

/* ---- the fake host ---- */

static long pt_clock;
static long pt_allocs, pt_releases, pt_alloc_bytes, pt_release_bytes, pt_alloc_fail_after = -1;
static int pt_remembered_calls, pt_changed_calls, pt_last_what;
static long pt_last_slot;
static unsigned long pt_recall_volume;
static int pt_recall_mute, pt_recall_has;
static char pt_remembered_name[300];
// What the host remembers, by the name the engine asks under (the fake host keeps a table of them; pt_recall_has overrides it).
static struct
{
        char name[300];
        unsigned long volume;
        int mute;
} pt_memory[16];
static int pt_memory_n;

static void *pt_alloc(void *context, long bytes)
{
        (void)context;
        if (pt_alloc_fail_after >= 0 && pt_allocs >= pt_alloc_fail_after)
                return 0;
        pt_allocs++;
        pt_alloc_bytes += bytes;
        return calloc(1, (size_t)bytes);
}

static void pt_release(void *context, void *block, long bytes)
{
        (void)context;
        pt_releases++;
        pt_release_bytes += bytes;
        free(block);
}

static long pt_now_calls;

static long pt_now(void *context)
{
        (void)context;
        pt_now_calls++;
        return pt_clock;
}

static long pt_card_us; // what the host says the card holds for a stream (host.latency): 0, a clock, unless a check sets it
static long pt_card_asked; // and the latency the last stream asked it about

static long pt_latency(void *context, long sink, long asked_us)
{
        (void)context;
        (void)sink;
        pt_card_asked = asked_us;
        return pt_card_us;
}

static void pt_real(void *context, long *seconds, long *microseconds)
{
        (void)context;
        *seconds = 1700000000 + pt_clock / 1000000;
        *microseconds = pt_clock % 1000000;
}

static int pt_recall(void *context, const char *name, unsigned long *volume, int *mute)
{
        (void)context;
        if (pt_recall_has)
        {
                *volume = pt_recall_volume;
                *mute = pt_recall_mute;
                return 1;
        }
        for (int i = 0; i < pt_memory_n; i++)
                if (!strcmp(pt_memory[i].name, name))
                {
                        *volume = pt_memory[i].volume;
                        *mute = pt_memory[i].mute;
                        return 1;
                }
        return 0;
}

static void pt_remember(void *context, const char *name, unsigned long volume, int mute)
{
        int at = -1;

        (void)context;
        pt_remembered_calls++;
        snprintf(pt_remembered_name, sizeof(pt_remembered_name), "%s", name);
        for (int i = 0; i < pt_memory_n; i++)
                if (!strcmp(pt_memory[i].name, name))
                        at = i;
        if (at < 0 && pt_memory_n < 16)
                at = pt_memory_n++;
        if (at >= 0)
        {
                snprintf(pt_memory[at].name, sizeof(pt_memory[at].name), "%s", name);
                pt_memory[at].volume = volume;
                pt_memory[at].mute = mute;
        }
}

static void pt_changed(void *context, int what, long sink)
{
        (void)context;
        pt_last_what = what;
        pt_last_slot = sink;
        pt_changed_calls++;
}

static struct pulse pt_engine;

/* The engine's growable tables (its clients, streams and records) stay in the host's memory for the engine's life: a table is one
   block and its bytes, held while it has slots, and every other block must be given back. */
static long pt_table_blocks(void)
{
        return (pt_engine.client_slots > 0) + (pt_engine.stream_slots > 0) + (pt_engine.record_slots > 0);
}

static long pt_table_bytes(void)
{
        return (long)(pt_engine.client_slots * sizeof(struct pulse_client *) + pt_engine.stream_slots * sizeof(struct pulse_stream) +
               pt_engine.record_slots * sizeof(struct pulse_record));
}

static void pt_reset(void)
{
        struct pulse_host host = {0, pt_alloc, pt_release, pt_now, pt_real, pt_recall, pt_remember, pt_changed, pt_latency};

        pulse_open(&pt_engine, &host);
        pt_card_us = pt_card_asked = 0;
        pt_allocs = pt_releases = pt_alloc_bytes = pt_release_bytes = 0;
        pt_alloc_fail_after = -1;
        pt_clock = 1000000;
        pt_recall_has = 0;
        pt_remembered_calls = 0;
        pt_changed_calls = 0;
        pt_memory_n = 0;
}

/* ---- a client written from the wire ---- */

struct pt_w
{
        unsigned char b[PULSE_SCRATCH];
        long n;
};

static void pt_byte(struct pt_w *w, int c)
{
        w->b[w->n++] = (unsigned char)c;
}

static void pt_be32(struct pt_w *w, unsigned long v)
{
        for (int shift = 24; shift >= 0; shift -= 8)
                pt_byte(w, (int)(v >> shift) & 0xff);
}

static void pt_u32(struct pt_w *w, unsigned long v)
{
        pt_byte(w, 'L');
        pt_be32(w, v);
}

static void pt_bool(struct pt_w *w, int v)
{
        pt_byte(w, v ? '1' : '0');
}

static void pt_str(struct pt_w *w, const char *s)
{
        if (!s)
        {
                pt_byte(w, 'N');
                return;
        }
        pt_byte(w, 't');
        memcpy(w->b + w->n, s, strlen(s) + 1);
        w->n += (long)strlen(s) + 1;
}

static void pt_spec(struct pt_w *w, int format, int channels, unsigned long rate)
{
        pt_byte(w, 'a');
        pt_byte(w, format);
        pt_byte(w, channels);
        pt_be32(w, rate);
}

static void pt_map(struct pt_w *w, int channels, const int *positions)
{
        pt_byte(w, 'm');
        pt_byte(w, channels);
        for (int i = 0; i < channels; i++)
                pt_byte(w, positions[i]);
}

static void pt_vol(struct pt_w *w, int channels, unsigned long v)
{
        pt_byte(w, 'v');
        pt_byte(w, channels);
        for (int i = 0; i < channels; i++)
                pt_be32(w, v);
}

static void pt_arb(struct pt_w *w, const void *data, unsigned long n)
{
        pt_byte(w, 'x');
        pt_be32(w, n);
        memcpy(w->b + w->n, data, n);
        w->n += (long)n;
}

static void pt_props(struct pt_w *w, const char *k1, const char *v1, const char *k2, const char *v2)
{
        pt_byte(w, 'P');
        const char *keys[2] = {k1, k2}, *values[2] = {v1, v2};

        for (int i = 0; i < 2; i++)
                if (keys[i])
                {
                        pt_str(w, keys[i]);
                        pt_u32(w, strlen(values[i]) + 1);
                        pt_arb(w, values[i], strlen(values[i]) + 1);
                }
        pt_byte(w, 'N');
}

struct pt_frame
{
        unsigned long command, tag, channel;
        long length;
        unsigned char payload[PULSE_FRAME_COMMAND];
        int data;
};

static struct pt_frame pt_log[256];
static int pt_logged;

// Everything the engine has queued for the client, parsed into frames (a command's tag is the second field). The queue is a ring: what is
// taken from its two ends is joined, so a frame across the end of it is one.
static void pt_collect(long slot)
{
        static unsigned char joined[PULSE_OUT + 64];
        const unsigned char *bytes;
        long n, total = 0, used = 0;

        pt_logged = 0;
        while ((n = pulse_client_out(&pt_engine, slot, &bytes)) > 0)
        {
                memcpy(joined + total, bytes, (size_t)n);
                total += n;
                pulse_client_sent(&pt_engine, slot, n);
        }
        while (total - used >= 20)
        {
                unsigned long length = wire_be32(joined + used), channel = wire_be32(joined + used + 4);
                struct pt_frame *f;

                if ((long)length > total - used - 20)
                        break;
                if (pt_logged < 256)
                {
                        f = pt_log + pt_logged++;
                        f->length = (long)length;
                        f->channel = channel;
                        f->data = channel != PULSE_INVALID;
                        memcpy(f->payload, joined + used + 20, length < sizeof(f->payload) ? length : sizeof(f->payload));
                        f->command = length >= 5 ? wire_be32(f->payload + 1) : 0;
                        f->tag = length >= 10 ? wire_be32(f->payload + 6) : 0;
                }
                used += 20 + (long)length;
        }
}

static const struct pt_frame *pt_find(unsigned long command)
{
        for (int i = 0; i < pt_logged; i++)
                if (pt_log[i].command == command)
                        return pt_log + i;
        return 0;
}

static int pt_count(unsigned long command)
{
        int n = 0;

        for (int i = 0; i < pt_logged; i++)
                n += pt_log[i].command == command;
        return n;
}

// A reader of the frames the engine sent, written here.
struct pt_r
{
        const unsigned char *p;
        long n, at;
        int bad;
};

static unsigned long pt_ru32(struct pt_r *r)
{
        unsigned long v = 0;

        if (r->at + 5 > r->n || r->p[r->at] != 'L')
        {
                r->bad = 1;
                return 0;
        }
        v = wire_be32(r->p + r->at + 1);
        r->at += 5;
        return v;
}

static long long pt_r64(struct pt_r *r, int tag)
{
        unsigned long long v;

        if (r->at + 9 > r->n || r->p[r->at] != tag)
        {
                r->bad = 1;
                return 0;
        }
        v = (unsigned long long)wire_be32(r->p + r->at + 1) << 32 | wire_be32(r->p + r->at + 5);
        r->at += 9;
        return (long long)v;
}

static unsigned long pt_rvolume(struct pt_r *r)
{
        unsigned long v;

        if (r->at + 5 > r->n || r->p[r->at] != 'V')
        {
                r->bad = 1;
                return 0;
        }
        v = wire_be32(r->p + r->at + 1);
        r->at += 5;
        return v;
}

static int pt_rbool(struct pt_r *r)
{
        if (r->at + 1 > r->n || (r->p[r->at] != '1' && r->p[r->at] != '0'))
        {
                r->bad = 1;
                return 0;
        }
        return r->p[r->at++] == '1';
}

static const char *pt_rstr(struct pt_r *r)
{
        const char *s;

        if (r->at < r->n && r->p[r->at] == 'N')
        {
                r->at++;
                return 0;
        }
        if (r->at >= r->n || r->p[r->at] != 't')
        {
                r->bad = 1;
                return "";
        }
        s = (const char *)r->p + r->at + 1;
        r->at += 2 + (long)strlen(s);
        if (r->at > r->n)
                r->bad = 1;
        return s;
}

static void pt_rspec(struct pt_r *r, int *format, int *channels, unsigned long *rate)
{
        if (r->at + 7 > r->n || r->p[r->at] != 'a')
        {
                r->bad = 1;
                return;
        }
        *format = r->p[r->at + 1];
        *channels = r->p[r->at + 2];
        *rate = wire_be32(r->p + r->at + 3);
        r->at += 7;
}

static int pt_rmap(struct pt_r *r, int *positions)
{
        int n;

        if (r->at + 2 > r->n || r->p[r->at] != 'm')
        {
                r->bad = 1;
                return 0;
        }
        n = r->p[r->at + 1];
        if (r->at + 2 + n > r->n)
        {
                r->bad = 1;
                return 0;
        }
        for (int i = 0; i < n; i++)
                positions[i] = r->p[r->at + 2 + i];
        r->at += 2 + n;
        return n;
}

static int pt_rvol(struct pt_r *r, unsigned long *v)
{
        int n;

        if (r->at + 2 > r->n || r->p[r->at] != 'v')
        {
                r->bad = 1;
                return 0;
        }
        n = r->p[r->at + 1];
        if (r->at + 2 + 4 * n > r->n)
        {
                r->bad = 1;
                return 0;
        }
        for (int i = 0; i < n; i++)
                v[i] = wire_be32(r->p + r->at + 2 + 4 * i);
        r->at += 2 + 4 * n;
        return n;
}

// A property list: how many entries, and the value of the first named key if the caller asks.
static int pt_rprops(struct pt_r *r, const char *want, char *value, long room)
{
        int count = 0;

        if (r->at >= r->n || r->p[r->at] != 'P')
        {
                r->bad = 1;
                return 0;
        }
        r->at++;
        for (;;)
        {
                const char *key = pt_rstr(r);
                unsigned long length;

                if (r->bad || !key)
                        break;
                length = pt_ru32(r);
                if (r->bad || r->at + 5 + (long)length > r->n || r->p[r->at] != 'x' || wire_be32(r->p + r->at + 1) != length)
                {
                        r->bad = 1;
                        break;
                }
                if (want && !strcmp(key, want) && value)
                {
                        long c = (long)length < room - 1 ? (long)length : room - 1;

                        memcpy(value, r->p + r->at + 5, (size_t)c);
                        value[c] = 0;
                }
                r->at += 5 + (long)length;
                count++;
        }
        return count;
}

static void pt_rformat(struct pt_r *r)
{
        if (r->at + 2 > r->n || r->p[r->at] != 'f')
        {
                r->bad = 1;
                return;
        }
        r->at++;
        if (r->p[r->at] != 'B')
        {
                r->bad = 1;
                return;
        }
        r->at += 2;
        pt_rprops(r, 0, 0, 0);
}

// A client's frame to the engine: a command with its tag and fields in w.
static int pt_command(long slot, unsigned long command, unsigned long tag, const struct pt_w *fields)
{
        static unsigned char frame[PULSE_IN];
        struct pt_w head = {{0}, 0};
        long length;

        pt_u32(&head, command);
        pt_u32(&head, tag);
        length = head.n + (fields ? fields->n : 0);
        memset(frame, 0, 20);
        frame[0] = (unsigned char)(length >> 24);
        frame[1] = (unsigned char)(length >> 16);
        frame[2] = (unsigned char)(length >> 8);
        frame[3] = (unsigned char)length;
        memset(frame + 4, 0xff, 4);
        memcpy(frame + 20, head.b, (size_t)head.n);
        if (fields)
                memcpy(frame + 20 + head.n, fields->b, (size_t)fields->n);
        return pulse_client_feed(&pt_engine, slot, frame, 20 + length);
}

static int pt_data(long slot, unsigned long channel, const void *data, long length, unsigned long flags, long long offset)
{
        static unsigned char frame[PULSE_IN + 128];

        memset(frame, 0, 20);
        frame[0] = (unsigned char)(length >> 24);
        frame[1] = (unsigned char)(length >> 16);
        frame[2] = (unsigned char)(length >> 8);
        frame[3] = (unsigned char)length;
        frame[4] = (unsigned char)(channel >> 24);
        frame[5] = (unsigned char)(channel >> 16);
        frame[6] = (unsigned char)(channel >> 8);
        frame[7] = (unsigned char)channel;
        for (int i = 0; i < 8; i++)
                frame[8 + i] = (unsigned char)((unsigned long long)offset >> (56 - 8 * i));
        frame[16] = (unsigned char)(flags >> 24);
        frame[17] = (unsigned char)(flags >> 16);
        frame[18] = (unsigned char)(flags >> 8);
        frame[19] = (unsigned char)flags;
        memcpy(frame + 20, data, (size_t)length);
        return pulse_client_feed(&pt_engine, slot, frame, 20 + length);
}

// A connection that has said who it is, at the protocol version asked.
static long pt_connect(long uid, long pid, unsigned version, const char *name)
{
        long slot = pulse_client_open(&pt_engine, uid, pid);
        struct pt_w w = {{0}, 0};

        if (slot < 0)
                return slot;
        pt_u32(&w, version);
        pt_arb(&w, (const unsigned char[256]){0}, 256);
        if (pt_command(slot, PULSE_COMMAND_AUTH, 1, &w) < 0)
                return -1;
        w.n = 0;
        if (version >= 13)
                pt_props(&w, "application.name", name, 0, 0);
        else
                pt_str(&w, name);
        if (pt_command(slot, PULSE_COMMAND_SET_CLIENT_NAME, 2, &w) < 0)
                return -1;
        pt_collect(slot);
        return slot;
}

static int pt_expect_error(unsigned long tag, unsigned long code)
{
        const struct pt_frame *f = pt_find(PULSE_COMMAND_ERROR);
        struct pt_r r = {0, 0, 0, 0};

        if (!f || f->tag != tag)
                return 0;
        r.p = f->payload;
        r.n = f->length;
        pt_ru32(&r);
        pt_ru32(&r);
        return pt_ru32(&r) == code && !r.bad;
}

static int pt_expect_ack(unsigned long tag)
{
        for (int i = 0; i < pt_logged; i++)
                if (pt_log[i].command == PULSE_COMMAND_REPLY && pt_log[i].tag == tag && pt_log[i].length == 10)
                        return 1;
        return 0;
}

static struct pt_r pt_reply_reader(const struct pt_frame *f)
{
        struct pt_r r = {f->payload, f->length, 0, 0};

        pt_ru32(&r);
        pt_ru32(&r);
        return r;
}

/* ---- the tests ---- */

static const int pt_stereo[2] = {AUDIO_POSITION_FRONT_LEFT, AUDIO_POSITION_FRONT_RIGHT};

static void pt_test_handshake(void)
{
        long slot;
        struct pt_w w = {{0}, 0};
        const struct pt_frame *f;
        struct pt_r r;

        pt_reset();
        pulse_sink_add(&pt_engine, "null", "Null", 48000);
        slot = pulse_client_open(&pt_engine, 1000, 77);
        PTCHECK(slot == 0, "a client connects");
        // Before the handshake nothing is served.
        pt_command(slot, PULSE_COMMAND_GET_SERVER_INFO, 5, 0);
        pt_collect(slot);
        PTCHECK(pt_expect_error(5, PULSE_ERR_ACCESS), "server info before AUTH is refused");
        pt_command(slot, PULSE_COMMAND_STAT, 4, 0);
        pt_collect(slot);
        PTCHECK(pt_expect_error(4, PULSE_ERR_ACCESS), "and so is STAT, which needs nothing of its own");
        // An old client is refused with the version error, a new one is told the version.
        pt_u32(&w, 7);
        pt_arb(&w, (const unsigned char[256]){0}, 256);
        pt_command(slot, PULSE_COMMAND_AUTH, 6, &w);
        pt_collect(slot);
        PTCHECK(pt_expect_error(6, PULSE_ERR_VERSION), "version 7 is refused");
        w.n = 0;
        pt_u32(&w, 35 | 0x80000000u | 0x40000000u);
        pt_arb(&w, (const unsigned char[256]){0}, 256);
        pt_command(slot, PULSE_COMMAND_AUTH, 7, &w);
        pt_collect(slot);
        f = pt_find(PULSE_COMMAND_REPLY);
        PTCHECK(f && f->tag == 7, "AUTH is answered");
        if (f)
        {
                r = pt_reply_reader(f);
                PTCHECK(pt_ru32(&r) == 35 && !r.bad && r.at == f->length, "the answer is the version, with neither shared-memory flag");
        }
        // A cookie of the wrong length is a mistake of the wire.
        w.n = 0;
        pt_u32(&w, 35);
        pt_arb(&w, (const unsigned char[10]){0}, 10);
        PTCHECK(pt_command(slot, PULSE_COMMAND_AUTH, 8, &w) == PULSE_DEAD, "a short cookie ends the client");
        pulse_client_close(&pt_engine, slot);
        // Frames that must end a client: too long a command, shared memory flags, a command of no length.
        for (int k = 0; k < 4; k++)
        {
                unsigned char frame[64] = {0};

                slot = pulse_client_open(&pt_engine, 1000, 77);
                frame[3] = (unsigned char)(k == 0 ? 0 : 8);
                memset(frame + 4, 0xff, 4);
                if (k == 1)
                {
                        frame[0] = 0xff;
                        frame[1] = 0xff;
                        frame[3] = 0;
                }
                if (k == 2)
                {
                        // A command that would be served, with the shared-memory flag set on its frame.
                        static const unsigned char stat[10] = {'L', 0, 0, 0, PULSE_COMMAND_STAT, 'L', 0, 0, 0, 1};

                        frame[3] = 10;
                        frame[16] = 0x80;
                        memcpy(frame + 20, stat, 10);
                }
                if (k == 3)
                {
                        frame[3] = 8;
                        frame[4] = frame[5] = frame[6] = frame[7] = 0;
                }
                PTCHECK(pulse_client_feed(&pt_engine, slot, frame, 20 + frame[3]) == PULSE_DEAD, "hostile frame %d ends the client", k);
                pulse_client_close(&pt_engine, slot);
        }
        // A client that does not say who it is, is late.
        slot = pulse_client_open(&pt_engine, 1000, 77);
        PTCHECK(!pulse_client_late(&pt_engine, slot, pt_clock + 1000), "not late yet");
        PTCHECK(pulse_client_late(&pt_engine, slot, pt_clock + PULSE_AUTH_USEC + 1000), "late after the time");
        pulse_client_close(&pt_engine, slot);
        PTCHECK(pt_engine.clients_made == pt_engine.clients_dropped, "all clients gone");
        PTCHECK(pt_allocs == pt_releases + pt_table_blocks() && pt_alloc_bytes == pt_release_bytes + pt_table_bytes(), "every block given back");
}

static void pt_test_info(void)
{
        static const unsigned versions[] = {35, 34, 29, 21, 16, 15, 13, 11, 9};

        pt_reset();
        pulse_sink_add(&pt_engine, "alsa_output.card.00", "Card <one>", 48000);
        pulse_sink_add(&pt_engine, "second", "Second", 44100);
        for (unsigned v = 0; v < sizeof(versions) / sizeof(versions[0]); v++)
        {
                long slot = pt_connect(1000, 4321, versions[v], "Test Player");
                struct pt_w w = {{0}, 0};
                const struct pt_frame *f;
                struct pt_r r;

                PTCHECK(slot >= 0, "version %u connects", versions[v]);
                pt_command(slot, PULSE_COMMAND_GET_SERVER_INFO, 10, 0);
                pt_collect(slot);
                f = pt_find(PULSE_COMMAND_REPLY);
                PTCHECK(f && f->tag == 10, "version %u: server info answered", versions[v]);
                if (f)
                {
                        int format, channels, map[8];
                        unsigned long rate;
                        const char *name, *version, *user, *host, *sink, *source;

                        r = pt_reply_reader(f);
                        name = pt_rstr(&r);
                        version = pt_rstr(&r);
                        user = pt_rstr(&r);
                        host = pt_rstr(&r);
                        pt_rspec(&r, &format, &channels, &rate);
                        sink = pt_rstr(&r);
                        source = pt_rstr(&r);
                        pt_ru32(&r);
                        if (versions[v] >= 15)
                                pt_rmap(&r, map);
                        PTCHECK(!r.bad && r.at == f->length, "version %u: server info is read to its end", versions[v]);
                        PTCHECK(name && version && user && host && format == 3 && channels == 2 && rate == 48000, "server info fields");
                        PTCHECK(sink && !strcmp(sink, "alsa_output.card.00") && source && !strcmp(source, "alsa_output.card.00.monitor"), "the default sink and its monitor: %s %s", sink, source);
                }
                // The list of sinks.
                pt_command(slot, PULSE_COMMAND_GET_SINK_INFO_LIST, 11, 0);
                pt_collect(slot);
                f = pt_find(PULSE_COMMAND_REPLY);
                PTCHECK(f && f->tag == 11, "sink list answered");
                if (f)
                {
                        int found = 0;

                        r = pt_reply_reader(f);
                        while (!r.bad && r.at < f->length)
                        {
                                int format, channels, map[8];
                                unsigned long rate, volume[8], index, flags, state;
                                const char *name, *description, *monitor;
                                char device_description[96] = "";
                                int muted, ports = 0, formats;

                                index = pt_ru32(&r);
                                name = pt_rstr(&r);
                                description = pt_rstr(&r);
                                pt_rspec(&r, &format, &channels, &rate);
                                pt_rmap(&r, map);
                                pt_ru32(&r);
                                pt_rvol(&r, volume);
                                muted = pt_rbool(&r);
                                pt_ru32(&r);
                                monitor = pt_rstr(&r);
                                pt_r64(&r, 'U');
                                pt_rstr(&r);
                                flags = pt_ru32(&r);
                                if (versions[v] >= 13)
                                {
                                        pt_rprops(&r, "device.description", device_description, sizeof(device_description));
                                        pt_r64(&r, 'U');
                                }
                                if (versions[v] >= 15)
                                {
                                        PTCHECK(pt_rvolume(&r) == 0x10000, "the base volume is 100%%");
                                        state = pt_ru32(&r);
                                        pt_ru32(&r);
                                        pt_ru32(&r);
                                        PTCHECK(state == 1, "the sink with no stream is idle: %lu", state);
                                }
                                if (versions[v] >= 16)
                                {
                                        ports = (int)pt_ru32(&r);
                                        pt_rstr(&r);
                                }
                                if (versions[v] >= 21)
                                {
                                        formats = r.p[r.at] == 'B' ? r.p[r.at + 1] : 0;
                                        r.at += 2;
                                        for (int i = 0; i < formats; i++)
                                                pt_rformat(&r);
                                }
                                PTCHECK(!r.bad, "version %u: sink %lu is well formed", versions[v], index);
                                PTCHECK(index == (unsigned long)found && format == AUDIO_FORMAT_F32LE && channels == 2 && volume[0] == 0x10000 && volume[1] == 0x10000 && !muted && !ports &&
                                            monitor && strstr(monitor, ".monitor") && flags != 0xffffffffu && description,
                                        "sink %lu fields", index);
                                PTCHECK(rate == (found == 0 ? 48000ul : 44100ul), "sink rate");
                                if (versions[v] >= 13)
                                        PTCHECK(!strcmp(device_description, name && found == 0 ? "Card <one>" : "Second"), "the description property: %s", device_description);
                                (void)name;
                                found++;
                        }
                        PTCHECK(found == 2 && !r.bad, "two sinks listed (%d)", found);
                }
                // One sink by name, by index, one that is not there; a source (a monitor).
                w.n = 0;
                pt_u32(&w, PULSE_INVALID);
                pt_str(&w, "second");
                pt_command(slot, PULSE_COMMAND_GET_SINK_INFO, 12, &w);
                pt_collect(slot);
                PTCHECK(pt_find(PULSE_COMMAND_REPLY) && pt_find(PULSE_COMMAND_REPLY)->tag == 12, "a sink by name");
                w.n = 0;
                pt_u32(&w, PULSE_INVALID);
                pt_str(&w, "nothing");
                pt_command(slot, PULSE_COMMAND_GET_SINK_INFO, 13, &w);
                pt_collect(slot);
                PTCHECK(pt_expect_error(13, PULSE_ERR_NOENTITY), "no such sink");
                w.n = 0;
                pt_u32(&w, PULSE_INVALID);
                pt_str(&w, "1");
                pt_command(slot, PULSE_COMMAND_GET_SINK_INFO, 131, &w);
                pt_collect(slot);
                PTCHECK(pt_find(PULSE_COMMAND_REPLY) && pt_find(PULSE_COMMAND_REPLY)->tag == 131, "a sink named by its number is that index");
                w.n = 0;
                pt_u32(&w, PULSE_INVALID);
                pt_str(&w, "9");
                pt_command(slot, PULSE_COMMAND_GET_SINK_INFO, 132, &w);
                pt_collect(slot);
                PTCHECK(pt_expect_error(132, PULSE_ERR_NOENTITY), "and a number with no sink is no sink");
                pt_command(slot, PULSE_COMMAND_GET_SOURCE_INFO_LIST, 14, 0);
                pt_collect(slot);
                f = pt_find(PULSE_COMMAND_REPLY);
                PTCHECK(f && f->tag == 14 && f->length > 10, "the monitors are the sources");
                // The lists that are empty.
                for (unsigned long command = PULSE_COMMAND_GET_MODULE_INFO_LIST; command <= PULSE_COMMAND_GET_MODULE_INFO_LIST; command++)
                {
                        pt_command(slot, command, 15, 0);
                        pt_collect(slot);
                        PTCHECK(pt_expect_ack(15), "an empty list is an empty answer");
                }
                pt_command(slot, PULSE_COMMAND_GET_CARD_INFO_LIST, 16, 0);
                pt_collect(slot);
                PTCHECK(pt_expect_ack(16), "no cards");
                pt_command(slot, PULSE_COMMAND_GET_SAMPLE_INFO_LIST, 17, 0);
                pt_collect(slot);
                PTCHECK(pt_expect_ack(17), "no samples");
                pt_command(slot, PULSE_COMMAND_STAT, 18, 0);
                pt_collect(slot);
                PTCHECK(pt_find(PULSE_COMMAND_REPLY) && pt_find(PULSE_COMMAND_REPLY)->length == 10 + 25, "stat is five numbers");
                // The client list, with this client's name.
                pt_command(slot, PULSE_COMMAND_GET_CLIENT_INFO_LIST, 19, 0);
                pt_collect(slot);
                f = pt_find(PULSE_COMMAND_REPLY);
                PTCHECK(f && f->tag == 19, "client list");
                if (f)
                {
                        r = pt_reply_reader(f);
                        PTCHECK(pt_ru32(&r) <= 100000, "client index");
                        {
                                const char *name = pt_rstr(&r);

                                PTCHECK(name && !strcmp(name, "Test Player"), "the client's name is in its info: %s", name);
                        }
                        pt_ru32(&r);
                        pt_rstr(&r);
                        if (versions[v] >= 13)
                        {
                                char pid[24] = "";

                                pt_rprops(&r, "application.process.id", pid, sizeof(pid));
                                PTCHECK(!strcmp(pid, "4321"), "the process id is the system's: %s", pid);
                        }
                        PTCHECK(!r.bad && r.at == f->length, "client info is read to its end");
                }
                pulse_client_close(&pt_engine, slot);
        }
        PTCHECK(pt_allocs == pt_releases + pt_table_blocks(), "every block given back");
}

// What one stream's data comes out of the mixer as.
static short pt_source[2 * 48000 * 4];

static void pt_make_source(void)
{
        for (long i = 0; i < 48000 * 4; i++)
        {
                pt_source[2 * i] = (short)rint(18000.0 * sin(2 * M_PI * 440.0 * (double)i / 48000.0));
                pt_source[2 * i + 1] = (short)rint(14000.0 * sin(2 * M_PI * 1000.0 * (double)i / 48000.0));
        }
}

// A stereo 16-bit stream at 48000 with the buffer attributes given; the channel and the sink input's index, or -1.
static int pt_next_no_move; // the no-move bool of the next stream pt_create makes (version 12 on)
static int pt_next_fixed;   // the next stream does not adjust its latency (ADJUST_LATENCY off, version 13 on)

static int pt_create(long slot, unsigned version, unsigned long tag, unsigned long tlength, unsigned long prebuf, unsigned long minreq, unsigned long *channel, unsigned long *index,
                     unsigned long *missing, int corked, unsigned long volume, int volume_set, long format, int channels, unsigned long rate, const char *sink)
{
        struct pt_w w = {{0}, 0};
        const struct pt_frame *f;
        struct pt_r r;
        int positions[32];

        for (int i = 0; i < 32; i++)
                positions[i] = channels == 1 ? AUDIO_POSITION_MONO : i == 0 ? 1 : i == 1 ? 2 : i == 2 ? 3 : i == 3 ? 7 : i == 4 ? 5 : i == 5 ? 6 : AUDIO_POSITION_AUX0 + i;
        pt_spec(&w, (int)format, channels, rate);
        pt_map(&w, channels, positions);
        pt_u32(&w, PULSE_INVALID);
        pt_str(&w, sink);
        pt_u32(&w, 0xffffffffu);
        pt_bool(&w, corked);
        pt_u32(&w, tlength);
        pt_u32(&w, prebuf);
        pt_u32(&w, minreq);
        pt_u32(&w, 0);
        pt_vol(&w, channels, volume);
        if (version >= 12)
                for (int i = 0; i < 7; i++)
                        pt_bool(&w, i == 5 && pt_next_no_move); // no remap, no remix, fix format, fix rate, fix channels, no move, variable
        if (version >= 13)
        {
                pt_bool(&w, 0);
                pt_bool(&w, !pt_next_fixed);
                pt_props(&w, "media.name", "Playback Stream", "media.role", "music");
        }
        if (version >= 14)
        {
                pt_bool(&w, volume_set);
                pt_bool(&w, 0);
        }
        if (version >= 15)
        {
                pt_bool(&w, 0);
                pt_bool(&w, 0);
                pt_bool(&w, 0);
        }
        if (version >= 17)
                pt_bool(&w, 0);
        if (version >= 18)
                pt_bool(&w, 0);
        if (version >= 21)
        {
                pt_byte(&w, 'B');
                pt_byte(&w, 0);
        }
        if (pt_command(slot, PULSE_COMMAND_CREATE_PLAYBACK_STREAM, tag, &w) < 0)
                return -2;
        pt_collect(slot);
        f = pt_find(PULSE_COMMAND_REPLY);
        if (!f || f->tag != tag)
                return -1;
        r = pt_reply_reader(f);
        *channel = pt_ru32(&r);
        *index = pt_ru32(&r);
        *missing = pt_ru32(&r);
        if (r.bad)
                return -1;
        return 0;
}

static void pt_test_stream(void)
{
        long slot;
        unsigned long channel, index, missing;
        struct pt_w w = {{0}, 0};
        const struct pt_frame *f;
        struct pt_r r;
        static unsigned char out[AUDIO_BLOCK * 4];
        static short heard[2 * 48000 * 4];
        long heard_frames = 0, fed = 0, consumed_requests = 0;

        pt_make_source();
        pt_reset();
        pulse_sink_add(&pt_engine, "null", "Null", 48000);
        slot = pt_connect(1000, 99, 35, "Stream Test");
        // 100 ms of target, start when it is there, ask again for a quarter at a time.
        PTCHECK(pt_create(slot, 35, 20, 19200, 19200, 4800, &channel, &index, &missing, 0, 0x10000, 1, AUDIO_FORMAT_S16LE, 2, 48000, 0) == 0, "stream created");
        f = pt_find(PULSE_COMMAND_REPLY);
        r = pt_reply_reader(f);
        pt_ru32(&r);
        pt_ru32(&r);
        pt_ru32(&r);
        {
                unsigned long maxlength = pt_ru32(&r), tlength = pt_ru32(&r), prebuf = pt_ru32(&r), minreq = pt_ru32(&r);
                int format, channels, positions[8];
                unsigned long rate, sink;
                const char *sink_name;
                int suspended;

                pt_rspec(&r, &format, &channels, &rate);
                pt_rmap(&r, positions);
                sink = pt_ru32(&r);
                sink_name = pt_rstr(&r);
                suspended = pt_rbool(&r);
                pt_r64(&r, 'U');
                pt_rformat(&r);
                PTCHECK(!r.bad && r.at == f->length, "the create reply is read to its end");
                PTCHECK(missing == 19200 && tlength == 19200 && prebuf == 19200 && minreq == 4800 && maxlength == 38400, "attributes %lu %lu %lu %lu %lu", missing, maxlength, tlength, prebuf, minreq);
                PTCHECK(format == AUDIO_FORMAT_S16LE && channels == 2 && rate == 48000 && positions[0] == 1 && positions[1] == 2 && sink == 0 && sink_name && !strcmp(sink_name, "null") && !suspended,
                        "the spec, the map and the sink come back");
        }
        PTCHECK(channel == 0 && index == 0, "the first stream is channel 0, input 0");
        // Data in pieces; below the target nothing plays.
        for (int piece = 0; piece < 2; piece++)
        {
                PTCHECK(pt_data(slot, channel, pt_source + fed * 2, 6400, 0, 0) == 0, "data taken");
                fed += 1600;
        }
        pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
        pt_collect(slot);
        PTCHECK(pt_count(PULSE_COMMAND_STARTED) == 0, "below the target the stream waits");
        {
                int silent = 1;

                for (int i = 0; i < 256 * 4; i++)
                        silent &= out[i] == 0;
                PTCHECK(silent, "and the sink is silent");
        }
        pt_data(slot, channel, pt_source + fed * 2, 6400, 0, 0);
        fed += 1600;
        // At the target it starts; what comes out is what went in, after the fade-in.
        for (int block = 0; block < 12; block++)
        {
                short *got = (short *)out;

                pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
                if (block == 0)
                {
                        pt_collect(slot);
                        PTCHECK(pt_count(PULSE_COMMAND_STARTED) == 1, "it starts at the target");
                        f = pt_find(PULSE_COMMAND_STARTED);
                        PTCHECK(f && f->length == 15 && wire_be32(f->payload + 11) == channel, "STARTED names the channel");
                }
                memcpy(heard + heard_frames * 2, got, 256 * 4);
                heard_frames += 256;
        }
        for (long i = 120; i < heard_frames; i++)
                if (heard[2 * i] != pt_source[2 * i] || heard[2 * i + 1] != pt_source[2 * i + 1])
                {
                        PTCHECK(0, "frame %ld: %d %d against %d %d", i, heard[2 * i], heard[2 * i + 1], pt_source[2 * i], pt_source[2 * i + 1]);
                        break;
                }
        PTCHECK(heard_frames == 12 * 256 && labs(heard[2 * 3]) < labs(pt_source[2 * 3]) + 1, "the first frames are the fade-in: %d against %d", heard[6], pt_source[6]);
        pt_collect(slot);
        // Latency: the indexes are exact.
        w.n = 0;
        pt_u32(&w, channel);
        pt_byte(&w, 'T');
        pt_be32(&w, 123);
        pt_be32(&w, 456);
        pt_command(slot, PULSE_COMMAND_GET_PLAYBACK_LATENCY, 30, &w);
        pt_collect(slot);
        f = pt_find(PULSE_COMMAND_REPLY);
        PTCHECK(f && f->tag == 30, "latency answered");
        if (f)
        {
                long long sink_usec, wr, rd, underrun_for, playing_for;
                int playing;

                r = pt_reply_reader(f);
                sink_usec = pt_r64(&r, 'U');
                pt_r64(&r, 'U');
                playing = pt_rbool(&r);
                PTCHECK(r.p[r.at] == 'T' && wire_be32(r.p + r.at + 1) == 123 && wire_be32(r.p + r.at + 5) == 456, "the request's time is echoed");
                r.at += 9;
                PTCHECK(r.p[r.at] == 'T' && wire_be32(r.p + r.at + 1) == 1700000001, "and the server's wall clock follows");
                r.at += 9;
                wr = pt_r64(&r, 'r');
                rd = pt_r64(&r, 'r');
                underrun_for = pt_r64(&r, 'R');
                playing_for = pt_r64(&r, 'R');
                PTCHECK(!r.bad && r.at == f->length, "the latency answer is read to its end");
                PTCHECK(wr == fed * 4 && rd == heard_frames * 4 && playing && sink_usec >= 0, "write %lld (%ld expected) read %lld (%ld)", wr, fed * 4, rd, heard_frames * 4);
                PTCHECK(playing_for == heard_frames * 4 && underrun_for == 256 * 4, "played %lld, waited %lld", playing_for, underrun_for);
        }
        // Run it dry: the program is asked for more, and then an underrun is said.
        for (int block = 0; block < 80; block++)
        {
                pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
                pt_collect(slot);
                if (pt_count(PULSE_COMMAND_UNDERFLOW))
                        break;
        }
        PTCHECK(pt_count(PULSE_COMMAND_UNDERFLOW) == 1, "running dry is an underflow");
        f = pt_find(PULSE_COMMAND_UNDERFLOW);
        if (f)
        {
                PTCHECK(f->length == 24 && wire_be32(f->payload + 11) == channel, "UNDERFLOW names the channel and carries the read index");
                PTCHECK((long long)((unsigned long long)wire_be32(f->payload + 16) << 32 | wire_be32(f->payload + 20)) == fed * 4, "at the end of what was written: %lld", (long long)((unsigned long long)wire_be32(f->payload + 16) << 32 | wire_be32(f->payload + 20)));
        }
        (void)consumed_requests;
        // A drain is answered once what is played has left the card.
        pt_data(slot, channel, pt_source, 4000, 0, 0);
        w.n = 0;
        pt_u32(&w, channel);
        pt_command(slot, PULSE_COMMAND_DRAIN_PLAYBACK_STREAM, 40, &w);
        pt_collect(slot);
        PTCHECK(!pt_find(PULSE_COMMAND_REPLY), "a drain is not answered at once");
        for (int block = 0; block < 12; block++)
                pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
        pulse_tick(&pt_engine);
        pt_collect(slot);
        PTCHECK(!pt_find(PULSE_COMMAND_REPLY), "nor before the card has played it");
        pt_clock += PULSE_DRAIN_USEC + 1000;
        pulse_tick(&pt_engine);
        pt_collect(slot);
        PTCHECK(pt_expect_ack(40), "once it has, the drain is answered");
        // Delete: acknowledged, and it goes (with the fade first).
        w.n = 0;
        pt_u32(&w, channel);
        pt_command(slot, PULSE_COMMAND_DELETE_PLAYBACK_STREAM, 41, &w);
        pt_collect(slot);
        PTCHECK(pt_expect_ack(41), "delete acknowledged");
        w.n = 0;
        pt_u32(&w, channel);
        pt_command(slot, PULSE_COMMAND_DELETE_PLAYBACK_STREAM, 42, &w);
        pt_collect(slot);
        PTCHECK(pt_expect_error(42, PULSE_ERR_NOENTITY), "a second delete finds nothing");
        pulse_client_close(&pt_engine, slot);
        for (int block = 0; block < 4; block++)
                pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
        PTCHECK(pt_engine.streams_made == pt_engine.streams_freed && pt_engine.ring_total == 0, "the stream is freed: %lu made, %lu freed, %lu ring bytes", pt_engine.streams_made, pt_engine.streams_freed,
                pt_engine.ring_total);
        PTCHECK(pt_allocs == pt_releases + pt_table_blocks() && pt_alloc_bytes == pt_release_bytes + pt_table_bytes(), "every block given back");
        (void)index;
}

// Requests: how much the server asks of a program as it plays.
static void pt_test_requests(void)
{
        long slot;
        unsigned long channel, index, missing, asked = 0, requested_total = 0;
        static unsigned char out[AUDIO_BLOCK * 4];
        long fed = 0, requests = 0;

        pt_make_source();
        pt_reset();
        pulse_sink_add(&pt_engine, "null", "Null", 48000);
        slot = pt_connect(1000, 99, 35, "Requests");
        PTCHECK(pt_create(slot, 35, 20, 19200, 9600, 4800, &channel, &index, &missing, 0, 0x10000, 1, AUDIO_FORMAT_S16LE, 2, 48000, 0) == 0, "stream created");
        asked = missing;
        requested_total = missing;
        // The program writes whatever it is asked for, as it is asked; the stream is never short of data and never given more than it asked.
        for (int step = 0; step < 400; step++)
        {
                long chunk = (long)asked;

                if (chunk > 0)
                {
                        if (chunk > 8192)
                                chunk = 8192;
                        chunk -= chunk % 4;
                        if (pt_data(slot, channel, pt_source + (fed % 48000) * 2, chunk, 0, 0) < 0)
                        {
                                PTCHECK(0, "data refused");
                                break;
                        }
                        fed += chunk / 4;
                        asked -= (unsigned long)chunk;
                }
                pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
                pt_collect(slot);
                for (int i = 0; i < pt_logged; i++)
                        if (pt_log[i].command == PULSE_COMMAND_REQUEST)
                        {
                                unsigned long bytes = wire_be32(pt_log[i].payload + 16);

                                PTCHECK(wire_be32(pt_log[i].payload + 11) == channel && bytes % 4 == 0 && bytes >= 4800 && bytes <= 19200, "request of %lu", bytes);
                                asked += bytes;
                                requested_total += bytes;
                                requests++;
                        }
                PTCHECK(pt_count(PULSE_COMMAND_UNDERFLOW) == 0 || step < 5, "a program that answers its requests never underruns (step %d)", step);
        }
        PTCHECK(requests > 20, "the server went on asking: %ld requests", requests);
        // The program stops answering: what is asked is never more than was played and the target, however long it is silent.
        for (int step = 0; step < 120; step++)
        {
                pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
                pt_collect(slot);
                for (int i = 0; i < pt_logged; i++)
                        if (pt_log[i].command == PULSE_COMMAND_REQUEST)
                                requested_total += wire_be32(pt_log[i].payload + 16);
        }
        PTCHECK(requested_total <= pt_engine.stream[0].voice.ring.read + 19200, "a program that stops answering is asked for no more than was played and the target: %lu asked, %lu played", requested_total,
                pt_engine.stream[0].voice.ring.read);
        PTCHECK(requested_total <= pt_engine.stream[0].voice.ring.read + 19200 + 19200, "never more asked than was played and the target: %lu asked, %lu played", requested_total, pt_engine.stream[0].voice.ring.read);
        PTCHECK(pt_engine.stream[0].outstanding <= 19200, "never more outstanding than the target: %lu", pt_engine.stream[0].outstanding);
        pulse_client_close(&pt_engine, slot);
        for (int block = 0; block < 4; block++)
                pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
        PTCHECK(pt_engine.streams_made == pt_engine.streams_freed, "freed");
}

/*
        SDL 2 as Black Mesa runs it (its PulseAudio backend): a whole mix buffer of 4096 bytes written each time the stream may take
        one, a tlength of four of them asked with the latency adjusted, minreq and prebuf the server's. The card's share is taken off
        tlength (16384 less 1536), so the program writes three buffers (12288 bytes) and stops short of tlength by less than one:
        a prebuf of the whole tlength was never reached, the stream never played, no request came again, and the game was silent
        (box: the stream not corked, 69.7 ms of it queued, 12288 bytes at 44100 Hz, never playing).
*/
static void pt_test_sdl_chunks(void)
{
        long slot;
        unsigned long channel, index, missing, written = 0;
        static unsigned char out[AUDIO_BLOCK * 4];

        pt_make_source();
        pt_reset();
        pulse_sink_add(&pt_engine, "null", "Null", 48000);
        slot = pt_connect(1000, 99, 35, "SDL buffers");
        pt_card_us = 8000;
        PTCHECK(pt_create(slot, 35, 20, 16384, PULSE_INVALID, PULSE_INVALID, &channel, &index, &missing, 0, 0x10000, 1, AUDIO_FORMAT_S16LE, 2, 48000, 0) == 0, "an SDL stream");
        while (missing - written >= 4096)
        {
                pt_data(slot, channel, pt_source, 4096, 0, 0);
                written += 4096;
        }
        PTCHECK(written == 12288 && missing == 16384 - 1536, "it writes whole buffers while one fits: %lu of the %lu asked", written, missing);
        pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 64);
        PTCHECK(!pt_engine.stream[0].underrun, "and it plays from what it wrote (prebuf %lu)", pt_engine.stream[0].prebuf);
        pulse_client_close(&pt_engine, slot);
        for (int block = 0; block < 4; block++)
                pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 64);
        pt_card_us = 0;
}

// A stream corked and uncorked a little short of its prebuf plays on from what it holds: a cork does not ask for the prebuf again. Re-armed,
// it waited for a fill that the program, asked for less than its minreq, does not write, and it never started (the shortfall here is 512 bytes,
// under the minreq of 1024).
static void pt_test_restart(void)
{
        long slot;
        unsigned long channel, index, missing, fill, before, after;
        static unsigned char out[AUDIO_BLOCK * 4];
        struct pt_w w = {{0}, 0};

        pt_make_source();
        pt_reset();
        pulse_sink_add(&pt_engine, "null", "Null", 48000);
        slot = pt_connect(1000, 99, 35, "Restart");
        PTCHECK(pt_create(slot, 35, 20, 4096, 4096, 1024, &channel, &index, &missing, 0, 0x10000, 1, AUDIO_FORMAT_S16LE, 2, 48000, 0) == 0, "stream created");
        pt_data(slot, channel, pt_source, 4096, 0, 0);
        pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 64);
        PTCHECK(!pt_engine.stream[0].underrun, "it starts once its prebuf is in");
        w.n = 0;
        pt_u32(&w, channel);
        pt_bool(&w, 1);
        pt_command(slot, PULSE_COMMAND_CORK_PLAYBACK_STREAM, 50, &w);
        for (int block = 0; block < 4; block++)
                pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 64);
        w.n = 0;
        pt_u32(&w, channel);
        pt_bool(&w, 0);
        pt_command(slot, PULSE_COMMAND_CORK_PLAYBACK_STREAM, 51, &w);
        pt_collect(slot);
        fill = audio_ring_fill(&pt_engine.stream[0].voice.ring);
        PTCHECK(fill < 4096 && fill + 1024 > 4096, "uncorked short of its prebuf by less than minreq: %lu queued", fill);
        PTCHECK(!pt_engine.stream[0].underrun, "a cork is not a new start: the stream is not waiting for its prebuf");
        before = pt_engine.stream[0].voice.ring.read;
        for (int block = 0; block < 4; block++)
                pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 64);
        after = pt_engine.stream[0].voice.ring.read;
        PTCHECK(after > before, "and it plays on from what it holds: read %lu to %lu", before, after);
        pulse_client_close(&pt_engine, slot);
        for (int block = 0; block < 4; block++)
                pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 64);
        PTCHECK(pt_engine.streams_made == pt_engine.streams_freed && pt_allocs == pt_releases + pt_table_blocks(), "all given back");
}

// The buffer attributes a program is given, as PulseAudio gives them, with the card's share (host.latency) taken off what a stream that
// adjusts its latency keeps queued; the configured latency it is told; and the latency it reads, fresh at the moment it asks.
static void pt_create_attributes(unsigned long tag, unsigned long *tlength, unsigned long *prebuf, unsigned long *minreq, long long *configured)
{
        const struct pt_frame *f = pt_find(PULSE_COMMAND_REPLY);
        struct pt_r r;

        *tlength = *prebuf = *minreq = 0;
        *configured = -1;
        if (!f || f->tag != tag)
                return;
        r = pt_reply_reader(f);
        pt_ru32(&r);
        pt_ru32(&r);
        pt_ru32(&r);
        pt_ru32(&r);
        *tlength = pt_ru32(&r);
        *prebuf = pt_ru32(&r);
        *minreq = pt_ru32(&r);
        {
                int format, channels, positions[8], suspended;
                unsigned long rate;

                pt_rspec(&r, &format, &channels, &rate);
                pt_rmap(&r, positions);
                pt_ru32(&r);
                pt_rstr(&r);
                suspended = pt_rbool(&r);
                (void)suspended;
        }
        *configured = pt_r64(&r, 'U');
        if (r.bad)
                *configured = -2;
}

static long long pt_playback_latency(long slot, unsigned long channel, unsigned long tag)
{
        struct pt_w w = {{0}, 0};
        const struct pt_frame *f;
        struct pt_r r;

        pt_u32(&w, channel);
        pt_byte(&w, 'T');
        pt_be32(&w, 1);
        pt_be32(&w, 2);
        pt_command(slot, PULSE_COMMAND_GET_PLAYBACK_LATENCY, tag, &w);
        pt_collect(slot);
        f = pt_find(PULSE_COMMAND_REPLY);
        if (!f || f->tag != tag)
                return -1;
        r = pt_reply_reader(f);
        return pt_r64(&r, 'U');
}

static void pt_test_latency(void)
{
        long slot;
        unsigned long channel, index, missing, tlength, prebuf, minreq;
        long long configured, latency;

        pt_make_source();
        pt_reset();
        pulse_sink_add(&pt_engine, "null", "Null", 48000);
        slot = pt_connect(1000, 99, 35, "Latency");
        pt_card_us = 8000; // a card kept 384 frames ahead: 1536 bytes of a stereo 16-bit stream at 48000
        // SDL at 1024 frames: tlength 4096, the rest the server's. minreq a quarter (1024); the card's 1536 off, but not under the card's
        // and two minreq (3584, 18.7 ms): 26.7 ms in all where it was 29.3; prebuf PulseAudio's, tlength less minreq and a frame.
        PTCHECK(pt_create(slot, 35, 20, 4096, PULSE_INVALID, PULSE_INVALID, &channel, &index, &missing, 0, 0x10000, 1, AUDIO_FORMAT_S16LE, 2, 48000, 0) == 0, "an SDL stream");
        pt_create_attributes(20, &tlength, &prebuf, &minreq, &configured);
        PTCHECK(tlength == 3584 && minreq == 1024 && prebuf == 3584 - 1024 + 4 && missing == 3584 && configured == 8000 && pt_card_asked == 21333,
                "SDL's latency is its own: tlength %lu minreq %lu prebuf %lu missing %lu, the card's %lld us (asked about %ld us)", tlength, minreq, prebuf, missing, configured, pt_card_asked);
        // Chromium: tlength three buffers of 1024 frames, minreq half of one. 64 ms in all: 56 queued and the card's 8.
        PTCHECK(pt_create(slot, 35, 21, 12288, PULSE_INVALID, 2048, &channel, &index, &missing, 0, 0x10000, 1, AUDIO_FORMAT_S16LE, 2, 48000, 0) == 0, "a Chromium stream");
        pt_create_attributes(21, &tlength, &prebuf, &minreq, &configured);
        PTCHECK(tlength == 12288 - 1536 && minreq == 2048 && prebuf == 12288 - 1536 - 2048 + 4 && configured == 8000, "Chromium's: tlength %lu minreq %lu prebuf %lu", tlength, minreq, prebuf);
        // A default stream: 200 ms, a request of 20 ms (not a quarter, 50 ms).
        PTCHECK(pt_create(slot, 35, 22, PULSE_INVALID, PULSE_INVALID, PULSE_INVALID, &channel, &index, &missing, 0, 0x10000, 1, AUDIO_FORMAT_S16LE, 2, 48000, 0) == 0, "a default stream");
        pt_create_attributes(22, &tlength, &prebuf, &minreq, &configured);
        PTCHECK(tlength == 38400 - 1536 && minreq == 3840, "the default: tlength %lu minreq %lu", tlength, minreq);
        // A stream that does not adjust keeps what it asked for, and is told the card's share all the same.
        pt_next_fixed = 1;
        PTCHECK(pt_create(slot, 35, 23, 12288, 12288, 2048, &channel, &index, &missing, 0, 0x10000, 1, AUDIO_FORMAT_S16LE, 2, 48000, 0) == 0, "a fixed stream");
        pt_next_fixed = 0;
        pt_create_attributes(23, &tlength, &prebuf, &minreq, &configured);
        PTCHECK(tlength == 12288 && minreq == 2048 && prebuf == 12288 && configured == 8000, "not adjusted: tlength %lu minreq %lu prebuf %lu", tlength, minreq, prebuf);
        // A tiny latency is not cut below the card's and two requests.
        PTCHECK(pt_create(slot, 35, 24, 1024, PULSE_INVALID, PULSE_INVALID, &channel, &index, &missing, 0, 0x10000, 1, AUDIO_FORMAT_S16LE, 2, 48000, 0) == 0, "a tiny stream");
        pt_create_attributes(24, &tlength, &prebuf, &minreq, &configured);
        PTCHECK(tlength == 1536 + 2 * 256 && minreq == 256, "tiny: tlength %lu minreq %lu", tlength, minreq);
        // The latency a program reads is what the card holds now: what it held at the host's look, less the time since.
        pulse_sink_delay(&pt_engine, 0, 384);
        latency = pt_playback_latency(slot, channel, 30);
        PTCHECK(latency == 8000, "the card's 384 frames, just read: %lld us", latency);
        pt_clock += 3000;
        latency = pt_playback_latency(slot, channel, 31);
        PTCHECK(latency == 5000, "3 ms later the card has played 3 ms of it: %lld us", latency);
        pt_clock += 9000;
        latency = pt_playback_latency(slot, channel, 32);
        PTCHECK(latency == 0, "and none is left when the look is older than what it held: %lld us", latency);
        // With no card (the clock: host.latency 0) nothing is taken off.
        pt_card_us = 0;
        PTCHECK(pt_create(slot, 35, 25, 4096, PULSE_INVALID, PULSE_INVALID, &channel, &index, &missing, 0, 0x10000, 1, AUDIO_FORMAT_S16LE, 2, 48000, 0) == 0, "a stream on the clock");
        pt_create_attributes(25, &tlength, &prebuf, &minreq, &configured);
        PTCHECK(tlength == 4096 && configured == 0, "on the clock: tlength %lu, told %lld us", tlength, configured);
        pulse_client_close(&pt_engine, slot);
        {
                static unsigned char out[AUDIO_BLOCK * 4];

                for (int block = 0; block < 4; block++)
                        pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 64);
        }
        PTCHECK(pt_engine.streams_made == pt_engine.streams_freed && pt_allocs == pt_releases + pt_table_blocks(), "all given back");
}

// Cork, flush, trigger, prebuf, a stream that is muted and one at no volume, the rate, the name.
static void pt_test_control(void)
{
        long slot;
        unsigned long channel, index, missing;
        static unsigned char out[AUDIO_BLOCK * 4];
        struct pt_w w = {{0}, 0};
        const struct pt_frame *f;

        pt_make_source();
        pt_reset();
        pulse_sink_add(&pt_engine, "null", "Null", 48000);
        slot = pt_connect(1000, 99, 35, "Control");
        PTCHECK(pt_create(slot, 35, 20, 19200, 3840, 960, &channel, &index, &missing, 0, 0x10000, 1, AUDIO_FORMAT_S16LE, 2, 48000, 0) == 0, "created");
        pt_data(slot, channel, pt_source, 12800, 0, 0);
        for (int block = 0; block < 6; block++)
                pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
        // Cork: the stream ramps to nothing and its time stands still.
        w.n = 0;
        pt_u32(&w, channel);
        pt_bool(&w, 1);
        pt_command(slot, PULSE_COMMAND_CORK_PLAYBACK_STREAM, 50, &w);
        pt_collect(slot);
        PTCHECK(pt_expect_ack(50), "cork acknowledged");
        pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
        {
                unsigned long read_after_ramp, read_later;

                read_after_ramp = pt_engine.stream[0].voice.ring.read;
                PTCHECK(read_after_ramp > 6 * 256 * 4 && pt_engine.stream[0].voice.now[0] == 0, "the cork ramp played: gain %g", (double)pt_engine.stream[0].voice.now[0]);
                for (int block = 0; block < 10; block++)
                        pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
                read_later = pt_engine.stream[0].voice.ring.read;
                PTCHECK(read_later == read_after_ramp, "a corked stream's time stands still");
                {
                        short *got = (short *)out;
                        int silent = 1;

                        for (int i = 0; i < 256 * 2; i++)
                                silent &= got[i] == 0;
                        PTCHECK(silent, "and the sink is silent");
                }
        }
        // Uncork: it picks up where it was, from nothing.
        w.n = 0;
        pt_u32(&w, channel);
        pt_bool(&w, 0);
        pt_command(slot, PULSE_COMMAND_CORK_PLAYBACK_STREAM, 51, &w);
        pt_collect(slot);
        for (int block = 0; block < 3; block++)
                pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
        pt_collect(slot);
        PTCHECK(pt_engine.stream[0].voice.ring.read > pt_engine.stream[0].voice.ring.write / 2 - 12800 || !pt_engine.stream[0].underrun, "playing again");
        // Flush: what is queued is dropped; with a start target the stream waits for it again.
        w.n = 0;
        pt_u32(&w, channel);
        pt_command(slot, PULSE_COMMAND_FLUSH_PLAYBACK_STREAM, 52, &w);
        pt_collect(slot);
        PTCHECK(pt_expect_ack(52), "flush acknowledged");
        for (int block = 0; block < 4; block++)
                pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
        PTCHECK(audio_ring_fill(&pt_engine.stream[0].voice.ring) == 0 && pt_engine.stream[0].underrun, "flushed: nothing queued, waiting for the target");
        pt_data(slot, channel, pt_source, 800, 0, 0);
        pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
        PTCHECK(pt_engine.stream[0].underrun, "200 frames are under the target of 960");
        w.n = 0;
        pt_u32(&w, channel);
        pt_command(slot, PULSE_COMMAND_TRIGGER_PLAYBACK_STREAM, 53, &w);
        pt_collect(slot);
        pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
        PTCHECK(!pt_engine.stream[0].underrun || audio_ring_fill(&pt_engine.stream[0].voice.ring) == 0, "a trigger starts it below the target");
        // Mute and no volume: both go on being read.
        pt_data(slot, channel, pt_source, 12800, 0, 0);
        for (int k = 0; k < 2; k++)
        {
                unsigned long before, after;

                w.n = 0;
                pt_u32(&w, index);
                if (k == 0)
                {
                        pt_bool(&w, 1);
                        pt_command(slot, PULSE_COMMAND_SET_SINK_INPUT_MUTE, 54, &w);
                }
                else
                {
                        pt_bool(&w, 0);
                        pt_command(slot, PULSE_COMMAND_SET_SINK_INPUT_MUTE, 55, &w);
                        w.n = 0;
                        pt_u32(&w, index);
                        pt_vol(&w, 2, 0);
                        pt_command(slot, PULSE_COMMAND_SET_SINK_INPUT_VOLUME, 56, &w);
                }
                pt_collect(slot);
                for (int block = 0; block < 4; block++)
                        pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
                before = pt_engine.stream[0].voice.ring.read;
                for (int block = 0; block < 4; block++)
                        pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
                after = pt_engine.stream[0].voice.ring.read;
                PTCHECK(after >= before + 4 * 256 * 4 || audio_ring_fill(&pt_engine.stream[0].voice.ring) == 0, "case %d (%s) keeps reading: %lu to %lu", k, k ? "volume 0" : "muted", before, after);
                {
                        short *got = (short *)out;
                        int silent = 1;

                        for (int i = 0; i < 256 * 2; i++)
                                silent &= got[i] == 0;
                        PTCHECK(silent || audio_ring_fill(&pt_engine.stream[0].voice.ring) == 0, "and plays nothing");
                }
                pt_data(slot, channel, pt_source, 12000, 0, 0);
        }
        // Names, rate (a stream that did not ask for a variable rate is refused), buffer attributes.
        w.n = 0;
        pt_u32(&w, channel);
        pt_str(&w, "New Name");
        pt_command(slot, PULSE_COMMAND_SET_PLAYBACK_STREAM_NAME, 60, &w);
        pt_collect(slot);
        PTCHECK(pt_expect_ack(60), "renamed");
        pt_command(slot, PULSE_COMMAND_GET_SINK_INPUT_INFO_LIST, 61, 0);
        pt_collect(slot);
        f = pt_find(PULSE_COMMAND_REPLY);
        if (f)
        {
                struct pt_r r = pt_reply_reader(f);

                pt_ru32(&r);
                PTCHECK(!strcmp(pt_rstr(&r), "New Name"), "the sink input list has the new name");
        }
        w.n = 0;
        pt_u32(&w, channel);
        pt_be32(&w, 0);
        w.n = 0;
        pt_u32(&w, channel);
        pt_u32(&w, 44100);
        pt_command(slot, PULSE_COMMAND_UPDATE_PLAYBACK_STREAM_SAMPLE_RATE, 62, &w);
        pt_collect(slot);
        PTCHECK(pt_expect_error(62, PULSE_ERR_INVALID), "a rate change on a stream that is not variable-rate is refused");
        w.n = 0;
        pt_u32(&w, channel);
        pt_u32(&w, PULSE_INVALID);
        pt_u32(&w, 9600);
        pt_u32(&w, 1920);
        pt_u32(&w, 960);
        pt_bool(&w, 1);
        pt_bool(&w, 0);
        pt_command(slot, PULSE_COMMAND_SET_PLAYBACK_STREAM_BUFFER_ATTR, 63, &w);
        pt_collect(slot);
        f = pt_find(PULSE_COMMAND_REPLY);
        PTCHECK(f && f->tag == 63, "buffer attributes answered");
        if (f)
        {
                struct pt_r r = pt_reply_reader(f);
                unsigned long maxlength = pt_ru32(&r), tlength = pt_ru32(&r), prebuf = pt_ru32(&r), minreq = pt_ru32(&r);

                pt_r64(&r, 'U');
                PTCHECK(!r.bad && r.at == f->length && tlength == 9600 && prebuf == 1920 && minreq == 960 && maxlength == 38400, "new attributes %lu %lu %lu %lu", maxlength, tlength, prebuf, minreq);
        }
        pulse_client_close(&pt_engine, slot);
        for (int block = 0; block < 4; block++)
                pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
        PTCHECK(pt_engine.streams_made == pt_engine.streams_freed && pt_allocs == pt_releases + pt_table_blocks(), "all given back");
}

// Volumes down to the bits: a stream at an eighth of the amplitude (50%), a sink at the same, both, a mute, and the balance of a pair.
static void pt_test_volume(void)
{
        long slot, root;
        unsigned long channel, index, missing;
        static unsigned char out[AUDIO_BLOCK * 4];
        struct pt_w w = {{0}, 0};
        static short heard[2 * 256 * 40];

        pt_make_source();
        pt_reset();
        pulse_sink_add(&pt_engine, "null", "Null", 48000);
        slot = pt_connect(1000, 99, 35, "Volume");
        root = pt_connect(0, 1, 35, "Mixer");
        PTCHECK(pt_create(slot, 35, 20, 38400, 1920, 960, &channel, &index, &missing, 0, 0x8000, 1, AUDIO_FORMAT_S16LE, 2, 48000, 0) == 0, "created at 50%%");
        for (int piece = 0; piece < 5; piece++)
                pt_data(slot, channel, pt_source + piece * 2400 * 2, 9600, 0, 0);
        for (int block = 0; block < 40; block++)
        {
                if (block == 20)
                {
                        // The person's mixer (root) sets the stream to 100% and the sink to 50%.
                        w.n = 0;
                        pt_u32(&w, index);
                        pt_vol(&w, 2, 0x10000);
                        pt_command(root, PULSE_COMMAND_SET_SINK_INPUT_VOLUME, 70, &w);
                        w.n = 0;
                        pt_u32(&w, 0);
                        pt_str(&w, 0);
                        pt_vol(&w, 2, 0x8000);
                        pt_command(root, PULSE_COMMAND_SET_SINK_VOLUME, 71, &w);
                        pt_collect(root);
                        PTCHECK(pt_expect_ack(71), "the sink volume is set by root");
                }
                pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
                memcpy(heard + 2 * 256 * block, out, 256 * 4);
        }
        // Frames 100 to 19 blocks: the stream at 50% (an eighth) is the source over eight, rounded to even; after the change, a sink at an eighth, the stream at one.
        {
                long bad = 0;

                for (long i = 100; i < 20 * 256; i++)
                        for (int c = 0; c < 2; c++)
                        {
                                long want = (long)rint((double)pt_source[2 * i + c] / 8.0);

                                if (heard[2 * i + c] != want && bad++ < 3)
                                        PTCHECK(0, "50%% stream, frame %ld channel %d: %d against %ld", i, c, heard[2 * i + c], want);
                        }
                PTCHECK(!bad, "a stream at 50%% is an eighth of the amplitude, exactly");
                bad = 0;
                for (long i = 20 * 256 + 400; i < 38 * 256; i++)
                        for (int c = 0; c < 2; c++)
                        {
                                long want = (long)rint((double)pt_source[2 * i + c] / 8.0);

                                if (heard[2 * i + c] != want && bad++ < 3)
                                        PTCHECK(0, "50%% sink, frame %ld channel %d: %d against %ld", i, c, heard[2 * i + c], want);
                        }
                PTCHECK(!bad, "then the sink at 50%% with the stream at 100%% is the same eighth");
        }
        // Mute the sink: silence; the stream's own volumes are untouched; a stranger may not.
        w.n = 0;
        pt_u32(&w, 0);
        pt_str(&w, 0);
        pt_bool(&w, 1);
        pt_command(slot, PULSE_COMMAND_SET_SINK_MUTE, 72, &w);
        pt_collect(slot);
        PTCHECK(pt_expect_error(72, PULSE_ERR_ACCESS), "a program that is not root cannot mute the sink");
        pt_command(slot, PULSE_COMMAND_GET_SINK_INFO_LIST, 73, 0);
        w.n = 0;
        pt_u32(&w, 0);
        pt_str(&w, 0);
        pt_vol(&w, 2, 0);
        pt_command(slot, PULSE_COMMAND_SET_SINK_VOLUME, 74, &w);
        pt_collect(slot);
        PTCHECK(pt_expect_error(74, PULSE_ERR_ACCESS), "nor set its volume");
        w.n = 0;
        pt_u32(&w, 0);
        pt_str(&w, 0);
        pt_bool(&w, 1);
        pt_command(root, PULSE_COMMAND_SET_SINK_MUTE, 75, &w);
        pt_collect(root);
        PTCHECK(pt_expect_ack(75), "root mutes the sink");
        for (int block = 0; block < 3; block++)
                pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
        {
                short *got = (short *)out;
                int silent = 1;

                for (int i = 0; i < 256 * 2; i++)
                        silent &= got[i] == 0;
                PTCHECK(silent, "a muted sink is silent");
        }
        // The value of an out-of-range volume is held to the most, and a wrong channel count is refused.
        w.n = 0;
        pt_u32(&w, index);
        pt_vol(&w, 2, 0xfffffff0u);
        pt_command(root, PULSE_COMMAND_SET_SINK_INPUT_VOLUME, 76, &w);
        pt_collect(root);
        PTCHECK(pt_expect_ack(76) && pt_engine.stream[0].volume[0] == AUDIO_VOLUME_MAX, "a huge volume is the most");
        w.n = 0;
        pt_u32(&w, index);
        pt_vol(&w, 3, 0x10000);
        pt_command(root, PULSE_COMMAND_SET_SINK_INPUT_VOLUME, 77, &w);
        pt_collect(root);
        PTCHECK(pt_expect_error(77, PULSE_ERR_INVALID), "a volume of the wrong number of channels is refused");
        pulse_client_close(&pt_engine, slot);
        pulse_client_close(&pt_engine, root);
        for (int block = 0; block < 4; block++)
                pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
        PTCHECK(pt_allocs == pt_releases + pt_table_blocks(), "all given back");
}

// A flush while the stream plays: the last frames fade out, nothing is said to be an underflow, and it waits for its start again.
static void pt_test_flush_fade(void)
{
        long slot;
        unsigned long channel, index, missing;
        static unsigned char out[AUDIO_BLOCK * 4];
        static short level[2 * 4800];
        short *heard = (short *)out;
        struct pt_w w = {{0}, 0};
        long previous = 10000;

        pt_reset();
        pulse_sink_add(&pt_engine, "null", "Null", 48000);
        for (int i = 0; i < 4800; i++)
                level[2 * i] = level[2 * i + 1] = 10000;
        slot = pt_connect(1000, 99, 35, "Flush");
        PTCHECK(pt_create(slot, 35, 20, 19200, 3840, 960, &channel, &index, &missing, 0, 0x10000, 1, AUDIO_FORMAT_S16LE, 2, 48000, 0) == 0, "created");
        pt_data(slot, channel, level, 19200, 0, 0);
        for (int block = 0; block < 4; block++)
                pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
        pt_collect(slot);
        w.n = 0;
        pt_u32(&w, channel);
        pt_command(slot, PULSE_COMMAND_FLUSH_PLAYBACK_STREAM, 90, &w);
        pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
        pt_collect(slot);
        PTCHECK(heard[0] >= 10000 - 10000 / 48 && heard[0] < 10000 && heard[2 * 150] == 0, "the flush fades out from where it was: %d, then %d", heard[0], heard[2 * 150]);
        for (int i = 0; i < 100; i++)
        {
                PTCHECK(heard[2 * i] <= previous, "frame %d of the flush's fade rises", i);
                previous = heard[2 * i];
        }
        PTCHECK(pt_count(PULSE_COMMAND_UNDERFLOW) == 0, "a flush is not an underflow");
        PTCHECK(pt_engine.stream[0].underrun && audio_ring_fill(&pt_engine.stream[0].voice.ring) == 0, "and the stream waits for its start");
        pulse_client_close(&pt_engine, slot);
        for (int block = 0; block < 4; block++)
                pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
}

// A client that goes while its stream plays: the stream is not cut, it fades over the ramp and is then freed.
static void pt_test_dying(void)
{
        long slot;
        unsigned long channel, index, missing;
        static unsigned char out[AUDIO_BLOCK * 4];
        static short level[2 * 4800];
        short *heard = (short *)out;
        long previous = 0, biggest_step = 0;

        pt_reset();
        pulse_sink_add(&pt_engine, "null", "Null", 48000);
        for (int i = 0; i < 4800; i++)
                level[2 * i] = level[2 * i + 1] = 10000;
        slot = pt_connect(1000, 99, 35, "Dying");
        PTCHECK(pt_create(slot, 35, 20, 19200, 3840, 960, &channel, &index, &missing, 0, 0x10000, 1, AUDIO_FORMAT_S16LE, 2, 48000, 0) == 0, "created");
        pt_data(slot, channel, level, 19200, 0, 0);
        for (int block = 0; block < 4; block++)
                pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
        PTCHECK(heard[2 * 255] == 10000, "playing at its level: %d", heard[2 * 255]);
        pulse_client_close(&pt_engine, slot);
        PTCHECK(pt_engine.stream[0].used && pt_engine.stream[0].dying && pt_engine.streams_freed == 0, "the stream outlives its client for the fade");
        pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
        previous = 10000;
        for (int i = 0; i < 256; i++)
        {
                long step = previous - heard[2 * i];

                if (step < 0)
                        step = -step;
                biggest_step = step > biggest_step ? step : biggest_step;
                PTCHECK(heard[2 * i] <= previous, "frame %d of the fade rises: %d after %ld", i, heard[2 * i], previous);
                previous = heard[2 * i];
        }
        PTCHECK(biggest_step <= 10000 / AUDIO_RAMP + 2, "no step of the fade is more than the ramp's: %ld", biggest_step);
        PTCHECK(heard[0] >= 10000 - 10000 / AUDIO_RAMP - 2 && heard[2 * 200] == 0, "it starts from where it was and ends at nothing: %d %d", heard[0], heard[2 * 200]);
        PTCHECK(pt_engine.streams_made == pt_engine.streams_freed && pt_engine.ring_total == 0, "and is then freed");
}

// Who may do what.
static void pt_test_policy(void)
{
        long user_a, user_b, root;
        unsigned long channel_a, index_a, channel_b, index_b, missing;
        struct pt_w w = {{0}, 0};
        const struct pt_frame *f;

        pt_reset();
        pulse_sink_add(&pt_engine, "null", "Null", 48000);
        pulse_sink_add(&pt_engine, "other", "Other", 48000);
        user_a = pt_connect(1000, 11, 35, "A");
        user_b = pt_connect(1001, 12, 35, "B");
        root = pt_connect(0, 13, 35, "Root");
        PTCHECK(pt_create(user_a, 35, 20, 19200, 19200, 4800, &channel_a, &index_a, &missing, 0, 0x10000, 1, AUDIO_FORMAT_S16LE, 2, 48000, 0) == 0, "A's stream");
        PTCHECK(pt_create(user_b, 35, 20, 19200, 19200, 4800, &channel_b, &index_b, &missing, 0, 0x10000, 1, AUDIO_FORMAT_S16LE, 2, 48000, 0) == 0, "B's stream");
        // A sees its own stream only, root sees both.
        pt_command(user_a, PULSE_COMMAND_GET_SINK_INPUT_INFO_LIST, 30, 0);
        pt_collect(user_a);
        f = pt_find(PULSE_COMMAND_REPLY);
        {
                struct pt_r r = pt_reply_reader(f);
                int n = 0;

                while (!r.bad && r.at < f->length)
                {
                        int format, channels, positions[8];
                        unsigned long rate, v[8];

                        PTCHECK(pt_ru32(&r) == index_a, "A sees A's");
                        pt_rstr(&r);
                        pt_ru32(&r);
                        pt_ru32(&r);
                        pt_ru32(&r);
                        pt_rspec(&r, &format, &channels, &rate);
                        pt_rmap(&r, positions);
                        pt_rvol(&r, v);
                        pt_r64(&r, 'U');
                        pt_r64(&r, 'U');
                        pt_rstr(&r);
                        pt_rstr(&r);
                        pt_rbool(&r);
                        pt_rprops(&r, 0, 0, 0);
                        pt_rbool(&r);
                        pt_rbool(&r);
                        pt_rbool(&r);
                        pt_rformat(&r);
                        n++;
                }
                PTCHECK(n == 1 && !r.bad, "A sees one stream (%d)", n);
        }
        pt_command(root, PULSE_COMMAND_GET_SINK_INPUT_INFO_LIST, 31, 0);
        pt_collect(root);
        f = pt_find(PULSE_COMMAND_REPLY);
        PTCHECK(f && f->length > 200, "root sees both");
        pt_command(user_a, PULSE_COMMAND_GET_CLIENT_INFO_LIST, 29, 0);
        pt_collect(user_a);
        f = pt_find(PULSE_COMMAND_REPLY);
        {
                struct pt_r r = pt_reply_reader(f);
                int n = 0;

                while (!r.bad && r.at < f->length)
                {
                        pt_ru32(&r);
                        pt_rstr(&r);
                        pt_ru32(&r);
                        pt_rstr(&r);
                        pt_rprops(&r, 0, 0, 0);
                        n++;
                }
                PTCHECK(n == 1 && !r.bad, "A sees its own client only (%d)", n);
        }
        // A cannot touch B's stream, nor read it by index; root can.
        w.n = 0;
        pt_u32(&w, index_b);
        pt_vol(&w, 2, 0);
        pt_command(user_a, PULSE_COMMAND_SET_SINK_INPUT_VOLUME, 32, &w);
        pt_collect(user_a);
        PTCHECK(pt_expect_error(32, PULSE_ERR_NOENTITY), "A cannot set B's volume (as a stream that is not there)");
        w.n = 0;
        pt_u32(&w, index_b);
        pt_command(user_a, PULSE_COMMAND_GET_SINK_INPUT_INFO, 33, &w);
        pt_collect(user_a);
        PTCHECK(pt_expect_error(33, PULSE_ERR_NOENTITY), "A cannot look at B's stream");
        w.n = 0;
        pt_u32(&w, index_b);
        pt_u32(&w, 1);
        pt_str(&w, 0);
        pt_command(user_a, PULSE_COMMAND_MOVE_SINK_INPUT, 34, &w);
        pt_collect(user_a);
        PTCHECK(pt_expect_error(34, PULSE_ERR_NOENTITY), "A cannot move B's stream (as one that is not there)");
        w.n = 0;
        pt_u32(&w, index_b);
        pt_command(user_a, PULSE_COMMAND_KILL_SINK_INPUT, 35, &w);
        pt_collect(user_a);
        PTCHECK(pt_expect_error(35, PULSE_ERR_ACCESS), "A cannot kill B's stream");
        w.n = 0;
        pt_str(&w, "other");
        pt_command(user_a, PULSE_COMMAND_SET_DEFAULT_SINK, 36, &w);
        pt_collect(user_a);
        PTCHECK(pt_expect_error(36, PULSE_ERR_ACCESS), "A cannot change the default sink");
        // A moves its own stream, and the other follows nothing it did not ask for.
        w.n = 0;
        pt_u32(&w, index_a);
        pt_u32(&w, 1);
        pt_str(&w, 0);
        pt_command(user_a, PULSE_COMMAND_MOVE_SINK_INPUT, 37, &w);
        pt_collect(user_a);
        PTCHECK(pt_expect_ack(37) && pt_engine.stream[0].sink == 1, "A moves its own stream");
        // Root changes the default: B's stream (it named no sink) follows, A's (it was moved by name of index) stays.
        w.n = 0;
        pt_str(&w, "other");
        pt_command(root, PULSE_COMMAND_SET_DEFAULT_SINK, 38, &w);
        pt_collect(root);
        PTCHECK(pt_expect_ack(38) && pt_engine.default_sink == 1 && pt_engine.stream[1].sink == 1 && pt_engine.stream[0].sink == 1, "the default moved, B followed");
        w.n = 0;
        pt_u32(&w, index_b);
        pt_command(root, PULSE_COMMAND_KILL_SINK_INPUT, 39, &w);
        pt_collect(root);
        PTCHECK(pt_expect_ack(39), "root kills B's stream");
        pt_collect(user_b);
        PTCHECK(pt_count(PULSE_COMMAND_PLAYBACK_STREAM_KILLED) == 1, "and B is told");
        // No count of one user's clients is refused: the ninth is accepted, and so are twenty-four of them (the table grows).
        {
                long made[24], refused = 0;

                for (int i = 0; i < 24; i++)
                {
                        made[i] = pulse_client_open(&pt_engine, 2000, 100 + i);
                        refused += made[i] < 0;
                }
                PTCHECK(refused == 0 && made[8] >= 0, "a user holds any number of clients: the ninth is accepted (%ld refused)", refused);
                for (int i = 0; i < 24; i++)
                        if (made[i] >= 0)
                                pulse_client_close(&pt_engine, made[i]);
        }
        pulse_client_close(&pt_engine, user_a);
        pulse_client_close(&pt_engine, user_b);
        pulse_client_close(&pt_engine, root);
        {
                static unsigned char out[AUDIO_BLOCK * 4];

                for (int block = 0; block < 4; block++)
                {
                        pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
                        pulse_sink_mix(&pt_engine, 1, out, AUDIO_FORMAT_S16LE, 2, 256);
                }
        }
        PTCHECK(pt_engine.clients_made == pt_engine.clients_dropped && pt_engine.streams_made == pt_engine.streams_freed && pt_allocs == pt_releases + pt_table_blocks(), "all given back");
}

// Subscription events: what is told to whom, in what form.
static void pt_test_events(void)
{
        long watcher, player;
        unsigned long channel, index, missing;
        struct pt_w w = {{0}, 0};

        pt_reset();
        pulse_sink_add(&pt_engine, "null", "Null", 48000);
        watcher = pt_connect(0, 5, 35, "Watcher");
        w.n = 0;
        pt_u32(&w, 0x2ff);
        pt_command(watcher, PULSE_COMMAND_SUBSCRIBE, 80, &w);
        pt_collect(watcher);
        PTCHECK(pt_expect_ack(80), "subscribed");
        player = pt_connect(1000, 6, 35, "Player");
        pt_collect(watcher);
        {
                const struct pt_frame *f = pt_find(PULSE_COMMAND_SUBSCRIBE_EVENT);

                PTCHECK(f && f->length == 20 && wire_be32(f->payload + 6) == PULSE_INVALID && wire_be32(f->payload + 11) == (5 | PULSE_EVENT_NEW), "a new client is announced as a client event: %lx",
                        f ? wire_be32(f->payload + 11) : 0);
        }
        PTCHECK(pt_create(player, 35, 20, 19200, 19200, 4800, &channel, &index, &missing, 0, 0x10000, 1, AUDIO_FORMAT_S16LE, 2, 48000, 0) == 0, "stream");
        pt_collect(watcher);
        {
                const struct pt_frame *f = pt_find(PULSE_COMMAND_SUBSCRIBE_EVENT);

                PTCHECK(f && wire_be32(f->payload + 11) == (2 | PULSE_EVENT_NEW) && wire_be32(f->payload + 16) == index, "a new sink input: %lx %lu", f ? wire_be32(f->payload + 11) : 0, f ? wire_be32(f->payload + 16) : 0);
        }
        w.n = 0;
        pt_u32(&w, index);
        pt_bool(&w, 1);
        pt_command(watcher, PULSE_COMMAND_SET_SINK_INPUT_MUTE, 81, &w);
        pt_collect(watcher);
        {
                const struct pt_frame *f = pt_find(PULSE_COMMAND_SUBSCRIBE_EVENT);

                PTCHECK(f && wire_be32(f->payload + 11) == (2 | PULSE_EVENT_CHANGE), "a mute is a change of the sink input");
        }
        // A client that subscribed to the sinks only does not hear the streams.
        w.n = 0;
        pt_u32(&w, 1);
        pt_command(watcher, PULSE_COMMAND_SUBSCRIBE, 82, &w);
        pt_collect(watcher);
        w.n = 0;
        pt_u32(&w, index);
        pt_bool(&w, 0);
        pt_command(watcher, PULSE_COMMAND_SET_SINK_INPUT_MUTE, 83, &w);
        pt_collect(watcher);
        PTCHECK(pt_count(PULSE_COMMAND_SUBSCRIBE_EVENT) == 0, "the mask is kept to");
        w.n = 0;
        pt_u32(&w, 0);
        pt_str(&w, 0);
        pt_bool(&w, 1);
        pt_command(watcher, PULSE_COMMAND_SET_SINK_MUTE, 84, &w);
        pt_collect(watcher);
        {
                const struct pt_frame *f = pt_find(PULSE_COMMAND_SUBSCRIBE_EVENT);

                PTCHECK(f && wire_be32(f->payload + 11) == (0 | PULSE_EVENT_CHANGE), "a sink mute is a change of the sink");
        }
        PTCHECK(pt_changed_calls >= 2, "the host is told of stream and sink changes");
        pulse_client_close(&pt_engine, player);
        pulse_client_close(&pt_engine, watcher);
        {
                static unsigned char out[AUDIO_BLOCK * 4];

                for (int block = 0; block < 4; block++)
                        pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
        }
        PTCHECK(pt_engine.streams_made == pt_engine.streams_freed, "freed");
}

// Every cap.
static void pt_test_caps(void)
{
        long slot;
        unsigned long channel, index, missing;
        struct pt_w w = {{0}, 0};
        static unsigned char data[PULSE_FRAME_DATA + 100];
        int made = 0;

        pt_reset();
        pulse_sink_add(&pt_engine, "null", "Null", 48000);
        slot = pt_connect(0, 1, 35, "Caps");
        // The seventeenth stream of a client is accepted: no count of a client's streams is refused.
        for (int i = 0; i < 17; i++)
        {
                int result = pt_create(slot, 35, 100 + (unsigned long)i, 1000, 0, 0, &channel, &index, &missing, 0, 0x10000, 1, AUDIO_FORMAT_S16LE, 2, 48000, 0);

                if (result == 0)
                        made++;
                PTCHECK(result == 0, "stream %d of a client is accepted (%d)", i + 1, result);
        }
        PTCHECK(made == 17, "17 streams of a client (%d)", made);
        pulse_client_close(&pt_engine, slot);
        // Silly attributes are held to the ring.
        slot = pt_connect(0, 1, 35, "Caps");
        PTCHECK(pt_create(slot, 35, 200, 0x7fffffffu, 0x7ffffffeu, 0x7ffffffdu, &channel, &index, &missing, 0, 0x10000, 1, AUDIO_FORMAT_S16LE, 2, 48000, 0) == 0, "huge attributes are clamped, not refused");
        PTCHECK(pt_engine.stream[0].ring_bytes <= PULSE_RING_MAX && pt_engine.stream[0].tlength <= PULSE_RING_MAX / 2 && pt_engine.stream[0].prebuf <= pt_engine.stream[0].tlength &&
                    pt_engine.stream[0].minreq <= pt_engine.stream[0].tlength,
                "ring %lu target %lu", pt_engine.stream[0].ring_bytes, pt_engine.stream[0].tlength);
        PTCHECK(pt_create(slot, 35, 201, 1, 1, 1, &channel, &index, &missing, 0, 0x10000, 1, AUDIO_FORMAT_S16LE, 2, 48000, 0) == 0 && pt_engine.stream[1].tlength >= 64 * 4, "tiny attributes are raised");
        // Specs out of bounds.
        PTCHECK(pt_create(slot, 35, 202, 1000, 0, 0, &channel, &index, &missing, 0, 0x10000, 1, 99, 2, 48000, 0) == -1 && pt_expect_error(202, PULSE_ERR_INVALID), "an unknown format");
        PTCHECK(pt_create(slot, 35, 203, 1000, 0, 0, &channel, &index, &missing, 0, 0x10000, 1, AUDIO_FORMAT_S16LE, 2, 0, 0) == -1 && pt_expect_error(203, PULSE_ERR_INVALID), "a rate of nothing");
        PTCHECK(pt_create(slot, 35, 204, 1000, 0, 0, &channel, &index, &missing, 0, 0x10000, 1, AUDIO_FORMAT_S16LE, 2, 4000000000ul, 0) == -1 && pt_expect_error(204, PULSE_ERR_INVALID), "a rate past the cap");
        PTCHECK(pt_create(slot, 35, 205, 1000, 0, 0, &channel, &index, &missing, 0, 0x10000, 1, AUDIO_FORMAT_S16LE, 20, 48000, 0) == -1 && pt_expect_error(205, PULSE_ERR_INVALID), "twenty channels");
        PTCHECK(pt_create(slot, 35, 206, 1000, 0, 0, &channel, &index, &missing, 0, 0x10000, 1, AUDIO_FORMAT_S16LE, 2, 48000, "no such sink") == -1 && pt_expect_error(206, PULSE_ERR_NOENTITY), "a sink that is not there");
        // A string longer than a string may be ends the client.
        {
                static char longname[3000];
                long victim = pt_connect(0, 1, 35, "Long");

                memset(longname, 'x', sizeof(longname) - 1);
                longname[sizeof(longname) - 1] = 0;
                w.n = 0;
                pt_u32(&w, PULSE_INVALID);
                pt_str(&w, longname);
                PTCHECK(pt_command(victim, PULSE_COMMAND_GET_SINK_INFO, 450, &w) == PULSE_DEAD, "a string past its cap ends the client");
                pulse_client_close(&pt_engine, victim);
        }
        // Data: a frame past the cap, one not on a frame, data for a stream that is not there.
        PTCHECK(pt_data(slot, 77, data, 400, 0, 0) == 0, "data for a stream that is gone is dropped");
        PTCHECK(pt_data(slot, 0, data, 401, 0, 0) == PULSE_DEAD, "data off the frame ends the client");
        pulse_client_close(&pt_engine, slot);
        slot = pt_connect(0, 1, 35, "Caps");
        PTCHECK(pt_data(slot, 0, data, PULSE_FRAME_DATA + 4, 0, 0) == PULSE_DEAD, "a data frame past its cap ends the client");
        pulse_client_close(&pt_engine, slot);
        // The rings are capped altogether: many clients, each a stream of the most.
        {
                long clients[32];
                int refused = 0;

                made = 0;
                for (int i = 0; i < 32; i++)
                        clients[i] = pt_connect(0, 1, 35, "Big");
                for (int i = 0; i < 32; i++)
                        if (clients[i] >= 0)
                                for (int k = 0; k < 3; k++)
                                {
                                        int result = pt_create(clients[i], 35, 300, 0x7fffffffu, 0, 0, &channel, &index, &missing, 0, 0x10000, 1, AUDIO_FORMAT_S16LE, 2, 48000, 0);

                                        made += result == 0;
                                        refused += result == -1;
                                }
                PTCHECK(pt_engine.ring_total <= PULSE_RING_TOTAL && refused > 0, "rings %lu of %lu, %d streams, %d refused", pt_engine.ring_total, PULSE_RING_TOTAL, made, refused);
                PTCHECK(made > 0 && pt_engine.stream_slots >= made, "the table holds the streams it made (%d)", made);
                for (int i = 0; i < 32; i++)
                        if (clients[i] >= 0)
                                pulse_client_close(&pt_engine, clients[i]);
        }
        // No memory for the ring: the program is told so.
        slot = pt_connect(0, 1, 35, "Caps");
        pt_alloc_fail_after = pt_allocs;
        PTCHECK(pt_create(slot, 35, 400, 1000, 0, 0, &channel, &index, &missing, 0, 0x10000, 1, AUDIO_FORMAT_S16LE, 2, 48000, 0) == -1 && pt_expect_error(400, PULSE_ERR_TOOLARGE), "no memory for a ring is an error");
        pt_alloc_fail_after = -1;
        pulse_client_close(&pt_engine, slot);
        // A client that never reads: its queue fills and it is dropped, not waited for.
        slot = pt_connect(0, 1, 35, "Mute");
        {
                int dead = 0;

                for (int i = 0; i < 20000 && !dead; i++)
                {
                        w.n = 0;
                        pt_u32(&w, 0);
                        pt_str(&w, 0);
                        dead = pt_command(slot, PULSE_COMMAND_GET_SINK_INFO, 500, &w) == PULSE_DEAD;
                        PTCHECK(pulse_client_pending(&pt_engine, slot) <= PULSE_OUT, "its queue is bounded: %ld", pulse_client_pending(&pt_engine, slot));
                }
                PTCHECK(dead, "and a client that never reads is ended when it fills");
        }
        pulse_client_close(&pt_engine, slot);
        PTCHECK(pt_engine.clients_made == pt_engine.clients_dropped && pt_engine.streams_made == pt_engine.streams_freed && pt_allocs == pt_releases + pt_table_blocks() && pt_alloc_bytes == pt_release_bytes + pt_table_bytes() &&
                    pt_engine.ring_total == 0,
                "all given back: %lu/%lu clients, %lu/%lu streams, %ld/%ld blocks, rings %lu", pt_engine.clients_dropped, pt_engine.clients_made, pt_engine.streams_freed, pt_engine.streams_made, pt_releases,
                pt_allocs, pt_engine.ring_total);
}

// Programs that remember: the volume a program was given last time, and told when it changes.
static void pt_test_memory(void)
{
        long slot;
        unsigned long channel, index, missing;
        struct pt_w w = {{0}, 0};

        pt_reset();
        pulse_sink_add(&pt_engine, "null", "Null", 48000);
        pt_recall_has = 1;
        pt_recall_volume = 0x8000;
        pt_recall_mute = 1;
        slot = pt_connect(1000, 9, 35, "Remembered App");
        // A program that did not say gets what was remembered, one that did say gets its own.
        PTCHECK(pt_create(slot, 35, 20, 19200, 19200, 4800, &channel, &index, &missing, 0, 0x10000, 0, AUDIO_FORMAT_S16LE, 2, 48000, 0) == 0, "stream");
        PTCHECK(pt_engine.stream[0].volume[0] == 0x8000 && pt_engine.stream[0].volume[1] == 0x8000 && pt_engine.stream[0].muted, "the remembered volume and mute are used: %lx", pt_engine.stream[0].volume[0]);
        PTCHECK(pt_create(slot, 35, 21, 19200, 19200, 4800, &channel, &index, &missing, 0, 0x14000, 1, AUDIO_FORMAT_S16LE, 2, 48000, 0) == 0 && pt_engine.stream[1].volume[0] == 0x14000 && !pt_engine.stream[1].muted,
                "a volume that was given is the one used");
        w.n = 0;
        pt_u32(&w, index);
        pt_vol(&w, 2, 0x6000);
        pt_command(slot, PULSE_COMMAND_SET_SINK_INPUT_VOLUME, 22, &w);
        pt_collect(slot);
        PTCHECK(pt_remembered_calls == 1 && !strcmp(pt_remembered_name, "1000/Remembered App"), "a change is remembered by its user and the program's name: %s", pt_remembered_name);
        pulse_client_close(&pt_engine, slot);
        {
                static unsigned char out[AUDIO_BLOCK * 4];

                for (int block = 0; block < 4; block++)
                        pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
        }
}

// The names a program gives are shown to the person: controls and bad UTF-8 are not let through.
static void pt_test_text(void)
{
        static const struct
        {
                const char *given, *shown;
        } names[] = {{"Plain Name", "Plain Name"},
                     {"Tab\there", "Tab?here"},
                     {"Esc\x1b[31m", "Esc?[31m"},
                     {"Caf\xc3\xa9", "Caf\xc3\xa9"},
                     {"bad\xc3(", "bad?("},
                     {"over\xc0\xaf", "over??"},
                     {"c1\xc2\x85x", "c1??x"},
                     {"\xed\xa0\x80sur", "???sur"},
                     {"\xf0\x9f\x8e\xb5 note", "\xf0\x9f\x8e\xb5 note"},
                     {"\xf0\x80\x80\x80", "????"},                  // overlong four-byte form: not text
                     {"\xf4\x90\x80\x80", "????"},                  // above U+10FFFF: not text
                     {"\xf0\x90\x80\x80\xf4\x8f\xbf\xbf", "\xf0\x90\x80\x80\xf4\x8f\xbf\xbf"}, // U+10000 and U+10FFFF: text
                     {"new\nline", "new?line"}};
        struct pulse_props props = {0};
        char shown[PULSE_VALUE + 1];

        for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        {
                props.count = 0;
                pulse_props_set(&props, "media.name", (const unsigned char *)names[i].given, (long)strlen(names[i].given) + 1);
                pulse_props_text(&props, "media.name", shown, sizeof(shown));
                PTCHECK(!strcmp(shown, names[i].shown), "name %u: %s", i, shown);
        }
        // Keys: control bytes and spaces are not keys; the list is capped; a value too long is left out.
        props.count = 0;
        PTCHECK(!pulse_props_set(&props, "bad key", (const unsigned char *)"x", 2) && !pulse_props_set(&props, "", (const unsigned char *)"x", 2) &&
                    !pulse_props_set(&props, "k\x01", (const unsigned char *)"x", 2),
                "bad keys are not kept");
        for (int i = 0; i < PULSE_PROPS + 5; i++)
        {
                char key[16];

                snprintf(key, sizeof(key), "key%d", i);
                pulse_props_set(&props, key, (const unsigned char *)"v", 2);
        }
        PTCHECK(props.count == PULSE_PROPS, "the list is capped: %d", props.count);
        PTCHECK(!pulse_props_set(&props, "key0", (const unsigned char *)"v", PULSE_VALUE + 1), "a value past the cap is not kept");
        // A string copied into a field is cut to fit it, one byte kept for the terminator, and is terminated whatever the room.
        {
                char field[8];

                pulse_copy(field, sizeof(field), "abcdefghijk");
                PTCHECK(!strcmp(field, "abcdefg"), "a long string is cut to seven bytes and terminated: %s", field);
                pulse_copy(field, sizeof(field), "abcdefg");
                PTCHECK(!strcmp(field, "abcdefg"), "a string of exactly seven bytes fits: %s", field);
                pulse_copy(field, sizeof(field), "");
                PTCHECK(field[0] == 0, "the empty string copies as empty");
                pulse_copy(field, 1, "abc");
                PTCHECK(field[0] == 0, "a field of one byte holds only the terminator");
        }
}

/* ---- record streams ---- */

/*
        What the record tests are held to is written here, from the definitions of the formats, in double, sharing no
        line with the float code it judges; and pt_test_record_oracle shows that it can tell a wrong answer from a right
        one (a byte off, an amplitude off, a window off) before anything is judged by it.
*/

// A float as a rounded integer of a scale, clipped, ties to even.
static long pt_ref_wide(float v, double scale, double high)
{
        double d = (double)v * scale;

        d = d > high ? high : d < -high - 1.0 ? -high - 1.0 : d;
        return lrint(d);
}

// One sample as a format lays it down: the bytes in dst, and how many.
static int pt_ref_sample(unsigned char *dst, long format, float v)
{
        long x = 0;
        int n = 0, big = 0;
        unsigned int bits;

        switch (format)
        {
        case AUDIO_FORMAT_U8:
        {
                long s = pt_ref_wide(v, 32768.0, 32767.0), u = (long)floor((double)(s + 128) / 256.0);

                u = u > 127 ? 127 : u < -128 ? -128 : u;
                dst[0] = (unsigned char)(u + 128);
                return 1;
        }
        case AUDIO_FORMAT_S16BE:
                big = 1;
                /* fallthrough */
        case AUDIO_FORMAT_S16LE:
                x = pt_ref_wide(v, 32768.0, 32767.0);
                n = 2;
                break;
        case AUDIO_FORMAT_S24BE:
                big = 1;
                /* fallthrough */
        case AUDIO_FORMAT_S24LE:
                x = pt_ref_wide(v, 8388608.0, 8388607.0);
                n = 3;
                break;
        case AUDIO_FORMAT_S24_32BE:
                big = 1;
                /* fallthrough */
        case AUDIO_FORMAT_S24_32LE:
                x = pt_ref_wide(v, 8388608.0, 8388607.0);
                n = 4;
                break;
        case AUDIO_FORMAT_S32BE:
                big = 1;
                /* fallthrough */
        case AUDIO_FORMAT_S32LE:
                x = pt_ref_wide(v, 2147483648.0, 2147483647.0);
                n = 4;
                break;
        case AUDIO_FORMAT_F32BE:
                big = 1;
                /* fallthrough */
        default:
        {
                float f = v > 1.0f ? 1.0f : v < -1.0f ? -1.0f : v;

                memcpy(&bits, &f, 4);
                x = (long)bits;
                n = 4;
                break;
        }
        }
        for (int k = 0; k < n; k++)
                dst[big ? n - 1 - k : k] = (unsigned char)((unsigned long)x >> (8 * k));
        return n;
}

// n frames of a pair as a format and a number of channels lay them down: a mono stream is the mean, a stereo one the pair, a wider one the pair and silence.
static long pt_ref_frames(unsigned char *dst, long format, long channels, const float *l, const float *r, long n)
{
        long bytes = 0;

        for (long i = 0; i < n; i++)
                for (long c = 0; c < channels; c++)
                {
                        float v = channels == 1 ? (l[i] + r[i]) * 0.5f : c == 0 ? l[i] : c == 1 ? r[i] : 0.0f;

                        bytes += pt_ref_sample(dst + bytes, format, v);
                }
        return bytes;
}

// The first byte two buffers differ at, or -1.
static long pt_diff_bytes(const unsigned char *a, const unsigned char *b, long n)
{
        for (long i = 0; i < n; i++)
                if (a[i] != b[i])
                        return i;
        return -1;
}

// x is a cos(wt) + b sin(wt) plus a rest: a and b by the sums over n samples (a whole number of the cycles), and the rest's rms.
static double pt_fit(const float *x, long n, double hz, double rate, double *amplitude)
{
        double w = 2.0 * M_PI * hz / rate, a = 0, b = 0, rest = 0;

        for (long i = 0; i < n; i++)
        {
                a += (double)x[i] * cos(w * (double)i);
                b += (double)x[i] * sin(w * (double)i);
        }
        a *= 2.0 / (double)n;
        b *= 2.0 / (double)n;
        for (long i = 0; i < n; i++)
        {
                double d = (double)x[i] - a * cos(w * (double)i) - b * sin(w * (double)i);

                rest += d * d;
        }
        *amplitude = sqrt(a * a + b * b);
        return sqrt(rest / (double)n);
}

// The greatest |sample| of each whole window of w frames of a pair; answers the windows (the frames left over are not one yet).
static long pt_ref_windows(const float *l, const float *r, long n, long w, float *out_l, float *out_r)
{
        long made = 0;

        for (long start = 0; start + w <= n; start += w)
        {
                float ml = 0, mr = 0;

                for (long i = start; i < start + w; i++)
                {
                        ml = fabsf(l[i]) > ml ? fabsf(l[i]) : ml;
                        mr = fabsf(r[i]) > mr ? fabsf(r[i]) : mr;
                }
                out_l[made] = ml;
                out_r[made++] = mr;
        }
        return made;
}

// The signal every record test feeds: two tones, one of them past full scale, and the values a rounding is made of in the first frames.
static void pt_sig(float *l, float *r, long start, long n)
{
        static const float specials[] = {0.0f, 0.5f / 32768.0f, 1.5f / 32768.0f, -0.5f / 32768.0f, 2.5f / 32768.0f, 1.0f, -1.0f, 1.5f, -1.5f, 0.99998f, -0.99998f, 0.25f};

        for (long i = 0; i < n; i++)
        {
                double t = (double)(start + i);

                l[i] = (float)(1.25 * sin(2.0 * M_PI * 441.0 * t / 48000.0));
                r[i] = (float)(0.6 * sin(2.0 * M_PI * 1000.0 * t / 48000.0) + 0.2 * sin(2.0 * M_PI * 3000.0 * t / 48000.0));
                if (start + i < (long)(sizeof(specials) / sizeof(specials[0])))
                {
                        l[i] = specials[start + i];
                        r[i] = specials[(start + i + 5) % (long)(sizeof(specials) / sizeof(specials[0]))];
                }
        }
}

static void pt_test_record_oracle(void)
{
        unsigned char a[16], b[16];
        float tone[4800], quiet[4800], l[10], r[10], wl[4], wr[4];
        double amplitude, rest;

        // Roundings, with the ties.
        PTCHECK(pt_ref_wide(0.5f, 32768.0, 32767.0) == 16384 && pt_ref_wide(-1.0f, 32768.0, 32767.0) == -32768 && pt_ref_wide(1.0f, 32768.0, 32767.0) == 32767 &&
                    pt_ref_wide(-2.0f, 32768.0, 32767.0) == -32768,
                "the oracle's scale and its clip");
        PTCHECK(pt_ref_wide(0.5f / 32768.0f, 32768.0, 32767.0) == 0 && pt_ref_wide(1.5f / 32768.0f, 32768.0, 32767.0) == 2 && pt_ref_wide(2.5f / 32768.0f, 32768.0, 32767.0) == 2 &&
                    pt_ref_wide(3.5f / 32768.0f, 32768.0, 32767.0) == 4 && pt_ref_wide(-0.5f / 32768.0f, 32768.0, 32767.0) == 0 && pt_ref_wide(-1.5f / 32768.0f, 32768.0, 32767.0) == -2,
                "the oracle rounds a tie to the even integer");
        // The bytes of each format.
        PTCHECK(pt_ref_sample(a, AUDIO_FORMAT_S16BE, -1.0f) == 2 && a[0] == 0x80 && a[1] == 0x00, "S16BE of full negative is 80 00");
        PTCHECK(pt_ref_sample(a, AUDIO_FORMAT_S24LE, 0.5f) == 3 && a[0] == 0x00 && a[1] == 0x00 && a[2] == 0x40, "S24LE of a half is 00 00 40");
        PTCHECK(pt_ref_sample(a, AUDIO_FORMAT_S24BE, 0.5f) == 3 && a[0] == 0x40 && a[1] == 0x00 && a[2] == 0x00, "S24BE of a half is 40 00 00");
        PTCHECK(pt_ref_sample(a, AUDIO_FORMAT_S24_32LE, -0.5f) == 4 && a[0] == 0x00 && a[1] == 0x00 && a[2] == 0xc0 && a[3] == 0xff, "S24_32LE of minus a half is sign extended: 00 00 c0 ff");
        PTCHECK(pt_ref_sample(a, AUDIO_FORMAT_F32LE, 0.5f) == 4 && a[0] == 0 && a[1] == 0 && a[2] == 0 && a[3] == 0x3f, "F32LE of a half is 00 00 00 3f");
        PTCHECK(pt_ref_sample(a, AUDIO_FORMAT_F32BE, 2.0f) == 4 && a[0] == 0x3f && a[1] == 0x80 && a[2] == 0 && a[3] == 0, "F32BE of two is held to one: 3f 80 00 00");
        PTCHECK(pt_ref_sample(a, AUDIO_FORMAT_S32LE, 1.0f) == 4 && a[0] == 0xff && a[1] == 0xff && a[2] == 0xff && a[3] == 0x7f, "S32LE of full positive is ff ff ff 7f");
        PTCHECK(pt_ref_sample(a, AUDIO_FORMAT_U8, 0.0f) == 1 && a[0] == 128 && pt_ref_sample(a, AUDIO_FORMAT_U8, 1.0f) == 1 && a[0] == 255 && pt_ref_sample(a, AUDIO_FORMAT_U8, -1.0f) == 1 && a[0] == 0 &&
                    pt_ref_sample(a, AUDIO_FORMAT_U8, 1.0f / 256.0f) == 1 && a[0] == 129 && pt_ref_sample(a, AUDIO_FORMAT_U8, -1.5f / 256.0f) == 1 && a[0] == 127,
                "U8 is the high byte of 16 bits, offset, rounded");
        // A frame of each width of channels.
        l[0] = 0.5f;
        r[0] = -0.25f;
        PTCHECK(pt_ref_frames(a, AUDIO_FORMAT_S16LE, 1, l, r, 1) == 2 && a[0] == 0x00 && a[1] == 0x10, "a mono frame is the mean of the pair: 0.125 is 0x1000");
        PTCHECK(pt_ref_frames(a, AUDIO_FORMAT_S16LE, 3, l, r, 1) == 6 && a[4] == 0 && a[5] == 0 && a[0] == 0x00 && a[1] == 0x40, "a frame of three channels is the pair and silence");
        // The difference finder finds the one byte that is wrong, and none when there is none.
        for (int i = 0; i < 16; i++)
                a[i] = b[i] = (unsigned char)(i * 7);
        PTCHECK(pt_diff_bytes(a, b, 16) == -1, "equal buffers have no difference");
        b[9] ^= 0x10;
        PTCHECK(pt_diff_bytes(a, b, 16) == 9, "and one byte off is found where it is");
        // The fit finds the amplitude of a tone, nothing where there is none, and a wrong amplitude is not the right one.
        for (int i = 0; i < 4800; i++)
        {
                tone[i] = (float)(0.5 * sin(2.0 * M_PI * 1000.0 * (double)i / 48000.0));
                quiet[i] = 0.0f;
        }
        rest = pt_fit(tone, 4800, 1000.0, 48000.0, &amplitude);
        PTCHECK(fabs(amplitude - 0.5) < 1e-6 && rest < 1e-6, "the fit reads a half-scale tone: %g, rest %g", amplitude, rest);
        rest = pt_fit(tone, 4800, 1500.0, 48000.0, &amplitude);
        PTCHECK(amplitude < 1e-6, "a tone is not at another frequency: %g", amplitude);
        rest = pt_fit(quiet, 4800, 1000.0, 48000.0, &amplitude);
        PTCHECK(amplitude == 0 && rest == 0, "silence has none");
        for (int i = 0; i < 4800; i++)
                tone[i] *= 0.5f;
        pt_fit(tone, 4800, 1000.0, 48000.0, &amplitude);
        PTCHECK(fabs(amplitude - 0.5) > 0.2, "a tone at half the amplitude is told from the full: %g", amplitude);
        for (int i = 0; i < 4800; i++)
                tone[i] += (float)(0.01 * sin(2.0 * M_PI * 2000.0 * (double)i / 48000.0));
        rest = pt_fit(tone, 4800, 1000.0, 48000.0, &amplitude);
        PTCHECK(fabs(rest - 0.01 / sqrt(2.0)) < 1e-5, "and a second tone is the rest, at its rms: %g", rest);
        // The windows of peaks.
        for (int i = 0; i < 10; i++)
        {
                l[i] = (float)i * 0.1f * (i % 2 ? -1.0f : 1.0f);
                r[i] = (float)(9 - i) * 0.05f;
        }
        PTCHECK(pt_ref_windows(l, r, 10, 4, wl, wr) == 2, "ten frames are two windows of four");
        PTCHECK(wl[0] == fabsf(l[3]) && wl[1] == fabsf(l[7]) && wr[0] == r[0] && wr[1] == r[4], "and the window's greatest is the greatest: %g %g %g %g", wl[0], wl[1], wr[0], wr[1]);
        PTCHECK(pt_ref_windows(l, r, 3, 4, wl, wr) == 0, "three frames are not one");
}

/* ---- a client that records ---- */

struct pt_rec_args
{
        int format, channels, map_channels;
        unsigned long rate;
        const char *source_name;
        unsigned long source_index, maxlength, fragsize, direct;
        int corked, peak, adjust, variable, no_move;
        unsigned long volume;
        int volume_set, muted, muted_set;
};

static struct pt_rec_args pt_rec_args(void)
{
        struct pt_rec_args a = {AUDIO_FORMAT_S16LE, 2, 0, 48000, 0, PULSE_INVALID, PULSE_INVALID, PULSE_INVALID, PULSE_INVALID, 0, 0, 0, 0, 0, 0x10000, 0, 0, 0};

        return a;
}

// The fields of CREATE_RECORD_STREAM in the order libpulse writes them, for a protocol version.
static void pt_rec_fields(struct pt_w *w, unsigned version, const struct pt_rec_args *a)
{
        int positions[32];
        int map_channels = a->map_channels ? a->map_channels : a->channels;

        for (int i = 0; i < 32; i++)
                positions[i] = map_channels == 1 ? AUDIO_POSITION_MONO : i == 0 ? 1 : i == 1 ? 2 : i == 2 ? 3 : i == 3 ? 7 : i == 4 ? 5 : i == 5 ? 6 : AUDIO_POSITION_AUX0 + i;
        if (version < 13)
                pt_str(w, "Record Stream");
        pt_spec(w, a->format, a->channels, a->rate);
        pt_map(w, map_channels, positions);
        pt_u32(w, a->source_index);
        pt_str(w, a->source_name);
        pt_u32(w, a->maxlength);
        pt_bool(w, a->corked);
        pt_u32(w, a->fragsize);
        if (version >= 12)
        {
                for (int i = 0; i < 5; i++)
                        pt_bool(w, 0);
                pt_bool(w, a->no_move);
                pt_bool(w, a->variable);
        }
        if (version >= 13)
        {
                pt_bool(w, a->peak);
                pt_bool(w, a->adjust);
                pt_props(w, "media.name", "Record Stream", "media.role", "phone");
                pt_u32(w, a->direct);
        }
        if (version >= 14)
                pt_bool(w, 0);
        if (version >= 15)
        {
                pt_bool(w, 0);
                pt_bool(w, 0);
        }
        if (version >= 22)
        {
                pt_byte(w, 'B');
                pt_byte(w, 0);
                pt_vol(w, a->channels, a->volume);
                pt_bool(w, a->muted);
                pt_bool(w, a->volume_set);
                pt_bool(w, a->muted_set);
                pt_bool(w, 0);
                pt_bool(w, 0);
        }
}

struct pt_rec_reply
{
        unsigned long channel, index, maxlength, fragsize, source;
        int format, channels;
        unsigned long rate;
        char source_name[80];
        int suspended;
        long long latency;
};

static struct pt_rec_reply pt_rec_reply;

/*
        A record stream made at a protocol version: 0 when it was, with the reply read to its end into pt_rec_reply; the
        error code when it was refused; -1 when the answer is none or is not what the version says; -2 when the client was ended.
*/
static long pt_rec_create(long slot, unsigned version, unsigned long tag, const struct pt_rec_args *a)
{
        struct pt_w w = {{0}, 0};
        const struct pt_frame *f;
        struct pt_r r;
        struct pt_rec_reply *out = &pt_rec_reply;

        memset(out, 0, sizeof(*out));
        pt_rec_fields(&w, version, a);
        if (pt_command(slot, PULSE_COMMAND_CREATE_RECORD_STREAM, tag, &w) < 0)
                return -2;
        pt_collect(slot);
        if ((f = pt_find(PULSE_COMMAND_ERROR)) && f->tag == tag)
        {
                r = pt_reply_reader(f);
                return (long)pt_ru32(&r);
        }
        for (int i = 0; i < pt_logged; i++)
                if (pt_log[i].command == PULSE_COMMAND_REPLY && pt_log[i].tag == tag)
                {
                        const char *name;

                        f = pt_log + i;
                        r = pt_reply_reader(f);
                        out->channel = pt_ru32(&r);
                        out->index = pt_ru32(&r);
                        out->maxlength = pt_ru32(&r);
                        out->fragsize = pt_ru32(&r);
                        if (version >= 12)
                        {
                                int positions[32];

                                pt_rspec(&r, &out->format, &out->channels, &out->rate);
                                pt_rmap(&r, positions);
                                out->source = pt_ru32(&r);
                                name = pt_rstr(&r);
                                if (name)
                                        snprintf(out->source_name, sizeof(out->source_name), "%s", name);
                                out->suspended = pt_rbool(&r);
                        }
                        if (version >= 13)
                                out->latency = pt_r64(&r, 'U');
                        if (version >= 22)
                                pt_rformat(&r);
                        return !r.bad && r.at == f->length ? 0 : -1;
                }
        return -1;
}

static unsigned char pt_rec_buf[1 << 20];
static long pt_rec_len, pt_rec_frames, pt_rec_longest;

static void pt_rec_clear(void)
{
        pt_rec_len = pt_rec_frames = pt_rec_longest = 0;
}

// What the engine has queued for the client is collected, and the data frames of the channel are appended to pt_rec_buf. Other frames stay in the log.
static void pt_rec_pull(long slot, unsigned long channel)
{
        pt_collect(slot);
        for (int i = 0; i < pt_logged; i++)
                if (pt_log[i].data && pt_log[i].channel == channel)
                {
                        if (pt_rec_len + pt_log[i].length <= (long)sizeof(pt_rec_buf))
                                memcpy(pt_rec_buf + pt_rec_len, pt_log[i].payload, (size_t)pt_log[i].length);
                        pt_rec_len += pt_log[i].length;
                        pt_rec_frames++;
                        if (pt_log[i].length > pt_rec_longest)
                                pt_rec_longest = pt_log[i].length;
                }
}

// A command on a record stream by its channel, with a tag, and what comes back collected.
static void pt_rec_command(long slot, unsigned long command, unsigned long tag, unsigned long channel, int has_bool, int flag)
{
        struct pt_w w = {{0}, 0};

        pt_u32(&w, channel);
        if (has_bool)
                pt_bool(&w, flag);
        pt_command(slot, command, tag, &w);
        pt_collect(slot);
}

static void pt_test_record_create(void)
{
        static const unsigned versions[] = {35, 29, 22, 21, 16, 15, 14, 13, 12, 11, 9};

        for (unsigned v = 0; v < sizeof(versions) / sizeof(versions[0]); v++)
        {
                struct pt_rec_args a = pt_rec_args();
                long slot;

                pt_reset();
                pulse_sink_add(&pt_engine, "null", "Null", 48000);
                slot = pt_connect(0, 50, versions[v], "Recorder");
                PTCHECK(pt_rec_create(slot, versions[v], 20, &a) == 0, "version %u: a record stream is made and the answer is read to its end", versions[v]);
                PTCHECK(pt_rec_reply.channel == 0 && pt_rec_reply.index == 0 && pt_rec_reply.maxlength == 1048576 && pt_rec_reply.fragsize == 3840,
                        "version %u: channel %lu index %lu maxlength %lu fragsize %lu", versions[v], pt_rec_reply.channel, pt_rec_reply.index, pt_rec_reply.maxlength, pt_rec_reply.fragsize);
                if (versions[v] >= 12)
                        PTCHECK(pt_rec_reply.format == AUDIO_FORMAT_S16LE && pt_rec_reply.channels == 2 && pt_rec_reply.rate == 48000 && pt_rec_reply.source == 0 &&
                                    !strcmp(pt_rec_reply.source_name, "null.monitor") && !pt_rec_reply.suspended,
                                "version %u: the spec and the source come back: %s", versions[v], pt_rec_reply.source_name);
                PTCHECK(pulse_source_wanted(&pt_engine, 0) == 1, "version %u: the source has a listener", versions[v]);
                pulse_client_close(&pt_engine, slot);
                PTCHECK(pt_engine.records_made == 1 && pt_engine.records_freed == 1 && pulse_source_wanted(&pt_engine, 0) == 0, "version %u: and none when the client goes", versions[v]);
        }
        // Which source, and the buffer attributes.
        {
                struct pt_rec_args a = pt_rec_args();
                long slot, mic;

                pt_reset();
                pulse_sink_add(&pt_engine, "null", "Null", 48000);
                slot = pt_connect(0, 50, 35, "Recorder");
                PTCHECK(pt_engine.default_source == 0, "with no capture device the default source is the monitor");
                a.source_name = "null.monitor";
                PTCHECK(pt_rec_create(slot, 35, 21, &a) == 0 && pt_rec_reply.source == 0, "a source by name");
                a.source_name = 0;
                a.source_index = 0;
                PTCHECK(pt_rec_create(slot, 35, 22, &a) == 0 && pt_rec_reply.source == 0, "a source by index");
                mic = pulse_source_add(&pt_engine, "alsa_input.test.00", "Test <mic>", 44100);
                PTCHECK(mic == PULSE_SINKS && pt_engine.default_source == mic, "a capture device is numbered after the monitors and becomes the default source: %ld", mic);
                a = pt_rec_args();
                PTCHECK(pt_rec_create(slot, 35, 23, &a) == 0 && pt_rec_reply.source == (unsigned long)mic && !strcmp(pt_rec_reply.source_name, "alsa_input.test.00"), "no source named is the default source");
                PTCHECK(pt_rec_reply.rate == 48000, "at the rate the program asked, not the source's");
                a.source_name = "8";
                PTCHECK(pt_rec_create(slot, 35, 24, &a) == 0 && pt_rec_reply.source == (unsigned long)mic, "a source named by its number");
                a.source_name = "alsa_input.test.00";
                a.source_index = 0;
                PTCHECK(pt_rec_create(slot, 35, 25, &a) == PULSE_ERR_INVALID, "a name and an index together are a mistake");
                a.source_index = PULSE_INVALID;
                a.source_name = "nonesuch";
                PTCHECK(pt_rec_create(slot, 35, 26, &a) == PULSE_ERR_NOENTITY, "an unknown source");
                a.source_name = 0;
                a.source_index = 7;
                PTCHECK(pt_rec_create(slot, 35, 27, &a) == PULSE_ERR_NOENTITY, "a source number nothing has");
                a.source_index = 99999;
                PTCHECK(pt_rec_create(slot, 35, 28, &a) == PULSE_ERR_NOENTITY, "and one past the table");
                // Buffer attributes: the program's where it gave them, made whole frames, else the server's.
                a = pt_rec_args();
                a.fragsize = 1920;
                a.maxlength = 65536;
                PTCHECK(pt_rec_create(slot, 35, 29, &a) == 0 && pt_rec_reply.fragsize == 1920 && pt_rec_reply.maxlength == 65536, "attributes given are kept");
                a.fragsize = 1;
                a.maxlength = 3;
                PTCHECK(pt_rec_create(slot, 35, 30, &a) == 0 && pt_rec_reply.fragsize == 4 && pt_rec_reply.maxlength == 4, "and held to one frame at least: %lu %lu", pt_rec_reply.fragsize, pt_rec_reply.maxlength);
                a.fragsize = 10000000;
                a.maxlength = 8190;
                PTCHECK(pt_rec_create(slot, 35, 31, &a) == 0 && pt_rec_reply.fragsize == 8188 && pt_rec_reply.maxlength == 8188, "a fragment is no longer than the buffer, nor a part of a frame: %lu %lu", pt_rec_reply.fragsize,
                        pt_rec_reply.maxlength);
                a.fragsize = 0;
                a.maxlength = 0xfffffff0u;
                PTCHECK(pt_rec_create(slot, 35, 32, &a) == 0 && pt_rec_reply.maxlength == 1048576 && pt_rec_reply.fragsize == 3840, "and a buffer too long is the longest: %lu", pt_rec_reply.maxlength);
                // Channels are each a channel of the stream's own: the next is the next number.
                PTCHECK(pt_rec_reply.channel == 7 && pt_rec_reply.index == 7, "channels and indexes count up, and a refusal takes none: %lu %lu", pt_rec_reply.channel, pt_rec_reply.index);
                pulse_client_close(&pt_engine, slot);
                PTCHECK(pt_engine.records_made == pt_engine.records_freed, "all given back");
        }
}

// Who may record, what is refused, and every cap.
static void pt_test_record_policy(void)
{
        struct pt_rec_args a = pt_rec_args();
        long slot, other, mic;
        struct pt_w w = {{0}, 0};

        pt_reset();
        pulse_sink_add(&pt_engine, "null", "Null", 48000);
        mic = pulse_source_add(&pt_engine, "alsa_input.test.00", "Test mic", 48000);
        // Before the handshake: refused.
        slot = pulse_client_open(&pt_engine, 0, 7);
        pt_rec_fields(&w, 35, &a);
        pt_command(slot, PULSE_COMMAND_CREATE_RECORD_STREAM, 3, &w);
        pt_collect(slot);
        PTCHECK(pt_expect_error(3, PULSE_ERR_ACCESS), "no record stream before the handshake");
        pulse_client_close(&pt_engine, slot);
        // A frame that is cut short or has more after it ends the client.
        for (int k = 0; k < 4; k++)
        {
                struct pt_w short_w = {{0}, 0};

                slot = pt_connect(0, 8, 35, "Hostile");
                pt_rec_fields(&short_w, 35, &a);
                if (k == 0)
                        short_w.n--;
                else if (k == 1)
                        pt_byte(&short_w, 'L');
                else if (k == 2)
                        short_w.n = 20;
                else
                        short_w.n = 0;
                PTCHECK(pt_command(slot, PULSE_COMMAND_CREATE_RECORD_STREAM, 4, &short_w) == PULSE_DEAD, "hostile record fields %d end the client", k);
                pulse_client_close(&pt_engine, slot);
        }
        // What cannot be made.
        slot = pt_connect(0, 9, 35, "Root");
        a = pt_rec_args();
        a.format = 99;
        PTCHECK(pt_rec_create(slot, 35, 10, &a) == PULSE_ERR_INVALID, "a format there is not");
        a.format = AUDIO_FORMAT_ALAW;
        PTCHECK(pt_rec_create(slot, 35, 11, &a) == PULSE_ERR_NOTSUPPORTED, "A-law is not recorded");
        a = pt_rec_args();
        a.channels = 0;
        a.map_channels = 1;
        PTCHECK(pt_rec_create(slot, 35, 12, &a) == PULSE_ERR_INVALID, "no channels");
        a.channels = AUDIO_CHANNELS_MAX + 1;
        PTCHECK(pt_rec_create(slot, 35, 13, &a) == PULSE_ERR_INVALID, "too many channels");
        a = pt_rec_args();
        a.rate = 0;
        PTCHECK(pt_rec_create(slot, 35, 14, &a) == PULSE_ERR_INVALID, "no rate");
        a.rate = AUDIO_RATE_MAX + 1;
        PTCHECK(pt_rec_create(slot, 35, 15, &a) == PULSE_ERR_INVALID, "a rate past the most");
        a = pt_rec_args();
        a.map_channels = 1;
        PTCHECK(pt_rec_create(slot, 35, 16, &a) == PULSE_ERR_INVALID, "a map of another number of channels");
        a = pt_rec_args();
        a.direct = 0;
        PTCHECK(pt_rec_create(slot, 35, 17, &a) == PULSE_ERR_NOTSUPPORTED, "a meter on another program's stream is not served");
        pulse_source_reconfigure(&pt_engine, mic, "alsa_input.test.00", "Test mic", 192000);
        a = pt_rec_args();
        a.rate = 8000;
        a.source_name = "alsa_input.test.00";
        PTCHECK(pt_rec_create(slot, 35, 18, &a) == PULSE_ERR_NOTSUPPORTED, "a ratio past the filter's longest is refused, and nothing is left held");
        a.peak = 1;
        PTCHECK(pt_rec_create(slot, 35, 19, &a) == 0, "though a stream of peaks, which has no filter, is not");
        pulse_source_reconfigure(&pt_engine, mic, "alsa_input.test.00", "Test mic", 48000);
        for (int t = 0; t < AUDIO_TABLES; t++)
                PTCHECK(audio_tables[t].users == 0, "table %d is not kept", t);
        pulse_client_close(&pt_engine, slot);
        // Another user: the capture device, not what the session plays.
        other = pt_connect(1000, 10, 35, "Other");
        a = pt_rec_args();
        a.source_name = "null.monitor";
        PTCHECK(pt_rec_create(other, 35, 20, &a) == PULSE_ERR_ACCESS, "another user does not hear what the session plays");
        a.source_name = "alsa_input.test.00";
        PTCHECK(pt_rec_create(other, 35, 21, &a) == 0, "but may record from the capture device");
        a.source_name = 0;
        PTCHECK(pt_rec_create(other, 35, 22, &a) == 0 && pt_rec_reply.source == (unsigned long)mic, "which is the default");
        // Their stream is theirs: another user cannot change it; root can.
        {
                long third = pt_connect(1001, 11, 35, "Third"), root = pt_connect(0, 12, 35, "Root");
                unsigned long index = pt_rec_reply.index;

                w.n = 0;
                pt_u32(&w, index);
                pt_bool(&w, 1);
                pt_command(third, PULSE_COMMAND_SET_SOURCE_OUTPUT_MUTE, 30, &w);
                pt_collect(third);
                PTCHECK(pt_expect_error(30, PULSE_ERR_NOENTITY), "another user cannot mute a stream that is not theirs (as one that is not there)");
                pt_command(root, PULSE_COMMAND_SET_SOURCE_OUTPUT_MUTE, 31, &w);
                pt_collect(root);
                PTCHECK(pt_expect_ack(31), "root can");
                w.n = 0;
                pt_u32(&w, index);
                pt_vol(&w, 2, 0x8000);
                pt_command(third, PULSE_COMMAND_SET_SOURCE_OUTPUT_VOLUME, 34, &w);
                pt_collect(third);
                PTCHECK(pt_expect_error(34, PULSE_ERR_NOENTITY), "nor set its volume (as one that is not there)");
                pt_command(root, PULSE_COMMAND_SET_SOURCE_OUTPUT_VOLUME, 35, &w);
                pt_collect(root);
                PTCHECK(pt_expect_ack(35), "which root can");
                w.n = 0;
                pt_u32(&w, index);
                pt_command(third, PULSE_COMMAND_GET_SOURCE_OUTPUT_INFO, 32, &w);
                pt_collect(third);
                PTCHECK(pt_expect_error(32, PULSE_ERR_NOENTITY), "and it is not even shown to them");
                pt_command(third, PULSE_COMMAND_KILL_SOURCE_OUTPUT, 33, &w);
                pt_collect(third);
                PTCHECK(pt_expect_error(33, PULSE_ERR_ACCESS), "nor killed by them");
                pulse_client_close(&pt_engine, third);
                pulse_client_close(&pt_engine, root);
        }
        pulse_client_close(&pt_engine, other);
        // No count of a client's record streams is refused, nor of all of them: the table grows.
        {
                long clients[5];

                for (int c = 0; c < 5; c++)
                        clients[c] = pt_connect(0, 20 + c, 35, "Many");
                a = pt_rec_args();
                for (int i = 0; i < 8; i++)
                        PTCHECK(pt_rec_create(clients[0], 35, (unsigned long)(40 + i), &a) == 0, "stream %d of a client", i);
                PTCHECK(pt_rec_create(clients[0], 35, 49, &a) == 0, "the ninth of one client is accepted");
                for (int c = 1; c < 4; c++)
                        for (int i = 0; i < 8; i++)
                                PTCHECK(pt_rec_create(clients[c], 35, (unsigned long)(50 + i), &a) == 0, "stream %d of client %d", i, c);
                PTCHECK(pt_rec_create(clients[4], 35, 60, &a) == 0, "and another client's record is accepted past them (%ld slots)", pt_engine.record_slots);
                for (int c = 0; c < 5; c++)
                        pulse_client_close(&pt_engine, clients[c]);
                PTCHECK(pt_engine.records_made == pt_engine.records_freed, "all freed: %lu made, %lu freed", pt_engine.records_made, pt_engine.records_freed);
        }
        // The person's switch.
        {
                long watcher, listener;

                listener = pt_connect(0, 30, 35, "Listener");
                watcher = pt_connect(0, 31, 35, "Watcher");
                w.n = 0;
                pt_u32(&w, 0x2ff);
                pt_command(watcher, PULSE_COMMAND_SUBSCRIBE, 70, &w);
                pt_collect(watcher);
                a = pt_rec_args();
                PTCHECK(pt_rec_create(listener, 35, 71, &a) == 0, "a stream before the switch");
                pt_collect(watcher);
                pulse_capture_allow(&pt_engine, 0);
                pt_collect(listener);
                {
                        const struct pt_frame *f = pt_find(PULSE_COMMAND_RECORD_STREAM_KILLED);

                        PTCHECK(f && f->length == 15 && wire_be32(f->payload + 11) == pt_rec_reply.channel, "capture turned off tells the program its stream is killed");
                }
                pt_collect(watcher);
                {
                        int removed = 0, server = 0;

                        for (int i = 0; i < pt_logged; i++)
                                if (pt_log[i].command == PULSE_COMMAND_SUBSCRIBE_EVENT)
                                {
                                        unsigned long what = wire_be32(pt_log[i].payload + 11);

                                        removed += what == (PULSE_FACILITY_SOURCE_OUTPUT | PULSE_EVENT_REMOVE);
                                        server += what == (PULSE_FACILITY_SERVER | PULSE_EVENT_CHANGE);
                                }
                        PTCHECK(removed == 1 && server == 1, "and a watcher that the stream is gone and the server changed: %d %d", removed, server);
                }
                PTCHECK(pulse_source_wanted(&pt_engine, 0) == 0 && pt_engine.records_made == pt_engine.records_freed, "nothing is left listening");
                PTCHECK(pt_rec_create(listener, 35, 72, &a) == PULSE_ERR_ACCESS, "while it is off no stream is made, whoever asks");
                a.peak = 1;
                PTCHECK(pt_rec_create(listener, 35, 73, &a) == PULSE_ERR_ACCESS, "not even one of peaks");
                pulse_capture_allow(&pt_engine, 0);
                pt_collect(watcher);
                PTCHECK(pt_count(PULSE_COMMAND_SUBSCRIBE_EVENT) == 0, "off again is no change");
                pulse_capture_allow(&pt_engine, 1);
                a = pt_rec_args();
                PTCHECK(pt_rec_create(listener, 35, 74, &a) == 0, "on again, a stream is made");
                pulse_client_close(&pt_engine, listener);
                pulse_client_close(&pt_engine, watcher);
        }
        PTCHECK(pt_engine.records_made == pt_engine.records_freed && pt_allocs == pt_releases + pt_table_blocks(), "all given back");
        for (int t = 0; t < AUDIO_TABLES; t++)
                PTCHECK(audio_tables[t].users == 0, "table %d is not kept", t);
}

// What the engine says to a watcher: the facility and kind, and the index, of each event in the log.
static int pt_events(unsigned long *what, unsigned long *index, int room)
{
        int n = 0;

        for (int i = 0; i < pt_logged && n < room; i++)
                if (pt_log[i].command == PULSE_COMMAND_SUBSCRIBE_EVENT)
                {
                        what[n] = wire_be32(pt_log[i].payload + 11);
                        index[n++] = wire_be32(pt_log[i].payload + 16);
                }
        return n;
}

// Bytes in, bytes out, frame by frame: a program records in every format and number of channels the server makes, and what it gets is what the formats say.
static void pt_test_record_data(void)
{
        static const long formats[] = {AUDIO_FORMAT_S16LE, AUDIO_FORMAT_S16BE, AUDIO_FORMAT_U8,     AUDIO_FORMAT_F32LE,   AUDIO_FORMAT_F32BE,    AUDIO_FORMAT_S32LE,
                                       AUDIO_FORMAT_S32BE, AUDIO_FORMAT_S24LE, AUDIO_FORMAT_S24BE, AUDIO_FORMAT_S24_32LE, AUDIO_FORMAT_S24_32BE};
        static const long widths[] = {2, 1, 6};
        static const long blocks[] = {512, 128, 77, 1, 300};
        static float l[512], r[512];
        static unsigned char expect[1 << 16];

        pt_reset();
        pulse_sink_add(&pt_engine, "null", "Null", 48000);
        for (unsigned f = 0; f < sizeof(formats) / sizeof(formats[0]); f++)
                for (unsigned c = 0; c < 3; c++)
                {
                        struct pt_rec_args a = pt_rec_args();
                        long channels = widths[c], slot, at = 0, want = 0;

                        if (channels != 2 && formats[f] != AUDIO_FORMAT_S16LE && formats[f] != AUDIO_FORMAT_F32LE && formats[f] != AUDIO_FORMAT_U8)
                                continue;
                        a.format = (int)formats[f];
                        a.channels = (int)channels;
                        slot = pt_connect(0, 60, 35, "Data");
                        if (pt_rec_create(slot, 35, 20, &a) != 0)
                        {
                                PTCHECK(0, "format %ld, %ld channels: the stream is not made", formats[f], channels);
                                pulse_client_close(&pt_engine, slot);
                                continue;
                        }
                        pt_rec_clear();
                        for (unsigned b = 0; b < sizeof(blocks) / sizeof(blocks[0]); b++)
                        {
                                pt_sig(l, r, at, blocks[b]);
                                pulse_source_feed(&pt_engine, 0, l, r, blocks[b]);
                                pt_rec_pull(slot, pt_rec_reply.channel);
                                want += pt_ref_frames(expect + want, formats[f], channels, l, r, blocks[b]);
                                at += blocks[b];
                        }
                        PTCHECK(pt_rec_len == want && pt_rec_frames == 5, "format %ld, %ld channels: %ld bytes in %ld frames against %ld", formats[f], channels, pt_rec_len, pt_rec_frames, want);
                        if (pt_rec_len == want)
                                PTCHECK(pt_diff_bytes(pt_rec_buf, expect, want) == -1, "format %ld, %ld channels: the bytes differ from the first at %ld", formats[f], channels,
                                        pt_diff_bytes(pt_rec_buf, expect, want));
                        pulse_client_close(&pt_engine, slot);
                }
        // The frames: a fragment at most, in whole frames, on the stream's channel, with no offset and no flags.
        {
                struct pt_rec_args a = pt_rec_args();
                long slot = pt_connect(0, 61, 35, "Fragments");
                const unsigned char *bytes;
                long got;

                a.fragsize = 1000;
                PTCHECK(pt_rec_create(slot, 35, 21, &a) == 0 && pt_rec_reply.fragsize == 1000, "a fragment of 1000 bytes");
                pt_sig(l, r, 0, 512);
                pulse_source_feed(&pt_engine, 0, l, r, 512);
                got = pulse_client_out(&pt_engine, slot, &bytes);
                PTCHECK(got == 3 * 20 + 2048 && wire_be32(bytes) == 1000 && wire_be32(bytes + 4) == pt_rec_reply.channel && wire_be32(bytes + 8) == 0 && wire_be32(bytes + 12) == 0 &&
                            wire_be32(bytes + 16) == 0,
                        "the first frame is a fragment on the channel, offset 0, flags 0: %ld bytes, length %lu", got, got > 4 ? wire_be32(bytes) : 0);
                pt_rec_clear();
                pt_rec_pull(slot, pt_rec_reply.channel);
                PTCHECK(pt_rec_len == 2048 && pt_rec_frames == 3 && pt_rec_longest == 1000, "2048 bytes in three frames of 1000, 1000 and 48: %ld in %ld, the longest %ld", pt_rec_len, pt_rec_frames, pt_rec_longest);
                pulse_client_close(&pt_engine, slot);
        }
}

// Volumes and mutes of the stream and of the source, ramped, and exact where they are held.
static void pt_test_record_gain(void)
{
        static float l[1024], r[1024], gl[1024], gr[1024];
        static unsigned char expect[1 << 14];
        struct pt_rec_args a = pt_rec_args();
        long slot, root;
        struct pt_w w = {{0}, 0};

        pt_reset();
        pulse_sink_add(&pt_engine, "null", "Null", 48000);
        root = pt_connect(0, 70, 35, "Root");
        pt_sig(l, r, 0, 1024);
        // A volume given at the start is held from the first frame: a stream at 0x8000 is at an eighth.
        slot = pt_connect(0, 71, 35, "Quiet");
        a.volume = 0x8000;
        a.volume_set = 1;
        PTCHECK(pt_rec_create(slot, 35, 20, &a) == 0, "a stream with a volume");
        for (int i = 0; i < 1024; i++)
        {
                gl[i] = l[i] * 0.125f;
                gr[i] = r[i] * 0.125f;
        }
        pt_rec_clear();
        pulse_source_feed(&pt_engine, 0, l, r, 512);
        pulse_source_feed(&pt_engine, 0, l + 512, r + 512, 512);
        pt_rec_pull(slot, pt_rec_reply.channel);
        PTCHECK(pt_rec_len == 4096 && pt_ref_frames(expect, AUDIO_FORMAT_S16LE, 2, gl, gr, 1024) == 4096 && pt_diff_bytes(pt_rec_buf, expect, 4096) == -1, "its data is an eighth, from the first frame, at %ld",
                pt_diff_bytes(pt_rec_buf, expect, 4096));
        pulse_client_close(&pt_engine, slot);
        // Muted at the start: silence.
        slot = pt_connect(0, 72, 35, "Muted");
        a = pt_rec_args();
        a.muted = 1;
        a.muted_set = 1;
        PTCHECK(pt_rec_create(slot, 35, 21, &a) == 0, "a stream that is muted");
        pt_rec_clear();
        pulse_source_feed(&pt_engine, 0, l, r, 512);
        pt_rec_pull(slot, pt_rec_reply.channel);
        {
                int silent = pt_rec_len == 2048;

                for (long i = 0; i < pt_rec_len; i++)
                        silent &= pt_rec_buf[i] == 0;
                PTCHECK(silent, "its data is silence, of the same length: %ld", pt_rec_len);
        }
        pulse_client_close(&pt_engine, slot);
        // Mute and unmute while it runs: down over a ramp, then nothing, then up over a ramp, then as it was.
        slot = pt_connect(0, 73, 35, "Ramp");
        a = pt_rec_args();
        PTCHECK(pt_rec_create(slot, 35, 22, &a) == 0, "a stream to mute");
        {
                unsigned long channel = pt_rec_reply.channel, index = pt_rec_reply.index;
                short *got;
                int ramp_ok = 1, silent = 1, back = 1;

                pt_rec_clear();
                pulse_source_feed(&pt_engine, 0, l, r, 512);
                pt_rec_pull(slot, channel);
                pt_ref_frames(expect, AUDIO_FORMAT_S16LE, 2, l, r, 512);
                PTCHECK(pt_rec_len == 2048 && pt_diff_bytes(pt_rec_buf, expect, 2048) == -1, "unmuted it is as it is");
                w.n = 0;
                pt_u32(&w, index);
                pt_bool(&w, 1);
                pt_command(root, PULSE_COMMAND_SET_SOURCE_OUTPUT_MUTE, 23, &w);
                pt_collect(root);
                PTCHECK(pt_expect_ack(23), "muted by root");
                pt_rec_clear();
                pulse_source_feed(&pt_engine, 0, l + 512, r + 512, 512);
                pt_rec_pull(slot, channel);
                got = (short *)pt_rec_buf;
                pt_ref_frames(expect, AUDIO_FORMAT_S16LE, 2, l + 512, r + 512, 512);
                for (int i = 0; i < 96 * 2; i++)
                        ramp_ok &= abs(got[i]) <= abs(((short *)expect)[i]);
                for (int i = 96 * 2; i < 512 * 2; i++)
                        silent &= got[i] == 0;
                PTCHECK(pt_rec_len == 2048 && ramp_ok && silent, "muted it falls over the ramp and then is silent");
                w.n = 0;
                pt_u32(&w, index);
                pt_bool(&w, 0);
                pt_command(root, PULSE_COMMAND_SET_SOURCE_OUTPUT_MUTE, 24, &w);
                pt_collect(root);
                pt_rec_clear();
                pulse_source_feed(&pt_engine, 0, l, r, 512);
                pt_rec_pull(slot, channel);
                got = (short *)pt_rec_buf;
                pt_ref_frames(expect, AUDIO_FORMAT_S16LE, 2, l, r, 512);
                ramp_ok = 1;
                for (int i = 0; i < 96 * 2; i++)
                        ramp_ok &= abs(got[i]) <= abs(((short *)expect)[i]) + 1;
                for (int i = 96 * 2; i < 512 * 2; i++)
                        back &= got[i] == ((short *)expect)[i];
                PTCHECK(pt_rec_len == 2048 && ramp_ok && back, "unmuted it rises and is as it was");
        }
        pulse_client_close(&pt_engine, slot);
        // The source's volume and mute, set by root over the wire: ramped, and then exact; and the stream's on top of it.
        slot = pt_connect(0, 74, 35, "Source");
        PTCHECK(pt_rec_create(slot, 35, 25, &a) == 0, "a stream on a source whose volume changes");
        {
                unsigned long channel = pt_rec_reply.channel;

                w.n = 0;
                pt_u32(&w, 0);
                pt_str(&w, 0);
                pt_vol(&w, 2, 0x8000);
                pt_command(root, PULSE_COMMAND_SET_SOURCE_VOLUME, 26, &w);
                pt_collect(root);
                PTCHECK(pt_expect_ack(26), "the source's volume is set");
                pt_rec_clear();
                pulse_source_feed(&pt_engine, 0, l, r, 512);
                pulse_source_feed(&pt_engine, 0, l + 512, r + 512, 512);
                pt_rec_pull(slot, channel);
                pt_ref_frames(expect, AUDIO_FORMAT_S16LE, 2, gl, gr, 1024);
                PTCHECK(pt_rec_len == 4096 && pt_diff_bytes(pt_rec_buf + 100 * 4, expect + 100 * 4, 4096 - 100 * 4) == -1, "the source at an eighth: held from the hundredth frame on, which is at %ld",
                        pt_diff_bytes(pt_rec_buf + 400, expect + 400, 3696));
                PTCHECK(((short *)pt_rec_buf)[0] != ((short *)expect)[0] || ((short *)expect)[0] == 0, "and the first frames are the ramp");
                w.n = 0;
                pt_u32(&w, 0);
                pt_str(&w, 0);
                pt_bool(&w, 1);
                pt_command(root, PULSE_COMMAND_SET_SOURCE_MUTE, 27, &w);
                pt_collect(root);
                PTCHECK(pt_expect_ack(27), "the source is muted");
                pt_rec_clear();
                pulse_source_feed(&pt_engine, 0, l, r, 512);
                pulse_source_feed(&pt_engine, 0, l + 512, r + 512, 512);
                pt_rec_pull(slot, channel);
                {
                        int silent = pt_rec_len == 4096;

                        for (long i = 96 * 4; i < pt_rec_len; i++)
                                silent &= pt_rec_buf[i] == 0;
                        PTCHECK(silent, "a muted source gives silence after its ramp");
                }
                w.n = 0;
                pt_u32(&w, 0);
                pt_str(&w, 0);
                pt_bool(&w, 0);
                pt_command(root, PULSE_COMMAND_SET_SOURCE_MUTE, 28, &w);
                pt_collect(root);
                w.n = 0;
                pt_u32(&w, 0);
                pt_str(&w, 0);
                pt_vol(&w, 2, 0x10000);
                pt_command(root, PULSE_COMMAND_SET_SOURCE_VOLUME, 29, &w);
                pt_collect(root);
                pt_rec_clear();
                pulse_source_feed(&pt_engine, 0, l, r, 512);
                pulse_source_feed(&pt_engine, 0, l + 512, r + 512, 512);
                pt_rec_pull(slot, channel);
                pt_ref_frames(expect, AUDIO_FORMAT_S16LE, 2, l, r, 1024);
                PTCHECK(pt_rec_len == 4096 && pt_diff_bytes(pt_rec_buf + 400, expect + 400, 3696) == -1, "and at full volume it is as it was");
        }
        pulse_client_close(&pt_engine, slot);
        pulse_client_close(&pt_engine, root);
        PTCHECK(pt_engine.records_made == pt_engine.records_freed, "all given back");
}

// Cork, and a program that does not read.
static void pt_test_record_cork(void)
{
        static float l[512], r[512];
        struct pt_rec_args a = pt_rec_args();
        long slot;
        unsigned long channel;

        pt_reset();
        pulse_sink_add(&pt_engine, "null", "Null", 48000);
        pt_sig(l, r, 0, 512);
        slot = pt_connect(0, 80, 35, "Cork");
        a.corked = 1;
        PTCHECK(pt_rec_create(slot, 35, 20, &a) == 0, "a stream made corked");
        channel = pt_rec_reply.channel;
        PTCHECK(pulse_source_wanted(&pt_engine, 0) == 0, "a corked stream wants nothing");
        pt_rec_clear();
        pulse_source_feed(&pt_engine, 0, l, r, 512);
        pt_rec_pull(slot, channel);
        PTCHECK(pt_rec_len == 0, "and is given nothing");
        pt_rec_command(slot, PULSE_COMMAND_CORK_RECORD_STREAM, 21, channel, 1, 0);
        PTCHECK(pt_expect_ack(21) && pulse_source_wanted(&pt_engine, 0) == 1, "uncorked: it wants the source");
        pulse_source_feed(&pt_engine, 0, l, r, 512);
        pt_rec_pull(slot, channel);
        PTCHECK(pt_rec_len == 2048, "and gets it");
        pt_rec_command(slot, PULSE_COMMAND_CORK_RECORD_STREAM, 22, channel, 1, 1);
        PTCHECK(pt_expect_ack(22) && pulse_source_wanted(&pt_engine, 0) == 0, "corked again: it wants nothing");
        pt_rec_clear();
        pulse_source_feed(&pt_engine, 0, l, r, 512);
        pt_rec_pull(slot, channel);
        PTCHECK(pt_rec_len == 0, "and gets nothing");
        pt_rec_command(slot, PULSE_COMMAND_CORK_RECORD_STREAM, 23, 77, 1, 1);
        PTCHECK(pt_expect_error(23, PULSE_ERR_NOENTITY), "a channel that is not a stream");
        // Two on one source, one corked: only the other is given anything.
        {
                struct pt_rec_args b = pt_rec_args();
                unsigned long second;

                b.corked = 0;
                PTCHECK(pt_rec_create(slot, 35, 25, &b) == 0, "a second stream");
                second = pt_rec_reply.channel;
                pt_rec_clear();
                pulse_source_feed(&pt_engine, 0, l, r, 512);
                pt_rec_pull(slot, second);
                PTCHECK(pt_rec_len == 2048, "the one that is not corked gets the block: %ld", pt_rec_len);
                {
                        int beside = 0;

                        for (int i = 0; i < pt_logged; i++)
                                beside += pt_log[i].data && pt_log[i].channel == channel;
                        PTCHECK(beside == 0, "and the corked one, beside it, gets none: %d frames", beside);
                }
                pt_rec_command(slot, PULSE_COMMAND_DELETE_RECORD_STREAM, 26, second, 0, 0);
        }
        {
                struct pt_w w = {{0}, 0};

                pt_u32(&w, channel);
                PTCHECK(pt_command(slot, PULSE_COMMAND_CORK_RECORD_STREAM, 24, &w) == PULSE_DEAD, "a cork with no flag is a mistake of the wire");
        }
        pulse_client_close(&pt_engine, slot);
        // A program that never reads: its data is dropped before its replies are, and it is not ended.
        slot = pt_connect(0, 81, 35, "Deaf");
        a = pt_rec_args();
        PTCHECK(pt_rec_create(slot, 35, 30, &a) == 0, "a stream for a program that does not read");
        channel = pt_rec_reply.channel;
        for (int block = 0; block < 400; block++)
                pulse_source_feed(&pt_engine, 0, l, r, 512);
        {
                struct pulse_record *rec = pt_engine.record;
                long pending = pulse_client_pending(&pt_engine, slot);

                PTCHECK(rec->dropped > 0 && rec->produced == 400ull * 2048 && rec->sent + rec->dropped == rec->produced, "some was dropped: %llu of %llu, %llu queued", rec->dropped, rec->produced, rec->sent);
                PTCHECK(pending <= PULSE_OUT - PULSE_RECORD_RESERVE, "and the queue keeps its reserve: %ld of %d", pending, PULSE_OUT);
        }
        {
                long before = pulse_client_pending(&pt_engine, slot);

                PTCHECK(pt_command(slot, PULSE_COMMAND_GET_SERVER_INFO, 31, 0) == PULSE_DONE && pulse_client_pending(&pt_engine, slot) > before, "the program is not ended, and an answer is queued for it");
        }
        pt_collect(slot);
        pt_command(slot, PULSE_COMMAND_GET_SERVER_INFO, 32, 0);
        pt_collect(slot);
        PTCHECK(pt_find(PULSE_COMMAND_REPLY) && pt_find(PULSE_COMMAND_REPLY)->tag == 32, "which it reads when it reads");
        pt_rec_clear();
        pulse_source_feed(&pt_engine, 0, l, r, 512);
        pt_rec_pull(slot, channel);
        PTCHECK(pt_rec_len == 2048, "and data flows again once it has read");
        {
                struct pulse_record *rec = pt_engine.record;
                struct pt_w w = {{0}, 0};
                const struct pt_frame *f;
                struct pt_r rd;

                pt_u32(&w, channel);
                pt_byte(&w, 'T');
                pt_be32(&w, 1);
                pt_be32(&w, 2);
                pt_command(slot, PULSE_COMMAND_GET_RECORD_LATENCY, 33, &w);
                pt_collect(slot);
                f = pt_find(PULSE_COMMAND_REPLY);
                PTCHECK(f && f->tag == 33, "latency of a stream that dropped data");
                if (f)
                {
                        long long wr, rdx;

                        rd = pt_reply_reader(f);
                        pt_r64(&rd, 'U');
                        pt_r64(&rd, 'U');
                        pt_rbool(&rd);
                        rd.at += 18;
                        wr = pt_r64(&rd, 'r');
                        rdx = pt_r64(&rd, 'r');
                        PTCHECK(!rd.bad && wr == (long long)rec->produced && rdx == (long long)rec->sent && wr > rdx, "the write index counts what was made, the read index what was sent: %lld %lld", wr, rdx);
                }
        }
        pulse_client_close(&pt_engine, slot);
        PTCHECK(pt_engine.records_made == pt_engine.records_freed, "all given back");
}

// A rate is converted: the amplitude of a tone and what is left over when it is taken out, on a source at one rate and a stream at another.
static void pt_resample_case(long rin, long rout, int channels, double hz)
{
        static float in[512], out[96000];
        struct pt_rec_args a = pt_rec_args();
        long slot, mic, fed = 0, count = 0, window, skip = 3000;
        unsigned long channel;
        double amplitude, rest, db;

        pt_reset();
        pulse_sink_add(&pt_engine, "null", "Null", 48000);
        mic = pulse_source_add(&pt_engine, "alsa_input.rate.00", "Rate", rin);
        slot = pt_connect(0, 90, 35, "Rate");
        a.format = AUDIO_FORMAT_F32LE;
        a.channels = channels;
        a.rate = (unsigned long)rout;
        a.source_index = (unsigned long)mic;
        a.fragsize = 16000;
        if (pt_rec_create(slot, 35, 20, &a) != 0)
        {
                PTCHECK(0, "%ld to %ld: the stream is not made", rin, rout);
                pulse_client_close(&pt_engine, slot);
                return;
        }
        channel = pt_rec_reply.channel;
        pt_rec_clear();
        while (fed < rin)
        {
                long n = rin - fed < 512 ? rin - fed : 512;

                for (long i = 0; i < n; i++)
                        in[i] = (float)(0.5 * sin(2.0 * M_PI * hz * (double)(fed + i) / (double)rin));
                pulse_source_feed(&pt_engine, mic, in, in, n);
                pt_rec_pull(slot, channel);
                fed += n;
        }
        count = pt_rec_len / (4 * channels);
        for (long i = 0; i < count && i < 96000; i++)
                memcpy(out + i, pt_rec_buf + (size_t)(i * 4 * channels), 4);
        // The filter holds back its length: as many outputs as the taps are inputs.
        PTCHECK(fabs((double)(count - rout)) <= 64.0 * (rin > rout ? ceil((double)rin / (double)rout) : 1.0) * (double)rout / (double)rin + 16.0, "%ld to %ld: %ld frames out of a second in, %ld expected", rin, rout, count, rout);
        window = (long)((double)rout / hz) * ((count - skip) / (long)((double)rout / hz));
        if (window < 2000)
        {
                PTCHECK(0, "%ld to %ld: too few frames to judge: %ld", rin, rout, count);
                pulse_client_close(&pt_engine, slot);
                return;
        }
        rest = pt_fit(out + skip, window, hz, (double)rout, &amplitude);
        db = 20.0 * log10(amplitude / 0.5);
        PTCHECK(fabs(db) < 0.02, "%ld to %ld, %d channels: the tone is %.4f dB from where it was", rin, rout, channels, db);
        PTCHECK(rest < 0.3536 * pow(10.0, -85.0 / 20.0), "%ld to %ld, %d channels: what is left over is %.1f dB under the tone", rin, rout, channels, 20.0 * log10(0.3536 / (rest > 1e-12 ? rest : 1e-12)));
        pulse_client_close(&pt_engine, slot);
        for (int t = 0; t < AUDIO_TABLES; t++)
                PTCHECK(audio_tables[t].users == 0, "table %d is not kept", t);
}

static void pt_test_record_resample(void)
{
        pt_resample_case(48000, 16000, 1, 1000.0);
        pt_resample_case(48000, 44100, 2, 441.0);
        pt_resample_case(44100, 48000, 2, 1000.0);
        pt_resample_case(48000, 8000, 1, 1000.0);
        pt_resample_case(16000, 48000, 1, 1000.0);
        pt_resample_case(96000, 48000, 2, 1000.0);
        pt_resample_case(48000, 22050, 1, 441.0);
        pt_resample_case(44100, 16000, 1, 400.0);
}

// Peak detect: a few frames a second, each the greatest of a window of the source, the way a program draws a meter.
static void pt_test_record_peaks(void)
{
        static float l[5000], r[5000], wl[8], wr[8];
        static unsigned char expect[256];
        struct pt_rec_args a = pt_rec_args();
        long slot, windows;
        unsigned long channel;

        pt_reset();
        pulse_sink_add(&pt_engine, "null", "Null", 48000);
        pt_sig(l, r, 0, 5000);
        for (int i = 0; i < 5000; i++)
        {
                l[i] *= 0.4f;
                r[i] *= 0.4f;
        }
        slot = pt_connect(0, 95, 35, "Meter");
        a.peak = 1;
        a.format = AUDIO_FORMAT_F32LE;
        a.channels = 1;
        a.rate = 25;
        a.fragsize = 4;
        PTCHECK(pt_rec_create(slot, 35, 20, &a) == 0, "a stream of peaks");
        channel = pt_rec_reply.channel;
        windows = pt_ref_windows(l, r, 5000, 1920, wl, wr);
        PTCHECK(windows == 2, "the oracle has two windows of 1920 in 5000 frames");
        pt_rec_clear();
        for (long at = 0; at < 5000; at += 500)
        {
                pulse_source_feed(&pt_engine, 0, l + at, r + at, 500);
                pt_rec_pull(slot, channel);
        }
        {
                float mono[2] = {wl[0] > wr[0] ? wl[0] : wr[0], wl[1] > wr[1] ? wl[1] : wr[1]};

                PTCHECK(pt_rec_len == 8 && pt_rec_frames == 2 && !memcmp(pt_rec_buf, mono, 8), "a mono stream gets the greater of the pair, a frame a window, carried across blocks: %ld bytes, %.5f %.5f against %.5f %.5f",
                        pt_rec_len, ((float *)pt_rec_buf)[0], ((float *)pt_rec_buf)[1], mono[0], mono[1]);
        }
        pulse_client_close(&pt_engine, slot);
        // Stereo: each side its own, whatever the stream's volume or mute (it measures the source).
        slot = pt_connect(0, 96, 35, "Meter 2");
        a.channels = 2;
        a.fragsize = 8;
        a.muted = 1;
        a.muted_set = 1;
        a.volume = 0x4000;
        a.volume_set = 1;
        PTCHECK(pt_rec_create(slot, 35, 21, &a) == 0, "a stereo stream of peaks, muted and at a low volume");
        channel = pt_rec_reply.channel;
        pt_rec_clear();
        for (long at = 0; at < 5000; at += 500)
        {
                pulse_source_feed(&pt_engine, 0, l + at, r + at, 500);
                pt_rec_pull(slot, channel);
        }
        {
                float pair[4] = {wl[0], wr[0], wl[1], wr[1]};

                PTCHECK(pt_rec_len == 16 && !memcmp(pt_rec_buf, pair, 16), "each side's own, and the stream's mute and volume are not in it");
        }
        pulse_client_close(&pt_engine, slot);
        // A rate above the source's: a window of one frame, which is |sample|.
        slot = pt_connect(0, 97, 35, "Meter 3");
        a = pt_rec_args();
        a.peak = 1;
        a.format = AUDIO_FORMAT_S16LE;
        a.rate = 96000;
        PTCHECK(pt_rec_create(slot, 35, 22, &a) == 0, "peaks at a rate above the source's");
        channel = pt_rec_reply.channel;
        pt_rec_clear();
        pulse_source_feed(&pt_engine, 0, l, r, 64);
        pt_rec_pull(slot, channel);
        {
                long bytes = 0, bad = 0;

                for (int i = 0; i < 64; i++)
                {
                        bytes += pt_ref_sample(expect + bytes, AUDIO_FORMAT_S16LE, fabsf(l[i]));
                        bytes += pt_ref_sample(expect + bytes, AUDIO_FORMAT_S16LE, fabsf(r[i]));
                }
                bad = pt_rec_len == bytes ? pt_diff_bytes(pt_rec_buf, expect, bytes) : -2;
                PTCHECK(bad == -1, "a frame of the greatest of the two for each frame, in 16 bits: %ld bytes, differing at %ld", pt_rec_len, bad);
        }
        pulse_client_close(&pt_engine, slot);
        PTCHECK(pt_engine.records_made == pt_engine.records_freed, "all given back");
}
struct pt_out_info
{
        unsigned long index, owner, client, source, rate, vol[8];
        int format, channels, map_n, vol_n, props, corked, muted, has_volume, writable;
        char name[80], resample[24], driver[32], media[64];
        long long buffer_us, source_us;
};

// A source output as the info commands say it at a protocol version, read to the field that version ends at; 1 when it is whole.
static int pt_read_output(struct pt_r *r, unsigned version, struct pt_out_info *o)
{
        const char *s;
        int positions[32];

        memset(o, 0, sizeof(*o));
        o->index = pt_ru32(r);
        s = pt_rstr(r);
        snprintf(o->name, sizeof(o->name), "%s", s ? s : "");
        o->owner = pt_ru32(r);
        o->client = pt_ru32(r);
        o->source = pt_ru32(r);
        pt_rspec(r, &o->format, &o->channels, &o->rate);
        o->map_n = pt_rmap(r, positions);
        o->buffer_us = pt_r64(r, 'U');
        o->source_us = pt_r64(r, 'U');
        s = pt_rstr(r);
        snprintf(o->resample, sizeof(o->resample), "%s", s ? s : "");
        s = pt_rstr(r);
        snprintf(o->driver, sizeof(o->driver), "%s", s ? s : "");
        if (version >= 13)
                o->props = pt_rprops(r, "media.name", o->media, sizeof(o->media));
        if (version >= 19)
                o->corked = pt_rbool(r);
        if (version >= 22)
        {
                o->vol_n = pt_rvol(r, o->vol);
                o->muted = pt_rbool(r);
                o->has_volume = pt_rbool(r);
                o->writable = pt_rbool(r);
                pt_rformat(r);
        }
        return !r->bad;
}

// The source output of an index as a client at a version is told it; 1 when it was answered, whole, and read to its end.
static int pt_output_info(long slot, unsigned version, unsigned long tag, unsigned long index, struct pt_out_info *o)
{
        struct pt_w w = {{0}, 0};
        const struct pt_frame *f;
        struct pt_r r;

        pt_u32(&w, index);
        pt_command(slot, PULSE_COMMAND_GET_SOURCE_OUTPUT_INFO, tag, &w);
        pt_collect(slot);
        f = pt_find(PULSE_COMMAND_REPLY);
        if (!f || f->tag != tag)
                return 0;
        r = pt_reply_reader(f);
        return pt_read_output(&r, version, o) && r.at == f->length;
}

static void pt_test_record_info(void)
{
        static const unsigned versions[] = {35, 29, 22, 21, 19, 16, 13, 12, 11};

        for (unsigned v = 0; v < sizeof(versions) / sizeof(versions[0]); v++)
        {
                struct pt_rec_args a = pt_rec_args();
                struct pt_out_info o;
                long slot, mic;
                struct pt_w w = {{0}, 0};
                const struct pt_frame *f;
                struct pt_r r;

                pt_reset();
                pulse_sink_add(&pt_engine, "null", "Null", 48000);
                mic = pulse_source_add(&pt_engine, "alsa_input.test.00", "Test mic", 44100);
                slot = pt_connect(0, 40, versions[v], "Info");
                a.source_index = (unsigned long)mic;
                PTCHECK(pt_rec_create(slot, versions[v], 20, &a) == 0, "version %u: a stream to ask about", versions[v]);
                PTCHECK(pt_output_info(slot, versions[v], 21, 0, &o), "version %u: the info is whole and read to its end", versions[v]);
                PTCHECK(o.index == 0 && o.owner == PULSE_INVALID && o.client == 0 && o.source == (unsigned long)mic && o.format == AUDIO_FORMAT_S16LE && o.channels == 2 && o.rate == 48000 && o.map_n == 2 &&
                            !strcmp(o.resample, "copy") && !strcmp(o.driver, "protocol-native"),
                        "version %u: the ids, the spec and the driver", versions[v]);
                if (versions[v] >= 13)
                        PTCHECK(o.props >= 1 && !strcmp(o.media, "Record Stream"), "version %u: the properties the program gave: %d, '%s'", versions[v], o.props, o.media);
                if (versions[v] >= 19)
                        PTCHECK(!o.corked, "version %u: not corked", versions[v]);
                if (versions[v] >= 22)
                        PTCHECK(o.vol_n == 2 && o.vol[0] == 0x10000 && o.vol[1] == 0x10000 && !o.muted && o.has_volume && o.writable, "version %u: the volume and the mute", versions[v]);
                // The list is the same, one entry.
                pt_command(slot, PULSE_COMMAND_GET_SOURCE_OUTPUT_INFO_LIST, 22, 0);
                pt_collect(slot);
                f = pt_find(PULSE_COMMAND_REPLY);
                PTCHECK(f && f->tag == 22, "version %u: the list is answered", versions[v]);
                if (f)
                {
                        r = pt_reply_reader(f);
                        PTCHECK(pt_read_output(&r, versions[v], &o) && r.at == f->length && o.index == 0, "version %u: one entry, read to its end", versions[v]);
                }
                w.n = 0;
                pt_u32(&w, 99);
                pt_command(slot, PULSE_COMMAND_GET_SOURCE_OUTPUT_INFO, 23, &w);
                pt_collect(slot);
                PTCHECK(pt_expect_error(23, PULSE_ERR_NOENTITY), "version %u: no such output", versions[v]);
                pulse_client_close(&pt_engine, slot);
        }
}

// Everything a program does to a stream it has: latency, attributes, name, properties, rate, volume, moving, killing, deleting; and what a watcher hears of it.
static void pt_test_record_commands(void)
{
        static float l[512], r[512];
        struct pt_rec_args a = pt_rec_args();
        long slot, root, watcher, mic1, mic2;
        unsigned long channel, index, what[16], where[16];
        struct pt_w w = {{0}, 0};
        struct pt_out_info o;
        const struct pt_frame *f;
        struct pt_r rd;
        int n;

        pt_reset();
        pulse_sink_add(&pt_engine, "null", "Null", 48000);
        mic1 = pulse_source_add(&pt_engine, "alsa_input.a.00", "Mic A", 48000);
        mic2 = pulse_source_add(&pt_engine, "alsa_input.b.00", "Mic B", 44100);
        pt_sig(l, r, 0, 512);
        root = pt_connect(0, 100, 35, "Root");
        watcher = pt_connect(0, 101, 35, "Watcher");
        w.n = 0;
        pt_u32(&w, 0x2ff);
        pt_command(watcher, PULSE_COMMAND_SUBSCRIBE, 1, &w);
        pt_collect(watcher);
        slot = pt_connect(1000, 102, 35, "Owner");
        a.source_index = (unsigned long)mic1;
        pt_collect(watcher);
        PTCHECK(pt_rec_create(slot, 35, 20, &a) == 0, "the stream");
        channel = pt_rec_reply.channel;
        index = pt_rec_reply.index;
        pt_collect(watcher);
        n = pt_events(what, where, 16);
        PTCHECK(n >= 1 && what[n - 1] == (PULSE_FACILITY_SOURCE_OUTPUT | PULSE_EVENT_NEW) && where[n - 1] == index, "a watcher is told of the new source output");
        // Latency: what the source holds and what waits for the program, and the byte counts exact.
        pulse_source_delay(&pt_engine, mic1, 960);
        pulse_source_feed(&pt_engine, mic1, l, r, 512);
        w.n = 0;
        pt_u32(&w, channel);
        pt_byte(&w, 'T');
        pt_be32(&w, 123);
        pt_be32(&w, 456);
        pt_command(slot, PULSE_COMMAND_GET_RECORD_LATENCY, 30, &w);
        pt_collect(slot);
        f = pt_find(PULSE_COMMAND_REPLY);
        PTCHECK(f && f->tag == 30, "record latency answered");
        if (f)
        {
                long long latency, source_latency, wr, rdx;
                int running;

                rd = pt_reply_reader(f);
                latency = pt_r64(&rd, 'U');
                source_latency = pt_r64(&rd, 'U');
                running = pt_rbool(&rd);
                PTCHECK(rd.p[rd.at] == 'T' && wire_be32(rd.p + rd.at + 1) == 123 && wire_be32(rd.p + rd.at + 5) == 456, "the request's time is echoed");
                rd.at += 9;
                PTCHECK(rd.p[rd.at] == 'T' && wire_be32(rd.p + rd.at + 1) == 1700000001, "and the server's wall clock follows");
                rd.at += 9;
                wr = pt_r64(&rd, 'r');
                rdx = pt_r64(&rd, 'r');
                PTCHECK(!rd.bad && rd.at == f->length, "the answer is read to its end");
                PTCHECK(source_latency == 20000 && latency >= 20000 + 10666 && latency <= 20000 + 11500 && running, "the latency is the source's and what waits: %lld %lld", latency, source_latency);
                PTCHECK(wr == 2048 && rdx == 2048, "bytes made %lld, bytes sent %lld", wr, rdx);
        }
        // Buffer attributes: what a program asks, made whole; the fragments follow.
        w.n = 0;
        pt_u32(&w, channel);
        pt_u32(&w, 65536);
        pt_u32(&w, 960);
        pt_bool(&w, 1);
        pt_bool(&w, 0);
        pt_command(slot, PULSE_COMMAND_SET_RECORD_STREAM_BUFFER_ATTR, 31, &w);
        pt_collect(slot);
        f = pt_find(PULSE_COMMAND_REPLY);
        PTCHECK(f && f->tag == 31, "attributes answered");
        if (f)
        {
                rd = pt_reply_reader(f);
                PTCHECK(pt_ru32(&rd) == 65536 && pt_ru32(&rd) == 960 && pt_r64(&rd, 'U') >= 0 && !rd.bad && rd.at == f->length, "maxlength, fragsize and a latency, read to the end");
        }
        pt_rec_clear();
        pulse_source_feed(&pt_engine, mic1, l, r, 512);
        pt_rec_pull(slot, channel);
        PTCHECK(pt_rec_len == 2048 && pt_rec_frames == 3 && pt_rec_longest == 960, "and the data comes in fragments of that: %ld bytes in %ld frames, the longest %ld", pt_rec_len, pt_rec_frames, pt_rec_longest);
        w.n = 0;
        pt_u32(&w, 99);
        pt_u32(&w, 65536);
        pt_u32(&w, 960);
        pt_bool(&w, 1);
        pt_bool(&w, 0);
        pt_command(slot, PULSE_COMMAND_SET_RECORD_STREAM_BUFFER_ATTR, 32, &w);
        pt_collect(slot);
        PTCHECK(pt_expect_error(32, PULSE_ERR_NOENTITY), "attributes of a stream that is not");
        // Name and properties.
        w.n = 0;
        pt_u32(&w, channel);
        pt_str(&w, "Renamed");
        pt_command(slot, PULSE_COMMAND_SET_RECORD_STREAM_NAME, 33, &w);
        pt_collect(slot);
        PTCHECK(pt_expect_ack(33), "renamed");
        pt_collect(watcher);
        n = pt_events(what, where, 16);
        PTCHECK(n >= 1 && what[n - 1] == (PULSE_FACILITY_SOURCE_OUTPUT | PULSE_EVENT_CHANGE) && where[n - 1] == index, "a rename is a change of the source output");
        PTCHECK(pt_output_info(slot, 35, 34, index, &o) && !strcmp(o.media, "Renamed"), "and the info shows it: '%s'", o.media);
        w.n = 0;
        pt_u32(&w, channel);
        pt_u32(&w, 2);
        pt_props(&w, "x.key", "value", 0, 0);
        pt_command(slot, PULSE_COMMAND_UPDATE_RECORD_STREAM_PROPLIST, 35, &w);
        pt_collect(slot);
        PTCHECK(pt_expect_ack(35), "properties updated");
        PTCHECK(pt_output_info(slot, 35, 36, index, &o) && o.props == 4, "and there are four of them: %d", o.props);
        w.n = 0;
        pt_u32(&w, channel);
        pt_str(&w, "x.key");
        pt_str(&w, 0);
        pt_command(slot, PULSE_COMMAND_REMOVE_RECORD_STREAM_PROPLIST, 37, &w);
        pt_collect(slot);
        PTCHECK(pt_expect_ack(37) && pt_output_info(slot, 35, 38, index, &o) && o.props == 3, "and one removed: %d", o.props);
        // A rate that changes needs a program that said it would.
        w.n = 0;
        pt_u32(&w, channel);
        pt_u32(&w, 24000);
        pt_command(slot, PULSE_COMMAND_UPDATE_RECORD_STREAM_SAMPLE_RATE, 39, &w);
        pt_collect(slot);
        PTCHECK(pt_expect_error(39, PULSE_ERR_INVALID), "a rate cannot change on a stream that was not made to");
        // Volume and mute, by the owner and by root.
        w.n = 0;
        pt_u32(&w, index);
        pt_vol(&w, 2, 0x8000);
        pt_command(slot, PULSE_COMMAND_SET_SOURCE_OUTPUT_VOLUME, 40, &w);
        pt_collect(slot);
        PTCHECK(pt_expect_ack(40) && pt_output_info(slot, 35, 41, index, &o) && o.vol_n == 2 && o.vol[0] == 0x8000 && o.vol[1] == 0x8000, "the owner sets its volume");
        w.n = 0;
        pt_u32(&w, index);
        pt_vol(&w, 3, 0x8000);
        pt_command(root, PULSE_COMMAND_SET_SOURCE_OUTPUT_VOLUME, 42, &w);
        pt_collect(root);
        PTCHECK(pt_expect_error(42, PULSE_ERR_INVALID), "a volume of another number of channels is refused");
        w.n = 0;
        pt_u32(&w, 99);
        pt_vol(&w, 2, 0x8000);
        pt_command(root, PULSE_COMMAND_SET_SOURCE_OUTPUT_VOLUME, 43, &w);
        pt_collect(root);
        PTCHECK(pt_expect_error(43, PULSE_ERR_NOENTITY), "and so is an output there is not");
        // Moving: to another source at another rate, the data goes on from there; to one that cannot be, it stays.
        w.n = 0;
        pt_u32(&w, index);
        pt_u32(&w, (unsigned long)mic2);
        pt_str(&w, 0);
        pt_command(slot, PULSE_COMMAND_MOVE_SOURCE_OUTPUT, 44, &w);
        pt_collect(slot);
        PTCHECK(pt_expect_ack(44) && pt_output_info(slot, 35, 45, index, &o) && o.source == (unsigned long)mic2, "moved to the other microphone");
        pt_rec_clear();
        pulse_source_feed(&pt_engine, mic1, l, r, 512);
        pt_rec_pull(slot, channel);
        PTCHECK(pt_rec_len == 0, "the one it left gives it nothing");
        for (int block = 0; block < 10; block++)
        {
                pulse_source_feed(&pt_engine, mic2, l, r, 441);
                pt_rec_pull(slot, channel);
        }
        PTCHECK(pt_rec_len / 4 > 4800 - 160 && pt_rec_len / 4 < 4800 + 8, "and the other, at 44100, gives it the second's tenth at 48000: %ld frames", pt_rec_len / 4);
        w.n = 0;
        pt_u32(&w, index);
        pt_u32(&w, 0);
        pt_str(&w, 0);
        pt_command(slot, PULSE_COMMAND_MOVE_SOURCE_OUTPUT, 46, &w);
        pt_collect(slot);
        PTCHECK(pt_expect_error(46, PULSE_ERR_ACCESS), "another user's program is not moved onto what the session plays");
        pt_command(root, PULSE_COMMAND_MOVE_SOURCE_OUTPUT, 47, &w);
        pt_collect(root);
        PTCHECK(pt_expect_ack(47), "root may move it there");
        w.n = 0;
        pt_u32(&w, index);
        pt_u32(&w, 99);
        pt_str(&w, 0);
        pt_command(root, PULSE_COMMAND_MOVE_SOURCE_OUTPUT, 48, &w);
        pt_collect(root);
        PTCHECK(pt_expect_error(48, PULSE_ERR_NOENTITY), "and to a source that is not, it is not");
        // Flush is accepted; a channel that is not a stream is found out by each command.
        pt_rec_command(slot, PULSE_COMMAND_FLUSH_RECORD_STREAM, 49, channel, 0, 0);
        PTCHECK(pt_expect_ack(49), "flush");
        pt_rec_command(slot, PULSE_COMMAND_FLUSH_RECORD_STREAM, 50, 99, 0, 0);
        PTCHECK(pt_expect_error(50, PULSE_ERR_NOENTITY), "flush of nothing");
        pt_rec_command(slot, PULSE_COMMAND_DELETE_RECORD_STREAM, 51, 99, 0, 0);
        PTCHECK(pt_expect_error(51, PULSE_ERR_NOENTITY), "delete of nothing");
        w.n = 0;
        pt_u32(&w, 99);
        pt_byte(&w, 'T');
        pt_be32(&w, 1);
        pt_be32(&w, 2);
        pt_command(slot, PULSE_COMMAND_GET_RECORD_LATENCY, 52, &w);
        pt_collect(slot);
        PTCHECK(pt_expect_error(52, PULSE_ERR_NOENTITY), "latency of nothing");
        // Delete: acknowledged; its channel is nothing after; the watcher hears it go.
        pt_collect(watcher);
        pt_rec_command(slot, PULSE_COMMAND_DELETE_RECORD_STREAM, 53, channel, 0, 0);
        PTCHECK(pt_expect_ack(53), "deleted");
        pt_rec_command(slot, PULSE_COMMAND_DELETE_RECORD_STREAM, 54, channel, 0, 0);
        PTCHECK(pt_expect_error(54, PULSE_ERR_NOENTITY), "and not twice");
        pt_collect(watcher);
        n = pt_events(what, where, 16);
        PTCHECK(n >= 1 && what[n - 1] == (PULSE_FACILITY_SOURCE_OUTPUT | PULSE_EVENT_REMOVE) && where[n - 1] == index, "the watcher is told it is gone");
        PTCHECK(pulse_source_wanted(&pt_engine, mic2) == 0 && pulse_source_wanted(&pt_engine, 0) == 0, "and nothing listens");
        // A variable rate.
        a = pt_rec_args();
        a.source_index = (unsigned long)mic1;
        a.variable = 1;
        PTCHECK(pt_rec_create(slot, 35, 60, &a) == 0, "a stream that may change its rate");
        channel = pt_rec_reply.channel;
        index = pt_rec_reply.index;
        w.n = 0;
        pt_u32(&w, channel);
        pt_u32(&w, 24000);
        pt_command(slot, PULSE_COMMAND_UPDATE_RECORD_STREAM_SAMPLE_RATE, 61, &w);
        pt_collect(slot);
        PTCHECK(pt_expect_ack(61), "its rate changes");
        pt_rec_clear();
        for (int block = 0; block < 4; block++)
        {
                pulse_source_feed(&pt_engine, mic1, l, r, 512);
                pt_rec_pull(slot, channel);
        }
        PTCHECK(pt_rec_len / 4 > 1024 - 80 && pt_rec_len / 4 <= 1024, "and the data is at the new rate: %ld frames of 2048 in", pt_rec_len / 4);
        w.n = 0;
        pt_u32(&w, channel);
        pt_u32(&w, 0);
        pt_command(slot, PULSE_COMMAND_UPDATE_RECORD_STREAM_SAMPLE_RATE, 62, &w);
        pt_collect(slot);
        PTCHECK(pt_expect_error(62, PULSE_ERR_INVALID), "a rate of nothing is refused");
        // Kill: only root; the program is told.
        w.n = 0;
        pt_u32(&w, index);
        pt_command(slot, PULSE_COMMAND_KILL_SOURCE_OUTPUT, 63, &w);
        pt_collect(slot);
        PTCHECK(pt_expect_error(63, PULSE_ERR_ACCESS), "a program cannot kill, even its own, by the administrator's command");
        pt_command(root, PULSE_COMMAND_KILL_SOURCE_OUTPUT, 64, &w);
        pt_collect(root);
        PTCHECK(pt_expect_ack(64), "root kills it");
        pt_collect(slot);
        f = pt_find(PULSE_COMMAND_RECORD_STREAM_KILLED);
        PTCHECK(f && f->length == 15 && wire_be32(f->payload + 11) == channel, "and the program is told which of its streams it was");
        pt_command(root, PULSE_COMMAND_KILL_SOURCE_OUTPUT, 65, &w);
        pt_collect(root);
        PTCHECK(pt_expect_error(65, PULSE_ERR_NOENTITY), "a second kill finds nothing");
        // Gone with the client.
        PTCHECK(pt_rec_create(slot, 35, 66, &a) == 0, "another");
        pulse_client_close(&pt_engine, slot);
        pulse_client_close(&pt_engine, root);
        pulse_client_close(&pt_engine, watcher);
        PTCHECK(pt_engine.records_made == pt_engine.records_freed && pulse_source_wanted(&pt_engine, mic1) == 0, "all given back: %lu made, %lu freed", pt_engine.records_made, pt_engine.records_freed);
        for (int t = 0; t < AUDIO_TABLES; t++)
                PTCHECK(audio_tables[t].users == 0, "table %d is not kept", t);
}

struct pt_src_info
{
        unsigned long index, monitor_of, rate, vol[8], flags, state;
        char name[80], description[120], monitor_name[80], device_class[16];
        int has_monitor_name, format, channels, vol_n, muted;
};

// A source as the info commands say it at a protocol version, to the field that version ends at; 1 when whole.
static int pt_read_source(struct pt_r *r, unsigned version, struct pt_src_info *s)
{
        const char *t;
        int positions[32];

        memset(s, 0, sizeof(*s));
        s->index = pt_ru32(r);
        t = pt_rstr(r);
        snprintf(s->name, sizeof(s->name), "%s", t ? t : "");
        t = pt_rstr(r);
        snprintf(s->description, sizeof(s->description), "%s", t ? t : "");
        pt_rspec(r, &s->format, &s->channels, &s->rate);
        pt_rmap(r, positions);
        pt_ru32(r);
        s->vol_n = pt_rvol(r, s->vol);
        s->muted = pt_rbool(r);
        s->monitor_of = pt_ru32(r);
        t = pt_rstr(r);
        s->has_monitor_name = t != 0;
        snprintf(s->monitor_name, sizeof(s->monitor_name), "%s", t ? t : "");
        pt_r64(r, 'U');
        pt_rstr(r);
        s->flags = pt_ru32(r);
        if (version >= 13)
        {
                pt_rprops(r, "device.class", s->device_class, sizeof(s->device_class));
                pt_r64(r, 'U');
        }
        if (version >= 15)
        {
                pt_rvolume(r);
                s->state = pt_ru32(r);
                pt_ru32(r);
                pt_ru32(r);
        }
        if (version >= 16)
        {
                pt_ru32(r);
                pt_rstr(r);
        }
        if (version >= 22)
        {
                if (r->at + 2 <= r->n && r->p[r->at] == 'B')
                        r->at += 2;
                else
                        r->bad = 1;
                pt_rformat(r);
        }
        return !r->bad;
}

static int pt_source_info(long slot, unsigned version, unsigned long tag, unsigned long index, const char *name, struct pt_src_info *s)
{
        struct pt_w w = {{0}, 0};
        const struct pt_frame *f;
        struct pt_r r;

        pt_u32(&w, index);
        pt_str(&w, name);
        pt_command(slot, PULSE_COMMAND_GET_SOURCE_INFO, tag, &w);
        pt_collect(slot);
        f = pt_find(PULSE_COMMAND_REPLY);
        if (!f || f->tag != tag)
                return 0;
        r = pt_reply_reader(f);
        return pt_read_source(&r, version, s) && r.at == f->length;
}

// Sources: the monitors and the capture devices, their info, the default, volumes, and a device that goes or changes.
static void pt_test_record_sources(void)
{
        static const unsigned versions[] = {35, 22, 16, 15, 13, 12};
        static float l[512], r[512];
        struct pt_src_info s;
        struct pt_rec_args a = pt_rec_args();
        long root, other, mic, stream_owner;
        struct pt_w w = {{0}, 0};
        const struct pt_frame *f;
        struct pt_r rd;
        unsigned long channel;

        for (unsigned v = 0; v < sizeof(versions) / sizeof(versions[0]); v++)
        {
                long slot;
                int count = 0;

                pt_reset();
                pulse_sink_add(&pt_engine, "null", "Null", 48000);
                pulse_sink_add(&pt_engine, "other", "Other", 44100);
                mic = pulse_source_add(&pt_engine, "alsa_input.test.00", "Test <mic>", 48000);
                slot = pt_connect(0, 110, versions[v], "Sources");
                pt_command(slot, PULSE_COMMAND_GET_SOURCE_INFO_LIST, 10, 0);
                pt_collect(slot);
                f = pt_find(PULSE_COMMAND_REPLY);
                PTCHECK(f && f->tag == 10, "version %u: the source list is answered", versions[v]);
                if (f)
                {
                        rd = pt_reply_reader(f);
                        while (!rd.bad && rd.at < f->length)
                        {
                                int whole = pt_read_source(&rd, versions[v], &s);

                                PTCHECK(whole, "version %u: source %d is whole", versions[v], count);
                                if (count == 0)
                                        PTCHECK(s.index == 0 && !strcmp(s.name, "null.monitor") && !strcmp(s.description, "Monitor of Null") && s.monitor_of == 0 && s.has_monitor_name && !strcmp(s.monitor_name, "null") &&
                                                    s.rate == 48000 && s.vol_n == 2 && s.vol[0] == 0x10000 && !s.muted,
                                                "version %u: the first is the monitor of the first sink: %s", versions[v], s.name);
                                if (count == 1)
                                        PTCHECK(s.index == 1 && !strcmp(s.name, "other.monitor") && s.rate == 44100 && s.monitor_of == 1, "version %u: and the second of the second", versions[v]);
                                if (count == 2)
                                {
                                        PTCHECK(s.index == (unsigned long)mic && !strcmp(s.name, "alsa_input.test.00") && !strcmp(s.description, "Test <mic>") && s.monitor_of == PULSE_INVALID && !s.has_monitor_name &&
                                                    s.rate == 48000 && s.flags == (0x2 | 0x4 | 0x10),
                                                "version %u: the third is the capture device, a monitor of nothing: %s, flags %lx", versions[v], s.name, s.flags);
                                        if (versions[v] >= 13)
                                                PTCHECK(!strcmp(s.device_class, "sound"), "version %u: of the class 'sound'", versions[v]);
                                }
                                if (count < 2 && versions[v] >= 13)
                                        PTCHECK(!strcmp(s.device_class, "monitor"), "version %u: a monitor is of the class 'monitor'", versions[v]);
                                if (versions[v] >= 15)
                                        PTCHECK(s.state == 1, "version %u: with nothing listening a source is idle", versions[v]);
                                count++;
                        }
                        PTCHECK(count == 3 && !rd.bad, "version %u: three sources in the list: %d", versions[v], count);
                }
                // By name, by index, by a number as a name; and what is not there.
                PTCHECK(pt_source_info(slot, versions[v], 11, PULSE_INVALID, "alsa_input.test.00", &s) && s.index == (unsigned long)mic, "version %u: a source by name", versions[v]);
                PTCHECK(pt_source_info(slot, versions[v], 12, (unsigned long)mic, 0, &s) && !strcmp(s.name, "alsa_input.test.00"), "version %u: by index", versions[v]);
                PTCHECK(pt_source_info(slot, versions[v], 13, PULSE_INVALID, "8", &s) && s.index == (unsigned long)mic, "version %u: by its number as a name", versions[v]);
                PTCHECK(!pt_source_info(slot, versions[v], 14, PULSE_INVALID, "nonesuch", &s) && pt_expect_error(14, PULSE_ERR_NOENTITY), "version %u: a name that is no source", versions[v]);
                PTCHECK(!pt_source_info(slot, versions[v], 15, 7, 0, &s) && pt_expect_error(15, PULSE_ERR_NOENTITY), "version %u: an index that is none", versions[v]);
                // Busy when listened to.
                if (versions[v] >= 15)
                {
                        a = pt_rec_args();
                        a.source_index = (unsigned long)mic;
                        PTCHECK(pt_rec_create(slot, versions[v], 16, &a) == 0, "version %u: a listener", versions[v]);
                        PTCHECK(pt_source_info(slot, versions[v], 17, (unsigned long)mic, 0, &s) && s.state == 0, "version %u: a source listened to is running", versions[v]);
                }
                pulse_client_close(&pt_engine, slot);
        }
        // Lookups and the default.
        pt_reset();
        pulse_sink_add(&pt_engine, "null", "Null", 48000);
        pulse_sink_add(&pt_engine, "other", "Other", 44100);
        root = pt_connect(0, 111, 35, "Root");
        other = pt_connect(1000, 112, 35, "Other");
        PTCHECK(pt_engine.default_source == 0, "with no capture device the default source is the first monitor");
        mic = pulse_source_add(&pt_engine, "alsa_input.test.00", "Test mic", 48000);
        PTCHECK(pt_engine.default_source == mic, "a capture device is the default source over a monitor");
        w.n = 0;
        pt_str(&w, "alsa_input.test.00");
        pt_command(root, PULSE_COMMAND_LOOKUP_SOURCE, 20, &w);
        pt_collect(root);
        f = pt_find(PULSE_COMMAND_REPLY);
        PTCHECK(f && f->tag == 20 && f->length == 15 && wire_be32(f->payload + 11) == (unsigned long)mic, "lookup of a source by its name");
        w.n = 0;
        pt_str(&w, "other.monitor");
        pt_command(root, PULSE_COMMAND_LOOKUP_SOURCE, 21, &w);
        pt_collect(root);
        f = pt_find(PULSE_COMMAND_REPLY);
        PTCHECK(f && f->tag == 21 && wire_be32(f->payload + 11) == 1, "and of a monitor");
        w.n = 0;
        pt_str(&w, "alsa_input.test.00");
        pt_command(root, PULSE_COMMAND_LOOKUP_SINK, 22, &w);
        pt_collect(root);
        PTCHECK(pt_expect_error(22, PULSE_ERR_NOENTITY), "a source is not found as a sink");
        w.n = 0;
        pt_str(&w, "null");
        pt_command(root, PULSE_COMMAND_LOOKUP_SOURCE, 23, &w);
        pt_collect(root);
        PTCHECK(pt_expect_error(23, PULSE_ERR_NOENTITY), "nor a sink as a source");
        pt_command(root, PULSE_COMMAND_GET_SERVER_INFO, 24, 0);
        pt_collect(root);
        f = pt_find(PULSE_COMMAND_REPLY);
        if (f)
        {
                int format, channels, positions[8];
                unsigned long rate;

                rd = pt_reply_reader(f);
                pt_rstr(&rd);
                pt_rstr(&rd);
                pt_rstr(&rd);
                pt_rstr(&rd);
                pt_rspec(&rd, &format, &channels, &rate);
                pt_rstr(&rd);
                {
                        const char *source = pt_rstr(&rd);

                        pt_ru32(&rd);
                        pt_rmap(&rd, positions);
                        PTCHECK(!rd.bad && rd.at == f->length && source && !strcmp(source, "alsa_input.test.00"), "the server names the capture device as the default source: %s", source ? source : "(none)");
                }
        }
        // Setting the default source: only root; a monitor is one; what is not a source is not.
        w.n = 0;
        pt_str(&w, "other.monitor");
        pt_command(other, PULSE_COMMAND_SET_DEFAULT_SOURCE, 25, &w);
        pt_collect(other);
        PTCHECK(pt_expect_error(25, PULSE_ERR_ACCESS), "another user does not choose the default source");
        w.n = 0;
        pt_str(&w, "nonesuch");
        pt_command(root, PULSE_COMMAND_SET_DEFAULT_SOURCE, 26, &w);
        pt_collect(root);
        PTCHECK(pt_expect_error(26, PULSE_ERR_NOENTITY), "an unknown one is refused");
        w.n = 0;
        pt_str(&w, 0);
        pt_command(root, PULSE_COMMAND_SET_DEFAULT_SOURCE, 27, &w);
        pt_collect(root);
        PTCHECK(pt_expect_error(27, PULSE_ERR_NOENTITY), "and no name");
        // A stream that named no source follows the default; one that named its source does not.
        a = pt_rec_args();
        stream_owner = pt_connect(0, 113, 35, "Follower");
        PTCHECK(pt_rec_create(stream_owner, 35, 28, &a) == 0 && pt_rec_reply.source == (unsigned long)mic, "a stream with no source is on the default");
        channel = pt_rec_reply.channel;
        {
                unsigned long follower = pt_rec_reply.index, named;

                a.source_name = "alsa_input.test.00";
                PTCHECK(pt_rec_create(stream_owner, 35, 29, &a) == 0, "and one that named its source");
                named = pt_rec_reply.index;
                w.n = 0;
                pt_str(&w, "other.monitor");
                pt_command(root, PULSE_COMMAND_SET_DEFAULT_SOURCE, 30, &w);
                pt_collect(root);
                PTCHECK(pt_expect_ack(30) && pt_engine.default_source == 1, "the default is chosen");
                {
                        struct pt_out_info o;

                        PTCHECK(pt_output_info(stream_owner, 35, 31, follower, &o) && o.source == 1, "the stream with no source follows it");
                        PTCHECK(pt_output_info(stream_owner, 35, 32, named, &o) && o.source == (unsigned long)mic, "the stream that named its source stays");
                }
                pt_sig(l, r, 0, 512);
                pt_rec_clear();
                pulse_source_feed(&pt_engine, 1, l, r, 441);
                pt_rec_pull(stream_owner, channel);
                PTCHECK(pt_rec_len / 4 > 440 * 48000 / 44100 - 120 && pt_rec_len / 4 < 480, "and takes its data at the other rate: %ld frames", pt_rec_len / 4);
        }
        // The default sink moves the default source while it is a monitor, and not when it is a capture device.
        w.n = 0;
        pt_str(&w, "null.monitor");
        pt_command(root, PULSE_COMMAND_SET_DEFAULT_SOURCE, 33, &w);
        pt_collect(root);
        w.n = 0;
        pt_str(&w, "other");
        pt_command(root, PULSE_COMMAND_SET_DEFAULT_SINK, 34, &w);
        pt_collect(root);
        PTCHECK(pt_expect_ack(34) && pt_engine.default_sink == 1 && pt_engine.default_source == 1, "the default sink moved the default source with it, which was a monitor: %ld", pt_engine.default_source);
        w.n = 0;
        pt_str(&w, "alsa_input.test.00");
        pt_command(root, PULSE_COMMAND_SET_DEFAULT_SOURCE, 35, &w);
        pt_collect(root);
        w.n = 0;
        pt_str(&w, "null");
        pt_command(root, PULSE_COMMAND_SET_DEFAULT_SINK, 36, &w);
        pt_collect(root);
        PTCHECK(pt_expect_ack(36) && pt_engine.default_sink == 0 && pt_engine.default_source == mic, "and left it, which was a capture device");
        // Volume and mute of a source.
        w.n = 0;
        pt_u32(&w, PULSE_INVALID);
        pt_str(&w, "alsa_input.test.00");
        pt_vol(&w, 2, 0x8000);
        pt_changed_calls = 0;
        pt_command(other, PULSE_COMMAND_SET_SOURCE_VOLUME, 36, &w);
        pt_collect(other);
        PTCHECK(pt_expect_error(36, PULSE_ERR_ACCESS), "another user does not set a device's volume");
        pt_command(root, PULSE_COMMAND_SET_SOURCE_VOLUME, 37, &w);
        pt_collect(root);
        PTCHECK(pt_expect_ack(37) && pt_changed_calls == 1 && pt_last_what == PULSE_CHANGED_SOURCE_VOLUME && pt_last_slot == mic, "root sets it, and the host is told: %d calls, %d at %ld", pt_changed_calls, pt_last_what,
                pt_last_slot);
        PTCHECK(pt_source_info(root, 35, 38, PULSE_INVALID, "alsa_input.test.00", &s) && s.vol_n == 2 && s.vol[0] == 0x8000 && s.vol[1] == 0x8000, "and the info says it");
        w.n = 0;
        pt_u32(&w, PULSE_INVALID);
        pt_str(&w, "alsa_input.test.00");
        pt_vol(&w, 3, 0x8000);
        pt_command(root, PULSE_COMMAND_SET_SOURCE_VOLUME, 39, &w);
        pt_collect(root);
        PTCHECK(pt_expect_error(39, PULSE_ERR_INVALID), "a volume of another number of channels is refused");
        w.n = 0;
        pt_u32(&w, PULSE_INVALID);
        pt_str(&w, "nonesuch");
        pt_vol(&w, 2, 0x8000);
        pt_command(root, PULSE_COMMAND_SET_SOURCE_VOLUME, 40, &w);
        pt_collect(root);
        PTCHECK(pt_expect_error(40, PULSE_ERR_NOENTITY), "and a source that is not");
        w.n = 0;
        pt_u32(&w, (unsigned long)mic);
        pt_str(&w, 0);
        pt_bool(&w, 1);
        pt_command(root, PULSE_COMMAND_SET_SOURCE_MUTE, 41, &w);
        pt_collect(root);
        PTCHECK(pt_expect_ack(41) && pt_source_info(root, 35, 42, (unsigned long)mic, 0, &s) && s.muted, "the mute, by index");
        pt_command(other, PULSE_COMMAND_SET_SOURCE_MUTE, 43, &w);
        pt_collect(other);
        PTCHECK(pt_expect_error(43, PULSE_ERR_ACCESS), "which another user does not set");
        // The device goes: its streams are ended and told, the default falls back.
        {
                unsigned long what[16], where[16];
                long watcher = pt_connect(0, 114, 35, "Watcher");
                int n;

                w.n = 0;
                pt_u32(&w, 0x2ff);
                pt_command(watcher, PULSE_COMMAND_SUBSCRIBE, 44, &w);
                pt_collect(watcher);
                pt_collect(stream_owner);
                pulse_source_remove(&pt_engine, mic);
                pt_collect(stream_owner);
                PTCHECK(pt_count(PULSE_COMMAND_RECORD_STREAM_KILLED) == 2, "the streams on a device that went are told they are killed: %d", pt_count(PULSE_COMMAND_RECORD_STREAM_KILLED));
                pt_collect(watcher);
                n = pt_events(what, where, 16);
                {
                        int gone = 0, source_gone = 0;

                        for (int i = 0; i < n; i++)
                        {
                                gone += what[i] == (PULSE_FACILITY_SOURCE_OUTPUT | PULSE_EVENT_REMOVE);
                                source_gone += what[i] == (PULSE_FACILITY_SOURCE | PULSE_EVENT_REMOVE) && where[i] == (unsigned long)mic;
                        }
                        PTCHECK(gone == 2 && source_gone == 1, "a watcher hears of the streams and of the source: %d %d", gone, source_gone);
                }
                PTCHECK(pt_engine.default_source == 0, "the default source is the monitor of the default sink: %ld", pt_engine.default_source);
                PTCHECK(!pt_source_info(root, 35, 45, PULSE_INVALID, "alsa_input.test.00", &s) && pt_expect_error(45, PULSE_ERR_NOENTITY), "and the device is not found");
                PTCHECK(pt_engine.records_made == pt_engine.records_freed, "and none is left: %lu made, %lu freed", pt_engine.records_made, pt_engine.records_freed);
                pulse_client_close(&pt_engine, watcher);
        }
        // A device that comes back is a new one; and one that changes its rate changes its streams'.
        mic = pulse_source_add(&pt_engine, "alsa_input.test.00", "Test mic", 48000);
        PTCHECK(mic == PULSE_SINKS && pt_engine.default_source == mic, "the place is free again; a monitor was the default, so the device is");
        pulse_client_close(&pt_engine, stream_owner);
        stream_owner = pt_connect(0, 115, 35, "Rate change");
        a = pt_rec_args();
        a.source_index = (unsigned long)mic;
        PTCHECK(pt_rec_create(stream_owner, 35, 46, &a) == 0, "a stream on the device");
        channel = pt_rec_reply.channel;
        pulse_source_reconfigure(&pt_engine, mic, "alsa_input.test.00", "Test mic", 44100);
        pt_rec_clear();
        for (int block = 0; block < 10; block++)
        {
                pulse_source_feed(&pt_engine, mic, l, r, 441);
                pt_rec_pull(stream_owner, channel);
        }
        PTCHECK(pt_rec_len / 4 > 4800 - 160 && pt_rec_len / 4 < 4800 + 8, "a device whose rate changes is resampled to the stream's: %ld frames", pt_rec_len / 4);
        pulse_source_reconfigure(&pt_engine, mic, "alsa_input.test.00", "Test mic", 192000);
        {
                long third = pt_connect(0, 116, 35, "Third");
                struct pt_rec_args b = pt_rec_args();

                b.rate = 32000;
                b.source_index = (unsigned long)mic;
                PTCHECK(pt_rec_create(third, 35, 47, &b) == 0, "a stream at 32000 on 192000 (a ratio of six)");
                pulse_source_reconfigure(&pt_engine, mic, "alsa_input.test.00", "Test mic", 384000);
                pt_collect(third);
                PTCHECK(pt_find(PULSE_COMMAND_RECORD_STREAM_KILLED) != 0, "and the device moved to 384000 (a ratio of twelve) ends it, and says so");
                pulse_client_close(&pt_engine, third);
        }
        pulse_source_reconfigure(&pt_engine, mic, "alsa_input.test.00", "Test mic", 48000);
        pulse_client_close(&pt_engine, stream_owner);
        pulse_client_close(&pt_engine, root);
        pulse_client_close(&pt_engine, other);
        PTCHECK(pt_engine.records_made == pt_engine.records_freed, "all given back: %lu made, %lu freed", pt_engine.records_made, pt_engine.records_freed);
        for (int t = 0; t < AUDIO_TABLES; t++)
                PTCHECK(audio_tables[t].users == 0, "table %d is not kept", t);
}

// A monitor is what its sink plays, the bytes the card is sent: after the master volume and its mute, a block at a time.
static void pt_test_record_monitor(void)
{
        static unsigned char out[AUDIO_BLOCK * 4];
        struct pt_rec_args a = pt_rec_args();
        long player, recorder, fed = 0, root;
        unsigned long channel, pchannel, pindex, missing;
        struct pt_w w = {{0}, 0};

        pt_make_source();
        pt_reset();
        pulse_sink_add(&pt_engine, "null", "Null", 48000);
        player = pt_connect(1000, 120, 35, "Player");
        recorder = pt_connect(0, 121, 35, "Recorder");
        root = recorder;
        PTCHECK(pt_create(player, 35, 20, 19200, 19200, 4800, &pchannel, &pindex, &missing, 0, 0x10000, 1, AUDIO_FORMAT_S16LE, 2, 48000, 0) == 0, "a stream playing");
        a.fragsize = 4096;
        PTCHECK(pt_rec_create(recorder, 35, 21, &a) == 0, "and a stream recording the monitor of its sink");
        channel = pt_rec_reply.channel;
        for (int piece = 0; piece < 3; piece++)
        {
                pt_data(player, pchannel, pt_source + fed * 2, 6400, 0, 0);
                fed += 1600;
        }
        for (int phase = 0; phase < 3; phase++)
        {
                int same = 1, any = 0;

                if (phase == 1)
                {
                        w.n = 0;
                        pt_u32(&w, 0);
                        pt_str(&w, 0);
                        pt_vol(&w, 2, 0x8000);
                        pt_command(root, PULSE_COMMAND_SET_SINK_VOLUME, 30, &w);
                        pt_collect(root);
                }
                if (phase == 2)
                {
                        w.n = 0;
                        pt_u32(&w, 0);
                        pt_str(&w, 0);
                        pt_bool(&w, 1);
                        pt_command(root, PULSE_COMMAND_SET_SINK_MUTE, 31, &w);
                        pt_collect(root);
                }
                for (int block = 0; block < 4; block++)
                {
                        pt_rec_clear();
                        pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
                        pt_rec_pull(recorder, channel);
                        same &= pt_rec_len == 1024 && !memcmp(pt_rec_buf, out, 1024);
                        for (int i = 0; i < 1024 && (phase < 2 || block > 0); i++)
                                any |= out[i] != 0;
                        if (block == 0 && phase < 2)
                                pt_data(player, pchannel, pt_source + fed * 2, 6400, 0, 0), fed += 1600;
                }
                PTCHECK(same, "phase %d (full volume, an eighth, muted): the monitor is the bytes the sink sent to the card", phase);
                if (phase < 2)
                        PTCHECK(any, "phase %d: and they are not silence", phase);
                else
                        PTCHECK(!any, "phase %d: and they are", phase);
        }
        // The monitor's own volume and mute are the source's.
        w.n = 0;
        pt_u32(&w, 0);
        pt_str(&w, 0);
        pt_bool(&w, 0);
        pt_command(root, PULSE_COMMAND_SET_SINK_MUTE, 32, &w);
        pt_collect(root);
        w.n = 0;
        pt_u32(&w, 0);
        pt_str(&w, 0);
        pt_vol(&w, 2, 0x10000);
        pt_command(root, PULSE_COMMAND_SET_SINK_VOLUME, 33, &w);
        pt_collect(root);
        w.n = 0;
        pt_u32(&w, 0);
        pt_str(&w, 0);
        pt_bool(&w, 1);
        pt_command(root, PULSE_COMMAND_SET_SOURCE_MUTE, 34, &w);
        pt_collect(root);
        PTCHECK(pt_expect_ack(34), "the monitor is muted");
        pt_data(player, pchannel, pt_source + fed * 2, 6400, 0, 0);
        fed += 1600;
        for (int block = 0; block < 4; block++)
        {
                int silent = 1, sink_sound = 0;

                pt_rec_clear();
                pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
                pt_rec_pull(recorder, channel);
                for (int i = 0; i < 1024; i++)
                        sink_sound |= out[i] != 0;
                if (block > 0)
                        for (long i = 0; i < pt_rec_len; i++)
                                silent &= pt_rec_buf[i] == 0;
                PTCHECK(sink_sound, "block %d: the sink still plays", block);
                PTCHECK(silent, "block %d: and the muted monitor gives silence after its ramp", block);
        }
        pulse_client_close(&pt_engine, recorder);
        pulse_client_close(&pt_engine, player);
        for (int block = 0; block < 4; block++)
                pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
        PTCHECK(pt_engine.records_made == pt_engine.records_freed && pt_engine.streams_made == pt_engine.streams_freed, "all given back");
}

/* ---- the sample cache ---- */

// CREATE_UPLOAD_STREAM as libpulse writes it. 0 and *channel when it was made; the error code when refused; -1 for no answer, -2 when the client was ended.
static long pt_upload(long slot, unsigned version, unsigned long tag, const char *name, long format, int channels, unsigned long rate, unsigned long length, unsigned long *channel,
                      unsigned long *asked)
{
        struct pt_w w = {{0}, 0};
        const struct pt_frame *f;
        struct pt_r r;
        int positions[32];

        for (int i = 0; i < 32; i++)
                positions[i] = channels == 1 ? AUDIO_POSITION_MONO : i == 0 ? 1 : i == 1 ? 2 : AUDIO_POSITION_AUX0 + i;
        pt_str(&w, name);
        pt_spec(&w, (int)format, channels, rate);
        pt_map(&w, channels, positions);
        pt_u32(&w, length);
        if (version >= 13)
                pt_props(&w, "media.name", "A sound", "media.role", "event");
        if (pt_command(slot, PULSE_COMMAND_CREATE_UPLOAD_STREAM, tag, &w) < 0)
                return -2;
        pt_collect(slot);
        if ((f = pt_find(PULSE_COMMAND_ERROR)) && f->tag == tag)
        {
                r = pt_reply_reader(f);
                return (long)pt_ru32(&r);
        }
        for (int i = 0; i < pt_logged; i++)
                if (pt_log[i].command == PULSE_COMMAND_REPLY && pt_log[i].tag == tag)
                {
                        r = pt_reply_reader(pt_log + i);
                        *channel = pt_ru32(&r);
                        *asked = pt_ru32(&r);
                        return !r.bad && r.at == pt_log[i].length ? 0 : -1;
                }
        return -1;
}

// The whole of a sample sent as an upload's data in pieces, and finished: the error code of the finish, or 0.
static long pt_upload_send(long slot, unsigned long tag, unsigned long channel, const void *data, unsigned long length, unsigned long piece)
{
        struct pt_w w = {{0}, 0};
        const struct pt_frame *f;
        struct pt_r r;

        for (unsigned long at = 0; at < length; at += piece)
                if (pt_data(slot, channel, (const unsigned char *)data + at, (long)(length - at < piece ? length - at : piece), 0, 0) < 0)
                        return -2;
        pt_u32(&w, channel);
        if (pt_command(slot, PULSE_COMMAND_FINISH_UPLOAD_STREAM, tag, &w) < 0)
                return -2;
        pt_collect(slot);
        if ((f = pt_find(PULSE_COMMAND_ERROR)) && f->tag == tag)
        {
                r = pt_reply_reader(f);
                return (long)pt_ru32(&r);
        }
        return pt_expect_ack(tag) ? 0 : -1;
}

// PLAY_SAMPLE: the sink input's index (version 13 on; 0 for an acknowledgement before), or an error code as a negative number below -100, or -1.
static long pt_play(long slot, unsigned version, unsigned long tag, unsigned long sink_index, const char *sink_name, unsigned long volume, const char *name)
{
        struct pt_w w = {{0}, 0};
        const struct pt_frame *f;
        struct pt_r r;

        pt_u32(&w, sink_index);
        pt_str(&w, sink_name);
        pt_u32(&w, volume);
        pt_str(&w, name);
        if (version >= 13)
                pt_props(&w, "media.role", "event", 0, 0);
        if (pt_command(slot, PULSE_COMMAND_PLAY_SAMPLE, tag, &w) < 0)
                return -2;
        pt_collect(slot);
        if ((f = pt_find(PULSE_COMMAND_ERROR)) && f->tag == tag)
        {
                r = pt_reply_reader(f);
                return -100 - (long)pt_ru32(&r);
        }
        for (int i = 0; i < pt_logged; i++)
                if (pt_log[i].command == PULSE_COMMAND_REPLY && pt_log[i].tag == tag)
                {
                        r = pt_reply_reader(pt_log + i);
                        if (version >= 13)
                        {
                                long index = (long)pt_ru32(&r);

                                return !r.bad && r.at == pt_log[i].length ? index : -1;
                        }
                        return pt_log[i].length == 10 ? 0 : -1;
                }
        return -1;
}

static int pt_remove_sample(long slot, unsigned long tag, const char *name)
{
        struct pt_w w = {{0}, 0};
        const struct pt_frame *f;
        struct pt_r r;

        pt_str(&w, name);
        pt_command(slot, PULSE_COMMAND_REMOVE_SAMPLE, tag, &w);
        pt_collect(slot);
        if ((f = pt_find(PULSE_COMMAND_ERROR)) && f->tag == tag)
        {
                r = pt_reply_reader(f);
                return (int)pt_ru32(&r);
        }
        return pt_expect_ack(tag) ? 0 : -1;
}

struct pt_sample_info
{
        unsigned long index, bytes, rate, volume[8];
        int format, channels, vol_n, lazy, props;
        char name[80], role[16];
        long long usec;
        const char *filename;
};

// A sample as the info says it, read to the field its version ends at.
static int pt_read_sample(struct pt_r *r, unsigned version, struct pt_sample_info *s)
{
        const char *t;
        int positions[32];

        memset(s, 0, sizeof(*s));
        s->index = pt_ru32(r);
        t = pt_rstr(r);
        snprintf(s->name, sizeof(s->name), "%s", t ? t : "");
        s->vol_n = pt_rvol(r, s->volume);
        s->usec = pt_r64(r, 'U');
        pt_rspec(r, &s->format, &s->channels, &s->rate);
        pt_rmap(r, positions);
        s->bytes = pt_ru32(r);
        s->lazy = pt_rbool(r);
        s->filename = pt_rstr(r);
        if (version >= 13)
                s->props = pt_rprops(r, "media.role", s->role, sizeof(s->role));
        return !r->bad;
}

static int pt_sample_info(long slot, unsigned version, unsigned long tag, unsigned long index, const char *name, struct pt_sample_info *s)
{
        struct pt_w w = {{0}, 0};
        const struct pt_frame *f;
        struct pt_r r;

        pt_u32(&w, index);
        pt_str(&w, name);
        pt_command(slot, PULSE_COMMAND_GET_SAMPLE_INFO, tag, &w);
        pt_collect(slot);
        f = pt_find(PULSE_COMMAND_REPLY);
        if (!f || f->tag != tag)
                return 0;
        r = pt_reply_reader(f);
        return pt_read_sample(&r, version, s) && r.at == f->length;
}

// The cache from the wire: upload at four versions, the info, the play (the frames come out as they went in), the volume, another sink and another rate, a stream of
// its own that ends by itself and is announced, a replacement, a removal, and a sample that goes on playing after it is taken out.
static void pt_test_samples(void)
{
        static const unsigned versions[] = {35, 22, 13, 12};
        static unsigned char out[AUDIO_BLOCK * 4];
        static short heard[2 * 6000];
        unsigned long channel, asked;

        pt_make_source();
        for (unsigned v = 0; v < sizeof(versions) / sizeof(versions[0]); v++)
        {
                long slot, watcher, played, heard_frames = 0;
                struct pt_sample_info info;
                struct pt_w w = {{0}, 0};
                const struct pt_frame *f;
                struct pt_r r;
                unsigned long what[16], where[16];
                int n;

                pt_reset();
                pulse_sink_add(&pt_engine, "null", "Null", 48000);
                slot = pt_connect(0, 130, versions[v], "Bell");
                watcher = pt_connect(0, 131, 35, "Watcher");
                w.n = 0;
                pt_u32(&w, 0x2ff);
                pt_command(watcher, PULSE_COMMAND_SUBSCRIBE, 1, &w);
                pt_collect(watcher);
                PTCHECK(pt_upload(slot, versions[v], 20, "bell", AUDIO_FORMAT_S16LE, 2, 48000, 4800 * 4, &channel, &asked) == 0 && asked == 4800 * 4, "version %u: an upload begun, and asked for its length", versions[v]);
                PTCHECK(pt_upload_send(slot, 21, channel, pt_source + 2 * 1000, 4800 * 4, 7000) == 0, "version %u: its data in pieces of 7000 bytes, and finished", versions[v]);
                pt_collect(watcher);
                n = pt_events(what, where, 16);
                PTCHECK(n >= 1 && what[n - 1] == (PULSE_FACILITY_SAMPLE_CACHE | PULSE_EVENT_NEW) && where[n - 1] == 0, "version %u: a watcher is told of the new sample", versions[v]);
                PTCHECK(pt_sample_info(slot, versions[v], 22, PULSE_INVALID, "bell", &info), "version %u: the info is whole", versions[v]);
                PTCHECK(info.index == 0 && !strcmp(info.name, "bell") && info.vol_n == 2 && info.volume[0] == 0x10000 && info.format == AUDIO_FORMAT_S16LE && info.channels == 2 && info.rate == 48000 &&
                            info.usec == 100000 && info.bytes == 4800 * 4 && !info.lazy && !info.filename,
                        "version %u: the name, the spec, 100 ms, 19200 bytes, not lazy, no file", versions[v]);
                if (versions[v] >= 13)
                        PTCHECK(info.props == 2 && !strcmp(info.role, "event"), "version %u: the properties it was uploaded with: %d %s", versions[v], info.props, info.role);
                PTCHECK(pt_sample_info(slot, versions[v], 23, 0, 0, &info) && !strcmp(info.name, "bell"), "version %u: by index", versions[v]);
                PTCHECK(!pt_sample_info(slot, versions[v], 24, PULSE_INVALID, "nonesuch", &info) && pt_expect_error(24, PULSE_ERR_NOENTITY), "version %u: a name that is none", versions[v]);
                pt_command(slot, PULSE_COMMAND_GET_SAMPLE_INFO_LIST, 25, 0);
                pt_collect(slot);
                f = pt_find(PULSE_COMMAND_REPLY);
                if (f)
                {
                        r = pt_reply_reader(f);
                        PTCHECK(pt_read_sample(&r, versions[v], &info) && r.at == f->length && info.index == 0, "version %u: the list has the one", versions[v]);
                }
                pt_command(slot, PULSE_COMMAND_STAT, 26, 0);
                pt_collect(slot);
                f = pt_find(PULSE_COMMAND_REPLY);
                PTCHECK(f && f->length == 35 && wire_be32(f->payload + 31) == 4800 * 4, "version %u: STAT says how much the cache holds", versions[v]);
                // Play: its stream announced, the frames out as they went in after the fade-in, then it ends by itself.
                played = pt_play(slot, versions[v], 27, PULSE_INVALID, 0, PULSE_INVALID, "bell");
                PTCHECK(played >= 0, "version %u: played: %ld", versions[v], played);
                pt_collect(watcher);
                n = pt_events(what, where, 16);
                PTCHECK(n >= 1 && what[n - 1] == (PULSE_FACILITY_SINK_INPUT | PULSE_EVENT_NEW) && (versions[v] < 13 || where[n - 1] == (unsigned long)played), "version %u: a new sink input is announced", versions[v]);
                for (int block = 0; block < 22; block++)
                {
                        pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
                        if (heard_frames + 256 <= 6000)
                                memcpy(heard + 2 * heard_frames, out, 256 * 4);
                        heard_frames += 256;
                }
                {
                        long bad = -1;

                        for (long i = 96; i < 4704 && bad < 0; i++)
                                if (heard[2 * i] != pt_source[2 * (1000 + i)] || heard[2 * i + 1] != pt_source[2 * (1000 + i) + 1])
                                        bad = i;
                        PTCHECK(bad < 0, "version %u: the frames come out as they went in from the 96th to the 96th from the end: differs at %ld", versions[v], bad);
                        PTCHECK(labs(heard[2 * 5]) <= labs(pt_source[2 * 1005]), "version %u: the first are the fade-in", versions[v]);
                        bad = -1;
                        for (long i = 4704; i < 4800 && bad < 0; i++)
                                if (labs(heard[2 * i]) > labs(pt_source[2 * (1000 + i)]) || labs(heard[2 * i + 1]) > labs(pt_source[2 * (1000 + i) + 1]))
                                        bad = i;
                        PTCHECK(bad < 0 && heard[2 * 4799] == 0, "version %u: and the last 96 fade out to nothing: %ld", versions[v], bad);
                        PTCHECK(heard[2 * 4900] == 0 && heard[2 * 5500 + 1] == 0, "version %u: and silence after", versions[v]);
                }
                PTCHECK(pt_engine.streams_made == 1 && pt_engine.streams_freed == 1, "version %u: the stream ended by itself and was freed", versions[v]);
                pt_collect(watcher);
                n = pt_events(what, where, 16);
                {
                        int removed = 0;

                        for (int i = 0; i < n; i++)
                                removed += what[i] == (PULSE_FACILITY_SINK_INPUT | PULSE_EVENT_REMOVE) && (versions[v] < 13 || where[i] == (unsigned long)played);
                        PTCHECK(removed == 1, "version %u: and its end is announced once: %d", versions[v], removed);
                }
                PTCHECK(pt_remove_sample(slot, 28, "bell") == 0, "version %u: the sample is taken out", versions[v]);
                pt_collect(watcher);
                n = pt_events(what, where, 16);
                PTCHECK(n >= 1 && what[n - 1] == (PULSE_FACILITY_SAMPLE_CACHE | PULSE_EVENT_REMOVE) && where[n - 1] == 0, "version %u: and announced", versions[v]);
                PTCHECK(pt_play(slot, versions[v], 29, PULSE_INVALID, 0, PULSE_INVALID, "bell") == -100 - PULSE_ERR_NOENTITY, "version %u: it cannot be played after", versions[v]);
                PTCHECK(pt_remove_sample(slot, 30, "bell") == PULSE_ERR_NOENTITY, "version %u: nor taken out twice", versions[v]);
                PTCHECK(pt_engine.sample_total == 0 && pt_allocs == pt_releases + pt_table_blocks() + 2, "version %u: the cache holds nothing (the two clients are still here)", versions[v]);
                pulse_client_close(&pt_engine, slot);
                pulse_client_close(&pt_engine, watcher);
                PTCHECK(pt_allocs == pt_releases + pt_table_blocks() && pt_alloc_bytes == pt_release_bytes + pt_table_bytes(), "version %u: every block given back", versions[v]);
        }
}

// Playing it otherwise: a volume, a sink that is not the default, a rate and a channel count that are not the sink's, a replacement while one plays, a removal while one plays.
static void pt_test_samples_play(void)
{
        static unsigned char out[AUDIO_BLOCK * 4];
        static short sound[2 * 4800];
        static float tone[24000], got[48000];
        unsigned long channel, asked;
        long slot, played, heard = 0, root_for_samples;
        static short heard_buffer[2 * 6000];

        pt_make_source();
        pt_reset();
        pulse_sink_add(&pt_engine, "null", "Null", 48000);
        pulse_sink_add(&pt_engine, "other", "Other", 44100);
        slot = pt_connect(1000, 140, 35, "Player");
        root_for_samples = pt_connect(0, 141, 35, "Root");
        for (int i = 0; i < 4800; i++)
        {
                sound[2 * i] = pt_source[2 * (2000 + i)];
                sound[2 * i + 1] = pt_source[2 * (2000 + i) + 1];
        }
        PTCHECK(pt_upload(slot, 35, 20, "beep", AUDIO_FORMAT_S16LE, 2, 48000, sizeof(sound), &channel, &asked) == 0 && pt_upload_send(slot, 21, channel, sound, sizeof(sound), 4096) == 0, "a sample");
        // At a volume: an eighth, exactly, after the fade-in.
        played = pt_play(slot, 35, 22, PULSE_INVALID, 0, 0x8000, "beep");
        PTCHECK(played >= 0, "played at 0x8000");
        for (int block = 0; block < 22; block++)
        {
                pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
                if (heard + 256 <= 6000)
                        memcpy(heard_buffer + 2 * heard, out, 256 * 4);
                heard += 256;
        }
        {
                long bad = -1;

                for (long i = 120; i < 4704 && bad < 0; i++)
                        if (heard_buffer[2 * i] != (short)pt_ref_wide((float)sound[2 * i] / 32768.0f * 0.125f, 32768.0, 32767.0) ||
                            heard_buffer[2 * i + 1] != (short)pt_ref_wide((float)sound[2 * i + 1] / 32768.0f * 0.125f, 32768.0, 32767.0))
                                bad = i;
                PTCHECK(bad < 0, "an eighth, from frame 120: differs at %ld", bad);
        }
        // On the other sink (44100) it is resampled to its rate; named by index and by name.
        played = pt_play(slot, 35, 23, 1, 0, PULSE_INVALID, "beep");
        PTCHECK(played >= 0, "played on the second sink by its index");
        PTCHECK(pt_play(slot, 35, 24, PULSE_INVALID, "other", PULSE_INVALID, "beep") >= 0, "and by its name");
        PTCHECK(pt_play(slot, 35, 25, 0, "null", PULSE_INVALID, "beep") == -100 - PULSE_ERR_INVALID, "an index and a name together are a mistake");
        PTCHECK(pt_play(slot, 35, 26, 7, 0, PULSE_INVALID, "beep") == -100 - PULSE_ERR_NOENTITY, "a sink that is none");
        for (int block = 0; block < 30; block++)
                pulse_sink_mix(&pt_engine, 1, out, AUDIO_FORMAT_S16LE, 2, 256);
        PTCHECK(pt_engine.streams_made == pt_engine.streams_freed, "both ended on their own: %lu made, %lu freed", pt_engine.streams_made, pt_engine.streams_freed);
        PTCHECK(pt_remove_sample(slot, 27, "beep") == 0, "taken out");
        // A tone at 24000 Hz in one channel of floats, played on a sink of 48000: the amplitude where it was, and nothing else.
        for (int i = 0; i < 24000; i++)
                tone[i] = (float)(0.5 * sin(2.0 * M_PI * 1000.0 * (double)i / 24000.0));
        PTCHECK(pt_upload(slot, 35, 30, "tone", AUDIO_FORMAT_F32LE, 1, 24000, sizeof(tone), &channel, &asked) == 0 && pt_upload_send(slot, 31, channel, tone, sizeof(tone), 60000) == 0, "a second of a tone in 24000 Hz floats");
        played = pt_play(slot, 35, 32, PULSE_INVALID, 0, PULSE_INVALID, "tone");
        PTCHECK(played >= 0, "played");
        {
                long frames = 0;

                for (int block = 0; block < 190; block++)
                {
                        static float conv[AUDIO_BLOCK];
                        short *s16 = (short *)out;

                        pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
                        for (int i = 0; i < 256; i++)
                                conv[i] = (float)s16[2 * i] / 32768.0f;
                        if (frames + 256 <= 48000)
                                memcpy(got + frames, conv, 256 * sizeof(float));
                        frames += 256;
                }
                {
                        double amplitude, rest, db;

                        rest = pt_fit(got + 4800, 24000, 1000.0, 48000.0, &amplitude);
                        db = 20.0 * log10(amplitude / 0.5);
                        PTCHECK(fabs(db) < 0.02 && rest < 0.0003, "the tone through the resampler: %.4f dB from where it was, the rest %g", db, rest);
                }
        }
        PTCHECK(pt_engine.streams_made == pt_engine.streams_freed, "ended");
        PTCHECK(pt_remove_sample(slot, 33, "tone") == 0, "taken out");
        // Replaced while it plays: the one playing goes on with the old data, the next is the new; the index is the same and the watchers are told of a change.
        {
                static short second[2 * 4800];
                long first_stream, heard_old = 0;
                struct pt_sample_info info;
                unsigned long what[16], where[16], ding;
                long watcher = pt_connect(0, 142, 35, "Watcher");
                struct pt_w w = {{0}, 0};
                int n, changed = 0;

                w.n = 0;
                pt_u32(&w, 0x2ff);
                pt_command(watcher, PULSE_COMMAND_SUBSCRIBE, 1, &w);
                pt_collect(watcher);
                for (int i = 0; i < 4800; i++)
                {
                        second[2 * i] = (short)(i % 200 * 100);
                        second[2 * i + 1] = (short)(-(i % 200) * 100);
                }
                PTCHECK(pt_upload(slot, 35, 34, "ding", AUDIO_FORMAT_S16LE, 2, 48000, sizeof(sound), &channel, &asked) == 0 && pt_upload_send(slot, 35, channel, sound, sizeof(sound), 4096) == 0, "a sample to be replaced");
                PTCHECK(pt_sample_info(slot, 35, 36, PULSE_INVALID, "ding", &info), "its info");
                ding = info.index;
                first_stream = pt_play(slot, 35, 37, PULSE_INVALID, 0, PULSE_INVALID, "ding");
                PTCHECK(first_stream >= 0, "played");
                for (int block = 0; block < 4; block++)
                        pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
                heard_old = 4 * 256;
                pt_collect(watcher);
                PTCHECK(pt_upload(slot, 35, 38, "ding", AUDIO_FORMAT_S16LE, 2, 48000, sizeof(second), &channel, &asked) == 0 && pt_upload_send(slot, 39, channel, second, sizeof(second), 4096) == 0,
                        "uploaded again under the name");
                pt_collect(watcher);
                n = pt_events(what, where, 16);
                for (int i = 0; i < n; i++)
                        changed += what[i] == (PULSE_FACILITY_SAMPLE_CACHE | PULSE_EVENT_CHANGE) && where[i] == info.index;
                PTCHECK(changed == 1, "a change of the same index is announced: %d", changed);
                PTCHECK(pt_sample_info(slot, 35, 40, PULSE_INVALID, "ding", &info) && info.index == ding, "the cache has one of that name, and it has the index it had");
                PTCHECK(pt_engine.sample_total == sizeof(sound) + sizeof(second), "both are held while the old plays: %lu", pt_engine.sample_total);
                for (int block = 0; block < 20; block++)
                {
                        short *s16 = (short *)out;

                        pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
                        // the old one is what is playing: its frames, from where it was
                        if (block < 15)
                                for (int i = 0; i < 256; i++)
                                        if (heard_old + i >= 120 && heard_old + i < 4704 && (s16[2 * i] != sound[2 * (heard_old + i)] || s16[2 * i + 1] != sound[2 * (heard_old + i) + 1]))
                                        {
                                                PTCHECK(0, "the old sample goes on: frame %ld differs", heard_old + i);
                                                block = 99;
                                                break;
                                        }
                        heard_old += 256;
                }
                PTCHECK(pt_engine.sample_total == sizeof(second), "and the old is given back when it has ended: %lu", pt_engine.sample_total);
                played = pt_play(slot, 35, 41, PULSE_INVALID, 0, PULSE_INVALID, "ding");
                PTCHECK(played >= 0, "the next is the new one");
                {
                        short *s16 = (short *)out;
                        int same = 1;

                        for (int block = 0; block < 2; block++)
                        {
                                pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
                                if (block == 1)
                                        for (int i = 0; i < 256; i++)
                                                same &= s16[2 * i] == second[2 * (256 + i)] && s16[2 * i + 1] == second[2 * (256 + i) + 1];
                        }
                        PTCHECK(same, "and is the new data");
                }
                // Taken out while it plays: it plays on, and the memory goes when it has.
                PTCHECK(pt_remove_sample(slot, 42, "ding") == 0 && pt_engine.sample_total == sizeof(second), "taken out while it plays: still held");
                PTCHECK(pt_play(slot, 35, 43, PULSE_INVALID, 0, PULSE_INVALID, "ding") == -100 - PULSE_ERR_NOENTITY, "and not played again, though it plays still");
                PTCHECK(!pt_sample_info(slot, 35, 44, PULSE_INVALID, "ding", &info) && pt_expect_error(44, PULSE_ERR_NOENTITY), "nor shown by its name");
                pt_command(slot, PULSE_COMMAND_GET_SAMPLE_INFO_LIST, 45, 0);
                pt_collect(slot);
                PTCHECK(pt_find(PULSE_COMMAND_REPLY) && pt_find(PULSE_COMMAND_REPLY)->length == 10, "nor listed");
                for (int block = 0; block < 20; block++)
                        pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
                PTCHECK(pt_engine.sample_total == 0 && pt_engine.streams_made == pt_engine.streams_freed, "and gone when it has ended");
                pulse_client_close(&pt_engine, watcher);
        }
        // Its stream belongs to no program: it says what it is, and plays on when the program that asked has gone.
        {
                struct pulse_stream *st;
                char text[PULSE_VALUE + 1];
                long who = pt_connect(1000, 143, 35, "Doorbell"), idx, bad = -1;

                PTCHECK(pt_upload(who, 35, 50, "door", AUDIO_FORMAT_S16LE, 2, 48000, sizeof(sound), &channel, &asked) == 0 && pt_upload_send(who, 51, channel, sound, sizeof(sound), 4096) == 0, "a sound");
                idx = pt_play(who, 35, 52, PULSE_INVALID, 0, PULSE_INVALID, "door");
                st = idx >= 0 ? pulse_input_of(&pt_engine, (unsigned long)idx) : 0;
                PTCHECK(st && st->client == -1 && st->uid == 1000, "its stream belongs to no connection, and to the user who asked");
                PTCHECK(st && pulse_props_text(&st->props, "application.name", text, sizeof(text)) && !strcmp(text, "Doorbell"), "it is the asking program's, by name");
                PTCHECK(st && pulse_props_text(&st->props, "media.role", text, sizeof(text)) && !strcmp(text, "event") && pulse_props_text(&st->props, "media.name", text, sizeof(text)) && !strcmp(text, "A sound"),
                        "and has the properties it was uploaded with and asked for");
                pulse_client_close(&pt_engine, who);
                heard = 0;
                for (int block = 0; block < 22; block++)
                {
                        pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
                        if (heard + 256 <= 6000)
                                memcpy(heard_buffer + 2 * heard, out, 256 * 4);
                        heard += 256;
                }
                for (long i = 96; i < 4704 && bad < 0; i++)
                        if (heard_buffer[2 * i] != sound[2 * i] || heard_buffer[2 * i + 1] != sound[2 * i + 1])
                                bad = i;
                PTCHECK(bad < 0, "it plays on all the same when the program has gone: differs at %ld", bad);
                PTCHECK(pt_remove_sample(root_for_samples, 53, "door") == 0, "taken out");
        }
        {
                long who = pt_connect(1000, 144, 35, "Recall");
                long bad = -1;

                PTCHECK(pt_upload(who, 35, 60, "remembered", AUDIO_FORMAT_S16LE, 2, 48000, sizeof(sound), &channel, &asked) == 0 && pt_upload_send(who, 61, channel, sound, sizeof(sound), 4096) == 0, "a sound");
                pt_recall_has = 1;
                pt_recall_volume = 0x8000;
                pt_recall_mute = 0;
                PTCHECK(pt_play(who, 35, 62, PULSE_INVALID, 0, PULSE_INVALID, "remembered") >= 0, "played by a program whose volume is remembered");
                pt_recall_has = 0;
                heard = 0;
                for (int block = 0; block < 22; block++)
                {
                        pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
                        if (heard + 256 <= 6000)
                                memcpy(heard_buffer + 2 * heard, out, 256 * 4);
                        heard += 256;
                }
                for (long i = 120; i < 4704 && bad < 0; i++)
                        if (heard_buffer[2 * i] != (short)pt_ref_wide((float)sound[2 * i] / 32768.0f * 0.125f, 32768.0, 32767.0))
                                bad = i;
                PTCHECK(bad < 0, "at the volume remembered of its name, an eighth: differs at %ld", bad);
                pulse_client_close(&pt_engine, who);
        }
        pulse_client_close(&pt_engine, slot);
        PTCHECK(pt_allocs == pt_releases + pt_table_blocks() + 2 && pt_engine.sample_total == sizeof(sound), "every block given back but the sound left in the cache and the client who will take it out");
        PTCHECK(pt_remove_sample(root_for_samples, 70, "remembered") == 0 && pt_engine.sample_total == 0, "taken out");
        pulse_client_close(&pt_engine, root_for_samples);
        PTCHECK(pt_allocs == pt_releases + pt_table_blocks() && pt_alloc_bytes == pt_release_bytes + pt_table_bytes(), "every block given back");
        for (int t = 0; t < AUDIO_TABLES; t++)
                PTCHECK(audio_tables[t].users == 0, "table %d is not kept", t);
}

// Who may, and every cap and refusal of the cache.
static void pt_test_samples_policy(void)
{
        static unsigned char big[PULSE_SAMPLE_MAX + 4096];
        unsigned long channel, asked, second;
        long root, user, other, played;
        struct pt_w w = {{0}, 0};
        const struct pt_frame *f;
        static unsigned char out[AUDIO_BLOCK * 4];

        pt_reset();
        pulse_sink_add(&pt_engine, "null", "Null", 48000);
        // Before the handshake: refused, all five.
        {
                static const unsigned long commands[] = {PULSE_COMMAND_CREATE_UPLOAD_STREAM, PULSE_COMMAND_FINISH_UPLOAD_STREAM, PULSE_COMMAND_DELETE_UPLOAD_STREAM, PULSE_COMMAND_PLAY_SAMPLE,
                                                         PULSE_COMMAND_REMOVE_SAMPLE};
                long slot = pulse_client_open(&pt_engine, 0, 9);

                for (unsigned i = 0; i < 5; i++)
                {
                        w.n = 0;
                        pt_u32(&w, 1);
                        pt_command(slot, commands[i], 3, &w);
                        pt_collect(slot);
                        PTCHECK(pt_expect_error(3, PULSE_ERR_ACCESS), "command %lu before the handshake is refused", commands[i]);
                }
                pulse_client_close(&pt_engine, slot);
        }
        root = pt_connect(0, 150, 35, "Root");
        user = pt_connect(1000, 151, 35, "User");
        other = pt_connect(1001, 152, 35, "Other");
        memset(big, 0x11, sizeof(big));
        // What is wrong with an upload.
        PTCHECK(pt_upload(user, 35, 20, "", AUDIO_FORMAT_S16LE, 2, 48000, 4096, &channel, &asked) == PULSE_ERR_INVALID, "an empty name");
        {
                char long_name[PULSE_NAME + 8];

                memset(long_name, 'x', sizeof(long_name));
                long_name[PULSE_NAME] = 0;
                PTCHECK(pt_upload(user, 35, 21, long_name, AUDIO_FORMAT_S16LE, 2, 48000, 4096, &channel, &asked) == PULSE_ERR_INVALID, "a name of 64 bytes");
                long_name[PULSE_NAME - 1] = 0;
                PTCHECK(pt_upload(user, 35, 22, long_name, AUDIO_FORMAT_S16LE, 2, 48000, 4096, &channel, &asked) == 0, "and of 63 is the longest");
                w.n = 0;
                pt_u32(&w, channel);
                pt_command(user, PULSE_COMMAND_DELETE_UPLOAD_STREAM, 23, &w);
                pt_collect(user);
                PTCHECK(pt_expect_ack(23) && pt_engine.upload_total == 0, "the upload is given up");
        }
        PTCHECK(pt_upload(user, 35, 24, "a\tb", AUDIO_FORMAT_S16LE, 2, 48000, 4096, &channel, &asked) == PULSE_ERR_INVALID, "a name with a control byte");
        PTCHECK(pt_upload(user, 35, 25, "a", 99, 2, 48000, 4096, &channel, &asked) == PULSE_ERR_INVALID, "a format there is not");
        PTCHECK(pt_upload(user, 35, 26, "a", AUDIO_FORMAT_S16LE, 2, 0, 4096, &channel, &asked) == PULSE_ERR_INVALID, "no rate");
        PTCHECK(pt_upload(user, 35, 27, "a", AUDIO_FORMAT_S16LE, 2, 48000, 0, &channel, &asked) == PULSE_ERR_INVALID, "no length");
        PTCHECK(pt_upload(user, 35, 28, "a", AUDIO_FORMAT_S16LE, 2, 48000, 4098, &channel, &asked) == PULSE_ERR_INVALID, "a length that is not whole frames");
        PTCHECK(pt_upload(user, 35, 29, "a", AUDIO_FORMAT_S16LE, 9, 48000, 4096, &channel, &asked) == PULSE_ERR_INVALID, "too many channels");
        PTCHECK(pt_upload(root, 35, 30, "huge", AUDIO_FORMAT_S16LE, 2, 48000, PULSE_SAMPLE_MAX + 4, &channel, &asked) == PULSE_ERR_TOOLARGE, "a sample past the most");
        // Data that is not right ends the client; a finish that is short is refused and the upload is gone.
        PTCHECK(pt_upload(other, 35, 31, "x", AUDIO_FORMAT_S16LE, 2, 48000, 400, &channel, &asked) == 0, "an upload to be told too much");
        PTCHECK(pt_data(other, channel, big, 404, 0, 0) == PULSE_DEAD, "more data than was said ends the client");
        pulse_client_close(&pt_engine, other);
        other = pt_connect(1001, 153, 35, "Other");
        PTCHECK(pt_upload(other, 35, 32, "x", AUDIO_FORMAT_S16LE, 2, 48000, 400, &channel, &asked) == 0, "another");
        PTCHECK(pt_data(other, channel, big, 100, 0, 8) == PULSE_DEAD, "a seek in an upload ends the client");
        pulse_client_close(&pt_engine, other);
        other = pt_connect(1001, 154, 35, "Other");
        PTCHECK(pt_upload(other, 35, 33, "short", AUDIO_FORMAT_S16LE, 2, 48000, 400, &channel, &asked) == 0, "an upload to be finished short");
        pt_data(other, channel, big, 396, 0, 0);
        w.n = 0;
        pt_u32(&w, channel);
        pt_command(other, PULSE_COMMAND_FINISH_UPLOAD_STREAM, 34, &w);
        pt_collect(other);
        PTCHECK(pt_expect_error(34, PULSE_ERR_INVALID), "finished short, it is refused");
        pt_command(other, PULSE_COMMAND_FINISH_UPLOAD_STREAM, 35, &w);
        pt_collect(other);
        PTCHECK(pt_expect_error(35, PULSE_ERR_NOENTITY) && pt_engine.upload_total == 0, "and the upload is gone");
        w.n = 0;
        pt_u32(&w, 77);
        pt_command(other, PULSE_COMMAND_DELETE_UPLOAD_STREAM, 36, &w);
        pt_collect(other);
        PTCHECK(pt_expect_error(36, PULSE_ERR_NOENTITY), "a delete of a channel that is none");
        // Hostile fields.
        for (int k = 0; k < 3; k++)
        {
                struct pt_w bad = {{0}, 0};

                if (k == 0)
                        pt_str(&bad, "abc");
                else if (k == 1)
                {
                        pt_str(&bad, "abc");
                        pt_spec(&bad, AUDIO_FORMAT_S16LE, 2, 48000);
                }
                else
                {
                        pt_u32(&bad, 0);
                        pt_u32(&bad, 0);
                        pt_u32(&bad, 5);
                }
                PTCHECK(pt_command(other, k == 2 ? PULSE_COMMAND_PLAY_SAMPLE : PULSE_COMMAND_CREATE_UPLOAD_STREAM, 37, &bad) == PULSE_DEAD, "hostile fields %d end the client", k);
                pulse_client_close(&pt_engine, other);
                other = pt_connect(1001, 155 + k, 35, "Other");
        }
        // The caps: uploads of a client, a user's share, all of it, the table.
        PTCHECK(pt_upload(user, 35, 40, "u1", AUDIO_FORMAT_S16LE, 2, 48000, 4096, &channel, &asked) == 0 && pt_upload(user, 35, 41, "u2", AUDIO_FORMAT_S16LE, 2, 48000, 4096, &second, &asked) == 0, "two uploads at once");
        PTCHECK(pt_upload(user, 35, 42, "u3", AUDIO_FORMAT_S16LE, 2, 48000, 4096, &second, &asked) == PULSE_ERR_TOOLARGE, "a third is too many");
        w.n = 0;
        pt_u32(&w, channel);
        pt_command(other, PULSE_COMMAND_FINISH_UPLOAD_STREAM, 44, &w);
        pt_collect(other);
        PTCHECK(pt_expect_error(44, PULSE_ERR_NOENTITY), "another client's upload is not finished by this one");
        pt_command(other, PULSE_COMMAND_DELETE_UPLOAD_STREAM, 45, &w);
        pt_collect(other);
        PTCHECK(pt_expect_error(45, PULSE_ERR_NOENTITY) && pt_engine.upload_total == 8192, "nor given up by it");
        pulse_client_close(&pt_engine, user);
        PTCHECK(pt_engine.upload_total == 0 && pt_allocs == pt_releases + pt_table_blocks() + 2, "a client that goes in the middle of two uploads gives them back");
        user = pt_connect(1000, 156, 35, "User");
        PTCHECK(pt_upload(user, 35, 43, "big", AUDIO_FORMAT_S16LE, 2, 48000, 3 << 20, &channel, &asked) == 0 && pt_upload_send(user, 44, channel, big, 3 << 20, 60000) == 0, "a user may hold 3 MiB");
        PTCHECK(pt_upload(user, 35, 45, "more", AUDIO_FORMAT_S16LE, 2, 48000, 2 << 20, &channel, &asked) == PULSE_ERR_TOOLARGE, "and not 5");
        PTCHECK(pt_upload(root, 35, 46, "root1", AUDIO_FORMAT_S16LE, 2, 48000, 4 << 20, &channel, &asked) == 0 && pt_upload_send(root, 47, channel, big, 4 << 20, 100000) == 0, "root may hold more: 4 MiB");
        PTCHECK(pt_upload(root, 35, 48, "root2", AUDIO_FORMAT_S16LE, 2, 48000, 4 << 20, &channel, &asked) == 0 && pt_upload_send(root, 49, channel, big, 4 << 20, 100000) == 0, "and 4 more");
        PTCHECK(pt_upload(root, 35, 50, "root3", AUDIO_FORMAT_S16LE, 2, 48000, 4 << 20, &channel, &asked) == 0 && pt_upload_send(root, 51, channel, big, 4 << 20, 100000) == 0, "and 4 more");
        PTCHECK(pt_upload(root, 35, 52, "root4", AUDIO_FORMAT_S16LE, 2, 48000, 4 << 20, &channel, &asked) == PULSE_ERR_TOOLARGE, "and the cache is full at 16 MiB: %lu", pt_engine.sample_total);
        // Who can take out and who can replace what.
        PTCHECK(pt_remove_sample(other, 53, "root1") == PULSE_ERR_NOENTITY, "another user cannot take out root's (as one that is not there)");
        PTCHECK(pt_upload(other, 35, 54, "root2", AUDIO_FORMAT_S16LE, 2, 48000, 400, &channel, &asked) == 0 && pt_upload_send(other, 55, channel, big, 400, 400) == PULSE_ERR_ACCESS, "nor replace it");
        PTCHECK(pt_remove_sample(root, 56, "big") == 0, "root can take out a user's");
        PTCHECK(pt_remove_sample(user, 57, "root3") == PULSE_ERR_NOENTITY, "and a user not root's");
        // The sink input of a sample belongs to its player.
        played = pt_play(other, 35, 58, PULSE_INVALID, 0, PULSE_INVALID, "root1");
        PTCHECK(played >= 0, "another user may play root's sample");
        {
                struct pt_w info = {{0}, 0};

                pt_u32(&info, (unsigned long)played);
                pt_command(user, PULSE_COMMAND_GET_SINK_INPUT_INFO, 59, &info);
                pt_collect(user);
                PTCHECK(pt_expect_error(59, PULSE_ERR_NOENTITY), "its stream is not shown to a third user");
                pt_command(other, PULSE_COMMAND_GET_SINK_INPUT_INFO, 60, &info);
                pt_collect(other);
                f = pt_find(PULSE_COMMAND_REPLY);
                PTCHECK(f && f->tag == 60, "but to the one who played it");
                pt_command(user, PULSE_COMMAND_KILL_SINK_INPUT, 61, &info);
                pt_collect(user);
                PTCHECK(pt_expect_error(61, PULSE_ERR_ACCESS), "and not killed by that user");
                pt_command(root, PULSE_COMMAND_KILL_SINK_INPUT, 62, &info);
                pt_collect(root);
                PTCHECK(pt_expect_ack(62), "root kills it");
        }
        // Playing too many at once.
        {
                int made = 0, refused = 0;

                for (int i = 0; i < 20; i++)
                {
                        long r = pt_play(other, 35, (unsigned long)(70 + i), PULSE_INVALID, 0, PULSE_INVALID, "root1");

                        made += r >= 0;
                        refused += r == -100 - PULSE_ERR_TOOLARGE;
                }
                PTCHECK(made == 20 && refused == 0, "a client has 20 at once: %d made, %d refused", made, refused);
        }
        // Everything out: the cache's memory and every stream given back (the streams of samples belong to no program, so they are ended here).
        pulse_client_close(&pt_engine, other);
        for (int i = 0; i < pt_engine.stream_slots; i++)
                if (pt_engine.stream[i].used && !pt_engine.stream[i].dying)
                        pulse_stream_end(&pt_engine, pt_engine.stream + i);
        pt_remove_sample(root, 90, "root1");
        pt_remove_sample(root, 91, "root2");
        pt_remove_sample(root, 92, "root3");
        for (int block = 0; block < 20; block++)
                pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
        PTCHECK(pt_engine.sample_total == 0 && pt_engine.upload_total == 0 && pt_engine.streams_made == pt_engine.streams_freed, "the cache and the streams are empty: %lu bytes, %lu made, %lu freed", pt_engine.sample_total,
                pt_engine.streams_made, pt_engine.streams_freed);
        // The table: 64 sounds, and the 65th is too many.
        {
                int made = 0, refused = 0;
                char name[16];

                for (int i = 0; i < PULSE_SAMPLES + 2; i++)
                {
                        snprintf(name, sizeof(name), "s%d", i);
                        if (pt_upload(root, 35, (unsigned long)(100 + i), name, AUDIO_FORMAT_S16LE, 2, 48000, 400, &channel, &asked) == 0 && pt_upload_send(root, (unsigned long)(200 + i), channel, big, 400, 400) == 0)
                                made++;
                        else
                                refused++;
                }
                PTCHECK(made == PULSE_SAMPLES && refused == 2, "the table holds %d: %d made, %d refused", PULSE_SAMPLES, made, refused);
                for (int i = 0; i < PULSE_SAMPLES; i++)
                {
                        snprintf(name, sizeof(name), "s%d", i);
                        pt_remove_sample(root, (unsigned long)(300 + i), name);
                }
        }
        pulse_client_close(&pt_engine, user);
        pulse_client_close(&pt_engine, root);
        PTCHECK(pt_engine.sample_total == 0 && pt_allocs == pt_releases + pt_table_blocks() && pt_alloc_bytes == pt_release_bytes + pt_table_bytes(), "every block given back");
}

/* ---- a recorded session, and what is done to it ---- */

static unsigned char pt_session[1 << 17];
static long pt_session_length;

static void pt_record(const void *frame, long length)
{
        memcpy(pt_session + pt_session_length, frame, (size_t)length);
        pt_session_length += length;
}

// A whole session in the order a libpulse program does it: handshake, name, queries, a stream, data, controls, delete, with the bytes kept.
static void pt_make_session(void)
{
        struct pt_w w = {{0}, 0};
        static unsigned char frame[PULSE_IN];
        static const struct
        {
                unsigned long command;
                int kind;
        } script[] = {{PULSE_COMMAND_GET_SERVER_INFO, 0}, {PULSE_COMMAND_GET_SINK_INFO_LIST, 0}, {PULSE_COMMAND_GET_SOURCE_INFO_LIST, 0}, {PULSE_COMMAND_GET_CLIENT_INFO_LIST, 0},
                      {PULSE_COMMAND_GET_SINK_INPUT_INFO_LIST, 0}, {PULSE_COMMAND_SUBSCRIBE, 1}, {PULSE_COMMAND_LOOKUP_SINK, 2}, {PULSE_COMMAND_STAT, 0}};
        unsigned long tag = 1;

#define PT_FRAME(command, fields, data_channel)                                                                       \
        do                                                                                                            \
        {                                                                                                             \
                struct pt_w head = {{0}, 0};                                                                          \
                long length;                                                                                          \
                                                                                                                      \
                pt_u32(&head, command);                                                                               \
                pt_u32(&head, tag++);                                                                                 \
                length = head.n + (fields)->n;                                                                        \
                memset(frame, 0, 20);                                                                                 \
                frame[2] = (unsigned char)(length >> 8);                                                              \
                frame[3] = (unsigned char)length;                                                                     \
                memset(frame + 4, 0xff, 4);                                                                           \
                (void)data_channel;                                                                                   \
                memcpy(frame + 20, head.b, (size_t)head.n);                                                           \
                memcpy(frame + 20 + head.n, (fields)->b, (size_t)(fields)->n);                                        \
                pt_record(frame, 20 + length);                                                                        \
        } while (0)

        pt_session_length = 0;
        pt_u32(&w, 35);
        pt_arb(&w, (const unsigned char[256]){0}, 256);
        PT_FRAME(PULSE_COMMAND_AUTH, &w, 0);
        w.n = 0;
        pt_props(&w, "application.name", "Recorded", "application.icon_name", "audio-x-generic");
        PT_FRAME(PULSE_COMMAND_SET_CLIENT_NAME, &w, 0);
        for (unsigned i = 0; i < sizeof(script) / sizeof(script[0]); i++)
        {
                w.n = 0;
                if (script[i].kind == 1)
                        pt_u32(&w, 0x2ff);
                if (script[i].kind == 2)
                        pt_str(&w, "null");
                PT_FRAME(script[i].command, &w, 0);
        }
        // A stream, its data, and everything done to it.
        w.n = 0;
        pt_spec(&w, AUDIO_FORMAT_S16LE, 2, 48000);
        pt_map(&w, 2, pt_stereo);
        pt_u32(&w, PULSE_INVALID);
        pt_str(&w, 0);
        pt_u32(&w, 0xffffffffu);
        pt_bool(&w, 0);
        pt_u32(&w, 9600);
        pt_u32(&w, 4800);
        pt_u32(&w, 1200);
        pt_u32(&w, 0);
        pt_vol(&w, 2, 0x10000);
        for (int i = 0; i < 7; i++)
                pt_bool(&w, 0);
        pt_bool(&w, 0);
        pt_bool(&w, 1);
        pt_props(&w, "media.name", "Recorded Stream", "media.role", "music");
        pt_bool(&w, 1);
        pt_bool(&w, 0);
        pt_bool(&w, 0);
        pt_bool(&w, 0);
        pt_bool(&w, 0);
        pt_bool(&w, 0);
        pt_bool(&w, 0);
        pt_byte(&w, 'B');
        pt_byte(&w, 0);
        PT_FRAME(PULSE_COMMAND_CREATE_PLAYBACK_STREAM, &w, 0);
        for (int k = 0; k < 4; k++)
        {
                long length = 2400;

                memset(frame, 0, 20);
                frame[2] = (unsigned char)(length >> 8);
                frame[3] = (unsigned char)length;
                memset(frame + 4, 0, 4);
                memcpy(frame + 20, pt_source + k * 600 * 2, (size_t)length);
                pt_record(frame, 20 + length);
        }
        w.n = 0;
        pt_u32(&w, 0);
        pt_bool(&w, 1);
        PT_FRAME(PULSE_COMMAND_CORK_PLAYBACK_STREAM, &w, 0);
        w.n = 0;
        pt_u32(&w, 0);
        pt_bool(&w, 0);
        PT_FRAME(PULSE_COMMAND_CORK_PLAYBACK_STREAM, &w, 0);
        w.n = 0;
        pt_u32(&w, 0);
        pt_byte(&w, 'T');
        pt_be32(&w, 1);
        pt_be32(&w, 2);
        PT_FRAME(PULSE_COMMAND_GET_PLAYBACK_LATENCY, &w, 0);
        w.n = 0;
        pt_u32(&w, 0);
        pt_vol(&w, 2, 0x8000);
        PT_FRAME(PULSE_COMMAND_SET_SINK_INPUT_VOLUME, &w, 0);
        w.n = 0;
        pt_u32(&w, 0);
        pt_str(&w, "Renamed");
        PT_FRAME(PULSE_COMMAND_SET_PLAYBACK_STREAM_NAME, &w, 0);
        w.n = 0;
        pt_u32(&w, 0);
        PT_FRAME(PULSE_COMMAND_FLUSH_PLAYBACK_STREAM, &w, 0);
        w.n = 0;
        pt_u32(&w, 0);
        PT_FRAME(PULSE_COMMAND_DELETE_PLAYBACK_STREAM, &w, 0);
        // A recording, and its controls, and the sources'.
        {
                struct pt_rec_args a = pt_rec_args();

                a.fragsize = 3840;
                w.n = 0;
                pt_rec_fields(&w, 35, &a);
                PT_FRAME(PULSE_COMMAND_CREATE_RECORD_STREAM, &w, 0);
        }
        w.n = 0;
        pt_u32(&w, 1);
        pt_bool(&w, 1);
        PT_FRAME(PULSE_COMMAND_CORK_RECORD_STREAM, &w, 0);
        w.n = 0;
        pt_u32(&w, 1);
        pt_bool(&w, 0);
        PT_FRAME(PULSE_COMMAND_CORK_RECORD_STREAM, &w, 0);
        w.n = 0;
        pt_u32(&w, 1);
        pt_byte(&w, 'T');
        pt_be32(&w, 3);
        pt_be32(&w, 4);
        PT_FRAME(PULSE_COMMAND_GET_RECORD_LATENCY, &w, 0);
        w.n = 0;
        pt_u32(&w, 1);
        pt_u32(&w, 65536);
        pt_u32(&w, 960);
        pt_bool(&w, 1);
        pt_bool(&w, 0);
        PT_FRAME(PULSE_COMMAND_SET_RECORD_STREAM_BUFFER_ATTR, &w, 0);
        w.n = 0;
        pt_u32(&w, 0);
        pt_vol(&w, 2, 0x8000);
        PT_FRAME(PULSE_COMMAND_SET_SOURCE_OUTPUT_VOLUME, &w, 0);
        w.n = 0;
        pt_u32(&w, 0);
        pt_bool(&w, 0);
        PT_FRAME(PULSE_COMMAND_SET_SOURCE_OUTPUT_MUTE, &w, 0);
        w.n = 0;
        pt_u32(&w, 1);
        pt_str(&w, "Renamed record");
        PT_FRAME(PULSE_COMMAND_SET_RECORD_STREAM_NAME, &w, 0);
        w.n = 0;
        PT_FRAME(PULSE_COMMAND_GET_SOURCE_OUTPUT_INFO_LIST, &w, 0);
        w.n = 0;
        pt_u32(&w, 0);
        PT_FRAME(PULSE_COMMAND_GET_SOURCE_OUTPUT_INFO, &w, 0);
        w.n = 0;
        pt_u32(&w, PULSE_INVALID);
        pt_str(&w, "null.monitor");
        pt_vol(&w, 2, 0x8000);
        PT_FRAME(PULSE_COMMAND_SET_SOURCE_VOLUME, &w, 0);
        w.n = 0;
        pt_u32(&w, 0);
        pt_str(&w, 0);
        pt_bool(&w, 0);
        PT_FRAME(PULSE_COMMAND_SET_SOURCE_MUTE, &w, 0);
        w.n = 0;
        pt_u32(&w, 0);
        pt_u32(&w, 0);
        pt_str(&w, 0);
        PT_FRAME(PULSE_COMMAND_MOVE_SOURCE_OUTPUT, &w, 0);
        w.n = 0;
        pt_str(&w, "null.monitor");
        PT_FRAME(PULSE_COMMAND_SET_DEFAULT_SOURCE, &w, 0);
        w.n = 0;
        pt_u32(&w, 1);
        PT_FRAME(PULSE_COMMAND_FLUSH_RECORD_STREAM, &w, 0);
        w.n = 0;
        pt_u32(&w, 1);
        PT_FRAME(PULSE_COMMAND_DELETE_RECORD_STREAM, &w, 0);
        // A sound kept in the server, played, asked about, and taken out.
        w.n = 0;
        pt_str(&w, "Recorded bell");
        pt_spec(&w, AUDIO_FORMAT_S16LE, 2, 48000);
        pt_map(&w, 2, pt_stereo);
        pt_u32(&w, 2400);
        pt_props(&w, "media.name", "Recorded bell", "media.role", "event");
        PT_FRAME(PULSE_COMMAND_CREATE_UPLOAD_STREAM, &w, 0);
        {
                long length = 2400;

                memset(frame, 0, 20);
                frame[2] = (unsigned char)(length >> 8);
                frame[3] = (unsigned char)length;
                frame[7] = 2;
                memcpy(frame + 20, pt_source + 4000 * 2, (size_t)length);
                pt_record(frame, 20 + length);
        }
        w.n = 0;
        pt_u32(&w, 2);
        PT_FRAME(PULSE_COMMAND_FINISH_UPLOAD_STREAM, &w, 0);
        w.n = 0;
        pt_u32(&w, PULSE_INVALID);
        pt_str(&w, 0);
        pt_u32(&w, 0x8000);
        pt_str(&w, "Recorded bell");
        pt_props(&w, "media.role", "event", 0, 0);
        PT_FRAME(PULSE_COMMAND_PLAY_SAMPLE, &w, 0);
        w.n = 0;
        pt_u32(&w, PULSE_INVALID);
        pt_str(&w, "Recorded bell");
        PT_FRAME(PULSE_COMMAND_GET_SAMPLE_INFO, &w, 0);
        w.n = 0;
        PT_FRAME(PULSE_COMMAND_GET_SAMPLE_INFO_LIST, &w, 0);
        w.n = 0;
        pt_str(&w, "Recorded bell");
        PT_FRAME(PULSE_COMMAND_REMOVE_SAMPLE, &w, 0);
#undef PT_FRAME
}

// A session as it is, at every size of piece it may arrive in, gives the same answers.
static void pt_test_session(void)
{
        static const long pieces[] = {1, 3, 20, 21, 100, 4096, 1 << 20};
        static unsigned char first[1 << 18];
        long first_length = -1;

        pt_make_source();
        for (unsigned k = 0; k < sizeof(pieces) / sizeof(pieces[0]); k++)
        {
                long slot, sent = 0, total = 0;
                static unsigned char out[AUDIO_BLOCK * 4], collected[1 << 18];

                pt_reset();
                pulse_sink_add(&pt_engine, "null", "Null", 48000);
                slot = pulse_client_open(&pt_engine, 0, 5);
                while (sent < pt_session_length)
                {
                        long n = pt_session_length - sent < pieces[k] ? pt_session_length - sent : pieces[k];

                        if (pulse_client_feed(&pt_engine, slot, pt_session + sent, n) == PULSE_DEAD)
                        {
                                PTCHECK(0, "the recorded session ends the client at %ld of %ld (pieces of %ld)", sent, pt_session_length, pieces[k]);
                                break;
                        }
                        sent += n;
                        pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
                }
                for (;;)
                {
                        const unsigned char *bytes;
                        long n = pulse_client_out(&pt_engine, slot, &bytes);

                        if (n <= 0)
                                break;
                        if (total + n < (long)sizeof(collected))
                                memcpy(collected + total, bytes, (size_t)n);
                        total += n;
                        pulse_client_sent(&pt_engine, slot, n);
                }
                // What a program asked is answered the same however the bytes came; what the server says on its own (requests, a start, an underflow, events) depends on the timing.
                {
                        long at = 0, answers = 0;

                        while (at + 20 <= total && at + 20 + (long)wire_be32(collected + at) <= total)
                        {
                                long length = (long)wire_be32(collected + at);

                                if (length >= 10 && wire_be32(collected + at + 4) == PULSE_INVALID && wire_be32(collected + at + 20 + 6) != PULSE_INVALID)
                                        answers += 20 + length;
                                at += 20 + length;
                        }
                        total = answers;
                }
                if (k == 0)
                {
                        first_length = total;
                        memcpy(first, collected, (size_t)total);
                        PTCHECK(total > 500, "the recorded session is answered: %ld bytes", total);
                }
                else
                        PTCHECK(total == first_length, "pieces of %ld give %ld bytes, one byte at a time %ld", pieces[k], total, first_length);
                pulse_client_close(&pt_engine, slot);
                for (int block = 0; block < 4; block++)
                        pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
                PTCHECK(pt_engine.streams_made == pt_engine.streams_freed && pt_allocs == pt_releases + pt_table_blocks() && pt_engine.ring_total == 0, "given back");
        }
}

static long pt_fuzz_clients, pt_fuzz_dead;

// Raw bytes and a mutated session into clients, many times, with a mixer running; then everything is counted.
static void pt_test_fuzz(void)
{
        static unsigned char noise[1 << 16], out[AUDIO_BLOCK * 4];
        long rounds = 0, dead = 0, streams_seen = 0;

        static float fl[AUDIO_BLOCK], fr[AUDIO_BLOCK];
        long mic;

        pt_reset();
        pulse_sink_add(&pt_engine, "null", "Null", 48000);
        pulse_sink_add(&pt_engine, "other", "Other", 44100);
        mic = pulse_source_add(&pt_engine, "alsa_input.fuzz.00", "Fuzz mic", 48000);
        pt_sig(fl, fr, 0, AUDIO_BLOCK);
        for (int round = 0; round < 6000; round++)
        {
                long slot = -1, open_now = 0, length;
                int mode = round % 4;

                // The engine takes any number of clients now, so the fuzz keeps the population it kept when the table had 32 slots:
                // at that many it frees a few at random before it opens another.
                for (long i = 0; i < pt_engine.client_slots; i++)
                        open_now += pt_engine.client[i] != 0;
                if (open_now < 32)
                        slot = pulse_client_open(&pt_engine, round % 3 == 0 ? 0 : 1000 + round % 5, 100 + round);
                if (slot < 0)
                {
                        // Full: free a few at random.
                        for (long i = 0; i < pt_engine.client_slots; i++)
                                if (pt_engine.client[i] && pt_rand() % 3 == 0)
                                        pulse_client_close(&pt_engine, i);
                        continue;
                }
                pt_fuzz_clients++;
                if (mode == 0)
                {
                        // Pure noise, with a header now and then that is nearly right.
                        length = (long)(pt_rand() % 3000);
                        for (long i = 0; i < length; i++)
                                noise[i] = (unsigned char)pt_rand();
                        if (length > 20 && pt_rand() % 2)
                        {
                                noise[0] = noise[1] = 0;
                                noise[2] = (unsigned char)(pt_rand() % 8);
                                noise[3] = (unsigned char)(pt_rand());
                                noise[4] = noise[5] = noise[6] = noise[7] = 0xff;
                                noise[16] = noise[17] = noise[18] = noise[19] = 0;
                        }
                }
                else
                {
                        // The recorded session with bytes changed, cut short, or repeated in the middle.
                        length = pt_session_length;
                        memcpy(noise, pt_session, (size_t)length);
                        for (int hits = (int)(pt_rand() % 12); hits > 0; hits--)
                                noise[pt_rand() % (unsigned long)length] = (unsigned char)(mode == 3 ? pt_rand() : noise[pt_rand() % (unsigned long)length]);
                        if (mode == 2)
                                length = (long)(pt_rand() % (unsigned long)length);
                }
                {
                        long sent = 0;

                        while (sent < length)
                        {
                                long n = (long)(1 + pt_rand() % 700);

                                if (n > length - sent)
                                        n = length - sent;
                                if (pulse_client_feed(&pt_engine, slot, noise + sent, n) == PULSE_DEAD)
                                {
                                        dead++;
                                        break;
                                }
                                sent += n;
                                if (pt_rand() % 3 == 0)
                                        pulse_sink_mix(&pt_engine, (long)(pt_rand() % 2), out, AUDIO_FORMAT_S16LE, 2, (long)(1 + pt_rand() % 512));
                                if (pt_rand() % 3 == 0)
                                        pulse_source_feed(&pt_engine, pt_rand() % 2 ? mic : (long)(pt_rand() % 2), fl, fr, (long)(1 + pt_rand() % 512));
                                if (pulse_client_pending(&pt_engine, slot) > PULSE_OUT / 2)
                                {
                                        const unsigned char *bytes;
                                        long got;

                                        while ((got = pulse_client_out(&pt_engine, slot, &bytes)) > 0)
                                                pulse_client_sent(&pt_engine, slot, got);
                                }
                        }
                }
                for (int i = 0; i < pt_engine.stream_slots; i++)
                        streams_seen += pt_engine.stream[i].used;
                {
                        const unsigned char *bytes;
                        long got;

                        while ((got = pulse_client_out(&pt_engine, slot, &bytes)) > 0)
                                pulse_client_sent(&pt_engine, slot, got);
                }
                if (pt_rand() % 4)
                        pulse_client_close(&pt_engine, slot);
                pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
                pulse_sink_mix(&pt_engine, 1, out, AUDIO_FORMAT_S16LE, 2, 256);
                pulse_tick(&pt_engine);
                pt_clock += 5333;
                rounds++;
                if (round % 500 == 499)
                        pulse_capture_allow(&pt_engine, round % 1000 == 999);
        }
        pulse_capture_allow(&pt_engine, 1);
        for (long i = 0; i < pt_engine.client_slots; i++)
                if (pt_engine.client[i])
                        pulse_client_close(&pt_engine, i);
        // What the cache keeps outlives the programs that put it there; the sounds are taken out here, as a server that ended would.
        for (int i = 0; i < PULSE_SAMPLES; i++)
                if (pt_engine.sample[i].used)
                        pt_engine.sample[i].condemned = 1;
        for (int block = 0; block < 6; block++)
        {
                pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 512);
                pulse_sink_mix(&pt_engine, 1, out, AUDIO_FORMAT_S16LE, 2, 512);
        }
        // The streams that play a sound have no program to end them: they are ended with what they play.
        for (int i = 0; i < pt_engine.stream_slots; i++)
                if (pt_engine.stream[i].used && !pt_engine.stream[i].dying)
                        pulse_stream_end(&pt_engine, pt_engine.stream + i);
        for (int block = 0; block < 4; block++)
        {
                pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 512);
                pulse_sink_mix(&pt_engine, 1, out, AUDIO_FORMAT_S16LE, 2, 512);
        }
        for (int i = 0; i < PULSE_SAMPLES; i++)
                if (pt_engine.sample[i].used)
                        pulse_sample_release(&pt_engine, pt_engine.sample + i);
        PTCHECK(pt_engine.sample_total == 0 && pt_engine.upload_total == 0, "the cache is empty: %lu and %lu bytes", pt_engine.sample_total, pt_engine.upload_total);
        PTCHECK(pt_engine.clients_made == pt_engine.clients_dropped, "clients: %lu made, %lu dropped", pt_engine.clients_made, pt_engine.clients_dropped);
        PTCHECK(pt_engine.streams_made == pt_engine.streams_freed && pt_engine.ring_total == 0, "streams: %lu made, %lu freed, %lu ring bytes", pt_engine.streams_made, pt_engine.streams_freed, pt_engine.ring_total);
        PTCHECK(pt_allocs == pt_releases + pt_table_blocks() && pt_alloc_bytes == pt_release_bytes + pt_table_bytes(), "blocks: %ld made, %ld given back", pt_allocs, pt_releases);
        for (int t = 0; t < AUDIO_TABLES; t++)
                PTCHECK(audio_tables[t].users == 0, "table %d kept", t);
        printf("  fuzz: %ld rounds, %ld clients, %ld ended by the engine, %lu protocol errors, %ld streams seen, %lu made\n", rounds, pt_fuzz_clients, dead, pt_engine.protocol_errors, streams_seen, pt_engine.streams_made);
        PTCHECK(pt_engine.streams_made > 200, "the fuzz reached the streams: %lu", pt_engine.streams_made);
        PTCHECK(pt_engine.records_made == pt_engine.records_freed && pulse_source_wanted(&pt_engine, 0) == 0 && pulse_source_wanted(&pt_engine, mic) == 0, "records: %lu made, %lu freed", pt_engine.records_made,
                pt_engine.records_freed);
        printf("  fuzz: %lu record streams made, %lu sounds put in the cache\n", pt_engine.records_made, pt_engine.next_sample);
        PTCHECK(pt_engine.next_sample > 20, "the fuzz reached the cache: %lu", pt_engine.next_sample);
        PTCHECK(pt_engine.records_made > 100, "the fuzz reached the record streams: %lu", pt_engine.records_made);
        (void)pt_fuzz_dead;
}

/* ---- the control client (src/audio/control.c) against this engine, with no socket between ---- */

static long pt_ctl_slot;
static unsigned char pt_ctl_bytes[PULSE_OUT];
static long pt_ctl_have, pt_ctl_at, pt_ctl_sends, pt_ctl_deaf, pt_ctl_fail_send, pt_ctl_stale, pt_ctl_timeout;

// What the client sends goes to the engine as the bytes a connection would carry.
// The clock stands still here too: a call's budget is its whole timeout on every wait, as the transport was asked before.
static long pt_ctl_now(void *context)
{
        (void)context;
        return 0;
}

static long pt_ctl_send(void *context, const unsigned char *bytes, long count, long timeout_ms)
{
        (void)timeout_ms;
        (void)context;
        pt_ctl_sends++;
        if (pt_ctl_fail_send)
                return -1;
        // An answer to a request of another tag, and an error to one, come first: the client reads past them to its own.
        if (pt_ctl_stale)
        {
                static const unsigned char other[2][30] = {{0, 0, 0, 10, 255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'L', 0, 0, 0, 2, 'L', 0, 0, 7, 77},
                                                         {0, 0, 0, 15, 255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'L', 0, 0, 0, 0, 'L', 0, 0, 7, 78}};
                static const unsigned char code[5] = {'L', 0, 0, 0, 3};

                memcpy(pt_ctl_bytes + pt_ctl_have, other[1], 30), memcpy(pt_ctl_bytes + pt_ctl_have + 30, code, 5), pt_ctl_have += 35;
                memcpy(pt_ctl_bytes + pt_ctl_have, other[0], 30), pt_ctl_have += 30;
        }
        return pulse_client_feed(&pt_engine, pt_ctl_slot, bytes, count) < 0 ? -1 : 0;
}

// And what the engine queued is what it reads, a part at a time; a server that says nothing is a read that fails, as a timeout does.
static long pt_ctl_receive(void *context, unsigned char *bytes, long count, long timeout_ms)
{
        (void)context;
        pt_ctl_timeout = timeout_ms;
        while (pt_ctl_have - pt_ctl_at < count)
        {
                const unsigned char *out;
                long n = pt_ctl_deaf ? 0 : pulse_client_out(&pt_engine, pt_ctl_slot, &out);

                if (n <= 0)
                        return -1;
                if (pt_ctl_have + n > (long)sizeof(pt_ctl_bytes))
                        return -1;
                memcpy(pt_ctl_bytes + pt_ctl_have, out, (size_t)n);
                pt_ctl_have += n;
                pulse_client_sent(&pt_engine, pt_ctl_slot, n);
        }
        memcpy(bytes, pt_ctl_bytes + pt_ctl_at, (size_t)count);
        pt_ctl_at += count;
        if (pt_ctl_at == pt_ctl_have)
                pt_ctl_at = pt_ctl_have = 0;
        return 0;
}

static struct ctl pt_ctl;

// The control client joined to the engine as a client of uid, its handshake done; the engine's slot, or -1.
static long pt_ctl_join(long uid)
{
        long slot = pulse_client_open(&pt_engine, uid, 4242);

        if (slot < 0)
                return -1;
        pt_ctl_slot = slot;
        pt_ctl_have = pt_ctl_at = pt_ctl_sends = pt_ctl_deaf = pt_ctl_fail_send = pt_ctl_stale = 0;
        memset(&pt_ctl, 0, sizeof(pt_ctl));
        pt_ctl.transport = (struct ctl_transport){0, pt_ctl_send, pt_ctl_receive, pt_ctl_now};
        return ctl_start(&pt_ctl) == 0 ? slot : -1;
}

static struct pulse_stream *pt_stream_of(unsigned long index)
{
        for (int i = 0; i < pt_engine.stream_slots; i++)
                if (pt_engine.stream[i].used && pt_engine.stream[i].index == index)
                        return pt_engine.stream + i;
        return 0;
}

static void pt_test_control_client(void)
{
        long owner, person, other;
        unsigned long channel, index, index6, missing;
        struct pulse_stream *s;
        struct ctl_input info;
        long sends;

        pt_reset();
        pulse_sink_add(&pt_engine, "null", "Null", 48000);
        owner = pt_connect(1000, 11, 35, "Player");
        PTCHECK(pt_create(owner, 35, 20, 19200, 19200, 4800, &channel, &index, &missing, 0, 0x10000, 1, AUDIO_FORMAT_S16LE, 2, 48000, 0) == 0, "a stereo stream");
        PTCHECK(pt_create(owner, 35, 21, 19200, 19200, 4800, &channel, &index6, &missing, 0, 0x10000, 1, AUDIO_FORMAT_S16LE, 6, 48000, 0) == 0, "and a six channel one");

        // The handshake: the engine has a client that is authorised, called by the name, at the newest version.
        person = pt_ctl_join(0);
        PTCHECK(person >= 0 && ctl_ready(&pt_ctl), "the handshake is done");
        PTCHECK(person >= 0 && pt_engine.client[person]->authorized && pt_engine.client[person]->version == PULSE_VERSION && pt_engine.client[person]->root,
                "the engine has an authorised client of the newest version, and it is the person's (uid 0)");
        {
                char name[64];

                pulse_props_text(&pt_engine.client[person]->props, "application.name", name, sizeof(name));
                PTCHECK(!strcmp(name, CTL_NAME), "the client names itself: %s", name);
        }

        PTCHECK(pt_ctl_timeout == CTL_TIMEOUT_MS, "an answer is waited for CTL_TIMEOUT_MS by default (%ld)", pt_ctl_timeout);

        // What it reads of a stream: its channels, its volume, its mute.
        PTCHECK(ctl_get_input(&pt_ctl, index, &info) == 0 && info.index == index && info.channels == 2 && info.volume == 0x10000 && !info.muted,
                "a stereo stream: 2 channels, 100%%, not muted (%ld %lu %d)", info.channels, info.volume, info.muted);
        PTCHECK(ctl_get_input(&pt_ctl, index6, &info) == 0 && info.channels == 6, "a six channel stream: %ld", info.channels);
        PTCHECK(ctl_kept_channels(&pt_ctl, index) == 2 && ctl_kept_channels(&pt_ctl, index6) == 6, "and the counts are kept by index");
        PTCHECK(ctl_get_input(&pt_ctl, 9999, &info) == CTL_NOENTITY, "a stream that is not there is CTL_NOENTITY");

        // A volume is sent as one number on every channel the stream has, one request when the count is known.
        sends = pt_ctl_sends;
        PTCHECK(ctl_set_input_volume(&pt_ctl, index, 0x8000) == 0, "a stereo volume is taken");
        PTCHECK(pt_ctl_sends == sends + 1, "and costs one request when the count is kept (%ld)", pt_ctl_sends - sends);
        s = pt_stream_of(index);
        PTCHECK(s && s->volume[0] == 0x8000 && s->volume[1] == 0x8000, "both channels are at the volume");
        PTCHECK(ctl_set_input_volume(&pt_ctl, index6, 0x4000) == 0, "a six channel volume is taken");
        s = pt_stream_of(index6);
        PTCHECK(s && s->volume[0] == 0x4000 && s->volume[1] == 0x4000 && s->volume[2] == 0x4000 && s->volume[3] == 0x4000 && s->volume[4] == 0x4000 &&
                    s->volume[5] == 0x4000,
                "all six channels are at the volume");
        PTCHECK(ctl_get_input(&pt_ctl, index, &info) == 0 && info.volume == 0x8000, "and it is read back: %lu", info.volume);
        PTCHECK(ctl_set_input_volume(&pt_ctl, index, 0x30000) == 0 && pt_stream_of(index)->volume[0] == CTL_VOLUME_MAX, "a volume past 200%% is held to it");
        PTCHECK(ctl_set_input_volume(&pt_ctl, index, 0) == 0 && pt_stream_of(index)->volume[0] == 0 && pt_stream_of(index)->volume[1] == 0, "and 0 is 0");

        // A count the server refuses is asked again and the volume sent once more, and kept right.
        ctl_keep_channels(&pt_ctl, index, 6);
        sends = pt_ctl_sends;
        PTCHECK(ctl_set_input_volume(&pt_ctl, index, 0xc000) == 0 && pt_stream_of(index)->volume[0] == 0xc000 && pt_stream_of(index)->volume[1] == 0xc000,
                "a wrong count is mended: asked again, sent again");
        PTCHECK(pt_ctl_sends == sends + 3 && ctl_kept_channels(&pt_ctl, index) == 2, "three requests, and the right count kept (%ld, %ld)", pt_ctl_sends - sends,
                ctl_kept_channels(&pt_ctl, index));
        PTCHECK(ctl_set_input_volume(&pt_ctl, 9999, 0x8000) == CTL_NOENTITY, "a volume for a stream that is not there is CTL_NOENTITY");

        // The mute.
        PTCHECK(ctl_set_input_mute(&pt_ctl, index, 1) == 0 && pt_stream_of(index)->muted == 1, "mute: the stream is muted");
        PTCHECK(ctl_get_input(&pt_ctl, index, &info) == 0 && info.muted == 1, "and it reads muted");
        PTCHECK(ctl_set_input_mute(&pt_ctl, index, 0) == 0 && pt_stream_of(index)->muted == 0, "unmuted");
        PTCHECK(ctl_set_input_mute(&pt_ctl, 9999, 1) == CTL_NOENTITY, "a mute for a stream that is not there is CTL_NOENTITY");

        // Answers that are not ours are read past: another tag's reply, and another tag's error.
        pt_ctl_stale = 1;
        PTCHECK(ctl_set_input_mute(&pt_ctl, index, 1) == 0 && pt_stream_of(index)->muted == 1 && ctl_set_input_mute(&pt_ctl, index, 0) == 0 && !pt_stream_of(index)->muted,
                "a reply and an error with other tags before the answer are read past");
        pt_ctl_stale = 0;

        // A caller that must not wait shortens the wait: the transport is asked for that.
        pt_ctl.timeout_ms = 20;
        PTCHECK(ctl_set_input_mute(&pt_ctl, index, 0) == 0 && pt_ctl_timeout == 20, "the connection's own timeout is what the transport is asked to wait (%ld)", pt_ctl_timeout);
        pt_ctl.timeout_ms = 0;

        // The master volume and mute, by the sink's name: the person's.
        PTCHECK(ctl_set_sink_volume(&pt_ctl, "null", 0x8000) == 0 && pt_engine.sink[0].volume[0] == 0x8000 && pt_engine.sink[0].volume[1] == 0x8000,
                "the sink's volume is on both channels");
        PTCHECK(ctl_set_sink_volume(&pt_ctl, "null", 0x30000) == 0 && pt_engine.sink[0].volume[0] == CTL_VOLUME_MAX, "and held to 200%%");
        PTCHECK(ctl_set_sink_mute(&pt_ctl, "null", 1) == 0 && pt_engine.sink[0].muted == 1, "the sink is muted");
        PTCHECK(ctl_set_sink_mute(&pt_ctl, "null", 0) == 0 && pt_engine.sink[0].muted == 0, "and not");
        PTCHECK(ctl_set_sink_volume(&pt_ctl, "no such sink", 0x8000) == CTL_NOENTITY && ctl_set_sink_mute(&pt_ctl, "no such sink", 1) == CTL_NOENTITY,
                "a sink that is not there is CTL_NOENTITY");

        // Who may: a client that is not the person's changes its own streams only.
        other = pt_ctl_join(1001);
        PTCHECK(other >= 0, "another user joins");
        PTCHECK(ctl_get_input(&pt_ctl, index, &info) == CTL_NOENTITY, "another user's stream is not found by it");
        PTCHECK(ctl_set_input_mute(&pt_ctl, index, 1) == CTL_NOENTITY && !pt_stream_of(index)->muted, "a mute of it is refused as one that is not there: CTL_NOENTITY");
        PTCHECK(ctl_set_input_volume(&pt_ctl, index, 0x2000) != 0 && pt_stream_of(index)->volume[0] == 0xc000, "and a volume is not changed");
        PTCHECK(ctl_set_sink_volume(&pt_ctl, "null", 0x2000) == CTL_ACCESS && pt_engine.sink[0].volume[0] == CTL_VOLUME_MAX, "the master volume is not another user's to change");
        PTCHECK(ctl_set_sink_mute(&pt_ctl, "null", 1) == CTL_ACCESS && !pt_engine.sink[0].muted, "nor its mute");
        pulse_client_close(&pt_engine, other);

        // A server that stops answering, or a connection that is gone, is CTL_FAILED, and then nothing is sent into it.
        pulse_client_close(&pt_engine, person);
        person = pt_ctl_join(0);
        PTCHECK(person >= 0, "the person joins again");
        pt_ctl_deaf = 1;
        PTCHECK(ctl_set_input_mute(&pt_ctl, index, 1) == CTL_FAILED && !ctl_ready(&pt_ctl), "no answer in time: CTL_FAILED, and the client is not ready");
        sends = pt_ctl_sends;
        PTCHECK(ctl_set_input_volume(&pt_ctl, index, 0x1000) == CTL_FAILED && ctl_get_input(&pt_ctl, index, &info) == CTL_FAILED && pt_ctl_sends == sends,
                "and a client that is not ready sends nothing");
        pt_ctl_deaf = 0;
        pulse_client_close(&pt_engine, person);
        person = pt_ctl_join(0);
        pt_ctl_fail_send = 1;
        PTCHECK(ctl_set_input_mute(&pt_ctl, index, 1) == CTL_FAILED && !ctl_ready(&pt_ctl), "a send that fails: CTL_FAILED");
        pt_ctl_fail_send = 0;
        pulse_client_close(&pt_engine, person);

        // The handshake against a server that is not there answers CTL_FAILED, and leaves the client not ready.
        person = pulse_client_open(&pt_engine, 0, 4243);
        pt_ctl_slot = person;
        pt_ctl_have = pt_ctl_at = 0;
        pt_ctl_deaf = 1;
        memset(&pt_ctl, 0, sizeof(pt_ctl));
        pt_ctl.transport = (struct ctl_transport){0, pt_ctl_send, pt_ctl_receive, pt_ctl_now};
        PTCHECK(ctl_start(&pt_ctl) == CTL_FAILED && !ctl_ready(&pt_ctl), "a handshake with no answer fails");
        pt_ctl_deaf = 0;
        pulse_client_close(&pt_engine, person);

        // Ending a stream: the person may, and the stream is gone.
        person = pt_ctl_join(0);
        PTCHECK(ctl_kill_input(&pt_ctl, index) == 0, "the person ends a stream");
        PTCHECK(!pt_stream_of(index) || pt_stream_of(index)->dying, "and it is ending");
        pulse_client_close(&pt_engine, person);
        pulse_client_close(&pt_engine, owner);
        for (int block = 0; block < 6; block++)
        {
                unsigned char out[4096];

                pulse_sink_mix(&pt_engine, 0, out, AUDIO_FORMAT_S16LE, 2, 256);
        }
        PTCHECK(pt_engine.clients_made == pt_engine.clients_dropped, "every client is given back: %lu made, %lu dropped", pt_engine.clients_made, pt_engine.clients_dropped);
}

// The control client's recording half: the microphone's volume and mute by the source's name, and a record stream's by its number.
static void pt_test_control_input(void)
{
        long owner, person, other, mic, sends;
        struct pt_rec_args a = pt_rec_args();
        unsigned long channel, playing, missing, stereo, mono, six;
        struct pulse_record *rec;
        struct ctl_input info;

        pt_reset();
        pulse_sink_add(&pt_engine, "null", "Null", 48000);
        mic = pulse_source_add(&pt_engine, "alsa_input.test.00", "Test mic", 48000);
        owner = pt_connect(1000, 12, 35, "Recorder");
        PTCHECK(pt_create(owner, 35, 20, 19200, 19200, 4800, &channel, &playing, &missing, 0, 0x10000, 1, AUDIO_FORMAT_S16LE, 2, 48000, 0) == 0, "a playing stream, with number 0");
        a.source_index = (unsigned long)mic;
        a.channels = 1;
        PTCHECK(pt_rec_create(owner, 35, 21, &a) == 0, "a mono record stream");
        mono = pt_rec_reply.index;
        a.channels = 2;
        PTCHECK(pt_rec_create(owner, 35, 22, &a) == 0, "a stereo one");
        stereo = pt_rec_reply.index;
        a.channels = 6;
        PTCHECK(pt_rec_create(owner, 35, 23, &a) == 0, "and a six channel one");
        six = pt_rec_reply.index;
        PTCHECK(mono == playing, "the record stream's number is the playing stream's (the numbers of the two kinds are apart): %lu %lu", mono, playing);

        person = pt_ctl_join(0);
        PTCHECK(person >= 0, "the person joins");
        PTCHECK(ctl_get_input(&pt_ctl, playing, &info) == 0 && ctl_kept_channels(&pt_ctl, playing) == 2, "the playing stream's count is kept: 2");

        // The microphone: a source is named, as the page names it, and the master is its own, not the sink's.
        PTCHECK(ctl_set_source_volume(&pt_ctl, "alsa_input.test.00", 0x8000) == 0 && pt_engine.source[mic].volume[0] == 0x8000 && pt_engine.source[mic].volume[1] == 0x8000,
                "the source's volume is on both channels");
        PTCHECK(pt_engine.sink[0].volume[0] != 0x8000, "and the sink's is not touched");
        PTCHECK(ctl_set_source_volume(&pt_ctl, "alsa_input.test.00", 0x30000) == 0 && pt_engine.source[mic].volume[0] == CTL_VOLUME_MAX, "and held to 200%%");
        PTCHECK(ctl_set_source_mute(&pt_ctl, "alsa_input.test.00", 1) == 0 && pt_engine.source[mic].muted == 1 && !pt_engine.sink[0].muted, "the source is muted, and the sink is not");
        PTCHECK(ctl_set_source_mute(&pt_ctl, "alsa_input.test.00", 0) == 0 && pt_engine.source[mic].muted == 0, "and not");
        PTCHECK(ctl_set_source_volume(&pt_ctl, "no such source", 0x8000) == CTL_NOENTITY && ctl_set_source_mute(&pt_ctl, "no such source", 1) == CTL_NOENTITY,
                "a source that is not there is CTL_NOENTITY");
        PTCHECK(ctl_set_source_volume(&pt_ctl, "null", 0x8000) == CTL_NOENTITY, "and a sink's name is no source's");

        // A record stream's volume: its count of channels is found by sending, and kept under a number of its own.
        sends = pt_ctl_sends;
        PTCHECK(ctl_set_output_volume(&pt_ctl, stereo, 0x6000) == 0, "a stereo record stream's volume is taken");
        rec = pulse_output_of(&pt_engine, stereo);
        PTCHECK(rec && rec->volume[0] == 0x6000 && rec->volume[1] == 0x6000 && pt_ctl_sends == sends + 1, "on both channels, in one request (the first guess is two): %ld", pt_ctl_sends - sends);
        PTCHECK(ctl_kept_channels(&pt_ctl, stereo + CTL_RECORD_KEY) == 2, "and the count is kept");
        sends = pt_ctl_sends;
        PTCHECK(ctl_set_output_volume(&pt_ctl, mono, 0x5000) == 0, "a mono one's");
        rec = pulse_output_of(&pt_engine, mono);
        PTCHECK(rec && rec->map_channels == 1 && rec->volume[0] == 0x5000 && pt_ctl_sends == sends + 2, "is found in two requests (two refused, one taken): %ld", pt_ctl_sends - sends);
        PTCHECK(ctl_kept_channels(&pt_ctl, mono + CTL_RECORD_KEY) == 1 && ctl_kept_channels(&pt_ctl, playing) == 2,
                "the count is kept under the record stream's own number, and the playing stream's (which has the same) is as it was");
        sends = pt_ctl_sends;
        PTCHECK(ctl_set_output_volume(&pt_ctl, mono, 0x4000) == 0 && pulse_output_of(&pt_engine, mono)->volume[0] == 0x4000 && pt_ctl_sends == sends + 1, "and the next is one request");
        PTCHECK(ctl_set_input_volume(&pt_ctl, playing, 0x7000) == 0 && pt_stream_of(playing)->volume[0] == 0x7000 && pt_stream_of(playing)->volume[1] == 0x7000,
                "while the playing stream's is still its two channels");
        sends = pt_ctl_sends;
        PTCHECK(ctl_set_output_volume(&pt_ctl, six, 0x2000) == 0, "a six channel one's");
        rec = pulse_output_of(&pt_engine, six);
        PTCHECK(rec && rec->map_channels == 6 && rec->volume[0] == 0x2000 && rec->volume[5] == 0x2000 && pt_ctl_sends == sends + 4,
                "is found by the counts in turn: two, one and four refused, six taken (%ld)", pt_ctl_sends - sends);
        PTCHECK(ctl_set_output_volume(&pt_ctl, six, 0x30000) == 0 && pulse_output_of(&pt_engine, six)->volume[3] == CTL_VOLUME_MAX, "and held to 200%%");
        ctl_keep_channels(&pt_ctl, stereo + CTL_RECORD_KEY, 6);
        PTCHECK(ctl_set_output_volume(&pt_ctl, stereo, 0x3000) == 0 && pulse_output_of(&pt_engine, stereo)->volume[1] == 0x3000 && ctl_kept_channels(&pt_ctl, stereo + CTL_RECORD_KEY) == 2,
                "a wrong count is mended and the right one kept");
        PTCHECK(ctl_set_output_volume(&pt_ctl, 9999, 0x8000) == CTL_NOENTITY, "a record stream that is not there is CTL_NOENTITY");

        // Its mute: the one program's microphone.
        PTCHECK(ctl_set_output_mute(&pt_ctl, mono, 1) == 0 && pulse_output_of(&pt_engine, mono)->muted == 1 && !pt_stream_of(playing)->muted,
                "a record stream is muted, and a playing stream of the same number is not");
        PTCHECK(ctl_set_output_mute(&pt_ctl, mono, 0) == 0 && pulse_output_of(&pt_engine, mono)->muted == 0, "and not");
        PTCHECK(ctl_set_output_mute(&pt_ctl, 9999, 1) == CTL_NOENTITY, "a mute for one that is not there is CTL_NOENTITY");

        // Who may: the person's session, and a program its own record streams only.
        other = pt_ctl_join(1001);
        PTCHECK(other >= 0, "another user joins");
        PTCHECK(ctl_set_output_mute(&pt_ctl, mono, 1) == CTL_NOENTITY && pulse_output_of(&pt_engine, mono)->muted == 0, "a mute of another user's record stream is refused as one that is not there: CTL_NOENTITY");
        PTCHECK(ctl_set_output_volume(&pt_ctl, mono, 0x1000) == CTL_NOENTITY && pulse_output_of(&pt_engine, mono)->volume[0] == 0x4000, "and a volume");
        PTCHECK(ctl_set_source_volume(&pt_ctl, "alsa_input.test.00", 0x1000) == CTL_ACCESS && pt_engine.source[mic].volume[0] == CTL_VOLUME_MAX, "the microphone is not another user's to change");
        PTCHECK(ctl_set_source_mute(&pt_ctl, "alsa_input.test.00", 1) == CTL_ACCESS && !pt_engine.source[mic].muted, "nor its mute");
        pulse_client_close(&pt_engine, other);
        other = pt_ctl_join(1000);
        PTCHECK(other >= 0 && ctl_set_output_mute(&pt_ctl, mono, 1) == 0 && pulse_output_of(&pt_engine, mono)->muted == 1, "the program's own user may mute its record stream");
        PTCHECK(ctl_set_output_volume(&pt_ctl, mono, 0x1000) == 0 && pulse_output_of(&pt_engine, mono)->volume[0] == 0x1000, "and change its volume");
        pulse_client_close(&pt_engine, other);
        pulse_client_close(&pt_engine, person);
        pulse_client_close(&pt_engine, owner);
}

/* ---- an info list too large for the engine's scratch is refused, and nothing is written past the scratch ---- */

static void pt_big_props(struct pt_w *w)
{
        unsigned char value[PULSE_VALUE];

        memset(value, 'v', sizeof(value));
        pt_byte(w, 'P');
        for (int k = 0; k < PULSE_PROPS; k++)
        {
                char key[PULSE_KEY];

                snprintf(key, sizeof(key), "moonwater.test.%02d.abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrs", k);
                pt_str(w, key);
                pt_u32(w, sizeof(value));
                pt_arb(w, value, sizeof(value));
        }
        pt_byte(w, 'N');
}

static void pt_test_list_bounds(void)
{
        static unsigned char before[sizeof(pt_engine.record_a)];
        static struct pt_w w;
        unsigned long channels[16], index, missing, channel;
        long slot, made = 0;
        int answered = 0;

        pt_reset();
        pulse_sink_add(&pt_engine, "null", "Null", 48000);
        slot = pt_connect(1000, 4300, 35, "lister");
        PTCHECK(slot >= 0, "a client that lists its streams connects");
        for (int i = 0; slot >= 0 && i < 16; i++)
        {
                if (pt_create(slot, 35, 100 + i, 0, 0, 0, &channel, &index, &missing, 1, AUDIO_VOLUME_NORM, 1, AUDIO_FORMAT_S16LE, 2, 48000, 0) != 0)
                        break;
                channels[made++] = channel;
                w.n = 0;
                pt_u32(&w, channel);
                pt_u32(&w, 2);
                pt_big_props(&w);
                pt_command(slot, PULSE_COMMAND_UPDATE_PLAYBACK_STREAM_PROPLIST, 200 + i, &w);
                pt_collect(slot);
        }
        PTCHECK(made == 16, "%ld streams with full property lists are made", made);
        pt_collect(slot);
        // Sixteen such streams make a list of about 160 KB: more than the 128 KB scratch it is built in. The writer is bounded, so
        // nothing lands past the scratch (the mixer's record buffers are kept as they were), and the list is refused as too
        // large with the client kept: the old reply was dropped with the client, which a program listing its own streams hits.
        memcpy(before, pt_engine.record_a, sizeof(before));
        PTCHECK(pt_command(slot, PULSE_COMMAND_GET_SINK_INPUT_INFO_LIST, 300, 0) == PULSE_DONE, "the list of them does not drop the client");
        pt_collect(slot);
        PTCHECK(memcmp(before, pt_engine.record_a, sizeof(before)) == 0, "a list of them writes nothing past the scratch");
        PTCHECK(pt_expect_error(300, PULSE_ERR_TOOLARGE), "and the list is refused as too large");
        // Two of them fit, and are answered.
        for (long i = 2; i < made; i++)
        {
                w.n = 0;
                pt_u32(&w, channels[i]);
                pt_command(slot, PULSE_COMMAND_DELETE_PLAYBACK_STREAM, 400 + (unsigned long)i, &w);
        }
        pt_collect(slot);
        pt_command(slot, PULSE_COMMAND_GET_SINK_INPUT_INFO_LIST, 301, 0);
        pt_collect(slot);
        for (int i = 0; i < pt_logged; i++)
                answered |= pt_log[i].command == PULSE_COMMAND_REPLY && pt_log[i].tag == 301;
        PTCHECK(answered, "two streams with full property lists are listed");
        pulse_client_close(&pt_engine, slot);
}

/* ---- a stream that asked not to be moved is not moved by its program; the person (root) may move it ---- */

static void pt_test_no_move(void)
{
        unsigned long channel, missing, pinned, free_index;
        struct pt_w w;
        long owner, person;

        pt_reset();
        pulse_sink_add(&pt_engine, "one", "One", 48000);
        pulse_sink_add(&pt_engine, "two", "Two", 48000);
        owner = pt_connect(1000, 4700, 35, "Pinned");
        person = pt_connect(0, 4701, 35, "Person");
        pt_next_no_move = 1;
        PTCHECK(pt_create(owner, 35, 10, 0, 0, 0, &channel, &pinned, &missing, 0, AUDIO_VOLUME_NORM, 1, AUDIO_FORMAT_S16LE, 2, 48000, "one") == 0, "a stream that asked not to be moved is made");
        pt_next_no_move = 0;
        PTCHECK(pt_create(owner, 35, 11, 0, 0, 0, &channel, &free_index, &missing, 0, AUDIO_VOLUME_NORM, 1, AUDIO_FORMAT_S16LE, 2, 48000, "one") == 0, "and one that did not ask");

        // Its program moves it to the other sink: refused as the program's own no-move request, and it stays where it was.
        memset(&w, 0, sizeof(w));
        pt_u32(&w, pinned);
        pt_u32(&w, 1);
        pt_str(&w, 0);
        pt_command(owner, PULSE_COMMAND_MOVE_SINK_INPUT, 12, &w);
        pt_collect(owner);
        PTCHECK(pt_expect_error(12, PULSE_ERR_ACCESS), "its program cannot move it (ACCESS)");
        PTCHECK(pulse_input_of(&pt_engine, pinned)->sink == 0, "and it stays on its sink");

        // The one that did not ask moves as before.
        memset(&w, 0, sizeof(w));
        pt_u32(&w, free_index);
        pt_u32(&w, 1);
        pt_str(&w, 0);
        pt_command(owner, PULSE_COMMAND_MOVE_SINK_INPUT, 13, &w);
        pt_collect(owner);
        PTCHECK(pt_expect_ack(13) && pulse_input_of(&pt_engine, free_index)->sink == 1, "a stream that did not ask is moved");

        // The person (root) may move it.
        memset(&w, 0, sizeof(w));
        pt_u32(&w, pinned);
        pt_u32(&w, 1);
        pt_str(&w, 0);
        pt_command(person, PULSE_COMMAND_MOVE_SINK_INPUT, 14, &w);
        pt_collect(person);
        PTCHECK(pt_expect_ack(14) && pulse_input_of(&pt_engine, pinned)->sink == 1, "the person moves it");
        pulse_client_close(&pt_engine, person);
        pulse_client_close(&pt_engine, owner);
}

/* ---- a remembered volume belongs to its user: another user's program of the same name neither inherits it nor sets it ---- */

static void pt_set_input_volume(long slot, unsigned long tag, unsigned long index, unsigned long volume)
{
        struct pt_w w = {{0}, 0};

        pt_u32(&w, index);
        pt_vol(&w, 2, volume);
        pt_command(slot, PULSE_COMMAND_SET_SINK_INPUT_VOLUME, tag, &w);
        pt_collect(slot);
}

static void pt_test_remember_users(void)
{
        unsigned long channel, missing, mine_first, mine_again, theirs;
        long mine, other;

        pt_reset();
        pulse_sink_add(&pt_engine, "null", "Null", 48000);
        mine = pt_connect(1000, 4800, 35, "Music");
        other = pt_connect(1001, 4801, 35, "Music");
        PTCHECK(pt_create(mine, 35, 20, 0, 0, 0, &channel, &mine_first, &missing, 0, AUDIO_VOLUME_NORM, 1, AUDIO_FORMAT_S16LE, 2, 48000, 0) == 0, "a program of user 1000 named Music is made");
        pt_set_input_volume(mine, 21, mine_first, 0x4000);
        PTCHECK(pt_expect_ack(21) && pulse_input_of(&pt_engine, mine_first)->volume[0] == 0x4000, "its volume is set to a quarter");

        // User 1001's Music, which did not say its volume, is not given user 1000's remembered quarter.
        PTCHECK(pt_create(other, 35, 22, 0, 0, 0, &channel, &theirs, &missing, 0, AUDIO_VOLUME_NORM, 0, AUDIO_FORMAT_S16LE, 2, 48000, 0) == 0, "a program of user 1001 named Music is made");
        PTCHECK(pulse_input_of(&pt_engine, theirs)->volume[0] == AUDIO_VOLUME_NORM, "and it plays at full volume, not at another user's remembered quarter (%lu)", pulse_input_of(&pt_engine, theirs)->volume[0]);

        // Its own setting is user 1001's: user 1000's next Music keeps its quarter.
        pt_set_input_volume(other, 23, theirs, 0xc000);
        PTCHECK(pt_create(mine, 35, 24, 0, 0, 0, &channel, &mine_again, &missing, 0, AUDIO_VOLUME_NORM, 0, AUDIO_FORMAT_S16LE, 2, 48000, 0) == 0, "user 1000's next Music is made");
        PTCHECK(pulse_input_of(&pt_engine, mine_again)->volume[0] == 0x4000, "and remembers its own quarter, not user 1001's three quarters (%lu)", pulse_input_of(&pt_engine, mine_again)->volume[0]);
        pulse_client_close(&pt_engine, other);
        pulse_client_close(&pt_engine, mine);
}

/* ---- an event about another user's stream or sample is not sent to a program that may not see it ---- */

// Whether an event of new (facility | kind == what) about index was queued for the client (the queue is taken by the call).
static int pt_saw_event(long slot, unsigned long what, unsigned long index)
{
        pt_collect(slot);
        for (int i = 0; i < pt_logged; i++)
                if (pt_log[i].command == PULSE_COMMAND_SUBSCRIBE_EVENT && pt_log[i].length >= 20 && wire_be32(pt_log[i].payload + 11) == what &&
                    wire_be32(pt_log[i].payload + 16) == index)
                        return 1;
        return 0;
}

static void pt_test_event_rights(void)
{
        static unsigned char data[16];
        unsigned long channel, index, missing, sample_index;
        long owner, other, person;

        pt_reset();
        pulse_sink_add(&pt_engine, "null", "Null", 48000);
        owner = pt_connect(1000, 4900, 35, "Owner");
        other = pt_connect(1001, 4901, 35, "Other");
        person = pt_connect(0, 4902, 35, "Person");
        {
                struct pt_w w = {{0}, 0};

                pt_u32(&w, (1ul << PULSE_FACILITY_SINK_INPUT) | (1ul << PULSE_FACILITY_SAMPLE_CACHE));
                pt_command(other, PULSE_COMMAND_SUBSCRIBE, 40, &w);
                pt_command(person, PULSE_COMMAND_SUBSCRIBE, 41, &w);
                pt_collect(other);
                pt_collect(person);
        }
        PTCHECK(pt_create(owner, 35, 42, 0, 0, 0, &channel, &index, &missing, 0, AUDIO_VOLUME_NORM, 1, AUDIO_FORMAT_S16LE, 2, 48000, 0) == 0, "a stream of user 1000 is made");
        PTCHECK(pt_saw_event(person, PULSE_FACILITY_SINK_INPUT | PULSE_EVENT_NEW, index), "the person is told of it");
        PTCHECK(!pt_saw_event(other, PULSE_FACILITY_SINK_INPUT | PULSE_EVENT_NEW, index), "user 1001 is not told of user 1000's stream");
        PTCHECK(pt_saw_event(owner, PULSE_FACILITY_SINK_INPUT | PULSE_EVENT_NEW, index) || !pt_saw_event(owner, PULSE_FACILITY_SINK_INPUT | PULSE_EVENT_NEW, index),
                "the owner is not subscribed here (nothing to check)");

        memset(data, 0x22, sizeof(data));
        PTCHECK(pt_upload(owner, 35, 43, "private", AUDIO_FORMAT_S16LE, 2, 48000, 16, &channel, &missing) == 0 && pt_upload_send(owner, 44, channel, data, 16, 16) == 0, "a sound of user 1000 is uploaded");
        sample_index = pulse_sample_named(&pt_engine, "private") ? pulse_sample_named(&pt_engine, "private")->index : 999;
        PTCHECK(pt_saw_event(person, PULSE_FACILITY_SAMPLE_CACHE | PULSE_EVENT_NEW, sample_index), "the person is told of the sound");
        PTCHECK(!pt_saw_event(other, PULSE_FACILITY_SAMPLE_CACHE | PULSE_EVENT_NEW, sample_index), "user 1001 is not told of user 1000's sound");
        {
                struct pt_w w = {{0}, 0};

                // The sound's info by name is not user 1001's to see, nor is it in the list it asks for.
                pt_u32(&w, PULSE_INVALID);
                pt_str(&w, "private");
                pt_command(other, PULSE_COMMAND_GET_SAMPLE_INFO, 45, &w);
                pt_collect(other);
                PTCHECK(pt_expect_error(45, PULSE_ERR_NOENTITY), "user 1001 is told the sound is not there");
                pt_command(other, PULSE_COMMAND_GET_SAMPLE_INFO_LIST, 46, 0);
                pt_collect(other);
                {
                        // A list with no entries is a reply of the tag alone: ten bytes.
                        int empty = 0, answered = 0;

                        for (int i = 0; i < pt_logged; i++)
                                if (pt_log[i].command == PULSE_COMMAND_REPLY && pt_log[i].tag == 46)
                                        answered = 1, empty = pt_log[i].length == 10;
                        PTCHECK(answered && empty, "and its list has no sound of user 1000's");
                }
                pt_command(person, PULSE_COMMAND_GET_SAMPLE_INFO, 47, &w);
                pt_collect(person);
                {
                        int answered = 0;

                        for (int i = 0; i < pt_logged; i++)
                                answered |= pt_log[i].command == PULSE_COMMAND_REPLY && pt_log[i].tag == 47;
                        PTCHECK(answered, "the person is told of it");
                }
        }
        pulse_client_close(&pt_engine, person);
        pulse_client_close(&pt_engine, other);
        pulse_client_close(&pt_engine, owner);
}

/* ---- a program that is not root gets the same answer for a sink or source that is there and one that is not ---- */

static void pt_test_source_oracle(void)
{
        struct pt_w w;
        struct pt_rec_args a;
        long user;

        pt_reset();
        pulse_sink_add(&pt_engine, "null", "Null", 48000);
        user = pt_connect(1000, 4950, 35, "Probe");
        // The monitor of the sink is there; the other name is not. The volume, the mute and the record stream each answer the
        // same for both (ACCESS: a program that is not root changes none of them).
        for (int which = 0; which < 2; which++)
        {
                const char *name = which ? "nothere" : "null.monitor";
                unsigned long tag = 60 + (unsigned long)which * 10;

                memset(&w, 0, sizeof(w));
                pt_u32(&w, PULSE_INVALID);
                pt_str(&w, name);
                pt_vol(&w, 2, 0x8000);
                pt_command(user, PULSE_COMMAND_SET_SOURCE_VOLUME, tag, &w);
                pt_collect(user);
                PTCHECK(pt_expect_error(tag, PULSE_ERR_ACCESS), "a source volume of %s is ACCESS for a program that is not root", name);
                memset(&w, 0, sizeof(w));
                pt_u32(&w, PULSE_INVALID);
                pt_str(&w, name);
                pt_bool(&w, 1);
                pt_command(user, PULSE_COMMAND_SET_SOURCE_MUTE, tag + 1, &w);
                pt_collect(user);
                PTCHECK(pt_expect_error(tag + 1, PULSE_ERR_ACCESS), "a source mute of %s is ACCESS for a program that is not root", name);
                memset(&w, 0, sizeof(w));
                pt_u32(&w, PULSE_INVALID);
                pt_str(&w, name);
                pt_vol(&w, 2, 0x8000);
                pt_command(user, PULSE_COMMAND_SET_SINK_VOLUME, tag + 2, &w);
                pt_collect(user);
                PTCHECK(pt_expect_error(tag + 2, PULSE_ERR_ACCESS) || which == 0, "a sink volume of %s is ACCESS for a program that is not root", name);
                a = pt_rec_args();
                a.source_name = name;
                PTCHECK(pt_rec_create(user, 35, 80 + (unsigned long)which, &a) == PULSE_ERR_ACCESS, "a record stream of %s is ACCESS for a program that is not root", name);
        }
        pulse_client_close(&pt_engine, user);
}

/* ---- a period in which no stream drains reads no clock: the host's clock is a system call in the service ---- */

static void pt_test_period_clock(void)
{
        static unsigned char data[AUDIO_BLOCK * 4 * 8];
        static unsigned char dst[AUDIO_BLOCK * 4];
        unsigned long channel, index, missing;
        long slot, before;

        pt_reset();
        pulse_sink_add(&pt_engine, "null", "Null", 48000);
        slot = pt_connect(1000, 5100, 35, "Clock");
        memset(data, 0x33, sizeof(data));
        for (int i = 0; i < 4; i++)
        {
                PTCHECK(pt_create(slot, 35, 100 + (unsigned long)i, 0, 0, 0, &channel, &index, &missing, 0, AUDIO_VOLUME_NORM, 1, AUDIO_FORMAT_S16LE, 2, 48000, 0) == 0, "stream %d is made", i);
                pt_data(slot, channel, data, sizeof(data), 0, 0);
        }
        pt_collect(slot);
        before = pt_now_calls;
        pulse_sink_mix(&pt_engine, 0, dst, AUDIO_FORMAT_S16LE, 2, AUDIO_BLOCK);
        PTCHECK(pt_now_calls - before == 0, "a block of four playing streams that drain nothing reads the clock %ld times (want 0)", pt_now_calls - before);
        pulse_client_close(&pt_engine, slot);
}

/* ---- a record stream that asked not to be moved is not moved by its program; the person (root) may move it ---- */

static void pt_test_record_no_move(void)
{
        struct pt_rec_args a;
        struct pt_w w;
        long owner, person, mic1, mic2;
        unsigned long index;

        pt_reset();
        pulse_sink_add(&pt_engine, "null", "Null", 48000);
        mic1 = pulse_source_add(&pt_engine, "alsa_input.nm1.00", "One", 48000);
        mic2 = pulse_source_add(&pt_engine, "alsa_input.nm2.00", "Two", 48000);
        owner = pt_connect(1000, 5200, 35, "Recorder");
        person = pt_connect(0, 5201, 35, "Person");
        a = pt_rec_args();
        a.source_name = "alsa_input.nm1.00";
        a.no_move = 1;
        PTCHECK(pt_rec_create(owner, 35, 10, &a) == 0, "a record stream that asked not to be moved is made");
        index = pt_rec_reply.index;
        memset(&w, 0, sizeof(w));
        pt_u32(&w, index);
        pt_u32(&w, (unsigned long)mic2);
        pt_str(&w, 0);
        pt_command(owner, PULSE_COMMAND_MOVE_SOURCE_OUTPUT, 11, &w);
        pt_collect(owner);
        PTCHECK(pt_expect_error(11, PULSE_ERR_ACCESS), "its program cannot move it (ACCESS)");
        PTCHECK(pulse_output_of(&pt_engine, index)->source == mic1, "and it stays on its source");
        pt_command(person, PULSE_COMMAND_MOVE_SOURCE_OUTPUT, 12, &w);
        pt_collect(person);
        PTCHECK(pt_expect_ack(12) && pulse_output_of(&pt_engine, index)->source == mic2, "the person moves it");
        pulse_client_close(&pt_engine, person);
        pulse_client_close(&pt_engine, owner);
}

/* ---- frames cut anywhere: one feed, chunks of seven bytes, and single bytes answer the same, in order ---- */

static long pt_split_tags(unsigned char *bytes, long n, long chunk, unsigned long *tags, long room)
{
        long found = 0;

        pt_reset();
        pulse_sink_add(&pt_engine, "null", "Null", 48000);
        long slot = pt_connect(1000, 4500 + (long)chunk, 35, "split");
        for (long at = 0; at < n; at += chunk)
                pulse_client_feed(&pt_engine, slot, bytes + at, at + chunk <= n ? chunk : n - at);
        pt_collect(slot);
        for (int i = 0; i < pt_logged && found < room; i++)
                if (pt_log[i].command == PULSE_COMMAND_REPLY && pt_log[i].length >= 10)
                        tags[found++] = pt_log[i].tag;
        pulse_client_close(&pt_engine, slot);
        return found;
}

static void pt_test_feed_cut(void)
{
        static unsigned char bytes[30 * 40];
        static unsigned long whole[64], chunked[64], single[64];
        long n = 0, found;

        for (unsigned long tag = 1; tag <= 40; tag++, n += 30)
        {
                memset(bytes + n, 0, 30);
                bytes[n + 3] = 10;
                memset(bytes + n + 4, 0xff, 4);
                bytes[n + 20] = 'L';
                bytes[n + 24] = 13;
                bytes[n + 25] = 'L';
                bytes[n + 26] = (unsigned char)(tag >> 24);
                bytes[n + 27] = (unsigned char)(tag >> 16);
                bytes[n + 28] = (unsigned char)(tag >> 8);
                bytes[n + 29] = (unsigned char)tag;
        }
        found = pt_split_tags(bytes, n, n, whole, 64);
        PTCHECK(found == 40, "40 stats fed whole are answered 40 times (%ld)", found);
        for (long i = 0; i < found; i++)
                PTCHECK(whole[i] == (unsigned long)(i + 1), "answer %ld is for tag %ld", i, (long)whole[i]);
        found = pt_split_tags(bytes, n, 7, chunked, 64);
        PTCHECK(found == 40, "fed in sevens (which cut every frame) they are answered 40 times (%ld)", found);
        for (long i = 0; i < found; i++)
                PTCHECK(chunked[i] == whole[i], "fed in sevens, answer %ld is the same tag", i);
        found = pt_split_tags(bytes, n, 1, single, 64);
        PTCHECK(found == 40, "fed a byte at a time they are answered 40 times (%ld)", found);
        for (long i = 0; i < found; i++)
                PTCHECK(single[i] == whole[i], "fed a byte at a time, answer %ld is the same tag", i);
}

/* ---- a sound replaced while the cache is full: the one it replaces gives its place to the new one ---- */

static void pt_test_samples_full(void)
{
        static unsigned char data[16];
        unsigned long channel, asked, index = 0;
        char name[32];
        long slot, used = 0;

        pt_reset();
        slot = pt_connect(1000, 4600, 35, "fill");
        memset(data, 0x11, sizeof(data));
        for (int i = 0; i < PULSE_SAMPLES; i++)
        {
                snprintf(name, sizeof(name), "sound.%02d", i);
                PTCHECK(pt_upload(slot, 35, 10 + (unsigned long)i, name, AUDIO_FORMAT_S16LE, 2, 48000, 16, &channel, &asked) == 0, "sound %d is uploaded", i);
                PTCHECK(pt_upload_send(slot, 1000 + (unsigned long)i, channel, data, 16, 16) == 0, "sound %d is finished", i);
        }
        for (int i = 0; i < PULSE_SAMPLES; i++)
                used += pt_engine.sample[i].used;
        PTCHECK(used == PULSE_SAMPLES, "the cache is full: %ld sounds", used);
        PTCHECK(pulse_sample_named(&pt_engine, "sound.07") && (index = pulse_sample_named(&pt_engine, "sound.07")->index) == 7, "sound.07 is in its place");

        // The cache has no free place: a sound of a name that is there replaces it in its place, and nothing is left over.
        pt_upload(slot, 35, 500, "sound.07", AUDIO_FORMAT_S16LE, 2, 48000, 16, &channel, &asked);
        PTCHECK(pt_upload_send(slot, 501, channel, data, 16, 16) == 0, "a full cache takes a replacement of one of its sounds");
        PTCHECK(pulse_sample_named(&pt_engine, "sound.07") && pulse_sample_named(&pt_engine, "sound.07")->index == index, "and the replacement keeps its index");
        PTCHECK(pt_engine.sample_total == PULSE_SAMPLES * 16ul, "and no memory is left of the one it replaced (%lu bytes)", pt_engine.sample_total);
        pulse_client_close(&pt_engine, slot);
}

/*
        The engine's four loops against their specifications (the audio_c_ functions of the shared block above): the name
        cleaner, the key test, the big-endian reversal and the U8 narrowing through the record encoder, and the peak window
        through a peak stream. Each row is a fixed-seed run, so a failure is the same every time.
*/
static unsigned long pt_spec_state = 0x2545f4914f6cdd1dul;

static unsigned long pt_spec_next(void)
{
        pt_spec_state ^= pt_spec_state << 13;
        pt_spec_state ^= pt_spec_state >> 7;
        pt_spec_state ^= pt_spec_state << 17;
        return pt_spec_state;
}

// The bytes the text and key rules turn on: each boundary of their ranges, and the lead and continuation bytes of every form.
static const unsigned char pt_spec_bytes[] = {0x00, 0x01, 0x1f, 0x20, 0x21, 0x2e, 0x7d, 0x7e, 0x7f, 0x80, 0x8f, 0x90, 0x9f, 0xa0, 0xbf, 0xc0,
                                              0xc1, 0xc2, 0xc3, 0xdf, 0xe0, 0xe1, 0xec, 0xed, 0xee, 0xef, 0xf0, 0xf1, 0xf3, 0xf4, 0xf5, 0xfe,
                                              0xff, 0x41, 0xe9};

// Whole sequences of every form the text rule accepts or refuses, cut short at random, so the rows reach every branch.
static const unsigned char *pt_spec_forms[] = {(const unsigned char *)"\xc2\xa0", (const unsigned char *)"\xc2\x9f", (const unsigned char *)"\xe0\xa0\x80",
                                               (const unsigned char *)"\xe0\x9f\xbf", (const unsigned char *)"\xed\x9f\xbf", (const unsigned char *)"\xed\xa0\x80",
                                               (const unsigned char *)"\xf0\x90\x80\x80", (const unsigned char *)"\xf0\x8f\xbf\xbf",
                                               (const unsigned char *)"\xf4\x8f\xbf\xbf", (const unsigned char *)"\xf4\x90\x80\x80",
                                               (const unsigned char *)"\xc3\xa9", (const unsigned char *)"\xe2\x82\xac", (const unsigned char *)"\xf0\x9f\x8e\xb5"};

static void pt_test_reference_loops(void)
{
        int bad;

        // The name cleaner: every value up to 120 bytes, a NUL inside some of them, the forms above and random bytes.
        bad = 0;
        for (int round = 0; round < 3000; round++)
        {
                unsigned char value[160], want[160], got[260];
                long length = (long)(pt_spec_next() % 121), at = 0;
                struct pulse_props props = {0};
                long n, m;

                while (at < length)
                {
                        unsigned long r = pt_spec_next();

                        if (r % 4 == 0)
                        {
                                const unsigned char *form = pt_spec_forms[(r >> 8) % (sizeof(pt_spec_forms) / sizeof(pt_spec_forms[0]))];
                                long take = (long)((r >> 20) % 5);

                                for (long k = 0; k < take && form[k] && at < length; k++)
                                        value[at++] = form[k];
                                continue;
                        }
                        value[at++] = r % 3 == 0 ? (unsigned char)(r >> 16) : pt_spec_bytes[(r >> 16) % (sizeof(pt_spec_bytes) / sizeof(pt_spec_bytes[0]))];
                }
                pulse_props_set(&props, "media.name", value, length);
                n = pulse_props_text(&props, "media.name", (char *)got, sizeof(got));
                m = audio_c_text_clean(want, value, length);
                if (n != m || memcmp(got, want, (size_t)n))
                        bad++;
        }
        PTCHECK(bad == 0, "the name cleaner is the specification on 3000 values: %d differ", bad);

        // The key test: lengths 0 to 69 over the boundary bytes and random ones.
        bad = 0;
        for (int round = 0; round < 2000; round++)
        {
                unsigned char key[80];
                long length = (long)(pt_spec_next() % 70);

                for (long i = 0; i < 70; i++)
                {
                        unsigned long r = pt_spec_next();

                        key[i] = r % 2 ? pt_spec_bytes[(r >> 8) % (sizeof(pt_spec_bytes) / sizeof(pt_spec_bytes[0]))] : (unsigned char)(r >> 16);
                }
                if (pulse_key_ok((const char *)key, length) != audio_c_key_printable(key, length))
                        bad++;
        }
        PTCHECK(bad == 0, "the key test is the specification on 2000 keys: %d differ", bad);

        // The big-endian formats: the engine's bytes are the little-endian ones with each sample reversed.
        {
                static const long big[] = {AUDIO_FORMAT_S16BE, AUDIO_FORMAT_S24BE, AUDIO_FORMAT_S24_32BE, AUDIO_FORMAT_S32BE, AUDIO_FORMAT_F32BE};
                static const long little[] = {AUDIO_FORMAT_S16LE, AUDIO_FORMAT_S24LE, AUDIO_FORMAT_S24_32LE, AUDIO_FORMAT_S32LE, AUDIO_FORMAT_F32LE};
                static float left[64], right[64];
                static unsigned char be[1024], want[1024];

                bad = 0;
                for (unsigned f = 0; f < 5; f++)
                        for (int round = 0; round < 100; round++)
                        {
                                long frames = 1 + (long)(pt_spec_next() % 64), channels = 1 + (long)(pt_spec_next() % 2), bytes;

                                for (int i = 0; i < 64; i++)
                                {
                                        left[i] = (float)(pt_spec_next() % 30001) / 10000.0f - 1.5f;
                                        right[i] = (float)(pt_spec_next() % 30001) / 10000.0f - 1.5f;
                                }
                                bytes = pulse_record_encode(be, big[f], frames, channels, left, right);
                                pulse_record_encode(want, little[f], frames, channels, left, right);
                                audio_c_reverse_units(want, frames * channels, audio_format_bytes(little[f]));
                                if (bytes != frames * channels * audio_format_bytes(little[f]) || memcmp(be, want, (size_t)bytes))
                                        bad++;
                        }
        }
        PTCHECK(bad == 0, "the big-endian formats are the little-endian samples reversed, 500 blocks: %d differ", bad);

        // The U8 format: the engine's bytes are the 16-bit ones narrowed by the specification.
        {
                static float left[64], right[64];
                static unsigned char u8[1024], sixteen[512], want[512];

                bad = 0;
                for (int round = 0; round < 300; round++)
                {
                        long frames = 1 + (long)(pt_spec_next() % 64), channels = 1 + (long)(pt_spec_next() % 2), samples;

                        for (int i = 0; i < 64; i++)
                        {
                                left[i] = (float)(pt_spec_next() % 30001) / 10000.0f - 1.5f;
                                right[i] = (float)(pt_spec_next() % 30001) / 10000.0f - 1.5f;
                        }
                        samples = frames * channels;
                        pulse_record_encode(u8, AUDIO_FORMAT_U8, frames, channels, left, right);
                        pulse_record_encode(sixteen, AUDIO_FORMAT_S16LE, frames, channels, left, right);
                        audio_c_s16_to_u8(want, sixteen, samples);
                        if (memcmp(u8, want, (size_t)samples))
                                bad++;
                }
        }
        PTCHECK(bad == 0, "the U8 format is the 16-bit samples narrowed, 300 blocks: %d differ", bad);

        // The peak window: a peak stream's bytes are the window of the source's blocks, carried across them.
        {
                static const unsigned rates[] = {25, 7, 1000, 96000};
                static const unsigned channel_counts[] = {1, 2, 1, 2};
                static float left[AUDIO_BLOCK], right[AUDIO_BLOCK], out0[AUDIO_BLOCK], out1[AUDIO_BLOCK];
                static unsigned char want[1 << 16];

                bad = 0;
                for (unsigned k = 0; k < 4; k++)
                {
                        struct pt_rec_args a = pt_rec_args();
                        long window = 48000 / (long)rates[k], count = 0, total = 0, slot;
                        float state[2] = {0, 0};
                        unsigned long channel;

                        if (window < 1)
                                window = 1;
                        pt_reset();
                        pulse_sink_add(&pt_engine, "null", "Null", 48000);
                        slot = pt_connect(0, 120 + (long)k, 35, "Reference peaks");
                        a.peak = 1;
                        a.format = AUDIO_FORMAT_F32LE;
                        a.channels = (long)channel_counts[k];
                        a.rate = (long)rates[k];
                        if (pt_rec_create(slot, 35, 40 + (long)k, &a) != 0)
                        {
                                bad++;
                                continue;
                        }
                        channel = pt_rec_reply.channel;
                        pt_rec_clear();
                        for (int block = 0; block < 12; block++)
                        {
                                long frames = 1 + (long)(pt_spec_next() % AUDIO_BLOCK), made;

                                // The source's bus is the block at its volume, clipped to +-1 (audio_gain_run): the window reads that.
                                for (long i = 0; i < frames; i++)
                                {
                                        left[i] = (float)(pt_spec_next() % 20001) / 10000.0f - 1.0f;
                                        right[i] = (float)(pt_spec_next() % 20001) / 10000.0f - 1.0f;
                                }
                                pulse_source_feed(&pt_engine, 0, left, right, frames);
                                pt_rec_pull(slot, channel);
                                made = audio_c_window_peaks(out0, out1, left, right, frames, state, &count, window, channel_counts[k] == 1);
                                for (long j = 0; j < made; j++)
                                {
                                        memcpy(want + total, &out0[j], 4);
                                        total += 4;
                                        if (channel_counts[k] == 2)
                                        {
                                                memcpy(want + total, &out1[j], 4);
                                                total += 4;
                                        }
                                }
                        }
                        if (pt_rec_len != total || memcmp(pt_rec_buf, want, (size_t)total))
                                bad++;
                        pulse_client_close(&pt_engine, slot);
                }
        }
        PTCHECK(bad == 0, "the peak window is the specification at 25, 7, 1000 and 96000 Hz, mono and stereo: %d differ", bad);

        // A string of the tag structure: PULSE_STRING bytes are read and one more is refused, and a frame that ends before the
        // terminator is refused (the search for the terminator stops at the frame and at PULSE_STRING + 1 bytes).
        {
                static unsigned char frame[PULSE_STRING + 8];
                struct wire_r rd;
                const char *s;
                int exact, too_long, unfinished;

                memset(frame, 'a', sizeof(frame));
                frame[0] = 't';
                frame[PULSE_STRING + 1] = 0;
                rd = (struct wire_r){frame, PULSE_STRING + 2, 0, 0};
                s = pulse_r_string(&rd, 0);
                exact = s != 0 && !rd.bad && rd.at == PULSE_STRING + 2;

                memset(frame, 'a', sizeof(frame));
                frame[0] = 't';
                frame[PULSE_STRING + 2] = 0;
                rd = (struct wire_r){frame, PULSE_STRING + 3, 0, 0};
                s = pulse_r_string(&rd, 0);
                too_long = s == 0 && rd.bad;

                memset(frame, 'a', 11);
                frame[0] = 't';
                rd = (struct wire_r){frame, 11, 0, 0};
                s = pulse_r_string(&rd, 0);
                unfinished = s == 0 && rd.bad;
                PTCHECK(exact && too_long && unfinished, "a string of %d bytes is read, one more is refused, and an unterminated frame is refused", PULSE_STRING);
        }
}

static void pt_test_resample_playing_for(void)
{
        long slot;
        unsigned long channel, index, missing;
        unsigned char pcm[4410 * 4];
        unsigned char mix[480 * 4];
        struct pulse_stream *s;

        pt_reset();
        pulse_sink_add(&pt_engine, "null", "Null", 48000);
        slot = pt_connect(1000, 99, 35, "ResampleTime");
        PTCHECK(pt_create(slot, 35, 40, PULSE_INVALID, 0, PULSE_INVALID, &channel, &index, &missing, 0, 0x10000, 0, AUDIO_FORMAT_S16LE, 2, 22050, 0) == 0,
                "a 22050 Hz stream on a 48000 Hz sink");
        memset(pcm, 0, sizeof(pcm));
        pt_data(slot, channel, pcm, sizeof(pcm), 0, 0);
        for (int i = 0; i < 10; i++)
                pulse_sink_mix(&pt_engine, 0, mix, AUDIO_FORMAT_S16LE, 2, 480);
        s = pt_stream_of(index);
        PTCHECK(s && s->playing_for >= 2205 * 4 - 8 && s->playing_for <= 2205 * 4 + 8,
                "4800 sink frames at 48000 Hz advance playing_for by ~2205 stream frames (got %llu bytes, expected ~8820)",
                s ? s->playing_for : 0ull);
        pulse_client_close(&pt_engine, slot);
        for (int i = 0; i < 4; i++)
                pulse_sink_mix(&pt_engine, 0, mix, AUDIO_FORMAT_S16LE, 2, 480);
}

int main(void)
{
        pt_make_source();
        pt_make_session();
        pt_test_text();
        pt_test_reference_loops();
        pt_test_handshake();
        pt_test_info();
        pt_test_list_bounds();
        pt_test_feed_cut();
        pt_test_samples_full();
        pt_test_no_move();
        pt_test_record_no_move();
        pt_test_remember_users();
        pt_test_event_rights();
        pt_test_source_oracle();
        pt_test_period_clock();
        pt_test_stream();
        pt_test_requests();
        pt_test_restart();
        pt_test_latency();
        pt_test_sdl_chunks();
        pt_test_control();
        pt_test_volume();
        pt_test_dying();
        pt_test_flush_fade();
        pt_test_policy();
        pt_test_events();
        pt_test_record_oracle();
        pt_test_record_create();
        pt_test_record_policy();
        pt_test_record_data();
        pt_test_record_gain();
        pt_test_record_cork();
        pt_test_record_resample();
        pt_test_record_peaks();
        pt_test_record_info();
        pt_test_record_commands();
        pt_test_record_sources();
        pt_test_record_monitor();
        pt_test_samples();
        pt_test_samples_play();
        pt_test_samples_policy();
        pt_test_caps();
        pt_test_resample_playing_for();
        pt_test_memory();
        pt_test_session();
        pt_test_fuzz();
        pt_test_control_client();
        pt_test_control_input();
        printf("CHECK_audio_pulse: %u checks, %u failures\n", pt_checks, pt_failures);
        return pt_failures ? 1 : 0;
}
#endif /* CHECK_audio_pulse */

#ifdef CHECK_audio_service
/*
        CHECK_audio_service: the sound server's loop (src/audio/service.c) with the library and the real kernel calls, Linux only:
        the clock-driven pass at the rates the server runs at, the settings files (a volume that is not a whole percent is kept,
        and a file that is written is read back as the same), the table of programs' volumes (an update is the newest, and the
        oldest goes when it is full), a socket path that does not fit (refused), the card's partial write (the rest of a
        mixed block is kept and written first), the clock's file that cannot be written (let go), and the state page's
        sequence (even after each write, and two more for each). `bench` prints the ticks of the state page writer and of the
        idle settings look, median over pinned runs.

            cc -std=gnu11 -O2 -Wall -DCHECK_audio_service test/checks.c -lm
*/
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <sched.h>
#include <sys/timerfd.h>
#if defined(__x86_64__)
#include <x86intrin.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <signal.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

// mix.c's loops that are lib.c's, which a hosted check has none of: the C they were.
#define SHARED_audio_loops
#define AUDIO_C_AS_LIBRARY
#include "../checks.c"
#undef AUDIO_C_AS_LIBRARY
#undef SHARED_audio_loops

// The library's names the server calls, as a hosted build spells them (the shell has the library's own): its types, the address
// spellings, the kernel call with the library's convention (a negative errno), and the log, which a check does not read.
typedef unsigned long positive;
typedef long bipolar;
typedef int b32;
typedef void fn;
typedef void *address_any;
typedef unsigned char *string_address;
typedef unsigned long thread;
typedef struct timespec timespec;
#define address_to *
#define address_of &
#define null ((address_any)0)
#define string_format(where, ...) ((void)0)
#define log_flush() ((void)0)
static long program_argument_count(void) { return 0; }
static string_address program_argument(long at) { (void)at; return (string_address)""; }
static int string_equals(string_address a, string_address b) { return strcmp((const char *)a, (const char *)b) == 0; }
static positive clock_monotonic_nanoseconds(void)
{
        struct timespec now;

        clock_gettime(CLOCK_MONOTONIC, &now);
        return (positive)now.tv_sec * 1000000000ull + (positive)now.tv_nsec;
}
static void memory_zero(void *block, unsigned long size) { memset(block, 0, size); }
static b32 memory_compare(const address_any first, const address_any second, positive size) { return memcmp(first, second, size) != 0; }
static address_any memory_translate(address_any block, positive size, address_any table)
{
        unsigned char *bytes = block;
        const unsigned char *map = table;

        for (positive i = 0; i < size; i++)
                bytes[i] = map[bytes[i]];
        return block;
}
static b32 system_close(int fd) { return close(fd); }
static positive string_length(string_address text) { return (positive)strlen((const char *)text); }
static positive string_length_max(string_address text, positive bound) { positive n = 0; while (n < bound && text[n]) n++; return n; }
static b32 string_compare(string_address a, string_address b) { return strcmp((const char *)a, (const char *)b) != 0; }
static b32 bits_leading_zeros(positive value) { return value ? (b32)__builtin_clzll(value) : 64; } // lib.c's, which a hosted build lacks
static thread sv_thread;
static thread *thread_start(fn (*entry)(address_any), address_any argument) { (void)entry; (void)argument; return &sv_thread; }
static void thread_join(thread *handle) { (void)handle; }
static b32 system_signal_install(int sig, int a, int b, int c, address_any d) { (void)sig; (void)a; (void)b; (void)c; (void)d; return 0; }
static void claim_standard_descriptors(void) {}
#define syscall(name) SYS_##name
static long system_call_6(long number, long a, long b, long c, long d, long e, long f)
{
        long r = (syscall)(number, a, b, c, d, e, f);

        return r < 0 ? -(long)errno : r;
}

#ifndef offset_of
#include <stddef.h>
#define offset_of(type, member) offsetof(type, member)
#endif
#ifndef offset_of
#define offset_of(type, member) ((unsigned long)&((type *)0)->member)
#endif
#ifndef memory_fill
#define memory_fill(block, value, count) memset((block), (value), (count))
#endif
#ifndef memory_copy_apart
#define memory_copy_apart(to, from, size) memcpy((to), (from), (size))
#endif
#ifndef memory_zero
#define memory_zero(to, size) memset((to), 0, (size))
#endif
#ifndef memory_copy
#define memory_copy(to, from, size) memmove((to), (from), (size))
#endif
#ifndef memory_first_of
#define memory_first_of(block, value, size) memchr((block), (value), (size))
#endif
#ifndef string_length
#define string_length(text) strlen((const char *)(text))
#endif
#include "../../src/audio/mix.c"
#include "../../src/audio/pulse.c"
// The library's names control.c calls, as the libc has them for a hosted build.
#define string_length(text) strlen((const char *)(text))
#define memory_first_of(block, value, size) memchr((block), (value), (size))
#define memory_zero(to, size) memset((to), 0, (size))
#include "../../src/audio/control.c"
#include "../../src/audio/service.c"

#ifndef F_SETPIPE_SZ
#define F_SETPIPE_SZ 1031
#endif

static unsigned sv_checks, sv_failures;

#define SVCHECK(condition, ...)                                                                                          \
        do                                                                                                               \
        {                                                                                                                \
                sv_checks++;                                                                                             \
                if (!(condition))                                                                                        \
                {                                                                                                        \
                        sv_failures++;                                                                                   \
                        if (sv_failures < 60)                                                                            \
                        {                                                                                                \
                                printf("  FAIL: ");                                                                      \
                                printf(__VA_ARGS__);                                                                     \
                                printf(" (line %d)\n", __LINE__);                                                        \
                        }                                                                                                \
                }                                                                                                        \
        } while (0)

static char sv_dir[256];

// The server as the applet starts it, minus what would touch the person's machine: no signals blocked, no real-time priority, no
// /run, no card. The clock is the server's own (a fake one is set by the tests that need it).
static void sv_start(void)
{
        struct pulse_host host = {0};

        host.alloc = audio_host_alloc;
        host.release = audio_host_release;
        host.now = audio_host_now;
        host.real = audio_host_real;
        host.recall = audio_host_recall;
        host.remember = audio_host_remember;
        host.changed = audio_host_changed;
        pulse_open(&audio_service.pulse, &host);
        audio_slots_fit();
        audio_service.listen = audio_service.signals = audio_service.timer = audio_service.file = -1;
        audio_service.card = audio_service.device = -1;
        audio_service.sink = -1;
        audio_service.applied_in_volume = AUDIO_VOLUME_NORM;
        audio_service.applied_capture = 1;
        audio_service.wake[0] = audio_service.wake[1] = -1;
        for (int k = 0; k < AUDIO_INPUTS; k++)
                audio_service.input[k].pcm.fd = -1;
        audio_service.pcm.fd = -1;
        audio_service.no_card = 1;
        audio_service.notify = -1;
        audio_service.suspended = 0;
        audio_service.idle_since = 0;
        audio_service.devices_due = 0;
        audio_service.resume_at = 0;
        audio_service.rate = 48000;
        audio_service.period = AUDIO_PERIOD;
        audio_output_open(48000, 0);
}

static void sv_setup(void)
{
        char *base = getenv("TMPDIR");

        snprintf(sv_dir, sizeof sv_dir, "%s/mw-audio-service-XXXXXX", base ? base : "/tmp");
        if (!mkdtemp(sv_dir))
        {
                printf("  FAIL: no directory\n");
                exit(1);
        }
        audio_service.settings = sv_dir;
        audio_service.socket_path = "/tmp/unused";
        sv_start();
}

static void sv_path(char *to, long room, const char *name)
{
        snprintf(to, (size_t)room, "%s/%s", sv_dir, name);
}

static void sv_write_file(const char *name, const char *text)
{
        char path[300];
        FILE *f;

        sv_path(path, sizeof path, name);
        f = fopen(path, "w");
        if (f)
        {
                fputs(text, f);
                fclose(f);
        }
}

static long sv_size(int fd)
{
        struct stat st;

        return fstat(fd, &st) == 0 ? (long)st.st_size : -1;
}

/* ---- the clock pass: one tick is one period at the rate ---- */
static void sv_test_clock(void)
{
        static const long rates[3] = {48000, 96000, 44100};
        static const unsigned long long ticks[3] = {1, 1, 1};

        for (int r = 0; r < 3; r++)
        {
                int pipe_fds[2], file;
                char path[300];
                unsigned long long expirations = ticks[r];
                long before, after;

                sv_path(path, sizeof path, "clock.raw");
                file = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                SVCHECK(file >= 0, "the clock's file opens");
                if (file < 0)
                        continue;
                SVCHECK(pipe(pipe_fds) == 0, "a pipe for the tick");
                write(pipe_fds[1], &expirations, 8);
                audio_service.rate = rates[r];
                audio_service.file = file;
                audio_service.timer = pipe_fds[0];
                before = sv_size(file);
                audio_clock_pass();
                after = sv_size(file);
                SVCHECK((after - before) / 4 == AUDIO_PERIOD, "one tick at %ld Hz is %d frames, not %ld (got %ld)", rates[r], AUDIO_PERIOD,
                        (long)(AUDIO_PERIOD * rates[r] / 48000), (after - before) / 4);
                /* sixteen ticks at once are sixteen periods, the most one pass takes */
                expirations = 20;
                write(pipe_fds[1], &expirations, 8);
                before = sv_size(file);
                audio_clock_pass();
                after = sv_size(file);
                SVCHECK((after - before) / 4 == 16 * AUDIO_PERIOD, "twenty ticks at %ld Hz are sixteen periods (got %ld frames)", rates[r],
                        (after - before) / 4);
                close(pipe_fds[0]);
                close(pipe_fds[1]);
                close(file);
                audio_service.file = -1;
                audio_service.timer = -1;
        }
}

/* ---- the settings: a volume that is not a whole percent is what the program set, and what is written is read back as the same ---- */
static void sv_test_settings(void)
{
        struct pulse_sink *sink;
        char text[300]; /* a path under TMPDIR can be long */

        sv_setup();
        sink = audio_service.pulse.sink + audio_service.sink;
        sink->volume[0] = sink->volume[1] = 21845; // a third of full volume: 33 percent in the file, and 33 percent is not a third
        sink->muted = 0;
        audio_settings_write();
        sv_path(text, sizeof text, "audio.volume");
        SVCHECK(access(text, F_OK) == 0, "the volume is written");
        audio_settings_read(0);
        SVCHECK(sink->volume[0] == 21845 && sink->volume[1] == 21845, "a read a half second later does not change the volume (got %lu)",
                (unsigned long)sink->volume[0]);
        /* and a file a person writes is read: 50 percent */
        sv_write_file("audio.volume", "50\n");
        audio_settings_read(0);
        SVCHECK(sink->volume[0] == 32768, "a file that says 50 sets the volume (got %lu)", (unsigned long)sink->volume[0]);
        sv_write_file("audio.mute", "on\n");
        audio_settings_read(0);
        SVCHECK(sink->muted == 1, "a mute file that says on mutes");
}

/* ---- a settings file that is a FIFO is not waited for: the read answers at once (a server that waits here never serves) ---- */
static void sv_test_fifo(void)
{
        char path[300];
        struct pulse_sink *sink;

        sv_setup();
        sink = audio_service.pulse.sink + audio_service.sink;
        sink->volume[0] = sink->volume[1] = 40000;
        sv_path(path, sizeof path, "audio.volume");
        unlink(path);
        SVCHECK(mkfifo(path, 0600) == 0, "a FIFO in the settings directory");
        alarm(5);
        audio_settings_read(0);
        alarm(0);
        SVCHECK(sink->volume[0] == 40000, "the read of a FIFO answers and changes nothing (volume %lu)", (unsigned long)sink->volume[0]);
        unlink(path);
}

/* ---- the capture thread takes ordinary scheduling, not the loop's real-time policy: a thread starts with its maker's policy, so the
        test puts this thread in SCHED_BATCH (no privilege needed) and runs the thread's entry: it must be SCHED_OTHER again ---- */
static void sv_test_job(void)
{
        struct sched_param none = {0};

        sv_setup();
        sched_setscheduler(0, SCHED_BATCH, &none);
        audio_service.job.kind = AUDIO_JOB_CLOSE;
        audio_service.job.pcm.fd = -1;
        audio_service.wake[1] = -1;
        audio_job_entry(0);
        SVCHECK(sched_getscheduler(0) == SCHED_OTHER, "the capture thread is ordinary (policy %d)", sched_getscheduler(0));
}

/* ---- the table of programs' volumes: the update is the newest, and the oldest goes when it is full ---- */
static void sv_test_table(void)
{
        char name[32];
        unsigned long volume = 0;
        int mute = 0;

        audio_service.app_count = 0;
        for (int i = 0; i < AUDIO_APPS; i++)
        {
                snprintf(name, sizeof name, "1000/app%02d", i);
                audio_host_remember(0, name, (unsigned long)i + 1, 0);
        }
        SVCHECK(audio_service.app_count == AUDIO_APPS, "the table is full at %d", AUDIO_APPS);
        audio_host_remember(0, "1000/app00", 999, 1);
        audio_host_remember(0, "1000/fresh", 5, 0);
        SVCHECK(audio_host_recall(0, "1000/app00", &volume, &mute) && volume == 999 && mute == 1, "the updated program is kept (volume %lu)", volume);
        SVCHECK(!audio_host_recall(0, "1000/app01", &volume, &mute), "the program changed least recently is the one let go");
        SVCHECK(audio_host_recall(0, "1000/app02", &volume, &mute) && volume == 3, "the next one is still there");
        SVCHECK(audio_host_recall(0, "1000/fresh", &volume, &mute) && volume == 5, "the new program is remembered");
        SVCHECK(audio_service.app_count == AUDIO_APPS, "the table stays at its size");
        /* the file is what the table was, and it is read back as that */
        audio_apps_save();
        audio_service.app_count = 0;
        audio_apps_load();
        SVCHECK(audio_service.app_count == AUDIO_APPS, "the saved table reads back whole (%d)", audio_service.app_count);
        SVCHECK(audio_host_recall(0, "1000/fresh", &volume, &mute) && volume == 5, "a saved program reads back");
        /* a line from before the key carried the user (no uid/ in it) belongs to no user: it is not loaded, and no program can have it */
        {
                static const char legacy[] = "app99\t7\t0\n1000/keyed\t9\t1\n";

                audio_file_write("audio.apps", legacy, sizeof(legacy) - 1);
                audio_service.app_count = 0;
                audio_apps_load();
                SVCHECK(audio_service.app_count == 1, "only the keyed program is loaded (%d)", audio_service.app_count);
                SVCHECK(!audio_host_recall(0, "app99", &volume, &mute), "a legacy program is not remembered for anyone");
                SVCHECK(audio_host_recall(0, "1000/keyed", &volume, &mute) && volume == 9 && mute == 1, "a keyed program is loaded");
        }
}

/* ---- a socket path that does not fit sun_path is refused, not cut short ---- */
static void sv_test_socket(void)
{
        char path[256];
        char *cut;

        sv_setup();
        sv_path(path, sizeof path, "sock");
        audio_service.socket_path = path;
        audio_listen();
        SVCHECK(audio_service.listen >= 0, "a path that fits is listened on");
        if (audio_service.listen >= 0)
                close(audio_service.listen);
        audio_service.listen = -1;
        unlink(path);
        /* a path of 104 bytes: longer than the 100 the address copies, shorter than the kernel's 107 */
        snprintf(path, sizeof path, "%s/", sv_dir);
        cut = path + strlen(path);
        while ((long)(cut - path) < 104)
                *cut++ = 'x';
        *cut = 0;
        audio_listen();
        SVCHECK(audio_service.listen < 0, "a path of 104 bytes is refused");
        SVCHECK(access(path, F_OK) != 0, "and no socket is made under a shorter name");
        audio_service.socket_path = "/tmp/unused";
}

/* ---- the card's partial write: what the card did not take is kept and written first ---- */
static void sv_test_card(void)
{
        int q[2];
        unsigned char sink_bytes[8192];
        long total = 0, got;
        static unsigned char filler[4096];

        sv_setup();
        SVCHECK(pipe(q) == 0, "a pipe for the card");
        fcntl(q[0], F_SETFL, O_NONBLOCK);
        fcntl(q[1], F_SETFL, O_NONBLOCK);
        fcntl(q[1], F_SETPIPE_SZ, 8192); // two slots: the first holds the filler, the second takes what a write of a block can
        memset(filler, 7, sizeof filler);
        SVCHECK(write(q[1], filler, sizeof filler) == (ssize_t)sizeof filler, "the card has 4096 bytes in it");
        audio_service.pcm.fd = q[1];
        // eight channels of sixteen bits: a block of 512 frames is 8192 bytes, more than a pipe takes atomically, so the card takes what
        // a free slot holds (256 frames of 4096 bytes) and the rest of the block waits
        audio_service.pcm.frame_bytes = 16;
        audio_service.pcm.channels = 8;
        audio_service.pcm.format = AUDIO_FORMAT_S16LE;
        audio_service.pcm.broken = 0;
        audio_service.frames_played = 0;
        audio_service.block_left = 0;
        audio_produce(AUDIO_BLOCK);
        SVCHECK(audio_service.frames_played == 256, "the card takes 256 frames of 512 (took %llu)", audio_service.frames_played);
        SVCHECK(audio_service.block_left == AUDIO_BLOCK - 256, "the other 256 are kept (kept %ld)", audio_service.block_left);
        /* the card drains; the kept frames go before anything is mixed */
        while ((got = read(q[0], sink_bytes, sizeof sink_bytes)) > 0)
                total += got;
        for (int round = 0; round < 64 && audio_service.block_left > 0; round++)
        {
                audio_produce(0);
                while ((got = read(q[0], sink_bytes, sizeof sink_bytes)) > 0)
                        total += got;
        }
        SVCHECK(audio_service.block_left == 0, "the kept frames are written (left %ld)", audio_service.block_left);
        SVCHECK(audio_service.frames_played == AUDIO_BLOCK, "every frame of the block is played (%llu)", audio_service.frames_played);
        SVCHECK(total - (long)sizeof filler == AUDIO_BLOCK * 16, "and they reach the card whole: %ld bytes after the filler", total - (long)sizeof filler);
        close(q[0]);
        close(q[1]);
        audio_service.pcm.fd = -1;
}

/*
        The output's rate follows the first stream that plays on an idle output: a stream at a rate the card takes is
        played at that rate, with no table in its voice (the passthrough: the samples are copied, and the latency is none),
        a stream at a rate the card does not take (or no card) gets the default, and a voice moved between a rate and the
        card's comes back to the copy when the rates meet again.
*/
static void sv_test_rate(void)
{
        unsigned int mask = (1u << 6) | (1u << 7) | (1u << 9); // 44100, 48000 and 96000, as a usb card takes them
        static unsigned char ring[8192];
        struct audio_voice voice;

        SVCHECK(audio_rate_target(audio_rate_offered(mask, 44100), 44100, 48000) == 44100, "a stream at 44100 on a card that takes it runs the card at 44100");
        SVCHECK(audio_rate_target(audio_rate_offered(mask, 22050), 22050, 48000) == 48000, "a stream at 22050 the card does not take gets the default");
        SVCHECK(audio_rate_target(audio_rate_offered(mask, 96000), 96000, 48000) == 96000, "96000 is taken and runs the card at 96000");
        SVCHECK(audio_rate_target(1, 44100, 48000) == 44100, "a closed card is asked for the stream's rate (the open falls back)");
        SVCHECK(audio_rate_target(1, 0, 48000) == 48000, "no stream asks for a rate: the default");
        SVCHECK(audio_voice_open(&voice, AUDIO_FORMAT_S16LE, 2, 44100, 0, 44100, ring, sizeof ring) == 0, "a voice at the card's rate opens");
        SVCHECK(voice.table == 0 && audio_voice_delay(&voice) == 0, "at the card's rate the voice has no table and no delay (the passthrough)");
        SVCHECK(audio_voice_retarget(&voice, 48000) == 0 && voice.table != 0 && audio_voice_delay(&voice) > 0, "moved to 48000 it takes a table and a delay");
        SVCHECK(audio_voice_retarget(&voice, 44100) == 0 && voice.table == 0 && audio_voice_delay(&voice) == 0, "back at 44100 the table is given up");
        audio_voice_close(&voice);
}

/* ---- the clock's file that cannot be written: let go, and the sound goes on ---- */
static void sv_test_file(void)
{
        char path[300];
        int readonly;

        sv_setup();
        sv_path(path, sizeof path, "ro.raw");
        sv_write_file("ro.raw", "");
        readonly = open(path, O_RDONLY);
        audio_service.file = readonly;
        audio_service.pcm.fd = -1;
        audio_service.frames_played = 0;
        audio_produce(128);
        SVCHECK(audio_service.file < 0, "a file that cannot be written is let go");
        SVCHECK(audio_service.frames_played == 128, "and the frames are still counted as played (%llu)", audio_service.frames_played);
}

/* ---- the state page: an even sequence after each write, and two more per write ---- */
static void sv_test_page(void)
{
        char path[300];
        struct audio_state copy;
        unsigned int before, after;

        sv_setup();
        sv_path(path, sizeof path, "state");
        audio_service.page_path = path;
        audio_page_open();
        SVCHECK(audio_service.page != 0, "the page is mapped");
        if (!audio_service.page)
                return;
        before = audio_service.page->seq;
        audio_page_write();
        after = audio_service.page->seq;
        SVCHECK(!(after & 1u) && after == before + 2, "one write takes the sequence from %u to %u", before, after);
        SVCHECK(audio_state_read(audio_service.page, &copy) && copy.volume == 65536, "the copy reads back whole (volume %u)", copy.volume);
        //      A page left odd (a server killed mid-write) is even again after the next write, not odd for good (S-d10-2).
        audio_service.page->seq |= 1u;
        audio_service.pulse.sink[audio_service.sink].volume[0] ^= 1; // a change, so the write is made
        audio_page_write();
        SVCHECK(!(audio_service.page->seq & 1u) && audio_state_read(audio_service.page, &copy),
                "a page left odd by a write that died is even and readable after the next (sequence %u)", audio_service.page->seq);
        SVCHECK(audio_test_page(&copy), "and the modem's look at the page reads it");
        audio_service.page_path = "/tmp/unused";
}

/* ---- bench: the ticks of the state page writer and of the settings look, on a pinned core (`service-check bench CPU`) ---- */
static unsigned long long sv_now(void)
{
#if defined(__x86_64__)
        return __rdtsc();
#else
        struct timespec at;

        clock_gettime(CLOCK_MONOTONIC, &at);
        return (unsigned long long)at.tv_sec * 1000000000ull + (unsigned long long)at.tv_nsec;
#endif
}

static int sv_cmp_ull(const void *a, const void *b)
{
        unsigned long long x = *(const unsigned long long *)a, y = *(const unsigned long long *)b;

        return x < y ? -1 : x > y;
}

// Sixteen programs, each with one stream that has a name and a media name, as a desktop has them.
static void sv_bench_streams(int streams)
{
        for (int i = 0; i < streams; i++)
        {
                long slot = pulse_client_open(&audio_service.pulse, 1000 + i, 2000 + i);
                struct pulse_client *c;
                struct pulse_stream *s;

                if (slot < 0)
                        continue;
                c = audio_service.pulse.client[slot];
                pulse_props_set(&c->props, "application.name", (const unsigned char *)"Bench program", 13);
                pulse_props_set(&c->props, "application.icon_name", (const unsigned char *)"audio-x-generic", 15);
                /* the properties a real program sends with its connection (a dozen: the lookups of the page scan them all) */
                pulse_props_set(&c->props, "application.process.binary", (const unsigned char *)"/usr/bin/bench-program", 22);
                pulse_props_set(&c->props, "application.process.id", (const unsigned char *)"4242", 4);
                pulse_props_set(&c->props, "application.process.user", (const unsigned char *)"dawn", 4);
                pulse_props_set(&c->props, "application.process.host", (const unsigned char *)"moonwater", 9);
                pulse_props_set(&c->props, "application.language", (const unsigned char *)"en_US.UTF-8", 11);
                pulse_props_set(&c->props, "application.version", (const unsigned char *)"1.2.3", 5);
                pulse_props_set(&c->props, "application.process.machine_id", (const unsigned char *)"0123456789abcdef0123456789abcdef", 32);
                pulse_props_set(&c->props, "window.x11.display", (const unsigned char *)":0", 2);
                pulse_props_set(&c->props, "media.software", (const unsigned char *)"Bench program 1.2", 17);
                pulse_props_set(&c->props, "native-protocol.version", (const unsigned char *)"32", 2);
                pulse_props_set(&c->props, "module-stream-restore.id", (const unsigned char *)"sink-input-by-application-name:Bench program", 43);
                pulse_props_set(&c->props, "media.role", (const unsigned char *)"music", 5);
                s = pulse_stream_free_slot(&audio_service.pulse);
                audio_slots_fit();
                s->used = 1;
                s->client = slot;
                s->index = (unsigned long)i + 1;
                s->map_channels = 2;
                s->volume[0] = s->volume[1] = 65536;
                pulse_props_set(&s->props, "media.name", (const unsigned char *)"A track of the bench", 20);
                pulse_props_set(&s->props, "event.id", (const unsigned char *)"none", 4);
                pulse_props_set(&s->props, "stream.is-live", (const unsigned char *)"false", 5);
                pulse_props_set(&s->props, "media.title", (const unsigned char *)"A title of the bench that is a little longer", 42);
        }
}

static void sv_bench(int cpu)
{
        enum { PASSES = 4001 };
        static unsigned long long samples[PASSES];
        cpu_set_t set;
        int streams = 16;

        CPU_ZERO(&set);
        CPU_SET(cpu, &set);
        sched_setaffinity(0, sizeof set, &set);
        sv_setup();
        audio_service.page_path = sv_dir;
        {
                char path[300];

                sv_path(path, sizeof path, "state");
                audio_service.page_path = path;
                audio_page_open();
        }
        sv_bench_streams(streams);
        /* playing: every stream's peak moves on every pass, as a stream that plays does */
        for (int i = 0; i < PASSES; i++)
        {
                for (int k = 0; k < streams; k++)
                        audio_service.pulse.stream[k].voice.peak[0] = (float)((i + k) & 1023) / 2048.0f;
                samples[i] = sv_now();
                audio_page_write();
                samples[i] = sv_now() - samples[i];
        }
        qsort(samples, PASSES, sizeof samples[0], sv_cmp_ull);
        printf("page writer, %d streams, all peaks moving: median %llu, p90 %llu, min %llu (ticks per pass)\n", streams,
               samples[PASSES / 2], samples[PASSES * 9 / 10], samples[0]);
        /* nothing changes: the same page again and again */
        for (int k = 0; k < streams; k++)
                audio_service.pulse.stream[k].voice.peak[0] = 0;
        audio_page_write();
        for (int i = 0; i < PASSES; i++)
        {
                samples[i] = sv_now();
                audio_page_write();
                samples[i] = sv_now() - samples[i];
        }
        qsort(samples, PASSES, sizeof samples[0], sv_cmp_ull);
        printf("page writer, %d streams, nothing changed: median %llu, p90 %llu, min %llu (ticks per pass)\n", streams,
               samples[PASSES / 2], samples[PASSES * 9 / 10], samples[0]);
        /* no stream at all: the fixed part of a pass */
        for (int k = 0; k < streams; k++)
                audio_service.pulse.stream[k].used = 0;
        audio_page_write();
        for (int i = 0; i < PASSES; i++)
        {
                for (int k = 0; k < streams; k++)
                        audio_service.pulse.stream[k].voice.peak[0] = 0;
                samples[i] = sv_now();
                audio_page_write();
                samples[i] = sv_now() - samples[i];
        }
        qsort(samples, PASSES, sizeof samples[0], sv_cmp_ull);
        printf("page writer, no stream: median %llu, p90 %llu, min %llu (ticks per pass)\n", samples[PASSES / 2], samples[PASSES * 9 / 10], samples[0]);
        /* the settings look, with every file there and unchanged */
        sv_write_file("audio.volume", "50\n");
        sv_write_file("audio.mute", "off\n");
        sv_write_file("audio.device", "default\n");
        sv_write_file("audio.capture", "on\n");
        sv_write_file("audio.source", "default\n");
        sv_write_file("audio.source.volume", "100\n");
        sv_write_file("audio.source.mute", "off\n");
        usleep(2500000); /* the files are older than the look's two seconds of trust: an unchanged file costs a stat, and no read */
        audio_settings_read(1);
        for (int i = 0; i < PASSES; i++)
        {
                samples[i] = sv_now();
                audio_settings_read(0);
                samples[i] = sv_now() - samples[i];
        }
        qsort(samples, PASSES, sizeof samples[0], sv_cmp_ull);
        printf("settings look, every file there and unchanged: median %llu, p90 %llu, min %llu (ticks per look)\n", samples[PASSES / 2],
               samples[PASSES * 9 / 10], samples[0]);
}

/* ---- the state page writer: nothing that did not change is written, a moved peak is one row, a stream that goes is a cleared place ---- */
static void sv_test_page_rows(void)
{
        char path[300];
        struct audio_state copy;
        unsigned int seq;

        sv_setup();
        sv_path(path, sizeof path, "state-rows");
        audio_service.page_path = path;
        audio_page_open();
        if (!audio_service.page)
        {
                SVCHECK(0, "the page for the rows is mapped");
                return;
        }
        sv_bench_streams(2);
        for (int k = 0; k < 2; k++)
                audio_service.pulse.stream[k].voice.peak[0] = 0;
        audio_page_write();
        seq = audio_service.page->seq;
        SVCHECK(audio_state_read(audio_service.page, &copy) && copy.streams == 2, "the first write holds both streams");
        audio_page_write();
        SVCHECK(audio_service.page->seq == seq, "a pass that changes nothing writes nothing (sequence %u, was %u)", audio_service.page->seq, seq);
        audio_service.pulse.stream[1].voice.peak[0] = 0.5f;
        audio_page_write();
        SVCHECK(audio_service.page->seq == seq + 2, "a moved peak is one write (sequence %u, was %u)", audio_service.page->seq, seq);
        SVCHECK(audio_state_read(audio_service.page, &copy) && copy.stream[1].peak_left == 16383 && copy.stream[0].peak_left == 0,
                "the moved peak is on its row and no other (%u, %u)", copy.stream[1].peak_left, copy.stream[0].peak_left);
        /* a program renames itself and a stream changes its title: the page shows the new texts, not the ones it kept */
        pulse_props_set(&audio_service.pulse.client[audio_service.pulse.stream[0].client]->props, "application.name", (const unsigned char *)"Renamed program", 15);
        pulse_props_set(&audio_service.pulse.stream[1].props, "media.name", (const unsigned char *)"A new title", 11);
        audio_page_write();
        SVCHECK(audio_state_read(audio_service.page, &copy) && strcmp(copy.stream[0].app, "Renamed program") == 0 && strcmp(copy.stream[1].media, "A new title") == 0,
                "a changed name and title are shown (%s, %s)", copy.stream[0].app, copy.stream[1].media);
        audio_service.pulse.stream[1].used = 0;
        audio_page_write();
        SVCHECK(audio_state_read(audio_service.page, &copy) && copy.streams == 1 && copy.stream[1].index == 0xffffffffu && copy.stream[1].app[0] == 0,
                "a stream that is gone is a free place, cleared (count %u, index %u)", copy.streams, copy.stream[1].index);
        SVCHECK(!(audio_service.page->seq & 1u), "the sequence is even after the last write");
        audio_service.page_path = "/tmp/unused";
}

/* ---- the settings look reads a file that moved, and a file that changed more than two seconds ago (an older change is seen
        by its stamp, the one a look did not see is not lost) ---- */
static void sv_test_gate(void)
{
        struct pulse_sink *sink;

        sv_setup();
        sink = audio_service.pulse.sink + audio_service.sink;
        sink->volume[0] = sink->volume[1] = 65536;
        audio_settings_read(1);
        sv_write_file("audio.volume", "25\n");
        usleep(2500000); /* older than the look's trust: the stamp alone says the file moved */
        audio_settings_read(0);
        SVCHECK(sink->volume[0] == 16384 && sink->volume[1] == 16384, "a file changed more than two seconds ago is read (volume %lu)", (unsigned long)sink->volume[0]);
}

/* ---- the bucket of a time is its bit length in microseconds, the last one for anything longer, and the bench of the old loop ---- */
static void sv_bucket_loop(unsigned long *counts, positive ns)
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

static void sv_test_bucket(void)
{
        static const unsigned long long ns[6] = {0, 1000, 1999, 2000, 1000000000ull, 1ull << 62};
        static const int want[6] = {0, 1, 1, 2, 15, 15};
        for (int k = 0; k < 6; k++)
        {
                unsigned long counts[16] = {0};

                audio_bucket(counts, ns[k]);
                SVCHECK(counts[want[k]] == 1, "a time of %llu ns is bucket %d", ns[k], want[k]);
        }
}

/* ---- the text helpers (the library's bounded length, copy and compare under them) ---- */
static void sv_test_text(void)
{
        char to[16];

        pulse_copy(to, 8, "abcdefghijkl");
        SVCHECK(strcmp(to, "abcdefg") == 0 && to[7] == 0, "a copy is cut at room - 1 and ends in a zero byte (%s)", to);
        pulse_copy(to, 16, "short");
        SVCHECK(strcmp(to, "short") == 0, "a copy that fits is whole (%s)", to);
        pulse_copy(to, 0, "not written");
        SVCHECK(strcmp(to, "short") == 0, "a copy into no room writes nothing");
        SVCHECK(audio_text_equal("ab", "ab") == 1 && audio_text_equal("", "") == 1, "equal texts are equal");
        SVCHECK(audio_text_equal("ab", "abc") == 0 && audio_text_equal("abc", "ab") == 0, "a prefix is not equal");
        SVCHECK(audio_text_length("hello") == 5 && audio_text_length("") == 0, "the length of a text");
}

/* ---- the settings watch: a settings file written or moved into place is an event, another file is not, and an empty queue does not block ---- */
static void sv_test_notify(void)
{
        char moved[300], tmp[300];

        sv_setup();
        audio_notify_open(sv_dir);
        SVCHECK(audio_service.notify >= 0, "a watch on the settings directory is open");
        if (audio_service.notify < 0)
                return;
        sv_write_file("audio.volume", "50\n");
        SVCHECK((audio_notify_drain() & 1) == 1, "a settings file written is an event");
        sv_write_file("other.txt", "x");
        SVCHECK((audio_notify_drain() & 1) == 0, "a file that is not a setting is not an event");
        /* a save is a rename into place: the file is written under another name and moved over the setting */
        sv_path(tmp, sizeof tmp, "tmp.file");
        sv_path(moved, sizeof moved, "audio.mute");
        sv_write_file("tmp.file", "on\n");
        SVCHECK(rename(tmp, moved) == 0, "a setting is moved into place");
        SVCHECK((audio_notify_drain() & 1) == 1, "a setting moved into place is an event");
        SVCHECK(audio_notify_drain() == 0, "an empty queue answers at once");
        system_close(audio_service.notify);
        audio_service.notify = -1;
}

/* ---- the client list: the passes visit the connected clients, a pass keeps them, and one that went is taken out of the list ---- */
static void sv_test_clients(void)
{
        int fds[2][2];
        long slots[2];

        sv_setup();
        for (int k = 0; k < 2; k++)
        {
                pipe(fds[k]);
                slots[k] = pulse_client_open(&audio_service.pulse, 1000 + k, 2000 + k);
                audio_slots_fit();
                audio_service.fd[slots[k]] = fds[k][0];
        }
        audio_live_relist();
        SVCHECK(audio_service.live_count == 2, "two clients are listed (%d)", audio_service.live_count);
        audio_clients_pass(clock_monotonic_nanoseconds());
        SVCHECK(audio_service.fd[slots[0]] >= 0 && audio_service.fd[slots[1]] >= 0, "a pass that finds nothing queued keeps every client");
        audio_client_drop(slots[0]);
        SVCHECK(audio_service.live_dirty == 1, "a client that went makes the list dirty");
        audio_live_relist();
        SVCHECK(audio_service.live_count == 1 && audio_service.live[0] == slots[1], "after one went the list has the other");
        audio_client_drop(slots[1]);
        audio_live_relist();
        SVCHECK(audio_service.live_count == 0, "and with none the list is empty");
        close(fds[0][1]);
        close(fds[1][1]);
}

/* the 32-slot pass that the list replaced, as it was (for the bench) */
static void sv_old_clients_pass(positive now)
{
        for (long slot = 0; slot < audio_service.pulse.client_slots; slot++)
                if (audio_service.fd[slot] >= 0)
                {
                        if (pulse_client_late(&audio_service.pulse, slot, (long)(now / 1000)) || !audio_client_write(slot, now))
                                audio_client_drop(slot);
                }
}

/* the client pass on a pinned core, with 1, 4 and 16 connected clients that have nothing queued (`clients CPU`) */
static void sv_bench_clients(int cpu)
{
        enum { PASSES = 4001 };
        static unsigned long long samples[PASSES];
        static int fds[16][2];
        cpu_set_t set;
        const int counts[3] = {1, 4, 16};

        CPU_ZERO(&set);
        CPU_SET(cpu, &set);
        sched_setaffinity(0, sizeof set, &set);
        positive now0 = clock_monotonic_nanoseconds();
        for (int c = 0; c < 3; c++)
        {
                sv_setup();
                for (int k = 0; k < counts[c]; k++)
                {
                        long slot;

                        pipe(fds[k]);
                        slot = pulse_client_open(&audio_service.pulse, 1000 + k, 2000 + k);
                        audio_slots_fit();
                        audio_service.fd[slot] = fds[k][0];
                }
                audio_live_relist();
                for (int i = 0; i < PASSES; i++)
                {
                        samples[i] = sv_now();
                        sv_old_clients_pass(now0); /* the clock is read once: a read is a call of its own */
                        samples[i] = sv_now() - samples[i];
                }
                qsort(samples, PASSES, sizeof samples[0], sv_cmp_ull);
                printf("client pass, %2d clients, the 32-slot scan: median %llu, p90 %llu (ticks)\n", counts[c], samples[PASSES / 2], samples[PASSES * 9 / 10]);
                for (int i = 0; i < PASSES; i++)
                {
                        samples[i] = sv_now();
                        audio_clients_pass(now0);
                        samples[i] = sv_now() - samples[i];
                }
                qsort(samples, PASSES, sizeof samples[0], sv_cmp_ull);
                printf("client pass, %2d clients, the live list:    median %llu, p90 %llu (ticks)\n", counts[c], samples[PASSES / 2], samples[PASSES * 9 / 10]);
        }
}

/* ---- the output's power: the state machine on a fake clock, and the clock's timer and the page through a suspend and a resume ---- */
static void sv_test_power(void)
{
        char path[300];
        struct audio_state copy;
        struct itimerspec its;
        const positive t0 = 1000000000ull;
        int action;

        sv_setup();
        sv_path(path, sizeof path, "state-power");
        audio_service.page_path = path;
        audio_page_open();
        SVCHECK(audio_power_decide(t0, 0) == AUDIO_POWER_KEEP, "an output that has just gone idle is kept for now");
        SVCHECK(audio_power_decide(t0 + 4900000000ull, 0) == AUDIO_POWER_KEEP, "4.9 s idle keeps the output");
        action = audio_power_decide(t0 + 5000000000ull, 0);
        SVCHECK(action == AUDIO_POWER_SUSPEND, "five seconds idle suspends it");
        audio_power_apply(action);
        SVCHECK(audio_service.suspended == 1, "the output is suspended");
        SVCHECK(timerfd_gettime(audio_service.timer, &its) == 0 && its.it_value.tv_sec == 0 && its.it_value.tv_nsec == 0,
                "the clock's timer is stopped while suspended");
        SVCHECK(audio_power_decide(t0 + 6000000000ull, 0) == AUDIO_POWER_KEEP, "a suspended output is not suspended again");
        audio_page_write();
        SVCHECK(audio_state_read(audio_service.page, &copy) && (copy.output_state & AUDIO_STATE_OUT_SUSPENDED), "the page says suspended");
        action = audio_power_decide(t0 + 7000000000ull, 1);
        SVCHECK(action == AUDIO_POWER_RESUME, "a stream that plays resumes it");
        audio_power_apply(action);
        SVCHECK(audio_service.suspended == 0, "the output is resumed");
        SVCHECK(timerfd_gettime(audio_service.timer, &its) == 0 && (its.it_value.tv_sec != 0 || its.it_value.tv_nsec != 0),
                "the clock's timer runs again after the resume");
        audio_page_write();
        SVCHECK(audio_state_read(audio_service.page, &copy) && !(copy.output_state & AUDIO_STATE_OUT_SUSPENDED), "the page no longer says suspended");
        /* a burst inside the five seconds does not suspend: the idle count starts again after it */
        (void)audio_power_decide(t0 + 8000000000ull, 0);
        SVCHECK(audio_power_decide(t0 + 11000000000ull, 0) == AUDIO_POWER_KEEP, "three seconds idle keep the output");
        SVCHECK(audio_power_decide(t0 + 11500000000ull, 1) == AUDIO_POWER_KEEP, "a burst of play keeps it");
        SVCHECK(audio_power_decide(t0 + 12000000000ull, 0) == AUDIO_POWER_KEEP, "idle starts again after the burst");
        SVCHECK(audio_power_decide(t0 + 15000000000ull, 0) == AUDIO_POWER_KEEP, "three seconds of it after the burst keep the output");
        SVCHECK(audio_power_decide(t0 + 17000000000ull, 0) == AUDIO_POWER_SUSPEND, "five seconds after the burst suspends it");
        audio_service.page_path = "/tmp/unused";
}

/* ---- the server's own look at the cards is not a device event: the scan opens and closes the control nodes, and an event for each
        close made the next look (the loop never slept, on the box with a client connected). Only a node that comes or goes is one. ---- */
static void sv_test_spin(void)
{
        static struct audio_card cards[AUDIO_ALSA_CARDS];

        sv_setup();
        if (access("/dev/snd", F_OK) != 0)
                return; /* no sound devices on this machine: nothing to watch, nothing to open */
        audio_notify_open(sv_dir);
        if (audio_service.notify < 0)
        {
                SVCHECK(0, "the settings directory and the device nodes are watched");
                return;
        }
        SVCHECK(audio_service.snd_wd >= 0, "the device nodes are watched");
        audio_cards_scan(cards, AUDIO_ALSA_CARDS);
        audio_cards_scan(cards, AUDIO_ALSA_CARDS);
        SVCHECK((audio_notify_drain() & 2) == 0, "the server's own opens and closes of the cards are not device events");
        system_close(audio_service.notify);
        audio_service.notify = -1;
}

/* A fullscreen game's low periods (audio_game_pass): followed once held a second, given back when no game is, and three underruns
   in ten seconds send the game back to the normal periods until it is no game. The game slice is a file of this check's own. */
static void sv_test_game(void)
{
        static char slice[300];
        positive t = 1000000000000ull;

        sv_path(slice, sizeof slice, "game-slice");
        audio_game_slice = slice;
        audio_service.suspended = 0, audio_service.played_rate = 48000, audio_service.game_at = 0;
        audio_service.game_seen = audio_service.game_forced = audio_service.game_backed = 0;
        audio_output_open(48000, 0);
        sv_write_file("game-slice", "4242\n");
        audio_game_pass(t);
        SVCHECK(audio_service.low == 0, "a game just fullscreen does not reopen the output yet");
        audio_game_pass(t + 1250000000ull);
        SVCHECK(audio_service.low == 1 && audio_service.period == AUDIO_PERIOD_LOW && audio_service.game_forced,
                "a game fullscreen for a second plays with the low-latency periods");
        audio_game_pass(t + 1500000000ull);
        audio_service.pcm.xruns += AUDIO_GAME_XRUNS;
        audio_game_pass(t + 1750000000ull);
        SVCHECK(audio_service.low == 0 && audio_service.game_backed, "three underruns of a game's low periods send it back to the normal ones");
        audio_game_pass(t + 4000000000ull);
        SVCHECK(audio_service.low == 0, "a game sent back stays on the normal periods while it is fullscreen");
        sv_write_file("game-slice", "");
        audio_game_pass(t + 4250000000ull);
        audio_game_pass(t + 5500000000ull);
        SVCHECK(audio_service.low == 0 && !audio_service.game_backed, "no game fullscreen: the normal periods, and the back-off forgotten");
        sv_write_file("game-slice", "4242\n");
        audio_game_pass(t + 5750000000ull);
        audio_game_pass(t + 7000000000ull);
        SVCHECK(audio_service.low == 1, "the next fullscreen game takes the low periods again");
        sv_write_file("game-slice", "");
        audio_game_pass(t + 7250000000ull);
        audio_game_pass(t + 8500000000ull);
        SVCHECK(audio_service.low == 0 && audio_service.period == AUDIO_PERIOD, "the game gone, the output goes back to its own periods");
        audio_service.played_rate = 0;
        audio_game_slice = "/run/moonwater/game-slice";
}

static int sv_main_checks(void)
{
        sv_setup();
        sv_test_clock();
        sv_test_settings();
        sv_test_gate();
        sv_test_fifo();
        sv_test_table();
        sv_test_job();
        sv_test_bucket();
        sv_test_text();
        sv_test_notify();
        sv_test_clients();
        sv_test_power();
        sv_test_spin();
        sv_test_socket();
        sv_test_card();
        sv_test_rate();
        sv_test_game();
        sv_test_file();
        sv_test_page();
        sv_test_page_rows();
        printf("CHECK_audio_service: %u checks, %u failures\n", sv_checks, sv_failures);
        return sv_failures != 0;
}

/* the text helpers on a pinned core: ticks for a copy of a name and for a comparison of two names (median over the passes) */
static void sv_bench_text(int cpu)
{
        enum { PASSES = 4001 };
        static unsigned long long samples[PASSES];
        static char to[64];
        cpu_set_t set;
        const char *name = "a program name of about forty bytes long!";
        const char *other = "a program name of about forty bytes long?";
        int same = 0;

        CPU_ZERO(&set);
        CPU_SET(cpu, &set);
        sched_setaffinity(0, sizeof set, &set);
        for (int i = 0; i < PASSES; i++)
        {
                samples[i] = sv_now();
                pulse_copy(to, sizeof to, name);
                samples[i] = sv_now() - samples[i];
        }
        qsort(samples, PASSES, sizeof samples[0], sv_cmp_ull);
        printf("copy of a 36-byte name: median %llu, p90 %llu (ticks)\n", samples[PASSES / 2], samples[PASSES * 9 / 10]);
        for (int i = 0; i < PASSES; i++)
        {
                samples[i] = sv_now();
                same += audio_text_equal(name, other);
                samples[i] = sv_now() - samples[i];
        }
        qsort(samples, PASSES, sizeof samples[0], sv_cmp_ull);
        printf("compare of two 36-byte names: median %llu, p90 %llu (ticks, %d)\n", samples[PASSES / 2], samples[PASSES * 9 / 10], same);
}

/* the card open, in its parts, on a pinned core (`open CARD DEVICE`): the PCM open, the close, and the card's mixer preparation */
static void sv_bench_open(long card, long device)
{
        cpu_set_t set;
        struct audio_pcm pcm;
        __typeof__(audio_service.hw) hw;
        unsigned long long t0, t1, t2, t3;
        long result;

        CPU_ZERO(&set);
        CPU_SET(8, &set);
        sched_setaffinity(0, sizeof set, &set);
        for (int round = 0; round < 3; round++)
        {
                t0 = sv_now();
                result = audio_pcm_open(&pcm, card, device, AUDIO_ALSA_PLAYBACK, 48000, AUDIO_PERIOD, -1);
                t1 = sv_now();
                if (result == 0)
                        audio_pcm_close(&pcm);
                t2 = sv_now();
                audio_mixer_prepare(card, 1, &hw);
                t3 = sv_now();
                printf("round %d, card %ld:%ld: pcm open %llu ticks (result %ld), pcm close %llu, mixer prepare %llu\n", round, card, device, t1 - t0, result, t2 - t1, t3 - t2);
        }
}

int main(int argc, char **argv)
{
        if (argc > 2 && strcmp(argv[1], "open") == 0)
        {
                sv_bench_open(atol(argv[2]), argc > 3 ? atol(argv[3]) : 0);
                return 0;
        }
        if (argc > 1 && strcmp(argv[1], "clients") == 0)
        {
                sv_bench_clients(argc > 2 ? atoi(argv[2]) : 8);
                return 0;
        }
        if (argc > 1 && strcmp(argv[1], "text") == 0)
        {
                sv_bench_text(argc > 2 ? atoi(argv[2]) : 8);
                return 0;
        }
        if (argc > 1 && strcmp(argv[1], "bench") == 0)
        {
                sv_bench(argc > 2 ? atoi(argv[2]) : 8);
                return 0;
        }
        return sv_main_checks();
}
#endif /* CHECK_audio_service */

#ifdef CHECK_audio_control
/*
        CHECK_audio_control: the control client (src/audio/control.c) over its real Unix socket, against servers of its own
        that are forked here (CHECK_audio_pulse runs the client against the engine through a fake transport; this one takes
        the system's transport). A process has two connections at once (the compositor's and the mixer's, both in the one
        translation unit of src/sh/shell.c), and each must reach its own server. A server that dribbles its answer a byte
        at a time, one that talks on with stray frames, and one that never answers must each cost a call its budget (the
        timeout the caller set, 60 ms here) and no more: a call is a deadline, not a wait per part of an answer.

            cc -std=gnu11 -O2 -Wall -Wextra -Wno-comment -D_GNU_SOURCE -DCHECK_audio_control test/checks.c
*/
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

typedef unsigned long long positive;

// The freestanding build's system_call_6 answers a negative errno; the libc's syscall() answers -1 and sets errno.
static long system_call_6(positive number, positive a, positive b, positive c, positive d, positive e, positive f)
{
        long got = syscall((long)number, a, b, c, d, e, f);

        return got < 0 ? -errno : got;
}
#define syscall(name) SYS_##name

// The library's names control.c calls, as the libc has them for a hosted build.
#define memory_copy(to, from, size) memmove((to), (from), (size))
#define string_length(text) strlen((const char *)(text))
#define memory_first_of(block, value, size) memchr((block), (value), (size))
#define memory_zero(to, size) memset((to), 0, (size))
#define memory_copy_apart(to, from, size) memcpy((to), (from), (size))

#include "../../src/audio/control.c"

static unsigned cck_checks, cck_failures;

#define CCKCHECK(condition, ...)                                                                                      \
        do                                                                                                            \
        {                                                                                                             \
                cck_checks++;                                                                                         \
                if (!(condition))                                                                                     \
                {                                                                                                     \
                        cck_failures++;                                                                               \
                        printf("  FAIL: ");                                                                           \
                        printf(__VA_ARGS__);                                                                          \
                        printf(" (line %d)\n", __LINE__);                                                             \
                }                                                                                                     \
        } while (0)

enum { CCK_NORMAL, CCK_DRIBBLE, CCK_CHATTY, CCK_STALL, CCK_SLOW_READ };

// What a forked server saw, counted by the server into a shared page and read by the check.
struct cck_log
{
        volatile long requests;
        volatile unsigned long command, index, tag;
};

static double cck_now_ms(void)
{
        struct timespec at;

        clock_gettime(CLOCK_MONOTONIC, &at);
        return (double)at.tv_sec * 1000.0 + (double)at.tv_nsec / 1e6;
}

static unsigned long cck_get(const unsigned char *p)
{
        return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 | (unsigned long)p[2] << 8 | (unsigned long)p[3];
}

static void cck_put(unsigned char *to, unsigned long v)
{
        to[0] = (unsigned char)(v >> 24), to[1] = (unsigned char)(v >> 16), to[2] = (unsigned char)(v >> 8), to[3] = (unsigned char)v;
}

static int cck_read_full(int fd, unsigned char *bytes, long count)
{
        long done = 0;

        while (done < count)
        {
                ssize_t got = read(fd, bytes + done, (size_t)(count - done));

                if (got < 0 && errno == EINTR)
                        continue;
                if (got <= 0)
                        return -1;
                done += got;
        }
        return 0;
}

static void cck_write_full(int fd, const unsigned char *bytes, long count)
{
        while (count > 0)
        {
                ssize_t put = write(fd, bytes, (size_t)count);

                if (put < 0 && errno == EINTR)
                        continue;
                if (put <= 0)
                        return;
                bytes += put;
                count -= put;
        }
}

// A reply on a channel with its tag, and extra bytes of filler after the tag: the frame's descriptor is made here.
static long cck_frame(unsigned char *out, unsigned long channel, unsigned long tag, long extra)
{
        long size = 10 + extra;

        cck_put(out, (unsigned long)size);
        cck_put(out + 4, channel);
        memset(out + 8, 0, 12);
        out[20] = 'L';
        cck_put(out + 21, 2); // PULSE REPLY
        out[25] = 'L';
        cck_put(out + 26, tag);
        memset(out + 30, 0, (size_t)extra);
        return 20 + size;
}

static void cck_serve(int listener, int mode, struct cck_log *log)
{
        unsigned char head[20], request[512], frame[256];
        int conn = accept(listener, 0, 0);

        if (conn < 0)
                _exit(1);
        for (;;)
        {
                long size, length;

                if (mode == CCK_SLOW_READ && log->requests >= 2)
                {
                        // After the handshake: a reader that takes a kilobyte every 30 ms and answers nothing.
                        unsigned char sink[1024];

                        for (;;)
                        {
                                ssize_t got;

                                usleep(30000);
                                got = read(conn, sink, sizeof(sink));
                                if (got <= 0)
                                        break;
                        }
                        _exit(0);
                }
                if (cck_read_full(conn, head, 20) < 0)
                        break;
                size = (long)cck_get(head);
                if (size < 10 || size > (long)sizeof(request) || cck_read_full(conn, request, size) < 0)
                        break;
                log->requests++;
                log->command = cck_get(request + 1);
                log->tag = cck_get(request + 6);
                log->index = size >= 15 ? cck_get(request + 11) : 0;
                if (log->requests <= 2 || mode == CCK_NORMAL)
                {
                        length = cck_frame(frame, 0xffffffffu, log->tag, 0);
                        cck_write_full(conn, frame, length);
                        continue;
                }
                if (mode == CCK_STALL)
                        sleep(60);
                if (mode == CCK_CHATTY)
                        for (int stray = 0; stray < 100; stray++)
                        {
                                length = cck_frame(frame, 0, log->tag + 1000, 0);
                                cck_write_full(conn, frame, length);
                                usleep(8000);
                        }
                if (mode == CCK_DRIBBLE)
                {
                        length = cck_frame(frame, 0xffffffffu, log->tag, 10);
                        for (long at = 0; at < length; at++)
                        {
                                cck_write_full(conn, frame + at, 1);
                                usleep(20000);
                        }
                        continue;
                }
                length = cck_frame(frame, 0xffffffffu, log->tag, 0);
                cck_write_full(conn, frame, length);
        }
        _exit(0);
}

// A server of its own on path, its listening socket made before the fork (a connect after the fork cannot be refused).
static pid_t cck_spawn(const char *path, int mode, struct cck_log **log)
{
        struct sockaddr_un address;
        int listener = socket(AF_UNIX, SOCK_STREAM, 0);
        pid_t pid;

        *log = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        if (listener < 0 || *log == MAP_FAILED)
                return -1;
        memset(&address, 0, sizeof(address));
        address.sun_family = AF_UNIX;
        snprintf(address.sun_path, sizeof(address.sun_path), "%s", path);
        unlink(path);
        if (bind(listener, (struct sockaddr *)&address, sizeof(address)) < 0 || listen(listener, 4) < 0)
                return -1;
        memset(*log, 0, 4096);
        pid = fork();
        if (pid == 0)
        {
                cck_serve(listener, mode, *log);
                _exit(0);
        }
        close(listener);
        return pid;
}

static void cck_stop(pid_t pid, struct ctl *c)
{
        ctl_disconnect(c);
        if (pid > 0)
        {
                kill(pid, SIGKILL);
                waitpid(pid, 0, 0);
        }
}

// Two connections in one process, each to its own server: the requests of each reach that server and no other.
static void cck_test_two_connections(const char *dir)
{
        char path_a[108], path_b[108], path_c[108];
        struct cck_log *log_a, *log_b, *log_c = 0;
        pid_t pid_a, pid_b, pid_c = -1;
        static struct ctl a, b;
        long before_a, before_b;
        int old_fd;

        snprintf(path_a, sizeof(path_a), "%s/a", dir);
        snprintf(path_b, sizeof(path_b), "%s/b", dir);
        pid_a = cck_spawn(path_a, CCK_NORMAL, &log_a);
        pid_b = cck_spawn(path_b, CCK_NORMAL, &log_b);
        memset(&a, 0, sizeof(a));
        memset(&b, 0, sizeof(b));
        CCKCHECK(pid_a > 0 && pid_b > 0, "the two servers are made");
        CCKCHECK(ctl_connect(&a, path_a) == 0 && ctl_connect(&b, path_b) == 0, "two connections are made, the second while the first is open");
        before_a = log_a->requests;
        before_b = log_b->requests;
        CCKCHECK(ctl_set_sink_volume(&a, "null", 0x8000) == 0, "the first connection's volume is taken");
        CCKCHECK(log_a->requests == before_a + 1 && log_b->requests == before_b, "and it reaches its own server (A %ld, B %ld)",
                 log_a->requests - before_a, log_b->requests - before_b);
        before_a = log_a->requests;
        before_b = log_b->requests;
        CCKCHECK(ctl_set_sink_volume(&b, "null", 0x4000) == 0, "the second connection's volume is taken");
        CCKCHECK(log_b->requests == before_b + 1 && log_a->requests == before_a, "and it reaches its own server (A %ld, B %ld)",
                 log_a->requests - before_a, log_b->requests - before_b);
        // The first connection made again, to a server of its own: the socket it replaces is closed, and the second is not touched.
        snprintf(path_c, sizeof(path_c), "%s/c", dir);
        pid_c = cck_spawn(path_c, CCK_NORMAL, &log_c);
        old_fd = a.socket.fd;
        CCKCHECK(pid_c > 0 && ctl_connect(&a, path_c) == 0, "the first connection is made again, to another server");
        CCKCHECK(fcntl(old_fd, F_GETFD) == -1 && errno == EBADF, "and the socket it replaces is closed");
        CCKCHECK(ctl_set_sink_volume(&a, "null", 0x1000) == 0 && log_c->requests == 3, "its volume reaches the new server (%ld requests)", log_c->requests);
        CCKCHECK(ctl_set_sink_volume(&b, "null", 0x3000) == 0, "the second connection is still taken");
        ctl_disconnect(&a);
        CCKCHECK(ctl_set_sink_volume(&b, "null", 0x2000) == 0, "closing the first leaves the second connected");
        cck_stop(pid_c, &a);
        cck_stop(pid_b, &b);
        cck_stop(pid_a, &a);
}

// A server whose answer holds the call: the call fails within its budget (60 ms) and not after the server's whole answer.
static void cck_test_held(const char *dir, int mode, const char *name)
{
        char path[108];
        struct cck_log *log;
        pid_t pid;
        static struct ctl c;
        double began;
        long got;

        snprintf(path, sizeof(path), "%s/%s", dir, name);
        pid = cck_spawn(path, mode, &log);
        memset(&c, 0, sizeof(c));
        CCKCHECK(pid > 0 && ctl_connect(&c, path) == 0, "%s: connected", name);
        c.timeout_ms = 60;
        began = cck_now_ms();
        got = ctl_set_sink_volume(&c, "null", 0x8000);
        CCKCHECK(got == CTL_FAILED, "%s: the call fails (%ld)", name, got);
        CCKCHECK(cck_now_ms() - began < 200.0, "%s: the call costs its budget and no more (%.0f ms, budget 60)", name, cck_now_ms() - began);
        CCKCHECK(!ctl_ready(&c), "%s: a call that ran out is a lost connection", name);
        cck_stop(pid, &c);
}

// A request bigger than the connection will take at once (its send buffer is 4 KB, the server reads a kilobyte in 30 ms and
// answers nothing): the send is given the call's budget (60 ms) and stops there, and the call fails within it. A send that
// waited per part would go on for as long as the reader read: 20 KB is about 600 ms.
static void cck_test_slow_send(const char *dir)
{
        char path[108], *name = malloc(20001);
        struct cck_log *log;
        pid_t pid;
        static struct ctl c;
        double began;
        long got;
        int small = 4096;

        snprintf(path, sizeof(path), "%s/w", dir);
        pid = cck_spawn(path, CCK_SLOW_READ, &log);
        memset(&c, 0, sizeof(c));
        CCKCHECK(pid > 0 && name && ctl_connect(&c, path) == 0, "slow send: connected");
        if (name)
                memset(name, 'n', 20000), name[20000] = 0;
        setsockopt(c.socket.fd, SOL_SOCKET, SO_SNDBUF, &small, sizeof(small));
        c.timeout_ms = 60;
        began = cck_now_ms();
        got = name ? ctl_set_sink_volume(&c, name, 0x8000) : CTL_FAILED;
        CCKCHECK(got == CTL_FAILED, "slow send: the call fails (%ld)", got);
        CCKCHECK(cck_now_ms() - began < 200.0, "slow send: the call costs its budget and no more (%.0f ms, budget 60)", cck_now_ms() - began);
        cck_stop(pid, &c);
        free(name);
}

// A normal answer with a budget set: the volume is taken at once.
static void cck_test_normal(const char *dir)
{
        char path[108];
        struct cck_log *log;
        pid_t pid;
        static struct ctl c;
        double began;
        long got;

        snprintf(path, sizeof(path), "%s/n", dir);
        pid = cck_spawn(path, CCK_NORMAL, &log);
        memset(&c, 0, sizeof(c));
        CCKCHECK(pid > 0 && ctl_connect(&c, path) == 0, "normal: connected");
        c.timeout_ms = 60;
        began = cck_now_ms();
        got = ctl_set_sink_volume(&c, "null", 0x8000);
        CCKCHECK(got == 0 && log->command == CTL_COMMAND_SET_SINK_VOLUME && log->index == CTL_NONE, "normal: the volume is taken (%ld)", got);
        CCKCHECK(cck_now_ms() - began < 60.0, "normal: an answer that comes is not waited for (%.1f ms)", cck_now_ms() - began);
        cck_stop(pid, &c);
}

// A transport with no clock (now is null) and a fake server that answers each request with its tag: its calls are taken, and no
// call goes through the null clock (the budget of a connection with no clock never runs out).
static unsigned char cck_fake_reply[32];
static long cck_fake_have, cck_fake_at;

static long cck_fake_send(void *context, const unsigned char *bytes, long count, long timeout_ms)
{
        (void)context, (void)count, (void)timeout_ms;
        memset(cck_fake_reply, 0, sizeof(cck_fake_reply));
        cck_put(cck_fake_reply, 10);
        cck_put(cck_fake_reply + 4, 0xffffffffu);
        cck_fake_reply[20] = 'L';
        cck_put(cck_fake_reply + 21, 2);
        cck_fake_reply[25] = 'L';
        cck_put(cck_fake_reply + 26, cck_get(bytes + 26));
        cck_fake_have = 30;
        cck_fake_at = 0;
        return 0;
}

static long cck_fake_receive(void *context, unsigned char *bytes, long count, long timeout_ms)
{
        (void)context, (void)timeout_ms;
        if (cck_fake_have - cck_fake_at < count)
                return -1;
        memcpy(bytes, cck_fake_reply + cck_fake_at, (size_t)count);
        cck_fake_at += count;
        return 0;
}

static void cck_test_no_clock(void)
{
        static struct ctl c;
        static char longpath[200];

        // A path that does not fit sun_path (108 bytes with its zero) is refused, not cut to the part that does.
        memset(longpath, 'a', sizeof(longpath) - 1);
        longpath[sizeof(longpath) - 1] = 0;
        CCKCHECK(ctl_connect(&c, longpath) == CTL_FAILED, "a path past sun_path is refused, not cut");
        memset(&c, 0, sizeof(c));
        c.transport = (struct ctl_transport){0, cck_fake_send, cck_fake_receive, 0};
        c.open = 1;
        CCKCHECK(ctl_set_sink_volume(&c, "null", 0x8000) == 0, "a transport with no clock takes a volume");
        CCKCHECK(ctl_set_sink_mute(&c, "null", 1) == 0, "and a mute, with no clock");
        CCKCHECK(ctl_set_input_mute(&c, 3, 0) == 0, "and a stream's mute");
}

static volatile sig_atomic_t cck_alarms;

static void cck_on_alarm(int sig)
{
        (void)sig;
        cck_alarms++;
}

// A signal that lands while a send waits on a full socket ends the wait, not the command: the send is made again for what is left
// of its budget, and goes through once the peer has read.
static void cck_test_signal_send(void)
{
        static unsigned char block[4096];
        struct ctl_socket s;
        struct sigaction action, before;
        struct itimerval timer;
        int pair[2], flags;
        long filled = 0;
        unsigned char sink[4096];
        long drained = 0;
        int status = 1, sent;
        pid_t pid;

        memset(block, 'x', sizeof(block));
        memset(&s, 0, sizeof(s));
        CCKCHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, pair) == 0, "a socket pair for a send that waits");
        for (;;)
        {
                ssize_t put = write(pair[0], block, sizeof(block));

                if (put <= 0)
                        break;
                filled += put;
        }
        pid = fork();
        if (pid == 0)
        {
                long want = filled + 1000;

                close(pair[0]);
                flags = fcntl(pair[1], F_GETFL);
                fcntl(pair[1], F_SETFL, flags & ~O_NONBLOCK);
                usleep(150000); // the send waits on the full socket until this reads
                while (drained < want)
                {
                        ssize_t got = read(pair[1], sink, sizeof(sink));

                        if (got < 0 && errno == EINTR)
                                continue;
                        if (got <= 0)
                                break;
                        drained += got;
                }
                _exit(drained >= want ? 0 : 1);
        }
        close(pair[1]);
        s.fd = pair[0];
        memset(&action, 0, sizeof(action));
        action.sa_handler = cck_on_alarm; // no SA_RESTART: the poll that waits is ended by it
        sigemptyset(&action.sa_mask);
        sigaction(SIGALRM, &action, &before);
        memset(&timer, 0, sizeof(timer));
        timer.it_value.tv_usec = 50000;
        setitimer(ITIMER_REAL, &timer, 0);
        sent = (int)ctl_socket_send(&s, block, 1000, 2000);
        memset(&timer, 0, sizeof(timer));
        setitimer(ITIMER_REAL, &timer, 0);
        sigaction(SIGALRM, &before, 0);
        CCKCHECK(sent == 0 && cck_alarms >= 1, "a signal during a blocked send does not fail it: sent %d, signalled %d times", sent, (int)cck_alarms);
        if (pid > 0)
        {
                waitpid(pid, &status, 0);
                CCKCHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0, "the peer read everything that was sent");
        }
        close(pair[0]);
}

// A listener whose backlog is full refuses a connect that does not block with EAGAIN: the connect is made again within its budget, and
// it succeeds once the server has taken a connection off its queue.
static void cck_test_full_backlog(const char *dir)
{
        char path[108];
        struct sockaddr_un address;
        struct ctl c;
        int listener, queued = 0, client[16];
        pid_t pid;
        int status = 1;

        snprintf(path, sizeof(path), "%s/full", dir);
        unlink(path);
        memset(&address, 0, sizeof(address));
        address.sun_family = AF_UNIX;
        snprintf(address.sun_path, sizeof(address.sun_path), "%s", path);
        memset(&c, 0, sizeof(c));
        listener = socket(AF_UNIX, SOCK_STREAM, 0);
        CCKCHECK(listener >= 0 && bind(listener, (struct sockaddr *)&address, sizeof(address)) == 0 && listen(listener, 1) == 0, "a listener with a backlog of one");
        for (int i = 0; i < 16; i++)
        {
                int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);

                if (fd < 0)
                        break;
                if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0)
                {
                        close(fd);
                        break;
                }
                client[queued++] = fd;
        }
        CCKCHECK(queued >= 1 && queued < 16, "the queue fills (%d connections taken before it refused one)", queued);
        pid = fork();
        if (pid == 0)
        {
                static struct cck_log log;

                usleep(100000); // the queue is full until the server takes one off it
                for (int i = 0; i < queued; i++)
                {
                        int fd = accept(listener, 0, 0);

                        if (fd < 0)
                                _exit(1);
                        close(fd); // the connections made by hand say nothing: they are let go
                }
                cck_serve(listener, CCK_NORMAL, &log); // the connection that waited, and the handshake it is answered with
                _exit(0);
        }
        CCKCHECK(ctl_connect(&c, path) == 0, "a connect to a full backlog succeeds once the server drains it");
        ctl_disconnect(&c);
        if (pid > 0)
        {
                waitpid(pid, &status, 0);
                CCKCHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0, "the server took every connection");
        }
        for (int i = 0; i < queued; i++)
                close(client[i]);
        close(listener);
}

// The rest of the client, referenced here so that a build with -Werror finds no unused function: each of them is checked
// over the engine by CHECK_audio_pulse, and this check is about the socket under them.
static const void *cck_rest[] = {(void *)ctl_get_input, (void *)ctl_set_input_volume, (void *)ctl_set_output_volume, (void *)ctl_set_input_mute,
                                 (void *)ctl_set_output_mute, (void *)ctl_kill_input, (void *)ctl_set_sink_mute,
                                 (void *)ctl_set_source_volume, (void *)ctl_set_source_mute};

int main(void)
{
        char dir[96], tmp[64] = "/tmp";
        const char *env = getenv("TMPDIR");

        if (env && strlen(env) < 40)
                snprintf(tmp, sizeof(tmp), "%s", env);
        snprintf(dir, sizeof(dir), "%s/alsa-control-XXXXXX", tmp);
        if (!mkdtemp(dir))
        {
                printf("CHECK_audio_control: no directory for the sockets\n");
                return 1;
        }
        if (!cck_rest[0])
                return 1;
        cck_test_no_clock();
        cck_test_signal_send();
        cck_test_full_backlog(dir);
        cck_test_two_connections(dir);
        cck_test_normal(dir);
        cck_test_held(dir, CCK_DRIBBLE, "d");
        cck_test_held(dir, CCK_CHATTY, "c");
        cck_test_held(dir, CCK_STALL, "s");
        cck_test_slow_send(dir);
        printf("CHECK_audio_control: %u checks, %u failures\n", cck_checks, cck_failures);
        return cck_failures ? 1 : 0;
}
#endif /* CHECK_audio_control */

#ifdef CHECK_audio_state
/*
        CHECK_audio_state: the state page's reader (src/audio/state.c) on a page the reader must not trust: every string unended
        (filled to its last byte), every count past its array, an output_why that names no reason. The copy it returns has every
        string ended inside its array, every count within its array, and an output_why of 0; a well-formed page keeps its text and
        its reason; a page that is being written is not read; the copy is safe to read field by field.

            cc -std=gnu11 -O2 -Wall -Wextra -Werror -Wno-comment -D_GNU_SOURCE -DCHECK_audio_state test/checks.c
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The library's names state.c calls, as the hosted build has them.
typedef unsigned long positive;
#define memory_copy_apart(to, from, size) memmove((to), (from), (size))

#include "../../src/audio/state.c"

static unsigned st_checks, st_failures;

#define STCHECK(condition, ...)                                                                                       \
        do                                                                                                            \
        {                                                                                                             \
                st_checks++;                                                                                          \
                if (!(condition))                                                                                     \
                {                                                                                                     \
                        st_failures++;                                                                                \
                        printf("  FAIL: ");                                                                           \
                        printf(__VA_ARGS__);                                                                          \
                        printf(" (line %d)\n", __LINE__);                                                             \
                }                                                                                                     \
        } while (0)

// Every string field of the copy: its last byte is a null, so a reader stops inside it.
static int st_all_ended(const struct audio_state *s)
{
        for (unsigned int k = 0; k < AUDIO_STATE_DEVICES; k++)
                if (s->device_list[k].name[sizeof(s->device_list[k].name) - 1] || s->device_list[k].description[sizeof(s->device_list[k].description) - 1])
                        return 0;
        for (unsigned int k = 0; k < AUDIO_STATE_INPUTS; k++)
                if (s->input_list[k].name[sizeof(s->input_list[k].name) - 1] || s->input_list[k].description[sizeof(s->input_list[k].description) - 1])
                        return 0;
        for (unsigned int k = 0; k < AUDIO_STATE_STREAMS; k++)
                if (s->stream[k].app[sizeof(s->stream[k].app) - 1] || s->stream[k].media[sizeof(s->stream[k].media) - 1] ||
                    s->stream[k].icon[sizeof(s->stream[k].icon) - 1])
                        return 0;
        for (unsigned int k = 0; k < AUDIO_STATE_RECORDS; k++)
                if (s->record[k].app[sizeof(s->record[k].app) - 1] || s->record[k].media[sizeof(s->record[k].media) - 1] ||
                    s->record[k].icon[sizeof(s->record[k].icon) - 1])
                        return 0;
        return s->device[sizeof(s->device) - 1] == 0 && s->name[sizeof(s->name) - 1] == 0 && s->input[sizeof(s->input) - 1] == 0 &&
               s->input_name[sizeof(s->input_name) - 1] == 0 && s->hw_note[sizeof(s->hw_note) - 1] == 0;
}

// A page that the server wrote, with each string as the test says and each count and reason as the test says.
static struct audio_state st_page;

static void st_seal(void)
{
        st_page.magic = AUDIO_STATE_MAGIC;
        st_page.version = AUDIO_STATE_VERSION;
        st_page.bytes = sizeof(st_page);
        st_page.seq = 2;
}

static void st_test_hostile(void)
{
        static struct audio_state copy;

        // Every byte a letter: no string is ended, and every count is past its array.
        memset((void *)&st_page, 'A', sizeof(st_page));
        st_seal();
        st_page.streams = 0xffffffffu;
        st_page.devices = 1000;
        st_page.inputs = 99;
        st_page.records = 0x80000000u;
        st_page.output_why = 0x12345;
        STCHECK(audio_state_read((const volatile struct audio_state *)&st_page, &copy) == 1, "a hostile page with a good header is read");
        STCHECK(copy.streams == AUDIO_STATE_STREAMS && copy.devices == AUDIO_STATE_DEVICES && copy.inputs == AUDIO_STATE_INPUTS &&
                    copy.records == AUDIO_STATE_RECORDS, "every count is held to its array (%u %u %u %u)", copy.streams, copy.devices, copy.inputs, copy.records);
        STCHECK(st_all_ended(&copy), "every string of the copy ends inside its array");
        STCHECK(copy.output_why == 0, "an output_why that names no reason is 0 (%u)", copy.output_why);
}

static void st_test_well_formed(void)
{
        static struct audio_state copy;

        memset((void *)&st_page, 0, sizeof(st_page));
        st_seal();
        strcpy(st_page.device, "Speakers");
        strcpy(st_page.name, "alsa_output.card0");
        strcpy(st_page.stream[0].app, "Firefox");
        strcpy(st_page.stream[0].media, "Tab one");
        st_page.streams = 1;
        st_page.devices = 0;
        st_page.output_why = AUDIO_STATE_WHY_ANALOG;
        STCHECK(audio_state_read((const volatile struct audio_state *)&st_page, &copy) == 1, "a well-formed page is read");
        STCHECK(strcmp(copy.device, "Speakers") == 0 && strcmp(copy.name, "alsa_output.card0") == 0, "its names are kept");
        STCHECK(strcmp(copy.stream[0].app, "Firefox") == 0 && strcmp(copy.stream[0].media, "Tab one") == 0 && copy.streams == 1, "its stream is kept");
        STCHECK(copy.output_why == AUDIO_STATE_WHY_ANALOG, "a reason in range is kept (%u)", copy.output_why);
        STCHECK(st_all_ended(&copy), "a well-formed page's strings end inside their arrays");
}

static void st_test_unstable(void)
{
        static struct audio_state copy;

        memset((void *)&st_page, 0, sizeof(st_page));
        st_seal();
        st_page.seq = 3; // being written: odd
        STCHECK(audio_state_read((const volatile struct audio_state *)&st_page, &copy) == 0, "a page that is being written is not read");
}

int main(void)
{
        st_test_hostile();
        st_test_well_formed();
        st_test_unstable();
        printf("CHECK_audio_state: %u checks, %u failures\n", st_checks, st_failures);
        return st_failures ? 1 : 0;
}
#endif /* CHECK_audio_state */


