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
        positive wrong = jpeg_idct_8x8_wrong(400000);

        string_format(log, "  %s: jpeg_idct_8x8 %p of 400000 blocks differ\n", tier, wrong);
        check("jpeg_idct_8x8: any coefficients and quantizers, honest, extreme, large and smooth blocks, the reference's bytes and "
              "nothing outside its rows or past its arrays",
              wrong == 0);
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
        jpeg_idct_8x8 over 4096 honest blocks (kind 1) and 4096 smooth ones (kind 4), in get_cpu_time's ticks (the time stamp
        counter on x86_64, the generic timer on arm64).
*/
#define jpeg_now() ((p64)get_cpu_time())

static fn jpeg_bench_idct(p32 kind, string_address name)
{
        enum { BLOCKS = 4096 };
        static b16 c[BLOCKS][64];
        static p16 q[64];
        static p8 out[BLOCKS][64];
        p64 best_reference = ~0ull, best = ~0ull;

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
}

b32 main()
{
        jpeg_bench_idct(1, "honest");
        jpeg_bench_idct(4, "smooth");
        log_flush();
        return 0;
}
#endif /* BENCH_jpeg */
#endif /* CHECK_jpeg || BENCH_jpeg */
