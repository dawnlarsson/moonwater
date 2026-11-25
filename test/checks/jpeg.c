/*
        test/checks/jpeg.c: lib.c's JPEG arithmetic (jpeg_idct_8x8 and the rest of its family) against the definitions its header
        comment gives, written here in plain C, and timed against them.

        A run of test/checks.c, which includes this file where the run stood: it is compiled only as part of that file, with its
        statics and macros, and never on its own. CHECK_jpeg and BENCH_jpeg are freestanding, built as test/checks.c is (gcc -O2
        -static -nostdlib -nostartfiles -march=x86-64 -w -DCHECK_jpeg -fwhole-program -o OUT test/checks.c); test/run's jpeg lane
        runs the check on x86_64 once per tier with the feature bytes walked down, and on arm64 and riscv64 under qemu where it has
        them. The references between the two marks below are also what the decoder's fuzz harness (differential.py's jpeg_fuzz)
        lifts in place of the library, so that the sanitizers see the whole decode in C.
*/

#if defined(CHECK_jpeg) || defined(BENCH_jpeg)
#include "../../src/lib.util.c"

#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

/* ---- the references ---- */

// jpeg_idct_8x8's definition: libjpeg's islow, its rotations as sums of two products, 16-bit dequantization and the four 16-bit
// sums wrapping, every other sum wrapping at 32 bits, the columns' results held to 16 bits and the rows' to a byte.
static inline b16 jpeg_reference_held16(b32 value)
{
        return (b16)(value > 32767 ? 32767 : value < -32768 ? -32768 : value);
}

static inline p8 jpeg_reference_sample(b32 value)
{
        return (p8)((value > 127 ? 127 : value < -128 ? -128 : value) + 128);
}

static inline b16 jpeg_reference_dequantized(b16 coefficient, p16 quantizer)
{
        return (b16)(p16)((p32)(p16)coefficient * quantizer);
}

static inline fn jpeg_reference_islow(const b16 address_to in, positive stride, p32 address_to out)
{
        b32 i0 = in[0], i1 = in[stride], i2 = in[2 * stride], i3 = in[3 * stride], i4 = in[4 * stride], i5 = in[5 * stride],
            i6 = in[6 * stride], i7 = in[7 * stride];
        b32 s04 = (b16)(p16)(i0 + i4), d04 = (b16)(p16)(i0 - i4), z3 = (b16)(p16)(i7 + i3), z4 = (b16)(p16)(i5 + i1);
        p32 tmp0 = (p32)(s04 * 8192), tmp1 = (p32)(d04 * 8192);
        p32 tmp3 = (p32)(i2 * 10703) + (p32)(i6 * 4433), tmp2 = (p32)(i2 * 4433) + (p32)(i6 * -10704);
        p32 tmp10 = tmp0 + tmp3, tmp13 = tmp0 - tmp3, tmp11 = tmp1 + tmp2, tmp12 = tmp1 - tmp2;
        p32 z3r = (p32)(z3 * -6436) + (p32)(z4 * 9633), z4r = (p32)(z3 * 9633) + (p32)(z4 * 6437);
        p32 o0 = (p32)(i7 * -4927) + (p32)(i1 * -7373) + z3r, o3 = (p32)(i7 * -7373) + (p32)(i1 * 4926) + z4r;
        p32 o1 = (p32)(i5 * -4176) + (p32)(i3 * -20995) + z4r, o2 = (p32)(i5 * -20995) + (p32)(i3 * 4177) + z3r;

        out[0] = tmp10 + o3, out[7] = tmp10 - o3, out[1] = tmp11 + o2, out[6] = tmp11 - o2;
        out[2] = tmp12 + o1, out[5] = tmp12 - o1, out[3] = tmp13 + o0, out[4] = tmp13 - o0;
}

static fn jpeg_reference_idct_8x8(p8 address_to into, positive pitch, const b16 address_to coefficients, const p16 address_to quantization)
{
        b16 in[64], work[64];
        p32 out[8];

        for (int k = 0; k < 64; k++)
                in[k] = jpeg_reference_dequantized(coefficients[k], quantization[k]);
        for (int column = 0; column < 8; column++)
        {
                jpeg_reference_islow(in + column, 8, out);
                for (int row = 0; row < 8; row++)
                        work[row * 8 + column] = jpeg_reference_held16((b32)(out[row] + 1024u) >> 11);
        }
        for (int row = 0; row < 8; row++, into += pitch)
        {
                jpeg_reference_islow(work + row * 8, 1, out);
                for (int column = 0; column < 8; column++)
                        into[column] = jpeg_reference_sample(jpeg_reference_held16((b32)(out[column] + (1u << 17)) >> 18));
        }
}

