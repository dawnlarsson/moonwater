/*
        test/checks/audiolib.c: the audio library's routines.

        A run of test/checks.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. A section is built as
        test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib -nostartfiles
        -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its
        freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name>
        test/checks.c).
*/

#ifdef CHECK_audiolib
#include "../../src/lib.util.c"

#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

#define SHARED_audio_loops
#include "../checks.c"
#undef SHARED_audio_loops

//      The mixing core as the service has it, with lib.c in the translation unit: the loops it calls are the assembly.
#ifndef offset_of
#include <stddef.h>
#define offset_of(type, member) offsetof(type, member)
#endif
#include "../../src/audio/mix.c"
/*
        CHECK_audiolib: lib.c's audio routines, the loops of the sound server's mixing core (src/audio/mix.c), on the
        three machines and on every body of each: x86_64 once for AVX2 with FMA, once for AVX2 without it and once for
        the SSE2 floor, riscv64 under qemu with V at the vector lengths test/run gives it and without, arm64 once.
        Each is held to the C it replaced (SHARED_audio_loops, which CHECK_audio_mix also uses) and to a sum in double,
        over every count and every placement the routine's contract allows, with the arrays ending against a page that
        faults and starting just after one, so that a read or a write one element out of bounds is a fault.

        resample_polyphase2_f32: tables of taps 64 to 512 and phases 1 to 512, positions and steps from nothing to
        eight inputs an output, counts to 512. Every output is within (taps / 8 + 12) eps of the exact sum of the
        |terms| (the bound in lib.c's comment); the SSE2 body is held to the C's bits; small integers and a weight of
        a half are exact on every body; and the weight itself is read out of the routine (rows of nothing and of one,
        one tap of each plane one, so that an output is f and nothing else) and held to its definition to the bit.
        Then the mixing core itself, mix.c with the assembly under it: a stereo stream of two tones through its voice
        and sink at four ratios of rates, held to the tones the output's own times ask for.
*/
static unsigned long long al_state = 0x2545f4914f6cdd1dull;

static unsigned long long al_next(void)
{
        XORSHIFT64(al_state);
        return al_state;
}

//      A float in [-1, 1) with 24 bits.
static float al_unit(void)
{
        return (float)(int)(al_next() >> 40) / 8388608.0f - 1.0f;
}

static unsigned al_bits(float value)
{
        unsigned bits;

        __builtin_memcpy(&bits, &value, 4);
        return bits;
}

static double al_abs(double value)
{
        return value < 0 ? -value : value;
}

/*
        Five regions, each a run of floats with a page that faults at either end: the arrays are put against one end
        or the other.
*/
#define AL_REGION (1u << 21)
#define AL_REGIONS 5

static p8 address_to al_low[AL_REGIONS];
static p8 address_to al_high[AL_REGIONS];

static p8 al_guard(void)
{
        for (int at = 0; at < AL_REGIONS; at++)
        {
                p8 address_to pages = memory(AL_REGION + 3 * 4096);
                positive first;

                if (!pages)
                        return false;
                first = ((positive)pages + 4095) & ~(positive)4095;
                if (system_call_3(syscall(mprotect), first, 4096, 0) != 0 ||
                    system_call_3(syscall(mprotect), first + 4096 + AL_REGION, 4096, 0) != 0)
                        return false;
                al_low[at] = (p8 address_to)(first + 4096);
                al_high[at] = (p8 address_to)(first + 4096 + AL_REGION);
        }
        return true;
}

//      A first call in a process on riscv64 asks the machine for V; the walk can make every call that first call.
static p8 al_first;

typedef struct
{
        unsigned long long cases, bound, bits, outside, kept, exact, weight;
} al_faults;

static void al_polyphase_routine(float *o0, float *o1, const float *x0, const float *x1, const float *table, unsigned long long taps,
                                 unsigned long long phases, unsigned long long pos, unsigned long long step, unsigned long long count)
{
        VECTOR_FIRST_CALL(al_first);
        resample_polyphase2_f32(o0, o1, x0, x1, table, taps, phases, pos, step, count);
}

//      The place the inputs end: the window of the last output, taps floats from the index.
static unsigned long long al_polyphase_need(unsigned long long taps, unsigned long long pos, unsigned long long step,
                                            unsigned long long count)
{
        return (count ? (pos + (count - 1) * step) >> 32 : pos >> 32) + taps;
}

/*
        One case through the routine: where the arrays sit (at the end of their regions or the start), what to fill
        them with (kind 0 random, 1 small integers, 2 the weight's probe), the bits it is to answer in (bits: the C's
        to the bit, else the bound).
*/
static void al_polyphase_case(unsigned long long taps, unsigned long long phases, unsigned long long count, unsigned long long pos,
                              unsigned long long step, int front, int kind, int bits, al_faults *faults)
{
        static double want[2][512], unit[2][512];
        unsigned long long need = al_polyphase_need(taps, pos, step, count), rows = (phases + 1) * taps;
        float *x[2], *table, *out[2];

        if (need * 4 > AL_REGION || rows * 4 > AL_REGION)
                return;
        faults->cases++;
        x[0] = front ? (float *)al_low[0] : (float *)al_high[0] - need;
        x[1] = front ? (float *)al_low[1] : (float *)al_high[1] - need;
        table = front ? (float *)al_low[2] : (float *)al_high[2] - rows;
        out[0] = front ? (float *)al_low[3] + 16 : (float *)al_high[3] - count;
        out[1] = front ? (float *)al_low[4] + 16 : (float *)al_high[4] - count;
        for (unsigned long long i = 0; i < need; i++)
        {
                x[0][i] = kind == 0 ? al_unit() : kind == 1 ? (float)(int)(al_next() % 65) - 32 : 0.0f;
                x[1][i] = kind == 0 ? al_unit() : kind == 1 ? (float)(int)(al_next() % 65) - 32 : 0.0f;
        }
        if (kind == 2)
        {
                //      One tap of each plane is one, so an output is that tap's coefficient and nothing else.
                x[0][(pos >> 32) + al_next() % taps] = 1.0f;
                x[1][(pos >> 32) + al_next() % taps] = 1.0f;
        }
        for (unsigned long long p = 0; p <= phases; p++)
                for (unsigned long long k = 0; k < taps; k++)
                        table[p * taps + k] = kind == 0 ? ((al_next() & 3) ? al_unit() * (float)(1 + (p & 3)) : 0.0f)
                                                         : kind == 1 ? (float)(int)(al_next() % 9) * 2 - 8 : (p == phases ? 1.0f : 0.0f);
        for (int c = 0; c < 2; c++)
                for (int i = 0; i < 16; i++)
                        out[c][i - 16] = (float)0x1234 + (float)i;
        {
                //      The arrays as they were, to hold the routine to not having written them.
                unsigned long long sum_x = 0, sum_t = 0;

                for (unsigned long long i = 0; i < need; i++)
                        sum_x += al_bits(x[0][i]) + 3 * al_bits(x[1][i]);
                for (unsigned long long i = 0; i < rows; i++)
                        sum_t += al_bits(table[i]) * (i | 1);
                al_polyphase_routine(out[0], out[1], x[0], x[1], table, taps, phases, pos, step, count);
                for (unsigned long long i = 0; i < need; i++)
                        sum_x -= al_bits(x[0][i]) + 3 * al_bits(x[1][i]);
                for (unsigned long long i = 0; i < rows; i++)
                        sum_t -= al_bits(table[i]) * (i | 1);
                faults->kept += sum_x != 0 || sum_t != 0;
        }
        for (int c = 0; c < 2; c++)
                for (int i = 0; i < 16; i++)
                        faults->outside += al_bits(out[c][i - 16]) != al_bits((float)0x1234 + (float)i);

        //      The definition, in double: the same weight (the one float the contract names), the exact row, the exact sum.
        {
                unsigned long long at = pos;

                for (unsigned long long m = 0; m < count; m++, at += step)
                {
                        unsigned long long index = at >> 32, scaled = (at & 0xffffffffu) * phases, phase = scaled >> 32;
                        float f = (float)(scaled & 0xffffffffu) * (1.0f / 4294967296.0f);
                        const float *a = table + phase * taps, *b = a + taps;
                        double s[2] = {0, 0}, u[2] = {0, 0};

                        for (unsigned long long k = 0; k < taps; k++)
                        {
                                double c = (double)a[k] + (double)f * ((double)b[k] - (double)a[k]);
                                double w = al_abs((double)a[k]) + al_abs((double)b[k]);

                                s[0] += c * (double)x[0][index + k];
                                s[1] += c * (double)x[1][index + k];
                                u[0] += w * al_abs((double)x[0][index + k]);
                                u[1] += w * al_abs((double)x[1][index + k]);
                        }
                        want[0][m] = s[0], want[1][m] = s[1], unit[0][m] = u[0], unit[1][m] = u[1];
                        if (kind == 2)
                                want[0][m] = want[1][m] = (double)f;
                }
        }
        if (bits)
        {
                //      The C's own, from the same inputs.
                static float again[2][512];

                audio_c_polyphase2(again[0], again[1], x[0], x[1], table, taps, phases, pos, step, count);
                for (unsigned long long m = 0; m < count; m++)
                {
                        faults->bits += al_bits(out[0][m]) != al_bits(again[0][m]);
                        faults->bits += al_bits(out[1][m]) != al_bits(again[1][m]);
                }
        }
        for (unsigned long long m = 0; m < count; m++)
                for (int c = 0; c < 2; c++)
                {
                        double bound = ((double)taps / 8.0 + 12.0) * 0x1p-24 * unit[c][m] + 1e-30;
                        double error = al_abs((double)out[c][m] - want[c][m]);

                        if (kind == 0)
                                faults->bound += error > bound;
                        else
                        {
                                //      Exact: integers and a half, or the weight whole.
                                faults->exact += kind == 1 && al_bits(out[c][m]) != al_bits((float)want[c][m]);
                                faults->weight += kind == 2 && al_bits(out[c][m]) != al_bits((float)want[c][m]);
                        }
                }
}

static const unsigned long long al_polyphase_steps[] = {0, 1, 0x3ffffff, 0x80000000ull, 0xeb851eb8ull, 0x100000000ull, 0x180000000ull,
                                                  0x20000007bull, 0x7e0000000ull, 0x0000000100000001ull};
#define AL_STEPS (sizeof(al_polyphase_steps) / sizeof(al_polyphase_steps[0]))

static void al_polyphase_walk(int bits, al_faults *faults)
{
        static const unsigned long long taps_list[] = {64, 128, 192, 256, 320, 384, 448, 512};
        static const unsigned long long counts[] = {0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31, 33, 63, 100, 255, 256, 257, 511, 512};
        static const unsigned long long phase_list[] = {1, 2, 3, 7, 64, 100, 255, 511, 512};

        //      Every count with every tap length and a step each, on both placements.
        for (unsigned long long ti = 0; ti < 8; ti++)
                for (unsigned long long ci = 0; ci < sizeof(counts) / sizeof(counts[0]); ci++)
                        for (int front = 0; front < 2; front++)
                        {
                                unsigned long long phases = phase_list[(ti + ci) % 9];
                                unsigned long long step = al_polyphase_steps[(ti * 3 + ci) % AL_STEPS];
                                unsigned long long pos = front ? (al_next() & 0xffffffffu) : (al_next() % 12) << 32 | (al_next() & 0xffffffffu);

                                al_polyphase_case(taps_list[ti], phases, counts[ci], pos, step, front, 0, bits, faults);
                        }
        //      The longest filter the mixer makes: 1024 taps (a ratio of eight, 384 kHz into 48), at 64 phases so the table fits its region.
        for (unsigned long long ci = 0; ci < sizeof(counts) / sizeof(counts[0]); ci++)
                for (int front = 0; front < 2; front++)
                        al_polyphase_case(1024, 64, counts[ci], front ? (al_next() & 0xffffffffu) : (al_next() % 12) << 32 | (al_next() & 0xffffffffu),
                                          al_polyphase_steps[(ci + front) % AL_STEPS], front, 0, bits, faults);
        //      Random shapes.
        for (int round = 0; round < 700; round++)
        {
                unsigned long long taps = taps_list[al_next() % 8], phases = 1 + al_next() % 512, count = al_next() % 70;
                unsigned long long step = (round % 4 == 0) ? al_next() % 0x800000000ull : 0xd0000000ull + al_next() % 0x40000000ull;
                unsigned long long pos = (al_next() % 20) << 32 | (al_next() & 0xffffffffu);

                if (round % 9 == 0)
                        phases = 1 + al_next() % (512 / (taps / 64));
                al_polyphase_case(taps, phases, count, pos, step, round & 1, 0, bits, faults);
        }
        //      Small integers with a weight of a half: one phase, the fraction 2^31. Every step of 1 or 0: the weight stays.
        for (unsigned long long ti = 0; ti < 8; ti++)
                for (int round = 0; round < 6; round++)
                        al_polyphase_case(taps_list[ti], 1, 1 + al_next() % 40, 0x80000000ull | (unsigned long long)(al_next() % 5) << 32, round & 1 ? 0x100000000ull : 0,
                                          round & 2, 1, bits, faults);
        //      The weight: rows of nothing then one, one tap of each plane one, a weight at every kind of fraction.
        for (unsigned long long ti = 0; ti < 8; ti++)
        {
                static const unsigned long long fractions[] = {0, 1, 0x7f, 0x800000, 0x1000001, 0x1000003, 0x7fffffff, 0x80000000ull, 0x80000001ull,
                                                               0xfffffffeull, 0xffffffffull, 0x55555555, 0xaaaaaaaa};

                for (unsigned long long fi = 0; fi < sizeof(fractions) / sizeof(fractions[0]); fi++)
                        //      phases 1: the fraction is the weight; step 0 repeats it.
                        for (int again = 0; again < 3; again++)
                                al_polyphase_case(taps_list[ti], 1, 3, fractions[fi], 0, (int)(fi & 1), 2, bits, faults);
        }
}

