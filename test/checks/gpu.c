/*
        test/checks/gpu.c: the card and its kit.

        A run of test/checks.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. A section is built as
        test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib -nostartfiles
        -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its
        freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name>
        test/checks.c).
*/

#ifdef CHECK_gpu
#include "../../src/lib.util.c"
/* The shim's signal macros, as terminal.c names them for gpu.c in its own lanes (shell.c's are not included here). */
#define SIGNAL_RESTART 0x10000000
#define SIGNAL_CATCH_RESTORER 0
#define SIGNAL_CATCH_FLAGS SIGNAL_RESTART
#include "../../src/canvas/gpu.c"
/*
        The GPU's four operations against the CPU's own routines (memory_copy, memory_fill_u32_rect, memory_over_tone_u32,
        memory_glass_over_u32), pixel for pixel over a whole screen, on a window of an odd width at an odd place, with
        random pixels of every alpha; then each operation timed on a window of a real size against the same routine.
        Needs an amdgpu render node; without one it says NOT RUN, and nothing of it has passed.
*/
#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

static unsigned int gpu_seed = 2463534242u;
static unsigned int gpu_random(void)
{
        gpu_seed ^= gpu_seed << 13, gpu_seed ^= gpu_seed >> 17, gpu_seed ^= gpu_seed << 5;
        return gpu_seed;
}

// The glass grain as compositor_grain_row builds it: a four by four ordered pattern of 0 to 3 levels, added (dark) or taken away (light).
static unsigned int gpu_grains[2][4][128];
static const unsigned int *gpu_grain(unsigned int light, int x, int y)
{
        static const unsigned char order[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

        for (int side = 0; side < 2; side++)
                for (int row = 0; row < 4; row++)
                        for (int at = 0; at < 64; at++)
                        {
                                unsigned int level = (unsigned int)order[row][at & 3] >> 2;

                                gpu_grains[side][row][at] = side ? 0 : level * 0x010101u;
                                gpu_grains[side][row][64 + at] = side ? level * 0x010101u : 0;
                        }
        return gpu_grains[light ? 1 : 0][y & 3] + (x & 15);
}

static unsigned int gpu_want[1500 * 700];

// The glass kernel's (op 3, gpu.asm glass) arithmetic as a C model: each of blue, green and red of the ground plus the grain level (dark) or minus it (light),
// clamped; then the ground's times (255 - the picture's alpha) rounded as the over does (x = g * (255 - a) + 127, q = (x + (x >> 8) + 1) >> 8) plus the picture's
// channel, clamped at 255. Alpha takes the over too, with no grain.
static unsigned int gpu_glass_model(unsigned int picture, unsigned int ground, unsigned int level, unsigned int light)
{
        unsigned int out = 0, inverse = 255u - (picture >> 24);

        for (unsigned int shift = 0; shift < 32; shift += 8)
        {
                unsigned int c = (ground >> shift) & 255u, q, v;

                if (shift < 24)
                        c = light ? (c > level ? c - level : 0u) : (c + level > 255u ? 255u : c + level);
                q = (c * inverse + 127u + ((c * inverse + 127u) >> 8) + 1u) >> 8;
                v = ((picture >> shift) & 255u) + q;
                out |= (v > 255u ? 255u : v) << shift;
        }
        return out;
}

// The scaled read on the card (op 8, bit 3): a 64 by 4 linear picture of random words, scaled by tables to 50 by 3 through the card's texture unit, equals the
// same picture scaled by the same tables on the processor (the linear twin). The mutant: rows of the table reversed must draw other pixels, so the row can tell
// a wrong table. Both need the card; on the host there is no card and the section returns.
static void gpu_scaled_rows(void)
{
        positive base = gpu.size - (1 << 20), src_at = base + 131072, dst_at = base + 262144, tab_at = base + 786432;
        unsigned int address_to src = (unsigned int address_to)((p8 address_to)gpu.at + src_at), address_to dst = (unsigned int address_to)((p8 address_to)gpu.at + dst_at);
        unsigned int address_to tab = (unsigned int address_to)((p8 address_to)gpu.at + tab_at);
        unsigned int desc[8], kind, seed = 31337u, twin = 0;
        b32 ran;

        if (gpu.pending)
                gpu_finish();
        for (unsigned int i = 0; i < 64 * 4; i++)
                seed = seed * 1664525u + 1013904223u, src[i] = seed;
        for (unsigned int x = 0; x < 50; x++)
                tab[x] = (x * 5 + 3) % 64;
        for (unsigned int y = 0; y < 3; y++)
                tab[50 + y] = (y * 2 + 1) % 4;
        kind = gpu_amd_image(desc, GPU_VA + src_at, 0, 64, 4, 0, false, false);
        memory_zero(dst, 64 * 4 * 4);
        gpu_slot_take();
        gpu_image_scaled_args(desc, kind | GPU_IMAGE_SCALED, GPU_VA + tab_at, 0, 0, GPU_VA + dst_at, 64, 50, 3);
        ran = gpu_run();
        for (unsigned int y = 0; y < 3; y++)
                for (unsigned int x = 0; x < 50; x++)
                        twin += dst[y * 64 + x] == src[((y * 2 + 1) % 4) * 64 + (x * 5 + 3) % 64];
        check("gpu scale (op 8, bit 3): the card's scaled read of a picture equals the linear twin scaled by the same tables (150 pixels)", kind && ran && twin == 150);
}

// Op 3 (gpu.asm glass): the grain the kernel adds is 2 * ((x ^ y) & 1) + (y & 1) for the absolute x and y, and that is the processor's 4x4 Bayer table of
// glass.c, order >> 2, at every position (checked here); and the kernel's arithmetic (gpu_glass_model) is bit-exact to memory_glass_over_u32 over random
// pictures and grounds, dark and light, at every origin mod 4. The fake card runs no kernel, so this holds the model, which is the kernel's text; the card's own
// result is a box run (step 5's card half).
static void gpu_glass_rows(void)
{
        static const unsigned int order[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
        unsigned int seed = 99991u, mismatches = 0, table_mismatches = 0, n = 0;

        for (unsigned int y = 0; y < 4; y++)
                for (unsigned int x = 0; x < 4; x++)
                        if ((order[y][x] >> 2) != 2u * ((x ^ y) & 1u) + (y & 1u))
                                table_mismatches++;
        for (unsigned int side = 0; side < 2; side++)
                for (unsigned int x0 = 0; x0 < 4; x0++)
                        for (unsigned int y0 = 0; y0 < 4; y0++)
                                for (unsigned int y = 0; y < 4; y++)
                                        for (unsigned int x = 0; x < 16; x++)
                                        {
                                                unsigned int grain[96], picture, ground, got = 0, level = 2u * (((x0 + x) ^ (y0 + y)) & 1u) + ((y0 + y) & 1u);

                                                seed = seed * 1664525u + 1013904223u, picture = seed;
                                                seed = seed * 1664525u + 1013904223u, ground = seed;
                                                memory_zero(grain, sizeof grain);
                                                grain[side ? 64 : 0] = level * 0x010101u;
                                                memory_glass_over_u32(&got, &picture, &ground, 1, grain);
                                                if (got != gpu_glass_model(picture, ground, level, side))
                                                        mismatches++;
                                                n++;
                                        }
        check("gpu glass (op 3): the kernel's grain formula is the processor's 4x4 Bayer table (order >> 2) at every position", table_mismatches == 0);
        check("gpu glass (op 3): the kernel's arithmetic is bit-exact to memory_glass_over_u32 over random pictures and grounds, dark and light, every origin mod 4",
              mismatches == 0 && n == 2 * 4 * 4 * 4 * 16);
}

// The Intel set (Gen7-Gen11 active, Gen12+ frame checks): the edge split of a byte fill, and its batch, on the host (no card).
static void gpu_i915_set_rows(void)
{
        unsigned int kept_family = gpu.family;
        gpu.family = 900;
        // The cp (op 4), the scene's batch: a picture 13 pixels by 9 rows on a pitch of 16 pixels, its destination at GPU_VA + 0x2000, its bins at
        // GPU_VA + 0x8000 (the table of one cell, then the lists). The walker's groups are the blocks of a row (4) and the rows (9); the CURBE holds the
        // width, the cells a row and the lists' offset (8 times the cells); the bins are binding table entry 1. A pitch that is not a multiple of 16
        // pixels, a destination or bins not aligned, or a width past the picture's fields is refused (0 words: the processor draws the scene).
        {
                static unsigned char buffer[4096] __attribute__((aligned(4096)));
                struct gpu_job job;
                struct gpu_args address_to a = (struct gpu_args address_to)(buffer + 2048);
                unsigned int words[64];
                unsigned int address_to state = (unsigned int address_to)(buffer + 2560);

                memset(buffer, 0, sizeof buffer);
                a->d = GPU_VA + 0x2000, a->g = GPU_VA + 0x8000, a->dp = 16, a->sp = 13, a->gp = 9, a->w = 13, a->rows = 3, a->op = 4;
                job.arguments = GPU_VA + 2048, job.state = GPU_VA + 2560, job.state_at = (unsigned int address_to)(buffer + 2560);
                job.code = GPU_VA + 3584;
                check("gpu i915 cp: a 13 by 9 scene on a pitch of 16 pixels is taken: the batch is the 64 dwords the walker takes",
                      gpu_i915_gen9_batch(&job, words, 64) == 64 && words[0] == 0x69040302u);
                check("gpu i915 cp: the walker's groups are the 4 blocks of a row and the 9 rows", words[44] == 4 && words[47] == 9);
                check("gpu i915 cp: the CURBE holds the width (13), the cells a row (1) and the lists' offset (8 * 1 cell = 8)",
                      state[48] == 13 && state[49] == 1 && state[50] == 8);
                check("gpu i915 cp: the binding table's entry 0 is the destination's surface state, entry 1 the bins'",
                      state[0] == 2560 + 64 && state[1] == 2560 + 256);
                check("gpu i915 cp: the bins' surface is at the bins' address (GPU_VA + 0x8000) and the destination's at GPU_VA + 0x2000",
                      state[(256 + 32) / 4] == (unsigned int)(GPU_VA + 0x8000) && state[(64 + 32) / 4] == (unsigned int)(GPU_VA + 0x2000));
                a->dp = 18, a->d = GPU_VA + 0x2000;
                check("gpu i915 cp: a pitch that is not a multiple of 16 pixels (18) is refused whole", gpu_i915_gen9_batch(&job, words, 64) == 0);
                a->dp = 0;
                check("gpu i915 cp: a pitch of 0 is refused whole", gpu_i915_gen9_batch(&job, words, 64) == 0);
                a->dp = 16;
                a->dp = 16, a->d = GPU_VA + 0x2010;
                check("gpu i915 cp: a destination that is not 32-byte aligned is refused whole", gpu_i915_gen9_batch(&job, words, 64) == 0);
                a->d = GPU_VA + 0x2000, a->g = GPU_VA + 0x8010;
                check("gpu i915 cp: bins that are not 64-byte aligned are refused whole", gpu_i915_gen9_batch(&job, words, 64) == 0);
                a->g = GPU_VA + 0x8000, a->x0 = 1;
                check("gpu i915 cp: a scene with an origin (x0) is refused whole", gpu_i915_gen9_batch(&job, words, 64) == 0);
                a->x0 = 0, a->w = 4097, a->dp = 4112;
                check("gpu i915 cp: a width past 4096 pixels is refused whole", gpu_i915_gen9_batch(&job, words, 64) == 0);
                a->w = 4096, a->dp = 4096;
                check("gpu i915 cp: 4096 pixels (the surface's 16384 bytes) is the most a row can be and is taken",
                      gpu_i915_gen9_batch(&job, words, 64) == 64 && words[44] == 1024);
                a->w = 13, a->dp = 16;
                check("gpu i915 cp: the scene is taken again at 13 by 9 after the refusals", gpu_i915_gen9_batch(&job, words, 64) == 64 && words[44] == 4);
        }
        // The Intel set (Gen9, fill only, not registered): the edge split of a byte fill, and its batch. The rules are measured on Gen9:
        // a 32-byte-aligned base and a span in multiples of 16. Here: d at 16 and 48, span 100 (the coordinator's rows), a pitch below the
        // span, a span under 16 bytes (all edge).
        {
                struct gpu_i915_split s;

                check("gpu i915: d at 16, span 100: the GPU takes 32..112, the head 16 bytes and the tail 4 go to the processor",
                      gpu_i915_fill_split(16, 100, 512, &s) == 0 && s.interior == 32 && s.span == 80 && s.head == 16 && s.tail == 4);
                check("gpu i915: d at 48, span 100: the GPU takes 64..144, the head 16 bytes and the tail 4 go to the processor",
                      gpu_i915_fill_split(48, 100, 512, &s) == 0 && s.interior == 64 && s.span == 80 && s.head == 16 && s.tail == 4);
                check("gpu i915: an aligned span is all interior",
                      gpu_i915_fill_split(64, 256, 1024, &s) == 0 && s.interior == 64 && s.span == 256 && s.head == 0 && s.tail == 0);
                check("gpu i915: a pitch below the span is refused, not split",
                      gpu_i915_fill_split(0, 256, 128, &s) == -1);
                check("gpu i915: a pitch that is not a multiple of 16 is refused",
                      gpu_i915_fill_split(0, 100, 510, &s) == -1);
                check("gpu i915: a pitch that is a multiple of 16 bytes but not 64 is refused (the card loses rows at 80, 96, 112 bytes)",
                      gpu_i915_fill_split(0, 100, 80, &s) == -1 && gpu_i915_fill_split(0, 100, 96, &s) == -1 && gpu_i915_fill_split(0, 100, 112, &s) == -1);
                check("gpu i915: a pitch of 64 bytes (and of 128) is taken",
                      gpu_i915_fill_split(0, 64, 64, &s) == 0 && gpu_i915_fill_split(0, 64, 128, &s) == 0);
                check("gpu i915: a span under 16 bytes is all edge: nothing for the GPU, the whole span to the processor",
                      gpu_i915_fill_split(16, 10, 512, &s) == 0 && s.span == 0 && s.head == 10 && s.tail == 0);
                check("gpu i915: a span under 16 bytes that crosses a 16-byte boundary is all edge too",
                      gpu_i915_fill_split(28, 10, 512, &s) == 0 && s.span == 0 && s.head + s.tail == 10);
                {
                        // every start and span of a sweep: the parts tile the fill exactly, and the GPU's are aligned
                        int tiled = 1;

                        for (positive d = 0; d < 96; d++)
                                for (unsigned int n = 1; n <= 160; n++)
                                {
                                        if (gpu_i915_fill_split(d, n, 512, &s) || s.head + s.span + s.tail != n || (s.span && ((s.interior & 31) || (s.span & 15))))
                                                tiled = 0;
                                }
                        check("gpu i915: over every start below 96 and span to 160, the parts tile the fill and the GPU's are aligned", tiled);
                }
                {
                        // the batch for one fill, on a card's buffer at GPU_VA: the arguments 512 bytes before the state, as the slot lays them.
                        // A whole fill: 20 pixels (80 bytes) at a 32-byte boundary, on a 512-byte pitch, 4 rows.
                        static unsigned char buffer[4096] __attribute__((aligned(4096)));
                        struct gpu_job job;
                        struct gpu_args address_to a = (struct gpu_args address_to)(buffer + 2048);
                        unsigned int words[64], n;
                        unsigned int address_to state = (unsigned int address_to)(buffer + 2560);

                        memset(buffer, 0, sizeof buffer);
                        a->d = GPU_VA + 0x2020, a->dp = 128, a->w = 20, a->rows = 4, a->op = 1, a->v = 0x11223344u;
                        job.arguments = GPU_VA + 2048, job.state = GPU_VA + 2560, job.state_at = (unsigned int address_to)(buffer + 2560);
                        job.code = GPU_VA + 3584;
                        n = gpu_i915_gen9_batch(&job, words, 64);
                        check("gpu i915: a whole fill's batch is the 64 dwords the Gen9 walker takes", n == 64 && words[0] == 0x69040302u && words[1] == 0x61010011u);
                        check("gpu i915: the walker's columns are the span's 80 bytes over 16: five, and the rows are the fill's",
                              words[44] == 5u && words[47] == 4u && words[37] == 0x7105000du && words[51] == 0xffffffffu);
                        check("gpu i915: the colour dword is the CURBE's first dword, the CURBE reads 64 bytes from the state",
                              state[48] == 0x11223344u && words[29] == 0x70010002u && words[32] == 2560 + 192);
                        check("gpu i915: the binding table's entry 0 is the surface state, and the descriptor's table pointer is the table (both in the state)",
                              state[0] == 2560 + 64 && state[36] == 2560 && words[36] == 2560 + 128);
                        check("gpu i915: the surface base is the row's first byte, 32-byte aligned",
                              state[(64 + 32) / 4] == (unsigned int)(GPU_VA + 0x2020) && ((state[(64 + 32) / 4]) & 31) == 0);
                        check("gpu i915: the surface is 80 bytes wide, 4 rows high, on a 512-byte pitch",
                              state[(64 + 8) / 4] == (79u | (3u << 16)) && state[(64 + 12) / 4] == 511u);
                        a->op = 5;
                        check("gpu i915: an op the set has no encoder for (5, bx) is refused (0 words)", gpu_i915_gen9_batch(&job, words, 64) == 0);
                        a->op = 8;
                        check("gpu i915: an op past the set (8, image) is refused (0 words)", gpu_i915_gen9_batch(&job, words, 64) == 0);
                        a->op = 1, a->d = 0;
                        check("gpu i915: a destination below GPU_VA (0) is refused (0 words)", gpu_i915_gen9_batch(&job, words, 64) == 0);
                        a->d = GPU_VA + 0x2020;
                        a->op = 3, a->s = GPU_VA + 0x4000, a->sp = 128, a->g = GPU_VA + 0x8000, a->gp = 128, a->v = 1, a->x0 = 3, a->y0 = 5;
                        check("gpu i915: glass (op 3) takes its ground: the batch is the 64 dwords, the ground is entry 2 at state 320, its base and pitch are the ground's",
                              gpu_i915_gen9_batch(&job, words, 64) == 64 && state[2] == 2560 + 320 && state[(320 + 32) / 4] == (unsigned int)(GPU_VA + 0x8000) &&
                              state[(320 + 12) / 4] == 511u && state[(64 + 32) / 4] == (unsigned int)(GPU_VA + 0x2020));
                        check("gpu i915: glass's CURBE holds v, x0 and y0 at 192, 196 and 200 (the kernel's r1.0 to r1.2)",
                              state[48] == 1u && state[49] == 3u && state[50] == 5u && words[32] == 2560 + 192);
                        a->g = GPU_VA + 0x8010;
                        check("gpu i915: a glass whose ground is not 32-byte aligned is refused whole", gpu_i915_gen9_batch(&job, words, 64) == 0);
                        a->g = GPU_VA + 0x8000, a->gp = 16;
                        check("gpu i915: a glass whose ground's pitch is below the span is refused whole", gpu_i915_gen9_batch(&job, words, 64) == 0);
                        a->gp = 128, a->op = 2;
                        check("gpu i915: x0 and y0 are glass's; a tone with a grain origin is refused whole", gpu_i915_gen9_batch(&job, words, 64) == 0);
                        a->op = 1, a->x0 = 0, a->y0 = 0;
                        a->op = 1, a->dp = 16;
                        check("gpu i915: a pitch below the span is refused (0 words)", gpu_i915_gen9_batch(&job, words, 64) == 0);
                        a->dp = 128, a->d = GPU_VA + 0x2010;
                        check("gpu i915: a fill with a head (16 bytes before the boundary) is refused whole: the processor draws all of it",
                              gpu_i915_gen9_batch(&job, words, 64) == 0);
                        a->d = GPU_VA + 0x2020, a->w = 25;
                        check("gpu i915: a fill whose span ends inside a 16-byte group (a tail) is refused whole", gpu_i915_gen9_batch(&job, words, 64) == 0);
                        a->w = 4096, a->dp = 4096;
                        check("gpu i915: 4096 pixels is the most a row can be (16384 bytes: the surface's 14-bit width) and is taken",
                              gpu_i915_gen9_batch(&job, words, 64) == 64);
                        a->w = 4097, a->dp = 4097;
                        check("gpu i915: 4097 pixels overflows the surface's width field, so it is refused, not wrapped",
                              gpu_i915_gen9_batch(&job, words, 64) == 0);
                        a->w = 20, a->dp = 128, a->rows = 16384;
                        check("gpu i915: 16384 rows is the most a surface can be (its height field is 14 bits) and is taken",
                              gpu_i915_gen9_batch(&job, words, 64) == 64);
                        a->rows = 16385;
                        check("gpu i915: 16385 rows overflows the height field, so it is refused", gpu_i915_gen9_batch(&job, words, 64) == 0);
                        a->rows = 4, a->dp = 65536;
                        check("gpu i915: a pitch of 256 KiB is the most the surface's 18-bit pitch holds and is taken", gpu_i915_gen9_batch(&job, words, 64) == 64);
                        a->dp = 65537;
                        check("gpu i915: a pitch over 256 KiB is refused", gpu_i915_gen9_batch(&job, words, 64) == 0);
                        a->dp = 128;
                        job.code = GPU_VA + 3590;
                        check("gpu i915: a kernel that does not start on a 64-byte boundary is refused (the descriptor holds bits 31:6)",
                              gpu_i915_gen9_batch(&job, words, 64) == 0);
                        job.code = GPU_VA + 3584;
                        a->op = 0, a->s = GPU_VA + 0x4000, a->sp = 128, a->w = 20, a->dp = 128, a->d = GPU_VA + 0x2020, a->rows = 4;
                        check("gpu i915: a copy's batch is 64 dwords; the source is binding table entry 1, its surface state at 256",
                              gpu_i915_gen9_batch(&job, words, 64) == 64 && state[1] == 2560 + 256 && state[0] == 2560 + 64);
                        check("gpu i915: the source's surface state: base the source's address, 80 bytes wide, 4 rows, its own 512-byte pitch",
                              state[(256 + 32) / 4] == (unsigned int)(GPU_VA + 0x4000) && state[(256 + 8) / 4] == (79u | (3u << 16)) && state[(256 + 12) / 4] == 511u);
                        check("gpu i915: a copy's destination is the same as the fill's: the GPU writes the same five columns on 4 rows",
                              words[44] == 5u && words[47] == 4u && state[(64 + 32) / 4] == (unsigned int)(GPU_VA + 0x2020));
                        a->s = GPU_VA + 0x4010;
                        check("gpu i915: a copy whose source base is not 32-byte aligned is refused (0 words)", gpu_i915_gen9_batch(&job, words, 64) == 0);
                        a->s = GPU_VA + 0x4000, a->sp = 16;
                        check("gpu i915: a copy whose source pitch holds less than the row is refused (0 words)", gpu_i915_gen9_batch(&job, words, 64) == 0);
                        a->sp = 21;
                        check("gpu i915: a copy whose source pitch is not whole 16-byte groups is refused (0 words)", gpu_i915_gen9_batch(&job, words, 64) == 0);
                        a->sp = 20;
                        check("gpu i915: a copy whose source pitch is 80 bytes (16 but not 64) is refused (0 words)", gpu_i915_gen9_batch(&job, words, 64) == 0);
                        a->sp = 128, a->s = 0;
                        check("gpu i915: a copy whose source is not in the card's buffer is refused (0 words)", gpu_i915_gen9_batch(&job, words, 64) == 0);
                        a->op = 2, a->v = 0x80402010u;
                        check("gpu i915: a tone's batch is 64 dwords, one surface (the destination), its colour in the CURBE",
                              gpu_i915_gen9_batch(&job, words, 64) == 64 && state[48] == 0x80402010u && state[0] == 2560 + 64 && state[(64 + 32) / 4] == (unsigned int)(GPU_VA + 0x2020));
                        check("gpu i915: the tone walks the same five columns on 4 rows as the fill and the copy", words[44] == 5u && words[47] == 4u);
                        a->op = 9, a->v = 0;
                        check("gpu i915: an op without an Intel encoder (9, nv12) is refused (0 words)", gpu_i915_gen9_batch(&job, words, 64) == 0);
                        a->op = 1, a->s = 0;
                        a->w = 2;
                        check("gpu i915: a fill whose span is under 16 bytes leaves the GPU nothing (0 words)", gpu_i915_gen9_batch(&job, words, 64) == 0);
                }
        }
        {
                // The scene (op 6): two records (a fill and a tone) on a 256 by 32 picture at a 256-pixel pitch, the records at GPU_VA + 3072 (64-byte aligned).
                static unsigned char buffer[8192] __attribute__((aligned(4096)));
                struct gpu_job job;
                struct gpu_args address_to a = (struct gpu_args address_to)(buffer + 2048);
                unsigned int words[64];
                unsigned int address_to state = (unsigned int address_to)(buffer + 2560);
                unsigned int address_to recs = (unsigned int address_to)(buffer + 3072);
                unsigned int n;

                positive gpu_size = gpu.size;

                memset(buffer, 0, sizeof buffer);
                gpu.size = sizeof buffer; // the records are read through the buffer: the encoder checks they lie in gpu.size
                recs[0] = 0, recs[1] = 0, recs[2] = 0, recs[3] = 256, recs[4] = 32, recs[5] = 0x11223344u, recs[6] = recs[7] = 0;
                recs[8] = 1, recs[9] = 3, recs[10] = 4, recs[11] = 20, recs[12] = 9, recs[13] = 0x80402010u, recs[14] = recs[15] = 0;
                a->d = GPU_VA + 0x2000, a->dp = 256, a->sp = 256, a->gp = 32, a->w = 256, a->rows = 8, a->op = 6, a->x0 = 0, a->y0 = 0, a->count = 2;
                a->ops = GPU_VA + 3072;
                job.arguments = GPU_VA + 2048, job.state = GPU_VA + 2560, job.state_at = (unsigned int address_to)(buffer + 2560);
                job.code = GPU_VA + 3584;
                n = gpu_i915_gen9_batch(&job, words, 64);
                check("gpu i915 scene: two records make the 64 dwords of the walker, over 64 blocks by 32 rows",
                      n == 64 && words[0] == 0x69040302u && words[29] == 0x70010002u && words[32] == 2560 + 192 && words[44] == 64u && words[47] == 32u);
                check("gpu i915 scene: the CURBE holds the count, then the first pixel's x and y (0 and 0)",
                      state[48] == 2u && state[49] == 0u && state[50] == 0u);
                check("gpu i915 scene: entry 1 is the records (two a 64-byte row, so one row: height 0, width and pitch 63), based on the first record",
                      state[1] == 2560 + 256 && state[(256 + 8) / 4] == 63u && state[(256 + 12) / 4] == 63u && state[(256 + 32) / 4] == (unsigned int)(GPU_VA + 3072));
                a->count = 3;
                check("gpu i915 scene: three records are two rows (height 1)", gpu_i915_gen9_batch(&job, words, 64) == 64 && state[(256 + 8) / 4] == (63u | (1u << 16)));
                a->count = 2, recs[8] = 2;
                check("gpu i915 scene: a record that is not a fill or a tone (kind 2, a box) is refused (0 words)", gpu_i915_gen9_batch(&job, words, 64) == 0);
                recs[8] = 1, a->ops = GPU_VA + 3072 + 8;
                check("gpu i915 scene: records not 64-byte aligned are refused (0 words)", gpu_i915_gen9_batch(&job, words, 64) == 0);
                a->ops = GPU_VA + 3072, a->count = 0;
                check("gpu i915 scene: no records is refused (0 words)", gpu_i915_gen9_batch(&job, words, 64) == 0);
                a->count = 16385;
                check("gpu i915 scene: more records than the surface's height holds is refused (0 words)", gpu_i915_gen9_batch(&job, words, 64) == 0);
                a->w = 7;
                check("gpu i915 scene: a rectangle 7 pixels wide (not a multiple of 4) is refused (0 words): its last block would be dropped",
                      gpu_i915_gen9_batch(&job, words, 64) == 0);
                a->w = 256;
                a->count = 300;
                check("gpu i915 scene: records that run past the GPU buffer are refused (0 words), not read", gpu_i915_gen9_batch(&job, words, 64) == 0);
                // Image records (kind 4 copy, 11 alpha-over, optional 0x80 R/B swap): BTI 2 surface state at offset 320 and CURBE dword 3 img_base_lo.
                {
                        positive img_va = GPU_VA + 0x4014ull;

                        a->count = 3;
                        recs[16] = 4u | 0x80u, recs[17] = 4, recs[18] = 2, recs[19] = 68, recs[20] = 18;
                        recs[21] = (unsigned int)img_va, recs[22] = (unsigned int)(img_va >> 32), recs[23] = 128;
                        check("gpu i915 scene: image copy (4 | 0x80) binds BTI 2 (32-byte aligned base) and sets CURBE dword 3",
                              gpu_i915_gen9_batch(&job, words, 64) == 64 && state[2] == 2560 + 320 &&
                                  state[51] == (unsigned int)(img_va & ~31ull) && state[(320 + 32) / 4] == (unsigned int)(img_va & ~31ull));
                        recs[16] = 11u;
                        check("gpu i915 scene: image alpha-over (11) is accepted with BTI 2", gpu_i915_gen9_batch(&job, words, 64) == 64);
                        recs[21] = (unsigned int)(img_va + 1); // unaligned (not multiple of 4)
                        check("gpu i915 scene: an image record with a non-4-byte-aligned VA is refused (0 words)", gpu_i915_gen9_batch(&job, words, 64) == 0);
                        recs[21] = (unsigned int)img_va, recs[23] = 0; // zero pitch
                        check("gpu i915 scene: an image record with zero pitch is refused (0 words)", gpu_i915_gen9_batch(&job, words, 64) == 0);
                }
                a->count = 2, a->op = 6;
                gpu.size = gpu_size;
        }
        {
                // The scale (op 7): 37 pixels of a 48-pixel source's rows, 23 rows, into a destination 48 pixels a row (whole 64-byte rows), the tables at
                // GPU_VA + 3072. The walker is ceil(37 / 4) = 10 columns by 23 rows.
                static unsigned char buffer[8192] __attribute__((aligned(4096)));
                struct gpu_job job;
                struct gpu_args address_to a = (struct gpu_args address_to)(buffer + 2048);
                unsigned int words[64];
                unsigned int address_to state = (unsigned int address_to)(buffer + 2560);
                unsigned int n;

                memset(buffer, 0, sizeof buffer);
                a->d = GPU_VA + 0x2000, a->dp = 48, a->s = GPU_VA + 0x4000, a->sp = 48, a->g = GPU_VA + 3072, a->w = 37, a->rows = 23, a->op = 7;
                job.arguments = GPU_VA + 2048, job.state = GPU_VA + 2560, job.state_at = (unsigned int address_to)(buffer + 2560);
                job.code = GPU_VA + 3584;
                n = gpu_i915_gen9_batch(&job, words, 64);
                check("gpu i915 scale: a scale's batch is the 64 dwords the Gen9 walker takes: ceil(37 / 4) = 10 columns by 23 rows",
                      n == 64 && words[0] == 0x69040302u && words[29] == 0x70010002u && words[32] == 2560 + 192 && words[44] == 10u && words[47] == 23u);
                check("gpu i915 scale: the binding table is the destination (entry 0), the source (entry 1) and the tables (entry 2)",
                      state[0] == 2560 + 64 && state[1] == 2560 + 256 && state[2] == 2560 + 320);
                check("gpu i915 scale: the CURBE holds the source's pitch (48), w (37) and the destination's pitch (48), in that order",
                      state[48] == 48u && state[49] == 37u && state[50] == 48u);
                check("gpu i915 scale: each surface is linear, 16384 bytes wide and high on a pitch of 16384, based at its own address",
                      state[(64 + 8) / 4] == (16383u | (16383u << 16)) && state[(64 + 12) / 4] == 16383u && state[(64 + 32) / 4] == (unsigned int)(GPU_VA + 0x2000) &&
                      state[(256 + 32) / 4] == (unsigned int)(GPU_VA + 0x4000) && state[(320 + 32) / 4] == (unsigned int)(GPU_VA + 3072) && state[(320 + 12) / 4] == 16383u);
                check("gpu i915 scale: the interface descriptor's binding table pointer is the state's table, and its kernel the scale's code",
                      state[36] == 2560 && state[32] == 3584);
                a->dp = 38;
                check("gpu i915 scale: a destination pitch of 38 pixels (152 bytes, not 64) is refused (0 words)", gpu_i915_gen9_batch(&job, words, 64) == 0);
                a->dp = 36;
                check("gpu i915 scale: a destination pitch of 36 pixels holds not the 37 pixels of a row, so it is refused (0 words)",
                      gpu_i915_gen9_batch(&job, words, 64) == 0);
                a->dp = 48, a->d = GPU_VA + 0x2004;
                check("gpu i915 scale: a destination base not 16-byte aligned is refused (0 words)", gpu_i915_gen9_batch(&job, words, 64) == 0);
                a->d = GPU_VA + 0x2000, a->s = GPU_VA + 0x4008;
                check("gpu i915 scale: a source base not 16-byte aligned is refused (0 words)", gpu_i915_gen9_batch(&job, words, 64) == 0);
                a->s = GPU_VA + 0x4000, a->sp = 65536;
                check("gpu i915 scale: a source pitch past a word is refused (0 words)", gpu_i915_gen9_batch(&job, words, 64) == 0);
                a->sp = 50;
                check("gpu i915 scale: a source pitch of 50 pixels (200 bytes, not 64) is refused (0 words)", gpu_i915_gen9_batch(&job, words, 64) == 0);
                a->sp = 48;
                a->sp = 48, a->w = 0;
                check("gpu i915 scale: no columns is refused (0 words)", gpu_i915_gen9_batch(&job, words, 64) == 0);
                a->w = 37, a->rows = 16385;
                check("gpu i915 scale: more rows than a surface holds is refused (0 words)", gpu_i915_gen9_batch(&job, words, 64) == 0);
                a->rows = 16384, a->dp = 65536;
                check("gpu i915 scale: a destination of 2^30 pixels is past the 2^26 the card's surfaces address, refused (0 words)", gpu_i915_gen9_batch(&job, words, 64) == 0);
                a->dp = 4096;
                check("gpu i915 scale: a destination of 2^26 pixels (4096 by 16384) is the most, and is taken", gpu_i915_gen9_batch(&job, words, 64) == 64);
                a->dp = 48, a->rows = 23;
                check("gpu i915 scale: a batch room under 64 dwords is refused (0 words)", gpu_i915_gen9_batch(&job, words, 63) == 0);
                n = gpu_i915_gen9_batch(&job, words, 64); /* the last call: the dump's words and state */
#ifdef CHECK_GPU_I915_DUMP
                // The encoder's words and state for the hardware harness (team-kernels/scale/harness.c, --encdump): GPU_VA, then the 64 batch dwords and the
                // state's 128 dwords (the state at GPU_VA + 2560 as the job lays it), one line each.
                string_format(log, "DUMP scale va %p\n", (positive)GPU_VA);
                for (unsigned int i = 0; i < 64; i++)
                        string_format(log, "W %p\n", (positive)words[i]);
                for (unsigned int i = 0; i < 128; i++)
                        string_format(log, "S %p %p\n", (positive)i, (positive)state[i]);
                log_flush();
#endif
                check("gpu i915 scale: the last batch is 64 dwords", n == 64);
        }
        gpu.family = kept_family;
}

b32 main(void)
{
        enum { BW = 1220, BH = 1320, SW = 2560, SH = 1440, W = 1219, H = 211, OW = 1500, OH = 300, X = 301, Y = 7 };
        positive src_at = GPU_DATA, dst_at = GPU_DATA + (positive)BW * BH * 4, gnd_at = dst_at + (positive)SW * SH * 4;
        positive scene_at = gnd_at + (positive)BW * BH * 4, ops_at = scene_at + (positive)7680 * 4320 * 4, bins_at = ops_at + (1 << 20);
        unsigned int address_to src, address_to dst, address_to gnd;
        struct gpu_args address_to a;
        static const char *const names[4] = {"copy", "fill", "tone over", "glass over (dark)"};
        unsigned int tones[5] = {0x80402010u, 0u, 0xff112233u, 0x01ffffffu, 0};
        positive began, cpu[4], gpu_time[4];

        gpu_glass_rows();
        gpu_i915_set_rows(); // on the host: no card needed, so before the prerequisites can return
        if (!gpu_start(((positive)BW * BH * 2 + (positive)SW * SH + (positive)7680 * 4320) * 4 + (33 << 20)))
        {
                check("gpu: a card that is there passes the self-test", !gpu.broken);
                string_format(log, "gpu: the card's four operations against the processor's NOT RUN -- no card starts here: step %p of the prerequisites failed (error %p)\n",
                              (positive)gpu_req.first, (positive)gpu_req.error);
                return test_report(null);
        }
        // The texture unit's read (op 8) drew its test at the start (gpu_image_test, a linear picture through an image resource, straight and swapped).
        check("gpu: the card's texture unit reads a picture through an image resource as it is and with red and blue the other way (gpu_image_test)", gpu_image_ok);
        gpu_scaled_rows();
        src = (unsigned int address_to)((p8 address_to)gpu.at + src_at), dst = (unsigned int address_to)((p8 address_to)gpu.at + dst_at);
        gnd = (unsigned int address_to)((p8 address_to)gpu.at + gnd_at), a = GPU_ARGS;
        tones[4] = gpu_random() | 0x80000000u;

        // 1. Every operation, the whole OW by OH screen compared, the rect W by H at X, Y.
        for (int op = 0; op < 8; op++)
        {
                unsigned int v = op == 1 ? gpu_random() : op >= 2 && op < 7 ? tones[op - 2] : 0, light = op == 7;
                int kind = op < 2 ? op : op < 7 ? 2 : 3;
                positive mismatches = 0;

                for (positive i = 0; i < (positive)W * H; i++)
                        src[i] = gpu_random(), gnd[i] = gpu_random() | 0xff000000u;
                for (positive i = 0; i < (positive)OW * OH; i++)
                        dst[i] = gpu_want[i] = gpu_random();
                for (int r = 0; r < H; r++)
                {
                        unsigned int address_to row = gpu_want + (positive)(Y + r) * OW + X;

                        if (kind == 0)
                                memory_copy(row, src + (positive)r * W, W * 4);
                        else if (kind == 1)
                                memory_fill_u32_rect(row, OW, W, 1, v);
                        else if (kind == 2)
                                memory_over_tone_u32(row, W, v);
                        else
                                memory_glass_over_u32(row, src + (positive)r * W, gnd + (positive)r * W, W, gpu_grain(light, X, Y + r));
                }
                a->s = GPU_VA + src_at, a->d = GPU_VA + dst_at + ((positive)Y * OW + X) * 4, a->g = GPU_VA + gnd_at;
                a->sp = W, a->dp = OW, a->gp = W, a->w = W, a->rows = H, a->op = (unsigned int)kind, a->v = kind == 3 ? light : v, a->x0 = X, a->y0 = Y;
                check("gpu: the operation finishes", gpu_run());
                for (positive i = 0; i < (positive)OW * OH; i++)
                {
                        checks++;
                        if (dst[i] != gpu_want[i] && (++failures, ++mismatches) < 4)
                                string_format(log, "  FAIL gpu %s %p: pixel %p,%p is %p, wanted %p\n", names[kind], (positive)op, i % OW, i / OW,
                                              (positive)dst[i], (positive)gpu_want[i]);
                }
        }

        // 2. Each operation at the size a window is, GPU (submit and wait) against the CPU's routine, best of 8, each side run back to back.
        for (int kind = 0; kind < 4; kind++)
        {
                cpu[kind] = gpu_time[kind] = ~0ull;
                a->s = GPU_VA + src_at, a->d = GPU_VA + dst_at + ((positive)60 * SW + 300) * 4, a->g = GPU_VA + src_at;
                a->sp = BW, a->dp = SW, a->gp = BW, a->w = BW, a->rows = BH, a->op = (unsigned int)kind, a->v = kind == 3 ? 0 : 0x80402010u, a->x0 = 300, a->y0 = 60;
                for (int i = 0; i < 8; i++)
                {
                        unsigned int address_to row = dst + (positive)60 * SW + 300;

                        began = clock_monotonic_nanoseconds();
                        for (int r = 0; r < BH; r++)
                                if (kind == 0)
                                        memory_copy(row + (positive)r * SW, src + (positive)r * BW, BW * 4);
                                else if (kind == 1)
                                        memory_fill_u32_rect(row + (positive)r * SW, SW, BW, 1, 0x80402010u);
                                else if (kind == 2)
                                        memory_over_tone_u32(row + (positive)r * SW, BW, 0x80402010u);
                                else
                                        memory_glass_over_u32(row + (positive)r * SW, src + (positive)r * BW, src + (positive)r * BW, BW, gpu_grain(0, 300, 60 + r));
                        began = clock_monotonic_nanoseconds() - began;
                        cpu[kind] = began < cpu[kind] ? began : cpu[kind];
                }
                for (int i = 0; i < 8; i++)
                {
                        began = clock_monotonic_nanoseconds();
                        gpu_run();
                        began = clock_monotonic_nanoseconds() - began;
                        gpu_time[kind] = began < gpu_time[kind] ? began : gpu_time[kind];
                }
                string_format(log, "  gpu: %s of %p x %p into %p x %p: GPU %p us, CPU %p us\n", names[kind], (positive)BW, (positive)BH, (positive)SW, (positive)SH,
                              gpu_time[kind] / 1000, cpu[kind] / 1000);
        }

        // 3. A scene: random fills and tones in order over a random picture, ops that run off the edge and ops with no area among them,
        // against the CPU's routines applied in the same order; the whole picture compared.
        {
                enum { SHT = 701, SP = 1024, N = 300 };
                for (int pass = 0; pass < 2; pass++)
                {
                unsigned int SWD = pass ? 1000 : 997;
                unsigned int address_to ops = (unsigned int address_to)((p8 address_to)gpu.at + ops_at), address_to sd = (unsigned int address_to)((p8 address_to)gpu.at + scene_at);
                positive bad = 0;

                for (positive i = 0; i < (positive)SP * SHT; i++)
                        sd[i] = gpu_want[i] = gpu_random();
                for (int k = 0; k < N; k++)
                {
                        unsigned int address_to q = ops + 8 * k, x0 = gpu_random() % SWD, y0 = gpu_random() % SHT;

                        q[0] = gpu_random() & 1, q[1] = x0, q[2] = y0, q[3] = x0 + gpu_random() % (k & 1 ? 1100 : 60), q[4] = y0 + gpu_random() % (k & 1 ? 800 : 40);
                        q[5] = gpu_random(), q[6] = q[7] = 0;
                        for (unsigned int y = q[2]; y < q[4] && y < SHT; y++)
                                if (q[1] < SWD && q[3] > q[1])
                                {
                                        unsigned int width = (q[3] < SWD ? q[3] : SWD) - q[1];

                                        if (q[0] == 0)
                                                memory_fill_u32_rect(gpu_want + (positive)y * SP + q[1], SP, width, 1, q[5]);
                                        else
                                                memory_over_tone_u32(gpu_want + (positive)y * SP + q[1], width, q[5]);
                                }
                }
                check("gpu: the scene finishes", gpu_scene(ops_at, N, bins_at, scene_at, SP, SWD, SHT, 0, 0, SWD, SHT));
                for (int y = 0; y < SHT; y++)
                        for (int x = 0; x < SWD; x++)
                        {
                                checks++;
                                if (sd[(positive)y * SP + x] != gpu_want[(positive)y * SP + x] && (++failures, ++bad) < 4)
                                        string_format(log, "  FAIL gpu scene: pixel %p,%p is %p, wanted %p\n", (positive)x, (positive)y, (positive)sd[(positive)y * SP + x],
                                                      (positive)gpu_want[(positive)y * SP + x]);
                        }
                }
        }

        // 4. The same at 8K: a window's clear and 199 more fills and tones, GPU (bins and run, then the run alone) against the CPU applying them, best of 5.
        {
                enum { KW = 7680, KH = 4320, M = 200 };
                unsigned int address_to ops = (unsigned int address_to)((p8 address_to)gpu.at + ops_at), address_to sd = (unsigned int address_to)((p8 address_to)gpu.at + scene_at);
                unsigned int address_to ref = (unsigned int address_to)memory((positive)KW * KH * 4);
                positive total = ~0ull, run = ~0ull, host = ~0ull, bad = 0;

                if (!ref)
                        return 1;
                ops[0] = 0, ops[1] = ops[2] = 0, ops[3] = KW, ops[4] = KH, ops[5] = 0xff203040u;
                for (int k = 1; k < M; k++)
                {
                        unsigned int address_to q = ops + 8 * k, w = 40 + gpu_random() % 1200, h = 40 + gpu_random() % 800;

                        q[0] = k % 3 == 0, q[1] = gpu_random() % (KW - w), q[2] = gpu_random() % (KH - h), q[3] = q[1] + w, q[4] = q[2] + h;
                        q[5] = k % 3 == 0 ? (gpu_random() | 0x40000000u) & 0x7fffffffu : gpu_random() | 0xff000000u, q[6] = q[7] = 0;
                }
                memory_zero(sd, (positive)KW * KH * 4);
                for (int i = 0; i < 5; i++)
                {
                        if (i)
                                memory_zero(ref, (positive)KW * KH * 4);
                        began = clock_monotonic_nanoseconds();
                        for (int k = 0; k < M; k++)
                        {
                                unsigned int address_to q = ops + 8 * k;

                                for (unsigned int y = q[2]; y < q[4]; y++)
                                        if (q[0] == 0)
                                                memory_fill_u32_rect(ref + (positive)y * KW + q[1], KW, q[3] - q[1], 1, q[5]);
                                        else
                                                memory_over_tone_u32(ref + (positive)y * KW + q[1], q[3] - q[1], q[5]);
                        }
                        began = clock_monotonic_nanoseconds() - began;
                        host = began < host ? began : host;
                }
                memory_zero(sd, (positive)KW * KH * 4);
                check("gpu: the 8K scene finishes", gpu_scene(ops_at, M, bins_at, scene_at, KW, KW, KH, 0, 0, KW, KH));
                for (positive i = 0; i < (positive)KW * KH; i++)
                {
                        checks++;
                        if (sd[i] != ref[i] && (++failures, ++bad) < 4)
                                string_format(log, "  FAIL gpu 8K scene: pixel %p,%p is %p, wanted %p\n", i % KW, i / KW, (positive)sd[i], (positive)ref[i]);
                }
                for (int i = 0; i < 5; i++)
                {
                        memory_zero(sd, (positive)KW * KH * 4);
                        began = clock_monotonic_nanoseconds();
                        gpu_scene(ops_at, M, bins_at, scene_at, KW, KW, KH, 0, 0, KW, KH);
                        began = clock_monotonic_nanoseconds() - began;
                        total = began < total ? began : total;
                        memory_zero(sd, (positive)KW * KH * 4);
                        began = clock_monotonic_nanoseconds();
                        gpu_run();
                        began = clock_monotonic_nanoseconds() - began;
                        run = began < run ? began : run;
                }
                string_format(log, "  gpu: 8K scene of %p ops: GPU with the binning %p us, the run alone %p us, CPU %p us\n", (positive)M, total / 1000, run / 1000, host / 1000);
        }
        return test_report(null);
}

#endif /* CHECK_gpu */

#if defined(CHECK_gpu_kit) || defined(CHECK_gpu_model_kit)
// The kit's seeded random words and picture (CHECK_gpu_kit's seed, shared with CHECK_gpu_model_kit).
static unsigned int gpu_seed = 88172645u;
static unsigned int gpu_random(void)
{
        gpu_seed ^= gpu_seed << 13, gpu_seed ^= gpu_seed >> 17, gpu_seed ^= gpu_seed << 5;
        return gpu_seed;
}

static unsigned int gpu_kit_want[700 * 420];

// A kit block's scene: its check, named when the scene does not finish, and the answer, so the block stops there.
static b32 gpu_scene_taken(b32 ok, string_address label)
{
        if (!ok)
        {
                checks++, failures++;
                string_format(log, "  FAIL %s\n", label);
                log_flush();
        }
        return ok;
}

/*
        A picture held to the processor's, pixel by pixel, is one check, as CHECK_gpu_model_kit counts it: the pixels that differ are counted
        and the first kept (gpu_px), and the picture's end (gpu_px_end) counts the check, a failure when any differed, and gives their number
        for its FAIL line, which says the picture and then the count and the first (gpu_px_tail). Counted pixel by pixel, one block's wrong
        corners read as 71470 failures of 2686261682 checks, and no two runs' totals could be read against each other.
*/
static struct
{
        positive bad, x, y;
        unsigned int got, want;
        string_address label;
        positive round;
} gpu_px_first;

static inline void gpu_px(b32 differs, positive x, positive y, unsigned int got, unsigned int want)
{
        if (differs && !gpu_px_first.bad++)
                gpu_px_first.x = x, gpu_px_first.y = y, gpu_px_first.got = got, gpu_px_first.want = want;
}

static positive gpu_px_end(void)
{
        positive bad = gpu_px_first.bad;

        checks++;
        failures += bad != 0;
        gpu_px_first.bad = 0;
        return bad;
}

static void gpu_px_tail(positive bad)
{
        string_format(log, ": %p pixels differ, the first %p,%p is %p, wanted %p\n", bad, gpu_px_first.x, gpu_px_first.y, (positive)gpu_px_first.got,
                      (positive)gpu_px_first.want);
        log_flush();
}

// A pixel of a kit block's picture against the processor's (its round's picture is one check, gpu_kit_picture); *bad counts the block's.
static void gpu_kit_pixel(positive *bad, string_address label, positive round, positive x, positive y, unsigned int got, unsigned int want)
{
        gpu_px(got != want, x, y, got, want);
        *bad += got != want;
        gpu_px_first.label = label, gpu_px_first.round = round;
}

// The end of a kit block's picture: one check, its FAIL line the block, the round, how many pixels differ and the first.
static void gpu_kit_picture(void)
{
        string_address label = gpu_px_first.label;
        positive round = gpu_px_first.round, bad = gpu_px_end();

        if (bad)
                string_format(log, "  FAIL %s round %p", label, round), gpu_px_tail(bad);
}
#endif

#ifdef CHECK_gpu_kit
#include "../../src/canvas/gpu.c"
// The window manager's state of the server these rows drive: the first slot (xwm.c keeps one a server since its slots, compositor_xwm_at).
#define compositor_xwm (*compositor_xwm_at)
/*
        The kit's anti-aliased box on the GPU against ui_kit_fill, the painter it replaces: rounds of random boxes laid in order
        over a random picture, in 256ths, off every edge, thin as a hair, wider than tall, with radii past the half (CSS
        scales them), none at all, no area, and every alpha, the whole picture compared after each round. Those rows need
        an amdgpu render node: without one they are said NOT RUN, and so are the prerequisites' steps a machine with no card
        cannot reach. The rows before them need none (the fake card, fence, log and kernel under gpu.c): they run anywhere.
*/

// The bench's samples sorted (insertion: a few runs), so that the median and the best are the middle one and the first.
// The calling thread's CPU time (CLOCK_THREAD_CPUTIME_ID), in ns: a card job's wait is in its wall time, and this is what the job took of the processor.
static positive thread_cpu_nanoseconds(void)
{
        timespec now = {0, 0};

        if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, address_of now) < 0)
                return 0;
        return (positive)now.tv_sec * 1000000000 + (positive)now.tv_nsec;
}

static void gpu_bench_sort(positive *took, int n)
{
        for (int i = 1; i < n; i++)
                for (int j = i; j > 0 && took[j - 1] > took[j]; j--)
                {
                        positive keep = took[j];

                        took[j] = took[j - 1], took[j - 1] = keep;
                }
}

/*
        The recorded controls on the GPU: the spinner's arc and ring (ui_spinner_draw on the processor against the same painter recording into
        scene memory the GPU reads, at scales 100, 150, 200 and 300, three sizes). Each recording is replayed by gpu_scene over a ground the
        scene's first op makes, and every pixel compared with the processor's. The whole field is not here: its text needs the field's state.
*/
/*
        The apps' pictures (the launcher first): the app's state set as its window has it, its picture painted on the processor, and the same painter
        run again with the app's kit ui recording into scene memory the GPU reads; the replay must equal the processor's picture bit for bit. Light
        then dark, at 100, 150, 200 and 300 percent. The app's region is the 64 MiB the check reserves past the kit's own. A frame whose scene is lost
        is printed as a named known diff with the painter that lost it (R3 and R4), and is not compared.
*/
static font_engine app_engine;
static p8 app_atlas[512 * 512];
static font_face app_face;
static unsigned int app_scratch[2000 * UI_SCRATCH_ROWS];
static struct windows *app_table_scratch; // the table, mapped at its first use (windows_scratch_table)
static struct ui_scene app_scene;

// The regions are the kit's own (the picture at GPU_DATA with its pitch, the ops, the binning, and the text covers' region the text rows reuse
// after this). At 100 percent the launcher (560 by 400) fits the kit's 700 by 419 picture; the larger scales need a bigger picture region.
// fixed_pitch: 0 for the picture's own width as its pitch, else that pitch (the kit's 700 buffer for 100 percent).
static struct mixer_big app_mixer;
static int app_group; // which Settings page (its group) app 4 paints
static struct ui_popmenu app_menu;
static struct ui_findbar app_find;

// The find bar, drawn at the top of its picture.
static void app_find_paint(unsigned int address_to pixels)
{
        ui_begin(&settings.ui, pixels, settings.own.table->stride, settings.width, settings.height);
        ui_findbar_draw(&settings.ui, &app_find, 0, settings.width, "Find");
}

// The popup menu: its picture is the window's, begun as the window is, with the menu drawn in its top left (ui_popmenu_draw).
static void app_popmenu_paint(unsigned int address_to pixels)
{
        ui_begin(&settings.ui, pixels, settings.own.table->stride, settings.width, settings.height);
        ui_popmenu_draw(&settings.ui, &app_menu);
}

static void app_settings_paint(unsigned int address_to pixels)
{
        settings_draw_rows(pixels, 0, 0);
}

// The gallery showcase: the page drawn into the picture (gallery_draw paints the paper and every control; the picture is begun here).
static void app_gallery_paint(unsigned int address_to pixels)
{
        ui_begin(&settings.ui, pixels, settings.own.table->stride, settings.width, settings.height);
        gallery_draw(&settings.ui);
}

// The mixer's tray popup in logical pixels: as wide as one strip comes to in the kit's row, at 100 percent.
static int app_quick_w(void)
{
        return ui_strips_tree(&(struct ui){.scale = 100}, &app_mixer.strips, 1, 0);
}

// which: 0 the launcher; 1 the mixer's window; 2 the mixer's tray popup; 3 the mixer's window with the recording page and no microphone.
// fixed_pitch: 0 for the picture's own width as its pitch, else that pitch.
static void app_rows(int which, positive pic_at, positive ops_at, positive bins_at, positive cover_at, positive cover_size, const int *scales, int count,
                     int fixed_pitch)
{
        unsigned int address_to ops = (unsigned int address_to)((p8 address_to)gpu.at + ops_at);
        unsigned int address_to pic = (unsigned int address_to)((p8 address_to)gpu.at + pic_at);
        positive size = 0, bytes = compositor_map_file(program_argument(3), &size);

        if (!bytes || font_load(&app_face, (const p8 address_to)bytes, size, 0) != 0)
        {
                check("gpu apps: the app's face loads", false);
                return;
        }
        font_start(&app_engine, app_atlas, 512, 512);
        // The processor's pictures are the largest of the scales' (memory(), as the 8K section takes its reference).
        positive need = 0;
        for (int k = 0; k < count; k++)
        {
                int s = scales[k], W = (which == 0 ? LAUNCHER_WIDTH : which == 2 ? app_quick_w() : which == 4 ? SETTINGS_WIDTH : which == 5 ? GALLERY_W : which == 6 ? 320 : which == 7 ? 480 : MIXER_WIDTH) * s / 100;
                int H = (which == 0 ? 400 : which == 2 ? MIXER_QUICK_H : which == 4 ? SETTINGS_HEIGHT : which == 5 ? GALLERY_H : which == 6 ? 300 : which == 7 ? 60 : MIXER_HEIGHT) * s / 100, pitch = fixed_pitch ? fixed_pitch : (W + 3) & ~3;

                if ((positive)pitch * H > need)
                        need = (positive)pitch * H;
        }
        unsigned int address_to cpu = (unsigned int address_to)memory(need * 4), address_to dummy = (unsigned int address_to)memory(need * 4);
        for (int mode = 0; mode < 2; mode++)
        {
                ui_kit_theme(mode ? UI_THEME_DARK : UI_THEME_LIGHT, UI_ACCENT_DAWN, 0);
                for (int k = 0; k < count; k++)
                {
                        int s = scales[k], W = (which == 0 ? LAUNCHER_WIDTH : which == 2 ? app_quick_w() : which == 4 ? SETTINGS_WIDTH : which == 5 ? GALLERY_W : which == 6 ? 320 : which == 7 ? 480 : MIXER_WIDTH) * s / 100;
                        int H = (which == 0 ? 400 : which == 2 ? MIXER_QUICK_H : which == 4 ? SETTINGS_HEIGHT : which == 5 ? GALLERY_H : which == 6 ? 300 : which == 7 ? 60 : MIXER_HEIGHT) * s / 100, pitch = fixed_pitch ? fixed_pitch : (W + 3) & ~3;
                        int bad = 0;
                        void (*paint)(unsigned int address_to pixels) = which == 0 ? launcher_draw : which == 4 ? app_settings_paint : which == 5 ? app_gallery_paint : which == 6 ? app_popmenu_paint : which == 7 ? app_find_paint : which == 8 ? editor_draw : mixer_draw;

                        // The app as its window has it, set up here and not by its start: the scale, the faces, the window's table, the picture's size.
                        windows_scratch_zero(windows_scratch_table(&app_table_scratch));
                        windows_scratch_table(&app_table_scratch)->stride = (unsigned int)pitch, windows_scratch_table(&app_table_scratch)->output_width = (unsigned int)W, windows_scratch_table(&app_table_scratch)->output_height = (unsigned int)H, windows_scratch_table(&app_table_scratch)->scale = (unsigned int)s;
                        if (which == 0)
                        {
                                // The launcher: three programs of the desktop listed, the field and the power row.
                                launcher.own.table = windows_scratch_table(&app_table_scratch);
                                launcher.width = W, launcher.height = H;
                                launcher.ui.scale = s, launcher.ui.engine = &app_engine, launcher.ui.regular = launcher.ui.bold = &app_face;
                                launcher.ui.scratch = app_scratch, launcher.ui.scratch_width = 2000, launcher.ui.opaque = 1, launcher.ui.glass = 0;
                                launcher.entries = 3;
                                for (int i = 0; i < 3; i++)
                                        launcher.entry[i].kind = LAUNCHER_WINDOW, launcher.entry[i].index = (unsigned short)i;
                                launcher.focused = 1;
                                launcher.search.kind = 0;
                        }
                        else if (which == 8)
                        {
                                // The editor: its file (opened by editor_open from a path, written here). Group 0 is the plain text area with lines, a tab,
                                // a long line, accented and wide text; group 1 the code view (every tab's mode) on the same file without the wide text, which
                                // the recorder takes (tabs skipped, accents as cells); group 2 the code view with the wide text, a named loss (font_draw on
                                // the ground: font_blend, the wide cell).
                                static const char text_wide[] = "first line\n\tindented with a tab\n"
                                                                "a long line that goes on for a while past the width of the window and beyond it, so the view has to cut it, over and over, to the end\n"
                                                                "na\xc3\xafve caf\xc3\xa9 \xe2\x80\x94 \xc3\xbcnicode \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e text\n"
                                                                "last\n";
                                static const char text_code[] = "first line\n\tindented with a tab\n"
                                                               "a long line that goes on for a while past the width of the window and beyond it, so the view has to cut it, over and over, to the end\n"
                                                               "na\xc3\xafve caf\xc3\xa9 \xe2\x80\x94 \xc3\xbcnicode text\n"
                                                               "last\n";
                                const char address_to content = app_group == 1 ? text_code : text_wide;
                                int handle = (int)system_open_at_mode(AT_FDCWD, (string_address)"/tmp/mw-editor-rows.txt", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);

                                if (handle >= 0)
                                {
                                        system_write_once(handle, (void *)content, string_length((string_address)content));
                                        system_close(handle);
                                }
                                editor.own.stride = (unsigned int)pitch;
                                editor.width = W, editor.height = H;
                                editor.ui.scale = s, editor.ui.engine = &app_engine, editor.ui.regular = editor.ui.bold = editor.ui.mono = &app_face;
                                editor.ui.scratch = app_scratch, editor.ui.scratch_width = 2000, editor.ui.opaque = 1, editor.ui.glass = 0;
                                editor.focused = 1, editor.count = 0, editor.tab = 0;
                                if (!editor_open((string_address)"/tmp/mw-editor-rows.txt"))
                                        check("gpu apps: the editor opens its file", false);
                                else if (app_group == 0)
                                        editor_now()->view.mode = UI_TEXT_AREA;
                        }
                        else if (which == 7)
                        {
                                // The find bar: its field (the query, the replacement, or the line's number), its mode (group 0 find, 1 replace, 2 line).
                                settings.own.table = windows_scratch_table(&app_table_scratch);
                                settings.width = W, settings.height = H;
                                settings.ui.scale = s, settings.ui.engine = &app_engine, settings.ui.regular = settings.ui.bold = settings.ui.mono = &app_face;
                                settings.ui.scratch = app_scratch, settings.ui.scratch_width = 2000, settings.ui.opaque = 1, settings.ui.glass = 0;
                                gallery.on = 0;
                                memory_zero(&app_find, sizeof(app_find));
                                app_find.mode = app_group == 0 ? UI_FINDBAR_FIND : app_group == 1 ? UI_FINDBAR_REPLACE : UI_FINDBAR_LINE;
                                ui_field_open(&app_find.field, &settings.own, "");
                                {
                                        const char address_to query = app_group == 2 ? "42" : "needle";

                                        ui_text_set(&app_find.field, (const p8 address_to)query, string_length((string_address)query));
                                }
                                app_find.field.focused = 1;
                        }
                        else if (which == 6)
                        {
                                // A popup menu: its items (a checked one, a disabled one, a separator, a submenu) and its hot item (the group's variant).
                                settings.own.table = windows_scratch_table(&app_table_scratch);
                                settings.width = W, settings.height = H;
                                settings.ui.scale = s, settings.ui.engine = &app_engine, settings.ui.regular = settings.ui.bold = settings.ui.mono = &app_face;
                                settings.ui.scratch = app_scratch, settings.ui.scratch_width = 2000, settings.ui.opaque = 1, settings.ui.glass = 0;
                                gallery.on = 0;
                                memory_zero(&app_menu, sizeof(app_menu));
                                ui_popmenu_add(&app_menu, "Copy", "Ctrl+C", 0, 0);
                                ui_popmenu_add(&app_menu, "Paste", "Ctrl+V", UI_MENU_DISABLED, 1);
                                ui_popmenu_add(&app_menu, null, null, UI_MENU_SEPARATOR, 2);
                                ui_popmenu_add(&app_menu, "Wrap lines", null, UI_MENU_CHECK | UI_MENU_ON, 3);
                                ui_popmenu_add(&app_menu, "Font size", null, UI_MENU_RADIO, 4);
                                ui_popmenu_add(&app_menu, "Open with", null, UI_MENU_SUBMENU, 5);
                                ui_popmenu_size(&settings.ui, &app_menu, H);
                                app_menu.hot = app_group == 1 ? 0 : app_group == 2 ? 5 : -1;
                                if (app_group == 3)
                                {
                                        // A press on the first item, as the menu's press records it: the press, the hot item and the ripple from the press.
                                        int top = ui_dev(&settings.ui, UI_MENU_PAD) + ui_popmenu_start(&app_menu, 0);

                                        ui_press_begin(&app_menu.press, 12, top + 4, 0, app_menu.touch);
                                        app_menu.hot = 0;
                                        ui_ripple_go(&app_menu.ripple, ui_qdiv(ui_q(12), ui_q(s) / 100), ui_qdiv(ui_q(4), ui_q(s) / 100),
                                                     ui_qdiv(ui_q(app_menu.width), ui_q(s) / 100), ui_qdiv(ui_q(popmenu_row(&app_menu)), ui_q(s) / 100), 0);
                                        app_menu.ripple_at = 0;
                                }
                        }
                        else if (which == 5)
                        {
                                // The gallery showcase: the window's table and size, the faces, and gallery.on (its page instead of the settings').
                                settings.own.table = windows_scratch_table(&app_table_scratch);
                                settings.width = W, settings.height = H;
                                settings.ui.scale = s, settings.ui.engine = &app_engine, settings.ui.regular = settings.ui.bold = settings.ui.mono = &app_face;
                                settings.ui.scratch = app_scratch, settings.ui.scratch_width = 2000, settings.ui.opaque = 1, settings.ui.glass = 0;
                                gallery.on = 1;
                        }
                        else if (which == 4)
                        {
                                // Settings: its window, the group chosen, its layout as the window lays it out (the list and the page).
                                gallery.on = 0;
                                settings.own.table = windows_scratch_table(&app_table_scratch);
                                settings.width = W, settings.height = H;
                                settings.ui.scale = s, settings.ui.engine = &app_engine, settings.ui.regular = settings.ui.bold = settings.ui.mono = &app_face;
                                settings.ui.scratch = app_scratch, settings.ui.scratch_width = 2000, settings.ui.opaque = 1, settings.ui.glass = 0;
                                settings.focused = 1;
                                settings.page.chosen = app_group;
                                settings_layout();
                        }
                        else
                        {
                                // The mixer: its window (the strips, the tabs and the message the page gives), its tray popup (-q), or its recording page.
                                memory_zero(&app_mixer, sizeof(app_mixer));
                                mixer.own.table = windows_scratch_table(&app_table_scratch);
                                mixer.width = W, mixer.height = H;
                                mixer.ui.scale = s, mixer.ui.engine = &app_engine, mixer.ui.regular = mixer.ui.bold = &app_face;
                                mixer.ui.scratch = app_scratch, mixer.ui.scratch_width = 2000, mixer.ui.opaque = 1, mixer.ui.glass = 0;
                                mixer.big = &app_mixer, mixer.have = 1, mixer.quick = which == 2, mixer.recording = which == 3, mixer.capture_off = 0;
                                mixer.focused = 1;
                        }

                        // A warm-up paint: a painter keeps state between paints (the mixer's tabs tween and their pip), and the first paint after a
                        // change is not the steady frame; the reference and the recording are both steady.
                        memory_zero(dummy, need * 4);
                        paint(dummy);
                        memory_zero(cpu, need * 4);
                        paint(cpu);

                        memory_zero(&app_scene, sizeof(app_scene));
                        app_scene.ops = ops, app_scene.room = 1u << 15, app_scene.cover = (unsigned char address_to)((p8 address_to)gpu.at + cover_at);
                        app_scene.size = cover_size, app_scene.at = cover_at;
                        if (which == 0)
                                launcher.ui.scene = &app_scene;
                        else if (which == 8)
                                editor.ui.scene = &app_scene;
                        else if (which == 4 || which == 5 || which == 6 || which == 7)
                                settings.ui.scene = &app_scene;
                        else
                                mixer.ui.scene = &app_scene;
                        paint(dummy);
                        launcher.ui.scene = null, mixer.ui.scene = null, settings.ui.scene = null, editor.ui.scene = null;
                        if (app_scene.lost)
                        {
                                string_format(log, "  known diff: app %p group %p (%p%%, %p) set lost (%p ops recorded): %s\n", (positive)which, (positive)app_group, (positive)s, (positive)mode,
                                              (positive)app_scene.count, (positive)(which == 8 ? "a wide (CJK) cell: font_draw on the ground, font_blend, not recorded" : "a painter that is not recorded (R3 or R4)"));
                                continue;
                        }
                        for (positive i = 0; i < (positive)pitch * H; i++)
                                pic[i] = 0x12345678u;
                        if (!gpu_scene(ops_at, app_scene.count, bins_at, pic_at, (positive)pitch, (unsigned int)pitch, H, 0, 0, W, H))
                        {
                                string_format(log, "  FAIL gpu app %p: the scene at %p%% (%p) does not finish: %p ops\n", (positive)which, (positive)s, (positive)mode,
                                              (positive)app_scene.count);
                                check("gpu apps: the scene finishes", false);
                                return;
                        }
                        for (int y = 0; y < H; y++)
                                for (int x = 0; x < W; x++)
                                {
                                        b32 differs = pic[(positive)y * pitch + x] != cpu[(positive)y * pitch + x];

                                        gpu_px(differs, (positive)x, (positive)y, pic[(positive)y * pitch + x], cpu[(positive)y * pitch + x]);
                                        bad += differs;
                                }
                        {
                                positive wrong = gpu_px_end();

                                if (wrong)
                                        string_format(log, "  FAIL gpu app %p %p%% %p", (positive)which, (positive)s, (positive)mode), gpu_px_tail(wrong);
                        }
                        if (which == 8)
                                check("gpu apps: the editor replays bit for bit", bad == 0);
                        else if (which == 7)
                                check("gpu apps: the find bar replays bit for bit", bad == 0);
                        else if (which == 6)
                                check("gpu apps: the popup menu replays bit for bit", bad == 0);
                        else if (which == 5)
                                check("gpu apps: the gallery showcase replays bit for bit", bad == 0);
                        else if (which == 4)
                                check("gpu apps: the settings page replays bit for bit", bad == 0);
                        else if (which == 0)
                                check("gpu apps: the launcher replays bit for bit", bad == 0);
                        else if (which == 1)
                                check("gpu apps: the mixer replays bit for bit", bad == 0);
                        else if (which == 2)
                                check("gpu apps: the mixer's tray popup replays bit for bit", bad == 0);
                        else
                                check("gpu apps: the mixer's recording page replays bit for bit", bad == 0);
                }
        }
        ui_kit_theme(UI_THEME_LIGHT, UI_ACCENT_DAWN, 0);
}

static unsigned int gpu_ctl_want[700 * 420], gpu_ctl_dummy[700 * 420];

static void gpu_kit_recorded_controls(positive pic_at, positive ops_at, positive bins_at)
{
        enum { CW = 240, CH = 160, CP = 700 };
        unsigned int address_to pic = (unsigned int address_to)((p8 address_to)gpu.at + pic_at), address_to ops = (unsigned int address_to)((p8 address_to)gpu.at + ops_at);
        const unsigned int ground = 0xff1b2a3cu;
        const int scales[4] = {100, 150, 200, 300}, sizes[3] = {14, 20, 28};
        static struct ui cpu, rec;
        static struct ui_scene scene;
        struct ui_control control;
        positive bad = 0, arc_pixels = 0;

        // The roles are the theme's: without one every role is transparent and the painters paint nothing (the kit-frames rig sets the light theme too).
        ui_kit_theme(UI_THEME_LIGHT, UI_ACCENT_DAWN, 0);

        // The spinner: its ring, recorded as boxes with a hole, and its arc, recorded as tone rects of coalesced covers.
        for (int k = 0; k < 4; k++)
                for (int s = 0; s < 3; s++)
                {
                        for (positive i = 0; i < (positive)CP * CH; i++)
                                gpu_ctl_want[i] = ground;
                        memory_zero(&cpu, sizeof(cpu));
                        cpu.pixels = gpu_ctl_want, cpu.pitch = CP, cpu.width = CW, cpu.height = CH, cpu.top = 0, cpu.bottom = CH, cpu.scale = scales[k];
                        memory_zero(&control, sizeof(control));
                        control.x = 20, control.y = 20, control.w = sizes[s];
                        ui_spinner_draw(&cpu, &control);
                        for (positive i = 0; i < (positive)CP * CH; i++)
                                arc_pixels += gpu_ctl_want[i] != ground;

                        memory_zero(&rec, sizeof(rec));
                        rec.pixels = gpu_ctl_dummy, rec.pitch = CP, rec.width = CW, rec.height = CH, rec.top = 0, rec.bottom = CH, rec.scale = scales[k];
                        memory_zero(&scene, sizeof(scene));
                        scene.ops = ops + 8, scene.room = 4096, scene.cover = (unsigned char address_to)((p8 address_to)gpu.at + ops_at + 8192), scene.size = 4096;
                        rec.scene = &scene;
                        ops[0] = 0, ops[1] = 0, ops[2] = 0, ops[3] = CW, ops[4] = CH, ops[5] = ground, ops[6] = ops[7] = 0;
                        control.x = 20, control.y = 20, control.w = sizes[s];
                        ui_spinner_draw(&rec, &control);
                        for (positive i = 0; i < (positive)CP * CH; i++)
                                pic[i] = 0x12345678u;
                        if (!gpu_scene_taken(gpu_scene(ops_at, 1 + scene.count, bins_at, pic_at, CP, CW, CH, 0, 0, CW, CH), "gpu controls: the spinner's scene finishes"))
                                return;
                        for (int y = 0; y < CH; y++)
                                for (int x = 0; x < CW; x++)
                                {
                                        b32 differs = pic[(positive)y * CP + x] != gpu_ctl_want[(positive)y * CP + x];

                                        gpu_px(differs, (positive)x, (positive)y, pic[(positive)y * CP + x], gpu_ctl_want[(positive)y * CP + x]);
                                        bad += differs;
                                }
                        {
                                positive wrong = gpu_px_end();

                                if (wrong)
                                        string_format(log, "  FAIL gpu spinner scale %p size %p", (positive)scales[k], (positive)sizes[s]), gpu_px_tail(wrong);
                        }
                }
        check("gpu controls: the spinner's arc and ring replay bit for bit at 100, 150, 200 and 300 percent", bad == 0);
        string_format(log, "  spinner pixels painted by the processor over 12 spinners: %p\n", (positive)arc_pixels);
        check("gpu controls: the spinner paints an arc to compare (its pixels are not all the ground)", arc_pixels > 1000);
}

/*
        The terminal's frames on the GPU: the cells the terminal draws into its window's list (terminal_list_draw, the kit's recorder), the list
        drawn by the card (compositor_gpu_list_render_bytes, the list's cut and its cover bytes) into a picture of its own, and the picture held to
        the processor's full draw of the same cells (render_reference) bit for bit, the bar included. Plain text, 256 and true colours, the
        attributes, the cursor's three shapes, a selected range, light and dark, the scales 100, 150, 200 and 300, and the largest grid the
        window holds. A frame the recorder lost (a wide character) is named, and not compared.
*/
#define TG_LIST_BYTES (4ul << 20)
#define TG_PITCH 4096
#define TG_ROWS 2048
static unsigned char address_to tg_list;
static unsigned int address_to tg_picture, address_to tg_cpu;
static struct ui_draw_list tg_draw;
static b32 tg_fonts;
static positive tg_cases;
static b32 tg_handles; // the next case draws its selection's handles (the overlay) as well

static void tg_check(const char address_to label, b32 okay)
{
        checks++;
        if (!okay)
        {
                failures++;
                string_format(log, "  FAIL %s\n", (positive)label);
        }
}

static b32 tg_memory(void)
{
        if (!tg_list)
        {
                long lfd = system_call_2(syscall(memfd_create), (positive) "mw-terminal-list", 1);
                long pfd = system_call_2(syscall(memfd_create), (positive) "mw-terminal-picture", 1);
                long lm = -1, pm = -1;
                positive picture_bytes = (positive)TG_PITCH * TG_ROWS * 4;

                if (lfd >= 0 && system_call_2(syscall(ftruncate), (positive)lfd, TG_LIST_BYTES) == 0)
                        lm = system_call_6(syscall(mmap), 0, TG_LIST_BYTES, 3, 1, (positive)lfd, 0);
                if (pfd >= 0 && system_call_2(syscall(ftruncate), (positive)pfd, picture_bytes) == 0)
                        pm = system_call_6(syscall(mmap), 0, picture_bytes, 3, 1, (positive)pfd, 0);
                if (!(lm < 0 && lm > -4096) && !(pm < 0 && pm > -4096))
                        tg_list = (unsigned char address_to)lm, tg_picture = (unsigned int address_to)pm;
        }
        if (!tg_cpu)
                tg_cpu = render_map((positive)2048 * 1280 * 4);
        compositor_gpu_state.up = 1, compositor_gpu_state.dead = 0, compositor_gpu = 1;
        compositor_gscene.ops = (unsigned int address_to)((p8 address_to)gpu.at + COMPOSITOR_GPU_OPS_AT), compositor_gscene.room = (1 << 20) / 32;
        return tg_list && tg_picture && tg_cpu;
}

// One frame: the window's grid (columns by rows, the text in it), at the scale's percent, light or dark, with an optional selection. The
// processor's full draw of its cells and the card's draw of the terminal's list are compared over the picture.
static void tg_case(const char address_to label, unsigned int columns, unsigned int rows, const char address_to text, b32 light, int percent,
                    b32 selected, unsigned int from_line, unsigned int from_column, unsigned int to_line, unsigned int to_column, b32 expect_lost)
{
        struct window_cell address_to now;
        unsigned int first, shown, total, bad = 0;
        int damage[4] = {0, 0, 0, 0}, drawn;

        ui_kit_theme(light ? UI_THEME_LIGHT : UI_THEME_DARK, UI_ACCENT_DAWN, 1);
        terminal.ui.scale = percent;
        cursor_hide();
        terminal_text_set(-1, 16 * (unsigned int)percent / 100);
        window->columns = columns;
        window->rows = rows;
        regrid(-1);
        full_reset();
        terminal_view_clamp();
        terminal_view_live();
        render_surface();
        term_bytes((const p8 address_to)text, string_length((string_address)text));
        to_shell_length = 0;
        cursor_show();
        if (selected)
                terminal_select_set(terminal_place(from_line, from_column), terminal_place(to_line, to_column));
        else
                terminal_select_clear();
        // The handles at the selection's ends (the bar is not held to the rows: its fade is the clock's), as the processor draws them over the grid.
        terminal.sel.handles = selected && tg_handles;

        now = terminal.frame[2];
        terminal_view_clamp();
        terminal_build(now);
        terminal_caret_frame();
        terminal_extent(&first, &shown, &total);
        render_reference(tg_cpu, now);
        if (terminal_overlay_wanted())
                terminal_overlay(tg_cpu, 0);

        ui_draw_list_init(&tg_draw, tg_list, TG_LIST_BYTES, WINDOW_LIST_ROOM);
        if (!ui_scene_begin(&terminal.ui, &tg_draw))
        {
                tg_check(label, false);
                return;
        }
        terminal_list_draw(now, first, shown, total);
        if (tg_draw.scene.lost || !ui_scene_end(&terminal.ui, &tg_draw))
        {
                string_format(log, "  %p: the recorder lost the frame (%p ops), the processor draws it\n", (positive)label, (positive)tg_draw.scene.count);
                tg_check(label, expect_lost);
                return;
        }
        tg_check(label, !expect_lost);
        tg_cases++;
        {
                // The frame's pool: the coverage copies and the tone sets its dedupe tables hold, and the bytes they take.
                positive covers = 0, tones = 0;

                for (unsigned int k = 0; k < UI_SCENE_SLOTS; k++)
                        covers += tg_draw.scene.cover_slots[k].used, tones += tg_draw.scene.tone_slots[k].used;
                string_format(log, "  terminal frame pool: %p ops, %p coverage copies, %p tone sets, %p bytes\n", (positive)tg_draw.scene.count,
                              covers, tones, (positive)tg_draw.scene.used);
        }
        for (positive i = 0; i < (positive)TG_PITCH * TG_ROWS; i++)
                tg_picture[i] = 0x12345678u;
        drawn = compositor_gpu_list_render_bytes(5, tg_list, TG_LIST_BYTES, tg_picture, TG_PITCH, (int)terminal.width, (int)terminal.height, damage);
        tg_check(label, drawn == 1);
        for (unsigned int y = 0; y < terminal.height; y++)
                for (unsigned int x = 0; x < terminal.width; x++)
                {
                        b32 differs = tg_picture[(positive)y * TG_PITCH + x] != tg_cpu[(positive)y * terminal.pitch + x];

                        gpu_px(differs, x, y, tg_picture[(positive)y * TG_PITCH + x], tg_cpu[(positive)y * terminal.pitch + x]);
                        bad += differs;
                }
        {
                positive wrong = gpu_px_end();

                if (wrong)
                        string_format(log, "  FAIL %s", (positive)label), gpu_px_tail(wrong);
        }
        gpu_trim();
}

static void terminal_gpu_rows(string_address font, string_address bold)
{
        static const char plain[] = "The quick brown fox jumps over the lazy dog 0123456789\r\nSecond line: abc DEF ghi\r\n\r\n$ ls -la\r\n";
        static const char colours[] = "\x1b[38;5;196mred256 \x1b[38;5;46mgreen \x1b[38;5;27mblue\x1b[0m \x1b[38;2;10;200;90mtrue\x1b[0m\r\n"
                                      "\x1b[48;5;21mbg21 \x1b[0m\x1b[48;2;200;100;10mbgtrue \x1b[0m\x1b[38;2;255;255;0m\x1b[48;2;0;0;80myel\x1b[0m\r\n";
        static const char styles[] = "\x1b[1mbold\x1b[0m \x1b[2mdim\x1b[0m \x1b[4munder\x1b[0m \x1b[7minverse\x1b[0m \x1b[9mstrike\x1b[0m \x1b[3mitalic\x1b[0m\r\n"
                                     "\x1b[1;4;9mcombo\x1b[0m \x1b[7;1mrev bold\x1b[0m \x1b[2;4mdim under\x1b[0m\r\n";
        static const char cursor_block[] = "block cursor here \x1b[2 q";
        static const char cursor_bar[] = "bar cursor here\x1b[6 q";
        static const char cursor_under[] = "underline cursor\x1b[4 q";
        static const char wide[] = "wide \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e text\r\n";
        static const char tall[] = "row 0 0123456789 abcdefghij ABCDEFGHIJ\r\n";
        static char long_text[80 * 50 * 2];
        unsigned int at = 0;

        if (!tg_fonts)
        {
                string_address paths[1] = {font};

                if (!terminal_fonts(paths, 1, bold, 16))
                {
                        tg_check("gpu terminal: the faces load", false);
                        return;
                }
                tg_fonts = true;
        }
        for (unsigned int k = 0; k < 256; k++)
                terminal_colour[k] = window_cell_colour(k);
        term_palette = terminal_palette;
        if (!window)
                window = window_grid_open(30, 8, 80, 256);
        if (!window || !tg_memory())
        {
                tg_check("gpu terminal: the window and its memory", false);
                return;
        }
        // A window and its picture first, as the terminal rig's start has them: a text size is set into a grid that has its window.
        window->columns = 50, window->rows = 14;
        regrid(-1);
        render_surface();
        // The largest grid: 80 columns by 50 rows, each row not like the others.
        for (unsigned int line = 0; line < 50; line++)
        {
                for (unsigned int col = 0; col < 79; col++)
                        long_text[at++] = (char)('a' + (line * 7 + col * 3) % 26);
                long_text[at++] = '\r', long_text[at++] = '\n';
        }
        long_text[at] = 0;

        tg_case("gpu terminal: plain text, light, 100 percent", 50, 14, plain, 1, 100, 0, 0, 0, 0, 0, 0);
        tg_case("gpu terminal: plain text, dark, 100 percent", 50, 14, plain, 0, 100, 0, 0, 0, 0, 0, 0);
        tg_case("gpu terminal: 256 and true colours, light", 50, 14, colours, 1, 100, 0, 0, 0, 0, 0, 0);
        tg_case("gpu terminal: 256 and true colours, dark", 50, 14, colours, 0, 100, 0, 0, 0, 0, 0, 0);
        tg_case("gpu terminal: bold, dim, underline, inverse, strike, italic", 50, 14, styles, 1, 100, 0, 0, 0, 0, 0, 0);
        tg_case("gpu terminal: the same styles, dark", 50, 14, styles, 0, 100, 0, 0, 0, 0, 0, 0);
        tg_case("gpu terminal: block cursor", 50, 14, cursor_block, 1, 100, 0, 0, 0, 0, 0, 0);
        tg_case("gpu terminal: bar cursor", 50, 14, cursor_bar, 0, 100, 0, 0, 0, 0, 0, 0);
        tg_case("gpu terminal: underline cursor", 50, 14, cursor_under, 1, 100, 0, 0, 0, 0, 0, 0);
        tg_case("gpu terminal: a selected range", 50, 14, plain, 1, 100, 1, 0, 2, 1, 6, 0);
        tg_case("gpu terminal: a selected range, dark", 50, 14, colours, 0, 100, 1, 0, 1, 1, 9, 0);
        tg_handles = true;
        tg_case("gpu terminal: a selected range with its handles", 50, 14, plain, 1, 100, 1, 0, 2, 1, 6, 0);
        tg_case("gpu terminal: a selected range with its handles, dark", 50, 14, colours, 0, 100, 1, 0, 1, 2, 9, 0);
        tg_handles = false;
        tg_case("gpu terminal: 150 percent", 50, 14, plain, 1, 150, 0, 0, 0, 0, 0, 0);
        tg_case("gpu terminal: 200 percent, dark", 50, 14, styles, 0, 200, 0, 0, 0, 0, 0, 0);
        tg_case("gpu terminal: 300 percent", 40, 8, tall, 1, 300, 0, 0, 0, 0, 0, 0);
        tg_case("gpu terminal: the largest grid, 80 by 50", 80, 50, long_text, 1, 100, 0, 0, 0, 0, 0, 0);
        tg_case("gpu terminal: a wide character is recorded and drawn by the card", 50, 14, wide, 1, 100, 0, 0, 0, 0, 0, 0);
        terminal_select_clear();
        terminal_text_set(-1, 16);
        window->columns = 50, window->rows = 14, regrid(-1), render_surface();
        string_format(log, "  gpu terminal: %p frames replayed on the card\n", (positive)tg_cases);
}

/*
        The dma-buf route (gpu_map_as's fallback for a refused userptr): with the kernel's userptr refused (ENODEV, then the flag that says it is gone), a picture of
        the window table's memfd is mapped by udmabuf, PRIME and GEM_VA; the card writes into it and the processor reads the memfd back exactly; the pin of its
        page goes on, and off when the map is let go of; the refusals say which step failed (5 not the table's pages, 6 udmabuf with its error).
*/
// The process's resident memory in KiB (VmRSS of /proc/self/status), 0 when it cannot be read.
static long gpu_rss_kib(void)
{
        static char text[4096];
        long handle = system_open_at(AT_FDCWD, (string_address) "/proc/self/status", FILE_READ | O_CLOEXEC), got = 0;
        char *at;

        if (handle < 0)
                return 0;
        got = system_read_once((int)handle, text, sizeof(text) - 1);
        system_close((int)handle);
        if (got <= 0)
                return 0;
        text[got] = 0;
        at = (char *)string_find((string_address)text, (string_address) "VmRSS:");
        return at ? (long)string_to_number((string_address)(at + 6), null, 10) : 0;
}
static int gpu_pin_count;
static long gpu_pin_sum;
static positive gpu_pin_offset;
static void gpu_fake_pin(positive offset, b32 on)
{
        gpu_pin_count++, gpu_pin_sum += on ? 1 : -1, gpu_pin_offset = offset;
}
static b32 gpu_route_rows(void)
{
        enum { BYTES = 1 << 20, W = 64 };
        long fd = system_call_2(syscall(memfd_create), (positive) "moonwater-gpu-route", 1 | 2);
        unsigned int address_to mem, address_to pic;
        positive base = gpu.size - (1 << 19), ops_at = base, bins_at = base + 4096, dst;
        unsigned int address_to ops = (unsigned int address_to)((p8 address_to)gpu.at + ops_at);
        long mapped, other, probe = system_open_at(AT_FDCWD, (string_address) "/dev/udmabuf", FILE_READ_WRITE | O_CLOEXEC);
        b32 kept_userptr_ok;

        // The route is udmabuf and PRIME: a user the device refuses (the box's dawn, EACCES; root opens it) cannot run it, and says so.
        if (probe < 0)
        {
                string_format(log, "  gpu route: /dev/udmabuf does not open for this user: the route rows are not run\n");
                if (fd >= 0)
                        system_close((int)fd);
                return true;
        }
        system_close((int)probe);
        if (fd < 0)
        {
                check("gpu route: a memfd for the table's pages", false);
                return false;
        }
        system_truncate_handle((int)fd, BYTES);
        system_call_3(syscall(fcntl), (positive)fd, 1033, 2); // F_ADD_SEALS, F_SEAL_SHRINK, as the table is sealed
        mapped = system_call_6(syscall(mmap), 0, BYTES, 3, 1, (positive)fd, 0);
        other = system_call_6(syscall(mmap), 0, 4096, 3, 0x4022, (positive)-1, 0);
        if ((mapped < 0 && mapped > -4096) || (other < 0 && other > -4096))
        {
                check("gpu route: memory for the rows", false);
                return false;
        }
        mem = (unsigned int address_to)mapped, pic = mem + 1024; // the picture at byte 4096 of the table's memfd
        for (positive i = 0; i < BYTES / 4; i++)
                mem[i] = 0xaaaaaaaau;
        gpu_shared.fd = (int)fd, gpu_shared.at = (unsigned char address_to)mem, gpu_shared.bytes = BYTES, gpu_shared.pin = gpu_fake_pin;
        gpu_userptr_off = 0, gpu_userptr_gone = 0, gpu_maps_dma = 0, gpu_pin_count = 0, gpu_pin_sum = 0;

        // No map of the rows before holds these pages: a live map that does is the answer (gpu_map_hit, 3b2e4a1bb) and no route is asked.
        while (gpu_map_count)
                gpu_drop(0, GPU_GONE_RECOVER);
        // The kernel without CONFIG_DRM_AMDGPU_USERPTR: userptr answers ENODEV, and the start's probe took no page by it (gpu_userptr_ok 0; with it, an ENODEV
        // after would be the card lost).
        gpu_fake_userptr = 19, kept_userptr_ok = gpu_userptr_ok, gpu_userptr_ok = 0;
        check("gpu route: the table's pages (a sealed memfd) map by the dma-buf route first, and userptr is not asked for them",
              gpu_map_written(pic, W * W * 4) && !gpu_userptr_gone && gpu_maps_dma == 1);
        check("gpu route: the map is the table's pages (the pin goes on for their offset)", gpu_pin_count == 1 && gpu_pin_sum == 1 && gpu_pin_offset == 4096);
        dst = (positive)gpu_va_written(pic, W * W * 4);
        check("gpu route: the card's address for the picture", dst != 0);
        // The card writes the picture: a fill of the whole of it, then the processor reads the memfd. Not without the address: a scene with no
        // destination would write at the start of the card's buffer.
        if (dst)
        {
                unsigned int q[8] = {0, 0, 0, W, W, 0xff112233u, 0, 0};

                memory_copy(ops, q, 32);
                check("gpu route: the card writes a picture the table's memfd holds", gpu_scene(ops_at, 1, bins_at, dst, W, W, W, 0, 0, W, W));
                for (unsigned int i = 0; i < W * W; i++)
                        if (pic[i] != 0xff112233u)
                                break;
                check("gpu route: the processor reads the card's pixels back from the memfd", pic[0] == 0xff112233u && pic[W * W - 1] == 0xff112233u);
                check("gpu route: the bytes past the picture are not the card's", mem[1024 + W * W] == 0xaaaaaaaau);
        }
        // Another memory of the process is not the table's: no route, and the reason says so. A refused userptr is then refused (5), not mapped.
        check("gpu route: pages outside the table's memfd ask userptr, which ENODEV gives up for the session, and are refused with no route (5)",
              !gpu_map_written((void *)other, 4096) && gpu_userptr_gone && !gpu_lost && (gpu_map_why & 255) == 5);
        // The table's pages past the memfd's end: udmabuf refuses them (6, with EINVAL).
        gpu_shared.bytes = BYTES + 8192;
        check("gpu route: udmabuf refuses pages past the memfd's end (6, EINVAL)", !gpu_map_written(mem + BYTES / 4, 4096) && (gpu_map_why & 255) == 6 && (gpu_map_why >> 8) == 22);
        gpu_shared.bytes = BYTES;
        // What the route costs in resident memory: twelve pictures of 64 KiB as dma-buf maps of memfd pages that are already resident (the fill above touched them).
        {
                long before = gpu_rss_kib(), after;

                for (unsigned int k = 0; k < 12; k++)
                        if (!gpu_map_written(mem + 16384 * k, 65536))
                                check("gpu route: twelve pictures of 64 KiB map by dma-buf", false);
                after = gpu_rss_kib();
                string_format(log, "gpu route: twelve 64 KiB pictures by dma-buf: resident memory %p KiB more (before %p, after %p; %p maps by dma-buf)\n",
                              (positive)(after - before), (positive)before, (positive)after, (positive)gpu_maps_dma);
        }
        // Let go of the maps: their pins come off, and the row's count is back to nothing.
        while (gpu_map_count)
                gpu_drop(0, GPU_GONE_RECOVER);
        check("gpu route: the pins are let go of when the maps are", gpu_pin_sum == 0);
        gpu_fake_userptr = 0, gpu_userptr_gone = 0, gpu_userptr_off = 0, gpu_userptr_ok = kept_userptr_ok;
        gpu_shared.fd = -1, gpu_shared.at = 0, gpu_shared.bytes = 0, gpu_shared.pin = 0;
        system_call_2(syscall(munmap), (positive)mapped, BYTES);
        system_call_2(syscall(munmap), (positive)other, 4096);
        system_close((int)fd);
        return true;
}

/*
        The i915 row with no card under it (the fake i915 node, gpu_kernel.i915, and gpu.c's calls of it): its chips by device id, a start on an i915 node that
        stops at the chip's step (no kernel set yet: the processor draws) and is settled, a node of a driver with no path, the late look that does not start a
        settled card again while the nodes are the same and does when they change, the outcome said once, the notices' words, the address windows, userptr's
        read-only rule, the submit's object list and its errors as the classifier takes them, the waits and the fence, and amdgpu's window and classifier as
        they were. Everything the rows change of gpu.c's and the compositor's state is put back.
*/
static unsigned int gpu_i915_test_words;
static unsigned int gpu_i915_test_batch(const struct gpu_job *job, unsigned int address_to words, unsigned int room)
{
        (void)job;
        if (room < 1 || !gpu_i915_test_words)
                return 0;
        words[0] = 0x05000000u; // MI_BATCH_BUFFER_END (one word: the call pads it to a quadword)
        return gpu_i915_test_words;
}
static const struct gpu_set gpu_i915_test_set = {gpu_kernels, 0, 0, 0, gpu_i915_test_batch};

static b32 gpu_i915_has(const char *text, const char *word)
{
        return string_find((string_address)text, (string_address)word) != 0;
}

static void gpu_i915_rows(void)
{
        static unsigned char kept_kernel[sizeof(gpu_kernel)];
        static unsigned char buffer[65536] __attribute__((aligned(4096)));
        unsigned char kept_gpu[sizeof(gpu)], kept_req[sizeof(gpu_req)], kept_state[sizeof(compositor_gpu_state)], kept_i915[sizeof(gpu_i915)];
        unsigned char kept_window[sizeof(gpu_window)], kept_layout[sizeof(gpu_layout)];
        const struct gpu_backend *kept_backend = gpu_backend;
        const struct gpu_set *kept_set = gpu_set;
        positive kept_next = gpu_map_next, kept_va_max = gpu_amd_va_max, va = 0, maps = 0, top = 0, nodes, sequence = 0;
        unsigned int family = 0, device = 0, made = 0, handle = 0, identified, kept_ours = gpu_jobs_ours, kept_error = gpu_last_error;
        int kept_on = compositor_gpu, fd = -1;
        b32 kept_lost = gpu_lost;
        char words[256];
        struct gpu_i915_object objects[8];
        struct gpu_i915_exec run;

        memory_copy(kept_kernel, &gpu_kernel, sizeof(gpu_kernel)), memory_copy(kept_gpu, &gpu, sizeof(gpu)), memory_copy(kept_req, &gpu_req, sizeof(gpu_req));
        memory_copy(kept_state, &compositor_gpu_state, sizeof(compositor_gpu_state)), memory_copy(kept_i915, &gpu_i915, sizeof(gpu_i915));
        memory_copy(kept_window, &gpu_window, sizeof(gpu_window)), memory_copy(kept_layout, &gpu_layout, sizeof(gpu_layout));
        // (The start's stop lets go of the card's buffer and maps: none are the rows' before them, so none of the earlier rows' is let go of.)
        unsigned int kept_maps = gpu_map_count, kept_faults = gpu_faults, kept_job_error = gpu_job_error;
        positive kept_fault_next = gpu_fault_next;
        char kept_fault[sizeof(gpu_fault)];
        struct windows *kept_table = compositor.table;
        int kept_card = compositor.card;
        unsigned char kept_shared[sizeof(gpu_shared)];

        memory_copy(kept_fault, gpu_fault, sizeof(gpu_fault)), memory_copy(kept_shared, &gpu_shared, sizeof(gpu_shared));
        gpu.at = 0, gpu.fd = -1, gpu.size = 0, gpu.imports = 0, gpu.pending = 0, gpu_map_count = 0, compositor.table = 0, compositor.card = -1;
        memory_zero(&gpu_kernel, sizeof(gpu_kernel));
        gpu_kernel.on = 1, gpu_kernel.i915_chip = 0x3e92, gpu_kernel.i915_ppgtt = 2, gpu_kernel.i915_gtt = 1ull << 48;
        string_copy_bounded((string_address)gpu_kernel.i915, (string_address) "i915", sizeof(gpu_kernel.i915));

        // The chips: the families by the kernel's device tables, 0 for one before Ivy Bridge.
        {
                static const unsigned int ids[][2] = {{0x0152, 700}, {0x0F31, 701}, {0x041E, 750}, {0x1616, 800}, {0x22B0, 801}, {0x3E92, 900}, {0x3185, 901},
                                                      {0x8A52, 1100}, {0x4E61, 1101}, {0x9A49, 1200}, {0xA7A0, 1200}, {0x0102, 0}, {0x56A0, 1255}, {0x7D55, 1270}};
                b32 all = true;

                for (unsigned int i = 0; i < sizeof(ids) / sizeof(ids[0]); i++)
                {
                        gpu_kernel.i915_chip = ids[i][0];
                        all &= gpu_i915_identify(GPU_KERNEL_I915, &family, &device) == 0 && family == ids[i][1] && device == ids[i][0];
                }
                check("gpu i915: CHIPSET_ID's device is the chip's family (Ivy Bridge 700 through Meteor Lake 1270; Sandy Bridge 0)", all);
                gpu_kernel.i915_chip = 0x9a49;
                check("gpu i915: Gen7-Gen11 have registered kernel sets; Gen12+ not yet",
                      gpu_i915_set(700) && gpu_i915_set(750) && gpu_i915_set(800) && gpu_i915_set(900) && gpu_i915_set(1100) && !gpu_i915_set(1200));
        }
        // A start on the i915 node: the driver is known, the chip (Gen12 0x9a49) has no kernels yet, the start stops at the chip's step with nothing of a card made, and it is settled.
        identified = gpu_kernel.i915_identifies;
        check("gpu i915: a start on an i915 node stops at the chip's step, with the driver and the chip kept",
              !gpu_start(1 << 20) && gpu_req.first == GPU_REQ_FAMILY && (gpu_req.mask & (1u << GPU_REQ_DRIVER)) && gpu_req.family == 1200 && gpu_req.device == 0x9a49 &&
                  string_equals((string_address)gpu_req.name, (string_address) "i915"));
        check("gpu i915: and nothing of a card was made (no buffer, no context), the descriptor closed", gpu_kernel.i915_creates == 0 && gpu_kernel.i915_contexts == 0 && gpu.fd == -1 &&
              gpu_kernel.i915_identifies == identified + 1);
        check("gpu i915: a chip with no kernel set is settled, and the nodes it saw are kept", gpu_req.settled && gpu_req.nodes != 0 && gpu_req.nodes == gpu_nodes_seen());
        check("gpu i915: the notice's words name the driver and the chip",
              gpu_req_words(words, sizeof(words)) && gpu_i915_has(words, "i915 Gen12 Tiger Lake to Raptor Lake") && gpu_i915_has(words, "0x9a49"));
        nodes = gpu_req.nodes;
        string_copy_bounded((string_address)gpu_kernel.i915, (string_address) "virtio_gpu", sizeof(gpu_kernel.i915));
        check("gpu i915: a driver loaded in the node's place is other nodes", gpu_nodes_seen() != nodes);
        check("gpu i915: a node of a driver with no path stops at the driver's step, settled, and its words name it and its node",
              !gpu_start(1 << 20) && gpu_req.first == GPU_REQ_DRIVER && gpu_req.settled && gpu_req_words(words, sizeof(words)) &&
                  gpu_i915_has(words, "virtio_gpu on /dev/dri/renderD128"));
        string_copy_bounded((string_address)gpu_kernel.i915, (string_address) "i915", sizeof(gpu_kernel.i915));

        // The compositor: the start fails once, is said once and given up as no card, settled; the late look sees the same nodes and starts nothing; the
        // nodes changed, it starts the card again and says the new outcome; the turn of the setting starts it once whatever the nodes.
        {
                positive now = 500000000000ull, said;

                compositor_gpu = 1, compositor_gpu_state.up = 0, compositor_gpu_state.dead = 0, compositor_gpu_state.settled = 0, compositor_gpu_state.said = 0;
                compositor_gpu_state.late_first = compositor_gpu_state.late_next = 0, compositor_gpu_state.hung_off = 0;
                check("gpu i915: the compositor's start fails and the card is given up as no card, settled on the nodes it saw",
                      !compositor_gpu_up() && compositor_gpu_state.dead && compositor_gpu_state.dead_why == COMPOSITOR_GPU_WHY_NO_CARD &&
                          compositor_gpu_state.settled == gpu_req.nodes && compositor_gpu_state.said != 0);
                said = compositor_gpu_state.said, identified = gpu_kernel.i915_identifies;
                compositor_gpu_late_at(now);
                compositor_gpu_late_tick(compositor_gpu_state.late_next);
                compositor_gpu_late_tick(compositor_gpu_state.late_next);
                check("gpu i915: the late look at the same nodes starts nothing (no chip asked, the outcome not said again)",
                      gpu_kernel.i915_identifies == identified && compositor_gpu_state.dead && compositor_gpu_state.said == said && compositor_gpu_late_at(now) != 0);
                compositor_gpu_state.dead = 0;
                check("gpu i915: a start that fails the same way again is not said again", !compositor_gpu_up() && compositor_gpu_state.said == said);
                gpu_kernel.i915_chip = 0x0152, identified = gpu_kernel.i915_identifies; // (the same nodes: the chip is not part of what the look sees)
                compositor_gpu_late_tick(compositor_gpu_state.late_next);
                check("gpu i915: the same nodes with another chip under them are not looked at either", gpu_kernel.i915_identifies == identified);
                gpu_kernel.i915[0] = 0; // the node gone: every node absent (the fake's i915 empty, so the walk sees the machine's own, which the row does not know)
                gpu_fake_absent = 1000;
                compositor_gpu_late_tick(compositor_gpu_state.late_next);
                gpu_fake_absent = 0;
                check("gpu i915: nodes that changed are looked at: the card is started again, and the new outcome said (no node, not settled)",
                      compositor_gpu_state.dead && gpu_req.first == GPU_REQ_NODE && !compositor_gpu_state.settled && compositor_gpu_state.said != said);
                string_copy_bounded((string_address)gpu_kernel.i915, (string_address) "i915", sizeof(gpu_kernel.i915));
                gpu_kernel.i915_chip = 0x3e92;
        }
        // The board and Settings' line: a chip with no kernels is no supported GPU (the processor draws everything), its family named with its driver, and the
        // message of the chip's step says the chip.
        {
                struct windows *table = windows_scratch_table(&app_table_scratch);
                struct windows_issue want[WINDOWS_GPU_CODES], issue;
                unsigned int count;
                char text[512];

                windows_scratch_zero(table);
                table->gpu_on = 1, table->gpu_state = WINDOWS_GPU_NO_CARD, table->gpu_family = 900;
                memory_copy(table->gpu_req_driver, "i915", 5);
                count = windows_gpu_wanted(table, want, WINDOWS_GPU_CODES);
                check("gpu i915: a chip with no kernels is said as no supported GPU on Settings' line", windows_gpu_judge(table, WINDOWS_GPU_STATUS_NOT, 0, 50) == WINDOWS_GPU_STATUS_NONE);
                {
                        b32 named = false;

                        for (unsigned int k = 0; k < count; k++)
                                named |= want[k].code == WINDOWS_ISSUE_GPU_FAMILY && want[k].a == 900 && string_equals((string_address)want[k].detail, (string_address) "i915");
                        check("gpu i915: the board's family warning names the family's driver", named);
                }
                memory_zero(&issue, sizeof(issue));
                issue.code = WINDOWS_ISSUE_GPU_REQUIRES, issue.a = GPU_REQ_FAMILY;
                string_copy_bounded((string_address)issue.detail, (string_address) "i915 Gen7.5 Haswell, device 0x41e", WINDOWS_ISSUE_DETAIL);
                settings_issue_text(&issue, text, sizeof(text));
                check("gpu i915: Settings' message of the chip's step says the chip", gpu_i915_has(text, "Gen7.5 Haswell") && gpu_i915_has(text, "kernels"));
                issue.a = GPU_REQ_DRIVER;
                string_copy_bounded((string_address)issue.detail, (string_address) "nouveau on /dev/dri/renderD128", WINDOWS_ISSUE_DETAIL);
                settings_issue_text(&issue, text, sizeof(text));
                check("gpu i915: and of the driver's step says the driver and its node, and none names amdgpu as the only one", gpu_i915_has(text, "nouveau on /dev/dri/renderD128") &&
                      !gpu_i915_has(text, "not amdgpu"));
                windows_scratch_zero(table);
        }

        // The windows: the whole record's reach where the space is 48 bits and not the display's; Ivy Bridge's 2 GiB shared with the display from 256 MiB, the maps
        // from 1 GiB to the last page; a space under 1.25 GiB refused.
        gpu_i915.gtt = 1ull << 48, gpu_i915.ppgtt = 2;
        check("gpu i915: a 48-bit address space of its own has amdgpu's window", gpu_i915_window(900, &va, &maps, &top) == 0 && va == GPU_AMD_VA && maps == GPU_AMD_VA_MAPS && top == GPU_VA_RECORD);
        gpu_i915.gtt = 1ull << 31, gpu_i915.ppgtt = 1;
        check("gpu i915: Ivy Bridge's 2 GiB shared with the display: the buffer at 256 MiB, the maps from 1 GiB to its last page",
              gpu_i915_window(700, &va, &maps, &top) == 0 && va == 0x10000000ull && maps == 1ull << 30 && top == (1ull << 31) - 4096);
        gpu_i915.gtt = 0;
        check("gpu i915: a space the kernel does not say is the chip's (Cherry View 4 GiB, Elkhart Lake 64 GiB)",
              gpu_i915_window(801, &va, &maps, &top) == 0 && top == (1ull << 32) - 4096 && gpu_i915_window(1101, &va, &maps, &top) == 0 && top == (1ull << 36) - 4096);
        gpu_i915.gtt = 1ull << 30;
        check("gpu i915: a space too small for the path is refused (ENOSPC)", gpu_i915_window(700, &va, &maps, &top) == 28);
        // amdgpu's window as it was where DEV_INFO says nothing or 2^40 and more (RDNA2); an old card's 64 GiB moves it below its top; 2 GiB is refused.
        gpu_amd_va_max = 0;
        check("gpu amd: a space DEV_INFO does not say keeps the window", gpu_amd_window(144, &va, &maps, &top) == 0 && va == GPU_AMD_VA && maps == GPU_AMD_VA_MAPS && top == GPU_VA_RECORD);
        gpu_amd_va_max = (1ull << 47) - (2ull << 20);
        check("gpu amd: RDNA2's 2^47 keeps the window", gpu_amd_window(144, &va, &maps, &top) == 0 && va == GPU_AMD_VA && top == GPU_VA_RECORD);
        gpu_amd_va_max = (64ull << 30) - 0x203000ull;
        check("gpu amd: a 64 GiB space (gmc v6 to v8 on a machine of less than 22 GiB) has the window below its top",
              gpu_amd_window(130, &va, &maps, &top) == 0 && va == 1ull << 30 && maps == 2ull << 30 && top < 64ull << 30 && va < maps && maps < top);
        gpu_amd_va_max = 2ull << 30;
        check("gpu amd: a space under 3 GiB is refused", gpu_amd_window(130, &va, &maps, &top) == 28);
        gpu_amd_va_max = kept_va_max;
        check("gpu amd: the classifier: ENODEV, ECANCELED and EIO the card lost, EINVAL a suspect (lost only the second time in a row), EAGAIN and EINTR asked again, EFAULT refused",
              gpu_amd_classify(22) == GPU_ERR_SUSPECT && gpu_amd_classify(19) == GPU_ERR_LOST && gpu_amd_classify(125) == GPU_ERR_LOST && gpu_amd_classify(11) == GPU_ERR_AGAIN &&
                  gpu_amd_classify(4) == GPU_ERR_AGAIN && gpu_amd_classify(14) == GPU_ERR_REFUSED && gpu_amd_classify(5) == GPU_ERR_LOST);
        check("gpu i915: the classifier: EINVAL, ENOENT and EOPNOTSUPP our own job, ENOSPC a place refused, EIO and ENODEV the card lost, EAGAIN and EINTR asked again, EFAULT refused",
              gpu_i915_classify(22) == GPU_ERR_OURS && gpu_i915_classify(28) == GPU_ERR_PLACE && gpu_i915_classify(2) == GPU_ERR_OURS && gpu_i915_classify(95) == GPU_ERR_OURS &&
                  gpu_i915_classify(5) == GPU_ERR_LOST && gpu_i915_classify(19) == GPU_ERR_LOST && gpu_i915_classify(11) == GPU_ERR_AGAIN && gpu_i915_classify(4) == GPU_ERR_AGAIN &&
                  gpu_i915_classify(14) == GPU_ERR_REFUSED);

        // The calls on an open card (the fake node as the card's descriptor, the 48-bit window).
        gpu_kernel.i915_chip = 0x3e92, gpu_kernel.i915_fence = 0;
        gpu_i915_identify(GPU_KERNEL_I915, &family, &device);
        gpu_backend = &gpu_i915_row, gpu.fd = GPU_KERNEL_I915, gpu.family = 900, gpu.device = 0x3e92;
        gpu_i915_window(900, &gpu_window.va, &gpu_window.maps, &gpu_window.top);
        gpu_layout.packets = gpu_i915_row.packets, gpu_layout.args = GPU_SLOTS * gpu_i915_row.packets, gpu_layout.code = 2560;
        {
                p64 offset = 0;

                check("gpu i915: the card's buffer is made and mapped write-back on a chip with LLC, bound at the window's start",
                      gpu_i915_buffer(sizeof(buffer), GPU_VA, &handle, &offset) == 0 && handle && offset == 0x100000ull * handle && gpu_kernel.i915_mmap_flags == 2 &&
                          gpu_i915.main == handle && gpu_i915.bound[handle].va == GPU_VA && gpu_i915.bound[handle].written);
        }
        check("gpu i915: a context is made not recoverable (RECOVERABLE 0), and the one before it let go of",
              gpu_i915_context(&made, 0) == 0 && made && gpu_kernel.i915_set_ctx == made && gpu_kernel.i915_set_param == 8 && gpu_kernel.i915_set_value == 0 &&
                  gpu_i915_context(&gpu.ctx, made) == 0 && gpu_kernel.i915_destroyed == made && gpu.ctx != made);
        gpu.at = (unsigned int address_to)buffer, gpu.size = sizeof(buffer);
        {
                unsigned int h = 0, calls = gpu_kernel.i915_userptrs;

                gpu_i915.read_only_refused = 0;
                check("gpu i915: a map to read on Gen9 is registered read-only, its range looked at now (READ_ONLY | PROBE)",
                      gpu_i915_user(0x10000000ull, 4096, GPU_USER_READ, &h) == 0 && h && gpu_kernel.i915_userptr_flags == 3);
                check("gpu i915: a written map is not read-only; one taken later is not looked at now",
                      gpu_i915_user(0x10000000ull, 4096, GPU_USER_WRITTEN, &h) == 0 && gpu_kernel.i915_userptr_flags == 2 &&
                          gpu_i915_user(0x10000000ull, 4096, GPU_USER_LATER, &h) == 0 && gpu_kernel.i915_userptr_flags == 0);
                calls = gpu_kernel.i915_userptrs, gpu.family = 750;
                check("gpu i915: a map to read on a chip without read-only entries (Haswell) is refused at once, EPERM, and the kernel not asked",
                      gpu_i915_user(0x10000000ull, 4096, GPU_USER_READ, &h) == 1 && h == 0 && gpu_kernel.i915_userptrs == calls);
                gpu.family = 1200;
                check("gpu i915: and on Gen12", gpu_i915_user(0x10000000ull, 4096, GPU_USER_READ, &h) == 1 && gpu_kernel.i915_userptrs == calls);
                gpu.family = 900, gpu_kernel.i915_userptr_error = 19, gpu_i915.read_only_refused = 0; // another chip: its own card's state
                check("gpu i915: a chip that answers read-only with ENODEV is refused (EPERM) and not asked read-only again",
                      gpu_i915_user(0x10000000ull, 4096, GPU_USER_READ, &h) == 1 && gpu_i915.read_only_refused &&
                          gpu_i915_user(0x10000000ull, 4096, GPU_USER_READ, &h) == 1 && gpu_kernel.i915_userptrs == calls + 1);
                gpu_kernel.i915_userptr_error = 0, gpu_i915.read_only_refused = 0;
        }
        check("gpu i915: a range bound outside the window or not of whole pages is refused (EINVAL), one inside is kept by its handle",
              gpu_i915_bind(40, GPU_VA_END, 4096, false) == 22 && gpu_i915_bind(40, GPU_VA_MAPS + 100, 4096, false) == 22 &&
                  gpu_i915_bind(40, GPU_VA_MAPS, 5000, false) == 0 && gpu_i915.bound[40].bytes == 8192 && !gpu_i915.bound[40].written &&
                  gpu_i915_bind(41, GPU_VA_MAPS + 8192, 4096, true) == 0);
        check("gpu i915: an unbind at another address is refused, at its own it is let go of", gpu_i915_unbind(41, GPU_VA_MAPS, 4096) == 2 &&
              gpu_i915_unbind(41, GPU_VA_MAPS + 8192, 4096) == 0 && !gpu_i915.bound[41].bytes && gpu_i915_bind(41, GPU_VA_MAPS + 8192, 4096, true) == 0);
        // The submit: none without a kernel set; with one, every buffer pinned at its address, the card's own buffer last, the batch at the slot's packets.
        {
                struct gpu_job job = {0, GPU_VA + 2560, GPU_VA + 2048, 1, 1, (unsigned int address_to)(buffer + 1024), GPU_VA + 1024};
                p64 bos[2] = {40, 41};
                unsigned int execs = gpu_kernel.i915_execs;

                gpu_set = 0;
                check("gpu i915: a submit with no kernel set is refused as our own job (EOPNOTSUPP) and the kernel is not asked",
                      gpu_i915_submit(&job, bos, 2, &sequence) == 95 && sequence == 0 && gpu_kernel.i915_execs == execs);
                gpu_set = &gpu_i915_test_set, gpu_i915_test_words = 1;
                check("gpu i915: with a set, the batch is submitted: its length padded to a quadword, at the slot's packets, on the render ring with NO_RELOC, in the context",
                      gpu_i915_submit(&job, bos, 2, &sequence) == 0 && sequence == gpu_i915.sequence && gpu_kernel.i915_execs == execs + 1 &&
                          (memory_copy(&run, gpu_kernel.i915_exec, 64), run.buffer_count == 3 && run.batch_start_offset == 1024 && run.batch_len == 8 &&
                                                                         run.flags == GPU_I915_RUN && run.rsvd1 == gpu.ctx) &&
                          buffer[1024 + 4] == 0);
                memory_copy(objects, gpu_kernel.i915_objects, 3 * sizeof(objects[0]));
                check("gpu i915: every buffer pinned at the address the table gave it, written only where bound written, the card's own buffer last",
                      objects[0].handle == 40 && objects[0].offset == GPU_VA_MAPS && objects[0].flags == (GPU_I915_PINNED | GPU_I915_WIDE) &&
                          objects[1].handle == 41 && objects[1].offset == GPU_VA_MAPS + 8192 && objects[1].flags == (GPU_I915_PINNED | GPU_I915_WIDE | GPU_I915_WRITTEN) &&
                          objects[2].handle == handle && objects[2].offset == GPU_VA && (objects[2].flags & GPU_I915_WRITTEN) && !objects[2].relocation_count);
                bos[1] = 42;
                check("gpu i915: a buffer the table never bound is refused as our own (ENOENT), not sent", gpu_i915_submit(&job, bos, 2, &sequence) == 2 &&
                      gpu_kernel.i915_execs == execs + 1);
                bos[1] = 41;
                gpu_i915_test_words = GPU_PACKETS / 4 + 2;
                check("gpu i915: a batch past its slot's room is refused as our own (EINVAL), not sent", gpu_i915_submit(&job, bos, 2, &sequence) == 22 &&
                      gpu_kernel.i915_execs == execs + 1);
                gpu_i915_test_words = 1;
        }
        // The waits: GEM_WAIT on the card's own buffer, what is left to the deadline (0 a look), ETIME still running; done, the context's lost batches looked at.
        {
                positive first = gpu_i915.sequence;

                gpu_kernel.i915_wait_error = 62;
                check("gpu i915: a look at a running job is a GEM_WAIT of 0 that answers ETIME: still running",
                      gpu_i915_wait(first, 0) == 0 && gpu_kernel.i915_wait_timeout == 0);
                check("gpu i915: a wait to a deadline asks for what is left of it", gpu_i915_wait(first, clock_monotonic_nanoseconds() + 1000000000ull) == 0 &&
                      gpu_kernel.i915_wait_timeout > 900000000ll && gpu_kernel.i915_wait_timeout <= 1000000000ll);
                gpu_kernel.i915_wait_error = 0;
                check("gpu i915: the buffer idle, the job is done, and every job before", gpu_i915_wait(first, 0) == 1 && gpu_i915.done == first && gpu_i915_wait(first - 1, 0) == 1);
                gpu_kernel.i915_wait_error = 5;
                gpu_i915_submit(&(struct gpu_job){0, GPU_VA + 2560, GPU_VA + 2048, 1, 1, (unsigned int address_to)(buffer + 1024), GPU_VA + 1024}, 0, 0, &sequence);
                check("gpu i915: a wait the kernel refuses (not ETIME) is a wait that failed", gpu_i915_wait(sequence, 0) == -1);
                gpu_kernel.i915_wait_error = 0, gpu_kernel.i915_lost = 1;
                check("gpu i915: a job done after a reset took a batch of the context is a wait that failed, not a job done", gpu_i915_wait(sequence, 0) == -1 && gpu_i915.lost == 1);
                check("gpu i915: no fence where the kernel has no EXEC_FENCE: the caller waits on the sequence", gpu_i915_fence(sequence, &fd) == 95 && fd == -1);
                // With EXEC_FENCE: the job's sync_file, polled (one job's end), and a descriptor of its own for the loop.
                gpu_i915.fence = 1;
                gpu_i915_submit(&(struct gpu_job){0, GPU_VA + 2560, GPU_VA + 2048, 1, 1, (unsigned int address_to)(buffer + 1024), GPU_VA + 1024}, 0, 0, &sequence);
                memory_copy(&run, gpu_kernel.i915_exec, 64);
                check("gpu i915: with EXEC_FENCE the submit asks for the job's sync_file (FENCE_OUT, by the _WR request)", (run.flags & GPU_I915_FENCE_OUT) && gpu_kernel.i915_fence_fd > 0);
                check("gpu i915: the fence is a descriptor of its own", gpu_i915_fence(sequence, &fd) == 0 && fd >= 0 && fd != gpu_kernel.i915_fence_fd);
                if (fd >= 0)
                        system_close(fd), fd = -1;
                gpu_kernel.i915_wait_error = 62;
                check("gpu i915: a job whose sync_file is not readable is still running (GEM_WAIT not asked)",
                      (gpu_kernel.i915_waits = 0, gpu_i915_wait(sequence, 0)) == 0 && gpu_kernel.i915_waits == 0);
                {
                        positive one = 1;

                        system_write_once(gpu_kernel.i915_fence_fd, &one, 8);
                }
                check("gpu i915: readable, it is done, its sync_file let go of", gpu_i915_wait(sequence, 0) == 1 && gpu_i915.done == sequence &&
                      !gpu_i915.jobs[sequence % GPU_I915_JOBS].fence);
                check("gpu i915: a job seen done has no fence left to poll (EALREADY)", gpu_i915_fence(sequence, &fd) == 114);
                gpu_kernel.i915_wait_error = 0, gpu_i915.fence = 0;
        }
        // The core's submit on the i915 row: EINVAL is our own job (one call, not asked again, the card not lost, counted); EIO is a lost card.
        {
                unsigned int execs, ours = gpu_jobs_ours;

                gpu_slot = 0, gpu.pending = 0, gpu_before.sequence = 0, gpu.imports = 0, gpu_map_count = 0, gpu_lost = 0;
                memory_zero(GPU_ARGS, sizeof(*GPU_ARGS));
                GPU_ARGS->w = 64, GPU_ARGS->rows = 1;
                gpu_kernel.i915_exec_error = 22, execs = gpu_kernel.i915_execs;
                check("gpu i915: a batch the kernel refuses with EINVAL is our own job: refused once, not asked again, the card not lost, counted",
                      gpu_submit() == 0 && gpu_last_error == 22 && gpu_kernel.i915_execs == execs + 1 && !gpu_lost && gpu_jobs_ours == ours + 1);
                gpu_kernel.i915_exec_error = 5;
                check("gpu i915: EIO (a context banned after a reset, a wedged GPU) is a card lost", gpu_submit() == 0 && gpu_last_error == 5 && gpu_lost);
                gpu_kernel.i915_exec_error = 0, gpu_lost = 0;
                gpu_set = 0, execs = gpu_kernel.i915_execs;
                check("gpu i915: with no kernel set the core's submit is refused at once and the card is not lost",
                      gpu_submit() == 0 && gpu_last_error == 95 && !gpu_lost && gpu_kernel.i915_execs == execs);
        }

        // Put back what the rows changed (the sync_files are the table's: gpu_i915_forget closes the ones a wait did not).
        gpu_i915_forget();
        memory_zero(gpu_i915.bound, (positive)64 * sizeof(gpu_i915.bound[0]));
        memory_copy(&gpu_kernel, kept_kernel, sizeof(gpu_kernel)), memory_copy(&gpu, kept_gpu, sizeof(gpu)), memory_copy(&gpu_req, kept_req, sizeof(gpu_req));
        memory_copy(&compositor_gpu_state, kept_state, sizeof(compositor_gpu_state));
        {
                struct gpu_i915_bound *bound = gpu_i915.bound;
                struct gpu_i915_object *list = gpu_i915.objects;

                memory_copy(&gpu_i915, kept_i915, sizeof(gpu_i915));
                gpu_i915.bound = bound, gpu_i915.objects = list;
        }
        memory_copy(&gpu_window, kept_window, sizeof(gpu_window)), memory_copy(&gpu_layout, kept_layout, sizeof(gpu_layout));
        gpu_backend = kept_backend, gpu_set = kept_set, gpu_map_next = kept_next, gpu_jobs_ours = kept_ours, gpu_last_error = kept_error, gpu_lost = kept_lost;
        compositor_gpu = kept_on, compositor.table = kept_table, compositor.card = kept_card;
        gpu_slot = 0, gpu_map_count = kept_maps, gpu_faults = kept_faults, gpu_job_error = kept_job_error, gpu_fault_next = kept_fault_next;
        memory_copy(gpu_fault, kept_fault, sizeof(gpu_fault)), memory_copy(&gpu_shared, kept_shared, sizeof(gpu_shared));
}

/*
        The card's path by generation (gpu.c's gpu_slower, the compositor's compositor_gpu_slower): i915's Gen7 and Gen7.5 (Ivy Bridge, Valleyview, Haswell)
        compose on the processor while canvas gpu is at its default (no word, or "default"), since there the card measured slower than the processor at every
        size (a Haswell HD 4600, 0x0412, two screens: the card's present 21.6 to 44.9 ms and up to 180, the processor's 0.27 to 0.34 ms). Gen8, Gen9 and later,
        and amdgpu, keep the card. canvas gpu said on puts the card back on any generation, and put back to its default lets it go again. The chips are the
        fake i915 node's device ids; the card that is up is the compositor's state as a start leaves it (the start itself is gpu_i915_rows').
*/
static void gpu_generation_rows(void)
{
        static unsigned char kept_kernel[sizeof(gpu_kernel)];
        static const unsigned int slower[][2] = {{0x0412, 750}, {0x041E, 750}, {0x0152, 700}, {0x0166, 700}, {0x0F31, 701}};
        static const unsigned int faster[][2] = {{0x1616, 800}, {0x3E92, 900}, {0x3185, 901}, {0x8A52, 1100}, {0x9A49, 1200}};
        unsigned char kept_gpu[sizeof(gpu)], kept_req[sizeof(gpu_req)], kept_state[sizeof(compositor_gpu_state)], kept_i915[sizeof(gpu_i915)];
        unsigned char kept_shared[sizeof(gpu_shared)];
        const struct gpu_backend *kept_backend = gpu_backend;
        struct windows *kept_table = compositor.table;
        int kept_on = compositor_gpu, kept_asked = compositor_gpu_asked, kept_card = compositor.card;
        unsigned int kept_maps = gpu_map_count, family = 0, device = 0, identified;
        positive kept_next = gpu_map_next;
        b32 off = true, on = true;

        memory_copy(kept_kernel, &gpu_kernel, sizeof(gpu_kernel)), memory_copy(kept_gpu, &gpu, sizeof(gpu)), memory_copy(kept_req, &gpu_req, sizeof(gpu_req));
        memory_copy(kept_state, &compositor_gpu_state, sizeof(compositor_gpu_state)), memory_copy(kept_i915, &gpu_i915, sizeof(gpu_i915));
        memory_copy(kept_shared, &gpu_shared, sizeof(gpu_shared));
        memory_zero(&gpu_kernel, sizeof(gpu_kernel));
        gpu_kernel.on = 1, gpu_kernel.i915_ppgtt = 1, gpu_kernel.i915_gtt = 1ull << 31;
        string_copy_bounded((string_address)gpu_kernel.i915, (string_address) "i915", sizeof(gpu_kernel.i915));
        for (unsigned int i = 0; i < sizeof(slower) / sizeof(slower[0]); i++)
        {
                gpu_kernel.i915_chip = slower[i][0];
                off &= gpu_i915_identify(GPU_KERNEL_I915, &family, &device) == 0 && family == slower[i][1] && gpu_slower(&gpu_i915_row, family);
        }
        for (unsigned int i = 0; i < sizeof(faster) / sizeof(faster[0]); i++)
        {
                gpu_kernel.i915_chip = faster[i][0];
                on &= gpu_i915_identify(GPU_KERNEL_I915, &family, &device) == 0 && family == faster[i][1] && !gpu_slower(&gpu_i915_row, family);
        }
        check("gpu generation: a Gen7.5 device (Haswell 0x0412, 0x041e) and Gen7's (Ivy Bridge 0x0152, 0x0166, Valleyview 0x0f31) are the processor's by default", off);
        check("gpu generation: Gen8, Gen9, Gen9 LP, Gen11 and Gen12 devices keep the card, and so does amdgpu", on && !gpu_slower(&gpu_amd, 144) && !gpu_slower(&gpu_amd, 750));

        // The compositor with a card up as a start leaves it (no descriptor, no buffer, no maps of the rows': the let-go closes and unmaps nothing of theirs).
        gpu.at = 0, gpu.fd = -1, gpu.size = 0, gpu.imports = 0, gpu.pending = 0, gpu_map_count = 0, compositor.table = 0, compositor.card = -1;
        memory_zero(&compositor_gpu_state, sizeof(compositor_gpu_state));
        compositor_gpu_word("");
        check("gpu generation: no word is the default: the card on, not said on", compositor_gpu && !compositor_gpu_asked);
        gpu_backend = &gpu_i915_row, gpu.family = 750, gpu.device = 0x0412, gpu_req.family = 750, gpu_req.device = 0x0412, compositor_gpu_state.up = 1;
        check("gpu generation: at the default a Haswell card that is up is let go of, its reason a slower generation",
              compositor_gpu_up() == 0 && !compositor_gpu_state.up && compositor_gpu_state.dead && compositor_gpu_state.dead_why == COMPOSITOR_GPU_WHY_GENERATION &&
                  gpu.fd == -1);
        identified = gpu_kernel.i915_identifies;
        check("gpu generation: and it is not started again while canvas gpu stays at its default (no chip asked)",
              compositor_gpu_up() == 0 && compositor_gpu_state.dead_why == COMPOSITOR_GPU_WHY_GENERATION && gpu_kernel.i915_identifies == identified);
        check("gpu generation: its reason's words are the default's, not a fault's",
              string_find((string_address)windows_gpu_reasons[COMPOSITOR_GPU_WHY_GENERATION], (string_address) "off by default on this GPU generation") != 0);
        memory_zero(&compositor_gpu_state, sizeof(compositor_gpu_state));
        gpu_backend = &gpu_i915_row, gpu.family = 900, gpu.device = 0x3e92, gpu_req.family = 900, compositor_gpu_state.up = 1;
        check("gpu generation: at the default a Gen9 card that is up stays up", compositor_gpu_up() == 1 && compositor_gpu_state.up && !compositor_gpu_state.dead);
        compositor_gpu_state.up = 0, compositor_gpu_state.dead = 1, compositor_gpu_state.dead_why = COMPOSITOR_GPU_WHY_GENERATION;
        compositor_gpu_word("on");
        check("gpu generation: canvas gpu said on is the setting's, and tries the card again at once", compositor_gpu && compositor_gpu_asked && !compositor_gpu_state.dead);
        gpu.family = 750, gpu.device = 0x0412, gpu_req.family = 750, compositor_gpu_state.up = 1;
        check("gpu generation: said on, a Haswell card that is up composes", compositor_gpu_up() == 1 && compositor_gpu_state.up && !compositor_gpu_state.dead);
        compositor_gpu_word("default");
        check("gpu generation: put back to its default, the Haswell card it turned on is let go of again",
              !compositor_gpu_asked && compositor_gpu_up() == 0 && compositor_gpu_state.dead_why == COMPOSITOR_GPU_WHY_GENERATION);
        compositor_gpu_word("off");
        check("gpu generation: off is off", !compositor_gpu && !compositor_gpu_asked);

        // What the table and the board say of it: the state for moonwater canvas's line, and no warning (a default, not a refusal of the card's).
        {
                struct windows *table = windows_scratch_table(&app_table_scratch);
                struct windows_issue want[WINDOWS_GPU_CODES];
                unsigned int count;
                b32 warned = false;

                windows_scratch_zero(table);
                compositor.table = table, compositor_gpu = 1;
                compositor_gpu_state.up = 0, compositor_gpu_state.dead = 1, compositor_gpu_state.dead_why = COMPOSITOR_GPU_WHY_GENERATION;
                compositor_gpu_issues();
                table->gpu_processor = 200, table->gpu_refused[COMPOSITOR_GPU_WHY_GENERATION] = 200;
                count = windows_gpu_wanted(table, want, WINDOWS_GPU_CODES);
                for (unsigned int k = 0; k < count; k++)
                        warned |= want[k].code == WINDOWS_ISSUE_GPU_REFUSED;
                check("gpu generation: the table says the card's path is off for its generation, and the board warns of no refused presents",
                      table->gpu_on && table->gpu_state == WINDOWS_GPU_SLOWER && !warned);
                windows_scratch_zero(table);
                compositor.table = 0;
        }

        gpu_i915_forget();
        {
                struct gpu_i915_bound *bound = gpu_i915.bound;
                struct gpu_i915_object *list = gpu_i915.objects;

                memory_copy(&gpu_i915, kept_i915, sizeof(gpu_i915));
                gpu_i915.bound = bound, gpu_i915.objects = list;
        }
        memory_copy(&gpu_kernel, kept_kernel, sizeof(gpu_kernel)), memory_copy(&gpu, kept_gpu, sizeof(gpu)), memory_copy(&gpu_req, kept_req, sizeof(gpu_req));
        memory_copy(&compositor_gpu_state, kept_state, sizeof(compositor_gpu_state)), memory_copy(&gpu_shared, kept_shared, sizeof(gpu_shared));
        gpu_backend = kept_backend, compositor_gpu = kept_on, compositor_gpu_asked = kept_asked, compositor.table = kept_table, compositor.card = kept_card;
        gpu_map_count = kept_maps, gpu_map_next = kept_next;
}

/*
        The card's prerequisites beyond the start: the node the card is (gpu_pick, the display's own device first), the late probe's schedule (compositor_gpu_late_due:
        a look at once, then every five seconds for the first minute, then every minute), and the card's losses on the real card: a submit that answers ECANCELED is
        a lost card (gpu_lost), recovered by gpu_recover (the maps let go, the card started again, its self-test drawn right), and a card whose node is absent
        (ENOENT for every node) does not start until one is there. These run last in main (they start and stop the card).
*/
static void gpu_node_rows(void)
{
        char candidate[2][32] = {"pci:0000:c5:00.0", "pci:0000:04:00.0"};
        unsigned int why = 9;
        b64 first = 0, next = 0;

        check("gpu node: the display's own device is the node", gpu_pick(candidate, 2, "pci:0000:04:00.0", &why) == 1 && why == 1);
        check("gpu node: with no display on either, the first amdgpu node, and the reason says the display is elsewhere", gpu_pick(candidate, 2, "pci:0000:00:02.0", &why) == 0 && why == 2);
        check("gpu node: with no display's bus known, the first, and no reason", gpu_pick(candidate, 2, "", &why) == 0 && why == 0);
        check("gpu node: no candidate, no node", gpu_pick(candidate, 0, "", &why) == -1);
        check("late probe: the first look is not due, the next is in five seconds", !compositor_gpu_late_due(10000000000ull, &first, &next) && next == 15000000000ull);
        check("late probe: due at five seconds, then five seconds more (the first minute)", compositor_gpu_late_due(15000000000ull, &first, &next) && next == 20000000000ull);
        check("late probe: due again at ten, then not before the next", compositor_gpu_late_due(20000000000ull, &first, &next) && !compositor_gpu_late_due(21000000000ull, &first, &next));
        check("late probe: after the first minute, every sixty seconds", compositor_gpu_late_due(80000000000ull, &first, &next) && next == 140000000000ull);
        check("late probe: not due before the sixty seconds are up", !compositor_gpu_late_due(100000000000ull, &first, &next) && compositor_gpu_late_due(140000000000ull, &first, &next));
}

/*
        The sleep and the late probe: a boot clock that ran on past the monotonic clock by more than three seconds since the last look is a sleep, and the card
        is started again before the next present (compositor_gpu_restart_check); the late probe's deadline is none while the card is up, off or wrong, and the
        tick of the loop starts a card that is there (the card is up after it). Last in main: the restart replaces the card's buffer.
*/
static void gpu_resume_rows(void)
{
        positive seen = 0;
        positive now = 1000000000ull;
        unsigned int before;

        check("resume: the first look only takes the gap", !compositor_gpu_slept(100000000ull, &seen) && seen == 100000000ull);
        check("resume: a gap that grew by more than three seconds is a sleep", compositor_gpu_slept(10000000000ull, &seen) && seen == 10000000000ull);
        check("resume: a gap that grew by less is no sleep", !compositor_gpu_slept(10500000000ull, &seen));
        compositor_gpu_state.up = 1, compositor_gpu_state.dead = 0, compositor_gpu_state.restart_due = 0;
        before = gpu_recovered;
        compositor_gpu_restart_check();
        check("resume: no flag and no sleep, the card is not started again", gpu_recovered == before && compositor_gpu_state.up);
        compositor_gpu_resumed();
        compositor_gpu_restart_check();
        check("resume: the flag restarts the card before the next present (counted, flag cleared)", gpu_recovered == before + 1 && !compositor_gpu_state.restart_due && compositor_gpu_state.up);
        // The late probe: nothing to wait for while the card is up or off; a missing card is looked at five seconds on, and the tick starts a card that is there.
        compositor_gpu = 1, compositor_gpu_state.up = 0, compositor_gpu_state.dead = 1, compositor_gpu_state.dead_why = COMPOSITOR_GPU_WHY_NO_CARD;
        compositor_gpu_state.late_first = compositor_gpu_state.late_next = 0;
        check("late probe: a missing card is looked for five seconds on", compositor_gpu_late_at(now) == now + 5000000000ull);
        compositor_gpu_late_tick(now + 1000000000ull);
        check("late probe: the tick does not look before its time", compositor_gpu_state.dead && compositor_gpu_late_at(now) == now + 5000000000ull);
        compositor_gpu_late_tick(now + 5000000000ull);
        check("late probe: the tick looks again at its time, and the card that is there is up", compositor_gpu_state.up && !compositor_gpu_state.dead);
        check("late probe: a card that is up waits for nothing", compositor_gpu_late_at(now) == 0);
        compositor_gpu_state.up = 0, compositor_gpu_state.dead = 1, compositor_gpu_state.dead_why = COMPOSITOR_GPU_WHY_NO_CARD;
        compositor_gpu = 0;
        check("late probe: canvas gpu off waits for nothing", compositor_gpu_late_at(now) == 0);
        compositor_gpu = 1, compositor_gpu_state.up = 1, compositor_gpu_state.dead = 0;
}

static void gpu_loss_rows(void)
{
        unsigned int recovered_before = gpu_recovered; // the restarts the earlier rows made (a hang's restart is one) are not this row's
        positive base = gpu.size - (1 << 19), ops_at = base, bins_at = base + 4096, d_at = base + 8192;
        unsigned int address_to ops = (unsigned int address_to)((p8 address_to)gpu.at + ops_at);
        unsigned int q[8] = {0, 0, 0, 64, 4, 0xff336699u, 0, 0}, started;

        // A submit answered EAGAIN (a page of a userptr moved as it was submitted: the kernel asks for the call again): the scene is drawn after eight of them, and is
        // not drawn when every submit is answered so (error 11 kept); neither is a lost card.
        memory_copy(ops, q, 32);
        gpu_fake_again = 8;
        check("gpu loss: a submit answered EAGAIN eight times is asked again and the scene is drawn", gpu_scene(ops_at, 1, bins_at, d_at, 64, 64, 4, 0, 0, 64, 4) && !gpu_lost && gpu_fake_again == 0);
        gpu_fake_again = 1000;
        check("gpu loss: a submit answered EAGAIN every time gives the job up with the error kept, and the card is not lost",
              !gpu_scene(ops_at, 1, bins_at, d_at, 64, 64, 4, 0, 0, 64, 4) && gpu_last_error == 11 && !gpu_lost);
        gpu_fake_again = 0;
        // A submit answered EINVAL once (a chunk or a buffer of the one job): refused and asked again, the scene drawn, the card not lost; answered so twice
        // in a row (the context or the VM the kernel has not): the card lost.
        memory_copy(ops, q, 32);
        gpu_fake_invalid = 1;
        check("gpu loss: a submit answered EINVAL once is asked again and the scene is drawn, and the card is not lost",
              gpu_scene(ops_at, 1, bins_at, d_at, 64, 64, 4, 0, 0, 64, 4) && !gpu_lost && gpu_fake_invalid == 0);
        memory_copy(ops, q, 32);
        gpu_fake_invalid = 2;
        check("gpu loss: a submit answered EINVAL twice in a row is a lost card", !gpu_scene(ops_at, 1, bins_at, d_at, 64, 64, 4, 0, 0, 64, 4) && gpu_lost);
        gpu_fake_invalid = 0, gpu_lost = 0, gpu_suspects = 0;
        // A submit answered ECANCELED: the card is lost (the scene is not drawn; the caller recovers), and the recovery draws the self-test right again.
        memory_copy(ops, q, 32);
        gpu_fake_cancel = 1;
        check("gpu loss: a submit answered ECANCELED is a lost card, and the scene is not drawn", !gpu_scene(ops_at, 1, bins_at, d_at, 64, 64, 4, 0, 0, 64, 4) && gpu_lost);
        gpu_fake_cancel = 0;
        check("gpu loss: the card starts again (gpu_recover) and counts it", gpu_recover() && gpu_recovered == recovered_before + 1 && !gpu_lost);
        check("gpu loss: the card draws its self-test right after the recovery", gpu_test_hash() == GPU_TEST_HASH);
        check("gpu loss: the maps are let go with the card", gpu_map_count == 0 && gpu.imports == 0);
        // A node that is absent (every node: ENOENT) is no card, and the start says so at the node; once it is there, the card starts.
        gpu_stop();
        gpu_fake_absent = 64;
        started = gpu_start(1 << 20);
        gpu_fake_absent = 0;
        check("gpu loss: no render node, the start stops at the node (ENOENT)", !started && gpu_req.first == GPU_REQ_NODE && gpu_req.error == 2);
        check("gpu loss: the node is there again, and the card starts", gpu_start(1 << 20) && gpu.fd >= 0);
}

// A card's import held by the card now (an address the card's descriptor maps, not let go of).
static b32 gpu_held_import(positive at)
{
        for (unsigned int i = 0; i < gpu.imports; i++)
                if (gpu.imported_va[i] == at && !gpu.imported_dead[i])
                        return true;
        return false;
}

static unsigned int gpu_lent_pixels[64 * 64];

/*
        A client's dma-buf lent to a row (compositor_wl_lend) keeps the card's address of its import, and a card started again (gpu_recover: a hang's
        restart, a resume, a context the card lost) has a descriptor and an address space of its own: an address kept from before it is none of its maps,
        and a job that names it faults. Real card only; the state is looked at (no job runs that names an address the card does not hold).
*/
static void gpu_lent_rows(void)
{
        struct wayland_picture picture;
        p64 make[4] = {sizeof(gpu_lent_pixels), 4096, 2, 1}, drop[1];
        unsigned int prime[3] = {0, 2 | 0x80000, 0};
        int saved_width = compositor.width, saved_most = compositor.most_w, dmabuf;
        const int row = 5;

        if (gpu.fd < 0 || gpu.broken)
        {
                check("gpu lent: no card here, so no picture is lent to one", true);
                return;
        }
        if (gpu_call(3u, 0, 32, make) != 0)
        {
                check("gpu lent: a buffer for a dma-buf is made on the card", false);
                return;
        }
        prime[0] = drop[0] = (unsigned int)make[0];
        if (system_control(gpu.fd, 3u << 30 | 12u << 16 | 0x642du, prime) != 0) // DRM_IOCTL_PRIME_HANDLE_TO_FD: the dma-buf holds the buffer
        {
                check("gpu lent: the buffer is a dma-buf", false);
                gpu_raw(gpu.fd, GPU_GEM_CLOSE, drop);
                return;
        }
        dmabuf = (int)prime[2];
        gpu_raw(gpu.fd, GPU_GEM_CLOSE, drop);
        compositor.width = 64, compositor.most_w = 0;
        memory_zero(&picture, sizeof(picture));
        picture.pixels = (const unsigned char *)gpu_lent_pixels, picture.stride = 256, picture.width = 64, picture.height = 64;
        picture.full = 1, picture.last = 1, picture.sync_fd = dmabuf, picture.map = picture.pixels, picture.map_bytes = sizeof(gpu_lent_pixels);
        check("gpu lent: a client's dma-buf is lent to a row, its card address held by the card",
              compositor_wl_lend(row, &picture, 32, 32) && compositor_lent_va(row) != 0 && gpu_held_import(compositor_lent_va(row)));
        check("gpu lent: the card started again", gpu_recover());
        {
                // The lent picture's address is asked again of the started card: imported into it (wl_ext_lent_info, by the epoch), never the old card's.
                positive again = compositor_lent_va(row);

                check("gpu lent: after the start the lent picture's address is the started card's, imported again when asked (not the old descriptor's)",
                      again != 0 && gpu_held_import(again) && compositor_wl.lent[row].epoch == gpu_epoch);
        }
        check("gpu lent: the picture lent again is imported into the started card, at an address that card holds",
              compositor_wl_lend(row, &picture, 32, 32) && compositor_lent_va(row) != 0 && gpu_held_import(compositor_lent_va(row)));
        compositor_wl_unlend(row);
        system_close(dmabuf);
        compositor.width = saved_width, compositor.most_w = saved_most;
}

/*
        A client's dma-buf as a framebuffer to scan out (compositor_lent_fb), the display node's requests faked (no card has run this path): the window's
        rectangle at the byte its first pixel is in the buffer (here 2 rows and 64 pixels in), a modifier the client named linear (0) or another passed with
        DRM_MODE_FB_MODIFIERS as it is, an implicit one (DRM_FORMAT_MOD_INVALID) with no flag so the kernel reads the buffer's own layout; on amdgpu a linear
        pitch or offset that is not whole 256-byte blocks refused before the request (amdgpu_display_verify_plane refuses it), and every refusal kept with
        what was asked and why.
*/
static void gpu_scan_rows(void)
{
        enum { ROW = 6 };
        struct fb_asked
        {
                unsigned int fb_id, width, height, pixel_format, flags;
                unsigned int handles[4], pitches[4], offsets[4];
                unsigned long long modifier[4];
        } asked;
        static unsigned int buffer[256 * 64];
        int kept_card = compositor.card;
        char kept_driver[sizeof compositor.driver];
        unsigned int refused = compositor_wl.scan_refused.count;

        memory_copy(kept_driver, compositor.driver, sizeof kept_driver);
        memory_zero(compositor.driver, sizeof compositor.driver);
        memory_copy(compositor.driver, "amdgpu", 7);
        compositor.card = 1000; // no descriptor: the requests are the fakes, and a framebuffer's removal is EBADF
#define GPU_SCAN_LEND(modifier_, pitch_, x_)                                                                                                              \
        (compositor_wl_unlend(ROW), memory_zero(&compositor_wl.lent[ROW], sizeof compositor_wl.lent[ROW]),                                                   \
         compositor_wl.lent[ROW].map = (const unsigned char *)buffer, compositor_wl.lent[ROW].pitch = (pitch_),                                             \
         compositor_wl.lent[ROW].pixels = buffer + 2 * (pitch_) + (x_), compositor_wl.lent[ROW].width = 128, compositor_wl.lent[ROW].height = 32,           \
         compositor_wl.lent[ROW].fd = 9, compositor_wl.lent[ROW].size = sizeof buffer, compositor_wl.lent[ROW].fourcc = 0x34325258u,                      \
         compositor_wl.lent[ROW].modifier = (modifier_))
        GPU_SCAN_LEND(0, 256, 64);
        gpu_fake_addfb_answer = 0;
        check("gpu scan: a lent dma-buf named linear is a framebuffer", compositor_lent_fb(ROW) != 0);
        memory_copy(&asked, gpu_fake_addfb, sizeof asked);
        check("gpu scan: of the window's rectangle at the byte its first pixel is (two rows and 64 pixels in)",
              asked.width == 128 && asked.height == 32 && asked.pitches[0] == 1024 && asked.offsets[0] == 2 * 1024 + 64 * 4 && asked.handles[0] == 7u &&
                      asked.pixel_format == 0x34325258u);
        check("gpu scan: linear asked as linear, with the modifiers flag", asked.flags == 2u && asked.modifier[0] == 0);
        GPU_SCAN_LEND(0x00ffffffffffffffULL, 256, 64);
        check("gpu scan: an implicit layout (DRM_FORMAT_MOD_INVALID) is asked with no flag, the kernel reading the buffer's own",
              compositor_lent_fb(ROW) != 0 && (memory_copy(&asked, gpu_fake_addfb, sizeof asked), asked.flags == 0 && asked.modifier[0] == 0));
        GPU_SCAN_LEND(0x0200000000000001ULL, 256, 64);
        check("gpu scan: another modifier is asked as it is, with the flag (the kernel refuses what no plane lists)",
              compositor_lent_fb(ROW) != 0 && (memory_copy(&asked, gpu_fake_addfb, sizeof asked), asked.flags == 2u && asked.modifier[0] == 0x0200000000000001ULL));
        {
                int calls = gpu_fake_addfb_calls;

                GPU_SCAN_LEND(0, 250, 64);
                check("gpu scan: on amdgpu a linear pitch of no whole 256-byte blocks is refused before it is asked, and kept",
                      !compositor_lent_fb(ROW) && gpu_fake_addfb_calls == calls && compositor_wl.scan_refused.error == -1 &&
                              compositor_wl.scan_refused.pitch == 1000 && compositor_wl.scan_refused.count == refused + 1);
                GPU_SCAN_LEND(0, 256, 3);
                {
                        // An offset of no whole block over an aligned base (the window three pixels and two rows in): made at the base, its origin the
                        // window's place in it (canvas_scan_fb_make's realign on amdgpu, comp-display's contract), not refused.
                        int ox = -1, oy = -1;
                        unsigned int made = compositor_lent_fb(ROW);

                        compositor_lent_fb_origin(ROW, &ox, &oy);
                        memory_copy(&asked, gpu_fake_addfb, sizeof asked);
                        check("gpu scan: an offset of no whole block over an aligned base is made at the base, with the window's origin in it (3, 2), not refused",
                              made && ox == 3 && oy == 2 && gpu_fake_addfb_calls == calls + 1 && asked.offsets[0] == 0 && asked.width == 131 &&
                                      compositor_wl.scan_refused.count == refused + 1);
                }
        }
        GPU_SCAN_LEND(0, 256, 64);
        gpu_fake_addfb_answer = -22;
        check("gpu scan: a framebuffer the kernel refuses is kept refused with its errno and what was asked",
              !compositor_lent_fb(ROW) && compositor_wl.scan_refused.error == 22 && compositor_wl.scan_refused.width == 128 &&
                      compositor_wl.scan_refused.fourcc == 0x34325258u && compositor_wl.lent[ROW].scan_failed);
        check("gpu scan: and not asked again for the same buffer", !compositor_lent_fb(ROW));
        gpu_fake_addfb_answer = 0;
        // A full grave list while a flip is on its way (every grave counts as scanned): the framebuffer on the screen is not the one removed (E-h2-3),
        // the oldest the screen does not show is, and it is counted.
        {
                unsigned int kept_screen = compositor.on_screen, kept_count = compositor_wl_grave_count, forced = compositor_wl_graves_forced;
                unsigned int kept_graves[COMPOSITOR_WL_GRAVES];
                int kept_flip = compositor.flip_pending;
                b32 screen_kept = false, oldest_gone = true;

                memory_copy(kept_graves, compositor_wl_graves, sizeof kept_graves);
                compositor.flip_pending = 1, compositor.on_screen = 501;
                compositor_wl_grave_count = 0;
                for (unsigned int k = 0; k < COMPOSITOR_WL_GRAVES; k++)
                        compositor_wl_drop_fb(501 + k);
                compositor_wl_drop_fb(600);
                for (unsigned int k = 0; k < compositor_wl_grave_count; k++)
                        screen_kept |= compositor_wl_graves[k] == 501, oldest_gone &= compositor_wl_graves[k] != 502;
                check("gpu scan: a full grave list never removes the framebuffer on the screen; the oldest the screen does not show goes, counted",
                      screen_kept && oldest_gone && compositor_wl_grave_count == COMPOSITOR_WL_GRAVES && compositor_wl_graves_forced == forced + 1 &&
                          compositor_wl_graves[COMPOSITOR_WL_GRAVES - 1] == 600);
                memory_copy(compositor_wl_graves, kept_graves, sizeof kept_graves);
                compositor_wl_grave_count = kept_count, compositor.on_screen = kept_screen, compositor.flip_pending = kept_flip;
        }
#undef GPU_SCAN_LEND
        memory_zero(&compositor_wl.lent[ROW], sizeof compositor_wl.lent[ROW]);
        compositor.card = kept_card;
        memory_copy(compositor.driver, kept_driver, sizeof kept_driver);
}

/*
        A client's window on the overlay plane (compositor_plane_candidate): a Wayland row's flags (no close button, no grab) are no glass, so its
        window, the top one, goes on the overlay; its picture blended (Chromium's: round top corners outside its opaque region) goes there only where
        the plane's rectangle, the window less its corners' rows, is inside the opaque rectangle the client said, and its framebuffer is then asked as
        the X format of its bytes (compositor_lent_fb_opaque), the composed path's keeping its alpha (compositor_lent_fb). A window with subsurfaces
        over it, anything the desktop draws over the plane's rectangle, and a window that is glass are composed. Before, every client's window was
        refused as glass (the row's whole flags word tested) and a blended one as not opaque wherever its opaque region was.
*/
static void gpu_plane_rows(void)
{
        enum { ROW = 6, W = 128, H = 96, X = 100, Y = 50 };
        struct fb_asked
        {
                unsigned int fb_id, width, height, pixel_format, flags;
                unsigned int handles[4], pitches[4], offsets[4];
                unsigned long long modifier[4];
        } asked;
        static unsigned int buffer[256 * 100];
        static struct windows *table_scratch;
        struct windows *const kept_table = compositor.table, *const table_at = windows_scratch_fresh(&table_scratch);
        struct compositor_frame frames[2];
        unsigned int rows[2] = {ROW, ROW};
        int kept_card = compositor.card, kept_width = compositor.width, kept_height = compositor.height, kept_scale = compositor.scale;
        int kept_overlay = compositor.overlay_state, kept_soft = compositor.cursor.soft, rect[4];
        unsigned int *kept_band = compositor.band;
        int kept_drag = compositor.drag, kept_resize = compositor.resize;
        unsigned int kept_outputs = compositor.outputs;
        char kept_driver[sizeof compositor.driver];

        windows_scratch_zero(table_at);
        compositor.table = table_at;
        memory_copy(kept_driver, compositor.driver, sizeof kept_driver);
        memory_zero(compositor.driver, sizeof compositor.driver);
        memory_copy(compositor.driver, "i915", 5);
        compositor.card = 1000; // no descriptor: the framebuffer's requests are the fakes
        compositor.width = 640, compositor.height = 480, compositor.scale = 100, compositor.outputs = 0;
        compositor.overlay_state = 1, compositor.cursor.soft = 0, compositor.band = 0, compositor.drag = compositor.resize = -1;
        compositor.x[ROW] = X, compositor.y[ROW] = Y, compositor.w[ROW] = compositor.picture_w[ROW] = W, compositor.h[ROW] = compositor.picture_h[ROW] = H;
        compositor.landing[ROW] = compositor.asking[ROW] = 0, compositor.glassy[ROW] = 0;
        table_at->glass[ROW] = WINDOW_NO_CLOSE | WINDOW_NO_GRAB;
        compositor_wl_unlend(ROW);
        memory_zero(&compositor_wl.lent[ROW], sizeof compositor_wl.lent[ROW]);
        compositor_wl.lent[ROW].map = (const unsigned char *)buffer, compositor_wl.lent[ROW].pitch = 256, compositor_wl.lent[ROW].pixels = buffer + 2 * 256 + 64;
        compositor_wl.lent[ROW].width = W, compositor_wl.lent[ROW].height = H, compositor_wl.lent[ROW].fd = 9, compositor_wl.lent[ROW].size = sizeof buffer;
        compositor_wl.lent[ROW].fourcc = 0x34325241u, compositor_wl.lent[ROW].modifier = 0; // AR24, linear
        compositor_wl.row_blend[ROW] = 1;
        compositor_wl.row_opaque[ROW].x1 = 0, compositor_wl.row_opaque[ROW].y1 = 8, compositor_wl.row_opaque[ROW].x2 = W, compositor_wl.row_opaque[ROW].y2 = H;
        memory_zero(frames, sizeof frames);
        frames[0].x = X, frames[0].y = Y, frames[0].width = W, frames[0].height = H, frames[0].scale = 100, frames[0].radius = 8, frames[0].line = 1;
        frames[0].blend = 1, frames[0].pixels = compositor_wl.lent[ROW].pixels, frames[0].pitch = 256;
        gpu_fake_addfb_answer = 0;
        check("gpu plane: a client's window (no close, no grab) on top goes on the overlay, less its corners' rows",
              compositor_plane_candidate(frames, rows, 1, 1, rect) == ROW && rect[0] == X && rect[1] == Y + 8 && rect[2] == X + W && rect[3] == Y + H - 8 &&
                      table_at->unscanned == 0);
        memory_copy(&asked, gpu_fake_addfb, sizeof asked);
        check("gpu plane: blended, its plane's framebuffer is the X format of its bytes (AR24 as XR24)", asked.pixel_format == 0x34325258u &&
                                                                                                           compositor_wl.lent[ROW].scan_fourcc == 0x34325258u);
        check("gpu plane: and its composed framebuffer keeps its alpha (AR24)", compositor_lent_fb(ROW) != 0 &&
                                                                               (memory_copy(&asked, gpu_fake_addfb, sizeof asked), asked.pixel_format == 0x34325241u));
        compositor_wl.row_opaque[ROW].y1 = 12;
        check("gpu plane: a blended window whose opaque rectangle does not hold the plane's is composed (20)",
              compositor_plane_candidate(frames, rows, 1, 1, rect) < 0 && table_at->unscanned == 20);
        compositor_wl.row_opaque[ROW].x1 = 0, compositor_wl.row_opaque[ROW].y1 = 8;
        compositor_wl.row_blend[ROW] = 0;
        compositor_wl.row_opaque[ROW].x2 = 0; // none known: an opaque window needs none
        frames[0].blend = 0;
        check("gpu plane: an opaque window goes on the overlay with no opaque rectangle told", compositor_plane_candidate(frames, rows, 1, 1, rect) == ROW);
        frames[1] = frames[0];
        frames[1].x = X + 20, frames[1].y = Y + 30, frames[1].width = frames[1].height = 16, frames[1].line = 0, frames[1].radius = 0;
        check("gpu plane: a frame the desktop draws over the plane's rectangle keeps it composed (19)",
              compositor_plane_candidate(frames, rows, 1, 2, rect) < 0 && table_at->unscanned == 19);
        frames[1].y = Y + H + 4;
        check("gpu plane: and one beside it does not", compositor_plane_candidate(frames, rows, 1, 2, rect) == ROW);
        check("gpu plane: a window with subsurfaces over it (its layers, frames of the same row) is composed (18)",
              compositor_plane_candidate(frames, rows, 2, 2, rect) < 0 && table_at->unscanned == 18);
        compositor.glassy[ROW] = 1;
        check("gpu plane: a window of glass is composed (5)", compositor_plane_candidate(frames, rows, 1, 1, rect) < 0 && table_at->unscanned == 5);
        compositor.glassy[ROW] = 0;
        table_at->glass[ROW] = 0;
        compositor_wl_unlend(ROW);
        memory_zero(&compositor_wl.lent[ROW], sizeof compositor_wl.lent[ROW]);
        compositor_wl.row_blend[ROW] = 0;
        compositor.table = kept_table;
        compositor.card = kept_card, compositor.width = kept_width, compositor.height = kept_height, compositor.scale = kept_scale;
        compositor.overlay_state = kept_overlay, compositor.cursor.soft = kept_soft, compositor.band = kept_band, compositor.outputs = kept_outputs;
        compositor.drag = kept_drag, compositor.resize = kept_resize;
        memory_copy(compositor.driver, kept_driver, sizeof kept_driver);
}

/*
        Layers off (/run/moonwater/layers-off at the shim's start, compositor_wl_layers.off): the host keeps no subsurface as a layer, whatever it is
        handed (row_layers answers 0, the shim's way to lay them into the window's picture as before), with the root's picture lent or not.
*/
static void gpu_layers_off_rows(void)
{
        static unsigned int pixels[8 * 8];
        struct wayland_picture layer;
        unsigned char kept_off = compositor_wl_layers.off;
        struct windows *kept_table = compositor.table;
        static struct windows *table_scratch;
        struct windows *const table_at = windows_scratch_fresh(&table_scratch); // zero, as the table on the stack was

        windows_scratch_zero(table_at);
        compositor.table = table_at;
        memory_zero(&layer, sizeof layer);
        // An 8 by 8 picture at (20, 20) of a 64 by 64 window: clear of its corners, a layer the host keeps when layers are on.
        layer.pixels = layer.map = (const unsigned char *)pixels, layer.map_bytes = sizeof pixels, layer.stride = 32, layer.width = 8, layer.height = 8;
        layer.dest_x = layer.dest_y = 20, layer.row_width = 64, layer.row_height = 64, layer.sync_fd = -1, layer.layer = 1, layer.changed = 1;
        layer.full = 1, layer.last = 0;
        compositor_wl_layers.off = 0;
        check("gpu layers off: without the file, the window's subsurface handed with its picture is a layer",
              compositor_wl_row_layers(0, 7, &layer, 1, 1) == WAYLAND_LAYERS_KEPT && compositor_wl_layers.count[7] == 1);
        compositor_wl_layers_drop(7);
        compositor_wl_layers.off = 1;
        check("gpu layers off: with the file there, a window's subsurfaces are no layers, handed with its picture or alone",
              compositor_wl_row_layers(0, 7, &layer, 1, 1) == 0 && compositor_wl_row_layers(0, 7, &layer, 1, 0) == 0 && !compositor_wl_layers.count[7]);
        compositor_wl_layers_drop(7);
        compositor_wl_layers.off = kept_off;
        compositor.table = kept_table;
}

/*
        The overlay's buffers (compositor_overlay_buffers) are made at the output's width, and the band composed into them is the output's width
        at the present (compositor_present_overlaid): buffers made for a narrower output are made again, not composed into, and with no card to make
        them the overlay is refused (its tabs are patched into the window instead). Before, buffers with rows enough were kept at any width, and a
        band of a wider output ran each row into the next and its last past the buffer's end (the owner's machine: a fill's store into the page after).
*/
static void gpu_overlay_rows(void)
{
        int kept_width = compositor.width, kept_card = compositor.card;
        unsigned int kept_rows = compositor.overlay_rows, kept_made = compositor_overlay_width;

        compositor.card = -1; // no card: a buffer made again is refused
        compositor.width = 1920;
        compositor.overlay_rows = 64, compositor_overlay_width = 1920;
        check("gpu overlay: buffers of the output's width with rows enough are kept", compositor_overlay_buffers(48));
        compositor.width = 2560;
        check("gpu overlay: buffers made for a narrower output are made again, not composed into (here refused: no card)",
              !compositor_overlay_buffers(48) && compositor.overlay_rows == 0);
        // A band grown past its rows while the plane is on (the card has no buffer to give here): the plane is not taken off, and keeps the buffers it
        // scans. The mutant takes it off first (compositor_overlay_off), which the row sees as overlay_on 0 and the buffers gone.
        {
                int kept_on = compositor.overlay_on;
                unsigned int kept_fb[2] = {compositor.overlay_fb[0], compositor.overlay_fb[1]};

                memory_zero(compositor_overlay_old, sizeof(compositor_overlay_old));
                compositor.width = 1920, compositor.overlay_rows = 64, compositor_overlay_width = 1920, compositor.overlay_on = 1;
                compositor.overlay_fb[0] = 7001, compositor.overlay_fb[1] = 7002;
                check("gpu overlay: a band grown past its rows with the plane on is refused, and the plane stays on its buffers",
                      !compositor_overlay_buffers(128) && compositor.overlay_on == 1 && compositor.overlay_fb[0] == 7001 && compositor.overlay_fb[1] == 7002);
                compositor.overlay_on = kept_on;
                compositor.overlay_fb[0] = kept_fb[0], compositor.overlay_fb[1] = kept_fb[1];
        }
        compositor.width = kept_width, compositor.card = kept_card;
        compositor.overlay_rows = kept_rows, compositor_overlay_width = kept_made;
}

/*
        A lent dma-buf the processor composes (the card drew no present): read in place where its client's card wrote it, which on a card with memory of
        its own is a write-combined mapping read with plain loads (13 ms a present of a 1080p game on the owner's machine). The processor reads it in two
        presents running (compositor_wl_lent_sync counts the first read of each present) and the row is copied into the table once (compositor_wl_seed,
        the streaming loads) and lent no dma-buf until the card draws a present (compositor_wl_count_card), which a present the card draws between two of
        the processor's also is. The lent state is made by hand: a dma-buf is lent only to a card that is there (gpu_lent_rows), and a memfd stands in for
        the buffer's descriptor (its sync is refused, as a file's is, and the read is counted all the same).
*/
static void gpu_wc_rows(void)
{
        enum { SIDE = 32, ROW = 5 };
        struct windows *kept_table = compositor.table;
        unsigned long kept_pixels = compositor.pixels, kept_buffer = compositor.buffer;
        int kept_width = compositor.width, kept_stride = compositor.stride, kept_damage[4], kept_x = compositor.x[ROW], kept_y = compositor.y[ROW];
        unsigned int kept_damages = compositor.damages, kept_w = compositor.w[ROW], kept_h = compositor.h[ROW], kept_front = compositor.front[ROW];
        unsigned long table_bytes = ((sizeof(struct windows) + 4095) & ~4095ul) + (unsigned long)COMPOSITOR_ROWS_FIRST * 2 * SIDE * SIDE * 4;
        long table = system_call_6(syscall(mmap), 0, table_bytes, 3, 0x4022, (positive)-1, 0);
        int fd = (int)system_call_2(syscall(memfd_create), (positive) "wc", 0);
        static unsigned int pixels[SIDE * SIDE];
        unsigned int seeded = compositor_wl.wc_seeded, presents = compositor_wl.wc_presents, *front;
        unsigned int rows[1] = {ROW};

        if ((table < 0 && table > -4096) || fd < 0)
        {
                check("gpu wc: a table and a descriptor for the rows", false);
                return;
        }
        memory_copy(kept_damage, compositor.damage[0], sizeof kept_damage);
        for (unsigned int at = 0; at < SIDE * SIDE; at++)
                pixels[at] = 0xff000000u | at;
        compositor.table = (struct windows *)table;
        compositor.pixels = (sizeof(struct windows) + 4095) & ~4095ul;
        compositor.buffer = SIDE * SIDE * 4;
        compositor.width = SIDE, compositor.stride = SIDE;
        compositor.x[ROW] = compositor.y[ROW] = 0, compositor.w[ROW] = compositor.h[ROW] = SIDE, compositor.front[ROW] = 0;
        compositor.damages = 1, compositor.damage[0][0] = compositor.damage[0][1] = 0, compositor.damage[0][2] = compositor.damage[0][3] = SIDE;
        memory_zero(compositor_wl.wc_reads, compositor_rows_made * sizeof(*compositor_wl.wc_reads));
        memory_zero(compositor_wl.wc_copy, compositor_rows_made * sizeof(*compositor_wl.wc_copy));
#define GPU_WC_LEND()                                                                                                                                   \
        (memory_zero(&compositor_wl.lent[ROW], sizeof compositor_wl.lent[ROW]), compositor_wl.lent[ROW].pixels = pixels, compositor_wl.lent[ROW].pitch = SIDE, \
         compositor_wl.lent[ROW].width = SIDE, compositor_wl.lent[ROW].height = SIDE, compositor_wl.lent[ROW].fd = fd,                                 \
         compositor_wl.lent[ROW].map = (const unsigned char *)pixels, compositor_wl.lent[ROW].size = sizeof pixels)
        GPU_WC_LEND();
        // One present of the processor's: read in place, and lent still.
        compositor_wl_lent_sync(1);
        compositor_wl_lent_sync(1); // the glass and the band read it in the same present: one read of the present
        compositor_wl_lent_sync(0);
        compositor_wl_settle(ROW);
        check("gpu wc: one present the processor read a lent dma-buf in leaves it lent", compositor_lent(ROW) && compositor_wl.wc_reads[ROW] == 1 &&
                                                                                              compositor_wl.wc_presents == presents + 1);
        // A present the card draws after it: the count is none again, and a second of the processor's leaves it lent.
        compositor_wl_count_card(rows, 1);
        compositor_wl_lent_sync(1);
        compositor_wl_lent_sync(0);
        compositor_wl_settle(ROW);
        check("gpu wc: a present the card draws between two of the processor's keeps the dma-buf lent", compositor_lent(ROW) && compositor_wl.wc_reads[ROW] == 1);
        // A second of the processor's running: copied into the table once, and lent no dma-buf.
        compositor_wl_lent_sync(1);
        compositor_wl_lent_sync(0);
        compositor_wl_settle(ROW);
        front = (unsigned int *)((char *)compositor.table + compositor.pixels + (unsigned long)ROW * 2 * compositor.buffer);
        check("gpu wc: two presents running the processor read it, it is copied into the table once and not lent",
              !compositor_lent(ROW) && compositor_wl.wc_copy[ROW] && compositor_wl.wc_seeded == seeded + 1 && front[0] == pixels[0] &&
                      front[SIDE * SIDE - 1] == pixels[SIDE * SIDE - 1]);
        {
                struct wayland_picture picture;

                memory_zero(&picture, sizeof picture);
                picture.pixels = (const unsigned char *)pixels, picture.stride = SIDE * 4, picture.width = SIDE, picture.height = SIDE;
                picture.full = 1, picture.last = 1, picture.sync_fd = fd, picture.map = picture.pixels, picture.map_bytes = sizeof pixels;
                check("gpu wc: and the row is lent no dma-buf while the processor composes", !compositor_wl_lend(ROW, &picture, SIDE, SIDE));
        }
        compositor_wl_count_card(rows, 1);
        check("gpu wc: until the card draws a present", !compositor_wl.wc_copy[ROW] && !compositor_wl.wc_reads[ROW]);
        // A pool lent (no descriptor) is the machine's memory, and is not counted.
        GPU_WC_LEND();
        compositor_wl.lent[ROW].fd = -1;
        compositor_wl_lent_sync(1);
        compositor_wl_lent_sync(0);
        compositor_wl_lent_sync(1);
        compositor_wl_lent_sync(0);
        compositor_wl_settle(ROW);
        check("gpu wc: a lent pool is not counted, and stays lent", compositor_lent(ROW) && !compositor_wl.wc_reads[ROW]);
#undef GPU_WC_LEND
        memory_zero(&compositor_wl.lent[ROW], sizeof compositor_wl.lent[ROW]);
        system_close(fd);
        system_call_2(syscall(munmap), (positive)table, table_bytes);
        compositor.table = kept_table, compositor.pixels = kept_pixels, compositor.buffer = kept_buffer;
        compositor.width = kept_width, compositor.stride = kept_stride;
        compositor.x[ROW] = kept_x, compositor.y[ROW] = kept_y, compositor.w[ROW] = kept_w, compositor.h[ROW] = kept_h, compositor.front[ROW] = kept_front;
        compositor.damages = kept_damages;
        memory_copy(compositor.damage[0], kept_damage, sizeof kept_damage);
}

/*
        A client's pool read by the card (compositor_wl_card_rect): the card maps the picture's own rows, not the pool. A pool of 64 MiB holds a picture
        of 1 MiB at a byte that is not a page's; two rectangles of it are copied by the card, the first at the picture's first pixel and the second
        ending at its last, and the read map the card holds must be the picture's pages (from the page of its first byte to the end of the page of its
        last, which the second rectangle is found in without another map) and no more of the pool, and the card's rectangles equal the processor's
        copy of the same pixels, byte for byte.
*/
static void gpu_pool_rows(void)
{
        enum { POOL = 64 << 20, OFFSET = (40 << 20) + 1000, PW = 512, PH = 512, STRIDE = PW * 4, DW = 640, DH = 480, DBYTES = DW * DH * 4 };
        const positive extent = (positive)STRIDE * (PH - 1) + PW * 4, expected = ((positive)(OFFSET & 4095) + extent + 4095) & ~4095ull;
        static const int rect[2][4] = {{0, 0, 300, 200}, {PW - 250, PH - 100, 250, 100}}; // sx, sy, w, h: the first pixel's corner, then the last pixel's
        struct wayland_picture picture;
        long pool = system_call_6(syscall(mmap), 0, POOL, 3, 0x21, (positive)-1, 0), out = system_call_6(syscall(mmap), 0, DBYTES, 3, 0x4022, (positive)-1, 0);
        unsigned int address_to want = (unsigned int address_to)memory(DBYTES), address_to to = (unsigned int address_to)out;
        unsigned int address_to pixels;
        unsigned long saved_buffer = compositor.buffer;
        unsigned int held, mapped = 0, found = 0;
        positive from = 0, bytes = 0;
        b32 drew[2] = {false, false}, same = true;

        if (gpu.fd < 0 || gpu.broken)
        {
                check("gpu pool: no card here, so no pool is read by one", true);
                return;
        }
        if ((pool < 0 && pool > -4096) || (out < 0 && out > -4096))
        {
                check("gpu pool: memory for a pool of 64 MiB and a picture to copy into", false);
                return;
        }
        while (gpu_map_count)
                gpu_drop(0, GPU_GONE_RECOVER);
        pixels = (unsigned int address_to)((p8 address_to)pool + OFFSET);
        for (unsigned int i = 0; i < (positive)STRIDE * PH / 4; i++)
                pixels[i] = gpu_random();
        memory_zero(want, DBYTES);
        memory_zero(&picture, sizeof(picture));
        picture.pixels = (const unsigned char *)pixels, picture.stride = STRIDE, picture.width = PW, picture.height = PH;
        picture.sync_fd = -1, picture.map = (const unsigned char *)pool, picture.map_bytes = POOL, picture.full = 1, picture.last = 1;
        compositor.buffer = DBYTES;
        held = gpu_map_count;
        for (int k = 0; k < 2; k++)
        {
                const int sx = rect[k][0], sy = rect[k][1], w = rect[k][2], h = rect[k][3], dx = 7 + 11 * k, dy = 5 + 13 * k;

                drew[k] = compositor_wl_card_rect(5, &picture, to, DW, dx, dy, sx, sy, w, h, 1) != 0;
                for (int y = 0; y < h; y++)
                        memory_copy(want + (positive)(dy + y) * DW + dx, pixels + (positive)(sy + y) * PW + sx, (positive)w * 4);
                if (k == 0)
                        mapped = gpu_map_count - held; // the read map of the picture and the written one of the picture's destination
        }
        for (unsigned int i = 0; i < gpu_map_count; i++)
                if (!gpu_maps[i].written && (positive)pixels >= gpu_maps[i].from && (positive)pixels < gpu_maps[i].to)
                        found++, from = gpu_maps[i].from, bytes = gpu_maps[i].to - gpu_maps[i].from;
        same = !memory_compare(to, want, DBYTES);
        check("gpu pool: the card copies a rectangle at the picture's first pixel and one ending at its last", drew[0] && drew[1]);
        check("gpu pool: the card's rectangles are the processor's copy, byte for byte", same);
        check("gpu pool: one read map holds the picture, and the second rectangle is found in it (two maps in all: the picture's and the destination's)",
              found == 1 && mapped == 2 && gpu_map_count == held + 2);
        check("gpu pool: the read map starts at the page of the picture's first byte", from == ((positive)pixels & ~4095ull));
        check("gpu pool: the read map is the picture's extent rounded to pages, not the pool", bytes == expected);
        string_format(log, "  gpu pool: a picture of %p bytes in a pool of %p, the card's read map %p bytes\n", extent, (positive)POOL, bytes);
        while (gpu_map_count)
                gpu_drop(0, GPU_GONE_RECOVER);
        compositor.buffer = saved_buffer;
        system_call_2(syscall(munmap), (positive)pool, POOL);
        system_call_2(syscall(munmap), (positive)out, DBYTES);
}

/*
        The registers a compute job's IB sets, read from the words the fake submit kept (gpu_fake_ib): a job after another must not read the shared state the
        last one left, so the thread management of every shader engine (COMPUTE_STATIC_THREAD_MGMT_SE0 to SE3, dwords 0x216, 0x217, 0x219, 0x21a from the
        SH registers' base, gc_10_3_0_offset.h's mm minus 0x19a0) is all enabled, and COMPUTE_PGM_RSRC3 (0x228) is set, with the registers the job set
        before kept as they were. The self-test's hash is the same: it is drawn by the job whose packets are read here.
*/
static void gpu_preamble_rows(void)
{
        enum { FIRST = 0x200, REGISTERS = 0x80 };
        static const unsigned int mgmt[4] = {0x216, 0x217, 0x219, 0x21a};
        unsigned int value[REGISTERS], seen[REGISTERS], dispatches = 0, sets = 0, all = 0xffffffffu;
        b32 shaped, enabled = true;

        if (gpu.fd < 0 || gpu.broken)
        {
                check("gpu preamble: no card here, so no job's packets are read", true);
                return;
        }
        memory_zero(value, sizeof(value)), memory_zero(seen, sizeof(seen));
        gpu_fake_ib_words = 0;
        check("gpu preamble: the self-test is drawn right by the job whose packets are read", gpu_test_hash() == GPU_TEST_HASH);
        shaped = gpu_fake_ib_words > 0;
        for (unsigned int at = 0; shaped && at < gpu_fake_ib_words;)
        {
                unsigned int head = gpu_fake_ib[at], body = (head >> 16 & 0x3fff) + 1, op = head >> 8 & 255;

                if (head >> 30 != 3 || at + 1 + body > gpu_fake_ib_words)
                {
                        shaped = false;
                        break;
                }
                if (op == 0x76) // SET_SH_REG: the first dword is the register, the rest its value and those of the registers after it
                        for (unsigned int n = 1; n < body; n++)
                        {
                                unsigned int reg = gpu_fake_ib[at + 1] + n - 1;

                                if (reg - FIRST < REGISTERS)
                                        value[reg - FIRST] = gpu_fake_ib[at + 1 + n], seen[reg - FIRST]++, sets++;
                        }
                if (op == 0x15) // DISPATCH_DIRECT
                        dispatches++;
                at += 1 + body;
        }
        check("gpu preamble: the job's words are whole packets, one dispatch, and end before the arguments at byte 512",
              shaped && dispatches == 1 && gpu_fake_ib_words * 4 <= 512);
        for (unsigned int i = 0; i < 4; i++)
                enabled &= seen[mgmt[i] - FIRST] == 1 && value[mgmt[i] - FIRST] == all;
        check("gpu preamble: the thread management of all four shader engines is all enabled (0xffffffff, once each)", enabled);
        check("gpu preamble: COMPUTE_PGM_RSRC3 is set (0, no shared vector registers), once", seen[0x228 - FIRST] == 1 && value[0x228 - FIRST] == 0);
        check("gpu preamble: the registers the job set before are as they were (limits 0, tmpring 0x100, threads 64 by 1 by 1, program and user data once each)",
              seen[0x215 - FIRST] == 1 && value[0x215 - FIRST] == 0 && seen[0x218 - FIRST] == 1 && value[0x218 - FIRST] == 0x100 && seen[0x207 - FIRST] == 1 &&
              value[0x207 - FIRST] == 64 && value[0x208 - FIRST] == 1 && value[0x209 - FIRST] == 1 && seen[0x20c - FIRST] == 1 && seen[0x20d - FIRST] == 1 &&
              seen[0x212 - FIRST] == 1 && seen[0x213 - FIRST] == 1 && seen[0x240 - FIRST] == 1 && seen[0x245 - FIRST] == 1);
        string_format(log, "  gpu preamble: %p words, %p register writes in all\n", (positive)gpu_fake_ib_words, (positive)sets);
}

/*
        The card's hang and loss, held back: while the card cools after a hang, or a restart is due, or a submit said the card lost its state, a list is
        not drawn on the card (the context and the maps are the old ones); and a restart due waits while the job given up on may still run (a new context
        is not started on the queue that job is on). The card is up (gpu_loss_rows started it).
*/
static void gpu_held_rows(void)
{
        static unsigned int pixels[4 * 4];
        static unsigned int list[16];
        int damage[4] = {0, 0, 4, 4}, saved_gpu = compositor_gpu;
        __typeof__(compositor_gpu_state) saved = compositor_gpu_state;
        unsigned int recovered;

        if (gpu.fd < 0 || gpu.broken)
        {
                check("gpu held: no card here, so nothing is held back", true);
                return;
        }
        compositor_gpu = 1, compositor_gpu_state.up = 1, compositor_gpu_state.dead = 0, compositor_gpu_state.cool_until = 0;
        compositor_gpu_state.restart_due = 0, gpu_lost = 0, gpu_fake_busy = 0, gpu.pending = 0;
        compositor_gpu_state.cool_until = clock_monotonic_nanoseconds() + 1000000000ull;
        compositor_gpu_list_render_bytes(0, (unsigned char *)list, sizeof(list), pixels, 4, 4, 4, damage);
        check("gpu held: a list is not drawn on the card while the card cools after a hang", compositor_list_why == COMPOSITOR_LIST_CARD);
        // A list looks for a due restart and a lost card first (compositor_gpu_restart_check, compositor_gpu_lost_check): a restart waits for a job given up
        // on that may still run, and while it waits the list is not the card's; a lost card is started again at once, and the list is then looked at.
        compositor_gpu_state.cool_until = 0, compositor_gpu_state.restart_due = 1, gpu.pending = 1ull << 40, gpu_fake_busy = 1;
        compositor_gpu_list_render_bytes(0, (unsigned char *)list, sizeof(list), pixels, 4, 4, 4, damage);
        check("gpu held: nor while a restart is due and waits for a job that may still run (the context and the maps are the old ones)",
              compositor_list_why == COMPOSITOR_LIST_CARD && compositor_gpu_state.restart_due);
        gpu_fake_busy = 0, gpu.pending = 0, compositor_gpu_state.restart_due = 0, gpu_lost = 1, recovered = gpu_recovered;
        compositor_gpu_list_render_bytes(0, (unsigned char *)list, sizeof(list), pixels, 4, 4, 4, damage);
        check("gpu held: after a submit said the card lost its state the card is started again before the list is looked at (an empty one is not open)",
              !gpu_lost && gpu_recovered == recovered + 1 && compositor_list_why == COMPOSITOR_LIST_OPEN && gpu.fd >= 0);
        gpu_lost = 0;
        compositor_gpu_list_render_bytes(0, (unsigned char *)list, sizeof(list), pixels, 4, 4, 4, damage);
        check("gpu held: with nothing held the list is looked at (an empty list is refused as not open)", compositor_list_why == COMPOSITOR_LIST_OPEN);

        recovered = gpu_recovered;
        gpu.pending = 1ull << 40, gpu_fake_busy = 1; // a job the wait says is still running
        compositor_gpu_state.restart_due = 1, compositor_gpu_state.cool_until = 0;
        compositor_gpu_restart_check();
        check("gpu held: a restart due waits while the job given up on may still run", gpu_recovered == recovered && compositor_gpu_state.restart_due);
        gpu_fake_busy = 0, gpu.pending = 0;
        compositor_gpu_restart_check();
        check("gpu held: once that job is over the restart happens, and the card is up", gpu_recovered == recovered + 1 && !compositor_gpu_state.restart_due &&
              gpu.fd >= 0);
        recovered = gpu_recovered;
        gpu_lost = 1;
        compositor_gpu_lost_check();
        check("gpu held: a card that lost its state is started again before the next present draws on it", gpu_recovered == recovered + 1 && !gpu_lost &&
              gpu.fd >= 0);

        compositor_gpu_state = saved;
        compositor_gpu = saved_gpu;
        gpu_fake_busy = 0, gpu.pending = 0, gpu_lost = 0;
}

/*
        The card's map records, its fault and its hang, with no card: the table is set by hand (fake handles, the card's fd absent), so each row is the
        compositor's own answer. A record handed an address that a written map would let go of is refused (9) and kept until the job has run; a
        map let go of is named by why and how long ago; a fault's address is named by what it was in; the kernel's log lines are read as the fault is
        read; an import is not unmapped while a job may read it (a rotated screen); a hang cools the card, and a second within a minute gives it up.
*/
static b32 gpu_text_has(const char *text, const char *word)
{
        return string_find((string_address)text, (string_address)word) != 0;
}

static void gpu_fault_rows(void)
{
        static struct windows *board_scratch;
        struct windows *const board_at = windows_scratch_table(&board_scratch);
        struct windows_issue want[WINDOWS_GPU_CODES];
        unsigned int count;
        positive value = 0;
        int saved_card = compositor.card, saved_gpu = compositor_gpu;
        unsigned int *saved_scanout = compositor.scanout;
        unsigned long saved_bytes = compositor.scanout_bytes;

        gpu.fd = -1, gpu.pending = 0, gpu_fake_busy = 0, gpu_tick = 100, gpu_map_count = 0, gpu.imports = 0, gpu_gone_at = 0;
        gpu_map_next = GPU_VA_MAPS;

        // 1. A written map over a read map whose address a record holds: refused (9), the read map kept; once the job has run it is let go of (overlap).
        gpu_maps[0].from = 0x10000000ull, gpu_maps[0].to = 0x10004000ull, gpu_maps[0].va = GPU_VA_MAPS, gpu_maps[0].bytes = 0x4000;
        gpu_maps[0].handle = 7, gpu_maps[0].used = gpu_tick, gpu_maps[0].written = 0, gpu_maps[0].dma = 0, gpu_maps[0].handed = 0, gpu_maps[0].offset = 0;
        gpu_map_count = 1;
        check("gpu hazard: a read map's address is handed to a record", gpu_va_bytes((const void *)0x10000100ull, 4) == GPU_VA_MAPS + 0x100 && gpu_maps[0].handed);
        check("gpu hazard: a written map over a read map a record holds is refused (9), and the read map is kept",
              !gpu_map_written((const void *)0x10002000ull, 0x1000) && (gpu_map_why & 255) == 9 && gpu_map_count == 1 && gpu_maps[0].to == 0x10004000ull &&
              gpu_maps[0].va == GPU_VA_MAPS);
        gpu_handed_clear();
        check("gpu hazard: once no record waits for a job, the same written map drops the read map (overlap), not the record's",
              !gpu_map_written((const void *)0x10002000ull, 0x1000) && gpu_map_count == 0 && gpu_gone[(gpu_gone_at - 1) % GPU_GONE].why == GPU_GONE_OVERLAP &&
              gpu_gone[(gpu_gone_at - 1) % GPU_GONE].va == GPU_VA_MAPS);

        // 2. The fault's address is named: a map let go of (its reason and ticks), a held map (handle, span, use), a forgotten one, an import, none.
        gpu_fault_say(GPU_VA_MAPS + 0x180);
        check("gpu fault: an address in a map let go of is named by its reason and the ticks since",
              gpu_text_has(gpu_fault, "map handle 7 dropped 0 ticks ago by overlap with a written map"));
        gpu_maps[0].from = 0x20000000ull, gpu_maps[0].to = 0x20002000ull, gpu_maps[0].va = GPU_VA_MAPS + 0x100000ull, gpu_maps[0].bytes = 0x2000;
        gpu_maps[0].handle = 9, gpu_maps[0].used = gpu_tick - 3, gpu_maps[0].written = 1, gpu_maps[0].dma = 1;
        gpu_map_count = 1;
        gpu_fault_say(GPU_VA_MAPS + 0x100010ull);
        check("gpu fault: an address in a held map is named with its handle, route, access, span and use",
              gpu_text_has(gpu_fault, "held map, handle 9, dma-buf, written, from 0x20000000 to 0x20002000, used 3 ticks ago"));
        gpu_maps[0].from = gpu_maps[0].to = 0;
        gpu_fault_say(GPU_VA_MAPS + 0x100010ull);
        check("gpu fault: a map forgotten (its pages let go of) and still mapped is named as forgotten", gpu_text_has(gpu_fault, "forgotten, used 3 ticks ago"));
        gpu_map_count = 0;
        gpu.imports = 1, gpu.imported[0] = 5, gpu.imported_va[0] = 0x40000000ull, gpu.imported_bytes[0] = 8192, gpu.imported_dead[0] = 0;
        gpu_fault_say(0x40000010ull);
        check("gpu fault: an address in a held import is named with its handle", gpu_text_has(gpu_fault, "held import, handle 5, live"));
        gpu.imports = 0;
        gpu_fault_say(0x1234ull);
        check("gpu fault: an address in no map the table made is named as none", gpu_text_has(gpu_fault, "in no map or import the table ever made"));

        // 2b. The log's tail kept open (gpu_kmsg_tail, E-h4-2): a fault's read takes the records written since its last read, and keeps the first fault
        // after the floor across reads; a record at or under the floor is not one. A pipe stands for the log, one record at a time.
        {
                int ends[2] = {-1, -1};
                long kept_tail = gpu_kmsg_tail;
                b32 kept_ready = gpu_kmsg_seq_ready;
                positive kept_floor = gpu_kmsg_floor_seq, kept_seen = gpu_kmsg_seen_seq, kept_since = gpu_kmsg_last_since;
                struct gpu_fault_scan scan;
                static const char old_fault[] = "3,9,5000,-;amdgpu: in page starting at address 0x0000100000000000 from client 0x1b\n";
                static const char new_fault[] = "3,11,6000,-;amdgpu: in page starting at address 0x0000203fd75000 from client 0x1b\n";
                b32 below, found, kept_across;

                compositor_call(syscall(pipe2), (positive)ends, 0x800 | 0x80000, 0, 0, 0, 0);
                gpu_kmsg_tail = ends[0], gpu_kmsg_seq_ready = 1, gpu_kmsg_floor_seq = 10, gpu_kmsg_seen_seq = 10, gpu_kmsg_last_since = 77;
                gpu_fault_pending_new = 1;
                system_write_once(ends[1], old_fault, sizeof old_fault - 1);
                gpu_fault_read(77, &scan);
                below = !scan.have_at;
                system_write_once(ends[1], new_fault, sizeof new_fault - 1);
                gpu_fault_read(77, &scan);
                found = scan.have_at && scan.at == 0x203fd75000ull && gpu_kmsg_seen_seq == 11;
                gpu_fault_read(77, &scan);
                kept_across = scan.have_at && scan.at == 0x203fd75000ull;
                check("gpu fault: the log's tail is read from where it was, a record under the floor is no fault, and the first after it is kept across reads",
                      below && found && kept_across);
                system_close(ends[0]), system_close(ends[1]);
                gpu_kmsg_tail = kept_tail, gpu_kmsg_seq_ready = kept_ready, gpu_kmsg_floor_seq = kept_floor, gpu_kmsg_seen_seq = kept_seen;
                gpu_kmsg_last_since = kept_since, gpu_fault_pending_new = 1;
        }

        // 3. The kernel's log: a record's time, the address after its key (hexadecimal), the status words.
        check("gpu fault: a kernel log record's time is its third field (microseconds)", gpu_kmsg_usec("6,1234,5000000,-;amdgpu: x") == 5000000);
        check("gpu fault: the page fault's address is read after its key, in hexadecimal",
              gpu_key_hex("amdgpu 0000:04:00.0: amdgpu:  in page starting at address 0x0000203fd75000 from client 0x1b", "in page starting at address 0x", &value) &&
              value == 0x203fd75000ull);
        check("gpu fault: a status word is read (zero included), and an absent key is not",
              gpu_key_hex("PERMISSION_FAULTS: 0x3 MAPPING_ERROR: 0x0 RW: 0x0", "MAPPING_ERROR: 0x", &value) && value == 0 && !gpu_key_hex("nothing", "RW: 0x", &value));
        gpu_job_began = clock_monotonic_nanoseconds(), gpu_fault[0] = 0, gpu_job_error = 62;
        gpu_fault_note(true);
        check("gpu fault: a hang with no page fault in the log since it was submitted says so, with its error",
              gpu_text_has(gpu_fault, "the card's job ended with error 62: the kernel's log has no page fault since it was submitted"));

        // 4. The scanout the card writes (headless): its address is a written map's, never a read map's (a write through a read map faults).
        compositor.card = -1, compositor.scanout = (unsigned int *)0x10000000ull, compositor.scanout_bytes = 0x4000;
        gpu_maps[0].from = 0x10000000ull, gpu_maps[0].to = 0x10004000ull, gpu_maps[0].va = GPU_VA_MAPS, gpu_maps[0].bytes = 0x4000;
        gpu_maps[0].written = 0, gpu_maps[0].handed = 0, gpu_maps[0].used = gpu_tick;
        gpu_map_count = 1;
        check("gpu hazard: the headless scanout under a read map has no card address (no write through the read map)", compositor_gpu_output() == 0);
        gpu_map_count = 0;
        compositor.card = saved_card, compositor.scanout = saved_scanout, compositor.scanout_bytes = saved_bytes;

        // 5. A screen turned while the card is busy: the import of its buffer is not let go of (a job may still read it).
        {
                int screen[4] = {0, 0, 8, 8};

                gpu.imports = 1, gpu.imported[0] = 5, gpu.imported_va[0] = 0x40000000ull, gpu.imported_bytes[0] = 8192, gpu.imported_dead[0] = 0;
                compositor.card = 3, compositor_gpu_state.up = 1, compositor_gpu_state.dead = 0, compositor_gpu_state.out = 0x50000000ull;
                compositor_gpu_state.turn_va[0] = 0x40000000ull, compositor_gpu_state.turn_key[0] = 0;
                compositor_turns[0].handle = 42;
                gpu.pending = 77, gpu_fake_busy = 1;
                check("gpu hazard: a turned screen does not let its import go while a job may still read it",
                      !compositor_gpu_rotate(0, screen, 90, 0, 0, 8, 8) && gpu.imports == 1 && compositor_gpu_state.turn_va[0] == 0x40000000ull);
                gpu.pending = 0, gpu_fake_busy = 0, gpu.imports = 0;
                compositor_gpu_state.turn_va[0] = 0, compositor_gpu_state.turn_key[0] = 0, compositor_gpu_state.out = 0;
                compositor.card = saved_card;
        }

        // 6. The circuit breaker: one hang cools the card (the processor draws) and marks it to start again with no maps; a second within a minute gives
        // it up for the session, and the board says so.
        compositor_gpu = 1, compositor_gpu_state.up = 1, compositor_gpu_state.dead = 0, compositor_gpu_state.failures = 0, compositor_gpu_state.cool_until = 0;
        compositor_gpu_state.hung_at = 0, compositor_gpu_state.hung_off = 0, compositor_gpu_state.restart_due = 0, compositor_gpu_state.cool_steps = 0;
        gpu_last_error = 62, gpu_end = GPU_END_HUNG;
        compositor_gpu_job_failed();
        check("hang: one hang cools the card for two seconds, and marks it to start again with no maps",
              compositor_gpu_state.cool_until > clock_monotonic_nanoseconds() && compositor_gpu_state.restart_due && !compositor_gpu_state.dead &&
              !compositor_gpu_state.hung_off);
        check("hang: while it cools, a present is the processor's and the card is not tried", compositor_gpu_draw(0, 0) == COMPOSITOR_GPU_WHY_DEAD);
        gpu_last_error = 62, gpu_end = GPU_END_HUNG;
        compositor_gpu_job_failed();
        check("hang: a second hang within a minute gives the card up for the session", compositor_gpu_state.dead && compositor_gpu_state.hung_off &&
              compositor_gpu_state.dead_why == COMPOSITOR_GPU_WHY_HANG && !compositor_gpu_state.restart_due);
        check("hang: a present after it is the processor's for good", compositor_gpu_draw(0, 0) == COMPOSITOR_GPU_WHY_HANG);
        windows_scratch_zero(board_at);
        board_at->gpu_on = 1, board_at->gpu_state = WINDOWS_GPU_HUNG;
        count = windows_gpu_wanted(board_at, want, WINDOWS_GPU_CODES);
        check("hang: the board says the card is off for the session", count && want[0].code == WINDOWS_ISSUE_GPU_HUNG);
        windows_scratch_zero(board_at);
        board_at->gpu_on = 1, board_at->gpu_state = WINDOWS_GPU_UP, board_at->gpu_faults = 1;
        string_copy_bounded((string_address)board_at->gpu_fault, (string_address)"card faulted reading 0x203fd75000: a map let go of", sizeof(board_at->gpu_fault));
        count = windows_gpu_wanted(board_at, want, WINDOWS_GPU_CODES);
        check("fault: the board carries the card's fault text", count && want[0].code == WINDOWS_ISSUE_GPU_FAULT && gpu_text_has(want[0].detail, "card faulted reading 0x203fd75000"));
        compositor_gpu_state.dead = 0, compositor_gpu_state.hung_off = 0, compositor_gpu_state.cool_until = 0, compositor_gpu_state.hung_at = 0;
        compositor_gpu_state.restart_due = 0, compositor_gpu_state.failures = 0, compositor_gpu_state.cool_steps = 0, compositor_gpu_state.up = 0;
        // 6b. A self-test after the card was started again waits half a second a pass on the loop, not the hang's two seconds (gpu_patience).
        {
                b32 saved_patient = gpu_patient;
                unsigned int saved_recovered = gpu_recovered;
                positive first, again, tick;

                gpu_recovered = 0, gpu_patient = 0, tick = gpu_patience();
                gpu_patient = gpu_recovered ? 2 : 1, first = gpu_patience();
                gpu_recovered = 1, gpu_patient = gpu_recovered ? 2 : 1, again = gpu_patience();
                check("gpu recover: the first start's self-test waits a hang's two seconds, one after a restart half a second, a frame's job a tick",
                      first == GPU_HANG_NS && again == GPU_RECOVER_NS && tick == GPU_LATE_NS && again < GPU_HANG_NS);
                gpu_patient = saved_patient, gpu_recovered = saved_recovered;
        }
#ifdef CONFIG_MOONWATER_WAYLAND
        // 7. Hangs whose fault is in a client's import (row 2's source, client slot 1): the client is barred from the card and the card is kept for the rest,
        // however many such hangs, below COMPOSITOR_GPU_BLAMED_MOST in a minute; a hang in nobody's buffer is the card's as before, and a new connection in
        // the slot starts unbarred.
        {
                int saved_started = compositor_wl.started;
                positive saved_va = compositor_wl.source[2].va;
                b32 kept, barred, others, fresh, card;

                compositor_wl.started = 1;
                kept = compositor_wl_slots_reach(2) && wayland_rows_reach(2); // the slots first: they give the shim's tables their host
                wayland.row[2].client = 1, compositor_wl.barred[1] = 0, compositor_wl.source[2].va = 0x40000000ull;
                gpu.imports = 1, gpu.imported[0] = 5, gpu.imported_va[0] = 0x40000000ull, gpu.imported_bytes[0] = 8192, gpu.imported_dead[0] = 0;
                compositor_gpu = 1, compositor_gpu_state.up = 1, compositor_gpu_state.blamed_at = 0, compositor_gpu_state.blamed = 0;
                for (unsigned int i = 0; i < 3; i++)
                {
                        gpu_fault_say(0x40000010ull);
                        gpu_last_error = 62, gpu_end = GPU_END_HUNG;
                        compositor_gpu_job_failed();
                }
                barred = compositor_wl_card_barred(2) && !compositor_wl_card_barred(3);
                kept = kept && !compositor_gpu_state.dead && !compositor_gpu_state.hung_off && compositor_gpu_state.restart_due && !compositor_gpu_state.hung_at;
                compositor_wl.barred[1] = 0;
                fresh = !compositor_wl_card_barred(2);
                gpu.imports = 0;
                gpu_fault_say(0x40000010ull);
                others = !gpu_fault_import;
                gpu_last_error = 62, gpu_end = GPU_END_HUNG;
                compositor_gpu_job_failed();
                gpu_last_error = 62, gpu_end = GPU_END_HUNG;
                compositor_gpu_job_failed();
                card = compositor_gpu_state.hung_off && !compositor_wl_card_barred(2);
                check("hang: three hangs in a minute in a client's import bar that client from the card and keep the card; one in no import is the card's again",
                      kept && barred && fresh && others && card);
                wayland.row[2].client = -1, compositor_wl.source[2].va = saved_va, compositor_wl.started = saved_started;
                compositor_gpu_state.dead = 0, compositor_gpu_state.hung_off = 0, compositor_gpu_state.cool_until = 0, compositor_gpu_state.hung_at = 0;
                compositor_gpu_state.restart_due = 0, compositor_gpu_state.failures = 0, compositor_gpu_state.cool_steps = 0, compositor_gpu_state.up = 0;
                compositor_gpu_state.blamed_at = 0, compositor_gpu_state.blamed = 0;
        }
#endif
        compositor_gpu = saved_gpu, gpu_last_error = 0, gpu_end = GPU_END_NONE, gpu_map_count = 0, gpu_fault[0] = 0;
}

/*
        The buffers a submit names (gpu_run_job's bos). A record gets a map's address from gpu_va_bytes or gpu_va_written, which mark the map handed, and the mark holds until a
        job has run (gpu_run): every map a record was given is in the list whatever the ticks between the lookup and the run (a trim moves the tick on). The submit is the
        fake's (answered ECANCELED before the call is made, with no card), so the list is what the hook saw.
*/
static b32 gpu_seen_has(unsigned int handle)
{
        for (unsigned int i = 0; i < gpu_seen_count; i++)
                if (gpu_seen_bos[i] == handle)
                        return true;
        return false;
}

// One job by the fake submit (gpu_run, which lets the marks go after it); true when every map handed to a record before it is in the list it named.
static b32 gpu_bo_run(void)
{
        unsigned int owed[256], owing = 0;
        b32 held = true;

        for (unsigned int i = 0; i < gpu_map_count; i++)
                if (gpu_maps[i].handed)
                        owed[owing++] = gpu_maps[i].handle;
        gpu_fake_cancel = 1, gpu_seen_count = 0;
        GPU_ARGS->w = 64, GPU_ARGS->rows = 1; // a job of no columns or rows is refused as our own before the kernel is asked (gpu_amd_submit)
        gpu_run();
        gpu_fake_cancel = 0, gpu_lost = 0;
        for (unsigned int i = 0; i < owing; i++)
                held &= gpu_seen_has(owed[i]);
        return held;
}

// Map i of the table by hand: a handle, a span of 0x4000 bytes from from at an address of its own, last used in the tick used, held by no record.
static void gpu_bo_map(unsigned int i, unsigned int handle, positive from, unsigned int used)
{
        memory_zero(&gpu_maps[i], sizeof(gpu_maps[i]));
        gpu_maps[i].from = from, gpu_maps[i].to = from + 0x4000, gpu_maps[i].va = GPU_VA_MAPS + 0x10000ull * i, gpu_maps[i].bytes = 0x4000;
        gpu_maps[i].handle = handle, gpu_maps[i].used = used;
}

static void gpu_bo_rows(void)
{
        static unsigned int scratch[1024]; // the job's packets and arguments are written at the card's buffer: a buffer of the row's, since there is no card
        __typeof__(gpu.at) kept_at = gpu.at;
        int kept_fd = gpu.fd;
        const void *first = (const void *)0x10000100ull;
        b32 held;

        gpu.at = scratch, gpu.fd = -1, gpu.pending = 0, gpu_fake_busy = 0, gpu_fake_idle = 0, gpu_tick = 100, gpu_map_count = 0, gpu.imports = 0, gpu_gone_at = 0;
        gpu_map_next = GPU_VA_MAPS, gpu_last_error = 0;
        gpu_bo_map(0, 7, 0x10000000ull, 100);
        gpu_bo_map(1, 8, 0x20000000ull, 95);
        gpu_map_count = 2;

        // 1. A record is given the first map's address, the present is over (gpu_trim: the tick moves on, the marks of the tick are let go of by the old rule) and
        // the job runs after it: the map is in the list.
        check("gpu bo: a map's address is handed to a record", gpu_va_bytes(first, 4) == GPU_VA_MAPS + 0x100 && gpu_maps[0].handed);
        gpu_trim();
        held = gpu_bo_run();
        check("gpu bo: a map looked up, then a trim, then the job, is in the bo list", gpu_seen_has(7));
        check("gpu bo: every map a record was given before the job is in its list", held);
        check("gpu bo: a map no record was given and not used in this tick is not in the list", !gpu_seen_has(8));
        check("gpu bo: a job marks the maps its list named with its serial, and no other", gpu_maps[0].listed == gpu_submit_serial && gpu_maps[1].listed != gpu_submit_serial);

        // 2. The job has run: the records are over, and the map is let go of. The job a tick later names neither map.
        check("gpu bo: once the job has run the map's mark is let go of", !gpu_maps[0].handed);
        gpu_trim();
        gpu_bo_run();
        check("gpu bo: a job a tick later names no map nobody was given in between", !gpu_seen_has(7) && !gpu_seen_has(8));

        // 3. Looked up and run in one tick (no trim between): named, as before.
        check("gpu bo: a map looked up in the tick of the job is handed again", gpu_va_bytes(first, 4) == GPU_VA_MAPS + 0x100 && gpu_maps[0].handed);
        held = gpu_bo_run();
        check("gpu bo: a map looked up and run in one tick is in the list", gpu_seen_has(7) && held && !gpu_seen_has(8));

        // 4. A map the compositor forgot (its pages are gone; the trim keeps it while a job given up on may run). Given to a record in an earlier tick, it is not named (the
        // kernel refuses a job that names pages that are gone, and the record of an earlier tick is not run again); given to one in this tick, it is (that job is refused).
        gpu_maps[1].from = gpu_maps[1].to = 0, gpu_maps[1].handed = 1, gpu_maps[1].used = gpu_tick - 1, gpu_maps_handed[0] |= 2; // (the submit reads the marks' bits)
        gpu_bo_run();
        check("gpu bo: a forgotten map handed in an earlier tick is not in the list", !gpu_seen_has(8));
        gpu_maps[1].handed = 1, gpu_maps[1].used = gpu_tick, gpu_maps_handed[0] |= 2;
        held = gpu_bo_run();
        check("gpu bo: a forgotten map handed in this tick is in the list (the kernel refuses that job)", gpu_seen_has(8) && held);

        // 5. The mark does not hold off a written map in a tick after its lookup (the old rule's meaning): the processor would otherwise draw until a job ran, and none runs
        // while it does.
        gpu_bo_map(0, 7, 0x10000000ull, gpu_tick);
        gpu_map_count = 1;
        check("gpu bo: a map is handed to a record", gpu_va_bytes(first, 4) == GPU_VA_MAPS + 0x100 && gpu_maps[0].handed);
        check("gpu bo: in the tick of the lookup a written map over it is refused (9)", !gpu_map_written((const void *)0x10002000ull, 0x1000) && (gpu_map_why & 255) == 9 && gpu_map_count == 1);
        gpu_trim();
        check("gpu bo: a tick on, with no job run, a written map over it is not refused (9) and drops it",
              !gpu_map_written((const void *)0x10002000ull, 0x1000) && (gpu_map_why & 255) != 9 && gpu_map_count == 0);

        // 6. The imports: one held and given to a record, one let go of since the last submit (a record of this job may hold its address), one let go of before
        // it (the client draws into it again: named, the job would wait for the client's frame and the client's next frame for the job). The job names the first
        // two; the one after it in the same tick the held one alone.
        if (gpu_imports_room(5))
        {
                gpu_map_count = 0, gpu.imports = 3;
                for (unsigned int i = 0; i < 5; i++)
                        gpu.imported[i] = 21 + i, gpu.imported_va[i] = 0x40000000ull + 0x10000ull * i, gpu.imported_bytes[i] = 0x10000, gpu.imported_holds[i] = 1,
                        gpu.imported_dead[i] = i == 1 || i == 2, gpu.imported_dead_at[i] = gpu_tick, gpu.imported_dead_after[i] = gpu_submit_serial - (i == 2),
                        gpu.imported_own[i] = i == 4, gpu.imported_handed[i] = 0, gpu.imported_used[i] = 0;
                gpu_import_handed(0x40000010ull);
                gpu_bo_run();
                check("gpu bo: a held import given to a record and one let go of since the last submit are in the list", gpu_seen_has(21) && gpu_seen_has(22));
                check("gpu bo: an import let go of before the last submit is not (its client draws into it again)", !gpu_seen_has(23));
                gpu_bo_run();
                check("gpu bo: the job after in the same tick, given no import's address, names none of the client's (a8720e4ae: handed alone, not used in the tick)",
                      !gpu_seen_has(21) && !gpu_seen_has(22) && !gpu_seen_has(23) && gpu_seen_count == 0);

                // 7. A client's import no record of this tick was given is not named (a game's buffer it draws its next frame into, which amdgpu would make the
                // job wait for and the game wait for the job: P-c1-1, G-g3-4); the compositor's own (a scanout) always is; gpu-imports-all names every live one.
                gpu.imports = 5;
                gpu.imported_dead[1] = gpu.imported_dead[2] = 0;
                gpu_trim();
                gpu_import_handed(0x40020010ull);
                gpu_bo_run();
                check("gpu bo: a tick on, only the client import a record was given and the compositor's own are named",
                      gpu_seen_has(23) && gpu_seen_has(25) && !gpu_seen_has(21) && !gpu_seen_has(22) && !gpu_seen_has(24) && gpu_seen_count == 2);
                gpu_imports_all = 1;
                gpu_bo_run();
                gpu_imports_all = 0;
                check("gpu bo: with gpu-imports-all every live import is named, as before", gpu_seen_count == 5);
                gpu_import_handed(0x50000000ull);
                check("gpu bo: an address in no import marks none", !gpu.imported_handed[0] && !gpu.imported_handed[1] && !gpu.imported_handed[3]);
                gpu.imports = 0;
        }
        else
                check("gpu bo: room for five imports", false);

        // 8. amdgpu's EINVAL (GPU_ERR_SUSPECT): once, the job is refused and asked again, the card not lost; twice in a row, the card lost (E-h6-2).
        {
                const struct gpu_backend *kept_backend = gpu_backend;

                gpu_backend = &gpu_amd, gpu_map_count = 0, gpu.imports = 0, gpu_suspects = 0;
                gpu_fake_invalid = 1, gpu_lost = 0, GPU_ARGS->w = 64, GPU_ARGS->rows = 1;
                gpu_run();
                held = !gpu_lost && gpu_fake_invalid == 0;
                gpu_fake_invalid = 2, gpu_lost = 0, gpu_suspects = 0;
                gpu_run();
                check("gpu submit: an EINVAL is asked again and not the card lost; two in a row are", held && gpu_lost && gpu_fake_invalid == 0);
                gpu_fake_invalid = 0, gpu_lost = 0, gpu_suspects = 0, gpu_backend = kept_backend;
        }

        gpu_map_count = 0, gpu_gone_at = 0, gpu_seen_count = 0, gpu_fake_cancel = 0, gpu_lost = 0, gpu_last_error = 0, gpu_fault[0] = 0;
        gpu.at = kept_at, gpu.fd = kept_fd;
}

/*
        The card's fault by the kernel's query (AMDGPU_INFO_GPUVM_FAULT, answered by the fake: gpu_fake_vm) and the kernel's log lines, with no card: the line gpu_fault says
        names the map or import that owns the address and whether the failed job's bo list (the serial of the last submit) had the map; the query's status word and hub, and
        the log's client, are kept (gpu_fault_last) and said; a kernel without the query leaves the log's text as it was.
*/
static b32 gpu_text_is(const char *text, const char *want)
{
        return string_equals((string_address)text, (string_address)want);
}

// A hang noted (gpu_fault_note) with the fake kernel's fault: at, the status word and the hub.
static void gpu_vm_note(positive at, unsigned int status, unsigned int hub)
{
        gpu_fake_vm = 1, gpu_fake_vm_addr = at, gpu_fake_vm_status = status, gpu_fake_vm_hub = hub;
        gpu_job_began = clock_monotonic_nanoseconds(), gpu_job_error = 62, gpu_fault[0] = 0;
        gpu_fault_note(true);
        gpu_fake_vm = 0;
}

// The first fault in a log, line by line (gpu_fault_line), in the kernel's own words; the client's line is client.
static void gpu_vm_log(struct gpu_fault_scan *scan, const char *client)
{
        memory_zero(scan, sizeof(*scan));
        gpu_fault_line("amdgpu 0000:04:00.0: amdgpu:  in page starting at address 0x0000002000000180 from client 0x1b (UTCL2)", scan);
        gpu_fault_line("GCVM_L2_PROTECTION_FAULT_STATUS:0x00701031", scan);
        gpu_fault_line(client, scan);
        gpu_fault_line("\t MORE_FAULTS: 0x1", scan);
        gpu_fault_line("\t PERMISSION_FAULTS: 0x3", scan);
        gpu_fault_line("\t MAPPING_ERROR: 0x0", scan);
        gpu_fault_line("\t RW: 0x0", scan);
}

static void gpu_vmfault_rows(void)
{
        struct gpu_fault_scan scan;
        const unsigned int failed = 5, kept_epoch = gpu_epoch; // the failed job is the fifth submit
        const positive in_named = GPU_VA_MAPS + 0x180, in_unnamed = GPU_VA_MAPS + 0x10040, in_gone = GPU_VA_MAPS + 0x20010, in_unlisted = GPU_VA_MAPS + 0x30010;

        gpu.fd = -1, gpu.pending = 0, gpu_fake_busy = 0, gpu_tick = 100, gpu_map_count = 0, gpu.imports = 0, gpu_gone_at = 0, gpu_map_next = GPU_VA_MAPS;
        gpu_submit_serial = failed;
        gpu_bo_map(0, 7, 0x10000000ull, 100), gpu_maps[0].listed = failed; // in the failed job's list
        gpu_bo_map(1, 8, 0x20000000ull, 100), gpu_maps[1].listed = failed - 1; // in the one before only
        gpu_bo_map(2, 9, 0x30000000ull, 100), gpu_maps[2].listed = failed; // dropped after the failed job named it
        gpu_bo_map(3, 10, 0x40000000ull, 100), gpu_maps[3].listed = failed - 2; // dropped without the failed job naming it
        gpu_map_count = 4;
        gpu_drop(3, GPU_GONE_EVICT);
        gpu_drop(2, GPU_GONE_TRIM);
        memory_zero(&gpu_fault_last, sizeof(gpu_fault_last));

        // 1. A map the failed job's list named: the map, that its list had it, and the query's status word and hub.
        gpu_vm_note(in_named, 0x00701031u, 0);
        check("gpu vm fault: an address in a map the job's list named is said with the map, the list, the status word and the hub",
              gpu_text_is(gpu_fault, "card faulted reading 0x2000000180: held map, handle 7, userptr, read, from 0x10000000 to 0x10004000, used 0 ticks ago, listed in the job's bo list; "
                                     "status 0x701031, hub 0 (graphics)"));
        check("gpu vm fault: the query's address, status word and hub are kept, and it is not a repeat",
              gpu_fault_last.have_query && gpu_fault_last.query_at == in_named && gpu_fault_last.query_status == 0x701031u && gpu_fault_last.query_hub == 0 && !gpu_fault_last.again &&
              !gpu_fault_last.have_at);

        // 2. A map the list did not name.
        gpu_vm_note(in_unnamed, 0x00701031u, 0);
        check("gpu vm fault: an address in a map the job's list did not name says the list missed it",
              gpu_text_is(gpu_fault, "card faulted reading 0x2000010040: held map, handle 8, userptr, read, from 0x20000000 to 0x20004000, used 0 ticks ago, missing from the job's bo list; "
                                     "status 0x701031, hub 0 (graphics)"));

        // 3. A map that was dropped: the owner from the ring of the maps let go of, why and when, and whether the failed job's list had named it.
        gpu_vm_note(in_gone, 0x00701031u, 2);
        check("gpu vm fault: an address in a map dropped after the job named it says so, with the reason, and the media hub",
              gpu_text_is(gpu_fault, "card faulted reading 0x2000020010: map handle 9 dropped 0 ticks ago by the 120-tick trim, listed in the job's bo list; status 0x701031, hub 2 (media)"));
        gpu_vm_note(in_unlisted, 0x00701031u, 1);
        check("gpu vm fault: an address in a map dropped without the job's list naming it says the list missed it",
              gpu_text_is(gpu_fault, "card faulted reading 0x2000030010: map handle 10 dropped 0 ticks ago to make room (every map held), missing from the job's bo list; "
                                     "status 0x701031, hub 1 (media)"));

        // 4. A map the compositor forgot but the table keeps (a job given up on may run): the list's words come with it.
        gpu_bo_map(2, 11, 0x50000000ull, 100), gpu_maps[2].listed = failed, gpu_map_count = 3;
        gpu_forget((const void *)0x50000000ull, 0x1000);
        gpu_vm_note(GPU_VA_MAPS + 0x20000, 0x00701031u, 0);
        check("gpu vm fault: an address in a forgotten map says it was forgotten, and that the list had it",
              gpu_text_is(gpu_fault, "card faulted reading 0x2000020000: held map, handle 11, userptr, read, from 0x0 to 0x0, forgotten, used 0 ticks ago, listed in the job's bo list; "
                                     "status 0x701031, hub 0 (graphics)"));
        gpu_map_count = 2;

        // 5. A kernel without the query (the fake answers EINVAL), and a kernel that keeps no fault (it answers zeros): the log's text as it was.
        memory_zero(&gpu_fault_last, sizeof(gpu_fault_last));
        gpu_fake_vm = -1, gpu_job_began = clock_monotonic_nanoseconds(), gpu_job_error = 62, gpu_fault[0] = 0;
        gpu_fault_note(true);
        gpu_fake_vm = 0;
        check("gpu vm fault: a kernel without the query leaves the line the job's error alone, with no fault in the log",
              gpu_text_is(gpu_fault, "the card's job ended with error 62: the kernel's log has no page fault since it was submitted") && !gpu_fault_last.have_query);
        gpu_vm_note(0, 0, 0);
        check("gpu vm fault: a kernel that keeps no fault (zeros) leaves it too", gpu_text_is(gpu_fault, "the card's job ended with error 62: the kernel's log has no page fault since it was submitted") &&
                                                                                    !gpu_fault_last.have_query);
        gpu_vm_log(&scan, "\t Faulty UTCL2 client ID: TCP (0x8)");
        gpu_fault_text(&scan);
        check("gpu vm fault: with the log's address and no query the line is the log's, with the list's words and the client",
              gpu_text_is(gpu_fault, "card faulted reading 0x2000000180: held map, handle 7, userptr, read, from 0x10000000 to 0x10004000, used 0 ticks ago, listed in the job's bo list; client TCP "
                                     "(MAPPING_ERROR 0, PERMISSION_FAULTS 3, RW 0)"));

        // 6. The log's client, in the words the kernel prints (the number after the name is not part of it).
        check("gpu vm fault: the client TCP is read from its line", gpu_text_is(scan.client, "TCP") && scan.have_at && scan.at == in_named);
        gpu_vm_log(&scan, "\t Faulty UTCL2 client ID: SQC (data) (0x9)");
        check("gpu vm fault: a client with a word in brackets keeps it (SQC (data))", gpu_text_is(scan.client, "SQC (data)"));
        gpu_vm_log(&scan, "\t Faulty UTCL2 client ID: CPC (0x5)");
        check("gpu vm fault: the client CPC is read from its line", gpu_text_is(scan.client, "CPC"));
        gpu_vm_log(&scan, "\t Faulty UTCL2 client ID: Reserved");
        check("gpu vm fault: a client with no number after it is read whole", gpu_text_is(scan.client, "Reserved"));
        gpu_vm_log(&scan, "\t not a client line");
        check("gpu vm fault: no client line, no client", scan.client[0] == 0 && scan.have_mapping && scan.have_permission && scan.have_rw);

        // 7. The log's address and the query's together: the log's is the job's (bounded by its time), the query's status word and hub are said beside it.
        gpu_vm_log(&scan, "\t Faulty UTCL2 client ID: TCP (0x8)");
        scan.have_query = 1, scan.query_at = in_unnamed, scan.query_status = 0x00701031u, scan.query_hub = 0;
        gpu_fault_text(&scan);
        check("gpu vm fault: the log's address names the owner when the query has another, and the query's words follow",
              gpu_text_is(gpu_fault, "card faulted reading 0x2000000180: held map, handle 7, userptr, read, from 0x10000000 to 0x10004000, used 0 ticks ago, listed in the job's bo list; "
                                     "status 0x701031, hub 0 (graphics); client TCP (MAPPING_ERROR 0, PERMISSION_FAULTS 3, RW 0)"));

        // 8. A fault of the address space that was survived is read again by a later job that failed for another reason: the second read says it is the same.
        memory_zero(&gpu_fault_last, sizeof(gpu_fault_last));
        gpu_vm_note(in_named, 0x00701031u, 0);
        check("gpu vm fault: the first read of a fault is not a repeat", !gpu_fault_last.again && !gpu_text_has(gpu_fault, "the same fault"));
        gpu_vm_note(in_named, 0x00701031u, 0);
        check("gpu vm fault: the same address and status word read again says it is the same fault",
              gpu_fault_last.again && gpu_text_has(gpu_fault, "status 0x701031, hub 0 (graphics); the same fault as the last one read"));
        gpu_epoch++; // the card started again: a new descriptor, a new address space, which keeps no fault of the old one's
        gpu_vm_note(in_named, 0x00701031u, 0);
        check("gpu vm fault: the same address and status word in a card started again is not a repeat", !gpu_fault_last.again && !gpu_text_has(gpu_fault, "the same fault"));
        gpu_vm_note(in_named, 0x00701032u, 0);
        check("gpu vm fault: another status word at the address is another fault", !gpu_fault_last.again && !gpu_text_has(gpu_fault, "the same fault"));
        gpu_vm_note(in_unnamed, 0x00701032u, 0);
        check("gpu vm fault: another address is another fault", !gpu_fault_last.again && gpu_text_has(gpu_fault, "held map, handle 8"));

        // 9. The whole chain by the fake submit: a map looked up, a trim, the job named it and was refused; the line says its list had the map (and not the other).
        gpu_map_count = 2;
        {
                static unsigned int scratch[1024];
                __typeof__(gpu.at) kept_at = gpu.at;
                const void *first = (const void *)0x10000100ull;

                gpu.at = scratch;
                gpu_bo_map(0, 7, 0x10000000ull, 100), gpu_bo_map(1, 8, 0x20000000ull, 100);
                gpu_va_bytes(first, 4);
                gpu_trim();
                gpu_fake_vm = 1, gpu_fake_vm_addr = GPU_VA_MAPS + 0x180, gpu_fake_vm_status = 0x00701031u, gpu_fake_vm_hub = 0, gpu_fault_next = 0;
                gpu_bo_run();
                check("gpu vm fault: a refused job names the map looked up before a trim, and its line says the list had it", gpu_text_has(gpu_fault, "held map, handle 7") &&
                                                                                                                              gpu_text_has(gpu_fault, "listed in the job's bo list"));
                gpu_fake_vm_addr = GPU_VA_MAPS + 0x10040, gpu_fault_next = 0;
                gpu_bo_run();
                check("gpu vm fault: the job after it, which no record gave the other map to, says the list missed that map", gpu_text_has(gpu_fault, "held map, handle 8") &&
                                                                                                                              gpu_text_has(gpu_fault, "missing from the job's bo list"));
                gpu_fake_vm = 0, gpu.at = kept_at;
        }

        gpu_map_count = 0, gpu_gone_at = 0, gpu_fault_next = 0, gpu_fault[0] = 0, gpu_job_error = 0, gpu_submit_serial = 0, gpu_fake_vm = 0, gpu_last_error = 0, gpu_lost = 0;
        gpu_epoch = kept_epoch;
        memory_zero(&gpu_fault_last, sizeof(gpu_fault_last));
}

// The kernel in this run answers the query on the card that is up (the box's has it; one without answers EINVAL): the encoding of the call is the kernel's.
static void gpu_vmfault_live_row(void)
{
        struct gpu_fault_scan scan;
        unsigned int e;

        if (gpu.fd < 0 || gpu.broken)
        {
                check("gpu vm fault: no card here, so the kernel is not asked", true);
                return;
        }
        memory_zero(&scan, sizeof(scan));
        e = gpu_fault_query(&scan);
        check("gpu vm fault: the kernel answers the query on the card (0) or refuses it as unknown (22), nothing else", e == 0 || e == 22);
        string_format(log, "  gpu vm fault: the kernel's answer to the query on the card: error %p, a fault kept: %p\n", (positive)e, (positive)scan.have_query);
}

// A job the wait gave up on (a hang: gpu_run_job leaves gpu.pending set) may still run on the card. Its maps are not unmapped, and its import is not unmapped, under
// it: the restart after the cool-down waits for the card to be idle (compositor_gpu_restart_check), and an import released meanwhile is unmapped by the trim
// once the card is idle (gpu_unimport, gpu_trim). Each step's answer is the fake fence's (gpu_fake_busy, gpu_fake_idle).
static void gpu_flight_rows(void)
{
        int saved_card = compositor.card, saved_gpu = compositor_gpu;
        unsigned int before;
        // The card's and the compositor's state as the rows before found them: a recovery starts the card again, and a present after it would lay the card's
        // buffer out anew (the rows after this one see the state the rows before left, not this one's).
        unsigned char kept_gpu[sizeof(gpu)], kept_req[sizeof(gpu_req)], kept_state[sizeof(compositor_gpu_state)];
        positive kept_bytes = gpu_bytes, kept_next = gpu_map_next;
        unsigned int kept_tick = gpu_tick, kept_at = gpu_gone_at, kept_recovered = gpu_recovered;
        b32 kept_lost = gpu_lost, kept_off = gpu_userptr_off, kept_gone = gpu_userptr_gone;

        memory_copy(kept_gpu, &gpu, sizeof(gpu)), memory_copy(kept_req, &gpu_req, sizeof(gpu_req)), memory_copy(kept_state, &compositor_gpu_state, sizeof(compositor_gpu_state));
        gpu.fd = -1, gpu.pending = 0, gpu_fake_busy = 0, gpu_fake_idle = 0, gpu_tick = 100, gpu_map_count = 0, gpu.imports = 0, gpu_gone_at = 0;
        gpu_map_next = GPU_VA_MAPS;
        compositor_gpu = 1, compositor_gpu_state.up = 1, compositor_gpu_state.dead = 0, compositor_gpu_state.cool_until = 0, compositor_gpu_state.restart_due = 1;
        compositor_gpu_state.hung_at = 0, compositor_gpu_state.hung_off = 0, compositor_gpu_state.failures = 0;

        // 1. A map of a job still running after a hang (pending 77, busy): the cool-down is over and the restart is due, and it waits.
        gpu_maps[0].from = 0x10000000ull, gpu_maps[0].to = 0x10004000ull, gpu_maps[0].va = GPU_VA_MAPS, gpu_maps[0].bytes = 0x4000;
        gpu_maps[0].handle = 7, gpu_maps[0].used = gpu_tick, gpu_maps[0].written = 0, gpu_maps[0].dma = 0, gpu_maps[0].handed = 0, gpu_maps[0].offset = 0;
        gpu_map_count = 1;
        gpu.pending = 77, gpu_fake_busy = 1;
        compositor_gpu_restart_check();
        check("flight: a restart due after a hang waits while the job it gave up on may still run (the map kept, the restart still due)",
              gpu_map_count == 1 && gpu_maps[0].va == GPU_VA_MAPS && gpu.pending == 77 && compositor_gpu_state.restart_due && !compositor_gpu_state.dead &&
              gpu_gone_at == 0);

        // 2. The job has ended (idle: the fence answers done), and the restart goes ahead: the recovery lets go of the map, and the card is given up for
        // the lack of a node (every node is absent here).
        gpu_fake_busy = 0, gpu_fake_idle = 1, gpu_fake_absent = 64;
        compositor_gpu_restart_check();
        check("flight: once the card is idle the restart goes ahead: the map is let go of by the recovery, the job's pending clears, and the card is given up",
              gpu_map_count == 0 && gpu_gone_at >= 1 && gpu_gone[(gpu_gone_at - 1) % GPU_GONE].why == GPU_GONE_RECOVER && gpu.pending == 0 &&
              !compositor_gpu_state.restart_due && compositor_gpu_state.dead);
        gpu_fake_absent = 0, compositor_gpu_state.dead = 0, compositor_gpu_state.up = 1;

        // 3. An import of a scanout's buffer let go of while a job may still read it (pending 77, busy): kept, released, not unmapped.
        gpu.imports = 1, gpu.imported[0] = 5, gpu.imported_va[0] = 0x40000000ull, gpu.imported_bytes[0] = 8192, gpu.imported_dead[0] = 0;
        gpu.pending = 77, gpu_fake_busy = 1, gpu_fake_idle = 0;
        before = gpu_gone_at;
        gpu_unimport(0x40000000ull, GPU_GONE_UNIMPORT);
        check("flight: an import let go of while a job may still read it is released, not unmapped",
              gpu.imports == 1 && gpu.imported_dead[0] && gpu.imported_va[0] == 0x40000000ull && gpu_gone_at == before && gpu.pending == 77);

        // 4. The job has ended: the trim unmaps the released import (its reason is the release's), and nothing is pending.
        gpu_fake_busy = 0, gpu_fake_idle = 1;
        for (unsigned int t = 0; t < GPU_IMPORT_GRACE; t++) // kept mapped GPU_IMPORT_GRACE ticks after its release, then unmapped
                gpu_trim();
        check("flight: once the card is idle the trim unmaps the released import, for the release's reason",
              gpu.imports == 0 && gpu.pending == 0 && gpu_gone[(gpu_gone_at - 1) % GPU_GONE].why == GPU_GONE_RELEASE);

        gpu_fake_busy = 0, gpu_fake_idle = 0, gpu_fake_absent = 0, gpu_map_count = 0;
        memory_copy(&gpu, kept_gpu, sizeof(gpu)), memory_copy(&gpu_req, kept_req, sizeof(gpu_req)), memory_copy(&compositor_gpu_state, kept_state, sizeof(compositor_gpu_state));
        gpu_bytes = kept_bytes, gpu_map_next = kept_next, gpu_tick = kept_tick, gpu_gone_at = kept_at, gpu_recovered = kept_recovered;
        gpu_lost = kept_lost, gpu_userptr_off = kept_off, gpu_userptr_gone = kept_gone;
        compositor_gpu = saved_gpu, compositor.card = saved_card;
}

/*
        The card's clocks while input arrives (compositor_gpu_clocks, gpu_clocks): input noted as a device is read is asked of the driver once the turn's
        input is forwarded, on a context of its own (never the jobs' one), at the minimum-clock level (2), once while held; the hold ends at its time, or
        at once when the card stops composing, with the level let go (0); a card started again (a new descriptor: gpu_epoch) is asked again on a new
        context; a refusal (EBUSY: another context holds the device's level) is not asked again on that card, and the owner's file (level 0) asks nothing.
        And a descriptor that is no sync_file has no signal time (compositor_fence_signalled).
*/
static void gpu_clocks_rows(void)
{
        unsigned char kept_gpu[sizeof(gpu)], kept_state[sizeof(compositor_gpu_state)], kept_clocks[sizeof(compositor_gpu_clocks)], kept_cs[sizeof(gpu_clocks_state)];
        int kept_on = compositor_gpu, efd;
        unsigned int kept_epoch = gpu_epoch, asks, ctx;
        positive t = 1000000000000ull, kept_last = gpu_last_job;

        memory_copy(kept_gpu, &gpu, sizeof(gpu)), memory_copy(kept_state, &compositor_gpu_state, sizeof(compositor_gpu_state));
        memory_copy(kept_clocks, &compositor_gpu_clocks, sizeof(compositor_gpu_clocks)), memory_copy(kept_cs, &gpu_clocks_state, sizeof(gpu_clocks_state));
        memory_zero(&gpu_kernel, sizeof(gpu_kernel));
        memory_zero(&gpu_clocks_state, sizeof(gpu_clocks_state));
        gpu_kernel.on = 1;
        gpu.fd = GPU_KERNEL_FD, gpu.ctx = 1, gpu.at = 0, gpu_last_job = 0; // no wake job can run (gpu_wake wants a buffer and a job before it)
        compositor_gpu = 1, compositor_gpu_state.up = 1, compositor_gpu_state.dead = 0;
        // The default holds no level (the driver chooses the clock; a held minimum capped a game at 600 MHz): the rows below are the hold's mechanism,
        // as with a level compiled in, the minimum-clock one.
        check("gpu clocks: by default input holds no level of the card's clock", COMPOSITOR_GPU_CLOCKS_LEVEL == 0);
        compositor_gpu_clocks.level = 2u, compositor_gpu_clocks.held = 0, compositor_gpu_clocks.refused = 0, compositor_gpu_clocks.input = 0;
        compositor_gpu_clocks.inline_asks = 1; // asked in place: the rows read each answer at once (the thread answers the same words)

        compositor_gpu_input_turn(t);
        check("gpu clocks: a turn with no input asks the driver nothing", gpu_kernel.pstate_asks == 0 && !compositor_gpu_clocks.held && compositor_gpu_clocks_at() == 0);
        compositor_gpu_wake();
        check("gpu clocks: input read is only noted: nothing is asked before the turn's input is forwarded", gpu_kernel.pstate_asks == 0 && compositor_gpu_clocks.input);
        compositor_gpu_input_turn(t);
        ctx = gpu_kernel.pstate_ctx;
        check("gpu clocks: the turn's end asks the minimum-clock level on a context made for it, not the jobs' one, and holds it for half a second",
              gpu_kernel.pstate_asks == 1 && gpu_kernel.pstate_level == 2 && gpu_kernel.contexts == 1 && ctx != 0 && ctx != gpu.ctx && compositor_gpu_clocks.held &&
                      compositor_gpu_clocks_at() == t + COMPOSITOR_GPU_CLOCKS_HOLD && !compositor_gpu_clocks.input);
        compositor_gpu_wake(), compositor_gpu_input_turn(t + 300000000ull);
        check("gpu clocks: input while it is held asks nothing again and holds it on from the newest input",
              gpu_kernel.pstate_asks == 1 && compositor_gpu_clocks_at() == t + 300000000ull + COMPOSITOR_GPU_CLOCKS_HOLD);
        compositor_gpu_clocks_tick(t + 300000000ull + COMPOSITOR_GPU_CLOCKS_HOLD - 1);
        check("gpu clocks: before the hold's end the loop's tick lets nothing go", gpu_kernel.pstate_asks == 1 && compositor_gpu_clocks.held);
        compositor_gpu_clocks_tick(t + 300000000ull + COMPOSITOR_GPU_CLOCKS_HOLD);
        check("gpu clocks: at the hold's end the level is let go (0, the driver's own choice) on the same context, and the loop has no time to wake for",
              gpu_kernel.pstate_asks == 2 && gpu_kernel.pstate_level == 0 && gpu_kernel.pstate_ctx == ctx && !compositor_gpu_clocks.held && compositor_gpu_clocks_at() == 0);
        compositor_gpu_clocks_tick(t + 2 * COMPOSITOR_GPU_CLOCKS_HOLD);
        check("gpu clocks: let go once, never again", gpu_kernel.pstate_asks == 2);

        // canvas gpu off while it is held: let go at the next tick, and no input asks while it is off.
        compositor_gpu_wake(), compositor_gpu_input_turn(t + 2000000000ull);
        compositor_gpu = 0;
        compositor_gpu_clocks_tick(t + 2000000001ull);
        asks = gpu_kernel.pstate_asks;
        compositor_gpu_wake(), compositor_gpu_input_turn(t + 2000000002ull);
        check("gpu clocks: the card set off while held lets the level go at once, and input with it off asks nothing",
              asks == 4 && gpu_kernel.pstate_level == 0 && !compositor_gpu_clocks.held && gpu_kernel.pstate_asks == 4);
        compositor_gpu = 1;

        // A card started again: a new descriptor (the old context went with the old one), so the next input asks on a new context.
        compositor_gpu_wake(), compositor_gpu_input_turn(t + 3000000000ull);
        gpu_epoch++;
        compositor_gpu_wake(), compositor_gpu_input_turn(t + 3100000000ull);
        check("gpu clocks: a card started again while held is asked again, on a context made on the new descriptor",
              gpu_kernel.pstate_asks == 6 && gpu_kernel.contexts == 2 && gpu_kernel.pstate_ctx != ctx && gpu_kernel.pstate_level == 2 && compositor_gpu_clocks.held);
        compositor_gpu_clocks_tick(t + 4000000000ull);

        // Refused (EBUSY: another context holds the device's level): not held, not asked again on this card; asked again on a card started again.
        gpu_kernel.pstate_error = 16;
        asks = gpu_kernel.pstate_asks;
        compositor_gpu_wake(), compositor_gpu_input_turn(t + 5000000000ull);
        compositor_gpu_wake(), compositor_gpu_input_turn(t + 5100000000ull);
        check("gpu clocks: a refusal holds nothing and is not asked again on that card", gpu_kernel.pstate_asks == asks + 1 && !compositor_gpu_clocks.held &&
              compositor_gpu_clocks.refused && compositor_gpu_clocks_at() == 0 && gpu_clocks_state.error == 16);
        gpu_kernel.pstate_error = 0, gpu_epoch++;
        compositor_gpu_wake(), compositor_gpu_input_turn(t + 5200000000ull);
        check("gpu clocks: on a card started again the refused level is asked again", gpu_kernel.pstate_asks == asks + 2 && compositor_gpu_clocks.held);
        compositor_gpu_clocks_tick(t + 6000000000ull);

        // The thread's take (compositor_gpu_clocks_answer, as the thread runs it, with no thread started): an ask is taken whole and once, and one
        // whose card was let go of before it was answered (its epoch old) is not made on whatever the descriptor's number is now.
        {
                b32 kept_inline = compositor_gpu_clocks.inline_asks, kept_started = compositor_gpu_clocks.started, stale, taken, made;

                compositor_gpu_clocks.inline_asks = 0, compositor_gpu_clocks.started = 1, gpu_clocks_state.level = 0;
                asks = gpu_kernel.pstate_asks;
                compositor_gpu_clocks_ask(2u);
                gpu_epoch++;
                compositor_gpu_clocks_answer();
                stale = gpu_kernel.pstate_asks == asks && compositor_gpu_clocks.want_fd == -1 &&
                        compositor_gpu_clocks.answered == compositor_gpu_clocks.asked;
                compositor_gpu_clocks_answer();
                taken = gpu_kernel.pstate_asks == asks;
                compositor_gpu_clocks_ask(2u);
                compositor_gpu_clocks_answer();
                made = gpu_kernel.pstate_asks == asks + 1 && gpu_kernel.pstate_level == 2 && compositor_gpu_clocks.want_fd == -1;
                check("gpu clocks: the thread takes an ask whole and once, and makes none whose card was let go of since it was asked", stale && taken && made);
                compositor_gpu_clocks_ask(0u), compositor_gpu_clocks_answer();
                compositor_gpu_clocks.inline_asks = kept_inline, compositor_gpu_clocks.started = kept_started;
        }

        // The owner's file: no level, nothing asked.
        compositor_gpu_clocks.level = 0;
        asks = gpu_kernel.pstate_asks;
        compositor_gpu_wake(), compositor_gpu_input_turn(t + 7000000000ull);
        check("gpu clocks: with the owner's file (level 0) input asks the driver nothing", gpu_kernel.pstate_asks == asks && !compositor_gpu_clocks.held);

        efd = (int)system_call_2(syscall(eventfd2), 0, 0x80000);
        check("gpu clocks: a descriptor that is no sync_file says no signal time", compositor_fence_signalled(efd) == 0 && compositor_fence_signalled(-1) == 0);
        if (efd >= 0)
                system_close(efd);

        memory_copy(&gpu, kept_gpu, sizeof(gpu)), memory_copy(&compositor_gpu_state, kept_state, sizeof(compositor_gpu_state));
        memory_copy(&compositor_gpu_clocks, kept_clocks, sizeof(compositor_gpu_clocks)), memory_copy(&gpu_clocks_state, kept_cs, sizeof(gpu_clocks_state));
        compositor_gpu = kept_on, gpu_epoch = kept_epoch, gpu_last_job = kept_last;
        memory_zero(&gpu_kernel, sizeof(gpu_kernel));
}

/*
        The imports, the lost card and the maps of a job still running, against the fake kernel (gpu_kernel: no card). The kernel answers a dma-buf it has
        imported with the handle that import has and counts nothing, so a handle closed for a buffer the table holds takes the buffer and its map from the
        holder: the rows ask the kernel whether the handle is still there and mapped after each way a held buffer is asked for again.
*/
static void gpu_import_rows(void)
{
        unsigned char kept_gpu[sizeof(gpu)], kept_req[sizeof(gpu_req)], kept_state[sizeof(compositor_gpu_state)];
        positive kept_bytes = gpu_bytes, kept_next = gpu_map_next, first, again, other, big;
        unsigned int kept_tick = gpu_tick, kept_at = gpu_gone_at, kept_recovered = gpu_recovered, kept_count = gpu_map_count, kept_why = gpu_map_why, h, before;
        b32 kept_lost = gpu_lost, kept_off = gpu_userptr_off, kept_gone = gpu_userptr_gone;
        int kept_on = compositor_gpu;
        char bus[32];

        memory_copy(kept_gpu, &gpu, sizeof(gpu)), memory_copy(kept_req, &gpu_req, sizeof(gpu_req)), memory_copy(kept_state, &compositor_gpu_state, sizeof(compositor_gpu_state));
        memory_zero(&gpu_kernel, sizeof(gpu_kernel));
        gpu_kernel.on = 1;
        gpu.fd = GPU_KERNEL_FD, gpu.at = 0, gpu.size = 0, gpu.broken = 0, gpu.pending = 0, gpu.imports = 0, gpu.ctx = 1;
        gpu_fake_busy = 0, gpu_fake_idle = 1, gpu_fake_errno = 0, gpu_tick = 100, gpu_map_count = 0, gpu_gone_at = 0, gpu_map_next = GPU_VA_MAPS, gpu_lost = 0;

        // 1. A client's dma-buf imported: one handle, mapped once, at the address answered.
        first = gpu_import_fd(GPU_KERNEL_DMABUF + 1, 8192);
        h = gpu_kernel_handle(1);
        check("gpu import: a client's dma-buf is imported: one handle of the kernel's, mapped at the address answered",
              first != 0 && gpu.imports == 1 && h != 0 && gpu.imported[0] == h && gpu_kernel.mapped[h] == 1 && gpu_kernel.va[h] == first);

        // 2. Released and asked for again before the trim has unmapped it (two commits of one buffer inside a present): the kernel answers the handle the
        // import has. It is held again at its address, and the kernel still has the handle and its map.
        gpu_release_import(first);
        again = gpu_import_fd(GPU_KERNEL_DMABUF + 1, 8192);
        check("gpu import: a released import asked for again is held again at its address", again == first && gpu.imports == 1 && !gpu.imported_dead[0]);
        check("gpu import: and the kernel still has its handle and its map (the handle the kernel answered is the import's own, not closed)",
              gpu_kernel_handle(1) == h && gpu_kernel.mapped[h] == 1 && gpu_kernel.closes == 0);

        // 3. The same buffer asked for while it is held (a row's damage copies read it, and the row then shows it in place): held twice, one handle, one map.
        other = gpu_import_fd(GPU_KERNEL_DMABUF + 1, 8192);
        check("gpu import: a buffer asked for while it is held is held twice at the one address", other == first && gpu.imports == 1 && gpu.imported_holds[0] == 2);
        check("gpu import: and the kernel still has its handle and its map", gpu_kernel_handle(1) == h && gpu_kernel.mapped[h] == 1 && gpu_kernel.closes == 0);
        big = gpu_import_fd(GPU_KERNEL_DMABUF + 1, 1 << 20);
        check("gpu import: asked for with more bytes than its map has, it is refused and held no more often", big == 0 && gpu.imported_holds[0] == 2 && gpu_kernel.closes == 0);

        // 4. Each holder lets go once: the first leaves it held, the last releases it, and the trim of an idle card unmaps it and closes its handle, once.
        gpu_release_import(first);
        gpu_trim();
        check("gpu import: one of two holders letting go leaves it held and mapped", gpu.imports == 1 && !gpu.imported_dead[0] && gpu_kernel.mapped[h] == 1);
        gpu_release_import(first);
        check("gpu import: the last holder letting go releases it", gpu.imports == 1 && gpu.imported_dead[0]);
        for (unsigned int t = 0; t < GPU_IMPORT_GRACE; t++) // kept mapped GPU_IMPORT_GRACE ticks after its release, then unmapped
                gpu_trim();
        check("gpu import: and the trim unmaps it and closes its handle, once, and no handle that was not there",
              gpu.imports == 0 && gpu_kernel_live() == 0 && gpu_kernel.closes == 1 && gpu_kernel.refused == 0);

        // 4b (S2). The placer wraps: a freed address is handed to a second buffer. A release of the first buffer's address with the first's generation is a
        // counted no-op on the second; the second's own generation releases it.
        first = gpu_import_fd(GPU_KERNEL_DMABUF + 1, 8192);
        {
                unsigned int gen_first = gpu_import_gen(first), gen_other, stale = gpu_import_stale;

                gpu_unimport(first, GPU_GONE_UNIMPORT);
                gpu_map_next = first - GPU_IMPORT_GUARD; // the placer's next address is the freed one
                other = gpu_import_fd(GPU_KERNEL_DMABUF + 2, 8192);
                gen_other = gpu_import_gen(other);
                check("gpu import (S2): the placer hands the freed address to a second buffer, under a generation of its own",
                      other == first && gen_first != 0 && gen_other != 0 && gen_other != gen_first && gpu.imports == 1);
                gpu_release_import_gen(first, gen_first);
                check("gpu import (S2): a stale release with the first buffer's generation is a no-op on the second, and counted",
                      gpu.imports == 1 && !gpu.imported_dead[0] && gpu.imported_holds[0] == 1 && gpu_import_stale == stale + 1);
                gpu_release_import_gen(other, gen_other);
                check("gpu import (S2): the second buffer's own release, with its generation, releases it", gpu.imports == 1 && gpu.imported_dead[0]);
                for (unsigned int t = 0; t < GPU_IMPORT_GRACE; t++)
                        gpu_trim();
                check("gpu import (S2): and the trim unmaps it once", gpu.imports == 0 && gpu_kernel_live() == 0 && gpu_kernel.refused == 0);
        }

        // 5. A scanout's buffer (the display node's handle 7) asked for twice: one import, and the first let go of leaves the second its map.
        first = gpu_import(GPU_KERNEL_CARD, 7, 16384);
        again = gpu_import(GPU_KERNEL_CARD, 7, 16384);
        h = gpu_kernel_handle(7);
        check("gpu import: a scanout's buffer asked for twice is one import, held twice", first != 0 && again == first && gpu.imports == 1 && h != 0 && gpu_kernel.mapped[h] == 1);
        gpu_unimport(first, GPU_GONE_UNIMPORT);
        check("gpu import: let go of once, it is still the other holder's: the handle and the map are the kernel's still", gpu.imports == 1 && gpu_kernel_handle(7) == h && gpu_kernel.mapped[h] == 1);
        gpu_unimport(first, GPU_GONE_UNIMPORT);
        check("gpu import: let go of by the last, it is unmapped and its handle closed", gpu.imports == 0 && gpu_kernel_live() == 0 && gpu_kernel.refused == 0);

        // 6. No count of imports is a limit: two hundred buffers are each an import (the table grown past its first 64), one more is too, and
        // one already held asked for again is held twice; let go of while the card is busy, an import is kept dead, not dropped.
        for (unsigned int b = 0; b < 200; b++)
                gpu_import_fd(GPU_KERNEL_DMABUF + 1 + (int)b, 4096);
        check("gpu import: two hundred buffers are two hundred imports", gpu.imports == 200 && gpu_kernel_live() == 200);
        check("gpu import: one more is one more import, not refused", gpu_import_fd(GPU_KERNEL_DMABUF + 201, 4096) != 0 && gpu.imports == 201 &&
              gpu_kernel_live() == 201);
        check("gpu import: an active import asked for again is held twice, not made again",
              gpu_import_fd(GPU_KERNEL_DMABUF + 3, 4096) != 0 && gpu.imports == 201);
        gpu.pending = 77, gpu_fake_busy = 1, gpu_fake_idle = 0;
        gpu_unimport(gpu.imported_va[0], GPU_GONE_UNIMPORT);
        check("gpu import: unimporting one slot while busy marks it dead without dropping it", gpu.imports == 201 && gpu.imported_dead[0]);
        gpu_fake_busy = 0, gpu_fake_idle = 0;
        while (gpu.imports)
                gpu_unimport(gpu.imported_va[0], GPU_GONE_UNIMPORT);
        check("gpu import: every import let go of by its holders leaves the kernel no handle", gpu_kernel_live() == 0 && gpu_kernel.refused == 0);

        // 7. A job the wait gave up on, and a map last used more than 120 ticks ago: the trim does not unmap it while that job may run, and does once it has ended.
        gpu_maps[0].from = 0x10000000ull, gpu_maps[0].to = 0x10004000ull, gpu_maps[0].va = GPU_VA_MAPS, gpu_maps[0].bytes = 0x4000;
        gpu_maps[0].handle = 9, gpu_maps[0].used = gpu_tick - 200, gpu_maps[0].written = 0, gpu_maps[0].dma = 0, gpu_maps[0].handed = 1, gpu_maps[0].offset = 0;
        gpu_map_count = 1, gpu_gone_at = 0;
        gpu.pending = 77, gpu_fake_busy = 1, gpu_fake_idle = 0;
        gpu_trim();
        check("gpu trim: a map unused for 120 ticks is not unmapped while a job given up on may still read it", gpu_map_count == 1 && gpu_gone_at == 0 && gpu.pending == 77);
        // 8. Nor is it let go of for a written map put over it: that map is refused (10).
        check("gpu map: a written map over a map a job given up on may still read is refused (10), and the map is kept",
              !gpu_map_written((const void *)0x10002000ull, 0x1000) && (gpu_map_why & 255) == 10 && gpu_map_count == 1 && gpu_gone_at == 0);
        gpu_fake_busy = 0, gpu_fake_idle = 1;
        gpu_trim();
        check("gpu trim: once the job has ended the trim unmaps it, as unused", gpu_map_count == 0 && gpu_gone_at == 1 && gpu_gone[0].why == GPU_GONE_TRIM && gpu.pending == 0);

        // 9. A wait the kernel refuses, with a new context to be had: the job is over, and the jobs after it have a context of their own.
        gpu.pending = 77, gpu.ctx = 1, gpu_fake_idle = 0, gpu_fake_errno = 1, gpu_lost = 0;
        check("gpu idle: a wait the kernel refuses ends the job, and the next jobs have a new context", gpu_idle() && gpu.pending == 0 && gpu.ctx != 1 && !gpu_lost);
        // A wait a signal ended (EINTR, three times running) is asked again and is no failed wait: the job is done when the kernel says so, with the context
        // it had (a failed wait makes a new one, and is the card's hang when the job's end was owed).
        gpu.pending = 77, gpu.ctx = 1, gpu_fake_errno = 0, gpu_fake_idle = 0, gpu_fake_busy = 0, gpu_kernel.interrupts = 3, gpu_kernel.waits = 0, gpu_lost = 0;
        check("gpu idle: a wait a signal ended is asked again, and the job is done with its context kept",
              gpu_idle() && gpu.pending == 0 && gpu.ctx == 1 && !gpu_lost && gpu_kernel.waits == 4 && gpu_kernel.interrupts == 0);
        gpu_fake_errno = 1;
        // 10. With no context to be had either (the descriptor answers nothing: the device is gone), the job can never be asked after: the card is lost, and
        // the compositor starts it again instead of holding its buffer for good. Every node is absent here, so the start finds no card and says so.
        gpu.pending = 77, gpu.ctx = 1, gpu_kernel.no_context = 1;
        check("gpu idle: a wait refused with no new context to be had is a lost card, not a job waited on for good", !gpu_idle() && gpu_lost && gpu.pending == 77);
        compositor_gpu = 1, compositor_gpu_state.up = 1, compositor_gpu_state.dead = 0, compositor_gpu_state.restart_due = 0, compositor_gpu_state.cool_until = 0;
        before = gpu_recovered, gpu_fake_absent = 64;
        compositor_gpu_lost_check();
        gpu_fake_absent = 0;
        check("gpu idle: the lost card is started again (here to no node: given up as no card), and nothing is pending on the old one",
              gpu_recovered == before + 1 && gpu.pending == 0 && !gpu_lost && compositor_gpu_state.dead && gpu.fd == -1);
        gpu_kernel.no_context = 0, gpu_fake_errno = 0;

        // The context's priority: asked high (AMDGPU_CTX_PRIORITY_HIGH, 512), and where the kernel refuses that (EACCES: no CAP_SYS_NICE) a normal one is made.
        {
                unsigned int made = 0, contexts = gpu_kernel.contexts;
                int kept_fd = gpu.fd, kept_on = gpu_kernel.on;

                gpu.fd = GPU_KERNEL_FD, gpu_kernel.on = 1, gpu_kernel.priority_refused = 0;
                check("gpu priority: the context is asked at high priority and made so",
                      gpu_amd_context(&made, 0) == 0 && made && gpu_kernel.priority_asked == 512 && gpu_priority == 2 && gpu_kernel.contexts == contexts + 1);
                gpu_kernel.priority_refused = 1, made = 0;
                check("gpu priority: refused high (EACCES), a normal context is made and the refusal kept",
                      gpu_amd_context(&made, 0) == 0 && made && gpu_kernel.priority_asked == 0 && gpu_priority == 1 && gpu_priority_error == 13);
                gpu_kernel.no_context = 1, gpu_kernel.priority_refused = 0;
                check("gpu priority: no context at all is the kernel's error, and no priority said", gpu_amd_context(&made, 0) == 19 && gpu_priority == 0);
                gpu_kernel.no_context = 0, gpu.fd = kept_fd, gpu_kernel.on = kept_on;
        }

        // The families: the four the kernels were written and run on are let in always; family 143 (the rest of gfx10: the same words by llvm-mc, run by nobody)
        // only where the owner's file asks, which a start looks for each time; no other family either way.
        gpu_fake_wide = -1, gpu_fake_absent = 64, gpu_start(1 << 20);
        check("gpu family: with no file asking, the families are the four the kernels were run on, and 143 is not one",
              !gpu_wide && gpu_family_ok(144) && gpu_family_ok(146) && gpu_family_ok(149) && gpu_family_ok(151) && !gpu_family_ok(143));
        gpu_fake_wide = 1, gpu_fake_absent = 64, gpu_start(1 << 20);
        check("gpu family: the owner's file lets family 143 in, and no other (RDNA3's 145, 148 and 150, Raven's 142 stay out)",
              gpu_wide && gpu_family_ok(143) && gpu_family_ok(149) && !gpu_family_ok(145) && !gpu_family_ok(148) && !gpu_family_ok(150) && !gpu_family_ok(142) && !gpu_family_ok(0));
        gpu_fake_wide = -1, gpu_fake_absent = 64, gpu_start(1 << 20);
        check("gpu family: the file gone at the next start, 143 is out again", !gpu_wide && !gpu_family_ok(143));
        gpu_fake_wide = 0, gpu_fake_absent = 0;
        gpu.fd = GPU_KERNEL_FD, gpu.at = 0;

        // 11. The bus of a node. The display node's is GET_UNIQUE's, which ends at the length the kernel writes back (it writes no 0): the bytes the room held
        // before are not part of it. A render node is refused the request. The name is the device's own, the same as sysfs gives for either node.
        memory_copy(gpu_kernel.unique, "pci:0000:c5:00.0", 16), gpu_kernel.unique_length = 16;
        memory_fill(bus, 'x', sizeof(bus));
        check("gpu bus: the display node's bus ends where the kernel says it does, whatever the room held",
              gpu_bus_of(GPU_KERNEL_CARD, bus, sizeof(bus)) && string_equals((string_address)bus, (string_address) "0000:c5:00.0"));
        memory_fill(bus, 'x', sizeof(bus));
        check("gpu bus: a render node the kernel refuses the request has no bus by it", !gpu_bus_of(GPU_KERNEL_FD, bus, sizeof(bus)) && bus[0] == 0);
        check("gpu bus: sysfs's link from a node to its device names the same device",
              gpu_bus_name("../../../0000:c5:00.0", 21, bus, sizeof(bus)) && string_equals((string_address)bus, (string_address) "0000:c5:00.0"));
        check("gpu bus: a device that is not on PCI is named as it is", gpu_bus_name("../../../fe9b0000.gpu", 21, bus, sizeof(bus)) && string_equals((string_address)bus, (string_address) "fe9b0000.gpu"));
        check("gpu bus: a name that does not fit, or none, is no bus", !gpu_bus_name("../../../0000:c5:00.0", 21, bus, 8) && bus[0] == 0 && !gpu_bus_name("../", 3, bus, sizeof(bus)) &&
              !gpu_bus_name("", 0, bus, sizeof(bus)));
        {
                char candidate[2][32];
                unsigned int why = 9;

                gpu_bus_name("../../../0000:04:00.0", 21, candidate[0], sizeof(candidate[0])), gpu_bus_name("../../../0000:c5:00.0", 21, candidate[1], sizeof(candidate[1]));
                gpu_bus_of(GPU_KERNEL_CARD, bus, sizeof(bus));
                check("gpu bus: the render node on the display's device is picked by it, the display's bus read from the kernel and the nodes' from sysfs",
                      gpu_pick(candidate, 2, bus, &why) == 1 && why == 1);
        }

        gpu_kernel.on = 0, gpu_fake_busy = 0, gpu_fake_idle = 0, gpu_fake_errno = 0, gpu_fake_absent = 0;
        memory_copy(&gpu, kept_gpu, sizeof(gpu)), memory_copy(&gpu_req, kept_req, sizeof(gpu_req)), memory_copy(&compositor_gpu_state, kept_state, sizeof(compositor_gpu_state));
        gpu_bytes = kept_bytes, gpu_map_next = kept_next, gpu_tick = kept_tick, gpu_gone_at = kept_at, gpu_recovered = kept_recovered, gpu_map_count = kept_count;
        gpu_lost = kept_lost, gpu_userptr_off = kept_off, gpu_userptr_gone = kept_gone, gpu_map_why = kept_why, compositor_gpu = kept_on;
}

#define GPU_BENCH_HASH 1713744629u // (the cut at binning: a cell's list starts at the record that covers it whole, 2395 entries the waves read of the 6799 placed)
#define GPU_BENCH_ENTRIES 2395u
/*
        The processor's share of a frame the card draws, with no card (the fake card: the buffer is memory, the submit is taken, the fence answers done): a
        desktop's scene of 85 records on 1920 by 1080 (a ground, twelve windows of 900 by 650, each a picture, a rounded box with one change of cover and
        four runs of text) is binned and submitted by gpu_scene, whole and for a damage of one 64-pixel square, and timed; what one present asks of the kernel
        is counted (the submit and the wait: nothing else), and what it writes for the card (the packets, the arguments, the bins and their lists) is held to
        the hash it had before the path was made lighter, so that lighter is the same bytes.
*/
static unsigned int gpu_bench_buffer[(24 << 20) / 4];

static unsigned int gpu_bench_hash(const void *bytes, positive count, unsigned int hash)
{
        const p8 address_to at = (const p8 address_to)bytes;

        for (positive i = 0; i < count; i++)
                hash = (hash ^ at[i]) * 16777619u;
        return hash;
}

static void gpu_bench_rows(void)
{
        enum { BW = 1920, BH = 1080, WINS = 12, ROUNDS = 2000, MAPS = 200 };
        unsigned char kept_gpu[sizeof(gpu)];
        unsigned int kept_count = gpu_map_count, kept_tick = gpu_tick, n = 0, hash, entries = 0;
        positive ops_at = GPU_DATA, bins_at = GPU_DATA + (1 << 20), d_at = GPU_VA_MAPS, began, whole, small, looked;
        unsigned int address_to ops = (unsigned int address_to)((p8 address_to)gpu_bench_buffer + ops_at);
        unsigned int address_to tab = (unsigned int address_to)((p8 address_to)gpu_bench_buffer + bins_at);
        static unsigned char kept_maps[sizeof(gpu_maps)];
        b32 drew = true;

        memory_copy(kept_gpu, &gpu, sizeof(gpu)), memory_copy(kept_maps, gpu_maps, sizeof(gpu_maps));
        gpu.fd = -1, gpu.ctx = 1, gpu.at = (unsigned int address_to)gpu_bench_buffer, gpu.size = sizeof(gpu_bench_buffer), gpu.imports = 0, gpu.pending = 0;
        gpu_end_said = 0, gpu_before.sequence = 0, gpu_found = GPU_END_NONE, gpu_fake_card = 1, gpu_fake_busy = 0, gpu_fake_idle = 1, gpu_fake_waits = 0, gpu_fake_errno = 0, gpu_tick = 100;
        // The maps a desktop holds: 200 of them, the scanout's the last made (the one a present looks up).
        for (unsigned int i = 0; i < MAPS; i++)
        {
                gpu_maps[i].from = 0x100000000ull + (positive)i * 0x1000000, gpu_maps[i].to = gpu_maps[i].from + 0x800000, gpu_maps[i].va = GPU_VA_MAPS + (positive)i * 0x1000000;
                gpu_maps[i].bytes = 0x800000, gpu_maps[i].handle = 20 + i, gpu_maps[i].used = gpu_tick, gpu_maps[i].written = i == MAPS - 1, gpu_maps[i].dma = 0;
                gpu_maps[i].handed = 0, gpu_maps[i].listed = 0, gpu_maps[i].offset = 0;
        }
        gpu_map_count = MAPS;
#define GPU_BENCH_OP(a, b, c, d, e, f, g, h) (ops[8 * n] = (a), ops[8 * n + 1] = (unsigned int)(b), ops[8 * n + 2] = (unsigned int)(c), ops[8 * n + 3] = (unsigned int)(d), \
                                              ops[8 * n + 4] = (unsigned int)(e), ops[8 * n + 5] = (f), ops[8 * n + 6] = (g), ops[8 * n + 7] = (h), n++)
        GPU_BENCH_OP(0, 0, 0, BW, BH, 0xff203040u, 0, 0);
        for (int k = 0; k < WINS; k++)
        {
                int x = 40 + k * 80, y = 20 + k * 34;

                GPU_BENCH_OP(4, x, y, x + 900, y + 650, (unsigned int)(GPU_VA_MAPS + (positive)k * 0x1000000), (unsigned int)((GPU_VA_MAPS + (positive)k * 0x1000000) >> 32), 900);
                GPU_BENCH_OP(2 | 1 << 8, x * 256, y * 256, (x + 900) * 256, (y + 650) * 256, 0xc0ffffffu, 12 * 256, 12 * 256);
                GPU_BENCH_OP(8, x * 256 + 256, y * 256 + 256, (x + 899) * 256, (y + 649) * 256, 0, 11 * 256, 11 * 256);
                for (int t = 0; t < 4; t++)
                        GPU_BENCH_OP(3 | 200 << 8, x + 20, y + 8 + t * 24, x + 220, y + 28 + t * 24, 0xffe0e0e0u, 255, 4096 * (unsigned int)(k * 4 + t));
        }
#undef GPU_BENCH_OP
        // One present: what it asks of the kernel, and the bytes it leaves for the card.
        gpu_fake_submits = gpu_fake_wait_calls = gpu_fake_requests = 0;
        drew = gpu_va_written((const void *)(gpu_maps[MAPS - 1].from + 64), 4096) != 0 && gpu_scene(ops_at, n, bins_at, d_at, BW, BW, BH, 0, 0, BW, BH);
        check("gpu bench: the scene of 85 records is drawn by the fake card", drew && n == 85);
        check("gpu bench: one present asks the kernel for one submit and one wait, and nothing else", gpu_fake_submits == 1 && gpu_fake_wait_calls == 1 && gpu_fake_requests == 0);
        for (unsigned int c = 0; c < ((BW + 63) >> 6) * ((BH + 63) >> 6); c++)
                entries += tab[2 * c + 1];
        hash = gpu_bench_hash(gpu_fake_ib, 4u * gpu_fake_ib_words, 2166136261u);
        hash = gpu_bench_hash(GPU_ARGS, sizeof(struct gpu_args), hash);
        hash = gpu_bench_hash(tab, 8ull * ((BW + 63) >> 6) * ((BH + 63) >> 6) + 32ull * entries, hash);
        check("gpu bench: the packets, the arguments, the bins and their lists are the bytes they were before the path was made lighter", hash == GPU_BENCH_HASH && entries == GPU_BENCH_ENTRIES);
        if (hash != GPU_BENCH_HASH || entries != GPU_BENCH_ENTRIES)
                string_format(log, "  gpu bench: the hash is %p over %p list entries and %p packet words\n", (positive)hash, (positive)entries, (positive)gpu_fake_ib_words);
        // Each is the best of twenty batches: the machine's other work is in the others.
        whole = small = looked = ~0ull;
        for (int batch = 0; batch < 20; batch++)
        {
                positive took;

                began = clock_monotonic_nanoseconds();
                for (int r = 0; r < ROUNDS / 20; r++)
                        drew &= gpu_scene(ops_at, n, bins_at, d_at, BW, BW, BH, 0, 0, BW, BH);
                took = (clock_monotonic_nanoseconds() - began) / (ROUNDS / 20), whole = took < whole ? took : whole;
                began = clock_monotonic_nanoseconds();
                for (int r = 0; r < ROUNDS / 20; r++)
                        drew &= gpu_scene(ops_at, n, bins_at, d_at, BW, BW, BH, 960, 540, 1024, 604);
                took = (clock_monotonic_nanoseconds() - began) / (ROUNDS / 20), small = took < small ? took : small;
                began = clock_monotonic_nanoseconds();
                for (int r = 0; r < ROUNDS * 5; r++)
                        drew &= gpu_va_bytes((const void *)(gpu_maps[r % 4 ? MAPS - 1 : MAPS - 2].from + 64), 4096) != 0;
                took = (clock_monotonic_nanoseconds() - began) * 100 / (ROUNDS * 5), looked = took < looked ? took : looked;
        }
        check("gpu bench: every timed present was drawn", drew);
        string_format(log, "  gpu bench: the processor's share of a present of 85 records on 1920 by 1080 is %p ns whole (%p list entries) and %p ns for a 64-pixel damage; a map found among 200 in %p hundredths of a ns\n",
                      (positive)whole, (positive)entries, (positive)small, (positive)looked);

        gpu_fake_card = 0, gpu_fake_idle = 0, gpu_handed_clear();
        memory_copy(&gpu, kept_gpu, sizeof(gpu)), memory_copy(gpu_maps, kept_maps, sizeof(gpu_maps));
        gpu_map_count = kept_count, gpu_tick = kept_tick, gpu_last_error = 0, gpu_end = GPU_END_NONE;
}

static unsigned int gpu_late_buffer[(4 << 20) / 4]; // the fake card's buffer (gpu_late_rows): the ops, the tables and the bins of a 64 by 4 scene
static unsigned int gpu_late_screen[64 * 4];        // and its scanout

// The compositor's state as a card that has just started has it, and the fake card (no descriptor, a submit the fake takes, a fence it answers).
static void gpu_late_reset(void)
{
        gpu.pending = 0, gpu_before.sequence = 0, gpu_end_said = 0, gpu_found = GPU_END_NONE, gpu_end = GPU_END_NONE, gpu_last_error = 0;
        gpu_fake_busy = 0, gpu_fake_idle = 1, gpu_fake_waits = 0, gpu_fake_errno = 0, gpu_fake_fault = 0;
        compositor_gpu_state.up = 1, compositor_gpu_state.dead = 0, compositor_gpu_state.told = 1, compositor_gpu_state.force = 1, compositor_gpu_state.cool_until = 0;
        compositor_gpu_state.restart_due = 0, compositor_gpu_state.failures = 0, compositor_gpu_state.hung_at = 0, compositor_gpu_state.hung_off = 0;
        compositor_gpu_state.cool_steps = 0;
}

/*
        A job the card is late with, with no card (the buffer is memory, the descriptor none, the fence the fake's, the submit one the fake takes): the wait is
        in slices up to a tick and then gives the job up as late, pending, and the present is the processor's with no cool-down; the card is hung only by the
        late job's end (its fence answers an error, the kernel's log has a page fault since it was submitted, or it is two seconds old). The present is
        compositor_gpu_present on a headless 64 by 4 scanout held by a written map, drawn by the card when the job finishes.
*/
static void gpu_late_rows(void)
{
        unsigned char kept_gpu[sizeof(gpu)], kept_state[sizeof(compositor_gpu_state)], kept_gscene[sizeof(compositor_gscene)], kept_map[sizeof(gpu_maps[0])];
        unsigned int kept_count = gpu_map_count, kept_tick = gpu_tick;
        int kept_card = compositor.card, kept_gpu_on = compositor_gpu, kept_width = compositor.width, kept_height = compositor.height, kept_damages = compositor.damages;
        int kept_damage[4];
        unsigned int *kept_scanout = compositor.scanout;
        unsigned long kept_pitch = compositor.scanout_pitch, kept_bytes = compositor.scanout_bytes;
        positive base = GPU_DATA + 65536, ops_at = base, bins_at = base + 4096, d_at = base + 8192, now;
        unsigned int address_to ops = (unsigned int address_to)((p8 address_to)gpu_late_buffer + ops_at);
        unsigned int q[8] = {0, 0, 0, 64, 4, 0xff336699u, 0, 0};
        struct compositor_frame f;
        int drew, kept_waits;

        memory_copy(kept_gpu, &gpu, sizeof(gpu)), memory_copy(kept_state, &compositor_gpu_state, sizeof(compositor_gpu_state));
        memory_copy(kept_gscene, &compositor_gscene, sizeof(compositor_gscene)), memory_copy(kept_map, &gpu_maps[0], sizeof(gpu_maps[0]));
        memory_copy(kept_damage, compositor.damage[0], sizeof(kept_damage));
        gpu.fd = -1, gpu.ctx = 1, gpu.at = (unsigned int address_to)gpu_late_buffer, gpu.size = sizeof(gpu_late_buffer), gpu.imports = 0;
        gpu_fake_card = 1, gpu_tick = 100, gpu_map_count = 1;
        gpu_maps[0].from = (positive)gpu_late_screen, gpu_maps[0].to = (positive)gpu_late_screen + sizeof(gpu_late_screen), gpu_maps[0].va = GPU_VA_MAPS;
        gpu_maps[0].bytes = sizeof(gpu_late_screen), gpu_maps[0].handle = 7, gpu_maps[0].used = gpu_tick, gpu_maps[0].written = 1, gpu_maps[0].dma = 0, gpu_maps[0].handed = 0;
        compositor_gpu = 1, compositor.card = -1, compositor.width = 64, compositor.height = 4, compositor.scanout = gpu_late_screen, compositor.scanout_pitch = 64 * 4;
        compositor.scanout_bytes = sizeof(gpu_late_screen), compositor.damages = 1, compositor.damage[0][0] = 0, compositor.damage[0][1] = 0, compositor.damage[0][2] = 64;
        compositor.damage[0][3] = 4;
        memory_zero(&f, sizeof(f));
        gpu_late_reset();
        compositor_gpu_layout();
        // These rows hold the present whose jobs are waited for in place (compositor_card.nowait 0, the default); gpu_card_rows hold the one that is not.
        kept_waits = compositor_card.nowait, compositor_card.nowait = 0;

        // 1. A job the fence ends within the cap: the wait goes on in slices, three of them answered running, and the job is done: nothing pending, no end.
        memory_copy(ops, q, 32);
        gpu_fake_waits = 3;
        check("gpu late: a job the fence ends within the late cap is asked after again while it runs, and done", gpu_scene(ops_at, 1, bins_at, d_at, 64, 64, 4, 0, 0, 64, 4) &&
              !gpu.pending && gpu_end == GPU_END_NONE && gpu_fake_waits == 0 && gpu_found == GPU_END_NONE);

        // 2. A job still running at the cap: given up on as late with the job pending; the present is the processor's, and nothing is cooled, counted or restarted.
        gpu_late_reset();
        gpu_fake_idle = 0, gpu_fake_busy = 1;
        drew = compositor_gpu_present(&f, 0);
        check("gpu late: a job still running at the late cap is given up on as late, held as pending, its end not said", gpu.pending && gpu_end == GPU_END_LATE && !gpu_end_said);
        check("gpu late: and that present is the processor's", !drew);
        check("gpu late: no cool-down, no restart, no failure counted, the card not given up", compositor_gpu_state.cool_until == 0 && !compositor_gpu_state.restart_due &&
              compositor_gpu_state.failures == 0 && !compositor_gpu_state.dead && compositor_gpu_state.hung_at == 0 && compositor_gpu_state.cool_steps == 0);
        drew = compositor_gpu_present(&f, 0);
        check("gpu late: while the job may run the next present is the processor's too, still with no cool-down and the job still pending",
              !drew && gpu.pending && compositor_gpu_state.cool_until == 0 && compositor_gpu_state.hung_at == 0);

        // 3. The late job's fence then says done: it is over, nothing is escalated, and the card draws the next present.
        gpu_fake_busy = 0, gpu_fake_idle = 1;
        drew = compositor_gpu_present(&f, 0);
        check("gpu late: a late job that finishes costs nothing, and the card draws the next present", drew == 1 && !gpu.pending && gpu_found == GPU_END_NONE && compositor_gpu_state.cool_until == 0 &&
              compositor_gpu_state.failures == 0 && compositor_gpu_state.hung_at == 0);

        // 4. A late job two seconds old and still running takes the hang path: the card cools and is to start again, the present is the processor's.
        gpu_late_reset();
        gpu_fake_idle = 0, gpu_fake_busy = 1;
        compositor_gpu_present(&f, 0);
        gpu_job_began = clock_monotonic_nanoseconds() - (GPU_HANG_NS - 500000000ull); // the job is a second and a half old
        compositor_gpu_present(&f, 0);
        check("gpu late: a late job a second and a half old and still running is not yet a hang", gpu.pending && compositor_gpu_state.cool_until == 0 &&
              compositor_gpu_state.hung_at == 0);
        now = clock_monotonic_nanoseconds();
        gpu_job_began = now - GPU_HANG_NS; // and now it is two seconds old
        drew = compositor_gpu_present(&f, 0);
        check("gpu late: a late job two seconds old and still running takes the hang path (cool-down, restart due, the hang counted)", !drew && gpu_end == GPU_END_HUNG &&
              gpu_last_error == 62 && compositor_gpu_state.cool_until > now && compositor_gpu_state.restart_due && compositor_gpu_state.hung_at != 0 &&
              compositor_gpu_state.cool_steps == 1 && !compositor_gpu_state.dead);
        check("gpu late: the hang is said once (the job's end is said, the job still pending)", gpu_end_said && gpu.pending);

        // 5. A late job whose fence then answers an error is a hang, at its first look.
        gpu_late_reset();
        gpu_fake_idle = 0, gpu_fake_busy = 1;
        compositor_gpu_present(&f, 0);
        check("gpu late: before its fence answers an error the late job is nothing", gpu.pending && compositor_gpu_state.cool_until == 0);
        gpu_fake_busy = 0, gpu_fake_errno = 1;
        now = clock_monotonic_nanoseconds();
        drew = compositor_gpu_present(&f, 0);
        check("gpu late: a late job whose fence answers an error takes the hang path", !drew && gpu_end == GPU_END_WAIT && compositor_gpu_state.cool_until > now &&
              compositor_gpu_state.hung_at != 0 && compositor_gpu_state.restart_due);

        // 6. A page fault in the kernel's log since the late job was submitted is a hang, at the next look at the log.
        gpu_late_reset();
        gpu_fake_idle = 0, gpu_fake_busy = 1;
        compositor_gpu_present(&f, 0);
        gpu_fault[0] = 0, gpu_fake_fault = 1, gpu_fault_poll = clock_monotonic_nanoseconds() + 3600000000000ull;
        compositor_gpu_present(&f, 0);
        check("gpu late: the log is not looked at before its time (a frame does not pay for it)", gpu.pending && compositor_gpu_state.cool_until == 0 && !gpu_fault[0]);
        gpu_fault_poll = 0;
        now = clock_monotonic_nanoseconds();
        drew = compositor_gpu_present(&f, 0);
        check("gpu late: a page fault in the kernel's log since the late job was submitted takes the hang path", !drew && gpu_end == GPU_END_HUNG &&
              compositor_gpu_state.cool_until > now && compositor_gpu_state.hung_at != 0 && gpu_text_has(gpu_fault, "card faulted reading 0x2000000100"));

        gpu_late_reset();
        gpu_fake_card = 0, gpu_fake_idle = 0, gpu_fake_busy = 0;
        memory_copy(&gpu, kept_gpu, sizeof(gpu)), memory_copy(&compositor_gpu_state, kept_state, sizeof(compositor_gpu_state));
        memory_copy(&compositor_gscene, kept_gscene, sizeof(compositor_gscene)), memory_copy(&gpu_maps[0], kept_map, sizeof(gpu_maps[0]));
        memory_copy(compositor.damage[0], kept_damage, sizeof(kept_damage));
        gpu_map_count = kept_count, gpu_tick = kept_tick, gpu_fault[0] = 0;
        compositor.card = kept_card, compositor_gpu = kept_gpu_on, compositor.width = kept_width, compositor.height = kept_height, compositor.damages = kept_damages;
        compositor.scanout = kept_scanout, compositor.scanout_pitch = kept_pitch, compositor.scanout_bytes = kept_bytes;
        compositor_card.nowait = kept_waits;
}

/*
        The compositor's own memory through one import (gpu.c's gpu_map_as and gpu_region_of, and the order before it that the owner's file gpu-own-userptr
        asks for), with the fake kernel's udmabuf (its size limit 64 MB, as the kernel's size_limit_mb), PRIME and userptr: a picture in a sealed
        registered memfd is mapped by udmabuf first, the whole memfd in one dma-buf, so no userptr is made for it, a second picture in it is that map's, and a job's
        bo list names one handle for both; an unsealed one goes by userptr as before; a picture of the sealed window table is a dma-buf of its own pages (the table's
        rows are made and given back apart), pinned; and a udmabuf refused falls back to userptr.
*/
static long gpu_memfd_make(positive bytes, unsigned int seals, unsigned int address_to address_to at)
{
        long fd = system_call_2(syscall(memfd_create), (positive) "moonwater-gpu-own", 1 | 2), mapped;

        *at = 0;
        if (fd < 0)
                return fd;
        system_truncate_handle((int)fd, bytes);
        if (seals)
                system_call_3(syscall(fcntl), (positive)fd, 1033, seals);
        mapped = system_call_6(syscall(mmap), 0, bytes, 3, 1, (positive)fd, 0);
        if (mapped < 0 && mapped > -4096)
                return system_close((int)fd), -12;
        *at = (unsigned int address_to)mapped;
        return fd;
}

static void gpu_own_rows(void)
{
        enum { BYTES = 256 << 10, PIC = 16 << 10 };
        unsigned char kept_gpu[sizeof(gpu)];
        unsigned int kept_count = gpu_map_count, kept_tick = gpu_tick, address_to sealed, address_to loose, address_to table, address_to refused, userptrs, udmabufs;
        positive kept_next = gpu_map_next, base = GPU_DATA + 65536, ops_at = base, bins_at = base + 65536, d_at = base + 8192, va_a, va_b;
        unsigned int address_to ops = (unsigned int address_to)((p8 address_to)gpu_late_buffer + ops_at);
        long fd_sealed = gpu_memfd_make(BYTES, 2, &sealed), fd_open = gpu_memfd_make(BYTES, 0, &loose), fd_table = gpu_memfd_make(BYTES, 2, &table),
             fd_refused = gpu_memfd_make(BYTES, 2, &refused);

        if (fd_sealed < 0 || fd_open < 0 || fd_table < 0 || fd_refused < 0)
        {
                check("gpu own: memfds for the rows", false);
                return;
        }
        memory_copy(kept_gpu, &gpu, sizeof(gpu));
        gpu.fd = GPU_KERNEL_FD, gpu.ctx = 1, gpu.at = (unsigned int address_to)gpu_late_buffer, gpu.size = sizeof(gpu_late_buffer), gpu.imports = 0, gpu.pending = 0, gpu.broken = 0;
        memory_zero(&gpu_kernel, sizeof(gpu_kernel));
        gpu_kernel.on = 1, gpu_kernel.udmabuf = 1, gpu_kernel.udmabuf_limit = 64ul << 20, gpu_own_userptr = 0, gpu_maps_own_user = 0, gpu_maps_own_errno = 0;
        gpu_fake_card = 1, gpu_fake_busy = 0, gpu_fake_idle = 1, gpu_fake_waits = 0, gpu_fake_errno = 0, gpu_fake_userptr = 0, gpu_lost = 0;
        gpu_map_count = 0, gpu_map_next = GPU_VA_MAPS, gpu_tick = 100, gpu_userptr_off = 0, gpu_userptr_gone = 0, gpu_maps_user = 0, gpu_maps_dma = 0;
        gpu_before.sequence = 0, gpu_end_said = 0, gpu_found = GPU_END_NONE, gpu_pin_count = 0, gpu_pin_sum = 0;

        // 1. A picture in a sealed registered memfd: one dma-buf of the whole memfd, no userptr.
        gpu_register_memfd((int)fd_sealed, sealed, BYTES, 0);
        check("gpu own: a picture in a sealed registered memfd is mapped by udmabuf of the whole memfd, and no userptr is made",
              (gpu_va_bytes(sealed + 1024, PIC) || gpu_map(sealed + 1024, PIC)) && gpu_kernel.userptrs == 0 && gpu_kernel.udmabufs == 1 &&
                  gpu_kernel.udmabuf_memfd == (unsigned int)fd_sealed && gpu_kernel.udmabuf_offset == 0 && gpu_kernel.udmabuf_size == BYTES && gpu_map_count == 1 &&
                  gpu_maps_dma == 1 && gpu_maps_user == 0);
        va_a = gpu_va_bytes(sealed + 1024, PIC), va_b = gpu_va_bytes(sealed + 32768, PIC);
        check("gpu own: a second picture in it is found in that map: no map, no udmabuf, its address the same offset from the first",
              va_a && va_b == va_a + (32768 - 1024) * 4 && gpu_map_count == 1 && gpu_kernel.udmabufs == 1 && gpu_kernel.userptrs == 0);
        {
                const unsigned int q[16] = {4, 0, 0, 32, 4, (unsigned int)va_a, (unsigned int)(va_a >> 32), 32, 4, 32, 0, 64, 4, (unsigned int)va_b, (unsigned int)(va_b >> 32), 32};

                memory_copy(ops, q, sizeof(q));
                gpu_seen_count = 0;
                check("gpu own: a job drawing both names one handle in its bo list for them", gpu_scene(ops_at, 2, bins_at, d_at, 64, 64, 4, 0, 0, 64, 4) &&
                      gpu_seen_count == 1 && gpu_seen_bos[0] == gpu_maps[0].handle);
        }

        // 2. An unsealed registered memfd: udmabuf would refuse it; userptr, as before.
        gpu_register_memfd((int)fd_open, loose, BYTES, 0);
        userptrs = gpu_kernel.userptrs, udmabufs = gpu_kernel.udmabufs;
        check("gpu own: a picture in an unsealed registered memfd is mapped by userptr, with no udmabuf asked and no fallback counted",
              gpu_map(loose + 1024, PIC) && gpu_kernel.userptrs == userptrs + 1 && gpu_kernel.udmabufs == udmabufs && gpu_maps_user == 1 && gpu_maps_own_user == 0);

        // 3. The sealed window table: a picture is a dma-buf of its own pages, and its row's pin goes on.
        gpu_shared.fd = (int)fd_table, gpu_shared.at = (unsigned char address_to)table, gpu_shared.bytes = BYTES, gpu_shared.pin = gpu_fake_pin;
        userptrs = gpu_kernel.userptrs, udmabufs = gpu_kernel.udmabufs;
        check("gpu own: a picture of the sealed window table is a dma-buf of its own pages first, pinned, and no userptr is made",
              gpu_map(table + 2048, 8192) && gpu_kernel.udmabufs == udmabufs + 1 && gpu_kernel.udmabuf_offset == 8192 && gpu_kernel.udmabuf_size == 8192 &&
                  gpu_kernel.userptrs == userptrs && gpu_pin_sum == 1 && gpu_pin_offset == 8192);

        // 4. A sealed memfd udmabuf refuses: userptr after it.
        gpu_register_memfd((int)fd_refused, refused, BYTES, 0);
        gpu_kernel.udmabuf_error = 22, userptrs = gpu_kernel.userptrs, udmabufs = gpu_kernel.udmabufs;
        check("gpu own: a sealed memfd udmabuf refuses is mapped by userptr after it, counted as the compositor's own fallen back with the refusal's errno",
              gpu_map(refused + 1024, PIC) && gpu_kernel.userptrs == userptrs + 1 && (gpu_map_why & 255) == 6 && gpu_maps_own_user == 1 && gpu_maps_own_errno == 22);
        gpu_kernel.udmabuf_error = 0;

        // 5. A registered memfd past udmabuf's size limit (72 MB: EINVAL, as the kernel answers past size_limit_mb): userptr after it, and the present is drawn.
        {
                unsigned int address_to big;
                long fd_big = gpu_memfd_make(72ul << 20, 2, &big);
                positive va;

                gpu_register_memfd((int)fd_big, big, 72ul << 20, 0);
                userptrs = gpu_kernel.userptrs, udmabufs = gpu_kernel.udmabufs;
                check("gpu own: a memfd past udmabuf's size limit is refused whole (EINVAL) and its picture's slice is made by udmabuf instead, with no userptr",
                      fd_big >= 0 && gpu_map(big + 1024, PIC) && (gpu_map_why & 255) == 6 && (gpu_map_why >> 8) == 22 && gpu_kernel.udmabuf_size == PIC &&
                          gpu_kernel.udmabufs == udmabufs + 1 && gpu_kernel.userptrs == userptrs);
                va = gpu_va_bytes(big + 1024, PIC);
                {
                        const unsigned int q[8] = {4, 0, 0, 64, 4, (unsigned int)va, (unsigned int)(va >> 32), 64};

                        memory_copy(ops, q, sizeof(q));
                        check("gpu own: and the present that reads it is drawn", va && gpu_scene(ops_at, 1, bins_at, d_at, 64, 64, 4, 0, 0, 64, 4));
                }
                gpu_forget(big, 72ul << 20), gpu_trim(), gpu_unregister_memfd(big);
                if (fd_big >= 0)
                        system_call_2(syscall(munmap), (positive)big, 72ul << 20), system_close((int)fd_big);
        }

        // 6. With the owner's file gpu-own-userptr the order is the one before: a sealed registered memfd asks userptr first, of the picture's own pages.
        gpu_own_userptr = 1;
        {
                unsigned int address_to plain;
                long fd_plain = gpu_memfd_make(BYTES, 2, &plain);

                gpu_register_memfd((int)fd_plain, plain, BYTES, 0);
                userptrs = gpu_kernel.userptrs, udmabufs = gpu_kernel.udmabufs;
                check("gpu own: with gpu-own-userptr a picture in a sealed registered memfd is a userptr of its own pages, no udmabuf asked",
                      fd_plain >= 0 && gpu_map(plain + 1024, PIC) && gpu_kernel.userptrs == userptrs + 1 && gpu_kernel.udmabufs == udmabufs &&
                          gpu_maps[gpu_map_count - 1].bytes == PIC && !gpu_maps[gpu_map_count - 1].dma);
                gpu_userptr_gone = 1;
                check("gpu own: and once userptr is gone the dma-buf route is its fallback, of the picture's own pages", gpu_map(plain + 32768, PIC) &&
                      gpu_kernel.udmabufs == udmabufs + 1 && gpu_kernel.udmabuf_offset == 32768 * 4 && gpu_kernel.udmabuf_size == PIC);
                gpu_userptr_gone = 0;
                gpu_forget(plain, BYTES), gpu_trim(), gpu_unregister_memfd(plain);
                if (fd_plain >= 0)
                        system_call_2(syscall(munmap), (positive)plain, BYTES), system_close((int)fd_plain);
        }

        // 7. A userptr the kernel refuses with EFAULT (a client dma-buf's mapping: hmm_range_fault refuses a VM_IO or VM_PFNMAP range) before any userptr map has
        //    succeeded, on a card whose start took an anonymous page by userptr: the refusal is the address's, userptr is not gone for the session, and the refused
        //    map is kept to be said (other memory, step 5, its address and bytes).
        {
                long page = system_call_6(syscall(mmap), 0, 4096, 3, 0x4022, (positive)-1, 0);

                gpu_own_userptr = 0, gpu_userptr_ok = 1, gpu_maps_user = 0, gpu_fake_userptr = 14, gpu_userptr_gone = 0;
                check("gpu own: an EFAULT before any userptr map, on a card whose start took a page by userptr, leaves userptr on and the refusal kept as other memory",
                      !gpu_map((const void *)page, 4096) && !gpu_userptr_gone && gpu_map_refusal.kind == GPU_MAP_OTHER && gpu_map_refusal.why == 5 &&
                          gpu_map_refusal.at == (positive)page && gpu_map_refusal.bytes == 4096);
                gpu_userptr_ok = 0;
                check("gpu own: on a card whose start did not take a page by userptr, the same EFAULT makes userptr gone, as before",
                      !gpu_map((const void *)page, 4096) && gpu_userptr_gone);
                gpu_fake_userptr = 0, gpu_userptr_gone = 0;
                gpu_kernel.udmabuf_error = 22;
                check("gpu own: a picture of the table the dma-buf route refuses with userptr gone is kept as the table's, step 6",
                      (gpu_userptr_gone = 1, !gpu_map(table + 16384, 8192)) && gpu_map_refusal.kind == GPU_MAP_TABLE && (gpu_map_refusal.why & 255) == 6);
                gpu_kernel.udmabuf_error = 0, gpu_userptr_gone = 0;
                system_call_2(syscall(munmap), (positive)page, 4096);
        }

        while (gpu_map_count)
                gpu_drop(0, GPU_GONE_RECOVER);
        check("gpu own: the table's pin is let go of with its map", gpu_pin_sum == 0);
        gpu_unregister_memfd(sealed), gpu_unregister_memfd(loose), gpu_unregister_memfd(refused);
        gpu_shared.fd = -1, gpu_shared.at = 0, gpu_shared.bytes = 0, gpu_shared.pin = 0;
        system_call_2(syscall(munmap), (positive)sealed, BYTES), system_call_2(syscall(munmap), (positive)loose, BYTES);
        system_call_2(syscall(munmap), (positive)table, BYTES), system_call_2(syscall(munmap), (positive)refused, BYTES);
        system_close((int)fd_sealed), system_close((int)fd_open), system_close((int)fd_table), system_close((int)fd_refused);
        memory_zero(&gpu_kernel, sizeof(gpu_kernel));
        gpu_fake_card = 0, gpu_fake_idle = 0, gpu_own_userptr = 0, gpu_maps_own_user = 0, gpu_maps_own_errno = 0, gpu_handed_clear();
        memory_copy(&gpu, kept_gpu, sizeof(gpu));
        gpu_map_count = kept_count, gpu_map_next = kept_next, gpu_tick = kept_tick, gpu_maps_user = 0, gpu_maps_dma = 0, gpu_last_error = 0, gpu_end = GPU_END_NONE;
}

/*
        A blended window's round corners (compose.c's compositor_gpu_frame): its picture laid over by each inner corner's cover (12) by default, and refused for the
        processor (COMPOSITOR_GPU_WHY_ROUND) while the owner's file gpu-round-off is there (gpu.c's gpu_round_off), as before 12. The picture is a client's dma-buf
        already on the card (gpu_base): no map is asked.
*/
static void gpu_round_rows(void)
{
        unsigned char kept_gpu[sizeof(gpu)], kept_gscene[sizeof(compositor_gscene)];
        struct compositor_frame f;
        unsigned int twelves = 0, fives = 0;
        b32 built;

        memory_copy(kept_gpu, &gpu, sizeof(gpu)), memory_copy(kept_gscene, &compositor_gscene, sizeof(compositor_gscene));
        gpu.at = (unsigned int address_to)gpu_late_buffer, gpu.size = sizeof(gpu_late_buffer);
        compositor_gpu_layout();
        compositor_gscene.width = 256, compositor_gscene.height = 160;
        memory_zero(&f, sizeof(f));
        f.x = 20, f.y = 20, f.width = 120, f.height = 80, f.scale = 100, f.pixels = (const unsigned int *)gpu_late_screen, f.pitch = 120, f.gpu_base = GPU_VA_MAPS;
        f.blend = 1, f.radius = 12, f.line = 1, f.edge = 0xff404040u, f.slot = -1, f.square = 2; // one corner square, three round
        gpu_round_off = 0;
        built = compositor_gpu_frames(&f, 1, 0xff102030u);
        for (unsigned int k = 0; k < compositor_gscene.count; k++)
                twelves += (compositor_gscene.ops[8 * k] & 255) == 12, fives += (compositor_gscene.ops[8 * k] & 255) == 5;
        check("gpu round: a blended window's three round corners are laid over by their covers (12), each corner's line mixed (5)", built && twelves == 3 && fives == 3);
        gpu_round_off = 1;
        built = compositor_gpu_frames(&f, 1, 0xff102030u);
        check("gpu round: with gpu-round-off the same window is refused for the processor, as a window with alpha and round corners",
              !built && compositor_gscene.why == COMPOSITOR_GPU_WHY_ROUND && compositor_gscene.count == 0);
        f.square = 15;
        check("gpu round: and a blended window whose corners are all square is the card's with the file there too", compositor_gpu_frames(&f, 1, 0xff102030u));
        gpu_round_off = 0;
        memory_copy(&gpu, kept_gpu, sizeof(gpu)), memory_copy(&compositor_gscene, kept_gscene, sizeof(compositor_gscene));
}

/*
        The jobs without a wait (gpu.c's gpu_submit, gpu_scene_submit, gpu_done, gpu_fence_fd), on the fake card (no descriptor, a submit the
        fake takes and numbers, a fence it answers for every job alike): a scene is submitted and not waited for; the next is built in the other slot and the other
        side of the bins while it runs; a third waits for the first, and nothing of the first's is written; a job is asked after without a wait; its sync_file is
        the kernel's answer to AMDGPU_FENCE_TO_HANDLE; and a pending job's deadline, its fence's error and a refused submit end it, or keep it, as gpu.c says.
*/
// The value the last job's packets (the fake's copy) set a shader register to (SET_SH_REG, the last one that sets it): 0 when none does.
static unsigned int gpu_fake_ib_reg(unsigned int reg)
{
        unsigned int value = 0;

        for (unsigned int at = 0; at < gpu_fake_ib_words;)
        {
                unsigned int head = gpu_fake_ib[at], body = (head >> 16 & 0x3fff) + 1;

                if (head >> 30 != 3 || at + 1 + body > gpu_fake_ib_words)
                        break;
                if ((head >> 8 & 255) == 0x76 && reg >= gpu_fake_ib[at + 1] && reg - gpu_fake_ib[at + 1] + 1 < body)
                        value = gpu_fake_ib[at + 2 + reg - gpu_fake_ib[at + 1]];
                at += 1 + body;
        }
        return value;
}

static void gpu_async_rows(void)
{
        unsigned char kept_gpu[sizeof(gpu)], args0[sizeof(struct gpu_args)], bins0[4096];
        unsigned int kept_count = gpu_map_count, kept_tick = gpu_tick, was_ctx, submits;
        positive base = GPU_DATA + 65536, ops_at = base, bins_at = base + 65536, d_at = base + 8192, first, second, third, room, first_g, now, kept_began, kept_pending;
        unsigned int address_to ops = (unsigned int address_to)((p8 address_to)gpu_late_buffer + ops_at);
        const unsigned int q[8] = {0, 0, 0, 64, 4, 0xff336699u, 0, 0};
        const struct gpu_args address_to slot0 = (const struct gpu_args address_to)((p8 address_to)gpu_late_buffer + GPU_ARGS_AT(0));
        int fd;

        memory_copy(kept_gpu, &gpu, sizeof(gpu));
        gpu.fd = -1, gpu.ctx = 1, gpu.at = (unsigned int address_to)gpu_late_buffer, gpu.size = sizeof(gpu_late_buffer), gpu.imports = 0, gpu.pending = 0;
        gpu_before.sequence = 0, gpu_end_said = 0, gpu_found = GPU_END_NONE, gpu_lost = 0, gpu_slot = gpu_pending_slot = 0, gpu_bins_forget();
        gpu_fake_card = 1, gpu_fake_busy = 1, gpu_fake_idle = 0, gpu_fake_waits = 0, gpu_fake_errno = 0, gpu_map_count = 0, gpu_tick = 100;
        room = ((gpu.size - (1 << 20) - bins_at) / 2) & ~4095ull;
        memory_copy(ops, q, 32);

        // 1. A scene submitted without a wait: the pending job, in slot 0, binned in a side of the bins; the kernel is asked for no wait.
        gpu_fake_wait_calls = 0, gpu_fake_submits = 0;
        first = gpu_scene_submit(ops_at, 1, bins_at, d_at, 64, 64, 4, 0, 0, 64, 4);
        first_g = slot0->g;
        check("gpu async: a scene is submitted and not waited for: its sequence is the pending job's, built in slot 0, its bins in a side of the room",
              first && gpu.pending == first && gpu_pending_slot == 0 && gpu_fake_submits == 1 && gpu_fake_wait_calls == 0 && slot0->op == 6 &&
                  (first_g == GPU_VA + bins_at || first_g == GPU_VA + bins_at + room));
        check("gpu async: the job's packets are slot 0's and name slot 0's arguments", gpu_fake_ib_reg(0x244) == (unsigned int)(GPU_VA + GPU_ARGS_AT(0)));

        // 2. The next, while the first runs: the other slot, the other side, the first held as the job before it; still no wait.
        second = gpu_scene_submit(ops_at, 1, bins_at, d_at, 64, 64, 4, 0, 0, 64, 4);
        check("gpu async: the next scene is built while the first runs, in slot 1 and the other side of the bins, and the first is the job before it",
              second && second != first && gpu.pending == second && gpu_before.sequence == first && gpu_pending_slot == 1 && gpu_fake_submits == 2 &&
                  gpu_fake_wait_calls == 0 && gpu_slot == 1 && !memory_compare(GPU_ARGS, (p8 address_to)gpu_late_buffer + GPU_ARGS_AT(1), sizeof(struct gpu_args)) && GPU_ARGS->g != first_g &&
                  (GPU_ARGS->g == GPU_VA + bins_at || GPU_ARGS->g == GPU_VA + bins_at + room) && gpu_fake_ib_reg(0x244) == (unsigned int)(GPU_VA + GPU_ARGS_AT(1)));

        // 3. A third while both run: refused as late, and nothing of the first's (its arguments, its bins) written.
        memory_copy(args0, slot0, sizeof(args0)), memory_copy(bins0, (p8 address_to)gpu_late_buffer + (first_g - GPU_VA), sizeof(bins0));
        submits = gpu_fake_submits, gpu_fake_until = 1;
        third = gpu_scene_submit(ops_at, 1, bins_at, d_at, 64, 64, 4, 0, 0, 64, 4);
        check("gpu async: a third scene while both run is refused as late, and the first's arguments and bins are as they were",
              !third && gpu_end == GPU_END_LATE && gpu_fake_submits == submits && !memory_compare(args0, slot0, sizeof(args0)) &&
                  !memory_compare(bins0, (p8 address_to)gpu_late_buffer + (first_g - GPU_VA), sizeof(bins0)) && gpu.pending == second && gpu_before.sequence == first);
        check("gpu async: the slot was asked after without a wait (the time waited until is 0)", gpu_fake_until == 0);
        gpu_slot = gpu_pending_slot;
        check("gpu async: a submit of a job in the pending job's own slot is refused as late, and the kernel not asked", !gpu_submit() && gpu_end == GPU_END_LATE &&
              gpu_fake_submits == submits && gpu.pending == second);

        // 4. A job asked after: running, then done, without a wait; the job after it is still pending.
        gpu_fake_until = 1;
        check("gpu async: gpu_done says a running job is running, without a wait", gpu_done(first) == 0 && gpu_fake_until == 0 && gpu_before.sequence == first);
        check("gpu async: gpu_done says a job no pending job has (none, or one seen done) is done, and asks the kernel nothing",
              (submits = gpu_fake_wait_calls, gpu_done(0) == 1 && gpu_done(first + second + 100) == 1 && gpu_fake_wait_calls == submits));
        gpu_fake_busy = 0, gpu_fake_idle = 1;
        check("gpu async: once the fence says done the first job is seen done, and the second is still the pending one", gpu_done(first) == 1 && !gpu_before.sequence &&
              gpu.pending == second);

        // 5. A job's sync_file: the kernel is asked for the job's fence as a sync_file (the context, the compute ring, the sequence, GET_SYNC_FILE_FD).
        fd = gpu_fence_fd(second);
        check("gpu async: a job's sync_file is a descriptor, asked of the kernel for the compute ring's fence of its sequence",
              fd >= 0 && gpu_fake_fence_ask[0] == gpu.ctx && gpu_fake_fence_ask[1] == 1 && gpu_fake_fence_ask[2] == 0 && gpu_fake_fence_ask[3] == 0 &&
                  gpu_fake_fence_ask[4] == (unsigned int)second && gpu_fake_fence_ask[5] == (unsigned int)(second >> 32) && gpu_fake_fence_ask[6] == 2);
        if (fd >= 0)
                system_close(fd);
        gpu_fake_fence_error = 22;
        check("gpu async: a sync_file the kernel refuses is its errno, and one of no job is EINVAL", gpu_fence_fd(second) == -22 && gpu_fence_fd(0) == -22);
        gpu_fake_fence_error = 0;

        // 6. The third now: slot 0 again (the first is done), the first's side again; then another rectangle of it in slot 1, over the same lists.
        gpu_fake_busy = 1, gpu_fake_idle = 0;
        third = gpu_scene_submit(ops_at, 1, bins_at, d_at, 64, 64, 4, 0, 0, 64, 4);
        check("gpu async: with the first done the third is built in its slot and its side", third && gpu_pending_slot == 0 && slot0->g == first_g && gpu_before.sequence == second);
        submits = gpu_fake_submits;
        gpu_fake_busy = 0, gpu_fake_idle = 1;
        check("gpu async: the second is seen done, the third still pending", gpu_done(second) == 1 && !gpu_before.sequence && gpu.pending == third);
        gpu_fake_busy = 1, gpu_fake_idle = 0;
        submits = gpu_fake_submits;
        check("gpu async: a scene that waits is refused while jobs are pending, and nothing submitted",
              !gpu_scene(ops_at, 1, bins_at, d_at, 64, 64, 4, 0, 0, 64, 4) && gpu_fake_submits == submits && gpu.pending == third);

        // 7. A refused submit while a job is pending leaves the pending job as it was (its sequence, its submit's time, the one before it).
        gpu_fake_busy = 0, gpu_fake_idle = 1, gpu_done(third);
        gpu_fake_busy = 1, gpu_fake_idle = 0, gpu_fake_again = 40;
        kept_pending = gpu.pending, kept_began = gpu_job_began;
        check("gpu async: a submit the kernel keeps refusing is not taken, and the pending job is as it was",
              !gpu_scene_submit(ops_at, 1, bins_at, d_at, 64, 64, 4, 0, 0, 64, 4) && gpu.pending == kept_pending && gpu_job_began == kept_began && !gpu_before.sequence);
        gpu_fake_again = 0;

        // 8. The deadline: a pending job within it is nothing; past it, it is hung, said once; the oldest job the ring has not done is the one whose deadline counts.
        gpu_fake_busy = 1, gpu_fake_idle = 0, gpu_last_error = 0;
        check("gpu async: a job is pending for the deadline's rows", gpu_scene_submit(ops_at, 1, bins_at, d_at, 64, 64, 4, 0, 0, 64, 4) && gpu.pending);
        now = clock_monotonic_nanoseconds();
        gpu_end_said = 0, gpu_found = GPU_END_NONE, gpu_job_began = now - (GPU_HANG_NS - 500000000ull), gpu_fault_poll = now + 3600000000000ull, gpu_fake_fault = 0;
        check("gpu async: a pending job a second and a half old is within its deadline: nothing is said", gpu_late_end() == GPU_END_NONE && !gpu_end_said);
        gpu_job_began = now - GPU_HANG_NS;
        check("gpu async: a pending job past its deadline is hung, said once", gpu_late_end() == GPU_END_HUNG && gpu_end_said && gpu_last_error == 62 &&
              gpu_late_end() == GPU_END_NONE && gpu.pending);
        gpu_end_said = 0, gpu_job_began = now, gpu_before.sequence = gpu.pending + 1, gpu_before.began = now - GPU_HANG_NS;
        check("gpu async: the job before the pending one, still running past its own deadline, is the hung one", gpu_late_end() == GPU_END_HUNG && gpu_end_said);
        gpu_before.sequence = 0;

        // 9. A pending job whose fence answers an error: over, on a new context, and its end said once as a failed wait.
        gpu_end_said = 0, gpu_found = GPU_END_NONE, gpu_fake_busy = 0, gpu_fake_errno = 1, was_ctx = gpu.ctx, gpu_lost = 0;
        check("gpu async: a pending job whose fence answers an error is over, the jobs after on a new context", gpu_done(gpu.pending) == -1 && !gpu.pending &&
              gpu.ctx != was_ctx && !gpu_lost && !gpu_bins.job[0] && !gpu_bins.job[1]);
        gpu_fake_errno = 0;
        check("gpu async: and its end is said once, as a failed wait", gpu_late_end() == GPU_END_WAIT && gpu_late_end() == GPU_END_NONE);

        // 10. The jobs counted apart: waited (gpu_run) and not (gpu_go), each kind (a scene, a copy), and a waited job's time split into its submit and the rest.
        {
                unsigned int waited = gpu_jobs_waited, unwaited = gpu_jobs_unwaited, scenes = gpu_jobs_kind[0], copies = gpu_jobs_kind[1];
                unsigned long long whole = gpu_job_ns, parts = gpu_job_submit_ns + gpu_job_wait_ns;
                b32 ran;

                gpu_fake_busy = 0, gpu_fake_idle = 1, gpu.pending = 0, gpu_before.sequence = 0;
                ran = gpu_scene(ops_at, 1, bins_at, d_at, 64, 64, 4, 0, 0, 64, 4) && gpu_blit(GPU_VA + d_at, 64, GPU_VA + d_at + 4096, 64, 0, 0, 64, 4);
                ran = ran && gpu_scene_submit(ops_at, 1, bins_at, d_at, 64, 64, 4, 0, 0, 64, 4) != 0;
                check("gpu async: jobs are counted waited and not, by kind, and a waited job's time is its submit's and its wait's",
                      ran && gpu_jobs_waited == waited + 2 && gpu_jobs_unwaited == unwaited + 1 && gpu_jobs_kind[0] == scenes + 2 && gpu_jobs_kind[1] == copies + 1 &&
                          gpu_job_submit_ns + gpu_job_wait_ns - parts == gpu_job_ns - whole && gpu_job_ns > whole);
        }

        gpu_fake_card = 0, gpu_fake_busy = 0, gpu_fake_idle = 0, gpu_fake_errno = 0, gpu_lost = 0, gpu_handed_clear();
        gpu_before.sequence = 0, gpu_end_said = 0, gpu_found = GPU_END_NONE, gpu_slot = gpu_pending_slot = 0, gpu_bins_forget();
        memory_copy(&gpu, kept_gpu, sizeof(gpu));
        gpu_map_count = kept_count, gpu_tick = kept_tick, gpu_last_error = 0, gpu_end = GPU_END_NONE, gpu_fault[0] = 0;
}

/*
        The card's present without a wait (compositor_card: present-nowait), on the fake card and the fake fence (gpu_fake_busy: every job running, gpu_fake_idle:
        every job done), on a headless 2048 by 64 scanout held by a written map: a present's jobs are submitted and none is waited for; the present is held,
        the clients' doors and sockets are not read while the jobs are younger than GPU_LATE_NS and are read again after, the input devices read whatever;
        what the jobs read stays theirs (compositor_card_reads) until the fence says done or the hang path ends it; a present of three rectangles has its
        third submitted when the first is done; its end is the present's (the native rows held for it given back, its time noted); and on a card whose
        primary plane takes IN_FENCE_FD the flip is committed with the job's sync_file, on one that does not (or refuses it) flipped the legacy way at the end.
*/
static unsigned int gpu_card_buffer[(GPU_DATA + (6 << 20)) / 4]; // the fake card's buffer: the ops, the tables, the scaled tables and both sides of the bins
static unsigned int gpu_card_screen[2048 * 64];                  // and the scanout
static struct windows *gpu_card_table_scratch; // the table, mapped at its first use (windows_scratch_table)

static positive gpu_card_present(const int (*rect)[4], unsigned int rects)
{
        struct compositor_frame f;

        memory_zero(&f, sizeof(f));
        compositor.damages = rects;
        for (unsigned int at = 0; at < rects; at++)
                memory_copy(compositor.damage[at], rect[at], sizeof(compositor.damage[at]));
        return (positive)compositor_gpu_present(&f, 0);
}

static void gpu_card_rows(void)
{
        static unsigned char kept_comp[sizeof(compositor)], kept_gpu[sizeof(gpu)], kept_state[sizeof(compositor_gpu_state)], kept_gscene[sizeof(compositor_gscene)];
        static unsigned char kept_map[sizeof(gpu_maps[0])], kept_card[sizeof(compositor_card)];
        unsigned int kept_count = gpu_map_count, kept_tick = gpu_tick, kept_metric = compositor_metric_card_count, submits, flips, atomics;
        int kept_gpu_on = compositor_gpu;
        const int one[1][4] = {{0, 0, 64, 64}}, three[3][4] = {{0, 0, 64, 64}, {1024, 0, 1088, 64}, {1984, 0, 2048, 64}};
        const unsigned int rows[1] = {3};
        const unsigned short clip[1][4] = {{0, 0, 64, 64}};
        struct compositor_poll *wanted = compositor_polls();
        struct compositor_slots held, open;
        positive now, drew;

        memory_copy(kept_comp, &compositor, sizeof(compositor)), memory_copy(kept_gpu, &gpu, sizeof(gpu));
        memory_copy(kept_state, &compositor_gpu_state, sizeof(compositor_gpu_state)), memory_copy(kept_gscene, &compositor_gscene, sizeof(compositor_gscene));
        memory_copy(kept_map, &gpu_maps[0], sizeof(gpu_maps[0])), memory_copy(kept_card, &compositor_card, sizeof(compositor_card));
        windows_scratch_zero(windows_scratch_table(&gpu_card_table_scratch));
        gpu.fd = -1, gpu.ctx = 1, gpu.at = (unsigned int address_to)gpu_card_buffer, gpu.size = sizeof(gpu_card_buffer), gpu.imports = 0;
        gpu_before.sequence = 0, gpu_slot = gpu_pending_slot = 0, gpu_bins_forget();
        gpu_fake_card = 1, gpu_tick = 100, gpu_map_count = 1;
        gpu_maps[0].from = (positive)gpu_card_screen, gpu_maps[0].to = (positive)gpu_card_screen + sizeof(gpu_card_screen), gpu_maps[0].va = GPU_VA_MAPS;
        gpu_maps[0].bytes = sizeof(gpu_card_screen), gpu_maps[0].handle = 7, gpu_maps[0].used = gpu_tick, gpu_maps[0].written = 1, gpu_maps[0].dma = 0, gpu_maps[0].handed = 0;
        compositor_gpu = 1, compositor.card = -1, compositor.table = windows_scratch_table(&gpu_card_table_scratch), compositor.width = 2048, compositor.height = 64;
        compositor.scanout = gpu_card_screen, compositor.scanout_pitch = 2048 * 4, compositor.scanout_bytes = sizeof(gpu_card_screen);
        compositor.direct_row = compositor.flip_row = compositor.plane_row = -1, compositor.flip_pending = 0, compositor.held_rows = rows_from(0), compositor.woken = rows_from(0);
        compositor_card_zero();
        compositor_card.nowait = 1, compositor_card.looped = 1; // the loop's present, without a wait
        gpu_late_reset();
        compositor_gpu_layout();

        // 1. A present of one rectangle: its job submitted and not waited for, the present the card's, the job on the card.
        gpu_fake_idle = 0, gpu_fake_busy = 1, gpu_fake_blocking = 0, submits = gpu_fake_submits;
        drew = gpu_card_present(one, 1);
        check("gpu card: a present's job is submitted and not waited for, and the present is the card's", drew == 1 && compositor_card.on && gpu.pending &&
              gpu_fake_submits == submits + 1 && gpu_fake_blocking == 0 && compositor_card.steps == 1 && compositor_card.next == 1);
        now = clock_monotonic_nanoseconds();
        compositor_card_after(clip, 1, 0, 1, 0, 0, 0, rows, 1), compositor_card_began(now);
        check("gpu card: the next present is held while its job runs", compositor_card_holds());
        check("gpu card: and the picture of the row it reads is the job's, no other's", compositor_card_reads(3) && !compositor_card_reads(4));
        check("gpu card: the clients are held while the job is younger than GPU_LATE_NS, and not after",
              compositor_card_clients_held(now + 1000000ull) && compositor_card_clients_held(now + GPU_LATE_NS - 1) && !compositor_card_clients_held(now + GPU_LATE_NS));
        {
                int kept_door = compositor.door[2], kept_input = compositor.inputs[0];
                unsigned int kept_inputs = compositor.input_count;
                struct compositor_rows kept_busy = compositor_doors.busy;

                compositor.door[2] = 5, compositor.inputs[0] = 6, compositor.input_count = 1, compositor_doors.busy = rows_from(0);
                compositor_loop_wanted(wanted, &held, 1);
                check("gpu card: while the clients are held no door and no Wayland socket is polled, the input devices are, and the job's sync_file is",
                      wanted[held.inputs].handle == 6 && wanted[held.doors + 2].handle == -1 && held.doors == held.inputs + 1 && !held.wayland_at && !held.done_at &&
                          held.fence_at && wanted[held.fence_at].handle >= 0 && wanted[held.fence_at].handle == compositor_card.fence - 1);
                compositor_loop_wanted(wanted, &open, 0);
                check("gpu card: with them not held the doors and the sockets are polled again, in the same places", wanted[open.inputs].handle == 6 &&
                      wanted[open.doors + 2].handle == 5 && open.doors == held.doors && open.wayland_at && open.fence_at);
                compositor.door[2] = kept_door, compositor.inputs[0] = kept_input, compositor.input_count = kept_inputs, compositor_doors.busy = kept_busy;
        }

        // 2. A fence that never signals: the turn asks without a wait, the clients are read again past GPU_LATE_NS, what the job reads stays the job's until the
        // hang path ends it, and then the frame is drawn again (the whole space damaged), the present the processor's.
        compositor_card_tick();
        check("gpu card: a turn while the job runs asks the card without a wait and leaves the present on it", compositor_card.on && gpu_fake_blocking == 0);
        now = clock_monotonic_nanoseconds();
        compositor_card.began = now - GPU_LATE_NS - 1, gpu_job_began = now - (GPU_HANG_NS - 500000000ull);
        compositor_card_tick();
        check("gpu card: a job late past GPU_LATE_NS has the clients read again, and what it reads still its own", !compositor_card_clients_held(now) &&
              compositor_card.on && compositor_card_reads(3) && compositor_card_holds());
        gpu_job_began = now - GPU_HANG_NS;
        compositor_card_tick();
        check("gpu card: at its deadline the hang path ends it: the card cools down, the frame is given up and drawn again whole, and nothing is the job's",
              !compositor_card.on && !compositor_card_reads(3) && gpu_end == GPU_END_HUNG && compositor_gpu_state.cool_until > now && compositor.damages == 1 &&
                  compositor.damage[0][2] == 2048 && compositor.damage[0][3] == 64 && !compositor_card.fence);
        check("gpu card: after it the processor draws, the hung job pending or not", !compositor_card_holds() && gpu_fake_blocking == 0);

        // 3. A job that ends: the present's end when the fence says so, the native row held for it given back, its time the card's.
        gpu_late_reset();
        gpu_fake_idle = 0, gpu_fake_busy = 1, windows_scratch_table(&gpu_card_table_scratch)->shown[3] = 0;
        drew = gpu_card_present(one, 1);
        compositor_card_after(clip, 1, 0, 1, 0, 0, 0, rows, 1), compositor_card_began(clock_monotonic_nanoseconds() - 1000000ull);
        compositor.held_rows = rows_from(1u << 3), compositor.taken_held[3] = 77, compositor.woken = rows_from(0); // its door's taken held for the job (compositor_take)
        kept_metric = compositor_metric_card_count;
        gpu_fake_busy = 0, gpu_fake_idle = 1;
        compositor_card_tick();
        check("gpu card: once the fence says done the present ends: nothing on the card, its time noted as the card's, a millisecond or more",
              drew == 1 && !compositor_card.on && !compositor_card_reads(3) && compositor_metric_card_count == kept_metric + 1 &&
                  compositor_metric_card[(compositor_metric_card_at + WINDOWS_METRIC_RUN - 1) % WINDOWS_METRIC_RUN] >= 1000000ull && !compositor_card.fence);
        check("gpu card: and the window held for the job is told its picture was taken", windows_scratch_table(&gpu_card_table_scratch)->shown[3] == 77 && !(rows_low(&compositor.held_rows) & (1u << 3)) &&
              (rows_low(&compositor.woken) & (1u << 3)));

        // 4. Three rectangles: two jobs on the card, the third submitted once the first is done, none waited for.
        gpu_late_reset();
        gpu_fake_idle = 0, gpu_fake_busy = 1, gpu_fake_blocking = 0, submits = gpu_fake_submits;
        drew = gpu_card_present(three, 3);
        check("gpu card: a present of three rectangles has two jobs on the card and the third waiting for the first", drew == 1 && compositor_card.on &&
              compositor_card.steps == 3 && compositor_card.next == 2 && gpu_fake_submits == submits + 2 && compositor_card_awaited() == gpu_before.sequence &&
              gpu_before.sequence);
        compositor_card_after(clip, 1, 0, 1, 0, 0, 0, rows, 1), compositor_card_began(clock_monotonic_nanoseconds());
        compositor_card_tick();
        check("gpu card: while both run the third waits", compositor_card.next == 2 && gpu_fake_submits == submits + 2);
        gpu_fake_busy = 0, gpu_fake_idle = 1;
        compositor_card_tick();
        check("gpu card: when they are done it is submitted, and the present ends when it is, nothing waited for", !compositor_card.on &&
              gpu_fake_submits == submits + 3 && gpu_fake_blocking == 0);

        // 5. A card whose primary plane takes IN_FENCE_FD: the flip of the plate composed into is committed with the job's sync_file, does not wait, has its event;
        // at the end nothing is flipped again.
        if (card_fake_open(2048, 64))
        {
                compositor.card = -1, compositor.overlay_state = 0, compositor.primary = 0, compositor.crtc_index = 0, compositor.crtc = CARD_FAKE_CRTC;
                compositor_fence_prop = 0, card_fake.in_fence = 1, card_fake.page_flips = 1;
                gpu_late_reset();
                gpu_fake_idle = 0, gpu_fake_busy = 1;
                drew = gpu_card_present(one, 1);
                compositor.card = card_fake.fd, compositor.fb = 100, compositor.on_screen = 101, compositor.flip_pending = 0;
                submits = (unsigned int)compositor_metric_flips_jobs;
                compositor_card_after(clip, 1, 0, 1, 1, 0, 0, rows, 1), compositor_card_began(clock_monotonic_nanoseconds());
                check("gpu card: the flip is committed with the job's sync_file as the primary plane's IN_FENCE_FD, not waiting, with its event",
                      drew == 1 && compositor_card.fenced && compositor_card.fence && card_fake.atomic_in_fence == compositor_card.fence - 1 &&
                          card_fake.atomic_fb == 100 && (card_fake.atomic_flags & COMPOSITOR_ATOMIC_NONBLOCK) && (card_fake.atomic_flags & COMPOSITOR_FLIP_EVENT) &&
                          card_fake.atomic_user == compositor.flip_serial && compositor.flip_pending && compositor.on_screen == 100);
                check("gpu card: and it is counted as a flip with a job in flight", compositor_metric_flips_jobs == submits + 1 && compositor_metric_flip_jobs_most >= 1);
                gpu_fake_busy = 0, gpu_fake_idle = 1, flips = card_fake.flips;
                compositor_card_tick();
                check("gpu card: at the end of its jobs the fenced flip is not flipped again", !compositor_card.on && card_fake.flips == flips);

                // 6. A commit the card refuses for a flip in flight (EBUSY): this present is flipped the legacy way when its jobs are done, the fence kept for the next.
                compositor.flip_pending = 0, compositor.on_screen = 101, card_fake.atomic_errno = 16;
                gpu_late_reset();
                gpu_fake_idle = 0, gpu_fake_busy = 1;
                compositor.card = -1, drew = gpu_card_present(one, 1), compositor.card = card_fake.fd;
                compositor_card_after(clip, 1, 0, 1, 1, 0, 0, rows, 1), compositor_card_began(clock_monotonic_nanoseconds());
                check("gpu card: a commit refused as busy is no fenced flip, and the property is kept", !compositor_card.fenced && !compositor.flip_pending &&
                      compositor_fence_prop == 13);
                gpu_fake_busy = 0, gpu_fake_idle = 1, flips = card_fake.flips;
                compositor_card_tick();
                check("gpu card: and the frame is flipped the legacy way once its jobs are done", !compositor_card.on && card_fake.flips == flips + 1 &&
                      card_fake.flip_fb == 100 && compositor.flip_pending);

                // 7. A plane with no IN_FENCE_FD: no commit is tried, and the end flips the legacy way.
                compositor.flip_pending = 0, compositor.on_screen = 101, card_fake.atomic_errno = 0, card_fake.in_fence = 0, compositor_fence_prop = 0;
                compositor.overlay_state = 0, compositor.primary = 0;
                gpu_late_reset();
                gpu_fake_idle = 0, gpu_fake_busy = 1;
                compositor.card = -1, drew = gpu_card_present(one, 1), compositor.card = card_fake.fd;
                atomics = card_fake.atomics;
                compositor_card_after(clip, 1, 0, 1, 1, 0, 0, rows, 1), compositor_card_began(clock_monotonic_nanoseconds());
                check("gpu card: a primary plane with no IN_FENCE_FD has no commit tried", !compositor_card.fenced && card_fake.atomics == atomics &&
                      compositor_fence_prop == -1 && !compositor.flip_pending);
                gpu_fake_busy = 0, gpu_fake_idle = 1, flips = card_fake.flips;
                compositor_card_tick();
                check("gpu card: and its frame is flipped the legacy way at the end", !compositor_card.on && card_fake.flips == flips + 1 && compositor.flip_pending);
                card_fake_close();
        }
        else
                check("gpu card: the fake card is made", false);

        gpu_late_reset();
        gpu_fake_card = 0, gpu_fake_idle = 0, gpu_fake_busy = 0, gpu_lost = 0, gpu_handed_clear();
        gpu_before.sequence = 0, gpu_end_said = 0, gpu_found = GPU_END_NONE, gpu_slot = gpu_pending_slot = 0, gpu_bins_forget();
        if (compositor_card.fence)
                system_close(compositor_card.fence - 1);
        compositor_fence_prop = 0;
        memory_copy(&compositor, kept_comp, sizeof(compositor)), memory_copy(&gpu, kept_gpu, sizeof(gpu));
        memory_copy(&compositor_gpu_state, kept_state, sizeof(compositor_gpu_state)), memory_copy(&compositor_gscene, kept_gscene, sizeof(compositor_gscene));
        memory_copy(&gpu_maps[0], kept_map, sizeof(gpu_maps[0])), memory_copy(&compositor_card, kept_card, sizeof(compositor_card));
        gpu_map_count = kept_count, gpu_tick = kept_tick, gpu_last_error = 0, gpu_end = GPU_END_NONE, gpu_fault[0] = 0, compositor_gpu = kept_gpu_on;
}

/*
        The scaled pool (compositor_gpu_scale_frames): a present of more scaled frames than the sixteen it had room for (the Scenes overview draws every
        window twice, large and on its Scene's card) stays on the card. On the fake card, 24 frames of one lent picture, each scaled to a size of its own,
        are 24 blits of the present, and the present is the card's; each frame drawn is a picture of its own in the one pool, none over another, and each
        blit reads the lent picture at its own address (nothing of it copied). Frames past COMPOSITOR_GPU_SCALED are refused as before.
*/
static unsigned int gpu_scaled_source[256 * 128];

static void gpu_scaled_pool_rows(void)
{
        static unsigned char kept_comp[sizeof(compositor)], kept_gpu[sizeof(gpu)], kept_card[sizeof(compositor_card)], kept_map[sizeof(gpu_maps[0])];
        enum { N = 24 };
        static struct compositor_frame frames[COMPOSITOR_GPU_SCALED + 1];
        const struct compositor_frame *drawn = frames;
        const positive lent_va = 0x500000000000ull;
        unsigned int kept_count = gpu_map_count, kept_tick = gpu_tick;
        int kept_gpu_on = compositor_gpu, own = 1, in_place = 1;
        enum compositor_gpu_why why;

        memory_copy(kept_comp, &compositor, sizeof(compositor)), memory_copy(kept_gpu, &gpu, sizeof(gpu)), memory_copy(kept_card, &compositor_card, sizeof(compositor_card));
        memory_copy(kept_map, &gpu_maps[0], sizeof(gpu_maps[0]));
        gpu.fd = -1, gpu.ctx = 1, gpu.at = (unsigned int address_to)gpu_card_buffer, gpu.size = sizeof(gpu_card_buffer), gpu.imports = 0;
        gpu_fake_card = 1, gpu_tick = 100, compositor_gpu = 1;
        compositor_card_zero();
        compositor_card.nowait = 1, compositor_card.looped = 1;
        for (unsigned int k = 0; k <= COMPOSITOR_GPU_SCALED; k++)
        {
                memory_zero(&frames[k], sizeof(frames[k]));
                frames[k].x = (int)(k % 8) * 100, frames[k].y = (int)(k / 8) * 60, frames[k].width = 80 + (int)k, frames[k].height = 40 + (int)(k % 5);
                frames[k].scale = 100, frames[k].pixels = gpu_scaled_source, frames[k].pitch = 256, frames[k].gpu_base = lent_va;
                frames[k].source_w = 256, frames[k].source_h = 128;
        }
        // The pool, and the card's map of it as a written map of the fake card (this check's user may not open udmabuf, which maps a memfd).
        if (!compositor_gpu_scaled_room(1 << 20))
        {
                check("gpu scaled: the pool is made", false);
                return;
        }
        gpu_maps[0].from = (positive)compositor_gpu_scaled_pool.memory, gpu_maps[0].to = gpu_maps[0].from + compositor_gpu_scaled_pool.bytes;
        gpu_maps[0].va = GPU_VA_MAPS, gpu_maps[0].bytes = compositor_gpu_scaled_pool.bytes, gpu_maps[0].handle = 7, gpu_maps[0].used = gpu_tick;
        gpu_maps[0].written = 1, gpu_maps[0].dma = 0, gpu_maps[0].handed = 0, gpu_map_count = 1;
        why = compositor_gpu_scale_frames(&drawn, N, 0, 0);
        check("gpu scaled: 24 scaled frames, more than the 16 a present had room for, are made by the card in 24 blits, the present the card's",
              why == COMPOSITOR_GPU_WHY_DRAWN && drawn != frames && compositor_card.steps == N);
        if (why == COMPOSITOR_GPU_WHY_DRAWN && drawn != frames)
                for (unsigned int k = 0; k < N; k++)
                {
                        const unsigned int *pool = compositor_gpu_scaled_pool.memory;

                        own &= drawn[k].pixels >= pool && drawn[k].pixels + drawn[k].pitch * (positive)drawn[k].height <= pool + compositor_gpu_scaled_pool.bytes / 4 &&
                               !compositor_scaled(&drawn[k]) && drawn[k].width == frames[k].width && drawn[k].gpu_base != lent_va;
                        if (k)
                                own &= drawn[k].pixels >= drawn[k - 1].pixels + drawn[k - 1].pitch * (positive)drawn[k - 1].height;
                        in_place &= compositor_card.step[k].src == lent_va && compositor_card.step[k].dst == drawn[k].gpu_base &&
                                    frames[k].pixels == gpu_scaled_source && frames[k].gpu_base == lent_va;
                }
        check("gpu scaled: each frame drawn is a picture of its own in the one pool, none over another", own);
        check("gpu scaled: each blit reads the lent picture at its own address, and the caller's frames are as they were (nothing copied)", in_place);
        compositor_card_zero();
        compositor_card.nowait = 1, compositor_card.looped = 1, drawn = frames;
        check("gpu scaled: a frame past COMPOSITOR_GPU_SCALED is refused, the present the processor's",
              compositor_gpu_scale_frames(&drawn, COMPOSITOR_GPU_SCALED + 1, 0, 0) == COMPOSITOR_GPU_WHY_SCALED);
        gpu_fake_card = 0, gpu_lost = 0, gpu_handed_clear();
        memory_copy(&compositor, kept_comp, sizeof(compositor)), memory_copy(&gpu, kept_gpu, sizeof(gpu)), memory_copy(&compositor_card, kept_card, sizeof(compositor_card));
        memory_copy(&gpu_maps[0], kept_map, sizeof(gpu_maps[0]));
        gpu_map_count = kept_count, gpu_tick = kept_tick, gpu_last_error = 0, gpu_end = GPU_END_NONE, compositor_gpu = kept_gpu_on;
}

/*
        A window's list drawn without a wait (compositor_card.lists_nowait, unless /run/moonwater/lists-wait), on the fake card and the fake fence, the list one
        box over a 64 by 4 picture held by a written map: a list's records are cut into their own half of the ops' megabyte and a present's into the other;
        its job is submitted and not waited for, the frame said drawn with its damage; the row's picture is the job's (compositor_card_reads, which the
        door's taken and a flip's end wait on) and a present is held while it runs, the clients while it is younger than GPU_LATE_NS, and the doors that
        knocked with it wait for a later turn; another list while it runs is refused as the card's being busy, for its row or another, nothing submitted
        and nothing counted against the card; the loop's turn gives the row's door back once the fence says done; a job that never signals has its row
        given back by the hang path at its deadline, and one whose fence answers an error or that a restart of the card took has it at once, the list
        refused (list_state 2) so the program paints that frame. Without the file the list is drawn and waited for in place, as it was.
*/
static unsigned char gpu_lists_memory[64 << 10] __attribute__((aligned(4096)));
static unsigned int gpu_lists_picture[64 * 4] __attribute__((aligned(4096)));

// A frame of the program's: one box of argb over its whole picture, recorded into the list and published (ui_scene_end) as the kit publishes one.
static b32 gpu_lists_publish(unsigned int argb)
{
        static struct ui lui;
        static struct ui_draw_list list;
        struct ui_box box = {0, 0, 64 << 8, 4 << 8, 0, 0};

        memory_zero(&lui, sizeof(lui));
        lui.width = 64, lui.height = 4, lui.pitch = 64, lui.top = 0, lui.bottom = 4;
        if (!list.ready && !ui_draw_list_init(&list, gpu_lists_memory, sizeof(gpu_lists_memory), 64))
                return false;
        if (!ui_scene_begin(&lui, &list))
                return false;
        ui_scene_box(&lui, &box, argb, false, 0, null);
        return ui_scene_end(&lui, &list);
}

static int gpu_lists_render(int row, int *damage)
{
        return compositor_gpu_list_render_bytes(row, gpu_lists_memory, sizeof(gpu_lists_memory), gpu_lists_picture, 64, 64, 4, damage);
}

static void gpu_lists_nowait_rows(void)
{
        static unsigned char kept_comp[sizeof(compositor)], kept_gpu[sizeof(gpu)], kept_state[sizeof(compositor_gpu_state)], kept_gscene[sizeof(compositor_gscene)];
        static unsigned char kept_maps[2 * sizeof(gpu_maps[0])], kept_card[sizeof(compositor_card)];
        unsigned int kept_count = gpu_map_count, kept_tick = gpu_tick, kept_recovered = gpu_recovered, submits, failures;
        unsigned int address_to present_ops = (unsigned int address_to)((p8 address_to)gpu_card_buffer + COMPOSITOR_GPU_OPS_AT);
        unsigned int address_to list_ops = (unsigned int address_to)((p8 address_to)gpu_card_buffer + COMPOSITOR_GPU_LIST_OPS_AT);
        int kept_gpu_on = compositor_gpu, damage[4] = {-1, -1, -1, -1}, drawn;
        struct compositor_poll *wanted = compositor_polls();
        struct compositor_slots slots;
        positive now, sequence;
        b32 published;

        memory_copy(kept_comp, &compositor, sizeof(compositor)), memory_copy(kept_gpu, &gpu, sizeof(gpu));
        memory_copy(kept_state, &compositor_gpu_state, sizeof(compositor_gpu_state)), memory_copy(kept_gscene, &compositor_gscene, sizeof(compositor_gscene));
        memory_copy(kept_maps, gpu_maps, sizeof(kept_maps)), memory_copy(kept_card, &compositor_card, sizeof(compositor_card));
        windows_scratch_zero(windows_scratch_table(&gpu_card_table_scratch));
        gpu.fd = -1, gpu.ctx = 1, gpu.at = (unsigned int address_to)gpu_card_buffer, gpu.size = sizeof(gpu_card_buffer), gpu.imports = 0;
        gpu_before.sequence = 0, gpu_slot = gpu_pending_slot = 0, gpu_bins_forget();
        gpu_fake_card = 1, gpu_tick = 100, gpu_map_count = 2;
        gpu_maps[0].from = (positive)gpu_card_screen, gpu_maps[0].to = (positive)gpu_card_screen + sizeof(gpu_card_screen), gpu_maps[0].va = GPU_VA_MAPS;
        gpu_maps[0].bytes = sizeof(gpu_card_screen), gpu_maps[0].handle = 7, gpu_maps[0].used = gpu_tick, gpu_maps[0].written = 1, gpu_maps[0].dma = 0, gpu_maps[0].handed = 0;
        gpu_maps[1].from = (positive)gpu_lists_picture, gpu_maps[1].to = (positive)gpu_lists_picture + sizeof(gpu_lists_picture), gpu_maps[1].va = GPU_VA_MAPS + (16 << 20);
        gpu_maps[1].bytes = sizeof(gpu_lists_picture), gpu_maps[1].handle = 8, gpu_maps[1].used = gpu_tick, gpu_maps[1].written = 1, gpu_maps[1].dma = 0, gpu_maps[1].handed = 0;
        compositor_gpu = 1, compositor.card = -1, compositor.table = windows_scratch_table(&gpu_card_table_scratch), compositor.width = 2048, compositor.height = 64;
        compositor.scanout = gpu_card_screen, compositor.scanout_pitch = 2048 * 4, compositor.scanout_bytes = sizeof(gpu_card_screen);
        compositor.direct_row = compositor.flip_row = compositor.plane_row = -1, compositor.flip_pending = 0, compositor.held_rows = rows_from(0), compositor.woken = rows_from(0);
        compositor.dropping = rows_from(0), compositor.showing[5] = 1, compositor.x[5] = 100, compositor.y[5] = 10, compositor.w[5] = 64, compositor.h[5] = 4;
        compositor_card_zero();
        compositor_card.nowait = 1, compositor_card.lists_nowait = 1;
        gpu_late_reset();
        compositor_gpu_layout();

        // a. The ops' megabyte in two: a present's records the first half, a list's the second.
        check("lists nowait: a present's records have the first half of the ops' megabyte, a list's the second",
              compositor_gscene.room == (512 << 10) / 32 && COMPOSITOR_GPU_LIST_OPS_AT == COMPOSITOR_GPU_OPS_AT + (512 << 10));

        // b. A list submitted and not waited for, its records in the list's half.
        gpu_fake_idle = 0, gpu_fake_busy = 1, gpu_fake_blocking = 0, submits = gpu_fake_submits;
        present_ops[0] = list_ops[0] = 0xdeadbeefu;
        published = gpu_lists_publish(0xff336699u);
        drawn = gpu_lists_render(5, damage);
        check("lists nowait: a window's list is submitted and not waited for, the frame said drawn with its damage", published && drawn == 1 &&
              gpu_fake_submits == submits + 1 && gpu_fake_blocking == 0 && gpu.pending && compositor_card.list_seq[5] == gpu.pending && damage[0] == 0 &&
                  damage[1] == 0 && damage[2] == 64 && damage[3] == 4);
        check("lists nowait: its records are cut into the list's half, none into the present's", list_ops[0] != 0xdeadbeefu && present_ops[0] == 0xdeadbeefu);

        // d. and e. The row's picture is the job's; a present is held, the clients while it is young, the doors after it wait.
        check("lists nowait: the row's picture is the job's, no other's", compositor_card_reads(5) && !compositor_card_reads(4) && rows_low(&compositor_card.list_rows) == 1u << 5);
        compositor_card.nowait = 0;
        check("lists nowait: a present is held while it runs, the present waiting for its jobs or not", compositor_card_holds() && compositor_card_awaits());
        compositor_card.nowait = 1;
        check("lists nowait: (without a wait too)", compositor_card_holds() && compositor_card_awaits());
        check("lists nowait: the clients are held while the job is younger than GPU_LATE_NS, and not after",
              compositor_card_clients_held(compositor_card.list_began + 1000000ull) && compositor_card_clients_held(compositor_card.list_began + GPU_LATE_NS - 1) &&
                  !compositor_card_clients_held(compositor_card.list_began + GPU_LATE_NS));
        {
                int kept_door = compositor.door[2];
                struct compositor_rows kept_busy = compositor_doors.busy;

                compositor.door[2] = 5, compositor_doors.busy = rows_from(0);
                compositor_loop_wanted(wanted, &slots, 1);
                check("lists nowait: while it holds them no door is polled, and the job's sync_file is", wanted[slots.doors + 2].handle == -1 && slots.fence_at &&
                      wanted[slots.fence_at].handle >= 0 && wanted[slots.fence_at].handle == compositor_card.fence - 1);
                compositor.door[2] = kept_door, compositor_doors.busy = kept_busy;
        }
        check("lists nowait: the doors that knocked with it wait for a later turn; a job already on the card when the doors are read (one past GPU_LATE_NS) holds none",
              compositor_card_door_waits(0) && !compositor_card_door_waits(1));

        // c. Another list while it runs: refused as the card's being busy, nothing submitted, nothing counted.
        failures = compositor_gpu_state.failures, submits = gpu_fake_submits, sequence = compositor_card.list_seq[5];
        published = gpu_lists_publish(0xff112233u);
        drawn = gpu_lists_render(5, damage);
        check("lists nowait: the row's next list while its job runs is refused as the card's being busy, nothing submitted, the job still the row's", published &&
              !drawn && compositor_list_why == COMPOSITOR_LIST_CARD && gpu_fake_submits == submits && compositor_card.list_seq[5] == sequence &&
                  compositor_card_reads(5));
        drawn = gpu_lists_render(6, damage);
        check("lists nowait: and another row's, nothing counted against the card (its failures, a cool-down)", !drawn && compositor_list_why == COMPOSITOR_LIST_CARD &&
              gpu_fake_submits == submits && compositor_gpu_state.failures == failures && !compositor_gpu_state.cool_until && !compositor_card_reads(6));

        // d. The door's taken held for the job; a flip's end leaves it; a turn while the job runs leaves it; the fence's done gives it back.
        windows_scratch_table(&gpu_card_table_scratch)->shown[5] = 0, compositor.held_rows = rows_from(1u << 5), compositor.taken_held[5] = 55, compositor.woken = rows_from(0), compositor.flip_pending = 1;
        compositor_flip_done();
        check("lists nowait: a flip that ends while the job runs leaves the row's door held", windows_scratch_table(&gpu_card_table_scratch)->shown[5] == 0 && (rows_has(&compositor.held_rows, 5)));
        compositor_card_tick();
        check("lists nowait: a turn while the job runs asks the card without a wait and leaves the row the job's", compositor_card_reads(5) && windows_scratch_table(&gpu_card_table_scratch)->shown[5] == 0 &&
              (rows_has(&compositor.held_rows, 5)) && gpu_fake_blocking == 0);
        gpu_fake_busy = 0, gpu_fake_idle = 1;
        compositor_card_tick();
        check("lists nowait: once the fence says done the turn gives the row back: its door told the picture was taken, nothing the job's, the present free",
              windows_scratch_table(&gpu_card_table_scratch)->shown[5] == 55 && !(rows_has(&compositor.held_rows, 5)) && (rows_has(&compositor.woken, 5)) && !rows_any(&compositor_card.list_rows) &&
                  !compositor_card_reads(5) && !compositor_card_holds() && !compositor_card_awaits() && windows_scratch_table(&gpu_card_table_scratch)->list_state[5] == 0 && gpu_fake_blocking == 0);

        // A list job that never signals: its row the job's past GPU_LATE_NS (the clients read again), given back by the hang path at its deadline.
        gpu_late_reset();
        gpu_fake_idle = 0, gpu_fake_busy = 1;
        gpu_lists_publish(0xff445566u);
        drawn = gpu_lists_render(5, damage);
        windows_scratch_table(&gpu_card_table_scratch)->shown[5] = 0, windows_scratch_table(&gpu_card_table_scratch)->list_state[5] = 1, compositor.held_rows = rows_from(1u << 5), compositor.taken_held[5] = 66, compositor.woken = rows_from(0);
        compositor.damages = 0;
        now = clock_monotonic_nanoseconds();
        compositor_card.list_began = now - GPU_LATE_NS - 1, gpu_job_began = now - (GPU_HANG_NS - 500000000ull);
        compositor_card_tick();
        check("lists nowait: a list's job that never signals is the row's past GPU_LATE_NS, the clients read again", drawn == 1 && compositor_card_reads(5) &&
              !compositor_card_clients_held(now) && windows_scratch_table(&gpu_card_table_scratch)->shown[5] == 0);
        gpu_job_began = now - GPU_HANG_NS;
        compositor_card_tick();
        check("lists nowait: at its deadline the hang path gives the row's door back, its list refused so the program paints the frame, its window drawn again",
              gpu_end == GPU_END_HUNG && windows_scratch_table(&gpu_card_table_scratch)->shown[5] == 66 && !(rows_has(&compositor.held_rows, 5)) && (rows_has(&compositor.woken, 5)) &&
                  windows_scratch_table(&gpu_card_table_scratch)->list_state[5] == 2 && !rows_any(&compositor_card.list_rows) && compositor.damages >= 1 && compositor_gpu_state.cool_until > now);
        check("lists nowait: and nothing holds the present, the clients or the loop after it: the program is not stuck", !compositor_card_holds() &&
              !compositor_card_clients_held(now) && !compositor_card_awaits() && !compositor_card_reads(5));

        // A list job whose fence answers an error, and one a restart of the card took: given back the same way, at once.
        gpu_late_reset();
        gpu_fake_idle = 0, gpu_fake_busy = 1, gpu_before.sequence = 0, gpu.pending = 0;
        gpu_lists_publish(0xff778899u);
        drawn = gpu_lists_render(5, damage);
        windows_scratch_table(&gpu_card_table_scratch)->shown[5] = 0, windows_scratch_table(&gpu_card_table_scratch)->list_state[5] = 1, compositor.held_rows = rows_from(1u << 5), compositor.taken_held[5] = 77;
        gpu_fake_busy = 0, gpu_fake_errno = 1;
        compositor_card_tick();
        gpu_fake_errno = 0;
        check("lists nowait: a list's job whose fence answers an error gives its row back, its list refused", drawn == 1 && windows_scratch_table(&gpu_card_table_scratch)->shown[5] == 77 &&
              windows_scratch_table(&gpu_card_table_scratch)->list_state[5] == 2 && !rows_any(&compositor_card.list_rows));
        gpu_late_reset();
        gpu_fake_idle = 0, gpu_fake_busy = 1, gpu_before.sequence = 0, gpu.pending = 0, gpu.ctx = 1;
        gpu_lists_publish(0xff998877u);
        drawn = gpu_lists_render(5, damage);
        windows_scratch_table(&gpu_card_table_scratch)->shown[5] = 0, windows_scratch_table(&gpu_card_table_scratch)->list_state[5] = 1, compositor.held_rows = rows_from(1u << 5), compositor.taken_held[5] = 88;
        gpu_recovered++;
        compositor_card_tick();
        check("lists nowait: and one a restart of the card took, while the card says nothing of it", drawn == 1 && windows_scratch_table(&gpu_card_table_scratch)->shown[5] == 88 &&
              windows_scratch_table(&gpu_card_table_scratch)->list_state[5] == 2 && !rows_any(&compositor_card.list_rows));
        gpu_recovered = kept_recovered;

        // Without the file: drawn and waited for in place, as it was.
        compositor_card.lists_nowait = 0;
        compositor_gpu_layout();
        gpu_late_reset();
        gpu_fake_idle = 1, gpu_fake_busy = 0, gpu_fake_blocking = 0, gpu_before.sequence = 0, gpu.pending = 0;
        present_ops[0] = list_ops[0] = 0xdeadbeefu;
        gpu_lists_publish(0xff102030u);
        drawn = gpu_lists_render(5, damage);
        check("lists nowait: without the file a list is drawn and waited for in place, as it was: its records in the ops' first half, nothing left on the card",
              drawn == 1 && gpu_fake_blocking > 0 && !rows_any(&compositor_card.list_rows) && !gpu.pending && present_ops[0] != 0xdeadbeefu && list_ops[0] == 0xdeadbeefu &&
                  compositor_gscene.room == (1 << 20) / 32 && !compositor_card_door_waits(0));

        gpu_late_reset();
        gpu_fake_card = 0, gpu_fake_idle = 0, gpu_fake_busy = 0, gpu_lost = 0, gpu_handed_clear();
        gpu_before.sequence = 0, gpu_end_said = 0, gpu_found = GPU_END_NONE, gpu_slot = gpu_pending_slot = 0, gpu_bins_forget();
        if (compositor_card.fence)
                system_close(compositor_card.fence - 1);
        memory_copy(&compositor, kept_comp, sizeof(compositor)), memory_copy(&gpu, kept_gpu, sizeof(gpu));
        memory_copy(&compositor_gpu_state, kept_state, sizeof(compositor_gpu_state)), memory_copy(&compositor_gscene, kept_gscene, sizeof(compositor_gscene));
        memory_copy(gpu_maps, kept_maps, sizeof(kept_maps)), memory_copy(&compositor_card, kept_card, sizeof(compositor_card));
        gpu_map_count = kept_count, gpu_tick = kept_tick, gpu_last_error = 0, gpu_end = GPU_END_NONE, gpu_fault[0] = 0, compositor_gpu = kept_gpu_on;
}

/*
        The card told what changed (compositor_dirty): never by a headless compositor (it made one failed call a present, EBADF on descriptor -1), once by a
        card that answers ENOSYS (amdgpu answers every program's DIRTYFB so) and not again while it is that card, and always by one that takes it.
*/
static void dirty_rows(void)
{
        const unsigned short clip[1][4] = {{0, 0, 64, 64}};
        int kept_card = compositor.card;
        char kept_driver[sizeof(compositor.driver)];
        unsigned int asks;

        memory_copy(kept_driver, compositor.driver, sizeof(kept_driver));
        compositor.card = -1, asks = card_fake.dirty_asks;
        compositor_dirty(100, clip, 1);
        check("dirty: a headless compositor asks no card what changed", card_fake.dirty_asks == asks);
        if (card_fake_open(64, 64))
        {
                asks = card_fake.dirty_asks; // the card made afresh counts from nothing
                string_copy((string_address)compositor.driver, (string_address) "virtio_gpu");
        compositor.driver_flags = drm_driver_flags(compositor.driver);
                compositor_dirty(100, clip, 1), compositor_dirty(100, clip, 1);
                check("dirty: a card that takes it is told every time", card_fake.dirty_asks == asks + 2);
                compositor_dirty(100, clip, 0);
                check("dirty: and nothing with nothing changed", card_fake.dirty_asks == asks + 2);
                string_copy((string_address)compositor.driver, (string_address) "amdgpu");
        compositor.driver_flags = drm_driver_flags(compositor.driver);
                card_fake.dirty_errno = 38;
                compositor_dirty(100, clip, 1), compositor_dirty(100, clip, 1), compositor_dirty(100, clip, 1);
                check("dirty: a card that answers ENOSYS is asked once, and not again", card_fake.dirty_asks == asks + 3);
                string_copy((string_address)compositor.driver, (string_address) "virtio_gpu");
        compositor.driver_flags = drm_driver_flags(compositor.driver);
                card_fake.dirty_errno = 0;
                compositor_dirty(100, clip, 1);
                check("dirty: another driver on the same descriptor is asked again", card_fake.dirty_asks == asks + 4);
                card_fake_close();
        }
        else
                check("dirty: the fake card is made", false);
        memory_zero(&compositor_dirty_none, sizeof(compositor_dirty_none)), compositor_dirty_none.card = -1;
        compositor.card = kept_card, memory_copy(compositor.driver, kept_driver, sizeof(kept_driver)); compositor.driver_flags = drm_driver_flags(compositor.driver);
}

/*
        The present's timing as the table says it, a second at a time (compositor_metric_publish, moonwater debug's presents line): the presents made late
        in the last second and those that missed, the lead in use (none headless), the loop's longest present, the flips with jobs in flight, and the
        loop's time inside the card's waits.
*/
static void metric_present_rows(void)
{
        static struct windows *table_scratch;
        struct windows *const table_at = windows_scratch_table(&table_scratch);
        struct windows *kept_table = compositor.table;
        positive kept_made = compositor_late.made, kept_missed = compositor_late.missed, kept_since = compositor_metric_since, now;
        unsigned long long kept_most = compositor_metric_loop_most, kept_flips = compositor_metric_flips_jobs, kept_waited = compositor_metric_waited_then;
        unsigned int kept_jobs_most = compositor_metric_flip_jobs_most;
        int kept_card = compositor.card;

        windows_scratch_zero(table_at);
        compositor.table = table_at, compositor.card = -1;
        compositor_metric_since = 0;
        compositor_metric_publish(); // the mark
        compositor_late.made += 60, compositor_late.missed += 2, compositor_metric_loop_most = 4321000ull;
        compositor_metric_flips_jobs += 30, compositor_metric_flip_jobs_most = 2;
        gpu_job_ns += 2500000ull; // 2.5 ms waited on the card's jobs in the second
        now = clock_monotonic_nanoseconds();
        compositor_metric_since = now - 1000000000ull; // a second ago
        compositor_metric_publish();
        check("metrics: the loop's time inside the card's waits in the second is the table's, in microseconds",
              table_at->metric_waited >= 2490 && table_at->metric_waited <= 2500);
        check("metrics: a second's presents made late and missed, the loop's longest present and the flips with jobs in flight are the table's",
              table_at->metric_late >= 59 && table_at->metric_late <= 60 && table_at->metric_missed >= 1 && table_at->metric_missed <= 2 && table_at->metric_present_most == 4321 &&
                  table_at->metric_flip_jobs >= 29 && table_at->metric_flip_jobs <= 30 && table_at->metric_flip_jobs_most == 2);
        check("metrics: a headless compositor says no lead", table_at->metric_lead == 0);
        {
                unsigned int kept_kinds[3], kept_waited = gpu_jobs_waited, kept_unwaited = gpu_jobs_unwaited;
                unsigned long long kept_kind_ns[3], kept_submit = gpu_job_submit_ns, kept_wait = gpu_job_wait_ns;

                memory_copy(kept_kinds, gpu_jobs_kind, sizeof(kept_kinds)), memory_copy(kept_kind_ns, gpu_job_ns_kind, sizeof(kept_kind_ns));
                gpu_jobs_kind[0] = 11, gpu_jobs_kind[1] = 22, gpu_jobs_kind[2] = 33;
                gpu_job_ns_kind[0] = 5000000000ull, gpu_job_ns_kind[1] = 6000000ull, gpu_job_ns_kind[2] = 7000ull;
                gpu_jobs_waited = 44, gpu_jobs_unwaited = 55, gpu_job_submit_ns = 4400000000ull, gpu_job_wait_ns = 8800ull;
                compositor_metric_publish();
                check("metrics: the card's jobs by kind, each with its time waited, and those waited for against those not with the submit's time, are the table's",
                      table_at->metric_gpu_jobs_scene == 11 && table_at->metric_gpu_job_ns_scene == 5000000000ull && table_at->metric_gpu_jobs_copy == 22 &&
                          table_at->metric_gpu_job_ns_copy == 6000000ull && table_at->metric_gpu_jobs_scale == 33 && table_at->metric_gpu_job_ns_scale == 7000ull &&
                          table_at->metric_jobs_waited == 44 && table_at->metric_jobs_unwaited == 55 && table_at->metric_job_submit_ns == 4400000000ull &&
                          table_at->metric_job_wait_ns == 8800ull);
                memory_copy(gpu_jobs_kind, kept_kinds, sizeof(kept_kinds)), memory_copy(gpu_job_ns_kind, kept_kind_ns, sizeof(kept_kind_ns));
                gpu_jobs_waited = kept_waited, gpu_jobs_unwaited = kept_unwaited, gpu_job_submit_ns = kept_submit, gpu_job_wait_ns = kept_wait;
        }
        check("metrics: and the next second starts from nothing", compositor_metric_loop_most == 0 && compositor_metric_flip_jobs_most == 0 &&
                                                                      compositor_metric_waited_then == gpu_job_ns);
        gpu_job_ns -= 2500000ull, compositor_metric_waited_then = kept_waited;
        compositor.table = kept_table, compositor.card = kept_card;
        compositor_late.made = kept_made, compositor_late.missed = kept_missed, compositor_metric_since = kept_since;
        compositor_metric_loop_most = kept_most, compositor_metric_flips_jobs = kept_flips, compositor_metric_flip_jobs_most = kept_jobs_most;
}

/*
        A table picture's framebuffer refused (compositor_window_fb_refused): its step and the kernel's error said in the table, which raise the board's warning
        of a refused direct scan-out, but on amdgpu the step alone (every amdgpu refuses an imported udmabuf for scanout), and no warning.
*/
static void scanout_refused_rows(void)
{
        static struct windows *table_scratch;
        struct windows *const table_at = windows_scratch_table(&table_scratch);
        struct windows *kept_table = compositor.table;
        struct windows_issue want[WINDOWS_GPU_CODES];
        char kept_driver[sizeof(compositor.driver)];
        int kept_refused = compositor.direct_refused, warned;
        unsigned int count;

        memory_copy(kept_driver, compositor.driver, sizeof(kept_driver));
        windows_scratch_zero(table_at), compositor.table = table_at, table_at->gpu_on = 1; // the board's GPU issues are judged with canvas gpu on
        string_copy((string_address)compositor.driver, (string_address) "i915");
        compositor.driver_flags = drm_driver_flags(compositor.driver);
        compositor_window_fb_refused(4, -22);
        count = windows_gpu_wanted(table_at, want, WINDOWS_GPU_CODES), warned = 0;
        for (unsigned int at = 0; at < count; at++)
                warned |= want[at].code == WINDOWS_ISSUE_GPU_SCANOUT;
        check("scanout: a refused framebuffer of a table picture is its step and its error, and a warning", compositor.direct_refused && table_at->refused == 4 &&
              table_at->refused_error == 22 && warned);
        windows_scratch_zero(table_at), table_at->gpu_on = 1, compositor.direct_refused = 0;
        string_copy((string_address)compositor.driver, (string_address) "amdgpu");
        compositor.driver_flags = drm_driver_flags(compositor.driver);
        compositor_window_fb_refused(4, -22);
        count = windows_gpu_wanted(table_at, want, WINDOWS_GPU_CODES), warned = 0;
        for (unsigned int at = 0; at < count; at++)
                warned |= want[at].code == WINDOWS_ISSUE_GPU_SCANOUT;
        check("scanout: on amdgpu the step is said and no warning is raised (amdgpu shows no imported buffer)", compositor.direct_refused && table_at->refused == 4 &&
              table_at->refused_error == 0 && !warned);
        compositor.table = kept_table, compositor.direct_refused = kept_refused, memory_copy(compositor.driver, kept_driver, sizeof(kept_driver)); compositor.driver_flags = drm_driver_flags(compositor.driver);
}

/*
        One way to the screen for each kind of card (compositor_plate_wanted): the second plate where the card shows what its buffer holds (and headless),
        none where it copies what it is told changed (virtio_gpu, simpledrm: in place, apart, told the rows that changed), none with a screen turned, two
        screens or a window on a plane.
*/
static void plate_wanted_rows(void)
{
        char kept_driver[sizeof(compositor.driver)];
        int kept_card = compositor.card, kept_plane = compositor.plane_row;
        unsigned int kept_outputs = compositor.outputs;

        memory_copy(kept_driver, compositor.driver, sizeof(kept_driver));
        compositor.plane_row = -1, compositor.outputs = 1, compositor.card = -1;
        check("plate: headless, the frame goes into the scan image's other name", compositor_plate_wanted(0));
        compositor.card = 7;
        string_copy((string_address)compositor.driver, (string_address) "amdgpu");
        compositor.driver_flags = drm_driver_flags(compositor.driver);
        check("plate: a card that shows what its buffer holds composes into the second plate", compositor_plate_wanted(0) && !compositor_copies_damage());
        string_copy((string_address)compositor.driver, (string_address) "i915");
        compositor.driver_flags = drm_driver_flags(compositor.driver);
        check("plate: and so does one whose buffer is the machine's memory scanned as it is", compositor_plate_wanted(0));
        string_copy((string_address)compositor.driver, (string_address) "virtio_gpu");
        compositor.driver_flags = drm_driver_flags(compositor.driver);
        check("plate: a card that copies its damage to its host has none: in place, told what changed", !compositor_plate_wanted(0) && compositor_copies_damage());
        string_copy((string_address)compositor.driver, (string_address) "simpledrm");
        compositor.driver_flags = drm_driver_flags(compositor.driver);
        check("plate: nor the firmware's", !compositor_plate_wanted(0));
        string_copy((string_address)compositor.driver, (string_address) "amdgpu");
        compositor.driver_flags = drm_driver_flags(compositor.driver);
        check("plate: a screen turned has none", !compositor_plate_wanted(1));
        compositor.plane_row = 2;
        check("plate: and a window on a plane has the desktop under it composed into the second plate too", compositor_plate_wanted(0));
        compositor.plane_row = -1, compositor.outputs = 2;
        check("plate: nor two screens", !compositor_plate_wanted(0));
        compositor.card = kept_card, compositor.plane_row = kept_plane, compositor.outputs = kept_outputs;
        memory_copy(compositor.driver, kept_driver, sizeof(kept_driver)); compositor.driver_flags = drm_driver_flags(compositor.driver);
}

/*
        What the tabs covered of a window shown straight (compositor_direct_under_ready): mapped the screen's size at the least, and when a space taken again
        asks it to hold a rectangle of more pixels than it has, made again that size with what it held, never written past.
*/
static void under_ready_rows(void)
{
        unsigned int *kept_under = compositor.under[1];
        positive kept_bytes = compositor_under_bytes[1];
        int kept_width = compositor.width, kept_height = compositor.height;
        unsigned int *first;
        b32 ok;

        compositor.under[1] = 0, compositor_under_bytes[1] = 0, compositor.width = 800, compositor.height = 600;
        ok = compositor_direct_under_ready(1, 4ull * 800 * 64);
        first = compositor.under[1];
        check("under: the first is the screen's size at the least", ok && first && compositor_under_bytes[1] >= 4ull * 800 * 600);
        first[0] = 0x11223344u, first[800 * 600 - 1] = 0x55667788u;
        compositor.width = 7680, compositor.height = 4320;
        ok = compositor_direct_under_ready(1, 4ull * 7680 * 64); // 491520 pixels: more than 800 by 600 has
        check("under: a band of more pixels than the screen it was made for has it made again, as large", ok && compositor_under_bytes[1] >= 4ull * 7680 * 64 &&
              compositor.under[1] && compositor.under[1][0] == 0x11223344u && compositor.under[1][800 * 600 - 1] == 0x55667788u);
        if (ok)
                compositor.under[1][7680 * 64 - 1] = 1; // its last pixel is its own
        first = compositor.under[1];
        ok = compositor_direct_under_ready(1, 4ull * 1920 * 64);
        check("under: and one that fits is the same, not made again", ok && compositor.under[1] == first);
        if (compositor.under[1])
                system_call_2(syscall(munmap), (positive)compositor.under[1], compositor_under_bytes[1]);
        compositor.under[1] = kept_under, compositor_under_bytes[1] = kept_bytes, compositor.width = kept_width, compositor.height = kept_height;
}

// A program's copy the compositor takes (compositor_clip_open) is read through on the loop, so one past WINDOWS_CLIP_MOST is declined before it is read;
// one under it is taken. Copies of this process (windows_clip_memfd), of text a page short of the most and of the most (its index and trailer then over).
static void clip_most_rows(void)
{
        unsigned int me = (unsigned int)compositor_call(syscall(getpid), 0, 0, 0, 0, 0, 0);
        long text = compositor_call(syscall(mmap), 0, WINDOWS_CLIP_MOST, 3, 0x22, (positive)-1, 0); // PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS
        long under = -1, over = -1;
        int taken = -1, declined = -1;
        unsigned long length = 0;
        b32 kept;

        if (!(text < 0 && text > -4096))
        {
                memory_fill((void *)text, 'a', WINDOWS_CLIP_MOST);
                under = windows_clip_memfd((const void *)text, WINDOWS_CLIP_MOST - 4096);
                over = windows_clip_memfd((const void *)text, WINDOWS_CLIP_MOST);
                compositor_call(syscall(munmap), (positive)text, WINDOWS_CLIP_MOST, 0, 0, 0, 0);
        }
        taken = under >= 0 ? compositor_clip_open(me, (int)under, &length) : -1;
        kept = taken >= 0 && length > WINDOWS_CLIP_MOST - 4096 && length <= WINDOWS_CLIP_MOST;
        declined = over >= 0 ? compositor_clip_open(me, (int)over, &length) : 0;
        check("clip: a program's copy under WINDOWS_CLIP_MOST is taken, and one past it is declined before it is read", kept && over >= 0 && declined < 0);
        if (taken >= 0)
                system_close(taken);
        if (declined >= 0)
                system_close(declined);
        if (under >= 0)
                system_close((int)under);
        if (over >= 0)
                system_close((int)over);
}

// The input thread reads its own duplicates of the devices (H-c5-1): a device the loop closes, whose number the kernel then gives to another file
// (a client's socket), is still the device to the reader until the loop's change hands it the next one; and a device let go of is closed there.
// Pipes stand for the devices: the loop's descriptor, closed, and a new pipe made at the same number.
static void reader_own_rows(void)
{
        int loop_pipe[2], other[2], saved_on = compositor_reader.on, saved_set = compositor_reader.set, saved_input = compositor.inputs[0];
        unsigned int saved_count = compositor.input_count, count = 0, generations[COMPOSITOR_INPUTS];
        int watched[COMPOSITOR_INPUTS];
        unsigned char byte = 0;
        b32 own, same_file, unwatched;

        for (unsigned int at = 0; at < COMPOSITOR_INPUTS; at++)
                watched[at] = -1, generations[at] = 0;
        compositor_call(syscall(pipe2), (positive)loop_pipe, 0x800 | 0x80000, 0, 0, 0, 0);
        compositor_reader.set = (int)system_call_1(syscall(epoll_create1), 0x80000);
        compositor_reader.on = 1, compositor.input_count = 1, compositor.inputs[0] = loop_pipe[0], compositor_reader.own[0] = 0;
        compositor_reader_changed(0);
        compositor_reader_sync(watched, generations, &count);
        own = watched[0] >= 0 && watched[0] != loop_pipe[0] && compositor_reader.own[0] == 0;
        // The loop lets the device go and a new file takes its number before the reader hears of it.
        system_close(loop_pipe[0]);
        compositor_call(syscall(pipe2), (positive)other, 0x800 | 0x80000, 0, 0, 0, 0);
        compositor_call(syscall(write), (positive)other[1], (positive)"x", 1, 0, 0, 0);
        compositor_call(syscall(write), (positive)loop_pipe[1], (positive)"d", 1, 0, 0, 0);
        same_file = system_read_once(watched[0], &byte, 1) == 1 && byte == 'd';
        compositor.inputs[0] = -1;
        compositor_reader_changed(0);
        compositor_reader_sync(watched, generations, &count);
        unwatched = watched[0] < 0 && compositor_reader.own[0] == 0;
        check("input reader: it reads a duplicate of the device the loop handed it, so the number the loop closed and another file took is not read, "
              "and a device let go of is unwatched and closed",
              own && same_file && unwatched);
        system_close(loop_pipe[1]), system_close(other[0]), system_close(other[1]), system_close(compositor_reader.set);
        compositor_reader.on = saved_on, compositor_reader.set = saved_set, compositor.inputs[0] = saved_input, compositor.input_count = saved_count;
}

// Display off (kms.c compositor_dark): while the screens are out the input thread sends nothing straight to a client (compositor_reader_direct), so
// every key and locked motion reaches the loop, whose take lights them; and a mode set while they are out (compositor_dark_set) puts them out again,
// where the early return on dark == dark left them lit with display-off in force.
static void display_off_rows(void)
{
        struct compositor_input_event key[2] = {{.type = COMPOSITOR_EV_KEY, .code = 30, .value = 1}, {.type = COMPOSITOR_EV_SYN, .code = 0}};
        int saved_dark = compositor.dark, saved_applied = compositor.dark_applied, saved_card = compositor.card;
        unsigned int saved_outputs = compositor.outputs;
        b32 direct, reput, kept;

        compositor.card = -1, compositor.outputs = 0;
        compositor.dark = 1, compositor.dark_applied = 1;
        direct = !compositor_reader_direct(0, key, 2);
        compositor.dark_applied = 1;
        compositor_dark_set();
        reput = compositor.dark == 1 && compositor.dark_applied == 1;
        compositor.dark = 0, compositor.dark_applied = 0;
        compositor_dark_set();
        kept = compositor.dark == 0 && compositor.dark_applied == 0;
        check("display off: a key while the screens are out goes to the loop, not straight to the client; a mode set while they are out puts them out again, "
              "and one while they are lit leaves them lit",
              direct && reput && kept);
        compositor.dark = saved_dark, compositor.dark_applied = saved_applied, compositor.card = saved_card, compositor.outputs = saved_outputs;
}

// The residue of a key or button edge the kernel's filter takes (hid-input's MSC_SCAN and its SYN, read from evdev where the kernel has no
// EVIOCSMASK): consumed by the reader, not handed to the loop, so the gate straight to the client stays open for the motion and keys after it.
static void reader_residue_rows(void)
{
        struct compositor_input_event residue[2] = {{.type = 4u, .code = 4, .value = 0x90004}, {.type = COMPOSITOR_EV_SYN, .code = 0}};
        struct compositor_input_event dropped[2] = {{.type = COMPOSITOR_EV_SYN, .code = COMPOSITOR_SYN_DROPPED}, {.type = COMPOSITOR_EV_SYN, .code = 0}};
        positive kept_taken = compositor_reader.taken, kept_out = compositor_reader.handed_out;
        int kept_dark = compositor.dark;
        b32 consumed, shut, lost;

        compositor_reader.taken = compositor_reader.handed_out = 7, compositor.dark = 0;
        consumed = compositor_reader_direct(0, residue, 2) == 1 && compositor_reader.handed_out == 7 && compositor_reader.taken == 7;
        compositor_reader.taken = 5; // something handed to the loop is not settled yet: the gate is shut, and the residue is still nobody's
        shut = compositor_reader_direct(0, residue, 2) == 1;
        compositor_reader.taken = 7;
        lost = compositor_reader_direct(0, dropped, 2) == 0; // a SYN_DROPPED goes to the loop, which reads the device again
        check("reader: an MSC_SCAN and its SYN alone are consumed, not handed to the loop, the gate open or shut; a SYN_DROPPED still goes to the loop",
              consumed && shut && lost);
        compositor_reader.taken = kept_taken, compositor_reader.handed_out = kept_out, compositor.dark = kept_dark;
}

/*
        The processes game mode takes for a fullscreen row (compositor_row_pids). An X window's table pid is the X client's (XRes), rewritten from the
        claim, which is the X server's (Xwayland, the Wayland client): the claim refused it, so no X game (HL2, every Proton game) had the game's slice,
        nor Xwayland. The client is taken where the X window manager heard it from the very server that claimed the row, and the server with it.
*/
static struct windows *game_pids_table;
static void game_pids_rows(void)
{
        __typeof__(compositor_xwm_slots[0]) *kept_at = compositor_xwm_at, *x = &compositor_xwm_slots[COMPOSITOR_XWM_SLOTS - 1];
        __typeof__(compositor_xwm_slots[0]) saved = *x;
        struct windows *kept_table = compositor.table;
        unsigned int self = (unsigned int)compositor_call(syscall(getpid), 0, 0, 0, 0, 0, 0), parent, got[2], n;
        int ends[2] = {-1, -1};
        bipolar child;
        const int wl_row = 4, x_row = 5;
        struct windows_owner kept_wl = windows_owner_held[wl_row], kept_x = windows_owner_held[x_row];
        char value[64];
        b32 wayland_ok, x_ok, x_alone, forged, other_server, environ_ok;

        // The game stands as a child of this process, held until the rows are done (the runner's parent may be outside its pid namespace: 0).
        compositor_call(syscall(pipe2), (positive)ends, 0x80000, 0, 0, 0, 0);
        child = (bipolar)system_fork();
        if (child == 0)
        {
                char byte;

                system_close(ends[1]), system_read_once(ends[0], &byte, 1);
                system_call_3(syscall(exit_group), 0, 0, 0);
        }
        parent = child > 0 ? (unsigned int)child : 0;
        compositor.table = windows_scratch_table(&game_pids_table);
        windows_scratch_zero(compositor.table);
        // A Wayland game: its claim.
        windows_owner_take(&windows_owner_held[wl_row], parent), compositor.table->pid[wl_row] = parent;
        n = compositor_row_pids(wl_row, 0, 1, got);
        wayland_ok = n == 1 && got[0] == parent;
        // An X game: the claim is the X server's (this process stands for Xwayland), the table's pid the client's (the child for the game).
        compositor_xwm_at = x, x->fd = 0, x->peer_pid = (int)self;
        {
                __typeof__(compositor_xwm_slots[0].window) w = compositor_xwm_find(0x400123u, 1);

                if (w)
                        xwm_row_bind(x, w, x_row), w->owner = parent;
                windows_owner_take(&windows_owner_held[x_row], self), compositor.table->pid[x_row] = parent;
                n = compositor_row_pids(x_row, 0, 1, got);
                x_ok = w && n == 2 && got[0] == parent && got[1] == self;
                n = compositor_row_pids(x_row, 0, 0, got);
                x_alone = n == 1 && got[0] == parent;
                compositor.table->pid[x_row] = 0x7ffffff0u; // a pid the X window manager never heard: refused
                forged = compositor_row_pids(x_row, 0, 1, got) == 0;
                compositor.table->pid[x_row] = parent, x->peer_pid = (int)parent; // the window's server is not the row's claim: refused
                other_server = compositor_row_pids(x_row, 0, 1, got) == 0;
                if (w)
                        compositor_xwm_release(w);
        }
        check("game: a Wayland game is its claim; an X game is its X client and its X server (Xwayland), the client alone for the background",
              wayland_ok && x_ok && x_alone);
        check("game: an X row's pid the X window manager did not name, or a window of another server than the row's claim, is refused", forged && other_server);
        // The wineserver is found by the game's SteamGameId (compositor_environ_word): a key the environment has, and one it has not.
        environ_ok = compositor_environ_word(self, "PATH=", value, sizeof value) > 0 && value[0] == '/' &&
                     compositor_environ_word(self, "MOONWATER_NO_SUCH_KEY=", value, sizeof value) == 0;
        check("game: a process's environment is read for a key's value, and a key it has not is none", environ_ok);
        x->fd = -1;
        wayland_table_drop(&x->window_table), wayland_table_drop(&x->window_index), x->windows = 0, x->window = 0;
        *x = saved, compositor_xwm_at = kept_at, compositor.table = kept_table;
        windows_owner_held[wl_row] = kept_wl, windows_owner_held[x_row] = kept_x;
        system_close(ends[0]), system_close(ends[1]);
        if (child > 0)
                (void)system_wait4_retry(child, 0, 0, null);
}

/*
        The wakeup latency game mode and an input hold (compositor_qos_keep): 20 us on each CPU of the compositor's CCD, its own resume-latency file,
        not /dev/cpu_dma_latency at 0 for the whole machine (every idle CPU spinning in POLL, 75 W against 52-63 W). The files are a tree of the
        check's own (compositor_qos_root): CPUs 0 and 1 are the CCD, 2 is the other one's; CPU 1 holds a 20 a compositor that died left.
*/
static unsigned int qos_value(const char *dir, unsigned int cpu)
{
        char path[128], text[16];
        unsigned int at = 0, value = 0;
        bipolar handle, got;

        windows_buf_str(path, sizeof path, &at, dir), windows_buf_kv(path, sizeof path, &at, "/cpu", (long)cpu);
        windows_buf_str(path, sizeof path, &at, "/power/pm_qos_resume_latency_us");
        if ((handle = system_open_at(AT_FDCWD, (string_address)path, FILE_READ | O_CLOEXEC)) < 0)
                return ~0u;
        got = system_read_once(handle, text, sizeof text);
        system_close(handle);
        for (bipolar k = 0; k < got && text[k] >= '0' && text[k] <= '9'; k++)
                value = value * 10 + (unsigned int)(text[k] - '0');
        return value;
}

static void qos_rows(void)
{
        static const char dir[] = "/run/moonwater/check-qos";
        __typeof__(compositor_ccd) kept_ccd = compositor_ccd;
        __typeof__(compositor_qos) kept_qos = compositor_qos;
        const char *kept_root = compositor_qos_root;
        int kept_hold = compositor.hold, kept_game = compositor.game;
        unsigned long kept_holds = compositor.holds;
        positive kept_until = compositor.hold_until, now = 5000000000ull;
        b32 made = true, input, dropped, game, kept, left;

        compositor_call(syscall(mkdirat), (positive)AT_FDCWD, (positive)dir, 0700, 0, 0, 0);
        for (unsigned int cpu = 0; cpu < 3; cpu++)
        {
                char path[128];
                unsigned int at = 0;
                bipolar handle;

                windows_buf_str(path, sizeof path, &at, dir), windows_buf_kv(path, sizeof path, &at, "/cpu", (long)cpu);
                compositor_call(syscall(mkdirat), (positive)AT_FDCWD, (positive)path, 0700, 0, 0, 0);
                windows_buf_str(path, sizeof path, &at, "/power");
                compositor_call(syscall(mkdirat), (positive)AT_FDCWD, (positive)path, 0700, 0, 0, 0);
                windows_buf_str(path, sizeof path, &at, "/pm_qos_resume_latency_us");
                handle = system_open_at_mode(AT_FDCWD, (string_address)path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
                made &= handle >= 0 && system_write_once(handle, cpu == 1 ? "20\n" : "0\n", cpu == 1 ? 3 : 2) > 0;
                if (handle >= 0)
                        system_close(handle);
        }
        compositor_ccd.ready = 1, compositor_ccd.multi = 1, compositor_ccd.ccd0[0] = 3, compositor_ccd.ccd0[1] = 0;
        memory_zero(&compositor_qos, sizeof compositor_qos), compositor_qos.global = -1, compositor_qos_root = dir;
        compositor.hold = -1, compositor.holds = 0, compositor.game = -1;
        compositor_hold_touch(now);
        input = compositor.hold >= 0 && qos_value(dir, 0) == 20 && qos_value(dir, 1) == 20 && qos_value(dir, 2) == 0 && compositor_qos.global < 0;
        compositor_hold_drop(now + UI_HOLD_NS);
        dropped = compositor.hold < 0 && qos_value(dir, 0) == 0 && qos_value(dir, 1) == 0 && qos_value(dir, 2) == 0;
        check("qos: an input holds 20 us on the compositor's CCD alone, and its end puts each CPU back (a dead compositor's 20 as no limit)",
              made && input && dropped);
        game = compositor_qos_keep(0, 1) && qos_value(dir, 0) == 20 && qos_value(dir, 1) == 20 && qos_value(dir, 2) == 0;
        compositor.game = 1;
        compositor_hold_touch(now), compositor_hold_drop(now + UI_HOLD_NS);
        kept = qos_value(dir, 0) == 20 && qos_value(dir, 1) == 20;
        compositor.game = -1, compositor_qos_keep(0, 0);
        left = qos_value(dir, 0) == 0 && qos_value(dir, 1) == 0 && qos_value(dir, 2) == 0;
        check("qos: a game holds the same, an input's end while it is up keeps it, and the game's end lets it go", game && kept && left);
        for (int at = 0; at < compositor_qos.count; at++)
                system_close(compositor_qos.fd[at]);
        compositor_ccd = kept_ccd, compositor_qos = kept_qos, compositor_qos_root = kept_root;
        compositor.hold = kept_hold, compositor.game = kept_game, compositor.holds = kept_holds, compositor.hold_until = kept_until;
}

/*
        The input core (compositor_input_core_take): CCD0's first core after cpu0 and its SMT sibling take the USB interrupts and the reader and are
        held at 2 us; a game's threads go to the rest of CCD0 (compositor_game_mask); and the hold of a game or an input (compositor_qos_keep, 20 us)
        leaves their stricter 2 us as it is, where it wrote its 20 over it and put the 2 back only at its end.
*/
static void qos_file(const char *dir, unsigned int cpu, const char *leaf, const char *text)
{
        char path[128];
        unsigned int at = 0;
        bipolar handle;

        windows_buf_str(path, sizeof path, &at, dir), windows_buf_kv(path, sizeof path, &at, "/cpu", (long)cpu);
        compositor_call(syscall(mkdirat), (positive)AT_FDCWD, (positive)path, 0700, 0, 0, 0);
        windows_buf_str(path, sizeof path, &at, leaf);
        compositor_call(syscall(mkdirat), (positive)AT_FDCWD, (positive)path, 0700, 0, 0, 0);
        windows_buf_str(path, sizeof path, &at, leaf[1] == 'p' ? "/pm_qos_resume_latency_us" : "/thread_siblings_list");
        handle = system_open_at_mode(AT_FDCWD, (string_address)path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        if (handle >= 0)
                system_write_once(handle, text, (positive)string_length((string_address)text)), system_close(handle);
}

static void input_core_rows(void)
{
        static const char dir[] = "/run/moonwater/check-core";
        __typeof__(compositor_ccd) kept_ccd = compositor_ccd;
        __typeof__(compositor_qos) kept_qos = compositor_qos;
        __typeof__(compositor_input_core) kept_core = compositor_input_core;
        const char *kept_root = compositor_qos_root;
        unsigned long mask[2] = {0, 0};
        b32 took, game, held, left;

        compositor_call(syscall(mkdirat), (positive)AT_FDCWD, (positive)dir, 0700, 0, 0, 0);
        qos_file(dir, 0, "/power", "0\n"), qos_file(dir, 1, "/power", "0\n"), qos_file(dir, 17, "/power", "0\n"), qos_file(dir, 1, "/topology", "1,17\n");
        compositor_ccd.ready = 1, compositor_ccd.multi = 1, compositor_ccd.ccd0[0] = 1ul | 1ul << 1 | 1ul << 17, compositor_ccd.ccd0[1] = 0;
        compositor_input_core.cpu = compositor_input_core.sibling = -1, compositor_input_core.mask[0] = compositor_input_core.mask[1] = 0;
        memory_zero(&compositor_qos, sizeof compositor_qos), compositor_qos.global = -1, compositor_qos_root = dir;
        compositor_input_core_take();
        took = compositor_input_core.cpu == 1 && compositor_input_core.sibling == 17 && qos_value(dir, 1) == 2 && qos_value(dir, 17) == 2 && qos_value(dir, 0) == 0;
        game = compositor_game_mask(mask) && mask[0] == 1ul && mask[1] == 0;
        compositor_qos_keep(0, 1);
        held = qos_value(dir, 0) == 20 && qos_value(dir, 1) == 2 && qos_value(dir, 17) == 2;
        compositor_qos_keep(0, 0);
        left = qos_value(dir, 0) == 0 && qos_value(dir, 1) == 2 && qos_value(dir, 17) == 2;
        check("qos: the input core is CCD0's first core after cpu0 and its sibling, held at 2 us, and a game's threads go to the rest of CCD0",
              took && game);
        check("qos: a game's 20 us hold leaves the input core's 2 us as it is, and its end too", held && left);
        for (int at = 0; at < compositor_qos.count; at++)
                system_close(compositor_qos.fd[at]);
        compositor_ccd = kept_ccd, compositor_qos = kept_qos, compositor_input_core = kept_core, compositor_qos_root = kept_root;
}

/*
        A fullscreen game at a lower resolution through Xwayland: a 1280x720 buffer its viewport shows at 1920x1080 needed a transform
        (compositor_wl_needs_xform), so every frame was copied and scaled into the table (a waited card job or a processor loop) and the copy
        scanned out. It is taken as a picture of the buffer's size (compositor_wl_view_take), which the plane scales, and its client is still
        spoken to at 1920x1080 (compositor_wl.view, compositor_window_point).
*/
static void view_rows(void)
{
        const int row = 6;
        struct wayland_picture p = {0}, scaled;
        int kept_full = compositor.fullscreen[row], kept_x = compositor.x[row], kept_y = compositor.y[row];
        unsigned int kept_w = compositor.w[row], kept_h = compositor.h[row], kept_pw = compositor.picture_w[row], kept_ph = compositor.picture_h[row];
        int wx = 0, wy = 0, sx = 0, sy = 0;
        b32 taken, spoken, back, shape, windowed;

        p.width = 1280, p.height = 720, p.dst_width = 1920, p.dst_height = 1080, p.row_width = 1920, p.row_height = 1080, p.sync_fd = 3;
        p.opaque_x = 0, p.opaque_y = 0, p.opaque_w = 1920, p.opaque_h = 1080;
        compositor.fullscreen[row] = 1, compositor.x[row] = 0, compositor.y[row] = 0, compositor.w[row] = 1920, compositor.h[row] = 1080;
        taken = compositor_wl_view_take(row, &p, &scaled) && scaled.row_width == 1280 && scaled.row_height == 720 && !scaled.dst_width &&
                scaled.full && !compositor_wl_needs_xform(&scaled, scaled.row_width, scaled.row_height) && scaled.opaque_w == 1280 && scaled.opaque_h == 720;
        compositor.picture_w[row] = 1280, compositor.picture_h[row] = 720;
        compositor_window_point(row, 960, 540, &wx, &wy);
        spoken = wx == 960 && wy == 540;
        back = compositor_screen_point(row, 1919, 1079, &sx, &sy) && sx == 1919 && sy == 1079;
        p.width = 1280, p.height = 1024; // 5:4 shown at 16:9: a stretch, which the plane's fit does not do
        shape = !compositor_wl_view_take(row, &p, &scaled) && !compositor_wl.view[row].view_w;
        compositor_window_point(row, 960, 540, &wx, &wy);
        p.height = 720, compositor.fullscreen[row] = 0;
        windowed = !compositor_wl_view_take(row, &p, &scaled);
        compositor.fullscreen[row] = 1, p.modifier = 0x0200000000000001ull; // an AMD tiled modifier: taken only where the card scales it by its tiling
        windowed &= compositor_wl_tiled(&p) && compositor_wl_view_take(row, &p, &scaled) == compositor_wl_tiled_scales();
        p.modifier = 0;
        check("view: a fullscreen 1280x720 buffer shown at 1920x1080 is taken at its own size, with no transform and its opaque part in its units, and its client is spoken to at 1920x1080",
              taken && spoken && back);
        check("view: one that changes the shape or a window not fullscreen is not, a tiled buffer only where the card scales tiling, and its client's points are the picture's again",
              shape && windowed && wx == 640 && wy == 360);
        compositor.fullscreen[row] = kept_full, compositor.x[row] = kept_x, compositor.y[row] = kept_y, compositor.w[row] = kept_w, compositor.h[row] = kept_h;
        compositor.picture_w[row] = kept_pw, compositor.picture_h[row] = kept_ph, compositor_wl.view[row].view_w = compositor_wl.view[row].view_h = 0;
}

// The X window manager's atoms by their numbers (compositor_xwm_atom_index, H-c3-1): every atom of a list of thousands found by the index as the walk
// found it, the first record of an atom two records have, none for one not there, and an interned name's atom found once its reply gives it.
static void xwm_atom_rows(void)
{
        __typeof__(compositor_xwm_slots[0]) *kept = compositor_xwm_at, *x = &compositor_xwm_slots[COMPOSITOR_XWM_SLOTS - 1]; // (compositor_xwm is a macro here)
        __typeof__(compositor_xwm_slots[0]) saved = *x;
        b32 found = true;
        long named, dup;
        int over;

        *x = compositor_xwm_fresh;
        compositor_xwm_at = x;
        for (unsigned int k = 1; k <= 2047; k++)
                compositor_xwm_atom_add(k * 7919u, 0, 0, 0);
        for (unsigned int k = 1; k <= 2047; k++)
                found = found && compositor_xwm_atom_index(k * 7919u) == (long)k - 1;
        named = compositor_xwm_atom_add(0, (const unsigned char *)"UTF8_STRING", 11, 0);
        over = compositor_xwm_atom_add(5000u * 7919u, 0, 0, 0) < 0 && compositor_xwm_atom_index(5000u * 7919u) < 0;
        found = found && compositor_xwm_atom_index(3u) < 0 && compositor_xwm_atom_index(0) < 0;
        compositor_xwm_interned((const unsigned char[32]){1, 0, 0, 0, 0, 0, 0, 0, 7919u * 3 & 0xff, 7919u * 3 >> 8 & 0xff, 7919u * 3 >> 16 & 0xff, 0},
                                32, &(struct compositor_xwm_pending){.slot = (unsigned int)named});
        dup = compositor_xwm_atom_index(7919u * 3);
        // A window not yet placed (row -1) is no row's: a drag's end, which has no row, is not routed to its server by it (B-h9-1).
        {
                __typeof__(compositor_xwm_slots[0].window) w = compositor_xwm_find(77u, 1);
                int kept_fd = x->fd;
                b32 unplaced, placed;

                x->fd = 0; // live for the lookup; no byte is sent
                unplaced = w && w->row == -1 && compositor_xwm_slot_of_row(-1) == 0;
                if (w)
                        w->row = 5;
                placed = compositor_xwm_slot_of_row(5) == x;
                check("xwm: a window not yet placed is no row's, so a drag's end is not sent to its server for it; a placed one is its server's", unplaced && placed);
                if (w)
                        compositor_xwm_release(w);
                x->fd = kept_fd;
                wayland_table_drop(&x->window_table), wayland_table_drop(&x->window_index), x->windows = 0, x->window = 0;
        }
        check("xwm atoms: 2047 atoms each found by the index at their record, none for one not there, the first record of an atom two have, an interned "
              "name's found by its reply's atom, and an atom past the cap of 2048 refused",
              found && named == 2047 && dup == 2 && over && x->atoms_indexed == 2048);
        wayland_table_drop(&x->atom_table), wayland_table_drop(&x->atom_index), wayland_table_drop(&x->name_table);
        *x = saved;
        compositor_xwm_at = kept;
}

// An X copy's INCR is ended where it would pass the clipboard's largest copy, so a client that never ends its INCR cannot grow the memfd (x11 B6).
static void xwm_incr_cap_rows(void)
{
        struct compositor_xwm_in in;
        unsigned char bytes[8] = {0};
        int fd = (int)compositor_call(syscall(memfd_create), (positive)"moonwater-check", 1, 0, 0, 0, 0);
        int over, under;

        memory_zero(&in, sizeof in);
        in.fd = fd, in.done = WINDOWS_CLIP_MOST - 3;
        over = compositor_xwm_in_write(&in, bytes, 4) == 0;
        under = compositor_xwm_in_write(&in, bytes, 3) == 1;
        check("xwm copy: an INCR is ended where it would pass the clipboard's largest copy (4 bytes past it refused, 3 taken)", over && under);
        if (fd >= 0)
                system_close(fd);
}

// A tray dock names one of the window manager's own windows (x11 S3): no tray slot is taken for it.
static void xwm_tray_rows(void)
{
        __typeof__(compositor_xwm_slots[0]) *kept = compositor_xwm_at, *x = &compositor_xwm_slots[COMPOSITOR_XWM_SLOTS - 1];
        __typeof__(compositor_xwm_slots[0]) saved = *x;
        int before = 0, after = 0;

        *x = compositor_xwm_fresh;
        compositor_xwm_at = x;
        x->claimed = 1;
        for (int i = 0; i < TASKBAR_TRAY_ITEM_MAX; i++)
                before += compositor.tray[i].used != 0;
        compositor_xwm_tray_dock(x, compositor_xwm_id(x, COMPOSITOR_XWM_SEL_WINDOW));
        compositor_xwm_tray_dock(x, compositor_xwm_id(x, COMPOSITOR_XWM_PROXY_WINDOW));
        for (int i = 0; i < TASKBAR_TRAY_ITEM_MAX; i++)
                after += compositor.tray[i].used != 0;
        check("xwm tray: a dock naming one of the window manager's own windows (selection, proxy) takes no tray slot", before == after);
        compositor_xwm_tray_undock(x, 0);
        wayland_table_drop(&x->window_table), wayland_table_drop(&x->window_index), x->windows = 0, x->window = 0;
        *x = saved;
        compositor_xwm_at = kept;
}

// WM_NORMAL_HINTS (ICCCM): the minimum is the PMinSize pair at 20 and 24, and only with PMinSize set. A PBaseSize alone (256) sets no
// minimum: offset 36 is width_inc, which the reader took for the minimum (x11 B1).
static void xwm_hints_rows(void)
{
        __typeof__(compositor_xwm_slots[0]) *kept = compositor_xwm_at, *x = &compositor_xwm_slots[COMPOSITOR_XWM_SLOTS - 1];
        __typeof__(compositor_xwm_slots[0]) saved = *x;
        unsigned char r[32 + 72];
        struct compositor_xwm_pending p = {0};
        int base_only, pmin;

        *x = compositor_xwm_fresh;
        compositor_xwm_at = x;
        memset(r, 0, sizeof r);
        r[0] = 1, r[1] = 32, r[2] = 9, r[4] = 18, r[8] = 40, r[16] = 18; // a reply (its first byte 1, which xwm_reply_head asks)
        r[32] = 0x00, r[33] = 0x01;            /* flags: PBaseSize (256) only */
        r[32 + 36] = 7, r[32 + 40] = 9;        /* width_inc 7, height_inc 9: no minimum */
        p.what = COMPOSITOR_XWM_PROP_NORMAL_HINTS, p.window = 91u;
        compositor_xwm_find(91u, 1);
        compositor_xwm_property(r, sizeof r, &p);
        base_only = compositor_xwm_find(91u, 0) && compositor_xwm_find(91u, 0)->min_w == 0 && compositor_xwm_find(91u, 0)->min_h == 0;
        r[32] = 0x10, r[33] = 0x01;            /* flags: PMinSize (16) and PBaseSize (256) */
        r[32 + 20] = 44, r[32 + 21] = 1;       /* min_width 300 */
        r[32 + 24] = 200, r[32 + 25] = 0;      /* min_height 200 */
        compositor_xwm_property(r, sizeof r, &p);
        pmin = compositor_xwm_find(91u, 0) && compositor_xwm_find(91u, 0)->min_w == 300 && compositor_xwm_find(91u, 0)->min_h == 200;
        check("xwm hints: a PBaseSize alone sets no minimum (width_inc at 36 is none), and PMinSize at 20 and 24 does", base_only && pmin);
        if (compositor_xwm_find(91u, 0))
                compositor_xwm_release(compositor_xwm_find(91u, 0));
        wayland_table_drop(&x->window_table), wayland_table_drop(&x->window_index), x->windows = 0, x->window = 0;
        *x = saved;
        compositor_xwm_at = kept;
}

// An X source's XdndPosition begins a drag on the Wayland side only while the person's hand is down (x11 finding S6): the gate reads the
// button and the row it was pressed on, and a row of this server holds it. No button down: no drag; a button down on a row of the server: a drag.
static void xwm_drag_gate_rows(void)
{
        __typeof__(compositor_xwm_slots[0]) *kept = compositor_xwm_at, *x = &compositor_xwm_slots[COMPOSITOR_XWM_SLOTS - 1];
        __typeof__(compositor_xwm_slots[0]) saved = *x;
        __typeof__(compositor.button_down) down0 = compositor.button_down;
        __typeof__(compositor.button_row[0]) row0 = compositor.button_row[0];
        __typeof__(compositor_xwm_slots[0].window) w;
        int hx = 0, hy = 0, none, held;

        *x = compositor_xwm_fresh;
        compositor_xwm_at = x;
        x->fd = 0;
        w = compositor_xwm_find(78u, 1);
        if (w)
                w->row = 5;
        compositor.button_down = 0;
        none = compositor_xwm_drag_hand(&hx, &hy);
        compositor.button_down = 1u, compositor.button_row[0] = 5;
        held = compositor_xwm_drag_hand(&hx, &hy);
        check("xwm drag gate: an X source's drag begins on the Wayland side only with a button down on a row of its server (no button: none)",
              !none && held);
        compositor.button_down = down0, compositor.button_row[0] = row0;
        if (w)
                compositor_xwm_release(w);
        wayland_table_drop(&x->window_table), wayland_table_drop(&x->window_index), x->windows = 0, x->window = 0;
        *x = saved;
        compositor_xwm_at = kept;
}

// A Wayland client is asked for (its first window takes the keys) by the record root made for it or a forebear (windows_asked_record,
// compositor_wl_asked, R-seams-13), never by its own environment: a child of this process is asked once this process records its ask, a record
// of another start is no ask and is taken away, and a directory others may write is not read. Over a directory of the check's own.
/*
        The asked record (windows_asked_record, compositor_wl_asked), made and read by a child of the check's: the gate runs the check as its pid
        namespace's pid 1, where the forebears' walk ends by design (a walk from init has no client), so the recorder is the check's child and the
        client its grandchild. The child says what it saw in one byte on a pipe, a bit a row, and the check holds the bits.
*/
static unsigned int asked_record_probe(void)
{
        static char dir[48] = "/tmp/mw-asked-check-";
        unsigned int me = (unsigned int)compositor_call(syscall(getpid), 0, 0, 0, 0, 0, 0), parent = 0, at = 20, bits = 0;
        unsigned long start = windows_proc_stat(me, &parent);
        int pipe_ends[2] = {-1, -1};
        long child;
        char path[64];

        bits |= (start > 0 && parent == (unsigned int)compositor_call(syscall(getppid), 0, 0, 0, 0, 0, 0)) << 0;
        for (unsigned int copy = me, digits = 0, index; ; )
        {
                for (unsigned int c = copy; c; c /= 10)
                        digits++;
                for (index = digits; index--; copy /= 10)
                        dir[at + index] = (char)('0' + copy % 10);
                dir[at + digits] = 0;
                break;
        }
        compositor_call(syscall(mkdirat), (positive)AT_FDCWD, (positive)dir, 0700, 0, 0, 0);
        windows_asked_dir = dir;
        compositor_wl.started = 1;
        (void)compositor_wl_slots_reach(1);
        compositor_call(syscall(pipe2), (positive)pipe_ends, 0x80000, 0, 0, 0, 0);
        child = (long)system_fork();
        if (child == 0)
        {
                char byte;

                system_close(pipe_ends[1]);
                system_read_once(pipe_ends[0], &byte, 1); // until the check is done with it
                system_call_1(syscall(exit_group), 0);
        }
        compositor_wl.pid[0] = (int)child;
        bits |= (unsigned int)!compositor_wl_asked(0) << 1; // none
        windows_asked_record();
        compositor_wl.pid[0] = (int)me;
        bits |= (unsigned int)!!compositor_wl_asked(0) << 2; // self
        compositor_wl.pid[0] = (int)child;
        bits |= (unsigned int)!!compositor_wl_asked(0) << 3; // descendant
        windows_asked_path(path, me);
        {
                long handle = (long)system_open_at(AT_FDCWD, (string_address)path, FILE_WRITE | O_TRUNC | O_CLOEXEC);

                if (handle >= 0)
                        compositor_call(syscall(write), (positive)handle, (positive) "7\n", 2, 0, 0, 0), system_close((int)handle);
        }
        bits |= (unsigned int)!compositor_wl_asked(0) << 4; // stale
        bits |= (unsigned int)(system_open_at(AT_FDCWD, (string_address)path, FILE_READ | O_CLOEXEC) < 0) << 5; // gone
        windows_asked_record();
        compositor_call(syscall(fchmodat), (positive)AT_FDCWD, (positive)dir, 0755, 0, 0, 0);
        bits |= (unsigned int)!compositor_wl_asked(0) << 6; // a directory others may read
        system_close(pipe_ends[1]), system_close(pipe_ends[0]);
        if (child > 0)
                system_wait4_retry((bipolar)child, null, 0, null);
        compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)path, 0, 0, 0, 0);
        compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)dir, 0x200, 0, 0, 0); // AT_REMOVEDIR
        return bits;
}

static void asked_record_rows(void)
{
        int ends[2] = {-1, -1};
        unsigned char bits = 0;
        long prober;

        compositor_call(syscall(pipe2), (positive)ends, 0x80000, 0, 0, 0, 0);
        prober = (long)system_fork();
        if (prober == 0)
        {
                unsigned char said = (unsigned char)asked_record_probe();

                system_close(ends[0]);
                system_write_once(ends[1], &said, 1);
                system_call_1(syscall(exit_group), 0);
        }
        system_close(ends[1]);
        if (prober > 0 && system_read_once(ends[0], &bits, 1) != 1)
                bits = 0;
        system_close(ends[0]);
        if (prober > 0)
                system_wait4_retry((bipolar)prober, null, 0, null);
        check("asked: a client is asked by its forebear's record and by nothing of its own; a record of another start is no ask and is taken away; "
              "a directory others may write is not read",
              bits == 0x7f);
}

// The X window manager's socket (xwm.c, S-c3-2): one connection for each wrapper (a second from the same pid takes the first one's place), and a
// connection whose wrapper has no child on the Wayland socket by COMPOSITOR_XWM_BIND_NS is closed: a program of uid 1000 that only holds the
// socket holds no slot. Over an abstract socket of the check's own, with this process as the wrapper.
static void xwm_socket_rows(void)
{
        static const unsigned char address[] = {1, 0, 0, 'm', 'w', '-', 'x', 'w', 'm', '-', 'c', 'h', 'e', 'c', 'k'};
        const positive length = sizeof address;
        int saved_listen = compositor_xwm_listen_fd, saved_uid = compositor_xwm_uid, live = 0, slot = -1;
        long me = compositor_call(syscall(getpid), 0, 0, 0, 0, 0, 0), first, second, listener;
        struct compositor_poll wanted[COMPOSITOR_XWM_POLLS];
        b32 made, replaced, dropped;

        memory_zero(wanted, sizeof wanted);
        listener = compositor_call(syscall(socket), 1, 1 | 0x800 | 0x80000, 0, 0, 0, 0);
        made = listener >= 0 && compositor_call(syscall(bind), (positive)listener, (positive)address, length, 0, 0, 0) >= 0 &&
               compositor_call(syscall(listen), (positive)listener, 4, 0, 0, 0, 0) >= 0;
        compositor_xwm_listen_fd = (int)listener, compositor_xwm_uid = (int)compositor_call(syscall(getuid), 0, 0, 0, 0, 0, 0);
        first = compositor_call(syscall(socket), 1, 1 | 0x80000, 0, 0, 0, 0);
        made = made && first >= 0 && compositor_call(syscall(connect), (positive)first, (positive)address, length, 0, 0, 0) >= 0;
        compositor_xwm_accept();
        second = compositor_call(syscall(socket), 1, 1 | 0x80000, 0, 0, 0, 0);
        made = made && second >= 0 && compositor_call(syscall(connect), (positive)second, (positive)address, length, 0, 0, 0) >= 0;
        compositor_xwm_accept();
        for (int at = 0; at < COMPOSITOR_XWM_SLOTS; at++)
                if (compositor_xwm_slots[at].fd >= 0 && compositor_xwm_slots[at].peer_pid == (int)me)
                        live++, slot = at;
        // The first connection's end was closed by the compositor: a read of it ends (0), where the second's has the setup request waiting to be flushed.
        replaced = live == 1 && compositor_call(syscall(recvfrom), (positive)first, (positive)wanted, 1, COMPOSITOR_XWM_MSG_DONTWAIT, 0, 0) == 0;
        dropped = false;
        if (slot >= 0)
        {
                compositor_xwm_slots[slot].since = clock_monotonic_nanoseconds() - COMPOSITOR_XWM_SETUP_NS / 2;
                compositor_xwm_slots[slot].ready = 1, compositor_xwm_slots[slot].poll_at = 0;
                compositor_xwm_at = &compositor_xwm_slots[slot];
                compositor_xwm_pass_one(wanted);
                dropped = compositor_xwm_slots[slot].fd >= 0; // within the time: kept
                compositor_xwm_slots[slot].since = clock_monotonic_nanoseconds() - COMPOSITOR_XWM_BIND_NS - 1;
                compositor_xwm_at = &compositor_xwm_slots[slot];
                compositor_xwm_pass_one(wanted);
                dropped = dropped && compositor_xwm_slots[slot].fd < 0;
        }
        check("xwm: a wrapper's second connection takes its first one's place, and a connection whose wrapper has no Wayland client by the bind time is closed",
              made && replaced && dropped);
        for (int at = 0; at < COMPOSITOR_XWM_SLOTS; at++)
                if (compositor_xwm_slots[at].fd >= 0 && compositor_xwm_slots[at].peer_pid == (int)me)
                        compositor_xwm_at = &compositor_xwm_slots[at], compositor_xwm_drop("the check is over");
        compositor_xwm_at = &compositor_xwm_slots[0];
        system_close((int)first), system_close((int)second), system_close((int)listener);
        compositor_xwm_listen_fd = saved_listen, compositor_xwm_uid = saved_uid;
}

// Managed X11 mapping lifecycle: WL_SURFACE_SERIAL arriving after MapRequest's property replies (or replacing a surface)
// must not wipe PROPS_READ or _NET_WM_STATE flags; an earlier empty GetProperty(_NET_WM_STATE) reply must not wipe a
// _NET_WM_STATE ClientMessage queued right after MapWindow; and UnmapNotify clears per-mapping state for the next map.
static void xwm_serial_state_rows(void)
{
        typeof(compositor_xwm_slots[0]) *x = &compositor_xwm_slots[0];
        typeof(compositor_xwm_slots[0]) saved = *x;
        struct compositor_xwm_window *w;
        struct xwm_event ev;
        struct compositor_xwm_pending pend = {0};
        unsigned char reply[36];
        b32 first_kept, swap_kept, cm_kept, unmap_cleared;

        memory_zero(x, sizeof *x);
        x->fd = -1;
        x->atoms[COMPOSITOR_XWM_A_SERIAL] = 101;
        x->atoms[COMPOSITOR_XWM_A_NET_STATE] = 102;
        x->atoms[COMPOSITOR_XWM_A_FULLSCREEN] = 103;
        x->atoms[COMPOSITOR_XWM_A_TYPE] = 104;
        compositor_xwm_at = x;

        // 1. MapRequest -> PROP_STATE (fullscreen) + PROP_TYPE (PROPS_READ) -> first WL_SURFACE_SERIAL.
        w = compositor_xwm_find(0x400001, 1);
        if (w)
                w->state = COMPOSITOR_XWM_MAPPED | COMPOSITOR_XWM_PROPS_READ | COMPOSITOR_XWM_FULLSCREEN;
        xwm_event_clear(&ev);
        ev.kind = XWM_EV_CLIENT_MESSAGE, ev.window = 0x400001, ev.format = XWM_FORMAT_32;
        ev.atom = x->atoms[COMPOSITOR_XWM_A_SERIAL], ev.data[0] = 0x1111, ev.data[1] = 0;
        compositor_xwm_event(&ev);
        first_kept = w && (w->state & COMPOSITOR_XWM_SERIAL) && (w->state & COMPOSITOR_XWM_PROPS_READ) &&
                     (w->state & COMPOSITOR_XWM_FULLSCREEN) && w->serial == 0x1111ull;

        // 2. Surface swap (new WL_SURFACE_SERIAL while mapped): detaches old row/ADOPTED, preserves PROPS_READ & FULLSCREEN.
        if (w)
                w->row = 2, w->state |= COMPOSITOR_XWM_ADOPTED | COMPOSITOR_XWM_RAISED;
        ev.data[0] = 0x2222;
        compositor_xwm_event(&ev);
        swap_kept = w && w->row == -1 && !(w->state & (COMPOSITOR_XWM_ADOPTED | COMPOSITOR_XWM_RAISED)) &&
                    (w->state & COMPOSITOR_XWM_PROPS_READ) && (w->state & COMPOSITOR_XWM_FULLSCREEN) && w->serial == 0x2222ull;

        // 3. Empty GetProperty(_NET_WM_STATE) reply arriving after _NET_WM_STATE ClientMessage (SDL2 fullscreen race) keeps FULLSCREEN.
        memory_zero(reply, sizeof reply);
        reply[0] = 1, reply[1] = XWM_FORMAT_32;
        xwm_put32(reply + 8, COMPOSITOR_XWM_ATOM_TYPE);
        pend.window = 0x400001, pend.what = COMPOSITOR_XWM_PROP_STATE;
        compositor_xwm_property(reply, 32, &pend);
        cm_kept = w && (w->state & COMPOSITOR_XWM_FULLSCREEN);

        // 4. Real UnmapNotify clears per-mapping state so the next MapRequest starts clean.
        xwm_event_clear(&ev);
        ev.kind = XWM_EV_UNMAP_NOTIFY, ev.window = 0x400001;
        compositor_xwm_event(&ev);
        unmap_cleared = w && w->row == -1 &&
                        !(w->state & (COMPOSITOR_XWM_MAPPED | COMPOSITOR_XWM_SERIAL | COMPOSITOR_XWM_PROPS_READ |
                                      COMPOSITOR_XWM_FULLSCREEN | COMPOSITOR_XWM_ADOPTED));

        check("xwm: WL_SURFACE_SERIAL keeps MapRequest properties and fullscreen, empty _NET_WM_STATE reply keeps ClientMessage fullscreen, UnmapNotify resets",
              first_kept && swap_kept && cm_kept && unmap_cleared);

        // 5. Promoted fullscreen override-redirect window is not treated as a popup and queues title/class/protocols/owner.
        {
                struct compositor_xwm_window *ow = compositor_xwm_find(0x400002, 1);
                b32 promoted_ok = false, queued_class = false, queued_proto = false, queued_owner = false, queued_title = false;

                x->xres = 140;
                x->atoms[COMPOSITOR_XWM_A_NET_NAME] = 105;
                x->atoms[COMPOSITOR_XWM_A_UTF8] = 106;
                x->atoms[COMPOSITOR_XWM_A_PROTOCOLS] = 107;
                if (ow)
                {
                        ow->state = COMPOSITOR_XWM_OVERRIDE | COMPOSITOR_XWM_FULLSCREEN | COMPOSITOR_XWM_COVERED;
                        compositor_xwm_promote_props(ow);
                        for (unsigned int k = 0; k < x->pending_count; k++)
                        {
                                const struct compositor_xwm_pending *p = &x->pending[(x->pending_at + k) % x->pending_room];

                                if (p->window != 0x400002)
                                        continue;
                                queued_title |= p->what == COMPOSITOR_XWM_PROP_NET_NAME;
                                queued_proto |= p->what == COMPOSITOR_XWM_PROP_PROTOCOLS;
                                queued_class |= p->what == COMPOSITOR_XWM_PROP_CLASS;
                                queued_owner |= p->what == COMPOSITOR_XWM_PROP_OWNER;
                        }
                        promoted_ok = !compositor_xwm_is_popup(ow) && queued_title && queued_proto && queued_class && queued_owner;
                }
                check("xwm: promoted fullscreen override-redirect window is a managed toplevel and queries title/class/protocols/owner", promoted_ok);
        }

        wayland_table_drop(&x->pending_table);
        wayland_table_drop(&x->window_index);
        wayland_table_drop(&x->window_table);
        *x = saved;
        compositor_xwm_at = &compositor_xwm_slots[0];
}

// A window asked to go is asked each time (compositor_close counts the asks, and the X window manager tells its window of each one it has not told it of): one that
// answered with a question and stayed is asked by the next press. WINDOW_CLOSING alone carried it, set once and never cleared, and the ask went once.
// The count is one, said three ways: the row's program reads it in the table and hears each ask once (windows_close_asked), the Wayland shim is given it as the
// row's facts, and the X window manager reads the compositor's own. A kit program read the bit, on every pass: an editor that stayed for unsaved changes left the
// moment they were saved.
static void close_ask_rows(void)
{
        static struct windows *table_scratch;
        struct windows *const table_at = windows_scratch_table(&table_scratch);
        struct windows *kept = compositor.table;
        struct compositor_xwm_window w = {.id = 9, .row = 3, .state = COMPOSITOR_XWM_PROTO_KNOWN, .protocols = COMPOSITOR_XWM_PROTO_DELETE};
        struct window_row own = {.table = table_at, .row = 3};
        struct wayland_facts facts;
        int parent = compositor_popup_parent[3];
        struct compositor_rows woken = compositor.woken;

        compositor.table = table_at;
        compositor.showing[3] = 1, compositor.state[3] = 0, compositor.close_asks[3] = 0, compositor_popup_parent[3] = -1;
        table_at->close_asks[3] = 0;
        check("close: a window nobody asked to go is owed no ask", !compositor_xwm_close_owed(&w));
        check("close: and its program hears none", !windows_close_asked(&own));
        check("close: nor does its Wayland client", compositor_wl_row_facts(0, 3, &facts) == 0 && facts.closing == 0);
        compositor_close(3);
        check("close: a window asked to go is said to be closing, in the table too, and is owed the ask",
              (compositor.state[3] & WINDOW_CLOSING) && (table_at->state[3] & WINDOW_CLOSING) && compositor.close_asks[3] == 1 && compositor_xwm_close_owed(&w));
        check("close: the table says how many times it was asked", table_at->close_asks[3] == 1);
        check("close: its program hears the ask once, however often it looks", windows_close_asked(&own) && !windows_close_asked(&own) && !windows_close_asked(&own));
        check("close: its Wayland client is given the count, and the bit stays the compositor's",
              compositor_wl_row_facts(0, 3, &facts) == 0 && facts.closing == 1 && (compositor.state[3] & WINDOW_CLOSING));
        w.closes = compositor.close_asks[3];
        check("close: once told it is owed nothing while it stays", !compositor_xwm_close_owed(&w));
        compositor_close(3);
        check("close: asked again it is owed the ask again", compositor.close_asks[3] == 2 && compositor_xwm_close_owed(&w));
        check("close: its program hears the second ask, once", table_at->close_asks[3] == 2 && windows_close_asked(&own) && !windows_close_asked(&own));
        check("close: and its Wayland client's count is one more", compositor_wl_row_facts(0, 3, &facts) == 0 && facts.closing == 2);
        // A program cannot ask itself to go, or another, by the table: the compositor's count is its own.
        table_at->close_asks[3] = 77;
        check("close: a count written into the table is not the compositor's", compositor.close_asks[3] == 2 && !compositor_xwm_close_owed(&(struct compositor_xwm_window){.row = 3, .closes = 2, .state = COMPOSITOR_XWM_PROTO_KNOWN}) &&
                                                                               compositor_wl_row_facts(0, 3, &facts) == 0 && facts.closing == 2);
        table_at->close_asks[3] = 2, own.closes = 2;
        w.closes = compositor.close_asks[3];
        w.state &= ~COMPOSITOR_XWM_PROTO_KNOWN;
        compositor_close(3);
        check("close: an ask waits for the window's protocols to be known", !compositor_xwm_close_owed(&w));
        w.state |= COMPOSITOR_XWM_PROTO_KNOWN;
        check("close: and is owed when they are", compositor_xwm_close_owed(&w));
        compositor.close_asks[3] = 255, w.closes = 255;
        table_at->close_asks[3] = 255, own.closes = 255;
        compositor_close(3);
        check("close: the count of asks never comes back to none", compositor.close_asks[3] == 1 && compositor_xwm_close_owed(&w));
        check("close: and the ask past the count's end is heard by the program", table_at->close_asks[3] == 1 && windows_close_asked(&own) && !windows_close_asked(&own));
        // The row let go of and shown again is another window: it has been asked nothing, and its first ask is heard.
        compositor_close_count(3, 0);
        check("close: a row whose count went back to none has been asked nothing", table_at->close_asks[3] == 0 && !windows_close_asked(&own) && own.closes == 0);
        compositor.state[3] = 0;
        compositor_close(3);
        check("close: and the next window in it hears its first ask", windows_close_asked(&own) && !windows_close_asked(&own));
        compositor.close_asks[3] = 1, w.closes = 0;
        compositor_xwm_unmapped(&w);
        check("close: a mapping that ended is owed nothing, and its next is told from the first ask", w.row == -1 && !w.closes && !compositor_xwm_close_owed(&w));
        {
                struct compositor_xwm_window *first = compositor_xwm_find(0x7700001u, 1), *next;

                if (first)
                        first->closes = 3;
                compositor_xwm_forget(0x7700001u);
                next = compositor_xwm_find(0x7700002u, 1);
                check("close: a record given to another X window has been told of no ask", first && next == first && !next->closes);
                compositor_xwm_forget(0x7700002u);
        }
        // A popup is dismissed once: a press outside it while it goes is not another ask.
        compositor.state[3] = 0, compositor.close_asks[3] = 0, compositor_popup_parent[3] = 2;
        compositor_close(3);
        compositor_close(3);
        check("close: a popup is asked to go once", (compositor.state[3] & WINDOW_CLOSING) && compositor.close_asks[3] == 1 && table_at->close_asks[3] == 1);
        compositor.showing[3] = 0, compositor.state[3] = 0, compositor.close_asks[3] = 0, compositor_popup_parent[3] = parent;
        table_at->close_asks[3] = 0;
        compositor.woken = woken, compositor.table = kept;
        // A title's request that cannot be queued is not waited for: counted as asked, no reply ever came to take it off, and the title was
        // never asked of again for as long as the window lived.
        {
                struct compositor_xwm_window *t = compositor_xwm_find(0x7700003u, 1);
                long out = compositor_xwm.out_len;
                unsigned int seq = compositor_xwm.seq, at = compositor_xwm.pending_at, count = compositor_xwm.pending_count;
                int broken = compositor_xwm.broken;

                compositor_xwm.out_len = COMPOSITOR_XWM_OUT;
                if (t)
                        compositor_xwm_ask_title(t);
                check("xwm title: a request with no room in the queue is not waited for", t && t->asking == 0 && compositor_xwm.pending_count == count);
                compositor_xwm.out_len = 0, compositor_xwm.broken = 0;
                if (t)
                        compositor_xwm_ask_title(t);
                check("xwm title: and the title is asked of when there is room, and waited for once",
                      t && t->asking == 1 && compositor_xwm.pending_count == count + 1 && !(t->state & COMPOSITOR_XWM_TITLE_AGAIN));
                compositor_xwm_forget(0x7700003u);
                compositor_xwm.out_len = out, compositor_xwm.seq = seq, compositor_xwm.pending_at = at, compositor_xwm.pending_count = count;
                compositor_xwm.broken = broken;
        }
}

// A flip the card has not completed is waited for (compositor_flip_wait), to a deadline of COMPOSITOR_FLIP_PERIODS periods after it was asked: a present tried against
// it composed nothing and used up its period. No flip in flight, or one past the deadline (a screen that is off never answers), is no wait.
/*
        The descriptor table's shares (compositor_descriptor_crowded) and the clipboard's two numbers (compositor_clip_reserve). The numbers
        are held by /dev/null while a selection is empty and given back to it when one is let go of, so the kernel never numbers another
        descriptor 900 or 901, which the next copy would close under its holder; and a client whose descriptors are numbered in the
        compositor's own share is dropped with them, so the clients cannot fill the table.
*/
static int descriptor_held(long number)
{
        return (long)system_call_3(syscall(fcntl), (positive)number, 1, 0) >= 0; // F_GETFD
}

static int descriptor_is(long number, const char *what)
{
        char path[40] = "/proc/self/fd/", link[64];
        long length;

        positive_into_string((string_address)path + 14, (positive)number);
        memory_zero(link, sizeof(link));
        length = (long)system_call_4(syscall(readlinkat), (positive)AT_FDCWD, (positive)path, (positive)link, sizeof(link) - 1);
        if (length <= 0)
                return 0;
        for (long at = 0; what[at]; at++)
                if (link[at] != what[at])
                        return 0;
        return 1;
}

static void descriptor_rows(void)
{
        static struct windows *table_scratch;
        struct windows *const table_at = windows_scratch_table(&table_scratch);
        struct windows *kept = compositor.table;
        long most = compositor_descriptors_most, memfd, lowest, after;
        int pair[2] = {-1, -1}, slot = 31, was;

        compositor_wl_slots_reach(32); // a client's slot the host has arrays for, as the shim's connect makes them
        was = compositor_wl.fd[slot];

        check("descriptors: a number below the compositor's own share is a client's to have",
              !compositor_descriptor_crowded(3) && !compositor_descriptor_crowded(most - COMPOSITOR_DESCRIPTORS_OWN - 1));
        check("descriptors: one in the compositor's own share says the clients' is full",
              compositor_descriptor_crowded(most - COMPOSITOR_DESCRIPTORS_OWN) && compositor_descriptor_crowded(most - 1));

        compositor.table = table_at;
        compositor_clip_reserve();
        check("clipboard numbers: both are held from the start, by /dev/null",
              compositor_clip.blank >= 0 && descriptor_is(WINDOWS_CLIP_FD, "/dev/null") && descriptor_is(WINDOWS_CLIP_FD + 1, "/dev/null"));
        memfd = (long)system_call_2(syscall(memfd_create), (positive) "copy", 1);
        compositor_clip_keep(WINDOWS_CLIP_CLIPBOARD, (int)memfd, 0, -2);
        check("clipboard numbers: a copy is at its number", compositor_clip.fd[0] == WINDOWS_CLIP_FD && descriptor_is(WINDOWS_CLIP_FD, "/memfd:copy"));
        compositor_clip_clear(WINDOWS_CLIP_CLIPBOARD);
        check("clipboard numbers: a selection let go of gives its number back to /dev/null, not to the table",
              compositor_clip.fd[0] == -1 && descriptor_is(WINDOWS_CLIP_FD, "/dev/null"));
        after = (long)system_call_3(syscall(fcntl), 0, 1030, WINDOWS_CLIP_FD); // F_DUPFD_CLOEXEC: the lowest free number from the selection's
        check("clipboard numbers: so the next descriptor made is at neither", after > WINDOWS_CLIP_FD + 1);
        if (after >= 0)
                system_close(after);

        // A client that sends a descriptor when every number left is the compositor's own: it is dropped, and the descriptor is not kept.
        if ((long)system_call_4(syscall(socketpair), 1, 1, 0, (positive)pair) == 0)
        {
                positive control[3] = {0, 0, 0}, message[7] = {0}, vector[2];
                char byte = 0;
                int sent = (int)system_call_2(syscall(memfd_create), (positive) "sent", 1);

                // One byte, and SCM_RIGHTS with one descriptor: a cmsghdr of 16 bytes (its length, SOL_SOCKET, SCM_RIGHTS) and the number.
                control[0] = 20;
                ((int *)control)[2] = 1, ((int *)control)[3] = 1, ((int *)control)[4] = sent;
                vector[0] = (positive)&byte, vector[1] = 1;
                message[2] = (positive)vector, message[3] = 1, message[4] = (positive)control, message[5] = 24;
                system_call_3(syscall(sendmsg), (positive)pair[1], (positive)message, 0);
                system_close(sent);
                lowest = (long)system_call_3(syscall(fcntl), 0, 1030, 3);
                system_close(lowest);
                compositor_wl.fd[slot] = pair[0];
                compositor_descriptors_most = COMPOSITOR_DESCRIPTORS_OWN + 3;
                compositor_wl_read(slot);
                compositor_descriptors_most = most;
                after = (long)system_call_3(syscall(fcntl), 0, 1030, 3);
                system_close(after);
                // The connection's own number is free again, so the lowest free is that or lower: the descriptor received took none for good.
                check("descriptors: a client whose descriptor is numbered in the compositor's share is dropped", compositor_wl.fd[slot] == -1 && !descriptor_held(pair[0]));
                check("descriptors: and what it sent is not kept open", after <= lowest && after <= pair[0]);
                system_close(pair[1]);
        }
        else
                check("descriptors: a socket pair is made", false);
        compositor_wl.fd[slot] = was;
        compositor.table = kept;
}

static void flip_wait_rows(void)
{
        positive period = compositor.period, issued = compositor.flip_issued;
        int pending = compositor.flip_pending;

        compositor.period = 16666666ull, compositor.flip_issued = 1000000000ull, compositor.flip_pending = 1;
        check("flip: a flip in flight is waited for, to a deadline of four periods", compositor_flip_wait(1000000000ull + 16666666ull) == 1000000000ull + 4 * 16666666ull);
        check("flip: the wait holds until its deadline", compositor_flip_wait(1000000000ull + 4 * 16666666ull - 1) == 1000000000ull + 4 * 16666666ull);
        check("flip: past the deadline it is no wait (an event that never comes)", compositor_flip_wait(1000000000ull + 4 * 16666666ull) == 0);
        check("flip: a flip is not lost while its wait runs, or a moment short of its sixteen periods", !compositor_flip_lost(1000000000ull + 4 * 16666666ull) &&
              !compositor_flip_lost(1000000000ull + COMPOSITOR_FLIP_LOST - 1));
        check("flip: sixteen periods on with no event it is lost, and is let go of", compositor_flip_lost(1000000000ull + COMPOSITOR_FLIP_LOST) == 1);
        // A lost event froze a fullscreen game for the whole give-up, a second; it is sixteen periods now, never less than 100 ms.
        check("flip: at 60 Hz a flip is given up on after 267 ms, not a second", COMPOSITOR_FLIP_LOST == 16 * 16666666ull &&
              compositor_flip_lost(1000000000ull + 266666656ull) && !compositor_flip_lost(1000000000ull + 266666655ull));
        compositor.period = 6944444ull;
        check("flip: at 144 Hz after 111 ms", compositor_flip_lost(1000000000ull + 111111104ull) && !compositor_flip_lost(1000000000ull + 111111103ull));
        compositor.period = 2000000ull;
        check("flip: and at 500 Hz never sooner than 100 ms", compositor_flip_lost(1000000000ull + 100000000ull) && !compositor_flip_lost(1000000000ull + 99999999ull));
        {
                // A fullscreen game's callbacks owed to a flip whose event is late are answered once the flip's wait is over, the flip still pending.
                positive kept_sent = compositor_wl_frame_sent_at, kept_at = compositor_wl_frame_at;
                int kept_owed = compositor_wl_frame_owed, kept_started = compositor_wl.started;
                b32 waits, answered;

                compositor.period = 6944444ull, compositor_wl.started = 0, compositor_wl_frame_owed = 1, compositor_wl_frame_at = 0, compositor_wl_jit_sent(5);
                compositor_wl_flip_overdue(1000000000ull + 4 * 6944444ull - 1);
                waits = compositor_wl_frame_owed == 1 && compositor_wl_frame_sent_at == 5;
                compositor_wl_flip_overdue(1000000000ull + 4 * 6944444ull);
                answered = compositor_wl_frame_owed == 0 && compositor_wl_frame_sent_at == 1000000000ull + 4 * 6944444ull && compositor.flip_pending;
                check("flip: a game's callbacks owed to a late flip wait its four periods, then are answered, the flip still pending", waits && answered);
                compositor_wl_frame_sent_at = kept_sent, compositor_wl_frame_at = kept_at, compositor_wl_frame_owed = kept_owed, compositor_wl.started = kept_started;
                compositor.period = 16666666ull;
        }
        check("flip: a clock read before the flip was asked loses nothing", !compositor_flip_lost(999999999ull));
        compositor.flip_pending = 0;
        check("flip: no flip in flight, no wait", compositor_flip_wait(1000000000ull + 1) == 0);
        check("flip: and none to lose", !compositor_flip_lost(1000000000ull + 2 * COMPOSITOR_FLIP_LOST));
        compositor.period = 0, compositor.flip_pending = 1;
        check("flip: before a screen has said its period the frame is a 60th of a second", compositor_flip_wait(1000000000ull + 1) == 1000000000ull + 4 * 16666667ull);
        compositor.period = period, compositor.flip_issued = issued, compositor.flip_pending = pending;
        // A flip's event is its own (compositor_card_events): the event of a flip given up on as lost, come late, is not the next flip's. Taken
        // for it, every flip after was let go of a frame early. The events are struct drm_event_vblank: type 2, length 32, the user data, a
        // stamp and a sequence.
        {
                static struct windows *flip_table_scratch;
                struct windows *const flip_table_at = windows_scratch_table(&flip_table_scratch);
                struct windows *kept = compositor.table;
                positive serial = compositor.flip_serial;
                struct compositor_rows held = compositor.held_rows, dropping = compositor.dropping;
                p8 events[64];
                unsigned int type = COMPOSITOR_EVENT_FLIP_COMPLETE, length = 32;
                positive stale = 7, mine = 8;

                compositor.table = flip_table_at, compositor.held_rows = compositor.dropping = rows_from(0);
                memory_zero(events, sizeof(events));
                memory_copy(events, &type, 4), memory_copy(events + 4, &length, 4), memory_copy(events + 8, &stale, 8);
                memory_copy(events + 32, &type, 4), memory_copy(events + 36, &length, 4), memory_copy(events + 40, &mine, 8);
                compositor.flip_serial = 8, compositor.flip_pending = 1;
                check("flip: the late event of a flip given up on is not the flip in flight's", compositor_card_events(events, 32) == 0 && compositor.flip_pending == 1);
                check("flip: the flip in flight's own event lets go of it, once", compositor_card_events(events, 64) == 1 && compositor.flip_pending == 0);
                check("flip: an event with no flip in flight lets go of nothing", compositor_card_events(events + 32, 32) == 0 && compositor.flip_pending == 0);
                compositor.flip_pending = 1;
                check("flip: an event cut short is read as far as it is whole", compositor_card_events(events, 40) == 0 && compositor.flip_pending == 1);
                compositor.table = kept, compositor.flip_serial = serial, compositor.flip_pending = pending;
                compositor.held_rows = held, compositor.dropping = dropping;
        }
}

// A frame is dated when it was due, not when the loop woke to make it (compositor_frame_dated), so the next is due a period after that and the
// time each wake took is not added to every frame after it; a first frame, an early one and one a period or more late are dated as made.
static void frame_dated_rows(void)
{
        positive period = 16666667ull, at = 5000000000ull, wake = 150000ull;

        check("frame: the first frame is dated when it is made", compositor_frame_dated(0, period, at) == at);
        check("frame: one made a moment after it was due is dated when it was due", compositor_frame_dated(at, period, at + period + wake) == at + period);
        check("frame: and one made the moment it was due", compositor_frame_dated(at, period, at + period) == at + period);
        check("frame: one made a nanosecond short of a period late is still dated when it was due",
              compositor_frame_dated(at, period, at + 2 * period - 1) == at + period);
        check("frame: one made a period late is dated as made: nothing was owed in between", compositor_frame_dated(at, period, at + 2 * period) == at + 2 * period);
        check("frame: one made before it was due is dated as made", compositor_frame_dated(at, period, at + period - 1) == at + period - 1);
        check("frame: with no period said, as made", compositor_frame_dated(at, 0, at + 5) == at + 5);
        {
                // A thousand frames, each woken 150 microseconds late: the last is dated a thousand periods after the first, where the
                // latenesses added up it was 150 milliseconds (nine frames) behind.
                positive dated = at;

                for (int frame = 0; frame < 1000; frame++)
                        dated = compositor_frame_dated(dated, period, dated + period + wake);
                check("frame: a thousand frames each woken late are a thousand periods, the latenesses not added", dated == at + 1000 * period);
        }
}

/*
        When a present is due (compositor_present_due): with no lead, no vblank's stamp, a stamp ahead of the clock or one a
        period old, a period after the last present or at once, as it was; with a lead and a vblank inside the last period,
        the lead before the vblank after it, at once when that has gone by, and never sooner than half a period after the last
        present. And the lead a present is given, and the count of the ones that missed.
*/
static void present_due_rows(void)
{
        positive period = 16666667ull, v = 7000000000ull, lead = 3000000ull, last = v - period + 100000ull;
        b32 same = true;

        // With no lead the rule is the old one to the nanosecond, whatever the vblank says.
        for (positive presented = v - 3 * period; presented <= v + period; presented += period / 7)
                for (positive now = v - period; now <= v + 3 * period; now += period / 5)
                {
                        positive old = now - presented < period ? presented + period : now;

                        same &= compositor_present_due(presented, period, v, 0, now) == old && compositor_present_due(presented, period, 0, lead, now) == old &&
                                compositor_present_due(presented, period, now + 1, lead, now) == old &&
                                compositor_present_due(presented, period, v, period, now) == old &&
                                (now < v + period || compositor_present_due(presented, period, v, lead, now) == old);
                }
        check("present: with no lead, no vblank's stamp, a stamp ahead of the clock, a lead of a period, or a vblank a period old, it is due as it was", same);
        check("present: the moment a flip lands, the next is due the lead before the vblank after it",
              compositor_present_due(last, period, v, lead, v + 50000) == v + period - lead);
        check("present: and still then a millisecond on", compositor_present_due(last, period, v, lead, v + 1000000) == v + period - lead);
        check("present: at that time it is due now", compositor_present_due(last, period, v, lead, v + period - lead) == v + period - lead);
        check("present: past it and before the vblank it is due at once, late as it is",
              compositor_present_due(last, period, v, lead, v + period - 1000) == v + period - 1000);
        check("present: a present made a moment ago holds the next for half a period, whatever the vblank says",
              compositor_present_due(v + period - lead, period, v, lead, v + period - lead + 1000) == v + period - lead + period / 2);
        check("present: a vblank a period old is no vblank to go by: a period after the last present",
              compositor_present_due(v + period / 2, period, v, lead, v + period) == v + period / 2 + period);
        {
                // A steady run: each flip lands at its vblank, and each present is made the lead before the next one, a period apart.
                positive presented = last, vblank = v, now = v + 30000;
                b32 steady = true;

                for (int frame = 0; frame < 500; frame++)
                {
                        positive due = compositor_present_due(presented, period, vblank, lead, now);

                        steady &= due == vblank + period - lead;
                        presented = due; // dated as made, as the loop dates a late one
                        vblank += period;
                        now = vblank + 30000;
                }
                check("present: five hundred frames are each made the lead before their vblank", steady);
        }
        {
                // The lead rule (compositor_lead_of): the worst present measured, half as much again, a millisecond and what misses widened it by,
                // clamped to the period; none with fewer than four presents timed or no period.
                static const struct
                {
                        positive worst;
                        unsigned int timed;
                        positive widen, period, lead;
                } leads[] = {
                    {2000000ull, 32, 0, 16666667ull, 4000000ull},                      // a card's 2 ms present: 4 ms before its vblank
                    {0, 4, 0, 16666667ull, COMPOSITOR_LEAD_SPARE},                     // four timed are enough, and the millisecond is always there
                    {2000000ull, 3, 0, 16666667ull, 0},                                // three are not: the old rule
                    {2000000ull, 32, 16666667ull / 4, 16666667ull, 4000000ull + 4166666ull}, // a miss's quarter period on top
                    {12000000ull, 32, 0, 16666667ull, 0},                              // 19 ms does not fit in the period: none, the old rule
                    {20000000ull, 32, 0, 16666667ull, 0},                              // a job longer than the period: none
                    {3000000ull, 32, 0, 6944444ull, 5500000ull},                       // 144 Hz: 3 ms of compose leaves 5.5 ms
                    {5000000ull, 32, 0, 6944444ull, 0},                                // and 5 ms does not fit: the old rule
                    {0, 32, 16666667ull, 16666667ull, 0},                              // widened to the period by misses: the old rule
                    {2000000ull, 32, 0, 0, 0},                                         // a screen that said no period: none
                };
                b32 rule = true;

                for (unsigned int at = 0; at < sizeof(leads) / sizeof(leads[0]); at++)
                        rule &= compositor_lead_of(leads[at].worst, leads[at].timed, leads[at].widen, leads[at].period) == leads[at].lead;
                check("present: the lead is the worst present measured, half as much again, a millisecond and the misses' widening, none where that does not fit the period", rule);
        }
        {
                // When the next present is due, from what was measured: the last present, the period, the vblank's stamp, the worst of the presents timed
                // (and how many, and the misses' widening) and the clock, against the time it is due.
                static const struct
                {
                        long long presented, vblank, now; // from v
                        positive period, worst;
                        unsigned int timed;
                        positive widen;
                        long long due; // from v
                } dues[] = {
                    {-16666667ll + 100000, 0, 50000, 16666667ull, 2000000ull, 32, 0, 16666667ll - 4000000},       // a card's 2 ms: 4 ms before the vblank
                    {-16666667ll + 100000, 0, 50000, 16666667ull, 12000000ull, 32, 0, 100000},                    // 12 ms measured: the old rule, a period on
                    {-16666667ll + 100000, 0, 50000, 16666667ull, 2000000ull, 3, 0, 100000},                      // too few timed: the old rule
                    {-16666667ll + 100000, 0, 50000, 16666667ull, 2000000ull, 32, 16666667ull / 4, 16666667ll - 8166666}, // widened by a miss
                    {-16666667ll + 100000, 0, 16666667ll - 2000000, 16666667ull, 2000000ull, 32, 0, 16666667ll - 2000000}, // its time gone by: now
                    {10000000ll, 0, 10000100ll, 16666667ull, 2000000ull, 32, 0, 10000000ll + 8333333},             // a present a moment ago: half a period on
                    {-16666667ll + 100000, 16666667ll, 16666667ll + 16666667ll, 16666667ull, 2000000ull, 32, 0, 16666667ll + 16666667ll}, // a vblank a period old
                    {-6944444ll + 100000, 0, 50000, 6944444ull, 3000000ull, 32, 0, 6944444ll - 5500000},          // 144 Hz, 3 ms: 5.5 ms before
                };
                b32 table = true;
                unsigned int wrong = 0;

                for (unsigned int at = 0; at < sizeof(dues) / sizeof(dues[0]); at++)
                {
                        positive lead = compositor_lead_of(dues[at].worst, dues[at].timed, dues[at].widen, dues[at].period);
                        positive due = compositor_present_due((positive)((long long)v + dues[at].presented), dues[at].period, (positive)((long long)v + dues[at].vblank), lead,
                                                              (positive)((long long)v + dues[at].now));

                        if (due != (positive)((long long)v + dues[at].due))
                                table = false, wrong = wrong ? wrong : at + 1;
                }
                check("present: the table of the last present, the period, the vblank and what was measured is due where it says", table);
                if (!table)
                        string_format(log, "present: the first wrong entry of the table is %p\n", (positive)wrong);
                check("present: and with no vblank's stamp (a headless compositor) it is the old rule whatever was measured",
                      compositor_present_due(v - period + 100000ull, period, 0, compositor_lead_of(2000000ull, 32, 0, period), v + 50000ull) == v + 100000ull);
        }
        {
                positive was_period = compositor.period, was_when = compositor.flip_when;
                int was_direct = compositor.direct_row, was_plane = compositor.plane_row;

                compositor.period = period, compositor.direct_row = -1, compositor.plane_row = -1; // composed presents
                memory_zero(&compositor_late, sizeof(compositor_late)), compositor_late.warm = COMPOSITOR_LEAD_WARM;
                compositor.flip_when = v;
                compositor_late_begin(lead, v + period - lead);
                check("present: a present made with a lead inside a vblank's period is for the vblank after it",
                      compositor_late.aim == v + period && compositor_late.made == 1);
                compositor_late_landed(v + period);
                check("present: its flip landing at that vblank is no miss", compositor_late.missed == 0 && compositor_late.composed.widen == 0 && compositor_late.aim == 0);
                compositor_late_begin(lead, v + period - lead);
                compositor_late_landed(v + 2 * period);
                check("present: its flip landing a vblank later is one, and widens the lead at once by a quarter of a period",
                      compositor_late.missed == 1 && compositor_late.composed.widen == period / 4 &&
                          compositor_lead_of(2000000ull, 32, compositor_late.composed.widen, period) == 4000000ull + period / 4);
                compositor_late_begin(0, v + period);
                check("present: a present by the old rule is for no vblank", compositor_late.aim == 0);
                compositor_late_landed(v + 9 * period);
                check("present: and its flip, whenever it lands, is no miss, and one of the calm", compositor_late.missed == 1 && compositor_late.composed.calm == 1);
                compositor_late_begin(lead, v + period - lead), compositor_late_landed(v + 2 * period);
                check("present: a second miss while widened is the old rule: the lead is none, and the next calm is twice as long",
                      compositor_late.missed == 2 && compositor_late.composed.widen == period && compositor_lead_of(2000000ull, 32, compositor_late.composed.widen, period) == 0 &&
                          compositor_late.composed.calm_need == 2 * COMPOSITOR_LEAD_CALM);
                for (unsigned int landed = 0; landed < 2 * COMPOSITOR_LEAD_CALM - 1; landed++)
                        compositor_late_begin(0, v + period), compositor_late_landed(v + period);
                check("present: flips by the old rule short of that calm keep it", compositor_late.composed.widen == period);
                compositor_late_begin(0, v + period), compositor_late_landed(v + period);
                check("present: the calm's last steps back to the quarter", compositor_late.composed.widen == period / 4 && compositor_late.composed.calm == 0);
                for (unsigned int landed = 0; landed < 2 * COMPOSITOR_LEAD_CALM; landed++)
                        compositor_late_begin(lead, v + period - lead), compositor_late_landed(v + period);
                check("present: and a calm of late presents that landed to nothing, the calm starting again from the first",
                      compositor_late.composed.widen == 0 && compositor_late.composed.calm_need == 0 && compositor_late.missed == 2);
                memory_zero(&compositor_late, sizeof(compositor_late)), compositor_late.warm = COMPOSITOR_LEAD_WARM;
                {
                        // A machine on which every late present misses (a driver that wants the flip long before the vblank): it falls to the old rule
                        // at the second miss and tries a late present again after a calm that doubles each time, so in 20000 frames (five and a half
                        // minutes at 60 Hz) it misses 8 times, and the lead in use is never one that grows towards the period: none, or the quarter.
                        positive measured = 2000000ull;
                        unsigned int none = 0, odd = 0;

                        for (unsigned int frame = 0; frame < 20000; frame++)
                        {
                                positive given = compositor_lead_of(measured, 32, compositor_late.composed.widen, period);

                                none += given == 0;
                                odd += given && given != compositor_lead_of(measured, 32, 0, period) && given != compositor_lead_of(measured, 32, period / 4, period);
                                compositor_late_begin(given, given ? v + period - given : v + period);
                                compositor_late_landed(given ? v + 2 * period : v + period);
                        }
                        check("present: a machine whose every late present misses is on the old rule nearly always, and tries one less and less often",
                              compositor_late.missed == 8 && none >= 20000 - 8 && odd == 0);
                        memory_zero(&compositor_late, sizeof(compositor_late)), compositor_late.warm = COMPOSITOR_LEAD_WARM;
                        // A 4K job longer than a period: measured, it is no lead at all, no present is made late and none misses.
                        for (unsigned int frame = 0; frame < 1000; frame++)
                        {
                                positive given = compositor_lead_of(20000000ull, 32, compositor_late.composed.widen, period);

                                compositor_late_begin(given, v + period);
                                compositor_late_landed(v + 2 * period);
                        }
                        check("present: a job longer than a period is presented by the old rule, none made late and none missed",
                              compositor_late.made == 0 && compositor_late.missed == 0 && compositor_late.composed.widen == 0);
                }
                memory_zero(&compositor_late, sizeof(compositor_late)), compositor_late.warm = COMPOSITOR_LEAD_WARM;
                {
                        // A composed present and one shown straight widen apart: misses at the desktop (composed) widened the one lead to the period, and
                        // a game shown straight then had the old rule too (500 us + the period is none) for up to 7680 flips, 53 s at 144 Hz.
                        int was_card = compositor.card, was_early = compositor_late.early;
                        positive direct_lead;
                        b32 composed_old;

                        compositor.card = 0, compositor_late.early = 0;
                        compositor_late_begin(lead, v + period - lead), compositor_late_landed(v + 2 * period);
                        compositor_late_begin(lead, v + period - lead), compositor_late_landed(v + 2 * period);
                        composed_old = compositor_late.composed.widen == period && compositor_late.missed == 2;
                        compositor.direct_row = 3, compositor.plane_row = -1;
                        direct_lead = compositor_present_lead();
                        check("present: two composed misses are the old rule for composed presents, and a flip shown straight keeps its 500 us lead",
                              composed_old && direct_lead == 500000ull && compositor_late.direct.widen == 0);
                        compositor_late_begin(direct_lead, v + period - direct_lead), compositor_late_landed(v + 2 * period);
                        check("present: a straight flip's miss widens its own lead a quarter, the composed one's as it was",
                              compositor_late.direct.widen == period / 4 && compositor_present_lead() == 500000ull + period / 4 && compositor_late.composed.widen == period);
                        compositor_late_reset();
                        check("present: a fullscreen game's start begins both leads from none", !compositor_late.direct.widen && !compositor_late.composed.widen &&
                              !compositor_late.direct.calm_need && !compositor_late.composed.calm_need && compositor_present_lead() == 500000ull);
                        // An idle gap (no flip for a quarter second, a card whose clock fell): the first flips' misses are counted and widen nothing.
                        compositor_late_begin(direct_lead, v + period - direct_lead), compositor_late_landed(v + period);
                        compositor_late_begin(0, v + 60 * period), compositor_late_landed(v + 60 * period); // the first flip after 58 periods (0.97 s) of none
                        compositor.flip_when = v + 60 * period;
                        compositor_late_begin(direct_lead, v + 61 * period - direct_lead), compositor_late_landed(v + 62 * period);
                        check("present: a miss in the first flips after an idle gap is counted and widens nothing",
                              compositor_late.missed == 4 && compositor_late.direct.widen == 0 && compositor_late.warm == 2);
                        compositor.flip_when = v + 61 * period;
                        for (unsigned int landed = 0; landed < COMPOSITOR_LEAD_WARM; landed++)
                                compositor_late_begin(direct_lead, v + 62 * period - direct_lead), compositor_late_landed(v + 62 * period);
                        compositor_late_begin(direct_lead, v + 62 * period - direct_lead), compositor_late_landed(v + 63 * period);
                        check("present: and once warm a miss widens again", compositor_late.missed == 5 && compositor_late.direct.widen == period / 4);
                        compositor.card = was_card, compositor_late.early = was_early;
                }
                memory_zero(&compositor_late, sizeof(compositor_late)), compositor_late.warm = COMPOSITOR_LEAD_WARM;
                compositor.period = was_period, compositor.flip_when = was_when, compositor.direct_row = was_direct, compositor.plane_row = was_plane;
        }
        {
                // The lead a present is given (compositor_present_lead) is measured on the last 32 presents, each in the metric ring of the path it took: all
                // the card's, the card's worst; all the processor's, the processor's; the two taking turns, the worse of them; none headless, none where the
                // owner asked for every present early, and none with fewer than four timed.
                static unsigned long long kept_card[WINDOWS_METRIC_RUN], kept_processor[WINDOWS_METRIC_RUN];
                unsigned int kept_card_at = compositor_metric_card_at, kept_card_count = compositor_metric_card_count;
                unsigned int kept_processor_at = compositor_metric_processor_at, kept_processor_count = compositor_metric_processor_count;
                unsigned int kept_paths = compositor_metric_paths, kept_paths_count = compositor_metric_paths_count;
                int kept_card_fd = compositor.card;
                positive was_period = compositor.period;

                memory_copy(kept_card, compositor_metric_card, sizeof(kept_card)), memory_copy(kept_processor, compositor_metric_processor, sizeof(kept_processor));
                memory_zero(&compositor_late, sizeof(compositor_late));
                compositor.period = period, compositor.card = 7; // a card (no call is made of it)
                for (unsigned int at = 0; at < WINDOWS_METRIC_RUN; at++)
                        compositor_metric_card[at] = 9000000ull, compositor_metric_processor[at] = 9000000ull; // older than the last 32: not read
                compositor_metric_card_at = 40, compositor_metric_card_count = 1000, compositor_metric_processor_at = 3, compositor_metric_processor_count = 3;
                for (unsigned int back = 1; back <= COMPOSITOR_LEAD_FRAMES; back++)
                        compositor_metric_card[(40 + WINDOWS_METRIC_RUN - back) % WINDOWS_METRIC_RUN] = back == 17 ? 2000000ull : 1000000ull;
                compositor_metric_paths = ~0u, compositor_metric_paths_count = 32;
                compositor_metric_worst_dirty = 1; // (the rings written by hand: the worst is cached until a present is noted)
                check("present: after 32 of the card's presents the lead is the worst of them, half again and a millisecond", compositor_present_lead() == 4000000ull);
                compositor_metric_paths = 0, compositor_metric_paths_count = 3;
                compositor_metric_worst_dirty = 1; // (the rings written by hand: the worst is cached until a present is noted)
                check("present: after three of the processor's, with no more timed, none (the old rule)", compositor_present_lead() == 0);
                compositor_metric_processor_count = 4, compositor_metric_processor_at = 4, compositor_metric_processor[3] = 6000000ull, compositor_metric_paths_count = 4;
                compositor_metric_worst_dirty = 1; // (the rings written by hand: the worst is cached until a present is noted)
                check("present: and after four the worst of them", compositor_present_lead() == 9000000ull + 4500000ull + COMPOSITOR_LEAD_SPARE);
                for (unsigned int at = 0; at < WINDOWS_METRIC_RUN; at++)
                        compositor_metric_processor[at] = 3000000ull;
                compositor_metric_processor_at = 8, compositor_metric_processor_count = 500, compositor_metric_processor[7] = 2500000ull;
                compositor_metric_paths = 0x55555555u, compositor_metric_paths_count = 32; // the card and the processor taking turns: 16 of each
                compositor_metric_worst_dirty = 1; // (the rings written by hand: the worst is cached until a present is noted)
                check("present: with the card and the processor taking turns, the worse of the two (the processor's 3 ms over the card's 1)",
                      compositor_present_lead() == 3000000ull + 1500000ull + COMPOSITOR_LEAD_SPARE);
                compositor_metric_paths = 0xfffffffeu; // one of the processor's among 31 of the card's: its last one, 2.5 ms
                compositor_metric_worst_dirty = 1; // (the rings written by hand: the worst is cached until a present is noted)
                check("present: and one of the processor's among the card's is its last one, not its older ones", compositor_present_lead() == 2500000ull + 1250000ull + COMPOSITOR_LEAD_SPARE);
                compositor_metric_paths = ~0u, compositor_late.composed.widen = period / 4;
                compositor_metric_worst_dirty = 1; // (the rings written by hand: the worst is cached until a present is noted)
                check("present: a miss's widening is in it", compositor_present_lead() == 4000000ull + period / 4);
                compositor_late.early = 1;
                check("present: the owner's present-early is the old rule", compositor_present_lead() == 0);
                compositor_late.early = 0, compositor.card = -1;
                check("present: a headless compositor has no lead", compositor_present_lead() == 0);
                memory_zero(&compositor_late, sizeof(compositor_late));
                memory_copy(compositor_metric_card, kept_card, sizeof(kept_card)), memory_copy(compositor_metric_processor, kept_processor, sizeof(kept_processor));
                compositor_metric_card_at = kept_card_at, compositor_metric_card_count = kept_card_count;
                compositor_metric_processor_at = kept_processor_at, compositor_metric_processor_count = kept_processor_count;
                compositor_metric_paths = kept_paths, compositor_metric_paths_count = kept_paths_count, compositor.card = kept_card_fd, compositor.period = was_period;
        }
}

/*
        Fullscreen for every kind of window: on and off keep what the window filled (floating at its place, or maximised in its room), it is put
        on top with the keys, the taskbar slides for a fullscreen window only while it is the top one, a client's row (NO_CLOSE and NO_GRAB, no
        WINDOW_GLASS) filling the screen from its own dma-buf is the one to show straight where a glass one is not, F11 is the compositor's for
        Canvas's own windows, an X window mapped over a whole screen is fullscreen on it, and a client framebuffer the card still shows waits to
        be removed until the card is off it.
*/
static void fullscreen_rows(void)
{
        static struct windows *tbl_scratch;
        struct windows *const tbl_at = windows_scratch_table(&tbl_scratch);
        static unsigned char kept_comp[sizeof(compositor)], kept_xwm[sizeof(compositor_xwm)];
        static unsigned int pixels[64 * 64];
        int kept_parent[2] = {compositor_popup_parent[0], compositor_popup_parent[1]};
        char kept_driver[sizeof compositor.driver];
        struct compositor_frame frame;
        struct compositor_xwm_window w;
        unsigned int rows[1] = {0}, graves;

        memory_copy(kept_comp, &compositor, sizeof(compositor));
        memory_copy(kept_xwm, &compositor_xwm, sizeof(compositor_xwm));
        memory_copy(kept_driver, compositor.driver, sizeof kept_driver);
        compositor_zero();
        memory_zero(&compositor_xwm, sizeof(compositor_xwm));
        windows_scratch_zero(tbl_at);
        compositor.card = -1;
        compositor.table = tbl_at;
        compositor.width = 1920, compositor.height = 1080, compositor.scale = 100;
        compositor.drag = COMPOSITOR_NONE, compositor.resize = COMPOSITOR_NONE;
        compositor.shared_row = COMPOSITOR_NONE, compositor.focus = COMPOSITOR_NONE, compositor.grab = COMPOSITOR_NONE;
        compositor_popup_parent[0] = compositor_popup_parent[1] = -1;
        compositor.showing[0] = compositor.showing[1] = 1;
        compositor.stacked = 2, compositor.stack[0] = 0, compositor.stack[1] = 1;
        compositor.x[0] = 100, compositor.y[0] = 120, compositor.w[0] = 400, compositor.h[0] = 300;
        compositor.saved_x[0] = 100, compositor.saved_y[0] = 120, compositor.saved_w[0] = 400, compositor.saved_h[0] = 300;
        compositor.x[1] = 700, compositor.y[1] = 200, compositor.w[1] = 300, compositor.h[1] = 200;

        compositor_fullscreen_on(0, -1);
        check("fullscreen: a floating window asked is maximised over its screen, marked, on top and with the keys",
              compositor.fullscreen[0] && compositor.arranged[0] == COMPOSITOR_MAXIMIZED && compositor.x[0] == 0 && compositor.y[0] == 0 &&
                  compositor.stack[compositor.stacked - 1] == 0 && compositor.focus == 0 && compositor.fullscreen_was[0] == COMPOSITOR_FLOATING);
        check("fullscreen: the taskbar slides away for the fullscreen window on top", compositor_fullscreen_up());
        {
                int tops[4] = {-1, -1, -1, -1};

                check("fullscreen: every screen's fullscreen top (the game slice's, one a screen) is that window here",
                      compositor_fullscreen_tops(tops, 4) == 1 && tops[0] == 0);
        }
        compositor_raise(1);
        check("fullscreen: and comes back while another window is over it", !compositor_fullscreen_up());
        {
                int tops[4];

                check("fullscreen: with another window over it no screen has a fullscreen top", compositor_fullscreen_tops(tops, 4) == 0);
        }
        compositor_raise(0);
        compositor_fullscreen_off(0);
        check("fullscreen: out of it, a window that floated floats again at its place and size",
              !compositor.fullscreen[0] && compositor.arranged[0] == COMPOSITOR_FLOATING && compositor.x[0] == 100 && compositor.y[0] == 120 &&
                  !compositor_fullscreen_up());
        compositor_arrange(0, COMPOSITOR_ROOM);
        compositor_fullscreen_on(0, -1);
        compositor_fullscreen_on(0, -1);
        compositor_fullscreen_off(0);
        check("fullscreen: a maximised window (asked twice) is maximised in its room again, where every way out floated it",
              !compositor.fullscreen[0] && compositor.arranged[0] == COMPOSITOR_ROOM);
        compositor_fullscreen_off(0);
        check("fullscreen: out of it when it is not fullscreen is nothing", compositor.arranged[0] == COMPOSITOR_ROOM);
        compositor_fullscreen_toggle(0);
        check("fullscreen: F11 puts a window in", compositor.fullscreen[0] && compositor.arranged[0] == COMPOSITOR_MAXIMIZED);
        check("fullscreen: F11 alone is the compositor's for a window of Canvas's own, and with Super",
              compositor_fullscreen_key(0, 1) && compositor_fullscreen_key(1, 0) && !compositor_fullscreen_key(0, 0));
        compositor_fullscreen_toggle(0);
        check("fullscreen: and F11 again takes it out", !compositor.fullscreen[0] && compositor.arranged[0] == COMPOSITOR_ROOM);

        // A client's window, fullscreen from its own dma-buf (lent): shown straight, unless it is glass.
        compositor_fullscreen_on(0, -1);
        memory_zero(compositor.driver, sizeof compositor.driver);
        memory_copy(compositor.driver, "amdgpu", 7);
        compositor.card = 1000; // no descriptor: the requests are the fakes, and RMFB is EBADF
        compositor.w[0] = 1920, compositor.h[0] = 1080, compositor.picture_w[0] = 1920, compositor.picture_h[0] = 1080;
        compositor.glide[0] = compositor.landing[0] = compositor.asking[0] = 0;
        tbl_at->glass[0] = WINDOW_NO_CLOSE | WINDOW_NO_GRAB, compositor.glassy[0] = 0;
        compositor_wl_unlend(0);
        memory_zero(&compositor_wl.lent[0], sizeof compositor_wl.lent[0]);
        compositor_wl.lent[0].map = (const unsigned char *)pixels, compositor_wl.lent[0].pixels = pixels, compositor_wl.lent[0].pitch = 1920;
        compositor_wl.lent[0].width = 1920, compositor_wl.lent[0].height = 1080, compositor_wl.lent[0].fd = 9;
        compositor_wl.lent[0].size = sizeof pixels, compositor_wl.lent[0].fourcc = 0x34325258u, compositor_wl.lent[0].modifier = 0;
        memory_zero(&frame, sizeof frame);
        frame.width = 1920, frame.height = 1080;
        gpu_fake_addfb_answer = 0;
        check("fullscreen: a client's window (no close, no grab, no glass) filling the screen from its dma-buf is the one shown straight",
              compositor_direct_candidate(&frame, rows, 1) == 0);
        compositor.glassy[0] = 1;
        check("fullscreen: and a glass window is not", compositor_direct_candidate(&frame, rows, 1) == -1);
        compositor.glassy[0] = 0;

        // The framebuffer the card shows is kept past the client's next buffer, and removed once the card is off it.
        graves = compositor_wl_grave_count;
        compositor.on_screen = compositor_lent_fb(0), compositor.flip_pending = 0;
        compositor_wl_lent_drop_fb(0);
        check("fullscreen: a client's framebuffer on the screen is not removed when its buffer is replaced", compositor_wl_grave_count == graves + 1);
        compositor_wl_graves_sweep();
        check("fullscreen: nor while it is still on the screen", compositor_wl_grave_count == graves + 1);
        compositor.on_screen = 77, compositor.flip_pending = 1;
        compositor_wl_graves_sweep();
        check("fullscreen: nor while a flip is on its way (the card scans the one before it until it lands)", compositor_wl_grave_count == graves + 1);
        compositor.flip_pending = 0;
        compositor_wl_graves_sweep();
        check("fullscreen: and is removed once the card is off it", compositor_wl_grave_count == graves);
        compositor.on_screen = 0;
        memory_zero(&compositor_wl.lent[0], sizeof compositor_wl.lent[0]);

        // An X window mapped over its whole screen is fullscreen there; a transient one or a popup is not.
        memory_zero(&w, sizeof w);
        w.x = 0, w.y = 0, w.width = 1920, w.height = 1080, w.row = -1;
        check("fullscreen: an X window mapped over the whole screen covers it (fullscreen without asking)", compositor_xwm_covers(&w) == 0);
        w.width = 1919;
        check("fullscreen: one a pixel narrower does not", compositor_xwm_covers(&w) == -1);
        w.width = 1920, w.transient = 0x77u;
        check("fullscreen: nor a transient one", compositor_xwm_covers(&w) == -1);
        w.transient = 0, w.state = COMPOSITOR_XWM_OVERRIDE;
        check("fullscreen: but an override-redirect one over the whole screen is (Proton's fullscreen windows: 832723dd0)", compositor_xwm_covers(&w) == 0);

        memory_copy(compositor.driver, kept_driver, sizeof kept_driver);
        compositor_popup_parent[0] = kept_parent[0], compositor_popup_parent[1] = kept_parent[1];
        memory_copy(&compositor_xwm, kept_xwm, sizeof(compositor_xwm));
        memory_copy(&compositor, kept_comp, sizeof(compositor));
}

/*
        Variable refresh and tearing: when the screen is set to variable refresh (kms.c's compositor_vrr_wants: the policy, a screen that can,
        one screen, every frame a flip, and for games a window shown straight), when a present is due (present.c's compositor_present_at: at
        once for a window that allows tearing, by the rule otherwise, and at once by the rule for an idle screen, which a screen at variable
        refresh then shows at once), what an asynchronous flip's event leaves of the vblank the presents are paced by (compositor_card_events),
        and the pointer's kick (compositor_vrr_moved, compositor_vrr_kick).
*/
static void vrr_rows(void)
{
        {
                b32 rule = true;

                for (unsigned int policy = 0; policy < 4; policy++)
                        for (int capable = 0; capable < 2; capable++)
                                for (unsigned int outputs = 0; outputs < 3; outputs++)
                                        for (int flipping = 0; flipping < 2; flipping++)
                                                for (int direct = 0; direct < 2; direct++)
                                                {
                                                        int want = capable && outputs == 1 && flipping && (policy == 0 || (policy == 1 && direct));

                                                        rule &= compositor_vrr_wants(policy, capable, outputs, flipping, direct) == want;
                                                }
                check("vrr: on wherever the screen can, the screens are one and every frame is a flip; games only with a window shown straight; off never",
                      rule);
                check("vrr: a screen that cannot is never set, whatever the policy and the frames", !compositor_vrr_wants(0, 0, 1, 1, 1));
                check("vrr: nor two screens, whose one scanout spans their crtcs and flips on one", !compositor_vrr_wants(0, 1, 2, 1, 1));
                check("vrr: nor frames composed into the buffer on the screen (a window on a plane, a turned screen, DIRTYFB): no flip ends the wait",
                      !compositor_vrr_wants(0, 1, 1, 0, 0) && !compositor_vrr_wants(1, 1, 1, 0, 1));
                check("vrr: games is on for a window shown straight and off for the desktop", compositor_vrr_wants(1, 1, 1, 1, 1) && !compositor_vrr_wants(1, 1, 1, 1, 0));
        }
        {
                unsigned int steady = 0, early = 0;
                int on = 0;

                for (unsigned int present = 1; present < COMPOSITOR_VRR_STEADY; present++)
                        early += (unsigned int)compositor_vrr_step(1, on, &steady);
                on = compositor_vrr_step(1, on, &steady);
                check("vrr: set on only once COMPOSITOR_VRR_STEADY presents in a row wanted it, not before", early == 0 && on == 1);
                check("vrr: and kept on while wanted", compositor_vrr_step(1, on, &steady) == 1 && steady == COMPOSITOR_VRR_STEADY);
                on = compositor_vrr_step(0, on, &steady);
                check("vrr: set off at the first present that does not want it (a frame with no flip waits for the least rate)", on == 0 && steady == 0);
                check("vrr: and on again only after another steady run", compositor_vrr_step(1, on, &steady) == 0 && steady == 1);
        }
        {
                positive period = 6944444ull, v = 9000000000ull, lead = 3000000ull;
                positive was_presented = compositor.presented, was_period = compositor.period, was_when = compositor.flip_when;

                compositor.presented = v - period + 100000ull, compositor.period = period, compositor.flip_when = v;
                check("tearing: a present for a window that allows tearing is due at once, whatever the vblank and the lead",
                      compositor_present_at(1, 0, v + 50000ull) == v + 50000ull && compositor_present_at(1, lead, v + 1) == v + 1);
                check("tearing: every other present keeps the rule, the lead before the vblank after the last",
                      compositor_present_at(0, lead, v + 50000ull) == v + period - lead &&
                          compositor_present_at(0, lead, v + 50000ull) == compositor_present_due(compositor.presented, period, v, lead, v + 50000ull));
                compositor.presented = v - 10 * period, compositor.flip_when = v - 9 * period;
                check("vrr: a present for an idle screen is due at once by the same rule (a screen at variable refresh then shows it at once)",
                      compositor_present_at(0, lead, v) == v);
                compositor.presented = was_presented, compositor.period = was_period, compositor.flip_when = was_when;
        }
        {
                // struct drm_event_vblank: type 2, length 32, the user data, tv_sec, tv_usec, the sequence and the crtc.
                static struct windows *vrr_table_scratch;
                struct windows *const vrr_table_at = windows_scratch_table(&vrr_table_scratch);
                struct windows *kept = compositor.table;
                struct compositor_rows held = compositor.held_rows, dropping = compositor.dropping;
                positive serial = compositor.flip_serial, when = compositor.flip_when, vblank = 9000000000ull;
                int pending = compositor.flip_pending, stamped = compositor.flip_stamped, tore = compositor.flip_tore;
                unsigned int sequence = compositor.flip_seq;
                p8 event[32];
                unsigned int type = COMPOSITOR_EVENT_FLIP_COMPLETE, length = 32, sec = 9, usec = 3000, seq = 77;
                positive mine = 41;

                compositor.table = vrr_table_at, compositor.held_rows = compositor.dropping = rows_from(0);
                memory_zero(event, sizeof(event));
                memory_copy(event, &type, 4), memory_copy(event + 4, &length, 4), memory_copy(event + 8, &mine, 8);
                memory_copy(event + 16, &sec, 4), memory_copy(event + 20, &usec, 4), memory_copy(event + 24, &seq, 4);
                compositor.flip_serial = mine, compositor.flip_pending = 1, compositor.flip_async = 1, compositor.flip_when = vblank;
                check("tearing: an asynchronous flip's event lets go of the flip", compositor_card_events(event, 32) == 1 && compositor.flip_pending == 0);
                check("tearing: and leaves the vblank the presents are paced by, its stamp no vblank, the flip said shown by the clock",
                      compositor.flip_when == vblank && compositor.flip_tore == 1 && compositor.flip_async == 0 && compositor.flip_stamped == 0);
                compositor.flip_pending = 1, compositor.flip_async = 0;
                check("tearing: a flip at the vblank, after it, is paced by its stamp again",
                      compositor_card_events(event, 32) == 1 && compositor.flip_when == 9000000000ull + 3000000ull && compositor.flip_tore == 0 &&
                          compositor.flip_seq == 77);
                compositor.table = kept, compositor.held_rows = held, compositor.dropping = dropping, compositor.flip_serial = serial;
                compositor.flip_when = when, compositor.flip_pending = pending, compositor.flip_stamped = stamped, compositor.flip_tore = tore;
                compositor.flip_seq = sequence, compositor.flip_async = 0;
        }
        {
                int on = compositor_vrr.on, kick = compositor.vrr_kick, pending = compositor.flip_pending;

                compositor_vrr.on = 0, compositor.vrr_kick = 0;
                compositor_vrr_moved();
                check("vrr: off, a move of the pointer kicks nothing", compositor.vrr_kick == 0);
                compositor_vrr.on = 1;
                compositor_vrr_moved();
                check("vrr: on, a move of the pointer on its plane asks for a kick", compositor.vrr_kick == 1);
                compositor.flip_pending = 1;
                compositor_vrr_kick();
                check("vrr: with a flip in flight the kick waits for its landing, which carries the pointer or kicks then", compositor.vrr_kick == 1);
                compositor_vrr.on = 0;
                compositor_vrr_kick();
                check("vrr: set off, a kick that waited is dropped", compositor.vrr_kick == 0);
                compositor_vrr.on = on, compositor.vrr_kick = kick, compositor.flip_pending = pending;
        }
}

// EWMH/XDG grab protocols: maximized vs fullscreen row_facts, immediate float past button slop on client move, and XWM _NET_SUPPORTED / _NET_WM_MOVERESIZE.
static void grab_protocol_rows(void)
{
        static struct windows *tbl_scratch;
        struct windows *const tbl_at = windows_scratch_table(&tbl_scratch);
        static unsigned char kept_comp[sizeof(compositor)], kept_xwm[sizeof(compositor_xwm)];
        struct wayland_facts facts;
        struct compositor_xwm_window *xw;
        struct xwm_event ev;
        unsigned long off = 0;
        unsigned int wm_win = 0, root_check_win = 0;
        int saw_check_on_wm = 0, saw_name_on_wm = 0, saw_check_on_root = 0, saw_supported = 0, saw_moveresize = 0, saw_active = 0, saw_max_v = 0;

        memory_copy(kept_comp, &compositor, sizeof(compositor));
        memory_copy(kept_xwm, &compositor_xwm, sizeof(compositor_xwm));
        compositor_zero();
        memory_zero(&compositor_xwm, sizeof(compositor_xwm));
        windows_scratch_zero(tbl_at);

        compositor.card = -1;
        compositor.table = tbl_at;
        compositor.width = 1920, compositor.height = 1080, compositor.scale = 100;
        compositor.drag = COMPOSITOR_NONE, compositor.resize = COMPOSITOR_NONE;
        compositor.shared_row = COMPOSITOR_NONE, compositor.focus = COMPOSITOR_NONE, compositor.grab = COMPOSITOR_NONE;
        compositor.showing[0] = 1;
        compositor.stacked = 1, compositor.stack[0] = 0;
        compositor.x[0] = 0, compositor.y[0] = 0, compositor.w[0] = 1920, compositor.h[0] = 1080;
        compositor.arranged[0] = COMPOSITOR_MAXIMIZED, compositor.fullscreen[0] = 0;
        tbl_at->state[0] = WINDOW_NO_GRAB;

        check("grab facts: COMPOSITOR_MAXIMIZED without fullscreen reports MAXIMIZED, not FULLSCREEN",
              compositor_wl_row_facts(0, 0, &facts) == 0 &&
                  (facts.states & WAYLAND_STATE_MAXIMIZED) && !(facts.states & WAYLAND_STATE_FULLSCREEN));
        compositor.arranged[0] = COMPOSITOR_ROOM;
        check("grab facts: COMPOSITOR_ROOM reports MAXIMIZED, not FULLSCREEN",
              compositor_wl_row_facts(0, 0, &facts) == 0 &&
                  (facts.states & WAYLAND_STATE_MAXIMIZED) && !(facts.states & WAYLAND_STATE_FULLSCREEN));
        compositor_fullscreen_row(0);
        check("grab facts: compositor_fullscreen_row sets fullscreen state and WAYLAND_STATE_FULLSCREEN",
              compositor.fullscreen[0] == 1 && compositor_wl_row_facts(0, 0, &facts) == 0 &&
                  (facts.states & WAYLAND_STATE_FULLSCREEN));

        // Client drag initiation after pointer already moved past button slop floats immediately without a second slop.
        compositor.fullscreen[0] = 0, compositor.arranged[0] = COMPOSITOR_MAXIMIZED;
        compositor.x[0] = 0, compositor.y[0] = 0, compositor.w[0] = 1920, compositor.h[0] = 1080;
        compositor.pointer_x = 100, compositor.pointer_y = 200;
        compositor_button(COMPOSITOR_BTN_LEFT, 1);
        compositor.pointer_x = 120, compositor.pointer_y = 200;
        compositor_drag_begin_client(0);
        check("client drag: floats immediately when pointer already moved past press slop",
              compositor.drag == 0 && compositor.arranged[0] == COMPOSITOR_FLOATING);
        compositor_drag_stop(0);
        check("client drag: compositor_drag_stop ends drag cleanly while keeping floating state",
              compositor.drag == COMPOSITOR_NONE && compositor.resize == COMPOSITOR_NONE &&
                  compositor.arranged[0] == COMPOSITOR_FLOATING);
        compositor_button(COMPOSITOR_BTN_LEFT, 0);

        // If the pointer has not yet left slop, client drag arms without prematurely unmaximizing.
        compositor.arranged[0] = COMPOSITOR_MAXIMIZED, compositor.fullscreen[0] = 0;
        compositor.x[0] = 0, compositor.y[0] = 0, compositor.w[0] = 1920, compositor.h[0] = 1080;
        compositor.pointer_x = 100, compositor.pointer_y = 200;
        compositor_button(COMPOSITOR_BTN_LEFT, 1);
        compositor.pointer_x = 101, compositor.pointer_y = 200;
        compositor_drag_begin_client(0);
        check("client drag: stays arranged until slop threshold is crossed",
              compositor.drag == 0 && compositor.arranged[0] == COMPOSITOR_MAXIMIZED);
        compositor_drag_stop(0);
        compositor_button(COMPOSITOR_BTN_LEFT, 0);

        // compositor_xwm_claim emits _NET_SUPPORTING_WM_CHECK, _NET_WM_NAME, and _NET_SUPPORTED including _NET_WM_MOVERESIZE.
        compositor_xwm_listen_fd = -1, compositor_xwm.fd = -1, compositor_xwm.poll_at = -1, compositor_xwm.focus_row = -2;
        compositor_xwm.root = 0x100u, compositor_xwm.resource_base = 0x2000u, compositor_xwm.resource_mask = 0x1fffffu;
        compositor_xwm.seq = 1;
        for (unsigned int i = 0; i < COMPOSITOR_XWM_ATOMS; i++)
                compositor_xwm.atoms[i] = 1000u + i;
        compositor_xwm_claim();
        while (off + 4 <= (unsigned long)compositor_xwm.out_len)
        {
                unsigned char op = compositor_xwm.out[off];
                unsigned long len = (unsigned long)xwm_get16(compositor_xwm.out + off + 2) * 4u;

                if (len < 4 || off + len > (unsigned long)compositor_xwm.out_len)
                        break;
                if (op == XWM_OP_CHANGE_PROPERTY && len >= 24)
                {
                        unsigned int win = xwm_get32(compositor_xwm.out + off + 4);
                        unsigned int prop = xwm_get32(compositor_xwm.out + off + 8);
                        unsigned int type = xwm_get32(compositor_xwm.out + off + 12);
                        unsigned int fmt = compositor_xwm.out[off + 16];
                        unsigned int count = xwm_get32(compositor_xwm.out + off + 20);

                        if (win == compositor_xwm.root && prop == compositor_xwm.atoms[COMPOSITOR_XWM_A_NET_CHECK] && fmt == 32 && count == 1 && off + 28 <= (unsigned long)compositor_xwm.out_len)
                        {
                                root_check_win = xwm_get32(compositor_xwm.out + off + 24);
                                saw_check_on_root = 1;
                        }
                        else if (win == compositor_xwm.root && prop == compositor_xwm.atoms[COMPOSITOR_XWM_A_NET_SUPPORTED] && fmt == 32)
                        {
                                saw_supported = 1;
                                for (unsigned int j = 0; j < count && off + 24 + (j + 1) * 4u <= (unsigned long)compositor_xwm.out_len; j++)
                                {
                                        unsigned int a = xwm_get32(compositor_xwm.out + off + 24 + j * 4u);

                                        if (a == compositor_xwm.atoms[COMPOSITOR_XWM_A_NET_MOVERESIZE])
                                                saw_moveresize = 1;
                                        else if (a == compositor_xwm.atoms[COMPOSITOR_XWM_A_NET_ACTIVE])
                                                saw_active = 1;
                                        else if (a == compositor_xwm.atoms[COMPOSITOR_XWM_A_MAX_VERT])
                                                saw_max_v = 1;
                                }
                        }
                        else if (win != compositor_xwm.root && prop == compositor_xwm.atoms[COMPOSITOR_XWM_A_NET_CHECK] && fmt == 32 && count == 1 &&
                                 type == COMPOSITOR_XWM_XA_WINDOW && off + 28 <= (unsigned long)compositor_xwm.out_len)
                        {
                                wm_win = xwm_get32(compositor_xwm.out + off + 24);
                                saw_check_on_wm = (wm_win == win);
                        }
                        else if (win != compositor_xwm.root && prop == compositor_xwm.atoms[COMPOSITOR_XWM_A_NET_NAME] && fmt == 8 && count == 9 &&
                                 off + 33 <= (unsigned long)compositor_xwm.out_len &&
                                 memory_compare(compositor_xwm.out + off + 24, "moonwater", 9) == 0)
                                saw_name_on_wm = 1;
                }
                off += len;
        }
        check("xwm claim: sets _NET_SUPPORTING_WM_CHECK on root and wm window, _NET_WM_NAME, and _NET_SUPPORTED with _NET_WM_MOVERESIZE",
              saw_check_on_root && saw_check_on_wm && wm_win != 0 && root_check_win == wm_win &&
                  saw_name_on_wm && saw_supported && saw_moveresize && saw_active && saw_max_v);

        // _NET_WM_MOVERESIZE ClientMessages start move/resize only when left button is held on the window, and CANCEL stops cleanly.
        xw = compositor_xwm_find(0x880001u, 1);
        if (xw)
                xw->row = 0;
        xwm_event_clear(&ev);
        ev.kind = XWM_EV_CLIENT_MESSAGE, ev.window = 0x880001u;
        ev.atom = compositor_xwm.atoms[COMPOSITOR_XWM_A_NET_MOVERESIZE], ev.format = XWM_FORMAT_32;
        ev.data[2] = COMPOSITOR_XWM_MOVERESIZE_MOVE;
        compositor_xwm_event(&ev);
        check("xwm moveresize: ignored when left mouse button is not held on the window",
              xw && compositor.drag == COMPOSITOR_NONE && compositor.resize == COMPOSITOR_NONE);

        compositor.arranged[0] = COMPOSITOR_FLOATING;
        compositor.x[0] = 50, compositor.y[0] = 50, compositor.w[0] = 400, compositor.h[0] = 300;
        compositor.pointer_x = 100, compositor.pointer_y = 200;
        compositor_button(COMPOSITOR_BTN_LEFT, 1);
        compositor.pointer_x = 120, compositor.pointer_y = 200;
        compositor_xwm_event(&ev);
        check("xwm moveresize: MOVE starts compositor drag while left button is held",
              compositor.drag == 0 && compositor.resize == COMPOSITOR_NONE);
        ev.data[2] = COMPOSITOR_XWM_MOVERESIZE_CANCEL;
        compositor_xwm_event(&ev);
        check("xwm moveresize: CANCEL stops compositor drag",
              compositor.drag == COMPOSITOR_NONE && compositor.resize == COMPOSITOR_NONE);

        ev.data[2] = 4; // _NET_WM_MOVERESIZE_SIZE_BOTTOMRIGHT
        compositor_xwm_event(&ev);
        check("xwm moveresize: SIZE_BOTTOMRIGHT starts compositor bottom-right edge resize",
              compositor.resize == 0 && compositor.edges[0] == (COMPOSITOR_EDGE_RIGHT | COMPOSITOR_EDGE_BOTTOM));
        ev.data[2] = COMPOSITOR_XWM_MOVERESIZE_CANCEL;
        compositor_xwm_event(&ev);
        check("xwm moveresize: CANCEL stops compositor edge resize",
              compositor.resize == COMPOSITOR_NONE && compositor.drag == COMPOSITOR_NONE);
        compositor_button(COMPOSITOR_BTN_LEFT, 0);

        // EWMH _NET_WM_STATE maximization, WM_CHANGE_STATE iconic minimize, _NET_ACTIVE_WINDOW restore+focus, and _NET_CLOSE_WINDOW.
        xwm_event_clear(&ev);
        ev.kind = XWM_EV_CLIENT_MESSAGE, ev.window = 0x880001u, ev.format = XWM_FORMAT_32;
        ev.atom = compositor_xwm.atoms[COMPOSITOR_XWM_A_NET_STATE];
        ev.data[0] = COMPOSITOR_XWM_NET_ADD;
        ev.data[1] = compositor_xwm.atoms[COMPOSITOR_XWM_A_MAX_VERT];
        ev.data[2] = compositor_xwm.atoms[COMPOSITOR_XWM_A_MAX_HORZ];
        compositor_xwm_event(&ev);
        check("xwm ewmh: _NET_WM_STATE MAXIMIZED_VERT/HORZ ADD arranges row as room and records COMPOSITOR_XWM_MAXIMIZED",
              xw && (xw->state & COMPOSITOR_XWM_MAXIMIZED) && compositor.arranged[0] == COMPOSITOR_ROOM);
        ev.data[0] = COMPOSITOR_XWM_NET_REMOVE;
        compositor_xwm_event(&ev);
        check("xwm ewmh: _NET_WM_STATE MAXIMIZED_VERT/HORZ REMOVE floats row and clears COMPOSITOR_XWM_MAXIMIZED",
              xw && !(xw->state & COMPOSITOR_XWM_MAXIMIZED) && compositor.arranged[0] == COMPOSITOR_FLOATING);

        ev.atom = compositor_xwm.atoms[COMPOSITOR_XWM_A_CHANGE_STATE];
        ev.data[0] = COMPOSITOR_XWM_STATE_ICONIC;
        compositor_xwm_event(&ev);
        check("xwm icccm: WM_CHANGE_STATE IconicState minimizes window row",
              (compositor.state[0] & WINDOW_MINIMIZED) != 0);

        ev.atom = compositor_xwm.atoms[COMPOSITOR_XWM_A_NET_ACTIVE];
        ev.data[0] = 1;
        compositor_xwm_event(&ev);
        check("xwm ewmh: _NET_ACTIVE_WINDOW unminimizes and focuses window row",
              !(compositor.state[0] & WINDOW_MINIMIZED) && compositor.focus == 0);

        compositor.state[0] = 0, compositor.close_asks[0] = 0;
        ev.atom = compositor_xwm.atoms[COMPOSITOR_XWM_A_NET_CLOSE];
        compositor_xwm_event(&ev);
        check("xwm ewmh: _NET_CLOSE_WINDOW requests window close on row",
              (compositor.state[0] & WINDOW_CLOSING) && compositor.close_asks[0] == 1);

        // ICCCM WM_HINTS InputHint=0 and WM_NORMAL_HINTS PMinSize/PMaxSize.
        {
                unsigned char reply[32 + 36];
                struct compositor_xwm_pending pend = {1, 0x880001u, COMPOSITOR_XWM_PROP_WM_HINTS};
                unsigned int cw = 100, ch = 900;

                memory_zero(reply, sizeof(reply));
                reply[0] = 1, reply[1] = XWM_FORMAT_32;
                xwm_put32(reply + 4, 2);
                xwm_put32(reply + 16, 2);
                xwm_put32(reply + 32, 1u); // InputHint flag
                xwm_put32(reply + 36, 0u); // input = False
                compositor_xwm_property(reply, 32 + 8, &pend);
                check("xwm icccm: WM_HINTS InputHint=0 sets no_input", xw && xw->no_input == 1);

                memory_zero(reply, sizeof(reply));
                reply[0] = 1, reply[1] = XWM_FORMAT_32;
                xwm_put32(reply + 4, 9);
                xwm_put32(reply + 16, 9);
                xwm_put32(reply + 32, 16u | 32u); // PMinSize | PMaxSize
                xwm_put32(reply + 32 + 20, 320u); // min_width
                xwm_put32(reply + 32 + 24, 240u); // min_height
                xwm_put32(reply + 32 + 28, 800u); // max_width
                xwm_put32(reply + 32 + 32, 600u); // max_height
                pend.what = COMPOSITOR_XWM_PROP_NORMAL_HINTS;
                compositor_xwm_property(reply, 32 + 36, &pend);
                compositor_xwm_clamp_size(xw, &cw, &ch);
                check("xwm icccm: WM_NORMAL_HINTS clamps configure size to min/max", cw == 320u && ch == 600u);
        }

        compositor_xwm_forget(0x880001u);

        memory_copy(&compositor_xwm, kept_xwm, sizeof(compositor_xwm));
        memory_copy(&compositor, kept_comp, sizeof(compositor));
}

static void gpu_engine_and_render_regressions(void)
{
        static unsigned int kept_gpu_buf[(sizeof(gpu) + 3) / 4];
        static unsigned int fake_vram[1 << 16];
        unsigned char *fill_a = 0, *fill_b = 0;
        unsigned int *kept_gpu = (unsigned int *)kept_gpu_buf;
        static struct compositor_gscene kept_gscene_val;
        struct compositor_gscene *kept_gscene = &kept_gscene_val;
        unsigned int kept_size = gpu.size, kept_wide = gpu_wide, kept_gfx103 = gpu_gfx103;
        unsigned int kept_lost = gpu_lost, kept_pending = gpu.pending;
        int kept_fd = gpu.fd, kept_card = compositor.card;
        unsigned int *kept_at = gpu.at;
        int kept_up = compositor_gpu_state.up, kept_list_mode = compositor.table ? compositor.table->list_mode : 0;

        /* 1. RDNA2 gpu_gfx103 family acceptance without wide flag */
        gpu_wide = 0, gpu_gfx103 = 0;
        check("gpu family: 143 refused when neither wide nor gfx103", !gpu_family_ok(143));
        gpu_gfx103 = 1;
        check("gpu family: 143 accepted when gpu_gfx103 is set", gpu_family_ok(143));
        gpu_gfx103 = 0;

        /* 2. gpu_register_memfd / gpu_unregister_memfd & gpu_drop_unmapped */
        {
                unsigned char dummy[8192];

                check("gpu_register_memfd registers page range and unregisters cleanly",
                      gpu_register_memfd(42, dummy, sizeof(dummy), 0));
                gpu_unregister_memfd(dummy);
        }
        gpu_map_count = 2;
        gpu_maps[0].from = 0, gpu_maps[0].to = 0, gpu_maps[0].va = 0x50000000ull, gpu_maps[0].bytes = 4096, gpu_maps[0].handle = 1;
        {
                // The second map's pages are there (a page of the check's own): gpu_drop_unmapped asks the kernel whether a map's range is mapped (msync), and an
                // address nothing is mapped at, which this row gave it, is a map it is right to drop.
                static unsigned char live[8192] __attribute__((aligned(4096)));

                gpu_maps[1].from = (positive)live, gpu_maps[1].to = (positive)live + 4096;
        }
        gpu_maps[1].va = 0x50010000ull, gpu_maps[1].bytes = 4096, gpu_maps[1].handle = 2;
        gpu.fd = GPU_KERNEL_FD, gpu_kernel.on = 1, gpu_kernel.buffer[1] = 10, gpu_kernel.mapped[1] = 1, gpu_kernel.buffer[2] = 11, gpu_kernel.mapped[2] = 1;
        gpu_drop_unmapped();
        check("gpu_drop_unmapped drops only forgotten (from==0&&to==0) maps",
              gpu_map_count == 1 && gpu_maps[0].handle == 2);
        gpu_map_count = 0, gpu_kernel.buffer[1] = 0, gpu_kernel.buffer[2] = 0, gpu_kernel.on = 0;

        /* 3. gpu_scene unaligned partial/hand refusal & GPU_END_OVERFLOW & gpu_finish */
        gpu.fd = GPU_KERNEL_FD, gpu.at = fake_vram, gpu.size = sizeof(fake_vram), gpu.pending = 0, gpu_lost = 0;
        fake_vram[0] = 0u; /* op 0 fill */
        check("gpu_scene refuses partial damage on unaligned width without starting ring",
              !gpu_scene(0, 1, 4096, 8192, 65, 65, 8, 1, 0, 64, 8));
        fake_vram[0] = 3u; /* op > 2 hand op */
        check("gpu_scene refuses hand op on unaligned width cleanly",
              !gpu_scene(0, 1, 4096, 8192, 65, 65, 8, 0, 0, 65, 8));
        fake_vram[0] = 0u;
        check("gpu_scene sets GPU_END_OVERFLOW when bins_at exceeds gpu.size",
              !gpu_scene(0, 1, sizeof(fake_vram), 8192, 64, 64, 8, 0, 0, 64, 8) && gpu_end == GPU_END_OVERFLOW);

        gpu_kernel.on = 1, gpu_kernel.no_context = 1;
        gpu.pending = 99, gpu_lost = 0, gpu_fake_errno = 1;
        check("gpu_finish returns false and marks gpu_lost when wait fails", !gpu_finish() && gpu_lost && gpu.pending == 99);
        gpu_kernel.on = 0, gpu_kernel.no_context = 0, gpu_fake_errno = 0;

        /* 4. compositor_over_pixel premultiplied ARGB8888 blend */
        check("compositor_over_pixel opaque alpha=255 replaces dst",
              compositor_over_pixel(0xff112233u, 0xff445566u) == 0xff445566u);
        check("compositor_over_pixel transparent alpha=0 keeps dst",
              compositor_over_pixel(0xff112233u, 0x00000000u) == 0xff112233u);
        {
                unsigned int blended = compositor_over_pixel(0xff0000ffu, 0x80008000u);

                check("compositor_over_pixel half alpha composites premultiplied channel cleanly",
                      (blended & 0xffu) > 100u && (blended & 0xffu) < 150u && ((blended >> 8) & 0xffu) >= 120u);
        }

        /* 5. compositor_gpu_slot_tag distinguishes tags & compositor_gpu_rotate rejects unaligned width */
        memory_zero(&compositor_gscene, sizeof(compositor_gscene));
        compositor_gscene.tables = (unsigned char *)fake_vram;
        compositor_gscene.tables_size = 4096;
        compositor_gscene.tables_at = 1024;
        {
                unsigned int a1 = compositor_gpu_slot_tag(16, 2, 20, 128, 0x1111UL, 64, &fill_a);
                unsigned int a2 = compositor_gpu_slot_tag(16, 2, 20, 128, 0x2222UL, 64, &fill_b);

                check("compositor_gpu_slot_tag allocates distinct slots for different chip tags",
                      a1 != 0 && a2 != 0 && a1 != a2 && compositor_gscene.kept == 2);
        }
        compositor_gpu_state.up = 1, compositor_gpu_state.out = 0x100000ull, compositor.card = GPU_KERNEL_FD;
        {
                int screen[4] = {0, 0, 100, 101}; /* 90-deg rotate gives pw=101 (non-mod-4) */

                compositor_turns[0].pitch = 512;
                check("compositor_gpu_rotate rejects non-mod-4 rotated width cleanly",
                      !compositor_gpu_rotate(0, screen, 90, 0, 0, 100, 101));
                screen[2] = 100, screen[3] = 100, compositor_turns[0].pitch = 404; /* non-mod-16 pitch */
                check("compositor_gpu_rotate rejects non-mod-16 turn pitch cleanly",
                      !compositor_gpu_rotate(0, screen, 90, 0, 0, 100, 100));
        }

        /* 6. compositor_direct_candidate blocks direct scanout when table->list_mode is non-zero */
        {
                static struct windows *fake_table_scratch;
                struct windows *const fake_table_at = windows_scratch_fresh(&fake_table_scratch); // zero, as the table on the stack was
                struct compositor_frame f = {0};
                unsigned int row = 0;

                fake_table_at->list_mode = 1;
                compositor.table = fake_table_at;
                compositor.table_memfd = 1;
                compositor.card = 1;
                compositor.width = 100, compositor.height = 100;
                compositor.w[0] = 100, compositor.h[0] = 100, compositor.picture_w[0] = 100, compositor.picture_h[0] = 100;
                check("compositor_direct_candidate rejects direct scanout when list_mode is 1",
                      compositor_direct_candidate(&f, &row, 1) == -1);
                compositor.table = 0, compositor.table_memfd = 0;
        }
        (void)kept_list_mode;

        memory_copy(&gpu, kept_gpu, sizeof(gpu));
        memory_copy(&compositor_gscene, kept_gscene, sizeof(compositor_gscene));
        gpu.size = kept_size, gpu_wide = kept_wide, gpu_gfx103 = kept_gfx103, gpu_lost = kept_lost, gpu.pending = kept_pending;
        gpu.fd = kept_fd, gpu.at = kept_at, compositor.card = kept_card, compositor_gpu_state.up = kept_up;
}

/*
        The just-in-time frame callback (Reflex-style) for a fullscreen game shown straight: armed at each flip the render estimate and a margin
        before the next vblank's slot. Its sample was the present's start less the callback's time, and the present's start is pinned to that
        slot, so every sample was the render plus the margin: the fast attack took it, the estimate climbed 0.6 ms a frame, and about nine
        frames in the budget passed the period and the JIT turned itself off for good (the box's game sessions ran with it off). The sample is
        the callback to the row's commit (compositor_wl_jit_ready, stamped as wayland_host takes the picture).
*/
static void jit_ready_rows(void)
{
        const int row = 2;
        const positive period = 6944444ull, render = 2000000ull;
        positive vblank = 9000000000ull, slot, target = 0;
        unsigned int armed = 0, frame;
        b32 stamped = 1, sent_ok = 1;
        positive kept_period = compositor.period;
        int kept_card = compositor.card, kept_plane = compositor.plane_row, kept_on = compositor_card.on, kept_direct = compositor.direct_row;
        int kept_flip_row = compositor.flip_row, kept_ready = compositor.spaces.ready, kept_started = compositor_wl.started;
        unsigned int kept_state = compositor.state[row];
        unsigned int kept_showing = compositor.showing[row];

        compositor.period = period, compositor.card = -1, compositor.plane_row = -1, compositor_card.on = 0;
        compositor.direct_row = row, compositor.flip_row = row, compositor.showing[row] = 1, compositor.state[row] = 0;
        compositor.spaces.ready = 0, compositor_wl.started = 0;
        compositor_wl_game_render_ns = 0, compositor_wl_jit_sent(0), compositor_wl_frame_at = 0;
        check("jit: the game's row is the JIT row", compositor_wl_jit_row() == row);
        // The first flip has no estimate: the callback goes at the flip.
        compositor_wl_frame_owed = 1;
        compositor_wl_jit_owed(vblank, vblank + 30000);
        sent_ok &= !compositor_wl_frame_at && compositor_wl_frame_sent_at == vblank + 30000;
        for (frame = 0; frame < 40; frame++)
        {
                positive sent = compositor_wl_frame_sent_at;

                // The game renders for 2 ms and commits; wayland_host stamps the commit (the clock's now), and the timeline's time is put in its place.
                compositor_wl_jit_ready(row);
                stamped &= compositor_wl_game_ready_at != 0;
                compositor_wl_game_ready_at = sent + render;
                compositor_wl_jit_ready(row);
                stamped &= compositor_wl_game_ready_at == sent + render; // the first commit since the callback is the one that counts
                // The present for the next vblank starts at its slot, the lead (none here: no card) before it, and shows the frame.
        slot = vblank + period;
                compositor_wl_jit_measure(slot);
                vblank += period;
                compositor_wl_frame_owed = 1;
                compositor_wl_jit_owed(vblank, vblank + 30000);
                if (compositor_wl_frame_at)
                {
                        armed++, target = compositor_wl_frame_at;
                        compositor_wl_frame_at = 0, compositor_wl_jit_sent(target); // the loop sends the callback at the time armed
                }
        }
        check("jit: each commit after the callback is stamped, the first one only", stamped && sent_ok);
        check("jit: a steady 2 ms render keeps the callback armed every frame, not off after about nine",
                    armed == 40 && target == vblank + period - (compositor_wl_game_render_ns + 600000ull));
        check("jit: the estimate is the render, not the render and the margin", compositor_wl_game_render_ns >= render && compositor_wl_game_render_ns < render + 10000);
        // A present before the game's commit (another row's damage) samples nothing, and the commit after it is sampled by the present that shows it.
        {
                positive sent = compositor_wl_frame_sent_at;

                compositor_wl_jit_measure(sent + 1000000);
                compositor_wl_jit_ready(row);
                compositor_wl_game_ready_at = sent + 5000000;
                compositor_wl_jit_measure(sent + 4000000);
                check("jit: a present begun before the commit does not take it", compositor_wl_game_ready_at == sent + 5000000 && compositor_wl_game_render_ns < render + 10000);
                compositor_wl_jit_measure(sent + 5500000);
                check("jit: a slow frame raises the estimate at once (it landed late)", compositor_wl_game_render_ns == 5000000ull);
                compositor_wl_jit_measure(sent + 6000000);
                check("jit: and a second present before the next callback samples it again not", compositor_wl_game_render_ns == 5000000ull);
        }
        // A render that fills the period turns the JIT off; light frames after it, sampled from the flip's callback, turn it on again.
        compositor_wl_game_render_ns = period - 400000ull;
        compositor_wl_frame_owed = 1;
        compositor_wl_jit_owed(vblank, vblank + 30000);
        {
                b32 off = !compositor_wl_frame_at && compositor_wl_frame_sent_at == vblank + 30000;

                for (frame = 0; frame < 40 && !compositor_wl_frame_at; frame++)
                {
                        positive sent = compositor_wl_frame_sent_at;

                        compositor_wl_jit_ready(row);
                        compositor_wl_game_ready_at = sent + render;
                        compositor_wl_jit_measure(vblank + period - 500000ull);
                        vblank += period;
                        compositor_wl_frame_owed = 1;
                        compositor_wl_jit_owed(vblank, vblank + 30000);
                }
                check("jit: a render that fills the period sends the callback at the flip, and light frames arm it again", off && compositor_wl_frame_at != 0);
        }
        compositor_wl_frame_at = 0, compositor_wl_game_render_ns = 0, compositor_wl_jit_sent(0);
        compositor.period = kept_period, compositor.card = kept_card, compositor.plane_row = kept_plane, compositor_card.on = kept_on;
        compositor.direct_row = kept_direct, compositor.flip_row = kept_flip_row, compositor.showing[row] = kept_showing, compositor.state[row] = kept_state;
        compositor.spaces.ready = kept_ready, compositor_wl.started = kept_started;
}

b32 main(void)
{
        compositor_rows_make(compositor_rows_in_use); // the compositor's rows, made before any check reads one
        gpu_imports_room(64);        compositor_qos_root = "/run/moonwater/check-no-cpus"; // no row's input hold writes the machine's own CPU files (they outlive the check)
                         // and the import table's first room, which the rows that set an import by hand write in
        enum { PH = 419, PP = 700, N = 64, ROUNDS = 40 };
        // The transitions run as designed in every check (ui_paced at 100): the durations the rows pin are the designed ones.
        ui_pace = UI_PACE_DESIGNED;

        gpu_fault_rows();
        gpu_bo_rows();
        gpu_vmfault_rows();
        gpu_flight_rows();
        gpu_import_rows();
        gpu_clocks_rows();
        gpu_engine_and_render_regressions();
        gpu_late_rows();
        gpu_async_rows();
        gpu_card_rows();
        gpu_scaled_pool_rows();
        gpu_lists_nowait_rows();
        gpu_own_rows();
        gpu_round_rows();
        gpu_bench_rows();
        flip_wait_rows();
        descriptor_rows();
        frame_dated_rows();
        present_due_rows();
        jit_ready_rows();
        vrr_rows();
        under_ready_rows();
        plate_wanted_rows();
        scanout_refused_rows();
        metric_present_rows();
        dirty_rows();
        close_ask_rows();
        xwm_socket_rows();
        xwm_serial_state_rows();
        xwm_atom_rows();
        xwm_drag_gate_rows();
        xwm_hints_rows();
        xwm_tray_rows();
        xwm_incr_cap_rows();
        display_off_rows();
        reader_residue_rows();
        game_pids_rows();
        qos_rows();
        input_core_rows();
        view_rows();
        reader_own_rows();
        clip_most_rows();
        asked_record_rows();
        grab_protocol_rows();
        fullscreen_rows();
        gpu_i915_rows();
        gpu_generation_rows();
        int PW = 696; // even rounds the kernel by hand (a width of a multiple of 4), odd rounds clang's (any width)
        positive pic_at = GPU_DATA, ops_at = pic_at + (positive)PP * PH * 4, bins_at = ops_at + (1 << 20);
        unsigned int address_to pic, address_to ops;
        struct ui ui;
        positive bad = 0;

        // The prerequisites (gpu.c's gpu_req): the fake fails each step in turn, and the start stops at that step with its error kept, every step before it
        // passed, and nothing of the card left open (the buffer, the context, the descriptor). The fake only fails a step: the steps before it are the
        // machine's own, so a step is asked only where the machine passes every step before it (a start with nothing faked says how far that is). On a
        // machine with no card that is the node's step alone, and the others are said NOT RUN with the step that stops them. The message of each step is there.
        {
                // The required steps: a fake that fails one stops the start there.
                static const enum gpu_req steps[] = {GPU_REQ_NODE, GPU_REQ_DRIVER, GPU_REQ_FAMILY, GPU_REQ_USERPTR, GPU_REQ_CONTEXT, GPU_REQ_RING, GPU_REQ_SELFTEST};
                static const unsigned int errors[] = {2, 0, 0, 19, 12, 62, 0};
                unsigned int own_first, own_error;
                int own_started = gpu_start(1 << 20);

                own_first = gpu_req.first, own_error = gpu_req.error; // the step this machine stops at by itself (0: it has a card that starts)
                if (own_started)
                        gpu_stop();
                check("gpu requirements: a start that stops by itself keeps the step it stopped at, and leaves nothing of the card open",
                      own_started ? own_first == 0 : own_first != 0 && gpu.fd == -1 && gpu.at == 0);
                for (unsigned int k = 0; k < sizeof(steps) / sizeof(steps[0]); k++)
                {
                        unsigned int want = (1u << steps[k]) - 2; // the steps before it, not its own bit (bit 0 is no step)
                        int started;

                        if (own_first && own_first < (unsigned int)steps[k])
                        {
                                string_format(log, "gpu requirements: a card failing at %s NOT RUN -- this machine stops before it, at %s (error %p)\n",
                                              windows_gpu_req_names[steps[k]], windows_gpu_req_names[own_first], (positive)own_error);
                                continue;
                        }
                        gpu_fake_step = (int)steps[k], gpu_fake_error = errors[k];
                        started = gpu_start(1 << 20);
                        gpu_fake_step = -1;
                        if (steps[k] == GPU_REQ_USERPTR && started)
                        {
                                check("gpu requirements: failing userptr with udmabuf+PRIME falls back to udmabuf and starts",
                                      gpu_userptr_gone && gpu_req.first == 0 &&
                                          (gpu_req.mask & (1u << GPU_REQ_USERPTR)) != 0 &&
                                          (gpu_req.mask & (1u << GPU_REQ_UDMABUF | 1u << GPU_REQ_PRIME)) == (1u << GPU_REQ_UDMABUF | 1u << GPU_REQ_PRIME));
                                gpu_stop();
                                continue;
                        }
                        check("gpu requirements: a card failing one step stops there, and that step is the first kept", !started && gpu_req.first == steps[k]);
                        check("gpu requirements: that step keeps its error", gpu_req.error == errors[k]);
                        check("gpu requirements: the steps before it passed, its own did not", (gpu_req.mask & (1u << steps[k])) == 0 && (gpu_req.mask & want) == want);
                        check("gpu requirements: nothing of the card is left open", gpu.fd == -1 && gpu.at == 0);
                }
                // The dma-buf route, once the card is up: a fake that fails one of its steps leaves the card up, the route kept with its error, its bits unset.
                {
                        static const enum gpu_req route[] = {GPU_REQ_UDMABUF, GPU_REQ_PRIME};
                        static const unsigned int errors[] = {2, 22};
                        // The PRIME step follows udmabuf: a user the device refuses (the box's dawn, EACCES; root opens it) has only the udmabuf row.
                        long probe = system_open_at(AT_FDCWD, (string_address) "/dev/udmabuf", FILE_READ_WRITE | O_CLOEXEC);
                        int openable = probe >= 0;

                        if (openable)
                                system_close((int)probe);

                        if (own_first)
                                string_format(log, "gpu requirements: a failed dma-buf step leaving the card up NOT RUN -- no card starts here to be left up (%s, error %p)\n",
                                              windows_gpu_req_names[own_first], (positive)own_error);
                        else if (!openable)
                                string_format(log, "gpu requirements: a failed PRIME step NOT RUN -- /dev/udmabuf does not open for this user\n");
                        for (unsigned int k = 0; !own_first && k < (openable ? 2u : 1u); k++)
                        {
                                int started;

                                gpu_fake_step = (int)route[k], gpu_fake_error = errors[k];
                                started = gpu_start(1 << 20);
                                gpu_fake_step = -1;
                                check("gpu requirements: a failed dma-buf step leaves the card up", started && gpu_req.first == 0 && gpu_req.route == route[k]);
                                check("gpu requirements: the route's step keeps its error and its bit stays unset", gpu_req.route_error == errors[k] && (gpu_req.mask & (1u << route[k])) == 0);
                                gpu_stop();
                        }
                }
                for (unsigned int code = 1; code < GPU_REQ_COUNT; code++)
                        check("gpu requirements: every step has its name and its fix in words", windows_gpu_req_names[code] && windows_gpu_req_names[code][0] && windows_gpu_req_fixes[code] && windows_gpu_req_fixes[code][0]);
                check("gpu requirements: the userptr fix names the kernel option", string_find((string_address)windows_gpu_req_fixes[GPU_REQ_USERPTR], (string_address)"CONFIG_DRM_AMDGPU_USERPTR") != 0);
        }
        // The rows of the shim's host that need no card (its state made by hand), before the card's.
        gpu_wc_rows();
        gpu_overlay_rows();
        gpu_layers_off_rows();
        gpu_scan_rows();
        gpu_plane_rows();
        if (!gpu_start((positive)PP * PH * 4 + (1 << 20) + (8 << 20) + (positive)7680 * 4320 * 4 + (16 << 20) + (24 << 20)))
        {
                // A card that is there and draws the self-test wrong fails here; no card at all is no failure, and no pass either: every row after this one
                // draws on the card and compares with the processor, and none of them ran.
                check("gpu: a card that is there passes the self-test", !gpu.broken);
                string_format(log, "gpu kit: the rows that draw on the card NOT RUN -- no card starts here: %s failed (error %p): %s\n", windows_gpu_req_names[gpu_req.first],
                              (positive)gpu_req.error, windows_gpu_req_fixes[gpu_req.first]);
                return test_report(null);
        }
        // What gpu_start's look at each render node stands on: the version call writes back the whole length of the driver's name and puts no 0 after the
        // bytes it copies, so its record and the name are made again for each node (a node after "i915" was read as "amdg", one after "virtio_gpu"
        // had ten bytes copied into eight).
        {
                char name[8] = {'x', 'x', 'x', 'x', 'x', 'x', 'x', 0};
                p64 ver[8] = {0, 0, 3, (p64)name};

                check("gpu: the version call answers the name's whole length and ends nothing it copies",
                      system_control(gpu.fd, 3u << 30 | 64u << 16 | 0x6400u, ver) == 0 && ver[2] == 6 && name[0] == 'a' && name[2] == 'd' && name[3] == 'x');
        }
        // The rows with a card wait for each job the whole two seconds of a hang, as the self-test does (gpu_patient): the late cap is a tick, and on a loaded box (the
        // kernel's scheduler thread that hands the job to the ring is a thread like another) a job takes longer than that to start; the blocks that test the late state set
        // it to 0 for themselves.
        gpu_patient = 1;
        gpu_route_rows();
        gpu_node_rows();
        pic = (unsigned int address_to)((p8 address_to)gpu.at + pic_at), ops = (unsigned int address_to)((p8 address_to)gpu.at + ops_at);
        memory_zero(&ui, sizeof(ui));
        ui.pixels = gpu_kit_want, ui.pitch = PP, ui.width = PW, ui.height = PH, ui.top = 0, ui.bottom = PH, ui.scale = 100;
        for (int round = 0; round < ROUNDS; round++)
        {
                PW = round & 1 ? 697 : 696, ui.width = PW;
                for (positive i = 0; i < (positive)PP * PH; i++)
                        pic[i] = gpu_kit_want[i] = gpu_random();
                for (int k = 0; k < N; k++)
                {
                        unsigned int address_to q = ops + 8 * k, mode = gpu_random() % 8;
                        int x0 = (int)(gpu_random() % ((PW + 16) * 256)) - 8 * 256, y0 = (int)(gpu_random() % ((PH + 16) * 256)) - 8 * 256;
                        int w = (int)(gpu_random() % (mode == 0 ? 3 * 256 : mode < 3 ? 40 * 256 : 400 * 256)) - (mode == 7 ? 100 : 0);
                        int h = (int)(gpu_random() % (mode == 1 ? 3 * 256 : mode < 3 ? 40 * 256 : 300 * 256)) - (mode == 7 ? 100 : 0);
                        int rx = mode == 4 ? 0 : (int)(gpu_random() % (mode == 5 ? 900 * 256 : 24 * 256)), ry = mode == 4 ? 0 : (int)(gpu_random() % (mode == 6 ? 900 * 256 : 24 * 256));
                        unsigned int argb = gpu_random();
                        struct ui_box box;

                        if (gpu_random() % 3 == 0)
                                argb |= 0xff000000u;
                        if (gpu_random() % 11 == 0)
                                argb &= 0x00ffffffu;
                        box.x0 = x0, box.y0 = y0, box.x1 = x0 + w, box.y1 = y0 + h, box.rx = rx, box.ry = ry;
                        q[0] = 2, q[1] = (unsigned int)box.x0, q[2] = (unsigned int)box.y0, q[3] = (unsigned int)box.x1, q[4] = (unsigned int)box.y1, q[5] = argb, q[6] = (unsigned int)rx,
                        q[7] = (unsigned int)ry;
                        ui_kit_fill(&ui, &box, argb, 0);
                }
                if (!gpu_scene_taken(gpu_scene(ops_at, N, bins_at, pic_at, PP, PW, PH, 0, 0, PW, PH), "gpu kit: the scene finishes"))
                        return test_report(null);
                for (int y = 0; y < PH; y++)
                        for (int x = 0; x < PW; x++)
                        {
                                gpu_kit_pixel(&bad, "gpu kit", round, x, y, pic[(positive)y * PP + x], gpu_kit_want[(positive)y * PP + x]);
                        }
                gpu_kit_picture();
        }

        gpu_kit_recorded_controls(pic_at, ops_at, bins_at);
        {
                static const int hundred[1] = {100};

                app_rows(0, pic_at, ops_at, bins_at, bins_at + (8 << 20) + (positive)7680 * 4320 * 4 + (16 << 20), 16u << 20, hundred, 1, 700);
        }

        // The terminal's frames: the grid's list (terminal_list_draw) drawn by the card and held to the processor's full draw, bit for bit.
        if (program_argument_count() > 2)
                terminal_gpu_rows(program_argument(1), program_argument(2));

        // Text: runs painted by ui_run_in on the processor, the same runs recorded (the font engine's coverage, packed to bytes) and drawn by the GPU, a window's
        // clear first; every size, ink, alpha, stroke and clip, over a random ground. Needs the face (argument 3); without it the rows are skipped.
        if (program_argument_count() > 3)
        {
                enum { TW = 696, TH = 220, TP = 700, RUNS = 40, TROUNDS = 30 };
                static font_engine engine;
                static p8 atlas[512 * 512];
                static font_face face;
                static unsigned int scratch[TW * UI_SCRATCH_ROWS], want_text[TP * TH];
                static const char *const words[] = {"Settings", "Display and brightness", "The quick brown fox jumps over the lazy dog 0123456789",
                                                    "Wi-Fi   Bluetooth   USB", "Zwischenablage \xc3\x9f \xc3\xbc \xc3\xb6 \xc3\xa9", "AaBbCc", "WWWW iiii llll", "[x] (y) {z} 100%"};
                positive size = 0, bytes_of_face = compositor_map_file(program_argument(3), &size);
                positive text_at = bins_at + (8 << 20) + (positive)7680 * 4320 * 4 + (16 << 20), tpic_at = text_at, tops_at = tpic_at + (positive)TP * TH * 4,
                         tcover_at = tops_at + (1 << 20), tbins_at = tcover_at + (8 << 20);
                unsigned int address_to tpic = (unsigned int address_to)((p8 address_to)gpu.at + tpic_at), address_to tops = (unsigned int address_to)((p8 address_to)gpu.at + tops_at);
                struct ui_scene scene;
                struct ui cpu, rec;
                const font_face address_to faces[2] = {&face, &face};
                positive text_bad = 0;

                if (bytes_of_face && font_load(&face, (const p8 address_to)bytes_of_face, size, 0) == 0)
                {
                        font_start(&engine, atlas, 512, 512);
                        memory_zero(&cpu, sizeof(cpu));
                        cpu.engine = &engine, cpu.regular = cpu.bold = &face, cpu.scratch = scratch, cpu.scratch_width = TW, cpu.scale = 100;
                        cpu.width = TW, cpu.height = TH, cpu.top = 0, cpu.bottom = TH, cpu.pitch = TP, cpu.pixels = want_text;
                        rec = cpu;
                        rec.scene = &scene;
                        for (int round = 0; round < TROUNDS; round++)
                        {
                                unsigned int ground = gpu_random() | 0xff000000u;

                                memory_zero(&scene, sizeof(scene));
                                scene.ops = tops + 8, scene.room = 512, scene.cover = (unsigned char address_to)((p8 address_to)gpu.at + tcover_at), scene.size = 8 << 20, scene.at = tcover_at;
                                for (positive i = 0; i < (positive)TP * TH; i++)
                                        want_text[i] = ground, tpic[i] = 0x12345678u;
                                tops[0] = 0, tops[1] = tops[2] = 0, tops[3] = TW, tops[4] = TH, tops[5] = ground, tops[6] = tops[7] = 0;
                                for (int k = 0; k < RUNS; k++)
                                {
                                        const char *word = words[gpu_random() % (sizeof(words) / sizeof(words[0]))];
                                        int size_px = 10 + (int)(gpu_random() % 30), x = (int)(gpu_random() % (TW + 80)) - 60, baseline = (int)(gpu_random() % (TH + 30)) - 5;
                                        int low = gpu_random() % 4 ? 0 : (int)(gpu_random() % (TW / 2)), high = gpu_random() % 4 ? TW : TW / 2 + (int)(gpu_random() % (TW / 2));
                                        unsigned int colour = gpu_random() & 0xffffffu, alpha = gpu_random() % 3 ? 255 : gpu_random() & 255, stroke = gpu_random() % 4 ? 0 : 1 + gpu_random() % 255;

                                        ui_run_in(&cpu, faces, x, baseline, (string_address)word, size_px, colour, alpha, low, high, stroke);
                                        ui_run_in(&rec, faces, x, baseline, (string_address)word, size_px, colour, alpha, low, high, stroke);
                                }
                                check("gpu text: the runs were recorded", scene.count > 10);
                                if (!gpu_scene_taken(gpu_scene(tops_at, 1 + scene.count, tbins_at, tpic_at, TP, TW, TH, 0, 0, TW, TH), "gpu text: the scene finishes"))
                                        break;
                                for (int y = 0; y < TH; y++)
                                        for (int x = 0; x < TW; x++)
                                        {
                                                gpu_kit_pixel(&text_bad, "gpu text", round, x, y, tpic[(positive)y * TP + x], want_text[(positive)y * TP + x]);
                                        }
                                gpu_kit_picture();
                                // The same runs as a program's list: the ops read by the compositor's cut (compositor_gpu_list_ops), the cover bytes being the list's, its base
                                // the buffer's start (base number 3) and its cover bytes [tcover_at, tcover_at + scene.used), and drawn from what the cut wrote.
                                {
                                        positive address_to bases = (positive address_to)((p8 address_to)gpu.at + GPU_BASES_AT);
                                        unsigned int address_to cut = tops + 8 * 600, made;

                                        bases[3] = GPU_VA;
                                        made = compositor_gpu_list_ops(tops, 1 + scene.count, tcover_at, scene.used, gpu.size, TW, TH, 3, cut, 1000);
                                        check("gpu list: the runs of a frame are taken whole", made == 1 + scene.count);
                                        for (positive i = 0; i < (positive)TP * TH; i++)
                                                tpic[i] = 0x12345678u;
                                        if (made == COMPOSITOR_GPU_LIST_REFUSED || !gpu_scene(tops_at + 32 * 600, made, tbins_at, tpic_at, TP, TW, TH, 0, 0, TW, TH))
                                        {
                                                check("gpu list: the scene finishes", false);
                                                break;
                                        }
                                        for (int y = 0; y < TH; y++)
                                                for (int x = 0; x < TW; x++)
                                                {
                                                        gpu_kit_pixel(&text_bad, "gpu list", round, x, y, tpic[(positive)y * TP + x], want_text[(positive)y * TP + x]);
                                                }
                                        gpu_kit_picture();
                                }
                                // The same runs as a program's published list in a memfd of its own, drawn by compositor_gpu_list_render_bytes into a picture in another memfd: the
                                // reader, the cut, the map of the cover bytes and the number of their base, the map of the picture the card writes, the damage; against the processor's pixels.
                                {
                                        static unsigned char address_to list_memory;
                                        static unsigned int address_to list_picture;
                                        enum { LIST_BYTES = 4 << 20 };
                                        struct ui_draw_list list;
                                        struct ui lui = rec;
                                        int damage[4] = {-1, -1, -1, -1}, drawn;
                                        positive picture_bytes = (positive)TP * TH * 4 + 8192;

                                        if (!list_memory)
                                        {
                                                long lfd = system_call_2(syscall(memfd_create), (positive) "mw-list", 1), pfd = system_call_2(syscall(memfd_create), (positive) "mw-picture", 1);
                                                long lm = -1, pm = -1;

                                                if (lfd >= 0 && system_call_2(syscall(ftruncate), (positive)lfd, LIST_BYTES) == 0)
                                                        lm = system_call_6(syscall(mmap), 0, LIST_BYTES, 3, 1, (positive)lfd, 0);
                                                if (pfd >= 0 && system_call_2(syscall(ftruncate), (positive)pfd, picture_bytes) == 0)
                                                        pm = system_call_6(syscall(mmap), 0, picture_bytes, 3, 1, (positive)pfd, 0);
                                                if (!(lm < 0 && lm > -4096) && !(pm < 0 && pm > -4096))
                                                        list_memory = (unsigned char address_to)lm, list_picture = (unsigned int address_to)pm;
                                        }
                                        check("gpu list render: the memory for a list and a picture", list_memory && list_picture);
                                        if (!list_memory || !list_picture)
                                                break;
                                        lui.scene = null;
                                        compositor_gpu_state.up = 1, compositor_gpu_state.dead = 0, compositor_gpu = 1;
                                        compositor_gscene.ops = (unsigned int address_to)((p8 address_to)gpu.at + COMPOSITOR_GPU_OPS_AT), compositor_gscene.room = (1 << 20) / 32;
                                        check("gpu list render: the list is made", ui_draw_list_init(&list, list_memory, LIST_BYTES, 4096) && ui_scene_begin(&lui, &list));
                                        if (1 + scene.count <= list.scene.room && scene.used <= list.scene.size)
                                        {
                                                memory_copy(list.scene.ops, tops, 32 * (1 + scene.count));
                                                memory_copy(list.scene.cover, scene.cover, scene.used);
                                                for (unsigned int k = 0; k < 1 + scene.count; k++)
                                                        if ((list.scene.ops[8 * k] & 255) == 3)
                                                                list.scene.ops[8 * k + 7] = (unsigned int)(list.scene.ops[8 * k + 7] - tcover_at + list.scene.at);
                                                list.scene.count = 1 + scene.count, list.scene.used = scene.used;
                                                list.scene.dx0 = 0, list.scene.dy0 = 0, list.scene.dx1 = TW, list.scene.dy1 = TH;
                                        }
                                        else
                                                list.scene.lost = 1;
                                        check("gpu list render: the frame is published", ui_scene_end(&lui, &list));
                                        for (positive i = 0; i < picture_bytes / 4; i++)
                                                list_picture[i] = 0x12345678u;
                                        drawn = compositor_gpu_list_render_bytes(5, list_memory, LIST_BYTES, list_picture, TP, TW, TH, damage);
                                        check("gpu list render: the frame is drawn, with the damage it had", drawn == 1 && damage[0] == 0 && damage[1] == 0 && damage[2] == TW && damage[3] == TH);
                                        for (int y = 0; y < TH; y++)
                                                for (int x = 0; x < TW; x++)
                                                {
                                                        gpu_kit_pixel(&text_bad, "gpu list render", round, x, y, list_picture[(positive)y * TP + x], want_text[(positive)y * TP + x]);
                                                }
                                        gpu_kit_picture();
                                        // The same list again (the next frame of a window whose cover bytes are the same, or a few less): its cover bytes are in the map the
                                        // first frame made, and no map is made for them (a map a frame was a userptr and a mapping of the card's each time).
                                        {
                                                unsigned int mapped = gpu_maps_user + gpu_maps_dma, held = gpu_map_count;
                                                int again = compositor_gpu_list_render_bytes(5, list_memory, LIST_BYTES, list_picture, TP, TW, TH, damage);

                                                check("gpu list render: the same list drawn again makes no map", again == 1 && gpu_maps_user + gpu_maps_dma == mapped && gpu_map_count == held);
                                        }
                                        if (round == 0)
                                        {
                                                positive began = clock_monotonic_nanoseconds();

                                                for (unsigned int k = 0; k < 64; k++)
                                                        compositor_gpu_list_render_bytes(5, list_memory, LIST_BYTES, list_picture, TP, TW, TH, damage);
                                                string_format(log, "  gpu list render: 64 frames of one list took %p us, %p maps held\n", (clock_monotonic_nanoseconds() - began) / 1000, (positive)gpu_map_count);
                                        }
                                        gpu_trim();
                                }
                                // The same runs with their bitmaps named by a base of the table (a list's memfd is mapped at an address of its own): the text op's word 6 holds the
                                // base's number and its word 7 the place from that base, which is the buffer's start plus 4096 here, so the place is 4096 less than it was.
                                {
                                        positive address_to bases = (positive address_to)((p8 address_to)gpu.at + GPU_BASES_AT);

                                        bases[1] = GPU_VA + 4096, bases[2] = GPU_VA + 1000000;
                                        for (unsigned int k = 0; k < scene.count; k++)
                                        {
                                                unsigned int address_to q = tops + 8 + 8 * k;

                                                if ((q[0] & 255) == 3)
                                                        q[6] |= (k & 1 ? 2u : 1u) << 16, q[7] -= k & 1 ? 1000000u : 4096u;
                                        }
                                        for (positive i = 0; i < (positive)TP * TH; i++)
                                                tpic[i] = 0x12345678u;
                                        if (!gpu_scene_taken(gpu_scene(tops_at, 1 + scene.count, tbins_at, tpic_at, TP, TW, TH, 0, 0, TW, TH), "gpu text bases: the scene finishes"))
                                                break;
                                        for (int y = 0; y < TH; y++)
                                                for (int x = 0; x < TW; x++)
                                                {
                                                        gpu_kit_pixel(&text_bad, "gpu text bases", round, x, y, tpic[(positive)y * TP + x], want_text[(positive)y * TP + x]);
                                                }
                                        gpu_kit_picture();
                                }
                        }
                }
        }

        // A list a program wrote is not trusted: lists of ops made of random words, and of near misses (a run just past its cover bytes, a rectangle past the picture,
        // a count of records past the end, a kind a window is not made of), through the cut. A list taken has to be one the card may draw: kinds a window is made of,
        // rectangles in the picture, a run's bitmap and what its lanes read round it inside the cover bytes and the margin mapped for them, the base its own.
        {
                enum { LW = 300, LH = 200, LN = 48, LROOM = 100 };
                const positive cover_at = 1 << 20, cover_used = 1 << 16, list_bytes = 4 << 20, reach = COMPOSITOR_GPU_LIST_REACH;
                unsigned int hostile[8 * LN], out[8 * LROOM], taken = 0, refused = 0, wrong = 0;

                for (unsigned int trial = 0; trial < 30000; trial++)
                {
                        unsigned int count = 1 + gpu_random() % LN, made;
                        b32 plausible = trial % 3 != 0; // two lists in three are made of the kinds a window is, with valid runs most of the time

                        for (unsigned int i = 0; i < count; i++)
                        {
                                static const unsigned int kinds[] = {0, 1, 2, 3, 3, 3, 3, 8, 9, 4, 5, 6, 7, 10, 255}, ours[] = {0, 1, 2, 3, 3};
                                unsigned int *q = hostile + 8 * i;
                                unsigned int kind = plausible ? ours[gpu_random() % 5] : kinds[gpu_random() % (sizeof(kinds) / sizeof(kinds[0]))];
                                unsigned int pitch = 3 + gpu_random() % (gpu_random() % 8 ? 200 : 4000);

                                q[0] = kind == 3 ? 3u | pitch << 8 : kind == 2 ? 2u | (gpu_random() % 5) << 8 : kind | (gpu_random() % 16 ? 0 : gpu_random() << 8);
                                for (int w = 1; w < 8; w++)
                                        q[w] = gpu_random() % 5 ? (unsigned int)((int)(gpu_random() % (LW + 120)) - 60) : gpu_random();
                                if (kind == 3 && (plausible ? gpu_random() % 4 : gpu_random() % 2))
                                {
                                        unsigned int rows = gpu_random() % 20, cols = gpu_random() % 40;

                                        pitch = cols + 2 + gpu_random() % 30, q[0] = 3u | pitch << 8;
                                        q[1] = gpu_random() % (LW - 40), q[3] = q[1] + cols, q[2] = gpu_random() % (LH - 20), q[4] = q[2] + rows;
                                        q[7] = (unsigned int)(cover_at + pitch + 1 + gpu_random() % (cover_used - (positive)rows * pitch - cols - pitch - 3));
                                        if (gpu_random() % 6 == 0) // a run a few bytes either side of the first byte it may read
                                                q[7] = (unsigned int)(cover_at + pitch + 1 - 20 + gpu_random() % 40);
                                        else if (gpu_random() % 6 == 0) // and either side of the last
                                                q[7] = (unsigned int)(cover_at + cover_used - (positive)rows * pitch - cols - 1 - 20 + gpu_random() % 40);
                                }
                                else if (kind == 3)
                                        q[7] = (unsigned int)(cover_at - 300 + gpu_random() % (cover_used + 600));
                                if (kind == 2 || kind == 8 || kind == 9)
                                        q[1] <<= 8, q[2] <<= 8, q[3] <<= 8, q[4] <<= 8;
                                if (plausible && kind == 2)
                                {
                                        unsigned int records = q[0] >> 8;

                                        if (records > 3)
                                                records = 3, q[0] = 2u | 3u << 8;
                                        for (unsigned int k = 1; k <= records && i + 1 < LN; k++)
                                        {
                                                unsigned int *r = hostile + 8 * (i + 1);

                                                for (int w = 1; w < 8; w++)
                                                        r[w] = (unsigned int)((int)(gpu_random() % ((LW + 60) * 256)) - 30 * 256);
                                                r[0] = 8 + gpu_random() % 2;
                                                i++;
                                        }
                                        if (i + 1 > count)
                                                count = i + 1;
                                }
                        }
                        made = compositor_gpu_list_ops(hostile, count, cover_at, cover_used, list_bytes, LW, LH, 1 + trial % 255, out, LROOM);
                        if (made == COMPOSITOR_GPU_LIST_REFUSED)
                        {
                                refused++;
                                continue;
                        }
                        taken++;
                        for (unsigned int i = 0; i < made && i < LROOM; i++)
                        {
                                const unsigned int *q = out + 8 * i;
                                unsigned int kind = q[0] & 255;

                                if (kind > 3 && kind != 8 && kind != 9)
                                        wrong++;
                                if (kind <= 1 && (q[0] >> 8 || (int)q[1] < 0 || q[1] >= q[3] || q[3] > LW || (int)q[2] < 0 || q[2] >= q[4] || q[4] > LH))
                                        wrong++;
                                if (kind == 3)
                                {
                                        positive pitch = q[0] >> 8, rows = q[4] - q[2], cols = q[3] - q[1];
                                        positive lowest = (positive)q[7] - pitch - 1 - 64 - 4, highest = (positive)q[7] + rows * pitch + cols + 1 + 64 + 4;

                                        if ((int)q[1] < 0 || q[1] >= q[3] || q[3] > LW || (int)q[2] < 0 || q[2] >= q[4] || q[4] > LH || q[6] >> 16 != 1 + trial % 255 ||
                                            q[7] < pitch + 1 || lowest < cover_at - reach || highest > cover_at + cover_used + reach || (positive)q[7] - pitch - 1 < cover_at ||
                                            (positive)q[7] + rows * pitch + cols + 1 > cover_at + cover_used)
                                                wrong++;
                                }
                        }
                        // A box takes its records with it: the ones after a kind 2 within its count are kinds 8 and 9.
                        for (unsigned int i = 0; i < made && i < LROOM; i++)
                                if ((out[8 * i] & 255) == 2)
                                {
                                        unsigned int records = out[8 * i] >> 8;

                                        if (records > 3 || i + records >= made)
                                                wrong++;
                                        for (unsigned int k = 1; k <= records && i + k < made; k++)
                                                if (out[8 * (i + k)] != 8 && out[8 * (i + k)] != 9)
                                                        wrong++;
                                        i += records;
                                }
                }
                check("gpu list: lists of random words are refused or cut to what the card may draw", wrong == 0 && taken > 100 && refused > 100);
        }

        // A glyph translated (op 10, font_cell's cell on its ground): coverage bytes through 256 tones, against the processor's translate, as a program's list through the cut
        // (compositor_gpu_list_ops) with the base of the list the buffer's start; rectangles anywhere in the picture, pitches past the rectangle's width, three tables
        // shared by the ops, coverage with zeros and full bytes, over a random picture, in order.
        {
                enum { TRW = 400, TRH = 100, TRP = 404, TRN = 24, TRROUNDS = 30 };
                positive base_t = bins_at + (8 << 20) + (positive)7680 * 4320 * 4 + (16 << 20) + (16 << 20), ops_t = base_t + (1 << 20), bins_t = ops_t + (1 << 20), cover_t = bins_t + (2 << 20);
                unsigned int address_to tpic = (unsigned int address_to)((p8 address_to)gpu.at + base_t), address_to tops = (unsigned int address_to)((p8 address_to)gpu.at + ops_t);
                unsigned int address_to cut = tops + 8 * 100, want_t[TRP * TRH];
                p8 address_to cover = (p8 address_to)gpu.at + cover_t;
                positive address_to tbases = (positive address_to)((p8 address_to)gpu.at + GPU_BASES_AT), translate_bad = 0;

                tbases[7] = GPU_VA;
                for (int round = 0; round < TRROUNDS; round++)
                {
                        positive used = 4096, tables[3];
                        unsigned int made;

                        for (int t = 0; t < 3; t++)
                        {
                                unsigned int address_to table = (unsigned int address_to)(cover + used);

                                for (int i = 0; i < 256; i++)
                                        table[i] = gpu_random();
                                tables[t] = cover_t + used, used += 1024;
                        }
                        for (positive i = 0; i < (positive)TRP * TRH; i++)
                                tpic[i] = want_t[i] = gpu_random();
                        for (int k = 0; k < TRN; k++)
                        {
                                unsigned int address_to q = tops + 8 * k;
                                int x0 = (int)(gpu_random() % (TRW - 1)), y0 = (int)(gpu_random() % (TRH - 1));
                                int x1 = x0 + 1 + (int)(gpu_random() % (unsigned int)(TRW - x0 - 1 < 40 ? TRW - x0 - 1 : 40)), y1 = y0 + 1 + (int)(gpu_random() % (unsigned int)(TRH - y0 - 1 < 30 ? TRH - y0 - 1 : 30));
                                positive cols = (positive)(x1 - x0), rows = (positive)(y1 - y0), pitch = cols + gpu_random() % 24, table = tables[gpu_random() % 3], at = cover_t + used;
                                unsigned int address_to table_words = (unsigned int address_to)(cover + (table - cover_t));

                                for (positive i = 0; i < rows * pitch + 16; i++)
                                        cover[used + i] = (p8)(gpu_random() % 5 == 0 ? 0 : gpu_random() % 7 == 0 ? 255 : gpu_random());
                                for (positive r = 0; r < rows; r++)
                                        for (positive c = 0; c < cols; c++)
                                                want_t[(positive)(y0 + (int)r) * TRP + (positive)(x0 + (int)c)] = table_words[cover[used + r * pitch + c]];
                                q[0] = 10u | (unsigned int)pitch << 8, q[1] = (unsigned int)x0, q[2] = (unsigned int)y0, q[3] = (unsigned int)x1, q[4] = (unsigned int)y1;
                                q[5] = (unsigned int)table, q[6] = 0, q[7] = (unsigned int)at;
                                used += (rows * pitch + 16 + 15) & ~15ull;
                        }
                        made = compositor_gpu_list_ops(tops, TRN, cover_t, used, gpu.size, TRW, TRH, 7, cut, 1000);
                        check("gpu translate: the glyphs of a frame are taken whole", made == TRN);
                        if (made != TRN || !gpu_scene(ops_t + 32 * 100, made, bins_t, base_t, TRP, TRW, TRH, 0, 0, TRW, TRH))
                        {
                                check("gpu translate: the scene finishes", false);
                                break;
                        }
                        for (int y = 0; y < TRH; y++)
                                for (int x = 0; x < TRW; x++)
                                {
                                        gpu_kit_pixel(&translate_bad, "gpu translate", round, x, y, tpic[(positive)y * TRP + x], want_t[(positive)y * TRP + x]);
                                }
                        gpu_kit_picture();
                }
        }

        // Rings, ripples and rows: the kit's own calls, ui_kit_ring and ui_kit_fill with a clip, on the processor and recorded (the box's record and the
        // records that change its cover), over rows a list is scrolled under (top and bottom of the ui), against a random picture; a width that is not a
        // multiple of 4 is for the kernels that cannot, and is refused.
        {
                struct ui_scene scene;
                struct ui rec;
                positive ring_bad = 0;

                for (int round = 0; round < 40; round++)
                {
                        int top = round % 3 ? 0 : (int)(gpu_random() % 200), bottom = round % 3 == 1 ? PH : PH - (int)(gpu_random() % 200);

                        PW = 696;
                        ui.width = PW, ui.top = top, ui.bottom = bottom, ui.opaque = round & 1;
                        ui.band_top = ui.band_bottom = 0;
                        if (round % 5 == 4)
                                ui.band_top = (int)(gpu_random() % 60), ui.band_bottom = PH - (int)(gpu_random() % 60);
                        rec = ui;
                        rec.scene = &scene;
                        memory_zero(&scene, sizeof(scene));
                        scene.ops = ops, scene.room = 4 * N;
                        for (positive i = 0; i < (positive)PP * PH; i++)
                                pic[i] = gpu_kit_want[i] = gpu_random();
                        for (int k = 0; k < N; k++)
                        {
                                unsigned int kind = gpu_random() % 5, mode = gpu_random() % 8, argb = gpu_random();
                                int x0 = (int)(gpu_random() % ((PW + 16) * 256)) - 8 * 256, y0 = (int)(gpu_random() % ((PH + 16) * 256)) - 8 * 256;
                                int w = (int)(gpu_random() % (mode == 0 ? 3 * 256 : mode < 3 ? 40 * 256 : 300 * 256)) - (mode == 7 ? 100 : 0);
                                int h = (int)(gpu_random() % (mode == 1 ? 3 * 256 : mode < 3 ? 40 * 256 : 200 * 256)) - (mode == 7 ? 100 : 0);
                                int rx = mode == 4 ? 0 : (int)(gpu_random() % (mode == 5 ? 900 * 256 : 24 * 256)), ry = mode == 4 ? 0 : (int)(gpu_random() % (mode == 6 ? 900 * 256 : 24 * 256));
                                int thickness = gpu_random() % 3 ? (int)(gpu_random() % (12 * 256)) : (int)(gpu_random() % 600);
                                struct ui_box box = {x0, y0, x0 + w, y0 + h, rx, ry}, clip;

                                if (gpu_random() % 3 == 0)
                                        argb |= 0xff000000u;
                                if (gpu_random() % 11 == 0)
                                        argb &= 0x00ffffffu;
                                if (gpu_random() % 16 == 0)
                                        thickness = -thickness;
                                clip.x0 = x0 + (int)(gpu_random() % (unsigned int)(w > 0 ? w : 1)) - w / 3, clip.y0 = y0 + (int)(gpu_random() % (unsigned int)(h > 0 ? h : 1)) - h / 3;
                                clip.x1 = clip.x0 + (w > 0 ? w : 256) / 2 + (int)(gpu_random() % 4096), clip.y1 = clip.y0 + (h > 0 ? h : 256) / 2 + (int)(gpu_random() % 4096);
                                clip.rx = (int)(gpu_random() % (20 * 256)), clip.ry = (int)(gpu_random() % (20 * 256));
                                // every so often a wide box with a small clip in it: most of its patches miss the clip
                                if (k % 16 == 0)
                                        kind = 2, box = (struct ui_box){20 * 256, 20 * 256, 620 * 256, 170 * 256, 8 * 256, 8 * 256},
                                        clip = (struct ui_box){300 * 256, 90 * 256, 340 * 256, 110 * 256, 4 * 256, 4 * 256};
                                // the rectangles, the single pixels and the clear are the kit's other painters of the same picture
                                if (k % 24 == 5)
                                        ui_clear(&ui), ui_clear(&rec);
                                else if (kind == 3)
                                        ui_fill(&ui, x0 >> 8, y0 >> 8, w >> 8, h >> 8, argb), ui_fill(&rec, x0 >> 8, y0 >> 8, w >> 8, h >> 8, argb);
                                else if (kind == 4)
                                        ui_dot(&ui, x0 >> 8, y0 >> 8, argb), ui_dot(&rec, x0 >> 8, y0 >> 8, argb);
                                else if (kind == 1)
                                        ui_kit_ring(&ui, &box, thickness, argb), ui_kit_ring(&rec, &box, thickness, argb);
                                else
                                        ui_kit_fill(&ui, &box, argb, kind == 2 ? &clip : 0), ui_kit_fill(&rec, &box, argb, kind == 2 ? &clip : 0);
                        }
                        check("gpu rings: the records fit", !scene.lost && scene.count >= N / 2);
                        check("gpu rings: an odd width is refused", !gpu_scene(ops_at, scene.count, bins_at, pic_at, PP, 697, PH, 0, 0, 697, PH));
                        if (!gpu_scene_taken(gpu_scene(ops_at, scene.count, bins_at, pic_at, PP, PW, PH, 0, 0, PW, PH), "gpu rings: the scene finishes"))
                                return test_report(null);
                        for (int y = 0; y < PH; y++)
                                for (int x = 0; x < PW; x++)
                                {
                                        gpu_kit_pixel(&ring_bad, "gpu rings", round, x, y, pic[(positive)y * PP + x], gpu_kit_want[(positive)y * PP + x]);
                                }
                        gpu_kit_picture();
                }
                ui.top = 0, ui.bottom = PH, ui.width = PW, ui.band_top = ui.band_bottom = 0, ui.opaque = 0;
        }

        // Images: pictures copied into the picture by the GPU, from two places: a buffer of the GPU's own, and the pages of a memfd mapped shared (what a
        // window's pixels are), read by the GPU as a userptr buffer at an address of their own. The rectangles are cut at the picture's edges, the copies start
        // anywhere in the source; against memory_copy_rect_u32 on the processor over a random picture.
        {
                enum { SW = 400, SH = 300, SP = 416, IMAGES = 40, IROUNDS = 12 };
                positive img_at = bins_at + (8 << 20) + (positive)7680 * 4320 * 4 + (16 << 20) + (12 << 20), up_va = 0;
                unsigned int address_to gpu_img = (unsigned int address_to)((p8 address_to)gpu.at + img_at), address_to shared;
                long fd = system_call_2(syscall(memfd_create), (positive) "mw-window", 1), mapped = -1;
                positive image_bad = 0;

                if (fd >= 0 && system_call_2(syscall(ftruncate), (positive)fd, 1 << 20) == 0)
                        mapped = system_call_6(syscall(mmap), 0, 1 << 20, 3, 1, (positive)fd, 0);
                shared = (unsigned int address_to)mapped;
                if (!(mapped < 0 && mapped > -4096) && gpu_map(shared, 1 << 20))
                        up_va = gpu_va(shared);
                check("gpu images: the memfd's pages are a userptr buffer", up_va != 0);
                for (positive i = 0; i < (positive)SP * SH; i++)
                        gpu_img[i] = gpu_random();
                for (positive i = 0; up_va && i < (positive)SW * SH; i++)
                        shared[i] = gpu_random();
                PW = 696;
                ui.width = PW;
                for (int round = 0; round < IROUNDS; round++)
                {
                        positive count = 0;

                        for (positive i = 0; i < (positive)PP * PH; i++)
                                pic[i] = gpu_kit_want[i] = gpu_random();
                        for (int k = 0; k < IMAGES; k++)
                        {
                                unsigned int address_to q = ops + 8 * count;
                                int from_shared = up_va && gpu_random() % 2, pitch = from_shared ? SW : SP;
                                int sx = (int)(gpu_random() % 300), sy = (int)(gpu_random() % 200), w = 1 + (int)(gpu_random() % (unsigned int)(SW - sx)), h = 1 + (int)(gpu_random() % (unsigned int)(SH - sy));
                                int x0 = (int)(gpu_random() % (PW + 100)) - 50, y0 = (int)(gpu_random() % (PH + 100)) - 50;
                                int cx0, cy0, cx1, cy1;
                                positive from, at;

                                if (gpu_random() % 6 == 0)
                                        x0 = y0 = 0; // the picture's top left corner
                                cx0 = x0 < 0 ? 0 : x0, cy0 = y0 < 0 ? 0 : y0, cx1 = x0 + w > PW ? PW : x0 + w, cy1 = y0 + h > PH ? PH : y0 + h;
                                if (cx1 <= cx0 || cy1 <= cy0)
                                        continue;
                                from = (positive)(sy + cy0 - y0) * (positive)pitch + (positive)(sx + cx0 - x0); // the source's pixel at the cut rectangle's corner
                                at = (from_shared ? up_va : GPU_VA + img_at) + 4ull * from;
                                q[0] = 4, q[1] = (unsigned int)cx0, q[2] = (unsigned int)cy0, q[3] = (unsigned int)cx1, q[4] = (unsigned int)cy1, q[5] = (unsigned int)at, q[6] = (unsigned int)(at >> 32), q[7] = (unsigned int)pitch;
                                memory_copy_rect_u32(gpu_kit_want + (positive)cy0 * PP + (positive)cx0, PP, (from_shared ? shared : gpu_img) + from, (positive)pitch,
                                                     (positive)(cx1 - cx0), (positive)(cy1 - cy0));
                                count++;
                        }
                        if (!gpu_scene_taken(gpu_scene(ops_at, (unsigned int)count, bins_at, pic_at, PP, PW, PH, 0, 0, PW, PH), "gpu images: the scene finishes"))
                                return test_report(null);
                        for (int y = 0; y < PH; y++)
                                for (int x = 0; x < PW; x++)
                                {
                                        gpu_kit_pixel(&image_bad, "gpu images", round, x, y, pic[(positive)y * PP + x], gpu_kit_want[(positive)y * PP + x]);
                                }
                        gpu_kit_picture();
                }
                // Mixes: memory_mix_u32_row over rectangles (a corner's square or a line's end), the ink a colour or a picture, the covers a table of bytes in the
                // buffer with a pitch of its own, a quarter of them none and a quarter whole.
                {
                        enum { CP = 96, CH = 80, MIXES = 48, MROUNDS = 12 };
                        positive cov_at = img_at + (positive)SP * SH * 4 + 4096;
                        unsigned char address_to table = (unsigned char address_to)((p8 address_to)gpu.at + cov_at);
                        static unsigned int ink_row[CP];
                        positive mix_bad = 0;

                        for (int round = 0; round < MROUNDS; round++)
                        {
                                positive count = 0;
                                int cp = CP - round % 5; // the table's rows are cp bytes apart in this round

                                for (positive i = 0; i < (positive)cp * CH; i++)
                                {
                                        unsigned int r = gpu_random();

                                        table[i] = (unsigned char)(r % 4 == 0 ? 0 : r % 4 == 1 ? 255 : r >> 8);
                                }
                                for (positive i = 0; i < (positive)PP * PH; i++)
                                        pic[i] = gpu_kit_want[i] = gpu_random();
                                for (int k = 0; k < MIXES; k++)
                                {
                                        unsigned int address_to q = ops + 8 * count;
                                        int constant = gpu_random() % 3 == 0, from_shared = up_va && gpu_random() % 2, ink_pitch = from_shared ? SW : SP;
                                        int w = 1 + (int)(gpu_random() % 80), h = 1 + (int)(gpu_random() % 70), tx = (int)(gpu_random() % (unsigned int)(cp - w - 4)), ty = (int)(gpu_random() % (unsigned int)(CH - h));
                                        int sx = (int)(gpu_random() % 250), sy = (int)(gpu_random() % 200);
                                        int x0 = (int)(gpu_random() % (PW + 60)) - 30, y0 = (int)(gpu_random() % (PH + 60)) - 30;
                                        int cx0 = x0 < 0 ? 0 : x0, cy0 = y0 < 0 ? 0 : y0, cx1 = x0 + w > PW ? PW : x0 + w, cy1 = y0 + h > PH ? PH : y0 + h;
                                        unsigned int colour = gpu_random();
                                        positive cover_place, ink_place;

                                        if (cx1 <= cx0 || cy1 <= cy0)
                                                continue;
                                        cover_place = (positive)(ty + cy0 - y0) * (positive)cp + (positive)(tx + cx0 - x0);
                                        ink_place = (positive)(sy + cy0 - y0) * (positive)ink_pitch + (positive)(sx + cx0 - x0);
                                        q[0] = 5u | (unsigned int)cp << 8 | (constant ? 0u : (unsigned int)ink_pitch << 16);
                                        q[1] = (unsigned int)cx0, q[2] = (unsigned int)cy0, q[3] = (unsigned int)cx1, q[4] = (unsigned int)cy1, q[5] = (unsigned int)(cov_at + cover_place);
                                        if (constant)
                                                q[6] = colour, q[7] = 0;
                                        else
                                        {
                                                positive at = (from_shared ? up_va : GPU_VA + img_at) + 4ull * ink_place;

                                                q[6] = (unsigned int)at, q[7] = (unsigned int)(at >> 32);
                                        }
                                        for (int y = cy0; y < cy1; y++)
                                        {
                                                const unsigned int address_to ink = constant ? ink_row : (from_shared ? shared : gpu_img) + ink_place + (positive)(y - cy0) * (positive)ink_pitch;

                                                if (constant)
                                                        memory_fill_u32(ink_row, (positive)(cx1 - cx0), colour);
                                                memory_mix_u32_row(gpu_kit_want + (positive)y * PP + (positive)cx0, ink, table + cover_place + (positive)(y - cy0) * (positive)cp, (positive)(cx1 - cx0));
                                        }
                                        count++;
                                }
                                if (!gpu_scene_taken(gpu_scene(ops_at, (unsigned int)count, bins_at, pic_at, PP, PW, PH, 0, 0, PW, PH), "gpu mixes: the scene finishes"))
                                        return test_report(null);
                                for (int y = 0; y < PH; y++)
                                        for (int x = 0; x < PW; x++)
                                        {
                                                gpu_kit_pixel(&mix_bad, "gpu mixes", round, x, y, pic[(positive)y * PP + x], gpu_kit_want[(positive)y * PP + x]);
                                        }
                                gpu_kit_picture();
                        }
                }
                // Turns: a screen's rectangle of a picture turned a quarter, a half and three quarters into a panel's buffer (windows_rotate_u32, which Canvas turns a
                // screen with), one op over the whole panel with a step along its rows and one down them, and a damage rectangle drawn alone: the rounded-out
                // rectangle of it is the whole turn's and the rest of the buffer is as it was.
                {
                        enum { TP = 544, TH = 320, DP = 544, DH = 540, TROUNDS = 12 };
                        positive tsrc_at = img_at + (1 << 20), tdst_at = img_at + (2 << 20);
                        unsigned int address_to tsrc = (unsigned int address_to)((p8 address_to)gpu.at + tsrc_at), address_to tdst = (unsigned int address_to)((p8 address_to)gpu.at + tdst_at);
                        static unsigned int turned_want[DP * DH];
                        positive turn_bad = 0;

                        for (int round = 0; round < TROUNDS; round++)
                        {
                                unsigned int degrees = round % 3 == 0 ? 90 : round % 3 == 1 ? 180 : 270;
                                int lw = 4 * (60 + (int)(gpu_random() % 70)), lh = 4 * (40 + (int)(gpu_random() % 40)), ox = (int)(gpu_random() % 20), oy = (int)(gpu_random() % 10);
                                int pw = degrees == 180 ? lw : lh, ph = degrees == 180 ? lh : lw, stepx, stepy;
                                long first;
                                positive at;
                                unsigned int x0 = gpu_random() % (unsigned int)lw, y0 = gpu_random() % (unsigned int)lh, x1, y1, panel[4];
                                unsigned int address_to q = ops;

                                x1 = x0 + 1 + gpu_random() % (unsigned int)(lw - (int)x0), y1 = y0 + 1 + gpu_random() % (unsigned int)(lh - (int)y0);
                                if (round % 4 == 3)
                                        x0 = 0, y0 = 0, x1 = (unsigned int)lw, y1 = (unsigned int)lh;
                                for (positive i = 0; i < (positive)TP * TH; i++)
                                        tsrc[i] = gpu_random();
                                for (positive i = 0; i < (positive)DP * DH; i++)
                                        tdst[i] = turned_want[i] = 0;
                                windows_rotate_u32(turned_want, DP, tsrc + (positive)oy * TP + (positive)ox, TP, (unsigned int)lw, (unsigned int)lh, degrees, 0, 0, (unsigned int)lw, (unsigned int)lh);
                                if (degrees == 90)
                                        stepx = -TP, stepy = 1, first = (long)(oy + lh - 1) * TP + ox;
                                else if (degrees == 270)
                                        stepx = TP, stepy = -1, first = (long)oy * TP + ox + lw - 1;
                                else
                                        stepx = -1, stepy = -TP, first = (long)(oy + lh - 1) * TP + ox + lw - 1;
                                at = GPU_VA + tsrc_at + 4ull * (positive)first;
                                q[0] = 7u | (unsigned int)(stepx + 0x800000) << 8, q[1] = 0, q[2] = 0, q[3] = (unsigned int)pw, q[4] = (unsigned int)ph;
                                q[5] = (unsigned int)at, q[6] = (unsigned int)(at >> 32), q[7] = (unsigned int)stepy;
                                windows_rotate_panel((unsigned int)lw, (unsigned int)lh, degrees, x0, y0, x1, y1, panel);
                                if (!gpu_scene_taken(gpu_scene(ops_at, 1, bins_at, tdst_at, DP, (unsigned int)pw, (unsigned int)ph, panel[0], panel[1], panel[2], panel[3]), "gpu turns: the scene finishes"))
                                        return test_report(null);
                                {
                                        unsigned int rx0 = (panel[0] >> 6) << 6, rx1 = ((panel[2] + 63) >> 6) << 6, ry0 = (panel[1] >> 2) << 2, ry1 = ((panel[3] + 3) >> 2) << 2;

                                        rx1 = rx1 > (unsigned int)pw ? (unsigned int)pw : rx1, ry1 = ry1 > (unsigned int)ph ? (unsigned int)ph : ry1;
                                        for (unsigned int y = 0; y < (unsigned int)ph; y++)
                                                for (unsigned int x = 0; x < (unsigned int)pw; x++)
                                                {
                                                        int inside = x >= rx0 && x < rx1 && y >= ry0 && y < ry1;
                                                        unsigned int want_pixel = inside ? turned_want[(positive)y * DP + x] : 0;

                                                        gpu_px(tdst[(positive)y * DP + x] != want_pixel, x, y, tdst[(positive)y * DP + x], want_pixel);
                                                        turn_bad += tdst[(positive)y * DP + x] != want_pixel;
                                                }
                                        {
                                                positive wrong = gpu_px_end();

                                                if (wrong)
                                                        string_format(log, "  FAIL gpu turns %p (%p degrees)", (positive)round, (positive)degrees), gpu_px_tail(wrong);
                                        }
                                }
                        }
                }
        }

        // Frames: the compositor's own windows (a picture in a memfd's pages, a radius, a line, corners kept square, any place, overlapping) composed by the GPU
        // from compositor_gpu_frames against compositor_draw on the processor over a ground: the whole picture, and a damage rectangle drawn alone.
        {
                enum { FRAMES = 6, FROUNDS = 40 };
                positive tables_at = bins_at + (8 << 20) + (positive)7680 * 4320 * 4 + (16 << 20) + (12 << 20) + (6 << 20);
                struct compositor_frame frames[FRAMES + 1]; // the last one a snap preview lifted into a frame of no area, as the present makes it
                long fd = system_call_2(syscall(memfd_create), (positive) "mw-window", 1), mapped = -1;
                unsigned int address_to region;
                struct compositor_target reference;
                positive frame_bad = 0, frame_dumped = 0;
                int cursor_note[5] = {0, 0, 0, 0, 0};

                if (fd >= 0 && system_call_2(syscall(ftruncate), (positive)fd, 4 << 20) == 0)
                        mapped = system_call_6(syscall(mmap), 0, 4 << 20, 3, 1, (positive)fd, 0);
                region = (unsigned int address_to)mapped;
                check("gpu frames: the pictures are userptr buffers", !(mapped < 0 && mapped > -4096) && gpu_map(region, 4 << 20));
                memory_zero(&compositor_gscene, sizeof(compositor_gscene));
                compositor_gscene.ops = ops, compositor_gscene.room = 4096, compositor_gscene.width = 696, compositor_gscene.height = PH;
                compositor_gscene.tables = (unsigned char address_to)((p8 address_to)gpu.at + tables_at), compositor_gscene.tables_at = (unsigned int)tables_at;
                compositor_gscene.tables_size = 1 << 20;
                reference = (struct compositor_target){gpu_kit_want, PP, 696, PH, 0, 0, 696, PH, 0, 0, 0};
                for (int round = 0; round < FROUNDS; round++)
                {
                        unsigned int ground = gpu_random() | 0xff000000u, used = 0;
                        int count = 2 + (int)(gpu_random() % (FRAMES - 1));

                        for (int k = 0; k < count; k++)
                        {
                                struct compositor_frame *f = &frames[k];
                                unsigned int pitch;

                                memory_zero(f, sizeof(*f));
                                f->width = 30 + (int)(gpu_random() % 260), f->height = 24 + (int)(gpu_random() % 200);
                                f->x = (int)(gpu_random() % (696 + 60)) - 50, f->y = (int)(gpu_random() % (PH + 60)) - 50;
                                f->scale = 100, f->radius = gpu_random() % 4 ? (int)(gpu_random() % 26) : 0, f->line = gpu_random() % 3 ? (int)(gpu_random() % 4) : 0;
                                f->edge = gpu_random() % 5 ? (gpu_random() | 0xff000000u) : gpu_random(), f->square = gpu_random() % 3 ? 0 : (int)(gpu_random() % 16);
                                pitch = (unsigned int)f->width + gpu_random() % 8;
                                if (gpu_random() % 5 == 0)
                                {
                                        // a landing: the contents are the picture of another size, scaled
                                        f->source_w = 16 + (int)(gpu_random() % 320), f->source_h = 12 + (int)(gpu_random() % 240);
                                        if (f->source_w == f->width && f->source_h == f->height)
                                                f->source_w++;
                                        pitch = (unsigned int)f->source_w + gpu_random() % 8;
                                        // wholly on the target: the processor walks a part of a scaled frame from the part's own position, and a frame's contents cut by the
                                        // target's edge is such a part (its columns can differ from the card's whole walk by a source cell)
                                        f->x = (int)(gpu_random() % (unsigned int)(696 - f->width + 1)), f->y = (int)(gpu_random() % (unsigned int)(PH - f->height + 1));
                                        // square: a round corner splits the contents into the pieces the processor scales one by one, each from the position of its own left and top
                                        // (memory_resample_u32_rect starts a part's walk half a cell in from the part's origin), which differs from the card's whole picture by a source
                                        // cell here and there; the contents drawn as one piece are the card's to the bit
                                        f->radius = 0;
                                }
                                if (gpu_random() % 4 == 0)
                                {
                                        // a tab of the panel: one ink, no picture and no line
                                        static const int slots[4] = {-2, 0, 1, 2};

                                        f->builtin = 1, f->slot = slots[gpu_random() % 4], f->line = 0, f->height = 20 + (int)(gpu_random() % 30), f->source_w = f->source_h = 0;
                                        continue;
                                }
                                f->pitch = pitch, f->pixels = region + used;
                                for (positive i = 0; i < (positive)pitch * (unsigned int)(f->source_w ? f->source_h : f->height); i++)
                                        region[used + i] = gpu_random();
                                used += (pitch * (unsigned int)(f->source_w ? f->source_h : f->height) + 15) & ~15u;
                                // the line lit where the hand holds it, on some windows: an edge, two that meet, all of it
                                if (gpu_random() % 3 == 0)
                                        f->lit = (gpu_random() % 5 == 0 ? 16u : 0u) | gpu_random() % 16, f->lit_ink = gpu_random() | 0xff000000u;
                                // a close button (and the minimise beside it) on most windows, where it lies wholly in the picture
                                if (gpu_random() % 4)
                                {
                                        int bx, by, side;

                                        f->close = 1, f->close_mean = gpu_random() & 0xffffffu;
                                        if (gpu_random() % 3 == 0)
                                                f->scale = 100 + 25 * (int)(gpu_random() % 5);
                                        if (!compositor_close_box(f, &bx, &by, &side) || bx < 0 || by < 0 || bx + side > 696 || by + side > PH)
                                                f->close = 0;
                                }
                        }
                        // a frame with a line and no picture (a grid's border): it draws its line and corners only, its radius no past its line (the
                        // processor reads a picture for a round corner's contents, so a frame with a bigger radius has none drawn at all)
                        for (int k = 0; k < count; k++)
                                if (!frames[k].builtin && gpu_random() % 8 == 0)
                                        frames[k].pixels = 0, frames[k].source_w = frames[k].source_h = 0, frames[k].close = 0, frames[k].radius = frames[k].line;
                        // the marks and the snap preview as the present has them: a shared edge's mark over some windows, and on some rounds the preview lifted
                        // into a frame of no area after the tabs (zero size, the rest of its fields as compositor_preview_lift leaves them).
                        // A round with a tint compares the channels only: compositor_mix leaves a pixel mixed at cover 0 with its top byte clear, where
                        // the card keeps the byte (memory_mix_u32_row's rule, "a pixel with cover 0 is left"). The top byte is not shown (the scanout is
                        // XRGB). The card's rule is the library routine's contract (memory_mix_u32_row, bit for bit, kept as it is); compositor_mix's clear
                        // is the processor's own, and nothing shows the byte, so the rounds with a tint or a spot compare channels: the one place the rig
                        // is not whole-word, and kept so until the processor's rule and the library's agree on the byte.
                        unsigned int frame_mask = 0xffffffffu;
                        for (int k = 0; k < count; k++)
                                if (gpu_random() % 3 == 0)
                                {
                                        frames[k].mark[0] = (int)(gpu_random() % 680) - 20, frames[k].mark[1] = (int)(gpu_random() % (PH + 20)) - 10;
                                        frames[k].mark[2] = 1 + (int)(gpu_random() % 120), frames[k].mark[3] = 1 + (int)(gpu_random() % 90);
                                        frames[k].mark_ink = gpu_random();
                                        frame_mask = 0x00ffffffu;
                                }
                        if (gpu_random() % 2)
                        {
                                struct compositor_frame *lifted = &frames[count++];

                                frame_mask = 0x00ffffffu;
                                memory_zero(lifted, sizeof(*lifted));
                                lifted->scale = 100;
                                lifted->preview[0] = (int)(gpu_random() % 680) - 20, lifted->preview[1] = (int)(gpu_random() % (PH + 20)) - 10;
                                lifted->preview[2] = 1 + (int)(gpu_random() % 200), lifted->preview[3] = 1 + (int)(gpu_random() % 150);
                                lifted->preview_ink = gpu_random();
                                lifted->preview_radius = gpu_random() % 4 ? (int)(gpu_random() % 40) : 0;
                                lifted->preview_line = gpu_random() % 3 ? (int)(gpu_random() % 6) : 0;
                                lifted->preview_edge = gpu_random();
                        }
                        // the fingers' marks: on some rounds, a few at random places, alphas and scales (compositor_spots_draw after the tints)
                        cursor_note[4] = 0;
                        compositor.scale = round % 3 == 0 ? 150 : 100;
                        compositor.spots_shown = round % 2 ? (unsigned int)(gpu_random() & 7) | 1u : 0;
                        for (unsigned int slot = 0; slot < UI_TOUCH_SLOTS; slot++)
                        {
                                compositor.spot[slot].on = gpu_random() % 3 != 0;
                                compositor.spot[slot].x = (int)(gpu_random() % 720) - 12, compositor.spot[slot].y = (int)(gpu_random() % (PH + 24)) - 12;
                                compositor.spot[slot].alpha = gpu_random() % 4 ? gpu_random() % 256 : 255;
                        }
                        if (compositor.spots_shown)
                                frame_mask = 0x00ffffffu;
                        memory_fill_u32_rect(gpu_kit_want, PP, 696, PH, ground);
                        for (int k = 0; k < count; k++)
                                compositor_draw(&reference, &frames[k]);
                        // the processor's tints after the frames, as compositor_scene_banded draws them (compositor_preview_draw)
                        {
                                int radii[FRAMES + 1];

                                for (int k = 0; k < count; k++)
                                        radii[k] = compositor_radius(&frames[k]);
                                compositor_preview_draw(&reference, frames, (unsigned int)count, radii);
                        }
                        compositor_spots_draw(&reference);
                        for (positive i = 0; i < (positive)PP * PH; i++)
                                pic[i] = 0;
                        const struct compositor_frame *drawn = frames;

                        check("gpu frames: the landings are scaled by the card into pictures of their own size", compositor_gpu_scale_frames(&drawn, (unsigned int)count, 0, 0) == COMPOSITOR_GPU_WHY_DRAWN);
                        b32 framed = compositor_gpu_frames(drawn, (unsigned int)count, ground);

                        if (!framed)
                                string_format(log, "  gpu frames round %p: refused for reason %p, %p frames\n", (positive)round, (positive)compositor_gscene.why, (positive)count);
                        check("gpu frames: the frames are drawn by the GPU", framed);
                        check("gpu frames: the fingers' marks are drawn by the GPU, after the scene", compositor_gpu_spots());
                        // the pointer the scene carries, over everything, on two rounds in three
                        if (round % 3)
                        {
                                int px = (int)(gpu_random() % (696 + 40)) - 20, py = (int)(gpu_random() % (PH + 40)) - 20, shape = (int)(gpu_random() % (CP_SHAPES + 1)), size = 1 + (int)(gpu_random() % 3);
                                unsigned int fill = gpu_random(), edge = gpu_random();

                                // a bitmap or the dynamic shape, at a whole size or one between
                                struct compositor_pointer pointer = {.dyn = shape == CP_SHAPES, .shape = shape < CP_SHAPES ? (unsigned int)shape : 0, .scale = size * 256 - (int)(gpu_random() % 3) * 40,
                                                                     .hw = 8 * 256, .hh = 8 * 256, .c = 8 * 256};

                                compositor_cursor_draw(&reference, px, py, &pointer, fill & 0xffffffu, edge & 0xffffffu);
                                check("gpu frames: the pointer is drawn by the GPU, over everything", compositor_gpu_cursor(px, py, &pointer, fill & 0xffffffu, edge & 0xffffffu));
                                cursor_note[0] = px, cursor_note[1] = py, cursor_note[2] = shape, cursor_note[3] = size, cursor_note[4] = 1;
                        }
                        if (!gpu_scene_taken(gpu_scene(ops_at, compositor_gscene.count, bins_at, pic_at, PP, 696, PH, 0, 0, 696, PH), "gpu frames: the scene finishes"))
                                return test_report(null);
                        for (int y = 0; y < PH; y++)
                                for (int x = 0; x < 696; x++)
                                {
                                        b32 differs = ((pic[(positive)y * PP + x] ^ gpu_kit_want[(positive)y * PP + x]) & frame_mask) != 0;

                                        gpu_px(differs, (positive)x, (positive)y, pic[(positive)y * PP + x], gpu_kit_want[(positive)y * PP + x]);
                                        frame_bad += differs;
                                }
                        {
                                positive wrong = gpu_px_end();

                                if (wrong)
                                        string_format(log, "  FAIL gpu frames round %p", (positive)round), gpu_px_tail(wrong);
                        }
                        if (frame_bad && frame_bad != frame_dumped)
                        {
                                frame_dumped = frame_bad;
                                string_format(log, "  cursor %p: %p,%p shape %p size %p, spots %p\n", (positive)cursor_note[4], (positive)cursor_note[0], (positive)cursor_note[1], (positive)cursor_note[2],
                                              (positive)cursor_note[3], (positive)compositor.spots_shown);
                                for (int k = 0; k < count; k++)
                                        string_format(log, "  preview %p,%p,%p,%p radius %p line %p mark %p,%p,%p,%p\n", (positive)frames[k].preview[0], (positive)frames[k].preview[1], (positive)frames[k].preview[2],
                                                      (positive)frames[k].preview[3], (positive)frames[k].preview_radius, (positive)frames[k].preview_line, (positive)frames[k].mark[0],
                                                      (positive)frames[k].mark[1], (positive)frames[k].mark[2], (positive)frames[k].mark[3]);
                                for (int k = 0; k < count; k++)
                                        string_format(log, "  frame %p: at %p,%p size %p,%p source %p,%p pitch %p radius %p line %p square %p close %p pixels %p builtin %p\n", (positive)k,
                                                      (positive)drawn[k].x, (positive)drawn[k].y, (positive)drawn[k].width, (positive)drawn[k].height, (positive)frames[k].source_w,
                                                      (positive)frames[k].source_h, (positive)frames[k].pitch, (positive)frames[k].radius, (positive)frames[k].line, (positive)frames[k].square,
                                                      (positive)frames[k].close, (positive)(frames[k].pixels != 0), (positive)frames[k].builtin);
                        }
                        // a part: the rounded-out rectangle is the whole's, the rest untouched
                        {
                                unsigned int x0 = gpu_random() % 600, y0 = gpu_random() % (PH - 10), x1 = x0 + 1 + gpu_random() % 90, y1 = y0 + 1 + gpu_random() % 60;
                                unsigned int rx0 = (x0 >> 6) << 6, rx1 = ((x1 + 63) >> 6) << 6, ry0 = (y0 >> 2) << 2, ry1 = ((y1 + 3) >> 2) << 2;

                                x1 = x1 > 696 ? 696 : x1, y1 = y1 > (unsigned int)PH ? (unsigned int)PH : y1;
                                rx1 = rx1 > 696 ? 696 : rx1, ry1 = ry1 > (unsigned int)PH ? (unsigned int)PH : ry1;
                                for (positive i = 0; i < (positive)PP * PH; i++)
                                        pic[i] = 0;
                                gpu_scene(ops_at, compositor_gscene.count, bins_at, pic_at, PP, 696, PH, x0, y0, x1, y1);
                                for (unsigned int y = 0; y < (unsigned int)PH; y++)
                                        for (unsigned int x = 0; x < 696; x++)
                                        {
                                                int inside = x >= rx0 && x < rx1 && y >= ry0 && y < ry1;

                                                b32 differs = ((pic[(positive)y * PP + x] ^ (inside ? gpu_kit_want[(positive)y * PP + x] : 0)) & frame_mask) != 0;

                                                gpu_px(differs, x, y, pic[(positive)y * PP + x], inside ? gpu_kit_want[(positive)y * PP + x] : 0);
                                                frame_bad += differs;
                                        }
                                {
                                        positive wrong = gpu_px_end();

                                        if (wrong)
                                                string_format(log, "  FAIL gpu frames part %p", (positive)round), gpu_px_tail(wrong);
                                }
                        }
                }
                // A card started again has a new buffer, which holds none of the tables the scene remembers (gpu_start makes it): compositor_gpu_layout lets
                // them go with the layout. One rounded frame (a line of 12 and a radius of 12, no picture) drawn, the buffer made again, and the same frame
                // drawn again: each is the processor's pixels, corner for corner. Without the layout forgetting them, the second draw reads the new buffer's
                // bytes where the remembered tables were.
                {
                        struct compositor_frame lone;
                        unsigned int ground = 0xff102030u;
                        unsigned int bad[2] = {0, 0};

                        memory_zero(&lone, sizeof(lone));
                        lone.x = 120, lone.y = 60, lone.width = 260, lone.height = 200, lone.source_w = 260, lone.source_h = 200;
                        lone.scale = 100, lone.line = 12, lone.radius = 12, lone.edge = 0xff3366aau;
                        reference = (struct compositor_target){gpu_kit_want, PP, 696, PH, 0, 0, 696, PH, 0, 0, 0};
                        memory_fill_u32_rect(gpu_kit_want, PP, 696, PH, ground);
                        compositor_draw(&reference, &lone);
                        compositor_gscene.kept = 0, compositor_gscene.tables_used = 0;
                        for (int pass = 0; pass < 2; pass++)
                        {
                                if (pass)
                                {
                                        memory_zero((p8 address_to)gpu.at + tables_at, 1 << 20);
                                        compositor_gpu_layout();
                                        compositor_gscene.ops = ops, compositor_gscene.room = 4096;
                                        compositor_gscene.tables = (unsigned char address_to)((p8 address_to)gpu.at + tables_at), compositor_gscene.tables_at = (unsigned int)tables_at;
                                        compositor_gscene.tables_size = 1 << 20;
                                }
                                for (positive i = 0; i < (positive)PP * PH; i++)
                                        pic[i] = 0;
                                if (!compositor_gpu_frames(&lone, 1, ground) || !gpu_scene(ops_at, compositor_gscene.count, bins_at, pic_at, PP, 696, PH, 0, 0, 696, PH))
                                        bad[pass] = PP * PH;
                                else
                                        for (unsigned int y = 0; y < (unsigned int)PH; y++)
                                                for (unsigned int x = 0; x < 696; x++)
                                                        bad[pass] += pic[(positive)y * PP + x] != gpu_kit_want[(positive)y * PP + x];
                        }
                        string_format(log, "  gpu restart: %p and %p pixels differ before and after the card starts again\n", (positive)bad[0], (positive)bad[1]);
                        check("gpu restart: the frame is drawn the processor's way before the card starts again", bad[0] == 0);
                        check("gpu restart: after the card starts again its remembered tables are not read, and the frame is the processor's way", bad[1] == 0);
                }
        }

        // The snap preview's shape at the picture's edges, every small size (1 to 6 by 1 to 6) with every line (0 to 8), radius 0, a radius of 1, and one
        // past the half: a line wider than the preview is the processor's clamp (compositor_tint), not a size the records take as given. The card's
        // records against compositor_preview_draw over the whole picture, two alphas of the ink and of the edge. Not random: the same rows every run.
        {
                static const int sizes[6] = {1, 2, 3, 4, 5, 6};
                static const int radii[3] = {0, 1, 9};
                static const int points[2][2] = {{0, 0}, {693, PH - 2}};
                static const unsigned int inks[2] = {0x80c06040u, 0xffc06040u}, edges[2] = {0xb0204080u, 0xff204080u};
                struct compositor_frame lifted;
                int radii_of[1];
                positive preview_bad = 0, runs = 0;

                // The picture is at pic_at (GPU_DATA), and the compositor's own pools are at its places too, so the row's records and tables are the Frames
                // rows' (ops_at, bins_at, and a table pool beyond them), not COMPOSITOR_GPU_OPS_AT, which would lie under the picture.
                positive preview_tables_at = bins_at + (8 << 20) + (positive)7680 * 4320 * 4 + (16 << 20) + (12 << 20) + (6 << 20);

                compositor_gscene.width = 696, compositor_gscene.height = PH;
                compositor_gscene.ops = (unsigned int address_to)((p8 address_to)gpu.at + ops_at), compositor_gscene.room = (1 << 20) / 32;
                compositor_gscene.tables = (unsigned char address_to)gpu.at + preview_tables_at, compositor_gscene.tables_at = (unsigned int)preview_tables_at;
                compositor_gscene.tables_size = 1 << 20;
                for (int pw = 0; pw < 6; pw++)
                        for (int ph = 0; ph < 6; ph++)
                                for (int line = 0; line <= 8; line++)
                                        for (int rr = 0; rr < 3; rr++)
                                                for (int at = 0; at < 2; at++)
                                                        for (int ink = 0; ink < 2; ink++)
                                                                for (int edge = 0; edge < 2 && gpu.at; edge++)
                                                                {
                                                                        struct compositor_target reference = {gpu_kit_want, PP, 696, PH, 0, 0, 696, PH, 0, 0, 0};
                                                                        unsigned int ground = 0xff102030u;
                                                                        int bad = 0;

                                                                        memory_zero(&lifted, sizeof(lifted));
                                                                        lifted.scale = 100;
                                                                        lifted.preview[0] = points[at][0], lifted.preview[1] = points[at][1];
                                                                        lifted.preview[2] = sizes[pw], lifted.preview[3] = sizes[ph];
                                                                        lifted.preview_ink = inks[ink], lifted.preview_radius = radii[rr], lifted.preview_line = line;
                                                                        lifted.preview_edge = edges[edge];
                                                                        radii_of[0] = compositor_radius(&lifted);
                                                                        memory_fill_u32_rect(gpu_kit_want, PP, 696, PH, ground);
                                                                        compositor_preview_draw(&reference, &lifted, 1, radii_of);
                                                                        for (positive i = 0; i < (positive)PP * PH; i++)
                                                                                pic[i] = 0;
                                                                        compositor_gscene.kept = 0, compositor_gscene.tables_used = 0; // each scene its own tables: the cache's places are the pool's
                                                                        if (!compositor_gpu_frames(&lifted, 1, ground) ||
                                                                            !gpu_scene(ops_at, compositor_gscene.count, bins_at, pic_at, PP, 696, PH, 0, 0, 696, PH))
                                                                        {
                                                                                check("gpu preview edges: the lifted preview is the card's records and the card finishes", false);
                                                                                break;
                                                                        }
                                                                        runs++;
                                                                        for (int y = 0; y < PH; y++)
                                                                                for (int x = 0; x < 696; x++)
                                                                                {
                                                                                        if (((pic[(positive)y * PP + x] ^ gpu_kit_want[(positive)y * PP + x]) & 0x00ffffffu) && ++bad)
                                                                                        {
                                                                                                if (bad == 1)
                                                                                                        string_format(log, "  FAIL gpu preview edges: size %p by %p line %p radius %p at %p,%p: first at %p,%p, card %p, processor %p\n",
                                                                                                                      (positive)sizes[pw], (positive)sizes[ph], (positive)line, (positive)radii[rr], (positive)points[at][0],
                                                                                                                      (positive)points[at][1], (positive)x, (positive)y, (positive)pic[(positive)y * PP + x],
                                                                                                                      (positive)gpu_kit_want[(positive)y * PP + x]);
                                                                                        }
                                                                                }
                                                                        preview_bad += (positive)bad;
                                                                }
                check("gpu preview edges: every small preview with every line and radius is the processor's, pixel for pixel", preview_bad == 0 && (runs > 0 || !gpu.at));
                string_format(log, "  gpu preview edges: %p scenes, %p pixels wrong\n", runs, preview_bad);
        }
        compositor.spots_shown = 0, compositor.scale = 100;

        // 8K: a window's clear and 159 boxes of the sizes cards and buttons have, the radii a card has, a third of them translucent;
        // the scene on the GPU against ui_kit_fill laid the same way, the whole picture compared once, then each timed, best of 5.
        {
                enum { KW = 7680, KH = 4320, M = 160 };
                positive big_at = bins_at + (8 << 20), kops_at = big_at + (positive)KW * KH * 4, kbins_at = kops_at + (1 << 20);
                unsigned int address_to big = (unsigned int address_to)((p8 address_to)gpu.at + big_at), address_to kops = (unsigned int address_to)((p8 address_to)gpu.at + kops_at);
                unsigned int address_to want = (unsigned int address_to)memory((positive)KW * KH * 4);
                positive total = ~0ull, run = ~0ull, host = ~0ull, began;

                if (!want)
                        return 1;
                for (int k = 0; k < M; k++)
                {
                        unsigned int address_to q = kops + 8 * k;
                        int w = k ? 40 * 256 + (int)(gpu_random() % (1200 * 256)) : KW * 256, h = k ? 40 * 256 + (int)(gpu_random() % (800 * 256)) : KH * 256;
                        int x0 = k ? (int)(gpu_random() % ((KW - 1240) * 256)) : 0, y0 = k ? (int)(gpu_random() % ((KH - 840) * 256)) : 0;
                        int r = k ? (int)(gpu_random() % (24 * 256)) : 0;
                        unsigned int argb = k == 0 ? 0xff203040u : (gpu_random() % 3 ? gpu_random() | 0xff000000u : gpu_random());

                        q[0] = 2, q[1] = (unsigned int)x0, q[2] = (unsigned int)y0, q[3] = (unsigned int)(x0 + w), q[4] = (unsigned int)(y0 + h), q[5] = argb, q[6] = q[7] = (unsigned int)r;
                }
                ui.pixels = want, ui.pitch = KW, ui.width = KW, ui.height = KH, ui.bottom = KH;
                for (int i = 0; i < 3; i++)
                {
                        memory_zero(want, (positive)KW * KH * 4);
                        began = clock_monotonic_nanoseconds();
                        for (int k = 0; k < M; k++)
                        {
                                struct ui_box box = {(int)kops[8 * k + 1], (int)kops[8 * k + 2], (int)kops[8 * k + 3], (int)kops[8 * k + 4], (int)kops[8 * k + 6], (int)kops[8 * k + 7]};

                                ui_kit_fill(&ui, &box, kops[8 * k + 5], 0);
                        }
                        began = clock_monotonic_nanoseconds() - began;
                        host = began < host ? began : host;
                }
                memory_zero(big, (positive)KW * KH * 4);
                check("gpu kit: the 8K scene finishes", gpu_scene(kops_at, M, kbins_at, big_at, KW, KW, KH, 0, 0, KW, KH));
                for (positive i = 0; i < (positive)KW * KH; i++)
                {
                        gpu_px(big[i] != want[i], i % KW, i / KW, big[i], want[i]);
                        bad += big[i] != want[i];
                }
                {
                        positive wrong = gpu_px_end();

                        if (wrong)
                                string_format(log, "  FAIL gpu kit 8K"), gpu_px_tail(wrong);
                }
                // Damage: a rectangle drawn alone is what the whole drawing makes in it (rounded out to the kernel's patches) and leaves everything outside as it was (the scene's first op
                // is the window's clear, so its patches do not need what was under them); 24 rectangles, one the size of a line of typing.
                {
                        positive part_bad = 0, small = ~0ull;

                        for (int d = 0; d < 24; d++)
                        {
                                unsigned int x0 = d == 0 ? 3001 : gpu_random() % (KW - 1), y0 = d == 0 ? 2003 : gpu_random() % (KH - 1);
                                unsigned int x1 = x0 + (d == 0 ? 256 : 1 + gpu_random() % 600), y1 = y0 + (d == 0 ? 64 : 1 + gpu_random() % 200);

                                x1 = x1 > KW ? KW : x1, y1 = y1 > KH ? KH : y1;
                                memory_zero(big, (positive)KW * KH * 4);
                                check("gpu kit: a part finishes", gpu_scene(kops_at, M, kbins_at, big_at, KW, KW, KH, x0, y0, x1, y1));
                                // what is drawn is the rectangle rounded out to the kernel's patches of 64 pixels by 4 rows: all of that is what the whole drawing makes
                                {
                                        unsigned int rx0 = (x0 >> 6) << 6, rx1 = ((x1 + 63) >> 6) << 6, ry0 = (y0 >> 2) << 2, ry1 = ((y1 + 3) >> 2) << 2;

                                        rx1 = rx1 > KW ? KW : rx1, ry1 = ry1 > KH ? KH : ry1;
                                        for (unsigned int y = ry0; y < ry1; y++)
                                                for (unsigned int x = rx0; x < rx1; x++)
                                                {
                                                        gpu_px(big[(positive)y * KW + x] != want[(positive)y * KW + x], x, y, big[(positive)y * KW + x], want[(positive)y * KW + x]);
                                                        part_bad += big[(positive)y * KW + x] != want[(positive)y * KW + x];
                                                }
                                        {
                                                positive wrong = gpu_px_end();

                                                if (wrong)
                                                        string_format(log, "  FAIL gpu kit part %p", (positive)d), gpu_px_tail(wrong);
                                        }
                                        for (int i = 0; i < 20000; i++)
                                        {
                                                unsigned int x = gpu_random() % KW, y = gpu_random() % KH;

                                                if (x >= rx0 && x < rx1 && y >= ry0 && y < ry1)
                                                        continue;
                                                gpu_px(big[(positive)y * KW + x] != 0, x, y, big[(positive)y * KW + x], 0);
                                                part_bad += big[(positive)y * KW + x] != 0;
                                        }
                                        {
                                                positive wrong = gpu_px_end();

                                                if (wrong)
                                                        string_format(log, "  FAIL gpu kit part %p outside its rectangle", (positive)d), gpu_px_tail(wrong);
                                        }
                                }
                        }
                        for (int i = 0; i < 8; i++)
                        {
                                began = clock_monotonic_nanoseconds();
                                gpu_scene(kops_at, M, kbins_at, big_at, KW, KW, KH, 3001, 2003, 3257, 2067);
                                began = clock_monotonic_nanoseconds() - began;
                                small = began < small ? began : small;
                        }
                        string_format(log, "  gpu kit: a 256 by 64 change in the 8K scene: %p us (the binning and the run)\n", small / 1000);
                }
                for (int i = 0; i < 5; i++)
                {
                        memory_zero(big, (positive)KW * KH * 4);
                        began = clock_monotonic_nanoseconds();
                        gpu_scene(kops_at, M, kbins_at, big_at, KW, KW, KH, 0, 0, KW, KH);
                        began = clock_monotonic_nanoseconds() - began;
                        total = began < total ? began : total;
                        memory_zero(big, (positive)KW * KH * 4);
                        began = clock_monotonic_nanoseconds();
                        gpu_run();
                        began = clock_monotonic_nanoseconds() - began;
                        run = began < run ? began : run;
                }
                string_format(log, "  gpu kit: 8K scene of %p boxes: GPU with the binning %p us, the run alone %p us, CPU ui_kit_fill %p us\n", (positive)M, total / 1000, run / 1000, host / 1000);
                // Where the time goes: the clear alone; the same 160 as plain fills run through the box kernel; the boxes without the clear.
                {
                        unsigned int save[8], address_to f = (unsigned int address_to)((p8 address_to)gpu.at + kops_at);
                        positive t_clear = ~0ull, t_fills = ~0ull, t_boxes = ~0ull;

                        memory_copy(save, f, 32);
                        for (int i = 0; i < 4; i++)
                        {
                                memory_zero(big, (positive)KW * KH * 4);
                                gpu_scene(kops_at, 1, kbins_at, big_at, KW, KW, KH, 0, 0, KW, KH);
                                began = clock_monotonic_nanoseconds();
                                gpu_run();
                                began = clock_monotonic_nanoseconds() - began;
                                t_clear = began < t_clear ? began : t_clear;
                                memory_zero(big, (positive)KW * KH * 4);
                                gpu_scene(kops_at + 32, M - 1, kbins_at, big_at, KW, KW, KH, 0, 0, KW, KH);
                                began = clock_monotonic_nanoseconds();
                                gpu_run();
                                began = clock_monotonic_nanoseconds() - began;
                                t_boxes = began < t_boxes ? began : t_boxes;
                        }
                        // the same boxes all opaque, then opaque with square corners
                        {
                                positive t_opaque = ~0ull, t_square = ~0ull;

                                for (int k = 0; k < M; k++)
                                        kops[8 * k + 5] |= 0xff000000u;
                                for (int i = 0; i < 3; i++)
                                {
                                        gpu_scene(kops_at, M, kbins_at, big_at, KW, KW, KH, 0, 0, KW, KH);
                                        memory_zero(big, (positive)KW * KH * 4);
                                        began = clock_monotonic_nanoseconds();
                                        gpu_run();
                                        began = clock_monotonic_nanoseconds() - began;
                                        t_opaque = began < t_opaque ? began : t_opaque;
                                }
                                for (int k = 0; k < M; k++)
                                        kops[8 * k + 6] = kops[8 * k + 7] = 0;
                                for (int i = 0; i < 3; i++)
                                {
                                        gpu_scene(kops_at, M, kbins_at, big_at, KW, KW, KH, 0, 0, KW, KH);
                                        memory_zero(big, (positive)KW * KH * 4);
                                        began = clock_monotonic_nanoseconds();
                                        gpu_run();
                                        began = clock_monotonic_nanoseconds() - began;
                                        t_square = began < t_square ? began : t_square;
                                }
                                string_format(log, "  gpu kit: the 160 boxes all opaque %p us, opaque and square %p us\n", t_opaque / 1000, t_square / 1000);
                        }
                        // fills in the box kernel: kind 0 ops made from the same rectangles, the kernel forced to the box one
                        for (int k = 0; k < M; k++)
                        {
                                unsigned int address_to q = kops + 8 * k;

                                q[0] = 0, q[1] = (unsigned int)((int)q[1] >> 8), q[2] = (unsigned int)((int)q[2] >> 8), q[3] = (unsigned int)((int)(q[3] + 255) >> 8), q[4] = (unsigned int)((int)(q[4] + 255) >> 8);
                                q[5] |= 0xff000000u;
                        }
                        gpu_scene(kops_at, M, kbins_at, big_at, KW, KW, KH, 0, 0, KW, KH);
                        for (int i = 0; i < 4; i++)
                        {
                                GPU_ARGS->op = 5;
                                memory_zero(big, (positive)KW * KH * 4);
                                began = clock_monotonic_nanoseconds();
                                gpu_run();
                                began = clock_monotonic_nanoseconds() - began;
                                t_fills = began < t_fills ? began : t_fills;
                        }
                        {
                                unsigned int address_to tab = (unsigned int address_to)((p8 address_to)gpu.at + kbins_at);
                                positive sum = 0, most = 0, squares = (positive)((KW + 63) >> 6) * ((KH + 63) >> 6);

                                for (positive c = 0; c < squares; c++)
                                        sum += tab[2 * c + 1], most = tab[2 * c + 1] > most ? tab[2 * c + 1] : most;
                                string_format(log, "  gpu kit: the lists: %p squares, %p entries (%p a square), the longest %p\n", squares, sum, sum / squares, most);
                        }
                        string_format(log, "  gpu kit: 8K, box kernel: the clear alone %p us, 159 boxes without the clear %p us, the 160 as plain fills %p us\n", t_clear / 1000, t_boxes / 1000,
                                      t_fills / 1000);
                        memory_copy(f, save, 32);
                }
        }

        // Hostile pictures: what a window's program sets (its picture's size, and so which of the processor's pages the GPU is given) against what the GPU may
        // read. Each frame is drawn by the GPU from compositor_gpu_frames after compositor_gpu_picture (the present's order) and by compositor_draw on the processor.
        {
                positive tables_at = bins_at + (8 << 20) + (positive)7680 * 4320 * 4 + (16 << 20) + (12 << 20) + (6 << 20);
                struct compositor_target reference = {gpu_kit_want, PP, 696, PH, 0, 0, 696, PH, 0, 0, 0};
                long grown = system_call_6(syscall(mmap), 0, 1 << 20, 3, 0x4022, (positive)-1, 0), other = system_call_6(syscall(mmap), 0, 1 << 20, 3, 0x4022, (positive)-1, 0);
                long far = system_call_6(syscall(mmap), 0, 1 << 20, 3, 0x4022, (positive)-1, 0);
                struct compositor_frame f;
                unsigned int ground = 0xff102030u;
                positive hostile_bad, leaked;
                b32 refused;

                if ((grown < 0 && grown > -4096) || (other < 0 && other > -4096) || (far < 0 && far > -4096))
                        return 1;
                memory_zero(&compositor_gscene, sizeof(compositor_gscene));
                compositor_gscene.ops = ops, compositor_gscene.room = 4096, compositor_gscene.width = 696, compositor_gscene.height = PH;
                compositor_gscene.tables = (unsigned char address_to)((p8 address_to)gpu.at + tables_at), compositor_gscene.tables_at = (unsigned int)tables_at;
                compositor_gscene.tables_size = 1 << 20;
                for (positive i = 0; i < (1 << 18); i++)
                        ((unsigned int address_to)grown)[i] = gpu_random() | 0xff000000u, ((unsigned int address_to)other)[i] = 0xff5a5a5au,
                        ((unsigned int address_to)far)[i] = gpu_random() | 0xff000000u;
#define GPU_HOSTILE(what, pixels_at, pitch_of, close_of)                                                                                                                     \
        memory_zero(&f, sizeof(f));                                                                                                                                         \
        f.x = 40, f.y = 40, f.width = 200, f.height = 300, f.scale = 100, f.pitch = (pitch_of), f.pixels = (const unsigned int address_to)(pixels_at), f.close = (close_of);     \
        f.close_mean = 0x406080u;                                                                                                                                         \
        memory_fill_u32_rect(gpu_kit_want, PP, 696, PH, ground);                                                                                                            \
        compositor_draw(&reference, &f);                                                                                                                                    \
        memory_zero(pic, (positive)PP * PH * 4);                                                                                                                            \
        refused = !compositor_gpu_picture(&f) || !compositor_gpu_frames(&f, 1, ground) || !gpu_scene(ops_at, compositor_gscene.count, bins_at, pic_at, PP, 696, PH, 0, 0, 696, PH); \
        hostile_bad = leaked = 0;                                                                                                                                           \
        for (positive y = 0; !refused && y < (positive)PH; y++)                                                                                                             \
                for (positive x = 0; x < 696; x++)                                                                                                                          \
                        if (pic[y * PP + x] != gpu_kit_want[y * PP + x])                                                                                                    \
                                hostile_bad++, leaked += pic[y * PP + x] == 0xff5a5a5au;                                                                                    \
        if (refused || hostile_bad)                                                                                                                                         \
                string_format(log, "  gpu hostile: %s: %s%p pixels wrong, %p of them another window's\n", (string_address)(what), refused ? (string_address) "refused, " : (string_address) "", \
                              hostile_bad, leaked);                                                                                                                         \
        check("gpu hostile: " what, !refused && !hostile_bad)
                // A window grown past the pages first mapped for it: the first map holds the picture's first byte and not its rows below; another window's
                // pages were mapped after it, so the addresses past its end are that window's.
                check("gpu hostile: the picture's first rows are mapped", gpu_map((const void address_to)grown, 4 * 256 * 16));
                check("gpu hostile: another window's pages are mapped after them", gpu_map((const void address_to)other, 1 << 20));
                GPU_HOSTILE("a window grown past its first map is read from its own pages", grown, 256, 0);
                // A long session: the addresses handed out have come past what a close op's record holds of one (40 bits, GPU_VA_END), and a window with a
                // close button is mapped next; its map is placed below that, in the first gap.
                gpu_map_next = GPU_VA_END + GPU_VA;
                GPU_HOSTILE("a close button's halo is read from its own window after 2^40 of addresses", far, 256, 1);
                check("gpu hostile: every map ends at or below 2^40", gpu_maps[gpu_map_count - 1].va + (gpu_maps[gpu_map_count - 1].to - gpu_maps[gpu_map_count - 1].from) <= GPU_VA_END);
                // Pages the GPU was given in an earlier present that the compositor let go of since (a glass window's backdrop, a band made again at another
                // size): every job names every map, and the kernel refuses one that names a map whose pages are gone; the scene still runs.
                {
                        long gone = system_call_6(syscall(mmap), 0, 1 << 20, 3, 0x4022, (positive)-1, 0), reused = system_call_6(syscall(mmap), 0, 2 << 20, 3, 0x4022, (positive)-1, 0);

                        if ((gone < 0 && gone > -4096) || (reused < 0 && reused > -4096))
                                return 1;
                        for (positive i = 0; i < (1 << 18); i++)
                                ((unsigned int address_to)reused)[i] = gpu_random() | 0xff000000u;
                        check("gpu hostile: pages to let go of are mapped", gpu_map((const void address_to)gone, 1 << 20) && gpu_map((const void address_to)reused, 2 << 20));
                        gpu_trim(); // a present is over
                        system_call_2(syscall(munmap), (positive)gone, 1 << 20);
                        GPU_HOSTILE("a scene runs after pages the GPU was given are unmapped", grown, 256, 0);
                        gpu_forget((const void address_to)gone, 1 << 20); // let go of after, as the compositor does before it maps those addresses anew (gpu_map_hit)
                        // The tick that used a map is the one that lets it go (a glass scratch grown after a present read it): compositor_scratch's gpu_forget and munmap, with no
                        // present between. The job that follows does not name it (the kernel refuses a job that names a buffer whose pages are gone, EFAULT, three times and
                        // gives the present to the processor), and it is not a hang.
                        {
                                long scratch = system_call_6(syscall(mmap), 0, 1 << 20, 3, 0x4022, (positive)-1, 0);
                                unsigned int held = gpu_map_count;

                                if (scratch < 0 && scratch > -4096)
                                        return 1;
                                check("gpu hostile: a scratch's pages are mapped for a present", gpu_map((const void address_to)scratch, 1 << 20) && gpu_map_count == held + 1 &&
                                                                                                    gpu_maps[gpu_map_count - 1].used == gpu_tick);
                                gpu_forget((const void address_to)scratch, 1 << 20);
                                system_call_2(syscall(munmap), (positive)scratch, 1 << 20);
                                GPU_HOSTILE("a scene runs in the tick that used a map the compositor then forgot and unmapped", grown, 256, 0);
                                check("gpu hostile: no job of that tick failed", gpu_last_error == 0);
                                gpu_trim();
                        }
                        // The tail of a map's pages let go of and its head a window's picture again (an address used again): the map holds the picture, so the present's
                        // job reads it and is refused; the map is dropped with it, and the next present maps the picture afresh.
                        system_call_2(syscall(munmap), (positive)reused + (1 << 20), 1 << 20);
                        gpu_trim();
                        f.pixels = (const unsigned int address_to)reused, f.pitch = 256, f.close = 0;
                        check("gpu hostile: a job that reads a map of pages let go of is not run",
                              !(compositor_gpu_picture(&f) && compositor_gpu_frames(&f, 1, ground) && gpu_scene(ops_at, compositor_gscene.count, bins_at, pic_at, PP, 696, PH, 0, 0, 696, PH)));
                        GPU_HOSTILE("the present after one that named a map of pages let go of maps them afresh", reused, 256, 0);
                }
                // A picture of 2 GiB or more (three rows 2^28 pixels apart): the kernels' 32-bit offsets cannot reach its last row, so it is the processor's,
                // refused before a map is asked for. (On this box the map would be refused too, as no 2 GiB are mapped there; the guard does not need that.)
                {
                        struct compositor_frame big;
                        unsigned int held = gpu_map_count;

                        memory_zero(&big, sizeof(big));
                        big.x = 40, big.y = 40, big.width = 200, big.height = 3, big.scale = 100, big.pitch = 1ull << 28, big.pixels = (const unsigned int address_to)grown;
                        check("gpu hostile: a picture of 2 GiB or more is the processor's", !compositor_gpu_picture(&big) && gpu_map_count == held);
                        big.pitch = 1ull << 40;
                        check("gpu hostile: a pitch whose product would wrap is the processor's", !compositor_gpu_picture(&big) && gpu_map_count == held);
                }
#undef GPU_HOSTILE
        }

        // The scanout made again twice with no present of the GPU's between (the mode between is one the GPU does not compose: a width that is not a multiple
        // of 4, or the setting off), by the compositor's own compositor_scanout_make and _free on the card's display node: the card gives the lowest free
        // handle, so the third buffer has the first's, by which the GPU's import of the first is known. The GPU's present must draw into the third.
        {
                char path[] = "/dev/dri/card0", name[8] = "";
                int card = -1;

                for (int n = 0; n < 8 && card < 0; n++)
                {
                        p64 ver[8] = {0, 0, 7, (p64)name};

                        path[13] = (char)('0' + n), name[0] = 0;
                        card = (int)system_open_at(AT_FDCWD, (string_address)path, 2 | O_CLOEXEC);
                        if (card >= 0 && !(system_control(card, 3u << 30 | 64u << 16 | 0x6400u, ver) == 0 && string_equals((string_address)name, (string_address) "amdgpu")))
                                system_close(card), card = -1;
                }
                if (card >= 0)
                {
                        enum { DW = 256, DH = 64 };
                        unsigned int fb[3] = {0}, handle[3] = {0}, address_to pixels[3] = {0}, address_to ops_of = (unsigned int address_to)((p8 address_to)gpu.at + ops_at);
                        unsigned long bytes[3] = {0}, pitch[3] = {0};
                        positive out, stale = 0;

                        compositor.card = card;
                        check("gpu scanout: the first scanout is made", compositor_scanout_make(DW, DH, &fb[0], &handle[0], &pixels[0], &bytes[0], &pitch[0]) == 0);
                        compositor.scanout_handle = handle[0], compositor.scanout_bytes = bytes[0];
                        check("gpu scanout: the GPU imports it", (out = compositor_gpu_output()) != 0);
                        check("gpu scanout: the second is made", compositor_scanout_make(DW, DH, &fb[1], &handle[1], &pixels[1], &bytes[1], &pitch[1]) == 0);
                        compositor_scanout_free(fb[0], handle[0], pixels[0], bytes[0]);
                        check("gpu scanout: the third is made", compositor_scanout_make(DW, DH, &fb[2], &handle[2], &pixels[2], &bytes[2], &pitch[2]) == 0);
                        compositor_scanout_free(fb[1], handle[1], pixels[1], bytes[1]);
                        string_format(log, "  gpu scanout: handles %p, %p, %p\n", (positive)handle[0], (positive)handle[1], (positive)handle[2]);
                        compositor.scanout_handle = handle[2], compositor.scanout_bytes = bytes[2];
                        out = compositor_gpu_output();
                        ops_of[0] = 0, ops_of[1] = 0, ops_of[2] = 0, ops_of[3] = DW, ops_of[4] = DH, ops_of[5] = 0xff2468acu, ops_of[6] = ops_of[7] = 0;
                        check("gpu scanout: the GPU's present into the third finishes", out && gpu_scene(ops_at, 1, bins_at, out, (unsigned int)(pitch[2] / 4), DW, DH, 0, 0, DW, DH));
                        for (unsigned int y = 0; pixels[2] && y < DH; y++)
                                for (unsigned int x = 0; x < DW; x++)
                                        stale += pixels[2][(positive)y * (pitch[2] / 4) + x] != 0xff2468acu;
                        if (stale)
                                string_format(log, "  gpu scanout: %p of the third scanout's pixels are not the present's\n", stale);
                        check("gpu scanout: the GPU draws into the scanout made last, not one given back", !stale);
                        compositor_scanout_free(fb[2], handle[2], pixels[2], bytes[2]);
                        // gpu_import's refusals (the compositor asks again each present) let go of what they took: a render node that will not take the shared
                        // buffer (here a descriptor that is no render node), an address map the buffer is too small for, no room for another import. The
                        // descriptors and the handles are the lowest free ones, so one left open shows as the next descriptor's number, or as a handle that is there.
                        {
                                long nobody = system_open_at(AT_FDCWD, (string_address) "/dev/null", 2 | O_CLOEXEC), probe;
                                p64 tiny[4] = {4096, 4096, 2, 0}, close_of[1] = {0};
                                unsigned int dumb_fb = 0, dumb = 0, address_to dumb_pixels = 0, saved_imports = gpu.imports;
                                unsigned long dumb_bytes = 0, dumb_pitch = 0;
                                int real = gpu.fd, refused = 1;

                                if (nobody >= 0 && compositor_scanout_make(64, 16, &dumb_fb, &dumb, &dumb_pixels, &dumb_bytes, &dumb_pitch) == 0 && gpu_io(3u, 0, 32, tiny))
                                {
                                        close_of[0] = tiny[0];
                                        system_control(gpu.fd, 1u << 30 | 8u << 16 | 0x6409u, close_of); // its handle is the lowest free one now
                                        probe = system_open_at(AT_FDCWD, (string_address) "/dev/null", 2 | O_CLOEXEC);
                                        system_close((int)probe); // and this the lowest free descriptor
                                        gpu.fd = (int)nobody;
                                        refused &= !gpu_import(card, dumb, dumb_bytes);
                                        gpu.fd = real;
                                        refused &= !gpu_import(card, dumb, dumb_bytes * 4);
                                        gpu.imports = 8;
                                        refused &= !gpu_import(card, dumb, dumb_bytes);
                                        gpu.imports = saved_imports;
                                        check("gpu import: each refusal answers none", refused);
                                        {
                                                long next = system_open_at(AT_FDCWD, (string_address) "/dev/null", 2 | O_CLOEXEC);

                                                if (next != probe)
                                                        string_format(log, "  gpu import: the next descriptor is %p, not %p\n", (positive)next, (positive)probe);
                                                check("gpu import: a refusal leaves no descriptor open", next == probe);
                                                system_close((int)next);
                                        }
                                        check("gpu import: a refusal leaves no handle open", system_control(gpu.fd, 1u << 30 | 8u << 16 | 0x6409u, close_of) != 0);
                                        compositor_scanout_free(dumb_fb, dumb, dumb_pixels, dumb_bytes);
                                }
                                else
                                        check("gpu import: a buffer to refuse is made", false);
                                if (nobody >= 0)
                                        system_close((int)nobody);
                        }
                        compositor.card = -1;
                        system_close(card);
                }
                else
                        string_format(log, "  gpu scanout: no amdgpu display node, skipped\n");
        }

        // A window dragged larger, a row a present, in presents the GPU does not draw (none of them has damage, as a present of a landing, scaled, has
        // none the GPU draws): compositor_gpu_present maps the picture at each new size, a hundred maps and more than the 64 there are room for, and then
        // composes the window at its last size into a headless scanout, which must be compositor_draw's.
        {
                enum { SW = 696, SH = 419 };
                long tall = system_call_6(syscall(mmap), 0, 2 << 20, 3, 0x4022, (positive)-1, 0), screen = system_call_6(syscall(mmap), 0, (positive)SW * SH * 4, 3, 0x4022, (positive)-1, 0);
                struct compositor_target reference = {gpu_kit_want, PP, SW, SH, 0, 0, SW, SH, 0, 0, 0};
                struct compositor_frame f;
                positive wrong = 0;
                int drew;

                if ((tall < 0 && tall > -4096) || (screen < 0 && screen > -4096))
                        return 1;
                for (positive i = 0; i < (2 << 18); i++)
                        ((unsigned int address_to)tall)[i] = gpu_random() | 0xff000000u;
                memory_zero(&compositor_gscene, sizeof(compositor_gscene));
                compositor_gscene.ops = (unsigned int address_to)((p8 address_to)gpu.at + COMPOSITOR_GPU_OPS_AT), compositor_gscene.room = (1 << 20) / 32;
                compositor_gscene.tables = (unsigned char address_to)gpu.at + COMPOSITOR_GPU_TABLES_AT, compositor_gscene.tables_at = COMPOSITOR_GPU_TABLES_AT;
                compositor_gscene.tables_size = 1 << 20;
                compositor_gpu_state.up = 1, compositor_gpu_state.force = 1, compositor_gpu_state.dead = 0, compositor_gpu = 1, compositor.card = -1;
                compositor.width = SW, compositor.height = SH, compositor.scanout = (unsigned int address_to)screen, compositor.scanout_pitch = SW * 4;
                compositor.scanout_bytes = (positive)SW * SH * 4, compositor.cursor.drawn = 0, compositor.damages = 0;
                memory_zero(&f, sizeof(f));
                f.x = 40, f.y = 40, f.width = 200, f.scale = 100, f.pitch = 1024, f.pixels = (const unsigned int address_to)tall;
                for (f.height = 200; f.height < 300; f.height++)
                        compositor_gpu_present(&f, 1);
                compositor.damages = 1, compositor.damage[0][0] = 0, compositor.damage[0][1] = 0, compositor.damage[0][2] = SW, compositor.damage[0][3] = SH;
                drew = compositor_gpu_present(&f, 1);
                memory_fill_u32_rect(gpu_kit_want, PP, SW, SH, compositor_ground);
                compositor_draw(&reference, &f);
                for (positive y = 0; drew && y < SH; y++)
                        for (positive x = 0; x < SW; x++)
                                wrong += ((unsigned int address_to)screen)[y * SW + x] != gpu_kit_want[y * PP + x];
                if (!drew || wrong)
                        string_format(log, "  gpu presents: %s, %p pixels wrong, %p maps held\n", drew ? (string_address) "drawn" : (string_address) "not drawn", wrong, (positive)gpu_map_count);
                check("gpu presents: a window grown through a hundred maps is composed by the GPU at its last size", drew && !wrong);

                // A job given up on that may still run, by the check's fake fence (gpu_fake_busy: the wait answers "still running" while it is set; nothing on
                // the card is held, the job itself is done in microseconds). While it may run, no present writes the buffer (a record already there stays) and
                // no scene runs; once the wait says done the GPU composes again. A job the wait answers with an error (a queue's reset ended it; here a
                // sequence never given) is over, and the jobs after it go to a new context.
                {
                        unsigned int address_to records = compositor_gscene.ops, was = gpu.ctx;
                        b32 refused, kept = true, idle;

                        ops[0] = 0, ops[1] = 0, ops[2] = 0, ops[3] = 64, ops[4] = 4, ops[5] = 0xff336699u, ops[6] = ops[7] = 0;
                        gpu_fake_busy = 1, gpu_patient = 0;
                        refused = !gpu_scene(ops_at, 1, bins_at, pic_at, PP, 64, 4, 0, 0, 64, 4) && gpu.pending;
                        for (int i = 0; i < 8; i++)
                                records[i] = 0xdeadbeefu;
                        compositor.damages = 1;
                        refused = refused && !compositor_gpu_present(&f, 1) && !gpu_scene(ops_at, 1, bins_at, pic_at, PP, 64, 4, 0, 0, 64, 4) && !gpu_idle();
                        for (int i = 0; i < 8; i++)
                                kept = kept && records[i] == 0xdeadbeefu;
                        gpu_fake_busy = 0, gpu_patient = 1; // the real fence: the job may still be on the card for some microseconds
                        for (positive began = clock_monotonic_nanoseconds(); !(idle = gpu_idle()) && clock_monotonic_nanoseconds() - began < 1000000000ull;)
                                ;
                        check("gpu pending: a scene whose job is not seen done is given up on, and the job held as pending", refused);
                        check("gpu pending: while it may run, the present draws on the processor and writes no record", kept);
                        check("gpu pending: once the wait says done the buffer is the processor's again", idle && !gpu.pending);
                        check("gpu pending: and the GPU composes the next present", compositor_gpu_present(&f, 1) == 1);
                        gpu.pending = 1ull << 40;
                        check("gpu pending: a job the wait answers with an error is over, on a new context", gpu_idle() && !gpu.pending && gpu.ctx != was);
                        pic[0] = 0;
                        check("gpu pending: the new context runs a scene", gpu_scene(ops_at, 1, bins_at, pic_at, PP, 64, 4, 0, 0, 64, 4) && pic[0] == 0xff336699u);
                }
                // A hang (a job two seconds old and still running; the fake fence says running, and the job's age is set back) is the processor's present, and the card
                // is given up for two seconds: the presents in them are the processor's, and when the cool-down is over the card is started again once with no maps
                // (gpu_recover), and the next present draws. A second hang within a minute gives the card up for the session: the processor's presents from then on,
                // and canvas gpu turned off and on does not bring it back (compositor_gpu_settings). A job that is only late is the processor's present and no more.
                {
                        unsigned int recovered = gpu_recovered;
                        int cooling, drew;

                        gpu_fake_errno = 0, gpu_found = GPU_END_NONE, gpu_end_said = 0; // (a failed wait an earlier row left is not this job's)
                        gpu_fake_busy = 1, gpu_patient = 0;
                        check("gpu hang: a job still running at the late cap is the processor's present, held as pending, with no cool-down",
                              !compositor_gpu_present(&f, 1) && gpu.pending && compositor_gpu_state.cool_until == 0 && !compositor_gpu_state.restart_due);
                        gpu_job_began = clock_monotonic_nanoseconds() - GPU_HANG_NS;
                        check("gpu hang: a hang is the processor's present, and cools the card for two seconds to start again with no maps",
                              !compositor_gpu_present(&f, 1) && compositor_gpu_state.cool_until > clock_monotonic_nanoseconds() && compositor_gpu_state.restart_due &&
                              compositor_gpu_state.failures == 0 && !compositor_gpu_state.dead);
                        gpu_fake_busy = 0, gpu_patient = 1;
                        for (positive began = clock_monotonic_nanoseconds(); !gpu_idle() && clock_monotonic_nanoseconds() - began < 1000000000ull;)
                                ; // the real job the fake said was running is over before a made-up one is put in its place (on a loaded box it may not be)
                        gpu.pending = 1ull << 40;
                        gpu_idle(); // the hung job is over (a new context: the card reset)
                        cooling = !compositor_gpu_present(&f, 1);
                        check("gpu hang: while it cools, the present is the processor's and the card is not started", cooling && gpu_recovered == recovered);
                        compositor_gpu_state.cool_until = 1; // the cool-down is over
                        for (positive began = clock_monotonic_nanoseconds(); !gpu_idle() && clock_monotonic_nanoseconds() - began < 1000000000ull;)
                                ; // the last hung job is over (the wait is answered at once only once it is)
                        drew = compositor_gpu_present(&f, 1) == 1;
                        check("gpu hang: when the cool-down is over the card starts again once, with no maps, and draws", drew && gpu_recovered == recovered + 1 &&
                              !compositor_gpu_state.restart_due);
                        gpu_fake_busy = 1, gpu_patient = 0;
                        compositor_gpu_present(&f, 1); // late
                        gpu_job_began = clock_monotonic_nanoseconds() - GPU_HANG_NS;
                        check("gpu hang: a second hang within a minute gives the card up for the session", !compositor_gpu_present(&f, 1) && compositor_gpu_state.dead &&
                              compositor_gpu_state.hung_off && compositor_gpu_state.dead_why == COMPOSITOR_GPU_WHY_HANG);
                        gpu_fake_busy = 0, gpu_patient = 1;
                        for (positive began = clock_monotonic_nanoseconds(); !gpu_idle() && clock_monotonic_nanoseconds() - began < 1000000000ull;)
                                ; // the real job the fake said was running is over before a made-up one is put in its place (on a loaded box it may not be)
                        gpu.pending = 1ull << 40;
                        gpu_idle();
                        compositor_gpu = 0;
                        compositor_gpu_settings(); // turned off and on again: the person's way back (compositor_gpu_word since ab935cd87)
                        check("gpu hang: turning canvas gpu off and on brings back a card given up for the session after its hangs (the person asked)",
                              compositor_gpu && !compositor_gpu_state.dead && !compositor_gpu_state.hung_off && !compositor_gpu_state.hung_at);
                        compositor_gpu_state.dead = 0, compositor_gpu_state.hung_off = 0, compositor_gpu_state.hung_at = 0, compositor_gpu_state.cool_until = 0;
                        compositor_gpu_state.restart_due = 0, compositor_gpu_state.cool_steps = 0;
                }

                compositor_gpu = 1;
                compositor_gpu_settings();
                for (positive began = clock_monotonic_nanoseconds(); !gpu_idle() && clock_monotonic_nanoseconds() - began < 1000000000ull;)
                        ;
                check("gpu settings: read again while on, a GPU that is not cooling is not changed by reading", compositor_gpu && !compositor_gpu_state.dead && compositor_gpu_present(&f, 1) == 1);
                compositor_gpu = 0;
                compositor_gpu_settings();
                for (positive began = clock_monotonic_nanoseconds(); !gpu_idle() && clock_monotonic_nanoseconds() - began < 1000000000ull;)
                        ;
                check("gpu settings: turned on again, it composes the next present", compositor_gpu && !compositor_gpu_state.dead && compositor_gpu_present(&f, 1) == 1);
                compositor_gpu_state.up = 0, compositor_gpu_state.dead = 1, compositor_gpu = 0;
                compositor_gpu_settings();
                check("gpu settings: a GPU that never started is looked for again when it is turned on", !compositor_gpu_state.dead);
                compositor_gpu_state.dead = 0;
                compositor.damages = 0, compositor_gpu_state.up = 0;
        }

        // Why the card is refused a present is counted, and the processor's presents with it, in the table moonwater canvas reads: a width the card does not
        // draw (695) is refused as an alignment, a present it draws is counted as the card's, and the counts are the table's, not only the compositor's.
        {
                static struct windows *why_table_scratch;
                struct windows *const why_table_at = windows_scratch_table(&why_table_scratch);
                struct compositor_frame f;
                unsigned int drawn0, processor0, align0, align1;
                int drew_now, refused_wide;
                enum { WW = 695, WH = 40 };
                long screen_mem = system_call_6(syscall(mmap), 0, (positive)WW * WH * 4, 3, 0x4022, (positive)-1, 0);

                if (screen_mem < 0 && screen_mem > -4096)
                        return 1;
                // The card is taken as started when the rows above started it (gpu.at is its buffer); with none, the present at the width the card draws is
                // not asked of it, and the refusal above is still the processor's.
                compositor_gpu_state.up = gpu.at != 0, compositor_gpu_state.force = 1, compositor_gpu_state.dead = 0, compositor_gpu = 1, compositor.card = -1;
                compositor.width = WW, compositor.height = WH, compositor.scanout = (unsigned int address_to)screen_mem, compositor.scanout_pitch = 2780;
                compositor.scanout_bytes = (positive)WW * WH * 4, compositor.cursor.drawn = 0, compositor.damages = 1;
                compositor.damage[0][0] = 0, compositor.damage[0][1] = 0, compositor.damage[0][2] = WW, compositor.damage[0][3] = WH;
                windows_scratch_zero(why_table_at);
                compositor.table = why_table_at;
                compositor_gpu_counts.drawn = compositor_gpu_counts.processor = 0;
                memory_zero(compositor_gpu_counts.why, sizeof(compositor_gpu_counts.why));
                memory_zero(&f, sizeof(f));
                f.x = 4, f.y = 4, f.width = 20, f.height = 20, f.scale = 100, f.builtin = 1, f.slot = -2, f.line = 0, f.pitch = 0;
                refused_wide = !compositor_gpu_present(&f, 1);
                align0 = why_table_at->gpu_refused[COMPOSITOR_GPU_WHY_ALIGN];
                processor0 = why_table_at->gpu_processor;
                drawn0 = why_table_at->gpu_drawn;
                check("gpu why: a scanout pitch that is not 16 bytes is refused", refused_wide);
                check("gpu why: the refusal is counted as an alignment reason", align0 == 1);
                check("gpu why: and as the processor's present, not the card's", processor0 == 1 && drawn0 == 0);
                check("gpu why: the table's counts are the compositor's", why_table_at->gpu_refused[COMPOSITOR_GPU_WHY_ALIGN] == compositor_gpu_counts.why[COMPOSITOR_GPU_WHY_ALIGN] && why_table_at->gpu_processor == compositor_gpu_counts.processor);
                // A snap preview on a window itself (not lifted into a frame of its own) is the processor's: its ring and its hide are not records.
                if (gpu.at)
                {
                        static unsigned int why_pixels[32 * 32];
                        struct compositor_frame win;
                        int refused_preview;

                        memory_zero(&win, sizeof(win));
                        win.x = 4, win.y = 4, win.width = 20, win.height = 20, win.scale = 100, win.pitch = 32, win.pixels = why_pixels, win.line = 1;
                        win.preview[0] = 2, win.preview[1] = 2, win.preview[2] = 10, win.preview[3] = 10, win.preview_ink = 0x80ffffffu;
                        gpu_map(why_pixels, sizeof(why_pixels));
                        compositor_gscene.width = WW, compositor_gscene.height = WH;
                        refused_preview = !compositor_gpu_frames(&win, 1, compositor_ground) && compositor_gscene.why == COMPOSITOR_GPU_WHY_PREVIEW;
                        check("gpu why: a snap preview on a window itself is refused as a preview", refused_preview);
                        win.preview[0] = 0, win.preview[1] = 0, win.preview[2] = 0, win.preview[3] = 0;
                        check("gpu why: and the same window without it is composed", compositor_gpu_frames(&win, 1, compositor_ground));
                }
                if (gpu.at) // a card: the same present at the width it draws is the card's, and counted so
                {
                        compositor.width = WW, compositor.scanout_pitch = 2784; // 695 wide: the card draws 692 of it, the processor the rest
                        drew_now = compositor_gpu_present(&f, 1);
                        align1 = why_table_at->gpu_refused[COMPOSITOR_GPU_WHY_ALIGN];
                        check("gpu why: a width of 695 is drawn by the card to 692, and counted as its present", drew_now == 1 && why_table_at->gpu_drawn == 1 && why_table_at->gpu_processor == 1 && align1 == 1);
                }
                else
                        string_format(log, "  gpu why: no amdgpu card, the present at the width it draws is not asked of it\n");
                compositor.table = 0;
                compositor.damages = 0;
        }

        // Whether the card composes in earnest (the lists go with it): sixteen presents in a row refused for want of its ability put it out, a scene it does not draw yet
        // (a landing, a preview) does not count, one present it draws starts the count again, and thirty-two in a row that it drew bring it back.
        {
                int a, b, c, d, e, f2, g2;

                compositor_gpu_able = 1;
                for (int i = 0; i < 15; i++)
                        compositor_gpu_able_count(COMPOSITOR_GPU_WHY_PICTURE);
                a = compositor_gpu_able;
                compositor_gpu_able_count(COMPOSITOR_GPU_WHY_DRAWN);
                for (int i = 0; i < 15; i++)
                        compositor_gpu_able_count(COMPOSITOR_GPU_WHY_OUTPUT);
                b = compositor_gpu_able;
                for (int i = 0; i < 100; i++)
                        compositor_gpu_able_count(i & 1 ? COMPOSITOR_GPU_WHY_SCALED : COMPOSITOR_GPU_WHY_PREVIEW);
                c = compositor_gpu_able;
                compositor_gpu_able_count(COMPOSITOR_GPU_WHY_OUTPUT);
                d = compositor_gpu_able;
                for (int i = 0; i < 31; i++)
                        compositor_gpu_able_count(COMPOSITOR_GPU_WHY_DRAWN);
                compositor_gpu_able_count(COMPOSITOR_GPU_WHY_BUSY);
                for (int i = 0; i < 31; i++)
                        compositor_gpu_able_count(COMPOSITOR_GPU_WHY_DRAWN);
                e = compositor_gpu_able;
                compositor_gpu_able_count(COMPOSITOR_GPU_WHY_DRAWN);
                f2 = compositor_gpu_able;
                for (int i = 0; i < 40; i++)
                        compositor_gpu_able_count(COMPOSITOR_GPU_WHY_DRAWN);
                g2 = compositor_gpu_able;
                check("gpu able: fifteen presents refused for want of the card's ability leave it in", a == 1);
                check("gpu able: one present it drew starts the count again", b == 1);
                check("gpu able: a landing or a preview is not a want of ability", c == 1);
                check("gpu able: the sixteenth in a row puts it out", d == 0);
                check("gpu able: thirty-one that it drew, one refused and thirty-one more leave it out", e == 0);
                check("gpu able: thirty-two in a row that it drew bring it back", f2 == 1 && g2 == 1);
                compositor_gpu_able = 1;
        }

        // A width that is not a multiple of 4 (695): the card draws the columns below 692, and the processor the three past them (compositor_present's strip),
        // which together are the whole picture the processor draws. The picture is a window with a line and round corners over the ground.
        {
                enum { SW = 695, SH = 60, STRIDE = 696, PAGE = 256 };
                long screen_mem = system_call_6(syscall(mmap), 0, (positive)STRIDE * SH * 4, 3, 0x4022, (positive)-1, 0);
                long picture_mem = system_call_6(syscall(mmap), 0, (positive)PAGE * 64 * 4, 3, 0x4022, (positive)-1, 0);
                struct compositor_target reference = {gpu_kit_want, PP, SW, SH, 0, 0, SW, SH, 0, 0, 0};
                struct compositor_frame f;
                int rows[2], drew = 0;
                positive wrong = 0;

                if ((screen_mem < 0 && screen_mem > -4096) || (picture_mem < 0 && picture_mem > -4096))
                        return 1;
                for (positive i = 0; i < (positive)PAGE * 64; i++)
                        ((unsigned int address_to)picture_mem)[i] = gpu_random() | 0xff000000u;
                if (gpu_map((const void address_to)picture_mem, (positive)PAGE * 64 * 4) == 0)
                        return 1;
                memory_zero(&f, sizeof(f));
                f.x = 40, f.y = 10, f.width = 200, f.height = 40, f.scale = 100, f.radius = 8, f.line = 2, f.edge = 0xff405060u;
                f.pitch = PAGE, f.pixels = (const unsigned int address_to)picture_mem;
                memory_fill_u32_rect(gpu_kit_want, PP, SW, SH, compositor_ground);
                compositor_draw(&reference, &f);
                compositor_gpu_state.up = gpu.at != 0, compositor_gpu_state.force = 1, compositor_gpu_state.dead = 0, compositor_gpu = 1, compositor.card = -1;
                compositor.width = SW, compositor.height = SH, compositor.scanout = (unsigned int address_to)screen_mem, compositor.scanout_pitch = STRIDE * 4;
                compositor.scanout_bytes = (positive)STRIDE * SH * 4, compositor.cursor.drawn = 0, compositor.damages = 1;
                compositor.damage[0][0] = 0, compositor.damage[0][1] = 0, compositor.damage[0][2] = SW, compositor.damage[0][3] = SH;
                memory_zero(compositor.scanout, (positive)STRIDE * SH * 4);
                if (gpu.at)
                {
                        struct compositor_target strip = {(unsigned int address_to)screen_mem, STRIDE, SW, SH, SW & ~3, 0, SW, SH, 0, 0, 0};

                        drew = compositor_gpu_present(&f, 1);
                        rows[0] = 0, rows[1] = SH - 1;
                        compositor_scene_banded(&strip, &f, 1, 0, rows);
                        for (positive y = 0; drew && y < SH; y++)
                                for (positive x = 0; x < SW; x++)
                                        wrong += ((unsigned int address_to)screen_mem)[y * STRIDE + x] != gpu_kit_want[y * PP + x];
                        if (!drew || wrong)
                                string_format(log, "  gpu strip: %s, %p pixels wrong\n", drew ? (string_address) "drawn" : (string_address) "not drawn", wrong);
                        check("gpu strip: a width of 695, the card's columns and the processor's strip, is the processor's picture", drew && !wrong);
                }
                else
                        string_format(log, "  gpu strip: no amdgpu card, skipped\n");
                compositor.damages = 0;
                compositor.width = 696;
        }

        // A busy desktop: eighty windows (more than the maps there were), each its own picture, and the band of the tabs, all held in one tick. Every picture is mapped (no room is
        // refused), and the frames of the whole desktop are composed as records by the card's compose, with the band's tabs over them.
        {
                enum { WINS = 80, SIDE = 48, DW = 696, DH = 419 }; // more than the 64 maps there were, and forty windows and more are what a desktop has
                long pictures = system_call_6(syscall(mmap), 0, (positive)WINS * SIDE * SIDE * 4, 3, 0x4022, (positive)-1, 0);
                long band = system_call_6(syscall(mmap), 0, (positive)DW * 48 * 4, 3, 0x4022, (positive)-1, 0);
                struct compositor_frame desk[WINS + 6];
                int mapped = 1;
                unsigned int composed = 0;

                if ((pictures < 0 && pictures > -4096) || (band < 0 && band > -4096))
                        return 1;
                memory_fill_u32((unsigned int address_to)pictures, (positive)WINS * SIDE * SIDE, 0xff3060a0u);
                memory_fill_u32((unsigned int address_to)band, (positive)DW * 48, 0xff202020u);
                while (gpu_map_count)
                        gpu_drop(0, GPU_GONE_RECOVER);
                for (int k = 0; k < WINS; k++)
                {
                        memory_zero(&desk[k], sizeof(desk[k]));
                        desk[k].x = (k % 10) * 68 + 4, desk[k].y = (k / 10) * 40 + 40, desk[k].width = SIDE, desk[k].height = SIDE, desk[k].scale = 100;
                        desk[k].radius = 8, desk[k].line = 2, desk[k].edge = 0xff405060u;
                        desk[k].pitch = SIDE, desk[k].pixels = (const unsigned int address_to)pictures + (positive)k * SIDE * SIDE;
                        mapped &= compositor_gpu_picture(&desk[k]);
                }
                for (int t = 0; t < 6; t++) // six tabs of the band, each a slice of it
                {
                        memory_zero(&desk[WINS + t], sizeof(desk[0]));
                        desk[WINS + t].builtin = 1, desk[WINS + t].slot = t - 2, desk[WINS + t].x = t * 110, desk[WINS + t].y = 0, desk[WINS + t].width = 100;
                        desk[WINS + t].height = 48, desk[WINS + t].scale = 100;
                }
                compositor_gscene.width = DW, compositor_gscene.height = DH;
                compositor_gscene.ops = (unsigned int address_to)((p8 address_to)gpu.at + COMPOSITOR_GPU_OPS_AT), compositor_gscene.room = (1 << 20) / 32;
                compositor_gscene.tables = (unsigned char address_to)gpu.at + COMPOSITOR_GPU_TABLES_AT, compositor_gscene.tables_at = COMPOSITOR_GPU_TABLES_AT;
                compositor_gscene.tables_size = 1 << 20, compositor_gscene.kept = compositor_gscene.tables_used = 0;
                composed = compositor_gpu_frames(desk, WINS + 6, compositor_ground);
                check("gpu desktop: eighty windows and a band, each mapped in one tick, are all in the room there is", mapped && gpu_map_count >= WINS);
                check("gpu desktop: the frames of eighty windows and the band are composed as records (no room refused)", composed && compositor_gscene.count > WINS * 4);
                if (!composed || !mapped)
                        string_format(log, "  gpu desktop: %s, %p maps held, the scene's why %p\n", composed ? (string_address) "composed" : (string_address) "refused",
                                      (positive)gpu_map_count, (positive)compositor_gscene.why);
                while (gpu_map_count)
                        gpu_drop(0, GPU_GONE_RECOVER);
        }

        // What the maps cost a present: every job names every map, and the kernel looks at the pages of each userptr buffer it is named at every
        // submission. A job of one 64 by 4 fill, best and median of 31, with no maps; the two buffers of five 1920 by 1080 windows of a 3840-wide
        // screen (the window table's rows); those and a band of 34 tabs, 48 rows of 3840, each tab a map of its own slice; and the windows and the tabs
        // as compositor_gpu_picture maps them (the band as one map); and the ten window buffers held but not used by the present (a window's other buffer,
        // a window not shown), which the job does not name.
        {
                enum { RUNS = 31, TABS = 34, BAND_ROWS = 48, WIDE = 3840 };
                positive window_bytes = (positive)WIDE * 1080 * 4, band_bytes = (positive)WIDE * BAND_ROWS * 4, took[RUNS], best[5], middle[5];
                long windows = system_call_6(syscall(mmap), 0, 10 * window_bytes, 3, 0x4022, (positive)-1, 0), band = system_call_6(syscall(mmap), 0, band_bytes, 3, 0x4022, (positive)-1, 0);

                if ((windows < 0 && windows > -4096) || (band < 0 && band > -4096))
                        return 1;
                memory_fill_u32((unsigned int address_to)windows, 10 * window_bytes / 4, 0xff202020u);
                memory_fill_u32((unsigned int address_to)band, band_bytes / 4, 0xff303030u);
                for (int way = 0; way < 5; way++)
                {
                        while (gpu_map_count)
                                gpu_drop(0, GPU_GONE_RECOVER);
                        for (int k = 0; way && k < 10; k++)
                                gpu_map((const void address_to)(windows + (long)k * (long)window_bytes), 4 * ((positive)WIDE * 1079 + 1920));
                        for (int t = 0; way == 2 && t < TABS; t++) // a tab's slice: from its left edge, every row of the band, its width
                                gpu_map((const void address_to)(band + 4 * t * (WIDE / TABS)), 4 * ((positive)WIDE * (BAND_ROWS - 1) + WIDE / TABS));
                        if (way == 3) // the tabs as the present maps them: compositor_gpu_picture of each tab's frame, the band the compositor's
                        {
                                unsigned int address_to was_band = compositor.band;
                                unsigned long was_bytes = compositor.band_bytes;
                                int tabs_mapped = 1;

                                compositor.band = (unsigned int address_to)band, compositor.band_bytes = band_bytes;
                                for (int t = 0; t < TABS; t++)
                                {
                                        struct compositor_frame tab;

                                        memory_zero(&tab, sizeof(tab));
                                        tab.width = WIDE / TABS, tab.height = BAND_ROWS, tab.pitch = WIDE, tab.scale = 100;
                                        tab.pixels = (const unsigned int address_to)band + t * (WIDE / TABS);
                                        tabs_mapped &= compositor_gpu_picture(&tab);
                                }
                                compositor.band = was_band, compositor.band_bytes = was_bytes;
                                if (gpu_map_count != 11)
                                        string_format(log, "  gpu cost: %p maps for ten window buffers and 34 tabs\n", (positive)gpu_map_count);
                                check("gpu cost: the tabs of a band are one map, the band's", tabs_mapped && gpu_map_count == 11);
                        }
                        if (way == 4)
                                gpu_trim(); // a present is over: the maps are held, and this one uses none of them
                        for (int run = 0; run < RUNS; run++)
                        {
                                positive began;

                                ops[0] = 0, ops[1] = 0, ops[2] = 0, ops[3] = 64, ops[4] = 4, ops[5] = 0xff336699u, ops[6] = ops[7] = 0;
                                began = clock_monotonic_nanoseconds();
                                gpu_scene(ops_at, 1, bins_at, pic_at, PP, 64, 4, 0, 0, 64, 4);
                                took[run] = clock_monotonic_nanoseconds() - began;
                        }
                        for (int i = 1; i < RUNS; i++) // sorted, for the median
                                for (int j = i; j > 0 && took[j - 1] > took[j]; j--)
                                {
                                        positive keep = took[j];

                                        took[j] = took[j - 1], took[j - 1] = keep;
                                }
                        best[way] = took[0], middle[way] = took[RUNS / 2];
                        if (way == 2)
                                string_format(log, "  gpu cost: %p maps held for the windows and the tabs\n", (positive)gpu_map_count);
                }
                string_format(log, "  gpu cost: a job with no maps %p us (median %p), ten window buffers %p (%p), and 34 tab slices %p (%p), the tabs as the present maps them %p (%p), ten held and none used %p (%p)\n",
                              best[0] / 1000, middle[0] / 1000, best[1] / 1000, middle[1] / 1000, best[2] / 1000, middle[2] / 1000, best[3] / 1000, middle[3] / 1000,
                              best[4] / 1000, middle[4] / 1000);
                while (gpu_map_count)
                        gpu_drop(0, GPU_GONE_RECOVER);
        }
        // The launcher at 150, 200 and 300 percent, in the 8K section's regions (its picture, its ops and its bins), after that section has run.
        {
                static const int larger[3] = {150, 200, 300};
                positive big = bins_at + (8 << 20), kops = big + (positive)7680 * 4320 * 4, kbins = kops + (1 << 20);

                app_rows(0, big, kops, kbins, kbins + (4 << 20), 4u << 20, larger, 3, 0);
                {
                        static const int four[4] = {100, 150, 200, 300};

                        app_rows(1, big, kops, kbins, kbins + (4 << 20), 4u << 20, four, 4, 0);
                        app_rows(2, big, kops, kbins, kbins + (4 << 20), 4u << 20, four, 4, 0);
                        app_rows(3, big, kops, kbins, kbins + (4 << 20), 4u << 20, four, 4, 0);
                        for (app_group = 0; app_group < settings_groups(); app_group++)
                                app_rows(4, big, kops, kbins, kbins + (4 << 20), 4u << 20, four, 4, 0);
                        {
                                static const int gallery_scales[4] = {100, 150, 200, 300};

                                app_rows(5, big, kops, kbins, kbins + (4 << 20), 4u << 20, gallery_scales, 4, 0);
                        }
                        {
                                static const int popmenu_scales[4] = {100, 150, 200, 300};

                                for (app_group = 0; app_group < 4; app_group++)
                                        app_rows(6, big, kops, kbins, kbins + (4 << 20), 4u << 20, popmenu_scales, 4, 0);
                        }
                        {
                                static const int find_scales[4] = {100, 150, 200, 300};

                                for (app_group = 0; app_group < 3; app_group++)
                                        app_rows(7, big, kops, kbins, kbins + (4 << 20), 4u << 20, find_scales, 4, 0);
                        }
                        {
                                static const int editor_scales[4] = {100, 150, 200, 300};

                                for (app_group = 0; app_group < 3; app_group++)
                                        app_rows(8, big, kops, kbins, kbins + (4 << 20), 4u << 20, editor_scales, 4, 0);
                        }
                }
        }

        // The bench: what a present costs the card, for a desktop of twelve 800 by 600 windows over four tabs of the taskbar and the ground, composed into a
        // screen of 1080p, 4K and 8K, for a caret's damage (4 by 20 pixels at the middle) and for the whole screen. Each figure is the median of nine runs
        // of gpu_scene (the binning and the job, its wait included); the best is the first. Then a tiny job (one 64 by 4 fill) back to back, and the same job
        // after half a second idle (the card's clock ramps up from idle): the median of nine and of five.
        {
                enum { WINS = 12, RUNS = 9, IDLE_RUNS = 5, WW = 800, WH = 600 };
                static const int sizes[3][2] = {{1920, 1080}, {3840, 2160}, {7680, 4320}};
                static const string_address bench_names[3] = {(string_address) "1080p", (string_address) "4K", (string_address) "8K"};
                positive base = bins_at + (8 << 20), took[RUNS], tiny[RUNS], idle[IDLE_RUNS];
                long pictures = system_call_6(syscall(mmap), 0, (positive)WINS * WW * WH * 4, 3, 0x4022, (positive)-1, 0);
                struct compositor_frame desk[WINS + 4];

                if (pictures < 0 && pictures > -4096)
                        return 1;
                memory_fill_u32((unsigned int address_to)pictures, (positive)WINS * WW * WH, 0xff3a6ea5u);
                for (int k = 0; k < WINS; k++)
                        gpu_map((const void address_to)(pictures + (long)k * WW * WH * 4), (positive)WW * WH * 4);
                for (int size = 0; size < 3; size++)
                {
                        int W = sizes[size][0], H = sizes[size][1];

                        for (int k = 0; k < WINS; k++)
                        {
                                memory_zero(&desk[k], sizeof(desk[k]));
                                desk[k].x = (k % 4) * (W / 4) + W / 16, desk[k].y = (k / 4) * (H / 4) + H / 12, desk[k].width = WW, desk[k].height = WH;
                                desk[k].scale = 100, desk[k].radius = 10, desk[k].line = 2, desk[k].edge = 0xff405060u, desk[k].pitch = WW;
                                desk[k].pixels = (const unsigned int address_to)pictures + (positive)k * WW * WH;
                        }
                        for (int t = 0; t < 4; t++)
                        {
                                memory_zero(&desk[WINS + t], sizeof(desk[0]));
                                desk[WINS + t].builtin = 1, desk[WINS + t].slot = t - 2, desk[WINS + t].x = t * (W / 4), desk[WINS + t].y = H - 48;
                                desk[WINS + t].width = W / 4, desk[WINS + t].height = 48, desk[WINS + t].scale = 100;
                        }
                        compositor_gscene.width = (unsigned int)W, compositor_gscene.height = (unsigned int)H;
                        compositor_gscene.ops = (unsigned int address_to)((p8 address_to)gpu.at + COMPOSITOR_GPU_OPS_AT), compositor_gscene.room = (1 << 20) / 32;
                        compositor_gscene.tables = (unsigned char address_to)gpu.at + COMPOSITOR_GPU_TABLES_AT, compositor_gscene.tables_at = COMPOSITOR_GPU_TABLES_AT;
                        compositor_gscene.tables_size = 1 << 20, compositor_gscene.kept = compositor_gscene.tables_used = 0;
                        if (!compositor_gpu_frames(desk, WINS + 4, compositor_ground))
                        {
                                check("gpu bench: the desktop's records are the card's", false);
                                break;
                        }
                        for (int which = 0; which < 2; which++)
                        {
                                unsigned int x0 = which ? 0 : (unsigned int)W / 2, y0 = which ? 0 : (unsigned int)H / 2;
                                unsigned int x1 = which ? (unsigned int)W : x0 + 4, y1 = which ? (unsigned int)H : y0 + 20;
                                int finished = 1;

                                for (int run = 0; run < RUNS; run++)
                                {
                                        positive began = clock_monotonic_nanoseconds();

                                        finished &= gpu_scene(COMPOSITOR_GPU_OPS_AT, compositor_gscene.count, COMPOSITOR_GPU_BINS_AT, base, (unsigned int)W,
                                                              (unsigned int)W, (unsigned int)H, x0, y0, x1, y1);
                                        took[run] = clock_monotonic_nanoseconds() - began;
                                }
                                gpu_bench_sort(took, RUNS);
                                check("gpu bench: the desktop's scene finishes", finished);
                                string_format(log, "  gpu bench: %s, %s damage, %p records: median %p us, best %p us\n", bench_names[size],
                                              which ? (string_address) "the whole screen" : (string_address) "a caret", (positive)compositor_gscene.count, took[RUNS / 2] / 1000, took[0] / 1000);
                        }
                }
                // a tiny job: one fill of 64 by 4 pixels, steady back to back, then after half a second of idle
                ops[0] = 0, ops[1] = 0, ops[2] = 0, ops[3] = 64, ops[4] = 4, ops[5] = 0xff336699u, ops[6] = ops[7] = 0;
                for (int run = 0; run < RUNS; run++)
                {
                        positive began = clock_monotonic_nanoseconds();

                        gpu_scene(ops_at, 1, bins_at, pic_at, PP, 64, 4, 0, 0, 64, 4);
                        tiny[run] = clock_monotonic_nanoseconds() - began;
                }
                for (int run = 0; run < IDLE_RUNS; run++)
                {
                        positive began;

                        for (began = clock_monotonic_nanoseconds(); clock_monotonic_nanoseconds() - began < 500000000ull;)
                                ; // half a second with the card idle
                        began = clock_monotonic_nanoseconds();
                        gpu_scene(ops_at, 1, bins_at, pic_at, PP, 64, 4, 0, 0, 64, 4);
                        idle[run] = clock_monotonic_nanoseconds() - began;
                }
                // the same after idle, with the wake an input event makes (gpu_wake at the event, then 5 ms of the program's handling, then the
                // commit's job): what the keep-alive buys the first present after a pause
                {
                        positive woken[IDLE_RUNS];

                        for (int run = 0; run < IDLE_RUNS; run++)
                        {
                                positive began;

                                for (began = clock_monotonic_nanoseconds(); clock_monotonic_nanoseconds() - began < 500000000ull;)
                                        ;
                                gpu_wake();
                                for (began = clock_monotonic_nanoseconds(); clock_monotonic_nanoseconds() - began < 5000000ull;)
                                        ;
                                began = clock_monotonic_nanoseconds();
                                gpu_scene(ops_at, 1, bins_at, pic_at, PP, 64, 4, 0, 0, 64, 4);
                                woken[run] = clock_monotonic_nanoseconds() - began;
                        }
                        gpu_bench_sort(woken, IDLE_RUNS);
                        gpu_bench_sort(idle, IDLE_RUNS);
                        string_format(log, "  gpu bench: after half a second idle with the input's wake, median %p us (best %p us), without it median %p us (best %p us)\n",
                                      woken[IDLE_RUNS / 2] / 1000, woken[0] / 1000, idle[IDLE_RUNS / 2] / 1000, idle[0] / 1000);
                }
                gpu_bench_sort(tiny, RUNS);
                gpu_bench_sort(idle, IDLE_RUNS);
                string_format(log, "  gpu bench: a tiny job back to back, median %p us (best %p); after half a second idle, median %p us (best %p)\n",
                              tiny[RUNS / 2] / 1000, tiny[0] / 1000, idle[IDLE_RUNS / 2] / 1000, idle[0] / 1000);
                // The copy bench (wayland_host.c's card copy of a damage rectangle, against the processor's copy): a square of 64 to 1024 on a side, which
                // is 4k to 1M pixels, at a pitch of 1024 pixels. Each is the processor's memory_copy_rect_u32 (the median of nine), the card's gpu_blit
                // (its job's wait included) back to back, the card awake, and after half a second idle (the clock ramps up from idle), the median of five.
                // The card's copy is compared with the processor's, byte for byte, for each size: a row of its own.
                {
                        enum { COPY_RUNS = 9, COPY_IDLE = 5, COPY_SIZES = 5, COPY_PITCH = 1024 };
                        static const unsigned int side[COPY_SIZES] = {64, 128, 256, 512, 1024};
                        positive copy_cpu[COPY_RUNS], copy_warm[COPY_RUNS], copy_idle[COPY_IDLE], warm_cpu[COPY_RUNS], cpu_mark;
                        // The source is 128 MiB, and each copy reads a 4 MiB slot of it the last copy did not: cold on both sides (a client's buffer
                        // is not in the cache either), not the one warm rectangle, which the processor would copy from its own cache.
                        long area = system_call_6(syscall(mmap), 0, (128 + 8) << 20, 3, 0x4022, (positive)-1, 0);
                        unsigned int address_to from, address_to into;
                        b32 same = true, mapped;
                        unsigned int slot = 0;

                        if (area < 0 && area > -4096)
                                return 1;
                        from = (unsigned int address_to)area, into = from + (128 << 20) / 4;
                        memory_fill_u32(from, (128 << 20) / 4, 0x5a3c1e7fu);
                        memory_fill_u32(into, (8 << 20) / 4, 0);
                        mapped = gpu_map((const void address_to)from, 128 << 20) && gpu_map_written((const void address_to)into, 8 << 20);
                        if (!mapped) // a copy named by no map reads the card's address 0: every row after it would fail on a card the fault reset
                                string_format(log, "  gpu copy bench: the maps were refused (step %p, error %p)\n", (positive)(gpu_map_why & 255), (positive)(gpu_map_why >> 8));
                        check("gpu copy: the bench's source and destination are mapped for the card", mapped);
                        for (int size = 0; mapped && size < COPY_SIZES; size++)
                        {
                                unsigned int w = side[size], h = side[size], bytes = 4 * (COPY_PITCH * (h - 1) + w);
                                unsigned int address_to src;
                                positive dva = gpu_va_written((const void address_to)into, bytes), sva;

                                for (int run = 0; run < COPY_RUNS; run++)
                                {
                                        positive began;

                                        src = from + (unsigned long)(slot++ % 32) * (1u << 20);
                                        began = clock_monotonic_nanoseconds();
                                        memory_copy_rect_u32(into, COPY_PITCH, src, COPY_PITCH, w, h);
                                        copy_cpu[run] = clock_monotonic_nanoseconds() - began;
                                }
                                for (int run = 0; run < COPY_RUNS; run++)
                                {
                                        positive began;
                                        b32 done;

                                        src = from + (unsigned long)(slot++ % 32) * (1u << 20);
                                        sva = gpu_va_bytes((const void address_to)src, bytes);
                                        began = clock_monotonic_nanoseconds();
                                        cpu_mark = thread_cpu_nanoseconds();
                                        done = gpu_blit(sva, COPY_PITCH, dva, COPY_PITCH, 0, 0, w, h);
                                        warm_cpu[run] = thread_cpu_nanoseconds() - cpu_mark;
                                        copy_warm[run] = clock_monotonic_nanoseconds() - began;
                                        check("gpu copy: the card copies a rectangle", done);
                                }
                                src = from + (unsigned long)(slot % 32) * (1u << 20), sva = gpu_va_bytes((const void address_to)src, bytes);
                                gpu_blit(sva, COPY_PITCH, dva, COPY_PITCH, 0, 0, w, h); // the card's copy: its destination is the rectangle of the source
                                for (unsigned int row = 0; row < h; row++)
                                        same &= !memory_compare(into + row * COPY_PITCH, src + row * COPY_PITCH, 4 * w);
                                check("gpu copy: the card's copy of the rectangle is the processor's, byte for byte", same);
                                for (int run = 0; run < COPY_IDLE; run++)
                                {
                                        positive began;

                                        for (began = clock_monotonic_nanoseconds(); clock_monotonic_nanoseconds() - began < 500000000ull;)
                                                ;
                                        src = from + (unsigned long)(slot++ % 32) * (1u << 20);
                                        sva = gpu_va_bytes((const void address_to)src, bytes);
                                        began = clock_monotonic_nanoseconds();
                                        gpu_blit(sva, COPY_PITCH, dva, COPY_PITCH, 0, 0, w, h);
                                        copy_idle[run] = clock_monotonic_nanoseconds() - began;
                                }
                                gpu_bench_sort(copy_cpu, COPY_RUNS);
                                gpu_bench_sort(copy_warm, COPY_RUNS);
                                gpu_bench_sort(copy_idle, COPY_IDLE);
                                gpu_bench_sort(warm_cpu, COPY_RUNS);
                                string_format(log, "  gpu copy bench: %p px (%p by %p): processor median %p us, card back to back median %p us (best %p) with %p us of the compositor's own CPU, card after half a second idle median %p us (best %p)\n",
                                              (positive)w * h, (positive)w, (positive)h, copy_cpu[COPY_RUNS / 2] / 1000, copy_warm[COPY_RUNS / 2] / 1000,
                                              copy_warm[0] / 1000, warm_cpu[COPY_RUNS / 2] / 1000, copy_idle[COPY_IDLE / 2] / 1000, copy_idle[0] / 1000);
                        }
                }
                while (gpu_map_count)
                        gpu_drop(0, GPU_GONE_RECOVER);
        }
        gpu_pool_rows();
        gpu_preamble_rows();
        gpu_loss_rows();
        gpu_lent_rows();
        gpu_held_rows();
        gpu_resume_rows();
        gpu_vmfault_live_row();
        return test_report(null);
}
#endif /* CHECK_gpu_kit */