static inline p8 jpeg_reference_byte(b32 value)
{
        return (p8)(value < 0 ? 0 : value > 255 ? 255 : value);
}

// pixels_from_ycbcr's definition: libjpeg's jdcolor.c in 16-bit fixed point, each term rounded as its tables round.
static fn jpeg_reference_pixels_from_ycbcr(p32 address_to into, const p8 address_to y, const p8 address_to cb, const p8 address_to cr, positive count)
{
        for (positive x = 0; x < count; x++)
        {
                b32 l = y[x], u = (b32)cb[x] - 128, v = (b32)cr[x] - 128;

                into[x] = 0xff000000u | (p32)jpeg_reference_byte(l + ((91881 * v + 32768) >> 16)) << 16 |
                          (p32)jpeg_reference_byte(l + ((-22554 * u - 46802 * v + 32768) >> 16)) << 8 |
                          jpeg_reference_byte(l + ((116130 * u + 32768) >> 16));
        }
}

static fn jpeg_reference_pixels_from_grey(p32 address_to into, const p8 address_to y, positive count)
{
        for (positive x = 0; x < count; x++)
                into[x] = 0xff000000u | (p32)y[x] * 0x010101u;
}

static fn jpeg_reference_pixels_from_planes(p32 address_to into, const p8 address_to r, const p8 address_to g, const p8 address_to b, positive count)
{
        for (positive x = 0; x < count; x++)
                into[x] = 0xff000000u | (p32)r[x] << 16 | (p32)g[x] << 8 | b[x];
}

// pixels_to_ycbcr's definition: libjpeg's jccolor.c (the alpha byte not read), Cb and Cr rounded by one half less a 65536th.
static fn jpeg_reference_pixels_to_ycbcr(p8 address_to y, p8 address_to cb, p8 address_to cr, const p32 address_to from, positive count)
{
        for (positive x = 0; x < count; x++)
        {
                b32 r = (b32)(from[x] >> 16 & 255), g = (b32)(from[x] >> 8 & 255), b = (b32)(from[x] & 255);

                y[x] = (p8)((19595 * r + 38470 * g + 7471 * b + 32768) >> 16);
                cb[x] = (p8)((-11059 * r - 21709 * g + 32768 * b + (128 << 16) + 32767) >> 16);
                cr[x] = (p8)((32768 * r - 27439 * g - 5329 * b + (128 << 16) + 32767) >> 16);
        }
}

// samples_double's definition: libjpeg's h2v1 triangle filter, the samples beyond either end the end ones again.
static fn jpeg_reference_samples_double(p8 address_to into, const p8 address_to from, positive count)
{
        for (positive x = 0; x < count; x++)
        {
                p32 near = from[x] * 3u, left = from[x ? x - 1 : 0], right = from[x + 1 < count ? x + 1 : x];

                into[2 * x] = (p8)((near + left + 1) >> 2), into[2 * x + 1] = (p8)((near + right + 2) >> 2);
        }
}

// samples_double_rows's definition: libjpeg's h2v2 triangle filter's row, the column sums 3 near + far, beyond the ends the end ones.
static fn jpeg_reference_samples_double_rows(p8 address_to into, const p8 address_to near, const p8 address_to far, positive count)
{
        for (positive x = 0; x < count; x++)
        {
                positive l = x ? x - 1 : 0, r = x + 1 < count ? x + 1 : x;
                p32 c = near[x] * 3u + far[x], cl = near[l] * 3u + far[l], cr = near[r] * 3u + far[r];

                into[2 * x] = (p8)((c * 3 + cl + 8) >> 4), into[2 * x + 1] = (p8)((c * 3 + cr + 7) >> 4);
        }
}