/*
        resample_polyphase2_f32_rows. The table is exactly phases rows and put against the end of its region (or at the
        start of one, with a page that faults before it), so that a read of the row after the last is a fault; the answer
        is held to pos + count * step; the outputs to the sum in double of the row the walk names (the row of pos, or,
        for a ratio p / q handed over as the caller does, a fraction rounded up and a step rounded up, the row
        (k + m * p) mod q and the index that goes with it, integers all the way, which is the caller's guarantee tested
        end to end); the SSE2 body to the C's bits; and where the weight of the weighted routine is nothing (phases a
        power of two, pos and step multiples of a row's share of a unit) to the weighted routine's bits.
*/
static float al_rows_table[513 * 512], al_rows_other[2][512];

static unsigned long long al_rows_routine(float *o0, float *o1, const float *x0, const float *x1, const float *table,
                                          unsigned long long taps, unsigned long long phases, unsigned long long pos,
                                          unsigned long long step, unsigned long long count)
{
        VECTOR_FIRST_CALL(al_first);
        return resample_polyphase2_f32_rows(o0, o1, x0, x1, table, taps, phases, pos, step, count);
}

static void al_rows_case(unsigned long long taps, unsigned long long phases, unsigned long long count, unsigned long long pos,
                         unsigned long long step, int front, int bits, int identity, unsigned long long ratio_p,
                         unsigned long long ratio_q, unsigned long long ratio_k, al_faults *faults)
{
        static double want[2][512], unit[2][512];
        unsigned long long need = al_polyphase_need(taps, pos, step, count), rows = phases * taps, returned;
        float *x[2], *table, *out[2];

        if (need * 4 > AL_REGION || rows * 4 > AL_REGION)
                return;
        faults->cases++;
        x[0] = front ? (float *)al_low[0] : (float *)al_high[0] - need;
        x[1] = front ? (float *)al_low[1] : (float *)al_high[1] - need;
        table = front ? (float *)al_low[2] : (float *)al_high[2] - rows;
        out[0] = front ? (float *)al_low[3] + 16 : (float *)al_high[3] - count;
        out[1] = front ? (float *)al_low[4] + 16 : (float *)al_high[4] - count;
        for (unsigned long long i = 0; i < need; i++)
        {
                x[0][i] = al_unit();
                x[1][i] = al_unit();
        }
        for (unsigned long long i = 0; i < rows; i++)
                table[i] = (al_next() & 3) ? al_unit() * (float)(1 + ((i / taps) & 3)) : 0.0f;
        for (int c = 0; c < 2; c++)
                for (int i = 0; i < 16; i++)
                        out[c][i - 16] = (float)0x1234 + (float)i;
        {
                unsigned long long sum_x = 0, sum_t = 0;

                for (unsigned long long i = 0; i < need; i++)
                        sum_x += al_bits(x[0][i]) + 3 * al_bits(x[1][i]);
                for (unsigned long long i = 0; i < rows; i++)
                        sum_t += al_bits(table[i]) * (i | 1);
                returned = al_rows_routine(out[0], out[1], x[0], x[1], table, taps, phases, pos, step, count);
                for (unsigned long long i = 0; i < need; i++)
                        sum_x -= al_bits(x[0][i]) + 3 * al_bits(x[1][i]);
                for (unsigned long long i = 0; i < rows; i++)
                        sum_t -= al_bits(table[i]) * (i | 1);
                faults->kept += sum_x != 0 || sum_t != 0;
        }
        for (int c = 0; c < 2; c++)
                for (int i = 0; i < 16; i++)
                        faults->outside += al_bits(out[c][i - 16]) != al_bits((float)0x1234 + (float)i);
        faults->exact += returned != pos + count * step;
        {
                unsigned long long at = pos;

                for (unsigned long long m = 0; m < count; m++, at += step)
                {
                        unsigned long long index = at >> 32, phase = ((at & 0xffffffffu) * phases) >> 32;
                        const float *a;
                        double s[2] = {0, 0}, u[2] = {0, 0};

                        if (ratio_q)
                        {
                                unsigned long long walked = ratio_k + m * ratio_p;

                                phase = walked % ratio_q;
                                index = (pos >> 32) + walked / ratio_q;
                        }
                        a = table + phase * taps;
                        for (unsigned long long k = 0; k < taps; k++)
                        {
                                s[0] += (double)a[k] * (double)x[0][index + k];
                                s[1] += (double)a[k] * (double)x[1][index + k];
                                u[0] += al_abs((double)a[k]) * al_abs((double)x[0][index + k]);
                                u[1] += al_abs((double)a[k]) * al_abs((double)x[1][index + k]);
                        }
                        want[0][m] = s[0], want[1][m] = s[1], unit[0][m] = u[0], unit[1][m] = u[1];
                }
        }
        if (bits)
        {
                static float again[2][512];
                unsigned long long ended = audio_c_polyphase2_rows(again[0], again[1], x[0], x[1], table, taps, phases, pos, step, count);

                faults->exact += ended != returned;
                for (unsigned long long m = 0; m < count; m++)
                {
                        faults->bits += al_bits(out[0][m]) != al_bits(again[0][m]);
                        faults->bits += al_bits(out[1][m]) != al_bits(again[1][m]);
                }
        }
        if (identity)
        {
                //      The weighted routine over the same rows and one more, at a weight of nothing.
                for (unsigned long long i = 0; i < rows; i++)
                        al_rows_table[i] = table[i];
                for (unsigned long long i = 0; i < taps; i++)
                        al_rows_table[rows + i] = al_unit();
                al_polyphase_routine(al_rows_other[0], al_rows_other[1], x[0], x[1], al_rows_table, taps, phases, pos, step, count);
                for (unsigned long long m = 0; m < count; m++)
                {
                        faults->weight += al_bits(out[0][m]) != al_bits(al_rows_other[0][m]);
                        faults->weight += al_bits(out[1][m]) != al_bits(al_rows_other[1][m]);
                }
        }
        for (unsigned long long m = 0; m < count; m++)
                for (int c = 0; c < 2; c++)
                {
                        double bound = ((double)taps / 8.0 + 12.0) * 0x1p-24 * unit[c][m] + 1e-30;

                        faults->bound += al_abs((double)out[c][m] - want[c][m]) > bound;
                }
}

static void al_rows_walk(int bits, al_faults *faults)
{
        static const unsigned long long taps_list[] = {64, 128, 192, 256, 320, 384, 448, 512};
        static const unsigned long long counts[] = {0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31, 33, 63, 100, 255, 256, 257, 511, 512};
        static const unsigned long long phase_list[] = {1, 2, 3, 7, 64, 100, 160, 255, 320, 511, 512};
        //      p / q in lowest terms: the ratios of the common rates into 48 kHz (44.1 and 22.05, 32, 96, 192, 24, 16, 8, 11.025),
        //      the end phases of a table of 512, a prime, and a ratio of 1.
        static const unsigned long long ratios[][2] = {{147, 160}, {147, 320}, {2, 3}, {2, 1}, {4, 1}, {1, 2}, {1, 3}, {1, 6}, {49, 220},
                                                      {1, 512}, {511, 512}, {333, 509}, {1, 1}};

        for (unsigned long long ti = 0; ti < 8; ti++)
                for (unsigned long long ci = 0; ci < sizeof(counts) / sizeof(counts[0]); ci++)
                        for (int front = 0; front < 2; front++)
                        {
                                unsigned long long phases = phase_list[(ti + ci) % 11];
                                unsigned long long step = al_polyphase_steps[(ti * 3 + ci) % AL_STEPS];
                                unsigned long long pos = front ? (al_next() & 0xffffffffu) : (al_next() % 12) << 32 | (al_next() & 0xffffffffu);

                                al_rows_case(taps_list[ti], phases, counts[ci], pos, step, front, bits, 0, 0, 0, 0, faults);
                        }
        for (int round = 0; round < 500; round++)
        {
                unsigned long long taps = taps_list[al_next() % 8], phases = 1 + al_next() % 512, count = al_next() % 70;
                unsigned long long step = (round % 4 == 0) ? al_next() % 0x800000000ull : 0xd0000000ull + al_next() % 0x40000000ull;
                unsigned long long pos = (al_next() % 20) << 32 | (al_next() & 0xffffffffu);

                if (round % 9 == 0)
                        phases = 1 + al_next() % (512 / (taps / 64));
                al_rows_case(taps, phases, count, pos, step, round & 1, bits, 0, 0, 0, 0, faults);
        }
        //      A weight of nothing: phases a power of two, pos and step multiples of a row's share of a unit.
        for (unsigned long long ti = 0; ti < 8; ti++)
                for (int round = 0; round < 12; round++)
                {
                        unsigned long long phases = 1ull << (al_next() % 10), share = 0x100000000ull / phases;
                        unsigned long long pos = (al_next() % 20) << 32 | share * (al_next() % phases);
                        unsigned long long step = share * (al_next() % (3 * phases + 1));

                        al_rows_case(taps_list[ti], phases, al_next() % 70, pos, step, round & 1, bits, 1, 0, 0, 0, faults);
                }
        //      A ratio handed over as the caller does: the first phase's fraction and the step rounded up.
        for (unsigned long long ti = 0; ti < 8; ti++)
                for (unsigned long long ri = 0; ri < sizeof(ratios) / sizeof(ratios[0]); ri++)
                        for (int round = 0; round < 3; round++)
                        {
                                unsigned long long p = ratios[ri][0], q = ratios[ri][1], k = round == 0 ? 0 : round == 1 ? q - 1 : al_next() % q;
                                unsigned long long count = round == 2 ? 1 + al_next() % 70 : (al_next() & 1) ? 512 : 100 + al_next() % 100;
                                unsigned long long pos = (al_next() % 12) << 32 | ((k << 32) + q - 1) / q;
                                unsigned long long step = ((p << 32) + q - 1) / q;

                                if (q > 512)
                                        continue;
                                al_rows_case(taps_list[ti], q, count, pos, step, (int)(ri + round) & 1, bits, 0, p, q, k, faults);
                        }
}

/*
        pcm_widen: the nine kinds, every count to 330 and long ones, the source put against the end of its region (a page that
        faults after the last byte read) or at the start of one with a page that faults before it, at four byte offsets, the
        words against the C's, the words before and after the destination untouched, and a kind past eight left alone.
*/
static const unsigned al_widen_width[7] = {1, 2, 4, 3, 3, 4, 4};

static void al_widen_case(unsigned kind, unsigned long long samples, int front, unsigned phase, int sweep, al_faults *faults)
{
        unsigned long long bytes = samples * al_widen_width[kind < 7 ? kind : 0];
        p8 *src = front ? (p8 *)al_low[0] + phase : (p8 *)al_high[0] - bytes;
        unsigned *dst = front ? (unsigned *)al_low[3] + 16 : (unsigned *)al_high[3] - samples;
        unsigned long long sum = 0;

        faults->cases++;
        for (unsigned long long i = 0; i < bytes; i++)
                src[i] = sweep ? (p8)(i + phase) : (p8)al_next();
        for (unsigned long long i = 0; i < bytes; i++)
                sum += (unsigned long long)src[i] * (i | 1);
        for (int i = 0; i < 16; i++)
        {
                dst[i - 16] = 0xa5a50000u + (unsigned)i;
                if (front)
                        dst[samples + i] = 0x5a5a0000u + (unsigned)i;
        }
        for (unsigned long long i = 0; i < samples; i++)
                dst[i] = 0xdeadbeefu;
        VECTOR_FIRST_CALL(al_first);
        pcm_widen(dst, src, samples, kind);
        for (unsigned long long i = 0; i < bytes; i++)
                sum -= (unsigned long long)src[i] * (i | 1);
        faults->kept += sum != 0;
        for (int i = 0; i < 16; i++)
        {
                faults->outside += dst[i - 16] != 0xa5a50000u + (unsigned)i;
                if (front)
                        faults->outside += dst[samples + i] != 0x5a5a0000u + (unsigned)i;
        }
        for (unsigned long long i = 0; i < samples; i++)
                faults->bits += kind < 7 ? dst[i] != audio_c_widen_one(kind, src + i * al_widen_width[kind]) : dst[i] != 0xdeadbeefu;
}

/*
        pcm_pack24: every count to 330 and long ones, the words against a page that faults after the last (or after a page that
        faults before the first), the bytes written exactly 3 * lanes (the guard page past them, the bytes either side kept).
*/
static void al_pack_case(unsigned long long lanes, int front, al_faults *faults)
{
        unsigned *src = front ? (unsigned *)al_low[0] : (unsigned *)al_high[0] - lanes;
        p8 *dst = front ? (p8 *)al_low[3] + 64 : (p8 *)al_high[3] - 3 * lanes;

        faults->cases++;
        for (unsigned long long i = 0; i < lanes; i++)
                src[i] = (unsigned)al_next();
        for (int i = 0; i < 16; i++)
        {
                dst[i - 16] = (p8)(0xa5 + i);
                if (front)
                        dst[3 * lanes + i] = (p8)(0x5a + i);
        }
        for (unsigned long long i = 0; i < 3 * lanes; i++)
                dst[i] = 0xee;
        VECTOR_FIRST_CALL(al_first);
        pcm_pack24(dst, src, lanes);
        for (unsigned long long i = 0; i < lanes; i++)
        {
                faults->bits += dst[3 * i] != (p8)src[i];
                faults->bits += dst[3 * i + 1] != (p8)(src[i] >> 8);
                faults->bits += dst[3 * i + 2] != (p8)(src[i] >> 16);
        }
        for (int i = 0; i < 16; i++)
        {
                faults->outside += dst[i - 16] != (p8)(0xa5 + i);
                if (front)
                        faults->outside += dst[3 * lanes + i] != (p8)(0x5a + i);
        }
}