// samples_blend_rows's definition: libjpeg's h1v2 triangle filter's row, (3 near + far + bias) / 4 with bias 0 to 3.
static fn jpeg_reference_samples_blend_rows(p8 address_to into, const p8 address_to near, const p8 address_to far, positive count, p32 bias)
{
        for (positive x = 0; x < count; x++)
                into[x] = (p8)((near[x] * 3u + far[x] + (bias & 3)) >> 2);
}

// jpeg_idct_4x4's and jpeg_idct_2x2's definitions: jidctred.c's, every sum wrapping at 32 bits (there is no 16-bit one).
static inline fn jpeg_reference_reduced4(const b16 address_to in, positive stride, p32 address_to out)
{
        b32 i0 = in[0], i1 = in[stride], i2 = in[2 * stride], i3 = in[3 * stride], i5 = in[5 * stride], i6 = in[6 * stride],
            i7 = in[7 * stride];
        p32 tmp0 = (p32)(i0 * 16384), tmp2 = (p32)(i2 * 15137) + (p32)(i6 * -6270);
        p32 tmp10 = tmp0 + tmp2, tmp12 = tmp0 - tmp2;
        p32 o0 = (p32)(i7 * -1730) + (p32)(i5 * 11893) + (p32)(i3 * -17799) + (p32)(i1 * 8697);
        p32 o2 = (p32)(i7 * -4176) + (p32)(i5 * -4926) + (p32)(i3 * 7373) + (p32)(i1 * 20995);

        out[0] = tmp10 + o2, out[3] = tmp10 - o2, out[1] = tmp12 + o0, out[2] = tmp12 - o0;
}

static fn jpeg_reference_idct_4x4(p8 address_to into, positive pitch, const b16 address_to coefficients, const p16 address_to quantization)
{
        b16 in[64], work[32];
        p32 out[4];

        for (int k = 0; k < 64; k++)
                in[k] = jpeg_reference_dequantized(coefficients[k], quantization[k]);
        for (int column = 0; column < 8; column++)
        {
                jpeg_reference_reduced4(in + column, 8, out);
                for (int row = 0; row < 4; row++)
                        work[row * 8 + column] = jpeg_reference_held16((b32)(out[row] + 2048u) >> 12);
        }
        for (int row = 0; row < 4; row++, into += pitch)
        {
                jpeg_reference_reduced4(work + row * 8, 1, out);
                for (int column = 0; column < 4; column++)
                        into[column] = jpeg_reference_sample(jpeg_reference_held16((b32)(out[column] + (1u << 18)) >> 19));
        }
}

static inline fn jpeg_reference_reduced2(const b16 address_to in, positive stride, p32 address_to out)
{
        p32 even = (p32)(in[0] * 32768);
        p32 odd = (p32)(in[7 * stride] * -5906) + (p32)(in[5 * stride] * 6967) + (p32)(in[3 * stride] * -10426) + (p32)(in[stride] * 29692);

        out[0] = even + odd, out[1] = even - odd;
}

static fn jpeg_reference_idct_2x2(p8 address_to into, positive pitch, const b16 address_to coefficients, const p16 address_to quantization)
{
        b16 in[64], work[16];
        p32 out[2];

        for (int k = 0; k < 64; k++)
                in[k] = jpeg_reference_dequantized(coefficients[k], quantization[k]);
        for (int column = 0; column < 8; column++)
        {
                jpeg_reference_reduced2(in + column, 8, out);
                work[column] = jpeg_reference_held16((b32)(out[0] + 4096u) >> 13);
                work[8 + column] = jpeg_reference_held16((b32)(out[1] + 4096u) >> 13);
        }
        for (int row = 0; row < 2; row++, into += pitch)
        {
                jpeg_reference_reduced2(work + row * 8, 1, out);
                into[0] = jpeg_reference_sample(jpeg_reference_held16((b32)(out[0] + (1u << 19)) >> 20));
                into[1] = jpeg_reference_sample(jpeg_reference_held16((b32)(out[1] + (1u << 19)) >> 20));
        }
}

/* ---- end of the references ---- */

static unsigned long long jpeg_state = 0x2545f4914f6cdd1dull;

static p32 jpeg_next(void)
{
        jpeg_state ^= jpeg_state << 13, jpeg_state ^= jpeg_state >> 7, jpeg_state ^= jpeg_state << 17;
        return (p32)(jpeg_state >> 20);
}

/*
        A block's coefficients and quantizers of a kind: 0 any 16-bit values at all; 1 an honest block's (small coefficients, the
        low frequencies most, quantizers 1 to 80); 2 the extremes (every coefficient +-32767 or -32768, every quantizer 1 or 65535,
        so the 16-bit sums and the holds all saturate or wrap); 3 large coefficients a hostile file may send; 4 a smooth block's
        (a DC and two ACs, the rest zero: the shortcuts a body may take).
*/
static fn jpeg_block_of(b16 address_to c, p16 address_to q, p32 kind)
{
        for (int k = 0; k < 64; k++)
                switch (kind)
                {
                case 0:
                        c[k] = (b16)jpeg_next(), q[k] = (p16)jpeg_next();
                        break;
                case 1:
                        c[k] = k < 10 || jpeg_next() % 4 == 0 ? (b16)((int)(jpeg_next() % 129) - 64) : 0, q[k] = (p16)(1 + jpeg_next() % 80);
                        break;
                case 2:
                        c[k] = (jpeg_next() & 1) ? 32767 : -32768, q[k] = (jpeg_next() & 1) ? 65535 : 1;
                        break;
                case 3:
                        c[k] = (b16)((int)(jpeg_next() % 4097) - 2048), q[k] = (p16)(1 + jpeg_next() % 255);
                        break;
                default:
                        c[k] = k == 0 ? (b16)((int)(jpeg_next() % 2001) - 1000) : k == 1 || k == 8 ? (b16)((int)(jpeg_next() % 21) - 10) : 0;
                        q[k] = (p16)(1 + jpeg_next() % 30);
                        break;
                }
}

/* ---- the comparisons ---- */

/*
        Each routine of the family against its reference, written over nothing but the references, the routines, jpeg_next and
        memory_fill and memory_compare, so the same text runs in CHECK_jpeg and wherever else the routines are (a harness of the
        machine's own lifts it). Each returns how many cases differed. The colour conversions are held to their references at
        every input there is (2^24 of each); the rest at every count to 70 and a few long ones, random and extreme samples, with
        16 sentinel bytes either side of every output, so a byte written outside a count is a difference too.
*/
#define JPEG_SENTINEL 16
static p8 jpeg_compare_a[4 * 4200 + 2 * JPEG_SENTINEL], jpeg_compare_b[4 * 4200 + 2 * JPEG_SENTINEL];
static p8 jpeg_compare_in[3][4200 + 64];

static positive jpeg_counts(positive at)
{
        static const positive longer[] = {255, 256, 257, 1000, 4096, 4097};

        return at <= 70 ? at : longer[(at - 71) % 6];
}

static fn jpeg_compare_reset(void)
{
        memory_fill(jpeg_compare_a, 0xa5, sizeof(jpeg_compare_a)), memory_fill(jpeg_compare_b, 0xa5, sizeof(jpeg_compare_b));
}

static fn jpeg_compare_fill(positive count, p32 kind)
{
        for (int plane = 0; plane < 3; plane++)
                for (positive at = 0; at < count + 64; at++)
                        jpeg_compare_in[plane][at] = (p8)(kind == 0 ? jpeg_next() : kind == 1 ? (jpeg_next() & 1) * 255 : (at * 37 + plane * 11) & 255);
}