static void al_pack_tier(void)
{
        static const unsigned long long longs[] = {331, 344, 511, 512, 1000, 4095, 4096};
        al_faults faults = {0, 0, 0, 0, 0, 0, 0};

        for (unsigned long long lanes = 0; lanes <= 330; lanes++)
                for (int front = 0; front < 2; front++)
                        al_pack_case(lanes, front, &faults);
        for (unsigned long i = 0; i < sizeof(longs) / sizeof(longs[0]); i++)
                for (int front = 0; front < 2; front++)
                        al_pack_case(longs[i], front, &faults);
        check("pcm_pack24: every count to 330 and long ones, the bytes the C's", faults.bits == 0);
        check("pcm_pack24: nothing written past the three bytes of the last lane (the guard page), the bytes either side untouched", faults.outside == 0);
        check("pcm_pack24: cases ran", faults.cases > 600);
}

/*
        f32_planes_from_law: both laws, one to ten channels, every frame count to 330 and long ones, every byte value, the bytes put against
        the end of their region (a page that faults after the last) or at the start of one, the planes one after another against the end
        of theirs, the floats the C's and nothing written before the first plane or past the last.
*/
static void al_law_case(unsigned long long frames, unsigned long long channels, unsigned long long law, int front, int sweep, al_faults *faults)
{
        unsigned long long bytes = frames * channels;
        p8 *src = front ? (p8 *)al_low[0] + (frames & 3) : (p8 *)al_high[0] - bytes;
        float *base = front ? (float *)al_low[3] + 64 : (float *)al_high[3] - bytes, *planes[10];

        faults->cases++;
        for (unsigned long long i = 0; i < bytes; i++)
                src[i] = sweep ? (p8)(i + channels) : (p8)al_next();
        for (unsigned long long c = 0; c < channels; c++)
                planes[c] = base + c * frames;
        for (int i = 0; i < 16; i++)
        {
                base[i - 16] = (float)(0x1234 + i);
                if (front)
                        base[bytes + i] = (float)(0x4321 + i);
        }
        for (unsigned long long i = 0; i < bytes; i++)
                base[i] = 777.0f;
        VECTOR_FIRST_CALL(al_first);
        f32_planes_from_law(planes, src, frames, channels, law);
        for (unsigned long long c = 0; c < channels; c++)
                for (unsigned long long i = 0; i < frames; i++)
                        faults->bits += al_bits(planes[c][i]) != al_bits(audio_c_law_one(src[i * channels + c], law));
        for (int i = 0; i < 16; i++)
        {
                faults->outside += base[i - 16] != (float)(0x1234 + i);
                if (front)
                        faults->outside += base[bytes + i] != (float)(0x4321 + i);
        }
}

static void al_law_tier(void)
{
        static const unsigned long long longs[] = {331, 512, 1000};
        al_faults faults = {0, 0, 0, 0, 0, 0, 0};

        for (unsigned long long law = 0; law < 2; law++)
                for (unsigned long long channels = 1; channels <= 10; channels++)
                {
                        for (unsigned long long frames = 0; frames <= 90; frames++)
                                for (int front = 0; front < 2; front++)
                                        al_law_case(frames, channels, law, front, (int)(frames & 1), &faults);
                        for (unsigned long i = 0; i < sizeof(longs) / sizeof(longs[0]); i++)
                                for (int front = 0; front < 2; front++)
                                        al_law_case(longs[i], channels, law, front, (int)(i & 1), &faults);
                        al_law_case(256, channels, law, 0, 1, &faults);
                }
        check("f32_planes_from_law: both laws, one to ten channels, every byte value, the floats the C's", faults.bits == 0);
        check("f32_planes_from_law: nothing written outside the planes (the guard pages and the floats either side)", faults.outside == 0);
        check("f32_planes_from_law: cases ran", faults.cases > 3000);
}

/*
        memory_zero (bzero) and memory_fill at every size to 600 and the sizes of the rungs, the bytes put against a page that faults after
        the last or after one before the first, at every offset of a vector, the bytes before and after untouched.
*/
static void al_zero_case(unsigned long long n, int front, unsigned phase, int zero, al_faults *faults)
{
        p8 *d = front ? (p8 *)al_low[0] + 64 + (phase & 63) : (p8 *)al_high[0] - n;
        p8 value = zero ? 0 : (p8)(1 + (n % 250));

        faults->cases++;
        for (int i = 1; i <= 16; i++)
        {
                d[-i] = (p8)(0xa5 + i);
                if (front)
                        d[n + i - 1] = (p8)(0x5a + i);
        }
        for (unsigned long long i = 0; i < n; i++)
                d[i] = 0xaa;
        VECTOR_FIRST_CALL(al_first);
        if (zero)
                memory_zero(d, n);
        else
                memory_fill(d, value, n);
        for (unsigned long long i = 0; i < n; i++)
                faults->bits += d[i] != value;
        for (int i = 1; i <= 16; i++)
        {
                faults->outside += d[-i] != (p8)(0xa5 + i);
                if (front)
                        faults->outside += d[n + i - 1] != (p8)(0x5a + i);
        }
}

static void al_zero_tier(void)
{
        al_faults faults = {0, 0, 0, 0, 0, 0, 0};

        for (unsigned long long n = 0; n <= 600; n++)
                for (int zero = 0; zero < 2; zero++)
                        al_zero_case(n, (int)(n & 1), (unsigned)(n * 7), zero, &faults);
        for (unsigned long long n = 1; n <= 520; n += 3)
                for (unsigned phase = 0; phase < 64; phase += 9)
                        al_zero_case(n, 1, phase, 1, &faults);
        for (unsigned long long n = 4090; n <= 4100; n++)
                al_zero_case(n, (int)(n & 1), (unsigned)n, 1, &faults);
        check("memory_zero and memory_fill: every size to 600, at every offset of a vector, the bytes the value's", faults.bits == 0);
        check("memory_zero and memory_fill: nothing written outside the span (the guard page, the bytes either side)", faults.outside == 0);
        check("memory_zero and memory_fill: cases ran", faults.cases > 1500);
}

/*
        The five loops of the protocol engine that are the library's: memory_s16_to_u8, memory_reverse_units, memory_key_printable,
        memory_text_clean and memory_window_peaks, each against the C of SHARED_audio_loops, at the counts and offsets of a vector body's edges
        (0, 1, 2, 3, 7, 8, 15, 16, 17, 31, 32, 33, 63, 64, 65 and more), with the bytes put against a page that faults, before and after,
        in place where the contract allows, and the bytes either side kept.
*/
static const unsigned long long al_pulse_counts[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 16, 17, 23, 24, 25, 31, 32, 33, 47, 48, 49, 63, 64, 65,
                                                       95, 96, 97, 127, 128, 129, 255, 256, 257, 1000, 4096};
#define AL_PULSE_COUNTS (sizeof(al_pulse_counts) / sizeof(al_pulse_counts[0]))

static void al_s16_u8_case(unsigned long long count, int front, int inplace, unsigned phase, al_faults *faults)
{
        p8 *src = front ? (p8 *)al_low[0] + (phase & 15) : (p8 *)al_high[0] - 2 * count;
        p8 *dst = inplace ? src : front ? (p8 *)al_low[3] + (phase & 15) + 64 : (p8 *)al_high[3] - count;
        static p8 keep[8192], want[4096];
        static const short edges[] = {-32768, -32767, -32641, -32640, -32639, -129, -128, -127, -1, 0, 1, 127, 128, 129, 32638, 32639, 32640, 32641, 32767};

        faults->cases++;
        for (unsigned long long i = 0; i < count; i++)
        {
                short v = (i % 7 == 0) ? edges[al_next() % (sizeof(edges) / sizeof(edges[0]))] : (short)al_next();

                src[2 * i] = (p8)v;
                src[2 * i + 1] = (p8)(v >> 8);
        }
        for (unsigned long long i = 0; i < 2 * count; i++)
                keep[i] = src[i];
        if (!inplace)
                for (int i = 0; i < 16; i++)
                {
                        dst[i - 16] = (p8)(0xa5 + i);
                        if (front)
                                dst[count + i] = (p8)(0x5a + i);
                }
        audio_c_s16_to_u8(want, keep, (long)count);
        VECTOR_FIRST_CALL(al_first);
        memory_s16_to_u8(dst, src, count);
        for (unsigned long long i = 0; i < count; i++)
                faults->bits += dst[i] != want[i];
        if (inplace)
                for (unsigned long long i = count; i < 2 * count; i++)
                        faults->kept += src[i] != keep[i];
        else
        {
                for (unsigned long long i = 0; i < 2 * count; i++)
                        faults->kept += src[i] != keep[i];
                for (int i = 0; i < 16; i++)
                {
                        faults->outside += dst[i - 16] != (p8)(0xa5 + i);
                        if (front)
                                faults->outside += dst[count + i] != (p8)(0x5a + i);
                }
        }
}

static void al_reverse_case(unsigned long long count, unsigned long long width, int front, unsigned phase, al_faults *faults)
{
        p8 *buffer = front ? (p8 *)al_low[0] + (phase & 15) : (p8 *)al_high[0] - count * width;
        static p8 want[4096 * 6];
        unsigned long long bytes = count * width;

        faults->cases++;
        if (bytes > sizeof(want))
                return;
        for (unsigned long long i = 0; i < bytes; i++)
                buffer[i] = (p8)al_next();
        for (unsigned long long i = 0; i < bytes; i++)
                want[i] = buffer[i];
        if (front)
                for (int i = 0; i < 16; i++)
                        buffer[bytes + i] = (p8)(0x5a + i);
        audio_c_reverse_units(want, (long)count, (long)width);
        VECTOR_FIRST_CALL(al_first);
        memory_reverse_units(buffer, count, width);
        for (unsigned long long i = 0; i < bytes; i++)
                faults->bits += buffer[i] != want[i];
        if (front)
                for (int i = 0; i < 16; i++)
                        faults->outside += buffer[bytes + i] != (p8)(0x5a + i);
}

static void al_key_case(unsigned long long length, int front, unsigned phase, int bad, al_faults *faults)
{
        p8 *key = front ? (p8 *)al_low[0] + (phase & 15) : (p8 *)al_high[0] - length;
        static const p8 odd[] = {0x00, 0x1f, 0x20, 0x21, 0x7e, 0x7f, 0x80, 0xff, 0xa0, 0x41};

        faults->cases++;
        for (unsigned long long i = 0; i < length; i++)
                key[i] = (p8)(0x21 + al_next() % 0x5e);
        if (bad && length)
                key[al_next() % length] = odd[al_next() % sizeof(odd)];
        faults->bits += (memory_key_printable(key, length) != 0) != (audio_c_key_printable(key, (long)length) != 0);
}

//      Names: printable runs, NULs, every form of UTF-8 sequence, valid, truncated and bad, in a random mixture.
static unsigned long long al_text_make(p8 *to, unsigned long long length)
{
        static const unsigned char samples[][4] = {
            {0xc2, 0xa0}, {0xc2, 0x80}, {0xc2, 0x9f}, {0xc3, 0xa9}, {0xdf, 0xbf}, {0xc0, 0x80}, {0xc1, 0xbf}, {0xe0, 0xa0, 0x80}, {0xe0, 0x9f, 0xbf},
            {0xe0, 0x80, 0x80}, {0xe2, 0x82, 0xac}, {0xed, 0x9f, 0xbf}, {0xed, 0xa0, 0x80}, {0xed, 0xbf, 0xbf}, {0xef, 0xbf, 0xbd}, {0xf0, 0x90, 0x80, 0x80},
            {0xf0, 0x8f, 0xbf, 0xbf}, {0xf0, 0x80, 0x80, 0x80}, {0xf1, 0x80, 0x80, 0x80}, {0xf4, 0x8f, 0xbf, 0xbf}, {0xf4, 0x90, 0x80, 0x80}, {0xf4, 0xbf, 0xbf, 0xbf},
            {0xf5, 0x80, 0x80, 0x80}, {0xff}, {0x80}, {0xbf}, {0x7f}, {0x1f}, {0x00}, {0x20}, {0x7e}};
        unsigned long long n = 0;

        while (n < length)
        {
                unsigned long long pick = al_next() % 8;

                if (pick < 4)
                {
                        to[n++] = (p8)(0x20 + al_next() % 0x5f);
                }
                else if (pick < 7)
                {
                        const unsigned char *sample = samples[al_next() % (sizeof(samples) / sizeof(samples[0]))];
                        unsigned long long k = 0;

                        for (; k < 4 && sample[k] && n < length; k++)
                                to[n++] = sample[k];
                        if (!sample[0] && n < length)
                                to[n++] = 0;
                }
                else
                        to[n++] = (p8)al_next();
                if (pick == 7 && (al_next() & 7) == 0)
                        break;
        }
        return n;
}