static positive jpeg_compare_colour(void)
{
        static p8 y[65536], cb[65536], cr[65536];
        static p32 want[65536], got[65536];
        positive wrong = 0;

        // Every (Cb, Cr) pair at each Y: all 2^24 inputs, a row of 65536 at a time.
        for (positive at = 0; at < 65536; at++)
                cb[at] = (p8)(at & 255), cr[at] = (p8)(at >> 8);
        for (positive l = 0; l < 256; l++)
        {
                memory_fill(y, (p8)l, sizeof(y));
                jpeg_reference_pixels_from_ycbcr(want, y, cb, cr, 65536);
                pixels_from_ycbcr(got, y, cb, cr, 65536);
                wrong += memory_compare(want, got, sizeof(want)) != 0;
        }
        // And every count to 70 and the long ones, with sentinels.
        for (positive at = 0; at < 71 + 6 * 2; at++)
                for (p32 kind = 0; kind < 3; kind++)
                {
                        positive count = jpeg_counts(at);

                        jpeg_compare_fill(count, kind), jpeg_compare_reset();
                        jpeg_reference_pixels_from_ycbcr((p32 address_to)(jpeg_compare_a + JPEG_SENTINEL), jpeg_compare_in[0], jpeg_compare_in[1], jpeg_compare_in[2], count);
                        pixels_from_ycbcr((p32 address_to)(jpeg_compare_b + JPEG_SENTINEL), jpeg_compare_in[0], jpeg_compare_in[1], jpeg_compare_in[2], count);
                        wrong += memory_compare(jpeg_compare_a, jpeg_compare_b, sizeof(jpeg_compare_a)) != 0;
                        jpeg_compare_reset();
                        jpeg_reference_pixels_from_grey((p32 address_to)(jpeg_compare_a + JPEG_SENTINEL), jpeg_compare_in[0], count);
                        pixels_from_grey((p32 address_to)(jpeg_compare_b + JPEG_SENTINEL), jpeg_compare_in[0], count);
                        wrong += memory_compare(jpeg_compare_a, jpeg_compare_b, sizeof(jpeg_compare_a)) != 0;
                        jpeg_compare_reset();
                        jpeg_reference_pixels_from_planes((p32 address_to)(jpeg_compare_a + JPEG_SENTINEL), jpeg_compare_in[0], jpeg_compare_in[1], jpeg_compare_in[2], count);
                        pixels_from_planes((p32 address_to)(jpeg_compare_b + JPEG_SENTINEL), jpeg_compare_in[0], jpeg_compare_in[1], jpeg_compare_in[2], count);
                        wrong += memory_compare(jpeg_compare_a, jpeg_compare_b, sizeof(jpeg_compare_a)) != 0;
                }
        return wrong;
}

static positive jpeg_compare_to_ycbcr(void)
{
        static p32 from[65536];
        static p8 want[3][65536], got[3][65536];
        positive wrong = 0;

        // Every RGB there is, a row of 65536 at a time, each word's alpha byte a different value (it is not read).
        for (positive r = 0; r < 256; r++)
        {
                for (positive at = 0; at < 65536; at++)
                        from[at] = (p32)(at * 2654435761u) << 24 | (p32)r << 16 | (p32)at;
                jpeg_reference_pixels_to_ycbcr(want[0], want[1], want[2], from, 65536);
                pixels_to_ycbcr(got[0], got[1], got[2], from, 65536);
                wrong += memory_compare(want, got, sizeof(want)) != 0;
        }
        for (positive at = 0; at < 71 + 6 * 2; at++)
        {
                positive count = jpeg_counts(at);
                p32 address_to words = (p32 address_to)jpeg_compare_in[0];

                for (positive k = 0; k < count && k < 1050; k++)
                        words[k] = jpeg_next();
                if (count > 1050)
                        continue;
                jpeg_compare_reset();
                jpeg_reference_pixels_to_ycbcr(jpeg_compare_a + JPEG_SENTINEL, jpeg_compare_a + 2 * JPEG_SENTINEL + count,
                                               jpeg_compare_a + 3 * JPEG_SENTINEL + 2 * count, words, count);
                pixels_to_ycbcr(jpeg_compare_b + JPEG_SENTINEL, jpeg_compare_b + 2 * JPEG_SENTINEL + count, jpeg_compare_b + 3 * JPEG_SENTINEL + 2 * count,
                                words, count);
                wrong += memory_compare(jpeg_compare_a, jpeg_compare_b, sizeof(jpeg_compare_a)) != 0;
        }
        return wrong;
}

static positive jpeg_compare_samples(void)
{
        positive wrong = 0;

        for (positive at = 0; at < 71 + 6 * 2; at++)
                for (p32 kind = 0; kind < 3; kind++)
                {
                        positive count = jpeg_counts(at);

                        jpeg_compare_fill(count, kind);
                        if (count)
                        {
                                jpeg_compare_reset();
                                jpeg_reference_samples_double(jpeg_compare_a + JPEG_SENTINEL, jpeg_compare_in[0], count);
                                samples_double(jpeg_compare_b + JPEG_SENTINEL, jpeg_compare_in[0], count);
                                wrong += memory_compare(jpeg_compare_a, jpeg_compare_b, sizeof(jpeg_compare_a)) != 0;
                                jpeg_compare_reset();
                                jpeg_reference_samples_double_rows(jpeg_compare_a + JPEG_SENTINEL, jpeg_compare_in[0], jpeg_compare_in[1], count);
                                samples_double_rows(jpeg_compare_b + JPEG_SENTINEL, jpeg_compare_in[0], jpeg_compare_in[1], count);
                                wrong += memory_compare(jpeg_compare_a, jpeg_compare_b, sizeof(jpeg_compare_a)) != 0;
                        }
                        for (p32 bias = 0; bias < 8; bias++)
                        {
                                jpeg_compare_reset();
                                jpeg_reference_samples_blend_rows(jpeg_compare_a + JPEG_SENTINEL, jpeg_compare_in[0], jpeg_compare_in[1], count, bias);
                                samples_blend_rows(jpeg_compare_b + JPEG_SENTINEL, jpeg_compare_in[0], jpeg_compare_in[1], count, bias);
                                wrong += memory_compare(jpeg_compare_a, jpeg_compare_b, sizeof(jpeg_compare_a)) != 0;
                        }
                }
        return wrong;
}

// The reduced inverse DCTs over the blocks jpeg_block_of makes, every kind, into rows at a pitch of 37 with sentinels about them.
static positive jpeg_compare_reduced(positive blocks)
{
        static b16 c[64];
        static p16 q[64];
        static p8 want[4 * 37 + 16], got[4 * 37 + 16];
        positive wrong = 0;

        for (positive at = 0; at < blocks; at++)
        {
                jpeg_block_of(c, q, (p32)(at % 5));
                memory_fill(want, 0x5a, sizeof(want)), memory_fill(got, 0x5a, sizeof(got));
                jpeg_reference_idct_4x4(want + 5, 37, c, q);
                jpeg_idct_4x4(got + 5, 37, c, q);
                wrong += memory_compare(want, got, sizeof(want)) != 0;
                memory_fill(want, 0x5a, sizeof(want)), memory_fill(got, 0x5a, sizeof(got));
                jpeg_reference_idct_2x2(want + 5, 37, c, q);
                jpeg_idct_2x2(got + 5, 37, c, q);
                wrong += memory_compare(want, got, sizeof(want)) != 0;
        }
        return wrong;
}

/* ---- end of the comparisons ---- */

/*
        A page that faults after each array a routine reads or writes, so a read or write past an end is caught; the output's
        rows lie in a region with sentinel bytes around each row, at an odd pitch, so a byte written outside a row is seen.
*/
static p8 address_to jpeg_page_end(positive bytes)
{
        positive span = (bytes + 4095) / 4096 * 4096;
        p8 address_to pages = memory(span + 4096);

        if (!pages || system_call_3(syscall(mprotect), (positive)(pages + span), 4096, 0) != 0)
                return null;
        return pages + span - bytes;
}

#ifdef CHECK_jpeg
// jpeg_idct_8x8 over kinds 0 to 4, its inputs ending against faulting pages and its rows at a pitch of 37 with 0x5a about them.
static positive jpeg_idct_8x8_wrong(positive blocks)
{
        static p8 want[8 * 37 + 16], got[8 * 37 + 16];
        b16 address_to c = (b16 address_to)jpeg_page_end(128);
        p16 address_to q = (p16 address_to)jpeg_page_end(128);
        positive wrong = 0;

        if (!c || !q)
                return blocks;
        for (positive at = 0; at < blocks; at++)
        {
                jpeg_block_of(c, q, (p32)(at % 5));
                memory_fill(want, 0x5a, sizeof(want)), memory_fill(got, 0x5a, sizeof(got));
                jpeg_reference_idct_8x8(want + 5, 37, c, q);
                jpeg_idct_8x8(got + 5, 37, c, q);
                wrong += memory_compare(want, got, sizeof(want)) != 0;
        }
        return wrong;
}