static void al_text_case(unsigned long long length, int front, unsigned phase, int inplace, int clean, al_faults *faults)
{
        p8 *from = front ? (p8 *)al_low[0] + (phase & 15) : (p8 *)al_high[0] - length;
        p8 *to = inplace ? from : front ? (p8 *)al_low[3] + 64 + (phase & 7) : (p8 *)al_high[3] - length;
        static p8 want[4200], keep[4200];
        unsigned long long made, expect;

        faults->cases++;
        if (length > 4096)
                return;
        if (clean)
                for (unsigned long long i = 0; i < length; i++)
                        from[i] = (p8)(0x20 + al_next() % 0x5f);
        else
                al_text_make(from, length);
        if (length && !clean && (al_next() & 3) == 0)
        {
                //      A sequence cut by the end of the count: its lead as the last byte read.
                static const p8 leads[] = {0xc2, 0xe0, 0xe2, 0xed, 0xf0, 0xf4, 0xf1, 0xc3};

                from[length - 1] = leads[al_next() % sizeof(leads)];
        }
        for (unsigned long long i = 0; i < length; i++)
                keep[i] = from[i];
        expect = (unsigned long long)audio_c_text_clean(want, keep, (long)length);
        if (!inplace)
                for (int i = 0; i < 16; i++)
                {
                        to[i - 16] = (p8)(0xa5 + i);
                        if (front)
                                to[length + i] = (p8)(0x5a + i);
                }
        VECTOR_FIRST_CALL(al_first);
        made = memory_text_clean(to, from, length);
        faults->exact += made != expect;
        for (unsigned long long i = 0; i < expect && i < made; i++)
                faults->bits += to[i] != want[i];
        if (!inplace)
        {
                for (unsigned long long i = 0; i < length; i++)
                        faults->kept += from[i] != keep[i];
                for (int i = 0; i < 16; i++)
                {
                        faults->outside += to[i - 16] != (p8)(0xa5 + i);
                        if (front)
                                faults->outside += to[length + i] != (p8)(0x5a + i);
                }
        }
}

static float al_peak_value(void)
{
        unsigned long long pick = al_next() % 40;
        unsigned bits;
        float value;

        if (pick == 0)
                bits = 0x7fc00000u | (unsigned)(al_next() & 0x3fffff);
        else if (pick == 1)
                bits = 0xffc00000u;
        else if (pick == 2)
                bits = 0x7f800000u;
        else if (pick == 3)
                bits = 0xff800000u;
        else if (pick == 4)
                bits = 0x80000000u;
        else if (pick == 5)
                bits = 0;
        else if (pick == 6)
                bits = 1u + (unsigned)(al_next() & 0x7fffff);
        else
        {
                value = al_unit() * (float)(1u << (al_next() % 6));
                return value;
        }
        __builtin_memcpy(&value, &bits, 4);
        return value;
}

static void al_peaks_case(unsigned long long frames, unsigned long long window, unsigned long long count_in, int mono, int front, al_faults *faults)
{
        float *left = front ? (float *)al_low[0] + 3 : (float *)al_high[0] - frames;
        float *right = front ? (float *)al_low[1] + 5 : (float *)al_high[1] - frames;
        float *out0 = front ? (float *)al_low[3] + 64 : (float *)al_high[3] - frames;
        float *out1 = front ? (float *)al_low[4] + 64 : (float *)al_high[4] - frames;
        static float want0[4200], want1[4200];
        float state[2], want_state[2];
        unsigned long long count = count_in, made, expect;
        long want_count = (long)count_in;

        faults->cases++;
        if (frames > 4096)
                return;
        for (unsigned long long i = 0; i < frames; i++)
        {
                left[i] = al_peak_value();
                right[i] = al_peak_value();
        }
        state[0] = want_state[0] = (al_next() & 1) ? 0.0f : al_abs(al_unit());
        state[1] = want_state[1] = (al_next() & 1) ? 0.0f : al_abs(al_unit());
        for (int i = 0; i < 16; i++)
        {
                out0[i - 16] = (float)(0x1234 + i);
                out1[i - 16] = (float)(0x4321 + i);
                if (front)
                {
                        out0[frames + i] = (float)(0x2345 + i);
                        out1[frames + i] = (float)(0x5432 + i);
                }
        }
        expect = (unsigned long long)audio_c_window_peaks(want0, want1, left, right, (long)frames, want_state, &want_count, (long)window, mono);
        VECTOR_FIRST_CALL(al_first);
        made = memory_window_peaks(out0, out1, left, right, frames, state, &count, window, mono);
        faults->exact += made != expect || (long)count != want_count;
        faults->weight += al_bits(state[0]) != al_bits(want_state[0]) || al_bits(state[1]) != al_bits(want_state[1]);
        for (unsigned long long i = 0; i < expect && i < made; i++)
                faults->bits += al_bits(out0[i]) != al_bits(want0[i]) || al_bits(out1[i]) != al_bits(want1[i]);
        for (int i = 0; i < 16; i++)
        {
                faults->outside += al_bits(out0[i - 16]) != al_bits((float)(0x1234 + i)) || al_bits(out1[i - 16]) != al_bits((float)(0x4321 + i));
                if (front)
                        faults->outside += al_bits(out0[frames + i]) != al_bits((float)(0x2345 + i)) || al_bits(out1[frames + i]) != al_bits((float)(0x5432 + i));
        }
}

static void al_pulse_tier(void)
{
        al_faults u8 = {0, 0, 0, 0, 0, 0, 0}, rev = {0, 0, 0, 0, 0, 0, 0}, key = {0, 0, 0, 0, 0, 0, 0}, text = {0, 0, 0, 0, 0, 0, 0}, peak = {0, 0, 0, 0, 0, 0, 0};
        static const unsigned long long windows[] = {1, 2, 3, 7, 8, 9, 25, 100, 1000};

        for (unsigned long i = 0; i < AL_PULSE_COUNTS; i++)
                for (int front = 0; front < 2; front++)
                        for (int inplace = 0; inplace < 2; inplace++)
                                for (unsigned phase = 0; phase < 16; phase += 5)
                                        al_s16_u8_case(al_pulse_counts[i], front, inplace, phase, &u8);
        for (unsigned long long count = 0; count <= 130; count++)
                al_s16_u8_case(count, (int)(count & 1), (int)((count >> 1) & 1), (unsigned)count, &u8);
        check("memory_s16_to_u8: every count, in place and apart, the C's bytes (the edges of the rounding and of the clip)", u8.bits == 0);
        check("memory_s16_to_u8: the source kept, nothing written past count (the guard page) or before dst", u8.outside == 0 && u8.kept == 0);

        for (unsigned long long width = 2; width <= 6; width++)
        {
                for (unsigned long i = 0; i < AL_PULSE_COUNTS; i++)
                        for (int front = 0; front < 2; front++)
                                for (unsigned phase = 0; phase < 16; phase += 3)
                                        al_reverse_case(al_pulse_counts[i], width, front, phase, &rev);
                for (unsigned long long count = 0; count <= 70; count++)
                        al_reverse_case(count, width, (int)(count & 1), (unsigned)count, &rev);
        }
        check("memory_reverse_units: widths 2 to 6, every count, at every offset of a vector, the C's bytes", rev.bits == 0);
        check("memory_reverse_units: nothing written past the last unit (the guard page)", rev.outside == 0);

        for (unsigned long long length = 0; length <= 70; length++)
                for (int front = 0; front < 2; front++)
                        for (int bad = 0; bad < 3; bad++)
                                for (unsigned phase = 0; phase < 16; phase += 5)
                                        al_key_case(length, front, phase, bad != 0, &key);
        check("memory_key_printable: every length to 70, a bad byte anywhere, the C's answer", key.bits == 0);

        for (unsigned long i = 0; i < AL_PULSE_COUNTS; i++)
                for (int front = 0; front < 2; front++)
                        for (int inplace = 0; inplace < 2; inplace++)
                                for (int round = 0; round < 6; round++)
                                        al_text_case(al_pulse_counts[i], front, (unsigned)(round * 3), inplace, round == 5, &text);
        for (unsigned long long length = 0; length <= 120; length++)
                for (int round = 0; round < 24; round++)
                        al_text_case(length, (int)(round & 1), (unsigned)round, (int)((round >> 1) & 1), round % 7 == 6, &text);
        check("memory_text_clean: the count and the bytes the C's (NULs, UTF-8 forms valid, truncated and overlong, in place or apart)", text.exact == 0 && text.bits == 0);
        check("memory_text_clean: nothing read or written past the count (the guard page), the source kept apart", text.outside == 0 && text.kept == 0);

        for (unsigned long i = 0; i < AL_PULSE_COUNTS; i++)
                for (unsigned long w = 0; w < sizeof(windows) / sizeof(windows[0]); w++)
                        for (int mono = 0; mono < 2; mono++)
                                for (int front = 0; front < 2; front++)
                                        al_peaks_case(al_pulse_counts[i], windows[w], al_next() % (windows[w] + 2), mono, front, &peak);
        for (unsigned long long frames = 0; frames <= 70; frames++)
                for (int round = 0; round < 6; round++)
                        al_peaks_case(frames, 1 + (unsigned long long)round * 3, (unsigned long long)round, round & 1, (int)(frames & 1), &peak);
        check("memory_window_peaks: the frames written, the count and the state the C's, every window", peak.exact == 0 && peak.weight == 0);
        check("memory_window_peaks: the peaks the C's to the bit (a NaN never a peak, minus zero not one)", peak.bits == 0);
        check("memory_window_peaks: nothing written past what it answers (the guard page, the floats either side)", peak.outside == 0);
        check("memory_window_peaks / the five loops: cases ran", u8.cases > 600 && rev.cases > 1000 && key.cases > 800 && text.cases > 1500 && peak.cases > 800);
}

static void al_widen_tier(void)
{
        static const unsigned long long longs[] = {331, 344, 511, 512, 1000, 4095, 4096};
        al_faults faults = {0, 0, 0, 0, 0, 0, 0};

        for (unsigned kind = 0; kind < 8; kind++)
        {
                for (unsigned long long samples = 0; samples <= 330; samples++)
                        for (int front = 0; front < 2; front++)
                                al_widen_case(kind, samples, front, (unsigned)(samples & 3), (int)(samples & 1), &faults);
                for (unsigned long i = 0; i < sizeof(longs) / sizeof(longs[0]); i++)
                        for (int front = 0; front < 2; front++)
                                al_widen_case(kind, longs[i], front, (unsigned)(i & 3), (int)(i & 1), &faults);
        }
        //      Every byte value, in every position of an eight.
        for (unsigned kind = 0; kind < 7; kind++)
                for (unsigned phase = 0; phase < 8; phase++)
                        al_widen_case(kind, 256 + phase, 0, phase, 1, &faults);
        check("pcm_widen: every kind, every count to 330 and long ones, the words the C's", faults.bits == 0);
        check("pcm_widen: nothing read past the source or written past the words (the guard pages), the words either side untouched", faults.outside == 0);
        check("pcm_widen: the source unchanged", faults.kept == 0);
        check("pcm_widen: cases ran", faults.cases > 5000);
}

/*
        The mixing core with the library's loops under it: a stream of two tones at rin through a voice and a sink
        at rout, the output held to the tones at the output's own times (a filter that is centred makes an output
        frame the signal at its time). The first frames, the fade-in of the voice and the filter's lead, are left out.
*/
static struct audio_sink al_sink;
static struct audio_voice al_voice;
static unsigned char al_ring[1 << 18], al_bytes[256 * 4];
static short al_pcm[2 * 12288];
static float al_heard[2][4096];

static double al_engine(long rin, long rout)
{
        const double tau = 6.283185307179586, f0 = 1000.0, f1 = 3300.0;
        long input = rin * 3584 / rout + 512, blocks = 14;
        double error = 0;
        long counted = 0;

        for (long n = 0; n < input; n++)
        {
                al_pcm[2 * n] = (short)((double)16383 * audio_sin(tau * f0 * (double)n / (double)rin) + 16383.5) - 16383;
                al_pcm[2 * n + 1] = (short)((double)8191 * audio_sin(tau * f1 * (double)n / (double)rin) + 8191.5) - 8191;
        }
        audio_sink_open(&al_sink, rout, 2);
        if (audio_voice_open(&al_voice, AUDIO_FORMAT_S16LE, 2, rin, 0, rout, al_ring, sizeof(al_ring)) < 0)
                return 1e9;
        audio_ring_put(&al_voice.ring, (const unsigned char *)al_pcm, (unsigned long)input * 4);
        for (long b = 0; b < blocks; b++)
        {
                audio_sink_begin(&al_sink, 256);
                audio_voice_mix(&al_sink, &al_voice, 256, 0);
                audio_sink_end(&al_sink, al_bytes, AUDIO_FORMAT_S16LE, 2, 256);
                for (long i = 0; i < 256; i++)
                        al_heard[0][b * 256 + i] = al_sink.mix[0][i], al_heard[1][b * 256 + i] = al_sink.mix[1][i];
        }
        for (long g = 160; g < blocks * 256; g++)
        {
                double e0 = (double)al_heard[0][g] - (16383.0 / 32768.0) * audio_sin(tau * f0 * (double)g / (double)rout);
                double e1 = (double)al_heard[1][g] - (8191.0 / 32768.0) * audio_sin(tau * f1 * (double)g / (double)rout);

                error += e0 * e0 + e1 * e1;
                counted += 2;
        }
        audio_voice_close(&al_voice);
        return audio_sqrt(error / (double)counted);
}