static fn jpeg_tier(string_address tier)
{
        positive wrong = jpeg_idct_8x8_wrong(400000), colour = jpeg_compare_colour(), to = jpeg_compare_to_ycbcr(), samples = jpeg_compare_samples();
        positive reduced = jpeg_compare_reduced(400000);

        string_format(log, "  %s: jpeg_idct_8x8 %p of 400000 blocks differ; colour %p, to YCbCr %p, samples %p cases differ; the reduced "
                      "%p of 800000\n", tier, wrong, colour, to, samples, reduced);
        check("jpeg_idct_8x8: any coefficients and quantizers, honest, extreme, large and smooth blocks, the reference's bytes and "
              "nothing outside its rows or past its arrays",
              wrong == 0);
        check("pixels_from_ycbcr, pixels_from_grey and pixels_from_planes: every input there is, every count to 70 and longer, the "
              "reference's words and nothing outside them",
              colour == 0);
        check("jpeg_idct_4x4 and jpeg_idct_2x2: any coefficients and quantizers, honest, extreme, large and smooth blocks, the "
              "reference's bytes and nothing outside their rows",
              reduced == 0);
        check("pixels_to_ycbcr: every RGB there is (the alpha not read), counts to 70, the reference's bytes and nothing outside them", to == 0);
        check("samples_double, samples_double_rows and samples_blend_rows: every count to 70 and longer, random, extreme and ramp "
              "samples, every bias, the reference's bytes and nothing outside them",
              samples == 0);
}

b32 main()
{
#if X64
        p8 wide512 = cpu_has_avx512, wide2 = cpu_has_avx2;

        for (p32 tier = 0; tier < 3; tier++)
        {
                cpu_has_avx512 = tier == 0 ? wide512 : 0;
                cpu_has_avx2 = tier < 2 ? wide2 : 0;
                jpeg_tier(cpu_has_avx512 ? "avx512" : cpu_has_avx2 ? "avx2" : "sse2");
        }
        cpu_has_avx512 = wide512, cpu_has_avx2 = wide2;
#elif RISCV64
        jpeg_tier("rv64");
#else
        jpeg_tier("neon");
#endif
        return test_report(null);
}
#endif /* CHECK_jpeg */

#ifdef BENCH_jpeg
/*
        Each routine against its reference, the two in turn a round at a time after a round to warm them, the best of twelve:
        jpeg_idct_8x8 and the reduced ones over 4096 honest blocks (kind 1) and 4096 smooth ones (kind 4), in get_cpu_time's ticks (the time stamp
        counter on x86_64, the generic timer on arm64).
*/
#define jpeg_now() ((p64)get_cpu_time())

static fn jpeg_bench_idct(p32 kind, string_address name)
{
        enum { BLOCKS = 4096 };
        static b16 c[BLOCKS][64];
        static p16 q[64];
        static p8 out[BLOCKS][64];
        p64 best_reference = ~0ull, best = ~0ull, reduced2_reference = ~0ull, reduced2 = ~0ull;

        for (int at = 0; at < BLOCKS; at++)
                jpeg_block_of(c[at], q, kind);
        for (int k = 0; k < 64; k++)
                q[k] = (p16)(1 + jpeg_next() % 60);
        for (int round = 0; round < 13; round++)
        {
                p64 t0 = jpeg_now();
                for (int at = 0; at < BLOCKS; at++)
                        jpeg_reference_idct_8x8(out[at], 8, c[at], q);
                p64 t1 = jpeg_now();
                for (int at = 0; at < BLOCKS; at++)
                        jpeg_idct_8x8(out[at], 8, c[at], q);
                p64 t2 = jpeg_now();
                if (round)
                        best_reference = t1 - t0 < best_reference ? t1 - t0 : best_reference, best = t2 - t1 < best ? t2 - t1 : best;
        }
        string_format(log, "  jpeg_idct_8x8 %s blocks: reference %p, lib.c %p (hundredths of a tick a block)\n", name,
                      (positive)(best_reference * 100 / BLOCKS), (positive)(best * 100 / BLOCKS));
        best_reference = best = ~0ull;
        for (int round = 0; round < 13; round++)
        {
                p64 t0 = jpeg_now();
                for (int at = 0; at < BLOCKS; at++)
                        jpeg_reference_idct_4x4(out[at], 8, c[at], q);
                p64 t1 = jpeg_now();
                for (int at = 0; at < BLOCKS; at++)
                        jpeg_idct_4x4(out[at], 8, c[at], q);
                p64 t2 = jpeg_now();
                for (int at = 0; at < BLOCKS; at++)
                        jpeg_reference_idct_2x2(out[at], 8, c[at], q);
                p64 t3 = jpeg_now();
                for (int at = 0; at < BLOCKS; at++)
                        jpeg_idct_2x2(out[at], 8, c[at], q);
                p64 t4 = jpeg_now();
                if (round)
                {
                        best_reference = t1 - t0 < best_reference ? t1 - t0 : best_reference, best = t2 - t1 < best ? t2 - t1 : best;
                        reduced2_reference = t3 - t2 < reduced2_reference ? t3 - t2 : reduced2_reference;
                        reduced2 = t4 - t3 < reduced2 ? t4 - t3 : reduced2;
                }
        }
        string_format(log, "  jpeg_idct_4x4 %s blocks: reference %p, lib.c %p; jpeg_idct_2x2: reference %p, lib.c %p (hundredths of a tick)\n",
                      name, (positive)(best_reference * 100 / BLOCKS), (positive)(best * 100 / BLOCKS),
                      (positive)(reduced2_reference * 100 / BLOCKS), (positive)(reduced2 * 100 / BLOCKS));
}