/*
        f32_scale_peak and f32_scale_add_peak against audio_c_scale, to the bit: the output of every element, and the peak
        (a NaN equal to a NaN, the machine's payload being its own). Every count to 520 and a few long ones, the two arrays
        against a page that faults or just after one, either at any of seventeen float offsets, in place, with the
        values that break a vector's maximum or a sum (zeros of both signs, infinities, NaNs, denormals, the largest
        float) in the data and as the gain; then the cases that are exact whatever the order: gains of a power of two.
*/
typedef struct
{
        unsigned long long cases, values, peaks, outside, kept;
} al_scale_faults;

static p8 al_same(float got, float want)
{
        return want != want ? got != got : al_bits(got) == al_bits(want);
}

static float al_special(p8 specials)
{
        static const unsigned pattern[] = {0x00000000, 0x80000000, 0x7f800000, 0xff800000, 0x7fc00000, 0xffc12345, 0x00000001, 0x807fffff,
                                           0x7f7fffff, 0x3f800000, 0xbf800000, 0x00800000};
        unsigned long long r = al_next();

        if (specials && (r & 7) == 0)
        {
                unsigned bits = pattern[(r >> 8) % 12];
                float value;

                __builtin_memcpy(&value, &bits, 4);
                return value;
        }
        return al_unit() * (float)(1u << (r >> 20 & 7));
}

static void al_scale_case(unsigned long long count, int add, int front, int inplace, unsigned dst_off, unsigned x_off, p8 specials, float gain,
                          al_scale_faults *faults)
{
        static float want[8192], from[8192];
        float *dst, *x;
        float peak, peak_want;

        if (count + 40 > 8192)
                return;
        faults->cases++;
        dst = front ? (float *)al_low[0] + 16 + dst_off : (float *)al_high[0] - count;
        x = inplace ? dst : front ? (float *)al_low[1] + 16 + x_off : (float *)al_high[1] - count;
        for (int i = 0; i < 16; i++)
        {
                dst[i - 16] = (float)0x1234 + (float)i;
                if (front)
                        dst[count + i] = (float)0x4321 + (float)i;
        }
        for (unsigned long long i = 0; i < count; i++)
        {
                dst[i] = al_special(specials);
                want[i] = dst[i];
                if (!inplace)
                {
                        x[i] = al_special(specials);
                        from[i] = x[i];
                }
        }
        peak_want = audio_c_scale(want, inplace ? want : from, count, gain, add);
        peak = add ? f32_scale_add_peak(dst, x, count, gain) : f32_scale_peak(dst, x, count, gain);
        faults->peaks += !al_same(peak, peak_want);
        for (unsigned long long i = 0; i < count; i++)
        {
                faults->values += !al_same(dst[i], want[i]);
                if (!inplace)
                        faults->kept += al_bits(x[i]) != al_bits(from[i]);
        }
        for (int i = 0; i < 16; i++)
        {
                faults->outside += al_bits(dst[i - 16]) != al_bits((float)0x1234 + (float)i);
                if (front)
                        faults->outside += al_bits(dst[count + i]) != al_bits((float)0x4321 + (float)i);
        }
}

/*
        A NaN that comes after the greatest value in the same lane of a vector, at every distance a body's turns make (4, 8, 16, 32,
        64 floats on), must leave the peak as it was: the maximum drops a NaN lane's samples and not its past.
*/
static void al_scale_planted(unsigned long long count, unsigned long long first, unsigned long long second, int add, int front, al_scale_faults *faults)
{
        static float want[1100], from[1100];
        float *dst = front ? (float *)al_low[0] + 16 : (float *)al_high[0] - count, *x = front ? (float *)al_low[1] + 16 : (float *)al_high[1] - count;
        float peak, peak_want;
        unsigned nan = 0x7fc00000;

        faults->cases++;
        for (unsigned long long i = 0; i < count; i++)
        {
                x[i] = (i & 1) ? 0.0f : -0.0f;
                dst[i] = 1.0f;
        }
        x[first] = 1.5f;
        __builtin_memcpy(&x[second], &nan, 4);
        for (unsigned long long i = 0; i < count; i++)
        {
                from[i] = x[i];
                want[i] = dst[i];
        }
        peak_want = audio_c_scale(want, from, count, 1.0f, add);
        peak = add ? f32_scale_add_peak(dst, x, count, 1.0f) : f32_scale_peak(dst, x, count, 1.0f);
        faults->peaks += !al_same(peak, peak_want) || peak != 1.5f;
        for (unsigned long long i = 0; i < count; i++)
                faults->values += !al_same(dst[i], want[i]);
}

static void al_scale_walk(al_scale_faults *faults)
{
        static const unsigned long long distances[] = {1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127, 128, 129};
        static const unsigned long long longer[] = {600, 777, 1000, 1023, 1024, 1025, 2047, 4095, 4096, 4097};

        for (int add = 0; add < 2; add++)
                for (unsigned long long at = 0; at < 521 + sizeof(longer) / sizeof(longer[0]); at++)
                {
                        unsigned long long count = at <= 520 ? at : longer[at - 521];

                        for (int place = 0; place < 4; place++)
                        {
                                p8 specials = (at + (unsigned long long)place) % 3 == 0;
                                float gain = (at + (unsigned long long)place) % 5 == 0 ? al_special(true) : place == 3 ? 0.125f : al_unit() * 3.0f;

                                al_scale_case(count, add, place & 1, place == 2, (unsigned)(al_next() % 17), (unsigned)(al_next() % 17), specials, gain, faults);
                        }
                }
        for (int add = 0; add < 2; add++)
                for (unsigned long long count = 5; count < 1100; count += count < 140 ? 1 : 43)
                        for (unsigned long long d = 0; d < sizeof(distances) / sizeof(distances[0]); d++)
                                for (unsigned long long first = 0; first < 6; first++)
                                        if (first + distances[d] < count)
                                                al_scale_planted(count, first, first + distances[d], add, (int)((count + d) & 1), faults);
        //      Powers of two and small integers: every product and sum exact, so any order of sums is right.
        for (int round = 0; round < 40; round++)
                al_scale_case(1 + al_next() % 300, round & 1, (round >> 1) & 1, 0, (unsigned)(al_next() % 17), (unsigned)(al_next() % 17), false, (float)(1 << (round % 6)) / 4.0f,
                              faults);
}

static void al_scale_tier(string_address tier)
{
        al_scale_faults faults = {0, 0, 0, 0, 0};

        string_format(log, "  %s\n", tier);
        al_scale_walk(&faults);
        check("f32_scale_peak, f32_scale_add_peak: every count and placement, each element the C's to the bit", faults.values == 0);
        check("f32_scale_peak, f32_scale_add_peak: the peak the C's, a NaN left out", faults.peaks == 0);
        check("f32_scale_peak, f32_scale_add_peak: sentinels before and after dst untouched, nothing written past count (the guard page)", faults.outside == 0);
        check("f32_scale_peak, f32_scale_add_peak: x unchanged", faults.kept == 0);
        check("f32_scale_peak, f32_scale_add_peak: cases ran", faults.cases > 4000);
}

/*
        f32_planes_from_s16, _s32 and _f32 against audio_c_decode, to the bit: 1 to 8 channels, every frame count to 330
        and a few long ones, the source and every plane against a page that faults or just after one, the source at
        any of four byte offsets (a program's bytes are not aligned), every one of the 65536 sixteen-bit samples, 32-bit
        samples at the edges of their rounding to a float and floats that are NaNs of both signs, infinities, denormals,
        zeros of both signs, 16 and -16 and a step either side of them; planes untouched before and after their frames.
*/
typedef struct
{
        unsigned long long cases, values, outside;
} al_decode_faults;

static unsigned al_decode_special(void)
{
        static const unsigned pattern[] = {0x00000000, 0x80000000, 0x7f800000, 0xff800000, 0x7fc00000, 0xffc12345, 0x7f800001, 0x00000001,
                                           0x807fffff, 0x7f7fffff, 0x3f800000, 0xbf800000, 0x41800000, 0x41800001, 0x4180ffff, 0xc1800000,
                                           0xc1800001, 0x417fffff, 0xc17fffff, 0x7fffffff, 0x80800000};
        return pattern[al_next() % 21];
}

static void al_decode_case(int format, unsigned long long frames, unsigned long long channels, int front, unsigned offset, int kind, al_decode_faults *faults)
{
        static float want[8][1600];
        unsigned long long bytes = format == 0 ? 2 : 4, total = frames * channels * bytes;
        unsigned char *src = front ? al_low[0] + offset : al_high[0] - total;
        float *planes[8], *wants[8], *base[8];

        if (frames > 1600 || channels > 8 || total + 8 > AL_REGION)
                return;
        faults->cases++;
        //      The planes in regions 1 to 4, a plane to a stretch of 2000 floats of a region: the first four at the start of the
        //      page that faults after their region's last (or just after the one before it), the rest 2000 floats on.
        for (unsigned long long c = 0; c < channels; c++)
        {
                base[c] = front ? (float *)al_low[1 + c % 4] + 16 + 2000 * (c / 4) : (float *)al_high[1 + c % 4] - frames - 2000 * (c / 4);
                planes[c] = base[c];
                wants[c] = want[c];
                for (int i = 1; i <= 8; i++)
                        base[c][-i] = (float)0x555 + (float)i;
                if (front)
                        for (int i = 0; i < 8; i++)
                                base[c][frames + i] = (float)0x777 + (float)i;
        }
        for (unsigned long long i = 0; i < total; i += 4)
        {
                unsigned long long r = al_next();
                unsigned word = (unsigned)r;

                if (kind == 1 && (r >> 40) % 3 == 0)
                        word = al_decode_special();
                else if (kind == 2)
                {
                        //      near the rounding edges of a float: 24 bits and a few more
                        word = (unsigned)(r >> 33) << (unsigned)(r >> 59 & 7) | (unsigned)(r >> 20 & 7);
                        if (r >> 63)
                                word = -word;
                }
                else if (format == 2 && kind == 0)
                {
                        float value = al_unit() * (float)(1u << (r >> 59 & 7));

                        __builtin_memcpy(&word, &value, 4);
                }
                for (unsigned long long k = 0; k < 4 && i + k < total; k++)
                        src[i + k] = (unsigned char)(word >> (8 * k));
        }
        audio_c_decode(format, src, frames, channels, wants);
        switch (format)
        {
        case 0:
                f32_planes_from_s16(planes, src, frames, channels);
                break;
        case 1:
                f32_planes_from_s32(planes, src, frames, channels);
                break;
        default:
                f32_planes_from_f32(planes, src, frames, channels);
                break;
        }
        for (unsigned long long c = 0; c < channels; c++)
        {
                for (unsigned long long i = 0; i < frames; i++)
                        faults->values += al_bits(planes[c][i]) != al_bits(want[c][i]);
                for (int i = 1; i <= 8; i++)
                        faults->outside += al_bits(base[c][-i]) != al_bits((float)0x555 + (float)i);
                if (front)
                        for (int i = 0; i < 8; i++)
                                faults->outside += al_bits(base[c][frames + i]) != al_bits((float)0x777 + (float)i);
        }
}

static void al_decode_tier(string_address tier)
{
        al_decode_faults faults[3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
        static const unsigned long long longer[] = {400, 511, 512, 513, 1000, 1023, 1024, 1025};

        string_format(log, "  %s\n", tier);
        for (int format = 0; format < 3; format++)
        {
                for (unsigned long long frames = 0; frames < 331 + sizeof(longer) / sizeof(longer[0]); frames++)
                        for (unsigned long long channels = 1; channels <= 8; channels++)
                        {
                                unsigned long long count = frames < 331 ? frames : longer[frames - 331];

                                //      Mono and stereo at every count; the other counts of channels at the counts that end a turn.
                                if (channels > 2 && count % 7 > 1 && count > 40)
                                        continue;
                                al_decode_case(format, count, channels, (int)((count + channels) & 1), (unsigned)(al_next() % 4), (int)((count + channels) % 3), &faults[format]);
                        }
                //      Every sixteen-bit sample, in a stereo pair of each and in a mono run.
                if (format == 0)
                {
                        unsigned char *src = al_high[0] - 65536 * 4;
                        float *left = (float *)al_high[1] - 65536, *right = (float *)al_high[2] - 65536, *planes[2] = {left, right};

                        for (unsigned long long v = 0; v < 65536; v++)
                        {
                                src[4 * v] = (unsigned char)v, src[4 * v + 1] = (unsigned char)(v >> 8);
                                src[4 * v + 2] = (unsigned char)~v, src[4 * v + 3] = (unsigned char)(~v >> 8);
                        }
                        f32_planes_from_s16(planes, src, 65536, 2);
                        for (unsigned long long v = 0; v < 65536; v++)
                        {
                                float a = (float)(short)v * (1.0f / 32768.0f), b = (float)(short)~v * (1.0f / 32768.0f);

                                faults[0].values += al_bits(left[v]) != al_bits(a) || al_bits(right[v]) != al_bits(b);
                        }
                        f32_planes_from_s16(planes, src, 65536, 1);
                        for (unsigned long long v = 0; v < 65536; v++)
                                faults[0].values += al_bits(left[v]) != al_bits((float)(short)(src[2 * v] | src[2 * v + 1] << 8) * (1.0f / 32768.0f));
                }
        }
        check("f32_planes_from_s16: every frame count and channel count, every plane the C's to the bit, every 16-bit sample", faults[0].values == 0);
        check("f32_planes_from_s32: every frame count and channel count, every plane the C's to the bit", faults[1].values == 0);
        check("f32_planes_from_f32: every frame count and channel count, every plane the C's to the bit (NaNs, infinities, denormals, zeros of both signs, the edges of 16)", faults[2].values == 0);
        check("f32_planes_from_s16, _s32, _f32: nothing written before or past the planes' frames (the guard page), nothing read past the samples", faults[0].outside + faults[1].outside + faults[2].outside == 0);
        check("f32_planes_from_s16, _s32, _f32: cases ran", faults[0].cases > 1000 && faults[1].cases > 1000 && faults[2].cases > 1000);
}

/*
        f32_pair_to_s16, _s32, _s24_32, _f32 and f32_pair_clip against audio_c_encode and audio_c_clip, to the bit (a NaN equal to
        a NaN in the floats): every count to 330 and long ones, the planes at any of four float offsets and the card's bytes at any
        of four, against a page that faults or just after one, with values uniform in range, those that make a bad rounding or
        a bad range (NaNs, infinities, zeros of both signs, denormals, one a step either side of full scale, the ties of the
        rounding), the halves of every 16 and 24-bit step, and a quarter again past full scale; then every one of the 65536
        sixteen-bit samples through decode and encode to the same bytes.
*/
typedef struct
{
        unsigned long long cases, values, outside;
} al_encode_faults;

static float al_encode_value(int kind)
{
        static const unsigned pattern[] = {0x00000000, 0x80000000, 0x7f800000, 0xff800000, 0x7fc00000, 0xffc12345, 0x00000001, 0x807fffff,
                                           0x7f7fffff, 0x3f800000, 0xbf800000, 0x3f800001, 0xbf800001, 0x3f7fffff, 0xbf7fffff, 0x3effff00,
                                           0x3f7fff00, 0x38ffffff, 0x3f000000, 0x3effffff, 0x3f7fffc0, 0x3f7fff80, 0xbf7fff80, 0x3f800040};
        unsigned long long r = al_next();

        if (kind == 1 && (r & 7) == 0)
        {
                unsigned bits = pattern[(r >> 8) % 24];
                float value;

                __builtin_memcpy(&value, &bits, 4);
                return value;
        }
        if (kind == 2)
        {
                //      A step of 16 or 24 bits, or a half of one or a quarter past it
                double step = (double)((long long)(r % 65536) - 32768) / 32768.0;

                switch (r >> 20 & 7)
                {
                case 0:
                        return (float)step;
                case 1:
                        return (float)(step + 1.0 / 65536.0);
                case 2:
                        return (float)(step - 1.0 / 65536.0);
                case 3:
                        return (float)(step + 0.25 / 32768.0);
                case 4:
                        return (float)((double)((long long)(r >> 24 & 0xffffff) - 8388608) / 8388608.0 + (r >> 52 & 1 ? 0.5 / 8388608.0 : 0.0));
                case 5:
                        return (float)((double)((long long)(r >> 24 & 0xffffff) - 8388608) / 8388608.0);
                default:
                        return (float)(step * 1.25);
                }
        }
        return kind == 3 ? al_unit() * 1.25f : al_unit();
}

static void al_encode_case(int format, unsigned long long count, int front, int kind, al_encode_faults *faults)
{
        static unsigned char want[8192 * 2];
        unsigned long long bytes = format == 0 ? 2 : 4, total = count * 2 * bytes;
        float *left = front ? (float *)al_low[0] + 16 + al_next() % 4 : (float *)al_high[0] - count;
        float *right = front ? (float *)al_low[1] + 16 + al_next() % 4 : (float *)al_high[1] - count;
        unsigned char *dst = front ? al_low[2] + 16 + al_next() % 4 : al_high[2] - total;

        if (count > 1200)
                return;
        faults->cases++;
        for (unsigned long long i = 0; i < count; i++)
        {
                left[i] = al_encode_value(kind);
                right[i] = al_encode_value(kind);
        }
        for (int i = 1; i <= 16; i++)
        {
                dst[-i] = (unsigned char)(0x5a + i);
                if (front)
                        dst[total + (unsigned long long)i - 1] = (unsigned char)(0xa5 + i);
        }
        audio_c_encode(format, want, count, left, right);
        switch (format)
        {
        case 0:
                f32_pair_to_s16(dst, left, right, count);
                break;
        case 1:
                f32_pair_to_s32(dst, left, right, count);
                break;
        case 2:
                f32_pair_to_s24_32(dst, left, right, count);
                break;
        default:
                f32_pair_to_f32(dst, left, right, count);
                break;
        }
        for (unsigned long long i = 0; i < total; i += bytes)
        {
                if (format == 3)
                {
                        float got, expect;

                        __builtin_memcpy(&got, dst + i, 4);
                        __builtin_memcpy(&expect, want + i, 4);
                        faults->values += !al_same(got, expect);
                }
                else
                        for (unsigned long long k = 0; k < bytes; k++)
                                faults->values += dst[i + k] != want[i + k];
        }
        for (int i = 1; i <= 16; i++)
        {
                faults->outside += dst[-i] != (unsigned char)(0x5a + i);
                if (front)
                        faults->outside += dst[total + (unsigned long long)i - 1] != (unsigned char)(0xa5 + i);
        }
}

static void al_clip_case(unsigned long long count, int front, int kind, al_encode_faults *faults)
{
        static float want_left[1300], want_right[1300];
        float *left = front ? (float *)al_low[0] + 16 + al_next() % 4 : (float *)al_high[0] - count;
        float *right = front ? (float *)al_low[1] + 16 + al_next() % 4 : (float *)al_high[1] - count;
        unsigned long long clips, want;

        if (count > 1200)
                return;
        faults->cases++;
        for (unsigned long long i = 0; i < count; i++)
        {
                //      mostly inside, with the ones either side of full scale and the odd NaN
                left[i] = want_left[i] = (kind == 1 && al_next() % 5 == 0) ? al_encode_value(1) : al_encode_value(kind == 0 ? 0 : 3) * (kind == 2 ? 0.9f : 1.0f);
                right[i] = want_right[i] = (kind == 1 && al_next() % 5 == 0) ? al_encode_value(1) : al_encode_value(kind == 0 ? 0 : 3) * (kind == 2 ? 0.9f : 1.0f);
        }
        want = audio_c_clip(want_left, want_right, count);
        clips = f32_pair_clip(left, right, count);
        faults->values += clips != want;
        for (unsigned long long i = 0; i < count; i++)
                faults->values += !al_same(left[i], want_left[i]) + !al_same(right[i], want_right[i]);
}

static void al_encode_tier(string_address tier)
{
        al_encode_faults faults[5] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
        static const unsigned long long longer[] = {400, 511, 512, 513, 1000, 1023, 1024, 1025};

        string_format(log, "  %s\n", tier);
        for (unsigned long long at = 0; at < 331 + sizeof(longer) / sizeof(longer[0]); at++)
        {
                unsigned long long count = at < 331 ? at : longer[at - 331];

                for (int place = 0; place < 4; place++)
                {
                        for (int format = 0; format < 4; format++)
                                al_encode_case(format, count, place & 1, (int)((at + (unsigned long long)place + (unsigned long long)format) & 3), &faults[format]);
                        al_clip_case(count, place & 1, (int)((at + (unsigned long long)place) % 3), &faults[4]);
                }
        }
        //      Every sixteen-bit sample out and in again: the bytes a program wrote are the bytes of the card.
        {
                unsigned char *src = al_high[0] - 65536 * 4, *back = al_high[3] - 65536 * 4;
                float *left = (float *)al_high[1] - 65536, *right = (float *)al_high[2] - 65536, *planes[2] = {left, right};

                for (unsigned long long v = 0; v < 65536; v++)
                {
                        src[4 * v] = (unsigned char)v, src[4 * v + 1] = (unsigned char)(v >> 8);
                        src[4 * v + 2] = (unsigned char)~v, src[4 * v + 3] = (unsigned char)(~v >> 8);
                }
                f32_planes_from_s16(planes, src, 65536, 2);
                f32_pair_to_s16(back, left, right, 65536);
                for (unsigned long long i = 0; i < 65536 * 4; i++)
                        faults[0].values += back[i] != src[i];
        }
        check("f32_pair_to_s16: every count and placement the C's bytes, ties to even, held to 16 bits, a NaN zero, and every 16-bit sample back as it was", faults[0].values == 0);
        check("f32_pair_to_s32: every count and placement the C's bytes (2^31 held to 2^31 - 1)", faults[1].values == 0);
        check("f32_pair_to_s24_32: every count and placement the C's bytes", faults[2].values == 0);
        check("f32_pair_to_f32: every count and placement the C's floats, held to [-1, 1], a NaN left", faults[3].values == 0);
        check("f32_pair_clip: every count and placement the C's planes and the C's count of frames", faults[4].values == 0);
        check("f32_pair_to_s16, _s32, _s24_32, _f32: nothing written before or past the frames (the guard page and sixteen bytes either side)", faults[0].outside + faults[1].outside + faults[2].outside + faults[3].outside == 0);
        check("f32_pair_to_s16, _s32, _s24_32, _f32, f32_pair_clip: cases ran", faults[0].cases > 1200 && faults[1].cases > 1200 && faults[2].cases > 1200 && faults[3].cases > 1200 && faults[4].cases > 1200);
}

/*
        f32_rows_mix2 against audio_c_mix2, to the bit (a NaN equal to a NaN): 0 to 8 channels, every count to 330 and long ones,
        rows apart by the count and by up to a hundred floats more, the first row just after a page that faults or the last
        one ending against it, the outputs against it, weights of every sort (zero, negative, one, tiny, the ones that make a
        sum round), inputs with NaNs, infinities, denormals and zeros of both signs.
*/
static void al_mix2_case(unsigned long long count, unsigned long long channels, int front, int kind, al_encode_faults *faults)
{
        static float want0[1300], want1[1300];
        float matrix[16];
        unsigned long long stride = count + (al_next() % 3 == 0 ? al_next() % 100 : 0), rows_floats;
        float *rows, *out0, *out1;

        if (count > 1200 || channels > 8 || stride * channels + 8 > (AL_REGION / 4))
                return;
        faults->cases++;
        rows_floats = stride * (channels ? channels : 1);
        rows = front ? (float *)al_low[0] + 16 : (float *)al_high[0] - rows_floats + (stride - count);
        out0 = front ? (float *)al_low[1] + 16 + al_next() % 4 : (float *)al_high[1] - count;
        out1 = front ? (float *)al_low[2] + 16 + al_next() % 4 : (float *)al_high[2] - count;
        for (int i = 0; i < 16; i++)
                matrix[i] = kind == 0 ? al_unit() : kind == 1 ? ((al_next() & 3) == 0 ? 0.0f : (al_next() & 3) == 1 ? 1.0f : al_special(false) * 0.01f) : al_special(true);
        for (unsigned long long c = 0; c < channels; c++)
                for (unsigned long long i = 0; i < count; i++)
                        rows[c * stride + i] = al_special(kind == 2);
        for (unsigned long long i = 0; i < count; i++)
                out0[i] = out1[i] = 1234.0f;
        audio_c_mix2(want0, want1, rows, stride, matrix, count, channels);
        f32_rows_mix2(out0, out1, rows, stride, matrix, count, channels);
        for (unsigned long long i = 0; i < count; i++)
                faults->values += !al_same(out0[i], want0[i]) + !al_same(out1[i], want1[i]);
}

static void al_mix2_tier(void)
{
        al_encode_faults faults = {0, 0, 0};
        static const unsigned long long longer[] = {400, 511, 512, 513, 1000, 1023, 1024, 1025};

        for (unsigned long long at = 0; at < 331 + sizeof(longer) / sizeof(longer[0]); at++)
                for (unsigned long long channels = 0; channels <= 8; channels++)
                {
                        unsigned long long count = at < 331 ? at : longer[at - 331];

                        if (channels > 2 && count > 40 && count % 5 > 1)
                                continue;
                        al_mix2_case(count, channels, (int)((at + channels) & 1), (int)((at + channels) % 3), &faults);
                }
        check("f32_rows_mix2: every count, channel count and placement the C's bits", faults.values == 0);
        check("f32_rows_mix2: cases ran", faults.cases > 1500);
}

/*
        The calling convention: a routine returns every register the machine's convention says it keeps. al_abi_call is a
        trampoline that puts a known value in each of them, calls the routine al_abi_target names with the arguments it was
        given (up to four on the stack), and counts the ones that came back different in al_abi_bad. x86_64: rbx, rbp and
        r12 to r15; arm64: x19 to x29 and the low halves of v8 to v15 (d8 to d15), which a routine that wants a vector
        register for a constant takes at its peril; riscv64: s0 to s11 and fs0 to fs11.
*/
extern unsigned long long al_abi_bad;
extern void *al_abi_target;
void al_abi_call(void);
__asm__(".comm al_abi_bad, 8, 8\n.comm al_abi_target, 8, 8\n");
#if X64
__asm__(".text\n.globl al_abi_call\nal_abi_call:\n"
        "push %rbx\n push %rbp\n push %r12\n push %r13\n push %r14\n push %r15\n"
        "push 80(%rsp)\n push 80(%rsp)\n push 80(%rsp)\n push 80(%rsp)\n"
        "movabs $0x1111111111111111, %rbx\n movabs $0x2222222222222222, %rbp\n movabs $0x3333333333333333, %r12\n"
        "movabs $0x4444444444444444, %r13\n movabs $0x5555555555555555, %r14\n movabs $0x6666666666666666, %r15\n"
        "call *al_abi_target(%rip)\n add $32, %rsp\n"
        "movabs $0x1111111111111111, %r10\n cmp %r10, %rbx\n je 1f\n incq al_abi_bad(%rip)\n1:\n"
        "movabs $0x2222222222222222, %r10\n cmp %r10, %rbp\n je 2f\n incq al_abi_bad(%rip)\n2:\n"
        "movabs $0x3333333333333333, %r10\n cmp %r10, %r12\n je 3f\n incq al_abi_bad(%rip)\n3:\n"
        "movabs $0x4444444444444444, %r10\n cmp %r10, %r13\n je 4f\n incq al_abi_bad(%rip)\n4:\n"
        "movabs $0x5555555555555555, %r10\n cmp %r10, %r14\n je 5f\n incq al_abi_bad(%rip)\n5:\n"
        "movabs $0x6666666666666666, %r10\n cmp %r10, %r15\n je 6f\n incq al_abi_bad(%rip)\n6:\n"
        "pop %r15\n pop %r14\n pop %r13\n pop %r12\n pop %rbp\n pop %rbx\n ret\n");
#elif ARM64
__asm__(".text\n.globl al_abi_call\nal_abi_call:\n"
        "sub sp, sp, #192\n stp x29, x30, [sp, #16]\n stp x19, x20, [sp, #32]\n stp x21, x22, [sp, #48]\n stp x23, x24, [sp, #64]\n"
        "stp x25, x26, [sp, #80]\n stp x27, x28, [sp, #96]\n stp d8, d9, [sp, #112]\n stp d10, d11, [sp, #128]\n stp d12, d13, [sp, #144]\n stp d14, d15, [sp, #160]\n"
        "ldp x9, x10, [sp, #192]\n stp x9, x10, [sp, #0]\n"
        "movz x19, #0x1901\n movz x20, #0x1902\n movz x21, #0x1903\n movz x22, #0x1904\n movz x23, #0x1905\n movz x24, #0x1906\n movz x25, #0x1907\n movz x26, #0x1908\n movz x27, #0x1909\n movz x28, #0x190a\n movz x29, #0x190b\n"
        "movz x9, #0x1801\n fmov d8, x9\n movz x9, #0x1802\n fmov d9, x9\n movz x9, #0x1803\n fmov d10, x9\n movz x9, #0x1804\n fmov d11, x9\n"
        "movz x9, #0x1805\n fmov d12, x9\n movz x9, #0x1806\n fmov d13, x9\n movz x9, #0x1807\n fmov d14, x9\n movz x9, #0x1808\n fmov d15, x9\n"
        "adrp x16, al_abi_target\n ldr x16, [x16, :lo12:al_abi_target]\n blr x16\n"
        "adrp x11, al_abi_bad\n add x11, x11, :lo12:al_abi_bad\n ldr x12, [x11]\n"
        ".macro chk_x r, v\n movz x13, #\\v\n cmp \\r, x13\n cinc x12, x12, ne\n .endm\n"
        "chk_x x19, 0x1901\n chk_x x20, 0x1902\n chk_x x21, 0x1903\n chk_x x22, 0x1904\n chk_x x23, 0x1905\n chk_x x24, 0x1906\n chk_x x25, 0x1907\n chk_x x26, 0x1908\n chk_x x27, 0x1909\n chk_x x28, 0x190a\n chk_x x29, 0x190b\n"
        ".macro chk_d r, v\n fmov x14, \\r\n movz x13, #\\v\n cmp x14, x13\n cinc x12, x12, ne\n .endm\n"
        "chk_d d8, 0x1801\n chk_d d9, 0x1802\n chk_d d10, 0x1803\n chk_d d11, 0x1804\n chk_d d12, 0x1805\n chk_d d13, 0x1806\n chk_d d14, 0x1807\n chk_d d15, 0x1808\n"
        "str x12, [x11]\n"
        "ldp x29, x30, [sp, #16]\n ldp x19, x20, [sp, #32]\n ldp x21, x22, [sp, #48]\n ldp x23, x24, [sp, #64]\n ldp x25, x26, [sp, #80]\n ldp x27, x28, [sp, #96]\n"
        "ldp d8, d9, [sp, #112]\n ldp d10, d11, [sp, #128]\n ldp d12, d13, [sp, #144]\n ldp d14, d15, [sp, #160]\n add sp, sp, #192\n ret\n");
#else
__asm__(".text\n.globl al_abi_call\nal_abi_call:\n.option push\n.option arch, +d\n"
        "addi sp, sp, -224\n sd ra, 16(sp)\n sd s0, 24(sp)\n sd s1, 32(sp)\n sd s2, 40(sp)\n sd s3, 48(sp)\n sd s4, 56(sp)\n sd s5, 64(sp)\n sd s6, 72(sp)\n sd s7, 80(sp)\n"
        "sd s8, 88(sp)\n sd s9, 96(sp)\n sd s10, 104(sp)\n sd s11, 112(sp)\n fsd fs0, 120(sp)\n fsd fs1, 128(sp)\n fsd fs2, 136(sp)\n fsd fs3, 144(sp)\n fsd fs4, 152(sp)\n"
        "fsd fs5, 160(sp)\n fsd fs6, 168(sp)\n fsd fs7, 176(sp)\n fsd fs8, 184(sp)\n fsd fs9, 192(sp)\n fsd fs10, 200(sp)\n fsd fs11, 208(sp)\n"
        "ld t0, 224(sp)\n ld t1, 232(sp)\n sd t0, 0(sp)\n sd t1, 8(sp)\n"
        "li s0, 0x101\n li s1, 0x102\n li s2, 0x103\n li s3, 0x104\n li s4, 0x105\n li s5, 0x106\n li s6, 0x107\n li s7, 0x108\n li s8, 0x109\n li s9, 0x10a\n li s10, 0x10b\n li s11, 0x10c\n"
        "li t0, 0x201\n fmv.d.x fs0, t0\n li t0, 0x202\n fmv.d.x fs1, t0\n li t0, 0x203\n fmv.d.x fs2, t0\n li t0, 0x204\n fmv.d.x fs3, t0\n li t0, 0x205\n fmv.d.x fs4, t0\n li t0, 0x206\n fmv.d.x fs5, t0\n"
        "li t0, 0x207\n fmv.d.x fs6, t0\n li t0, 0x208\n fmv.d.x fs7, t0\n li t0, 0x209\n fmv.d.x fs8, t0\n li t0, 0x20a\n fmv.d.x fs9, t0\n li t0, 0x20b\n fmv.d.x fs10, t0\n li t0, 0x20c\n fmv.d.x fs11, t0\n"
        "la t2, al_abi_target\n ld t2, 0(t2)\n jalr t2\n"
        "la t3, al_abi_bad\n ld t4, 0(t3)\n"
        ".macro chk_s r, v\n li t5, \\v\n xor t5, \\r, t5\n snez t5, t5\n add t4, t4, t5\n .endm\n"
        "chk_s s0, 0x101\n chk_s s1, 0x102\n chk_s s2, 0x103\n chk_s s3, 0x104\n chk_s s4, 0x105\n chk_s s5, 0x106\n chk_s s6, 0x107\n chk_s s7, 0x108\n chk_s s8, 0x109\n chk_s s9, 0x10a\n chk_s s10, 0x10b\n chk_s s11, 0x10c\n"
        ".macro chk_f r, v\n fmv.x.d t6, \\r\n li t5, \\v\n xor t5, t6, t5\n snez t5, t5\n add t4, t4, t5\n .endm\n"
        "chk_f fs0, 0x201\n chk_f fs1, 0x202\n chk_f fs2, 0x203\n chk_f fs3, 0x204\n chk_f fs4, 0x205\n chk_f fs5, 0x206\n chk_f fs6, 0x207\n chk_f fs7, 0x208\n chk_f fs8, 0x209\n chk_f fs9, 0x20a\n chk_f fs10, 0x20b\n chk_f fs11, 0x20c\n"
        "sd t4, 0(t3)\n"
        "ld ra, 16(sp)\n ld s0, 24(sp)\n ld s1, 32(sp)\n ld s2, 40(sp)\n ld s3, 48(sp)\n ld s4, 56(sp)\n ld s5, 64(sp)\n ld s6, 72(sp)\n ld s7, 80(sp)\n ld s8, 88(sp)\n ld s9, 96(sp)\n ld s10, 104(sp)\n ld s11, 112(sp)\n"
        "fld fs0, 120(sp)\n fld fs1, 128(sp)\n fld fs2, 136(sp)\n fld fs3, 144(sp)\n fld fs4, 152(sp)\n fld fs5, 160(sp)\n fld fs6, 168(sp)\n fld fs7, 176(sp)\n fld fs8, 184(sp)\n fld fs9, 192(sp)\n fld fs10, 200(sp)\n fld fs11, 208(sp)\n"
        "addi sp, sp, 224\n ret\n.option pop\n");
#endif

//      Every routine once, through the trampoline, with the shapes the mixing core gives them.
static void al_abi_all(void)
{
        float *left = (float *)al_high[0] - 512, *right = (float *)al_high[1] - 512, *table = (float *)al_high[2] - 3 * 64, *out0 = (float *)al_high[3] - 512, *out1 = (float *)al_high[4] - 512;
        unsigned char *bytes = al_high[3] - 8192 - 4096;
        float *planes[3] = {out0, out1, (float *)al_high[1] - 1024};
        float matrix[16] = {1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0};

        for (int i = 0; i < 512; i++)
                left[i] = right[i] = 0.25f;
        for (int i = 0; i < 3 * 64; i++)
                table[i] = 0.5f;
        __builtin_memset(al_high[3] - 8192 - 4096, 0x11, 8192);
        al_abi_target = (void *)resample_polyphase2_f32;
        ((void (*)(float *, float *, const float *, const float *, const float *, unsigned long long, unsigned long long, unsigned long long, unsigned long long, unsigned long long))al_abi_call)(out0, out1, left, right, table, 64, 2, 0x180000000ull, 0x100000000ull, 40);
        al_abi_target = (void *)resample_polyphase2_f32_rows;
        ((unsigned long long (*)(float *, float *, const float *, const float *, const float *, unsigned long long, unsigned long long, unsigned long long, unsigned long long, unsigned long long))al_abi_call)(out0, out1, left, right, table, 64, 2, 0x180000000ull, 0x100000000ull, 3);
        al_abi_target = (void *)memory_zero;
        ((void (*)(unsigned char *, unsigned long long))al_abi_call)(bytes + 2048, 136);
        ((void (*)(unsigned char *, unsigned long long))al_abi_call)(bytes + 2048, 300);
        al_abi_target = (void *)memory_s16_to_u8;
        ((void (*)(unsigned char *, const unsigned char *, unsigned long long))al_abi_call)(bytes + 2048, bytes, 103);
        al_abi_target = (void *)memory_reverse_units;
        ((void (*)(unsigned char *, unsigned long long, unsigned long long))al_abi_call)(bytes + 1024, 33, 3);
        ((void (*)(unsigned char *, unsigned long long, unsigned long long))al_abi_call)(bytes + 1024, 20, 4);
        ((void (*)(unsigned char *, unsigned long long, unsigned long long))al_abi_call)(bytes + 1024, 40, 2);
        al_abi_target = (void *)memory_key_printable;
        ((unsigned long long (*)(const unsigned char *, unsigned long long))al_abi_call)(bytes, 40);
        al_abi_target = (void *)memory_text_clean;
        ((unsigned long long (*)(unsigned char *, const unsigned char *, unsigned long long))al_abi_call)(bytes + 2048, bytes, 103);
        {
                float state[2] = {0, 0};
                unsigned long long count = 0;

                al_abi_target = (void *)memory_window_peaks;
                ((unsigned long long (*)(float *, float *, const float *, const float *, unsigned long long, float *, unsigned long long *, unsigned long long, unsigned long long))al_abi_call)(out0, out1, left, right, 100, state, &count, 7, 1);
        }
        al_abi_target = (void *)f32_planes_from_law;
        ((void (*)(float **, const unsigned char *, unsigned long long, unsigned long long, unsigned long long))al_abi_call)(planes, bytes, 103, 2, 1);
        ((void (*)(float **, const unsigned char *, unsigned long long, unsigned long long, unsigned long long))al_abi_call)(planes, bytes, 103, 3, 0);
        al_abi_target = (void *)pcm_pack24;
        ((void (*)(unsigned char *, const unsigned *, unsigned long long))al_abi_call)(bytes, (const unsigned *)left, 103);
        al_abi_target = (void *)pcm_widen;
        for (unsigned long long kind = 0; kind < 7; kind++)
                ((void (*)(unsigned *, const unsigned char *, unsigned long long, unsigned long long))al_abi_call)((unsigned *)out0, bytes, 103, kind);
        al_abi_target = (void *)f32_scale_peak;
        ((float (*)(float *, const float *, unsigned long long, float))al_abi_call)(out0, left, 295, 0.5f);
        al_abi_target = (void *)f32_scale_add_peak;
        ((float (*)(float *, const float *, unsigned long long, float))al_abi_call)(out0, left, 295, 0.5f);
        //      Counts that leave something over for every step down (a turn of 32 or 16, then 8, then 4, then ones) and
        //      the channel counts that have a body of their own and the one that walks a sample at a time.
        for (unsigned long long channels = 1; channels <= 3; channels++)
        {
                al_abi_target = (void *)f32_planes_from_s16;
                ((void (*)(float **, const unsigned char *, unsigned long long, unsigned long long))al_abi_call)(planes, bytes, 103, channels);
                al_abi_target = (void *)f32_planes_from_s32;
                ((void (*)(float **, const unsigned char *, unsigned long long, unsigned long long))al_abi_call)(planes, bytes, 103, channels);
                al_abi_target = (void *)f32_planes_from_f32;
                ((void (*)(float **, const unsigned char *, unsigned long long, unsigned long long))al_abi_call)(planes, bytes, 103, channels);
        }
        al_abi_target = (void *)f32_pair_to_s16;
        ((void (*)(unsigned char *, const float *, const float *, unsigned long long))al_abi_call)(bytes, left, right, 103);
        al_abi_target = (void *)f32_pair_to_s32;
        ((void (*)(unsigned char *, const float *, const float *, unsigned long long))al_abi_call)(bytes, left, right, 103);
        al_abi_target = (void *)f32_pair_to_s24_32;
        ((void (*)(unsigned char *, const float *, const float *, unsigned long long))al_abi_call)(bytes, left, right, 103);
        al_abi_target = (void *)f32_pair_to_f32;
        ((void (*)(unsigned char *, const float *, const float *, unsigned long long))al_abi_call)(bytes, left, right, 103);
        al_abi_target = (void *)f32_pair_clip;
        ((unsigned long long (*)(float *, float *, unsigned long long))al_abi_call)(out0, out1, 103);
        al_abi_target = (void *)f32_rows_mix2;
        ((void (*)(float *, float *, const float *, unsigned long long, const float *, unsigned long long, unsigned long long))al_abi_call)(out0, out1, left, 80, matrix, 77, 1);
        ((void (*)(float *, float *, const float *, unsigned long long, const float *, unsigned long long, unsigned long long))al_abi_call)(out0, out1, left, 80, matrix, 77, 6);
        ((void (*)(float *, float *, const float *, unsigned long long, const float *, unsigned long long, unsigned long long))al_abi_call)(out0, out1, left, 80, matrix, 77, 0);
}

static void al_abi_tier(string_address tier)
{
        al_abi_bad = 0;
        al_abi_all();
        string_format(log, "  %s\n", tier);
        check("every routine returns the registers its machine's convention keeps (the callee-saved set)", al_abi_bad == 0);
}

/*
        The frames of a call are the one-frame walk's to the bit: an even count's pairs (the x86 bodies take two frames a turn,
        with a frame's accumulators apart from the next's) answer what the same frames answer one call at a time, count 1 being
        the one-frame walk. Every output of both is compared, and so is the frame each call starts at (pos + k * step).
*/
static float al_pairs_in[2][16384], al_pairs_table[513 * 512], al_pairs_two[2][512], al_pairs_one[2][512];

static unsigned long long al_polyphase_pairs(void)
{
        unsigned long long bad = 0;
        static p8 filled;

        if (!filled)
        {
                for (int plane = 0; plane < 2; plane++)
                        for (int i = 0; i < 16384; i++)
                                al_pairs_in[plane][i] = al_unit();
                for (int i = 0; i < 513 * 512; i++)
                        al_pairs_table[i] = al_unit();
                filled = true;
        }
        for (int round = 0; round < 400; round++)
        {
                unsigned long long taps = 64ull << (al_next() % 4), phases = 1 + al_next() % 512, count = 2 * (1 + al_next() % 128);
                unsigned long long pos = al_next() & 0xffffffffffull, step = al_next() % (4ull << 32);

                for (int plane = 0; plane < 2; plane++)
                        for (int i = 0; i < 512; i++)
                                al_pairs_two[plane][i] = al_pairs_one[plane][i] = 1234.5f;
                al_polyphase_routine(al_pairs_two[0], al_pairs_two[1], al_pairs_in[0], al_pairs_in[1], al_pairs_table, taps, phases, pos, step, count);
                for (unsigned long long k = 0; k < count; k++)
                        al_polyphase_routine(al_pairs_one[0] + k, al_pairs_one[1] + k, al_pairs_in[0], al_pairs_in[1], al_pairs_table, taps, phases,
                                             pos + k * step, step, 1);
                for (int plane = 0; plane < 2; plane++)
                        for (int i = 0; i < 512; i++)
                                if (al_bits(al_pairs_two[plane][i]) != al_bits(al_pairs_one[plane][i]))
                                        bad++;
        }
        return bad;
}

static void al_polyphase_tier(string_address tier, int bits)
{
        al_faults faults = {0, 0, 0, 0, 0, 0, 0};

        string_format(log, "  %s\n", tier);
        al_polyphase_walk(bits, &faults);
        check("resample_polyphase2_f32: every shape and placement within (taps / 8 + 12) eps of the exact sum of the terms", faults.bound == 0);
        check("resample_polyphase2_f32: sentinels before the outputs untouched, nothing written past count (the guard page)", faults.outside == 0);
        check("resample_polyphase2_f32: inputs and table unchanged", faults.kept == 0);
        check("resample_polyphase2_f32: small integers with a weight of a half exactly", faults.exact == 0);
        check("resample_polyphase2_f32: the weight of a row is its definition to the bit", faults.weight == 0);
        if (bits)
                check("resample_polyphase2_f32: the C's bits", faults.bits == 0);
        check("resample_polyphase2_f32: cases ran", faults.cases > 900);
#if X64
        check("resample_polyphase2_f32: an even count is its frames one call at a time, to the bit (the frames of a pair are the one-frame walk's)", al_polyphase_pairs() == 0);
#endif
        {
                al_faults rows = {0, 0, 0, 0, 0, 0, 0};

                al_rows_walk(bits, &rows);
                check("resample_polyphase2_f32_rows: every shape and placement within (taps / 8 + 12) eps of the exact sum of the terms of its row", rows.bound == 0);
                check("resample_polyphase2_f32_rows: nothing written past count and sentinels before the outputs untouched (the guard page), no row past the last read", rows.outside == 0);
                check("resample_polyphase2_f32_rows: inputs and table unchanged", rows.kept == 0);
                check("resample_polyphase2_f32_rows: the answer is pos + count * step", rows.exact == 0);
                check("resample_polyphase2_f32_rows: at a weight of nothing the weighted routine's bits", rows.weight == 0);
                if (bits)
                        check("resample_polyphase2_f32_rows: the C's bits", rows.bits == 0);
                check("resample_polyphase2_f32_rows: cases ran", rows.cases > 800);
        }
        check("mix.c: 44.1 kHz into 48 kHz, two tones within 2e-5 rms of the tones", al_engine(44100, 48000) < 2e-5);
        check("mix.c: 22.05 kHz into 48 kHz, two tones within 2e-5 rms of the tones", al_engine(22050, 48000) < 2e-5);
        check("mix.c: 96 kHz into 48 kHz, two tones within 2e-5 rms of the tones", al_engine(96000, 48000) < 2e-5);
        check("mix.c: 32 kHz into 48 kHz, two tones within 2e-5 rms of the tones", al_engine(32000, 48000) < 2e-5);
}

/*
        The sound graph's cost an edge with the library's loop (src/audio/graph.c, audio_graph_spread, as the server runs it): a block of
        512 frames given along 1, 16 and 64 edges, the median of 101 passes of 64 blocks each, on this machine's own body. Said, not judged.
*/
#include "../../src/audio/graph.c"
static unsigned char al_graph_arena[1 << 21] __attribute__((aligned(64)));
static long al_graph_used;
static void *al_graph_memory(void *context, void *old, long old_bytes, long bytes)
{
        void *at;

        (void)context, (void)old, (void)old_bytes;
        bytes = (bytes + 63) & ~63l;
        if (bytes <= 0 || al_graph_used + bytes > (long)sizeof(al_graph_arena))
                return 0;
        at = al_graph_arena + al_graph_used;
        al_graph_used += bytes;
        memory_fill(at, 0, (positive)bytes);
        return at;
}

static fn al_graph_bench(void)
{
        static float block[AUDIO_BLOCK];
        static positive samples[101];
        struct audio_graph g;

        for (int i = 0; i < AUDIO_BLOCK; i++)
                block[i] = 0.001f * (float)(i % 97);
        for (int edges = 1; edges <= 64; edges *= edges == 1 ? 16 : 4)
        {
                long source;

                memory_fill(&g, 0, sizeof(g));
                al_graph_used = 0;
                g.grow = al_graph_memory;
                audio_graph_begin(&g);
                source = audio_graph_add(&g, AUDIO_NODE_CAPTURE, "mic", 0);
                for (int e = 0; e < edges; e++)
                {
                        char name[8] = {'o', 'u', 't', (char)('0' + e / 10), (char)('0' + e % 10), 0};

                        audio_graph_wire(&g, source, audio_graph_add(&g, AUDIO_NODE_OUTPUT, name, 0), 50, 0);
                }
                if (!audio_graph_end(&g))
                        return;
                for (int r = 0; r < 4; r++)
                        audio_graph_spread(&g, source, block, block, AUDIO_BLOCK, 1.0f);
                for (int pass = 0; pass < 101; pass++)
                {
                        positive t0 = clock_monotonic_nanoseconds();

                        for (int r = 0; r < 64; r++)
                                audio_graph_spread(&g, source, block, block, AUDIO_BLOCK, 1.0f);
                        samples[pass] = (clock_monotonic_nanoseconds() - t0) / 64;
                }
                for (int i = 1; i < 101; i++)
                        for (int j = i; j > 0 && samples[j - 1] > samples[j]; j--)
                        {
                                positive t = samples[j];

                                samples[j] = samples[j - 1], samples[j - 1] = t;
                        }
                string_format(log, "  graph: a block of 512 frames along %p edges: %p ns, %p ns an edge\n", (positive)edges, samples[50], samples[50] / (positive)edges);
        }
}

b32 main(void)
{
        check("audiolib: guard pages", al_guard());
        al_graph_bench();
        if (!al_high[AL_REGIONS - 1])
                return test_report(null);
#if X64
        {
                p8 had2 = cpu_has_avx2, hadfma = cpu_has_fma, had512 = cpu_has_avx512;

                //      The resampler's floor is the C's to the bit and its AVX2 with FMA is to the bound; the scale routines
                //      are the C's to the bit on all three of theirs, AVX-512 (the one a machine that has it takes), AVX2 and SSE2.
                if (had512)
                {
                        cpu_has_avx512 = 1;
                        al_abi_tier("avx512");
                        al_scale_tier("avx512");
                        al_zero_tier();
                }
                cpu_has_avx512 = 0;
                if (had2 && hadfma)
                {
                        al_abi_tier("avx2");
                        al_polyphase_tier("avx2 fma", 0);
                        al_scale_tier("avx2");
                        al_decode_tier("avx2");
                        al_encode_tier("avx2");
                        al_mix2_tier();
                        al_widen_tier();
                        al_pack_tier();
                        al_law_tier();
                        al_zero_tier();
                        al_pulse_tier();
                }
                cpu_has_fma = 0;
                if (had2)
                        al_polyphase_tier("avx2 without fma (sse2)", 1);
                cpu_has_avx2 = 0;
                al_abi_tier("sse2");
                al_polyphase_tier("sse2", 1);
                al_scale_tier("sse2");
                al_decode_tier("sse2");
                al_encode_tier("sse2");
                al_mix2_tier();
                al_widen_tier();
                al_pack_tier();
                al_law_tier();
                al_zero_tier();
                al_pulse_tier();
                cpu_has_avx2 = had2, cpu_has_fma = hadfma, cpu_has_avx512 = had512;
        }
#elif RISCV64
        {
                p8 vector;

                cpu_has_vector = 0;
                {
                        float o0[1], o1[1], x[128], table[128 * 2];

                        for (int i = 0; i < 128; i++)
                                x[i] = (float)i, table[i] = 1.0f, table[64 + i] = 1.0f;
                        al_first = false;
                        resample_polyphase2_f32(o0, o1, x, x, table, 64, 1, 0, 0, 1);
                        check("resample_polyphase2_f32: the first call asks for V and keeps its arguments", cpu_has_vector != 0 && o0[0] == 2016.0f && o1[0] == 2016.0f);
                }
                vector = cpu_has_vector;
                for (p32 tier = 0; tier < 2; tier++)
                {
                        cpu_has_vector = tier ? 1 : vector;
                        al_first = tier == 0;
                        al_abi_tier(tier ? "no V" : vector == 2 ? "V, every call the first" : "no V, every call the first");
                        al_polyphase_tier(tier ? "no V" : vector == 2 ? "V, every call the first" : "no V, every call the first", 0);
                        al_scale_tier(tier ? "no V" : vector == 2 ? "V, every call the first" : "no V, every call the first");
                        al_decode_tier(tier ? "no V" : vector == 2 ? "V, every call the first" : "no V, every call the first");
                        al_encode_tier(tier ? "no V" : vector == 2 ? "V, every call the first" : "no V, every call the first");
                        al_mix2_tier();
                        al_widen_tier();
                        al_pack_tier();
                        al_law_tier();
                        al_zero_tier();
                        al_pulse_tier();
                }
                al_first = false;
                cpu_has_vector = vector;
        }
#else
        al_abi_tier("neon");
        al_polyphase_tier("neon", 0);
        al_scale_tier("neon");
        al_decode_tier("neon");
        al_encode_tier("neon");
        al_mix2_tier();
        al_widen_tier();
        al_pack_tier();
        al_law_tier();
        al_zero_tier();
        al_pulse_tier();
#endif
        return test_report(null);
}
#endif /* CHECK_audiolib */