//      A row of 3840 samples (a 4K picture's) through each of the rest, the reference and the routine in turn; ticks a row.
#define JPEG_BENCH_ROW(NAME, REFERENCE, ROUTINE)                                                                                         \
        do                                                                                                                              \
        {                                                                                                                               \
                p64 best_reference = ~0ull, best = ~0ull;                                                                               \
                for (int round = 0; round < 41; round++)                                                                                \
                {                                                                                                                       \
                        p64 t0 = jpeg_now();                                                                                            \
                        REFERENCE;                                                                                                      \
                        p64 t1 = jpeg_now();                                                                                            \
                        ROUTINE;                                                                                                        \
                        p64 t2 = jpeg_now();                                                                                            \
                        if (round)                                                                                                      \
                                best_reference = t1 - t0 < best_reference ? t1 - t0 : best_reference, best = t2 - t1 < best ? t2 - t1 : best; \
                }                                                                                                                       \
                string_format(log, "  %s, 3840 a row: reference %p, lib.c %p ticks\n", (string_address)NAME, (positive)best_reference, (positive)best); \
        } while (0)

static fn jpeg_bench_rows(void)
{
        static p8 y[3840 + 64], cb[3840 + 64], cr[3840 + 64], out8[2 * 3840 + 64], planes[3][3840];
        static p32 out[3840];

        for (int at = 0; at < 3840 + 64; at++)
                y[at] = (p8)jpeg_next(), cb[at] = (p8)(96 + jpeg_next() % 64), cr[at] = (p8)(96 + jpeg_next() % 64);
        JPEG_BENCH_ROW("pixels_from_ycbcr", jpeg_reference_pixels_from_ycbcr(out, y, cb, cr, 3840), pixels_from_ycbcr(out, y, cb, cr, 3840));
        JPEG_BENCH_ROW("pixels_from_grey", jpeg_reference_pixels_from_grey(out, y, 3840), pixels_from_grey(out, y, 3840));
        JPEG_BENCH_ROW("pixels_from_planes", jpeg_reference_pixels_from_planes(out, y, cb, cr, 3840), pixels_from_planes(out, y, cb, cr, 3840));
        JPEG_BENCH_ROW("pixels_to_ycbcr", jpeg_reference_pixels_to_ycbcr(planes[0], planes[1], planes[2], out, 3840),
                       pixels_to_ycbcr(planes[0], planes[1], planes[2], out, 3840));
        JPEG_BENCH_ROW("samples_double (1920 to 3840)", jpeg_reference_samples_double(out8, cb, 1920), samples_double(out8, cb, 1920));
        JPEG_BENCH_ROW("samples_double_rows (1920 to 3840)", jpeg_reference_samples_double_rows(out8, cb, cr, 1920),
                       samples_double_rows(out8, cb, cr, 1920));
        JPEG_BENCH_ROW("samples_blend_rows", jpeg_reference_samples_blend_rows(out8, cb, cr, 3840, 1), samples_blend_rows(out8, cb, cr, 3840, 1));
}

b32 main()
{
        jpeg_bench_idct(1, "honest");
        jpeg_bench_idct(4, "smooth");
        jpeg_bench_rows();
        log_flush();
        return 0;
}
#endif /* BENCH_jpeg */
#endif /* CHECK_jpeg || BENCH_jpeg */
