/*
        test/checks/enhance.c: the picture network routines of lib.c (conv3x3_f32, u8_to_f32, picture_quads_f32,
        picture_quads_add_f32) and the wallpaper enhancer built on them.

        A run of test/checks.c, which includes this file where the run stood: it is compiled only as part of that
        file, with its statics and macros, and never on its own. CHECK_enhance is freestanding, built as test/checks.c
        is (gcc -O2 -static -nostdlib -nostartfiles -march=x86-64 -w -DCHECK_enhance -fwhole-program -o OUT
        test/checks.c); test/run's enhance lane runs it on x86_64 once per tier with the feature bytes walked down and
        on riscv64 under qemu with no V and at VLEN 128 to 1024.
*/

#ifdef CHECK_enhance
#include "../../src/lib.util.c"
#include "../../src/canvas/enhance.c"

#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

/*
        Each routine against a model in double precision (conv3x3_f32) or exact arithmetic (the byte routines). Every
        array ends against a page that faults and the output has sentinels in front of it, so a read or a write past
        either end is caught; the inputs are compared with copies afterwards, so nothing but the output is written.

        conv3x3_f32: widths 1 to 14 and then in steps to 40 (every tile and every tail of each machine), in_channels
        from 0 to 48 and out_channels from 1 to 65 (every full and short panel), with and without the ReLU. Random
        floats are held to the bound of a serial sum, |got - want| <= (9 in + 2) eps S + 1e-30 with S the sum of
        the terms' magnitudes; small integers, whose products and sums are exact, to equality. The ReLU gives plus
        zero for a negative sum, a minus zero and a NaN.
*/
#define ENHANCE_REGION_FLOATS 32768
static float *enhance_region[6];
static unsigned long long enhance_state = 0x9e3779b97f4a7c15ull;

static unsigned long long enhance_next(void)
{
        enhance_state ^= enhance_state << 13, enhance_state ^= enhance_state >> 7, enhance_state ^= enhance_state << 17;
        return enhance_state;
}

// A float in [-1, 1), or a small integer in [-3, 3].
static float enhance_value(p8 integers)
{
        unsigned long long r = enhance_next();

        return integers ? (float)((int)(r % 7) - 3) : (float)((double)(r >> 11) / 4503599627370496.0 - 1.0);
}

static p8 enhance_guard(void)
{
        positive span = (ENHANCE_REGION_FLOATS * 4 + 4095) / 4096 * 4096;

        for (int at = 0; at < 6; at++)
        {
                p8 address_to pages = memory(span + 4096);

                if (!pages || system_call_3(syscall(mprotect), (positive)(pages + span), 4096, 0) != 0)
                        return false;
                enhance_region[at] = (float *)(pages + span) - ENHANCE_REGION_FLOATS;
        }
        return true;
}

// n floats ending at region at's faulting page.
static float *enhance_end(int at, positive n)
{
        return enhance_region[at] + ENHANCE_REGION_FLOATS - n;
}

static double enhance_abs(double v)
{
        return v < 0 ? -v : v;
}

static p8 enhance_first;

static fn enhance_conv(float *out, const float *const *rows, const float *w, const float *b, positive width,
                       positive cin, positive cout, p32 relu)
{
        VECTOR_FIRST_CALL(enhance_first);
        conv3x3_f32(out, rows, w, b, width, cin, cout, relu);
}

typedef struct
{
        positive values, outside, written, cases;
} enhance_faults;

static fn enhance_conv_case(positive width, positive cin, positive cout, p32 relu, p8 integers, enhance_faults *f)
{
        positive row_floats = (width + 2) * cin, wn = 9 * cin * cout;
        float *rows[3], *w = enhance_end(3, wn), *b = enhance_end(4, cout), *out = enhance_end(5, width * cout);
        static float copy[3][4096], wcopy[32768], bcopy[128];

        for (int r = 0; r < 3; r++)
        {
                rows[r] = enhance_end(r, row_floats);
                for (positive i = 0; i < row_floats; i++)
                        copy[r][i] = rows[r][i] = enhance_value(integers);
        }
        for (positive i = 0; i < wn; i++)
                wcopy[i] = w[i] = enhance_value(integers);
        for (positive i = 0; i < cout; i++)
                bcopy[i] = b[i] = enhance_value(integers);
        for (positive i = 0; i < 16; i++)
                out[-1 - (bipolar)i] = 1234.5f;
        enhance_conv(out, (const float *const *)rows, w, b, width, cin, cout, relu);
        f->cases++;
        for (positive i = 0; i < 16; i++)
                f->outside += out[-1 - (bipolar)i] != 1234.5f;
        for (int r = 0; r < 3; r++)
                for (positive i = 0; i < row_floats; i++)
                        f->written += rows[r][i] != copy[r][i];
        for (positive i = 0; i < wn; i++)
                f->written += w[i] != wcopy[i];
        for (positive i = 0; i < cout; i++)
                f->written += b[i] != bcopy[i];
        for (positive x = 0; x < width; x++)
                for (positive o = 0; o < cout; o++)
                {
                        double sum = b[o], size = enhance_abs(b[o]);

                        for (int r = 0; r < 3; r++)
                                for (positive j = 0; j < 3 * cin; j++)
                                {
                                        double term = (double)rows[r][x * cin + j] * (double)w[(r * 3 * cin + j) * cout + o];

                                        sum += term, size += enhance_abs(term);
                                }
                        if (relu && !(sum > 0))
                                sum = 0, size = 0;
                        float got = out[x * cout + o];
                        double bound = integers ? 0 : (double)(9 * cin + 2) * 5.9604644775390625e-8 * size + 1e-30;

                        f->values += !(enhance_abs((double)got - sum) <= bound) || (relu && __builtin_signbit(got));
                }
}

static fn enhance_conv_cases(enhance_faults *f)
{
        static const positive cins[] = {0, 1, 2, 3, 4, 5, 7, 12, 13, 16, 32, 33, 48};
        static const positive couts[] = {1, 2, 3, 4, 5, 7, 8, 9, 12, 15, 16, 17, 24, 31, 32, 33, 47, 48, 64, 65};
        positive turn = 0;

        for (positive width = 1; width <= 40; width += width < 14 ? 1 : 7)
                for (positive a = 0; a < sizeof(cins) / sizeof(cins[0]); a++)
                        for (positive c = 0; c < sizeof(couts) / sizeof(couts[0]); c++)
                                for (p32 relu = 0; relu < 2; relu++)
                                        enhance_conv_case(width, cins[a], couts[c], relu, (p8)(turn++ & 1), f);
}

// The ReLU's edges: a sum of minus zero, a NaN and a negative are plus zero; nothing else changes. No in channels,
// so each sum is its bias exactly (a minus zero plus a product of zeros would be plus zero already).
static p8 enhance_conv_relu_edges(void)
{
        positive cin = 0, cout = 3, width = 2;
        float *rows[3], *w = enhance_end(3, 9 * cin * cout), *b = enhance_end(4, cout), *out = enhance_end(5, width * cout);
        p8 holds = true;

        for (int r = 0; r < 3; r++)
        {
                rows[r] = enhance_end(r, (width + 2) * cin);
                for (positive i = 0; i < (width + 2) * cin; i++)
                        rows[r][i] = 0.0f;
        }
        for (positive i = 0; i < 9 * cin * cout; i++)
                w[i] = 0.0f;
        b[0] = -0.0f, b[1] = __builtin_nanf(""), b[2] = -2.0f;
        for (p32 relu = 0; relu < 2; relu++)
        {
                enhance_conv(out, (const float *const *)rows, w, b, width, cin, cout, relu);
                for (positive x = 0; x < width; x++)
                        holds &= relu ? out[x * 3] == 0.0f && !__builtin_signbit(out[x * 3]) && out[x * 3 + 1] == 0.0f &&
                                            !__builtin_signbit(out[x * 3 + 1]) && out[x * 3 + 2] == 0.0f && !__builtin_signbit(out[x * 3 + 2])
                                      : __builtin_signbit(out[x * 3]) && __builtin_isnan(out[x * 3 + 1]) && out[x * 3 + 2] == -2.0f;
        }
        //      A width of 0 and an out_channels of 0 touch nothing.
        out[0] = 7.0f;
        enhance_conv(out, (const float *const *)rows, w, b, 0, cin, cout, 1);
        enhance_conv(out, (const float *const *)rows, w, b, width, cin, 0, 1);
        return holds && out[0] == 7.0f;
}

/*
        The byte routines: u8_to_f32 at every count to 100 over random bytes; picture_quads_f32 at every quad count to
        40 over random words; picture_quads_add_f32 the same with residuals from -300 to 300, halves that make exact
        ties (each must round to even), NaN (0), and both infinities (255 and 0), the alpha always 0xff.
*/
static int enhance_nearest_even(float v)
{
        int t = (int)v;
        float frac = v - (float)t;

        return frac > 0.5f || (frac == 0.5f && (t & 1)) ? t + 1 : t;
}

static p32 enhance_add_model(p32 word, const float *r)
{
        p32 made = 0xff000000u;

        for (int b = 0; b < 3; b++)
        {
                float v = (float)((word >> (8 * b)) & 255) + r[b];

                v = v > 0 ? v : 0;      // a NaN fails the test and is 0
                v = v < 255 ? v : 255;
                made |= (p32)enhance_nearest_even(v) << (8 * b);
        }
        return made;
}

static p8 enhance_bytes_hold(void)
{
        p8 holds = true;
        p8 *bytes = (p8 *)enhance_end(0, 32);
        float *out = enhance_end(1, 100);

        for (positive n = 0; n <= 100; n++)
        {
                p8 *in = (p8 *)enhance_end(0, 0) - n;

                for (positive i = 0; i < n; i++)
                        in[i] = (p8)enhance_next();
                out = enhance_end(1, n);
                out[-1] = 99.0f;
                u8_to_f32(out, in, n);
                for (positive i = 0; i < n; i++)
                        holds &= out[i] == (float)in[i];
                holds &= out[-1] == 99.0f;
        }
        (void)bytes;
        return holds;
}

static p8 enhance_quads_hold(void)
{
        p8 holds = true;

        for (positive n = 0; n <= 40; n++)
        {
                p32 *top = (p32 *)enhance_end(0, 2 * n), *bottom = (p32 *)enhance_end(1, 2 * n);
                float *out = enhance_end(2, 16 * n);

                for (positive i = 0; i < 2 * n; i++)
                        top[i] = (p32)enhance_next(), bottom[i] = (p32)enhance_next();
                out[-1] = 99.0f;
                picture_quads_f32(out, top, bottom, n);
                for (positive q = 0; q < n; q++)
                        for (int p = 0; p < 4; p++)
                        {
                                p32 word = p < 2 ? top[2 * q + p] : bottom[2 * q + p - 2];

                                for (int b = 0; b < 4; b++)
                                        holds &= out[16 * q + 4 * p + b] == (float)((word >> (8 * b)) & 255);
                        }
                holds &= out[-1] == 99.0f;
        }
        return holds;
}

static p8 enhance_quads_add_hold(void)
{
        p8 holds = true;
        static const float special[] = {0.5f, 1.5f, -0.5f, -1.5f, 2.5f, 0.49999997f, 0.50000006f, 255.5f, -255.5f};

        for (positive n = 0; n <= 40; n++)
                for (int kind = 0; kind < 3; kind++)
                {
                        p32 *top = (p32 *)enhance_end(0, 2 * n), *bottom = (p32 *)enhance_end(1, 2 * n);
                        float *res = enhance_end(2, 16 * n);
                        static p32 was_top[80], was_bottom[80];

                        for (positive i = 0; i < 2 * n; i++)
                                was_top[i] = top[i] = (p32)enhance_next(), was_bottom[i] = bottom[i] = (p32)enhance_next();
                        for (positive i = 0; i < 16 * n; i++)
                        {
                                unsigned long long r = enhance_next();

                                res[i] = kind == 0   ? (float)((double)(r >> 11) / 9007199254740992.0 * 600.0 - 300.0)
                                         : kind == 1 ? special[r % (sizeof(special) / sizeof(special[0]))]
                                                     : (r % 3 == 0 ? __builtin_nanf("") : r % 3 == 1 ? __builtin_inff() : -__builtin_inff());
                        }
                        top[-1] = bottom[-1] = 0x12345678u;
                        picture_quads_add_f32(top, bottom, res, n);
                        for (positive q = 0; q < n; q++)
                                for (int p = 0; p < 4; p++)
                                {
                                        p32 was = p < 2 ? was_top[2 * q + p] : was_bottom[2 * q + p - 2];
                                        p32 got = p < 2 ? top[2 * q + p] : bottom[2 * q + p - 2];

                                        holds &= got == enhance_add_model(was, res + 16 * q + 4 * p);
                                }
                        holds &= top[-1] == 0x12345678u && bottom[-1] == 0x12345678u;
                }
        return holds;
}


/*
        picture_lut3d_u32 against its definition in integers: random tables over a stride through all 2^24 colours (every
        alpha kept), the identity table giving every colour back exactly, and counts 0 to 40 with sentinels.
*/
static p32 enhance_lut_model(p32 word, const p16 *lut)
{
        int v[3] = {(int)(word >> 16) & 255, (int)(word >> 8) & 255, (int)word & 255}, cell[3], f[3];
        static const int stride[3] = {324, 18, 1};
        int key[3];

        for (int c = 0; c < 3; c++)
                cell[c] = v[c] / 15 > 16 ? 16 : v[c] / 15, f[c] = v[c] - 15 * cell[c], key[c] = f[c] << 10 | stride[c];
        for (int pass = 0; pass < 3; pass++)
                for (int i = 0; i < 2; i++)
                        if (key[i] < key[i + 1])
                        {
                                int t = key[i];

                                key[i] = key[i + 1], key[i + 1] = t;
                        }
        int base = cell[0] * 324 + cell[1] * 18 + cell[2], n1 = base + (key[0] & 1023), n2 = n1 + (key[1] & 1023), n3 = base + 343;
        int f1 = key[0] >> 10, f2 = key[1] >> 10, f3 = key[2] >> 10;
        p32 made = word & 0xff000000u;

        for (int b = 0; b < 3; b++)
        {
                int sum = (15 - f1) * lut[4 * base + b] + (f1 - f2) * lut[4 * n1 + b] + (f2 - f3) * lut[4 * n2 + b] + f3 * lut[4 * n3 + b];

                made |= (p32)((sum + 960) / 1920) << (8 * b);
        }
        return made;
}

static p8 enhance_lut_hold(p8 identity)
{
        static p16 lut[18 * 18 * 18 * 4];
        static p32 pixels[4096], want[4096];
        p8 holds = true;

        for (int r = 0; r < 18; r++)
                for (int g = 0; g < 18; g++)
                        for (int b = 0; b < 18; b++)
                        {
                                p16 *node = lut + 4 * ((r * 18 + g) * 18 + b);

                                node[0] = (p16)(identity ? b * 15 * 128 : enhance_next() % 32641);
                                node[1] = (p16)(identity ? g * 15 * 128 : enhance_next() % 32641);
                                node[2] = (p16)(identity ? r * 15 * 128 : enhance_next() % 32641);
                                node[3] = (p16)enhance_next();
                        }
#if X64
        p32 step = 3;
#else
        p32 step = 61;
#endif
        for (p32 colour = 0; colour < (1u << 24); colour += 4096 * step)
        {
                for (p32 i = 0; i < 4096; i++)
                {
                        p32 c = colour + i * step;

                        pixels[i] = (c & 0xffffffu) | (p32)enhance_next() << 24;
                        want[i] = identity ? pixels[i] : enhance_lut_model(pixels[i], lut);
                }
                picture_lut3d_u32(pixels, 4096, lut);
                for (p32 i = 0; i < 4096; i++)
                        holds &= pixels[i] == want[i];
        }
        for (positive n = 0; n <= 40; n++)
        {
                for (positive i = 0; i < n + 2; i++)
                        pixels[i] = (p32)enhance_next(), want[i] = enhance_lut_model(pixels[i], lut);
                p32 before = pixels[0], after = pixels[n + 1];

                picture_lut3d_u32(pixels + 1, n, lut);
                for (positive i = 1; i <= n; i++)
                        holds &= pixels[i] == want[i];
                holds &= pixels[0] == before && pixels[n + 1] == after;
        }
        return holds;
}


/*
        The enhancer's runtime (src/canvas/enhance.c) on a file of random weights for the architecture it was written
        for, made here with a fixed seed (the real model is never in this tree): the parser takes it and refuses every
        changed copy (a byte short or long, the magic, the version, the count, a kind, a layer's channels, a ReLU of 2,
        a NaN or an infinity among the floats, a misaligned buffer); a fixed picture gives a fixed picture, the same
        whether one thread runs it or many, and the same bytes on every machine whose multiply-add is fused (SSE2's is
        a multiply and an add, so its bytes are held within one of them); every side from 1 to 9 and odd ones run.
*/
#define ENHANCE_TEST_HASH 0xaa6c03b2e804d671ull
static p8 *enhance_file;
static positive enhance_file_size;

static fn enhance_make_file(void)
{
        positive size = enhance_file_bytes(enhance_shapes, ENHANCE_SHAPES), at = 16;

        enhance_file_size = size;
        enhance_file = (p8 *)memory(size + 64);
        memory_copy(enhance_file, ENHANCE_MAGIC, 8);
        p32 words[3] = {ENHANCE_VERSION, (p32)ENHANCE_SHAPES, 0};

        memory_copy(enhance_file + 8, words, 8);
        for (positive n = 0; n < ENHANCE_SHAPES; n++)
        {
                const enhance_shape *sh = enhance_shapes + n;
                p32 head[4] = {sh->kind, sh->shuffle, sh->up, sh->count};

                memory_copy(enhance_file + at, head, 16), at += 16;
                for (p32 l = 0; l < sh->count; l++, at += 12)
                        memory_copy(enhance_file + at, sh->layers[l], 12);
        }
        enhance_state = 0x2545f4914f6cdd1dull;
        for (positive n = 0; n < ENHANCE_SHAPES; n++)
                for (p32 l = 0; l < enhance_shapes[n].count; l++)
                {
                        const p32 *ly = enhance_shapes[n].layers[l];
                        positive count = (ly[2] >= 2 ? 1 : 9) * (positive)ly[0] * ly[1];
                        float scale = l + 1 == enhance_shapes[n].count ? 0.5f : 1.0f / (float)(3 * ly[0]);

                        for (positive i = 0; i < count + ly[1]; i++, at += 4)
                        {
                                float v = enhance_value(false) * (i < count ? scale : 0.05f);

                                memory_copy(enhance_file + at, &v, 4);
                        }
                }
}

static p64 enhance_hash(const p32 *pixels, positive count)
{
        p64 h = 0xcbf29ce484222325ull;

        for (positive i = 0; i < count; i++)
                h = (h ^ pixels[i]) * 0x100000001b3ull;
        return h;
}

// A picture with ramps, edges and noise, the same on every machine.
static fn enhance_test_picture(p32 *pixels, positive w, positive h)
{
        p64 st = 0x8a5cd789635d2dffull;

        for (positive y = 0; y < h; y++)
                for (positive x = 0; x < w; x++)
                {
                        st ^= st << 13, st ^= st >> 7, st ^= st << 17;
                        p32 r = (p32)(x * 255 / w), g = (p32)((y * 255 / h + (x / 8 % 2) * 40) & 255), b = (p32)((st >> 20) & 63) + (x > w / 2 ? 150 : 20);

                        pixels[y * w + x] = 0xff000000u | r << 16 | g << 8 | b;
                }
}

static p8 enhance_refuses(positive at, p32 value)
{
        enhance_model m;
        p8 *copy = (p8 *)memory(enhance_file_size + 64);
        p32 was;

        memory_copy(copy, enhance_file, enhance_file_size);
        memory_copy(&was, copy + at, 4);
        memory_copy(copy + at, &value, 4);
        p8 refused = was == value || !enhance_parse(copy, enhance_file_size, enhance_shapes, ENHANCE_SHAPES, &m);

        memory_free(copy, enhance_file_size + 64);
        return refused;
}

static p8 enhance_runtime_parse(void)
{
        enhance_model m;
        p8 holds = enhance_parse(enhance_file, enhance_file_size, enhance_shapes, ENHANCE_SHAPES, &m) && m.count == ENHANCE_SHAPES;
        positive layers0 = 16 + 16;

        holds &= !enhance_parse(enhance_file, enhance_file_size - 1, enhance_shapes, ENHANCE_SHAPES, &m);
        holds &= !enhance_parse(enhance_file, enhance_file_size + 4, enhance_shapes, ENHANCE_SHAPES, &m);
        holds &= !enhance_parse(enhance_file + 1, enhance_file_size, enhance_shapes, ENHANCE_SHAPES, &m);
        holds &= !enhance_parse(null, enhance_file_size, enhance_shapes, ENHANCE_SHAPES, &m);
        holds &= enhance_refuses(0, 0x4e45574du) && enhance_refuses(8, 2) && enhance_refuses(12, 3) && enhance_refuses(16, 9);
        holds &= enhance_refuses(20, 3) && enhance_refuses(28, 99);
        holds &= enhance_refuses(layers0, 17) && enhance_refuses(layers0 + 4, 31) && enhance_refuses(layers0 + 8, 2);
        holds &= enhance_refuses(layers0 + 12, 15);
        holds &= enhance_refuses(enhance_file_size - 4, 0x7fc00000u) && enhance_refuses(enhance_file_size - 8, 0x7f800000u);
        holds &= enhance_refuses(enhance_file_size / 2 & ~(positive)3, 0xff800000u);
        return holds;
}

#define ENHANCE_TEST_W 517
#define ENHANCE_TEST_H 301
static p32 *enhance_fused_picture;

// The fixed picture through the random weights; its bytes kept in into when into is not null.
static p64 enhance_runtime_hash(positive threads, p32 *into)
{
        enhance_model m;
        positive w = ENHANCE_TEST_W, h = ENHANCE_TEST_H;
        p32 *src = (p32 *)memory(w * h * 4), *dst = (p32 *)memory(w * h * 4);
        p64 hash = 0;

        enhance_test_picture(src, w, h);
        if (enhance_parse(enhance_file, enhance_file_size, enhance_shapes, ENHANCE_SHAPES, &m) &&
            enhance_run_net(enhance_find(&m, ENHANCE_RESTORE), dst, w, src, w, w, h, threads))
                hash = enhance_hash(dst, w * h);
        if (into)
                memory_copy(into, dst, w * h * 4);
        memory_free(src, w * h * 4), memory_free(dst, w * h * 4);
        return hash;
}

// Every side from 1 to 9 against every other, and the source left as it was.
static p8 enhance_runtime_sides(void)
{
        enhance_model m;
        p32 src[100], copy[100], dst[100];
        p8 holds = enhance_parse(enhance_file, enhance_file_size, enhance_shapes, ENHANCE_SHAPES, &m);
        const enhance_net *net = enhance_find(&m, ENHANCE_RESTORE);

        for (positive w = 1; w <= 9 && holds; w++)
                for (positive h = 1; h <= 9; h++)
                {
                        enhance_test_picture(src, w, h);
                        memory_copy(copy, src, w * h * 4);
                        holds &= enhance_run_net(net, dst, w, src, w, w, h, 1) && !memory_compare(copy, src, w * h * 4);
                        for (positive i = 0; i < w * h; i++)
                                holds &= (dst[i] >> 24) == 0xff;
                }
        holds &= !enhance_run_net(net, dst, 1, src, 1, 0, 5, 1) && !enhance_run_net(net, dst, 1, src, 1, 5, 0, 1);
        holds &= !enhance_run_net(null, dst, 1, src, 1, 1, 1, 1);
        //      The upscaler, when the shapes have one: every side doubled, the source kept, alpha 0xff.
        const enhance_net *up = enhance_find(&m, ENHANCE_UPSCALE);
        static p32 wide[4 * 100];

        for (positive w = 1; up && w <= 9 && holds; w++)
                for (positive h = 1; h <= 9; h++)
                {
                        enhance_test_picture(src, w, h);
                        memory_copy(copy, src, w * h * 4);
                        holds &= enhance_run_net(up, wide, 2 * w, src, w, w, h, 1) && !memory_compare(copy, src, w * h * 4);
                        for (positive i = 0; i < 4 * w * h; i++)
                                holds &= (wide[i] >> 24) == 0xff;
                }
        return holds;
}


/*
        The grade on the random weights: s 0 leaves a picture as it was and bakes the identity table exactly; at full
        strength every byte stays within the channel bound, 25 levels, and one more for the interpolation; the same
        bytes whether one thread grades it or many.
*/
static p8 enhance_grade_hold(void)
{
        enhance_model m;
        positive w = 300, h = 211;
        p32 *src = (p32 *)memory(w * h * 4), *one = (p32 *)memory(w * h * 4), *many = (p32 *)memory(w * h * 4);
        static p16 lut[4 * ENHANCE_LUT_NODES];
        f64 params[6] = {0.4, -0.3, 0.2, 0.3, 0.3, -0.2};
        p8 holds = enhance_parse(enhance_file, enhance_file_size, enhance_shapes, ENHANCE_SHAPES, &m) && enhance_find(&m, ENHANCE_GRADE);

        enhance_bake(params, 0, lut);
        for (int r = 0; r < 18; r++)
                for (int g = 0; g < 18; g++)
                        for (int b = 0; b < 18; b++)
                        {
                                const p16 *node = lut + 4 * ((r * 18 + g) * 18 + b);

                                holds &= node[0] == b * 1920 && node[1] == g * 1920 && node[2] == r * 1920;
                        }
        enhance_test_picture(src, w, h);
        memory_copy(one, src, w * h * 4);
        holds &= enhance_grade(&m, one, w, w, h, 0, 1) && !memory_compare(one, src, w * h * 4);
        memory_copy(one, src, w * h * 4), memory_copy(many, src, w * h * 4);
        holds &= enhance_grade(&m, one, w, w, h, 1, 1) && enhance_grade(&m, many, w, w, h, 1, 0) && !memory_compare(one, many, w * h * 4);
        for (positive i = 0; i < w * h; i++)
                for (int b = 0; b < 24; b += 8)
                {
                        int d = (int)(one[i] >> b & 255) - (int)(src[i] >> b & 255);

                        holds &= d <= 26 && d >= -26;
                }
        holds &= !enhance_grade(&m, one, w, 63, h, 1, 1) && !enhance_grade(&m, one, w, w, 63, 1, 1);
        memory_free(src, w * h * 4), memory_free(one, w * h * 4), memory_free(many, w * h * 4);
        return holds;
}

static p64 enhance_fused_hash;

static fn enhance_runtime_tier(string_address tier, p8 fused)
{
        p32 *picture = (p32 *)memory(ENHANCE_TEST_W * ENHANCE_TEST_H * 4);
        p64 one = enhance_runtime_hash(1, picture), many = enhance_runtime_hash(0, null);

        string_format(log, "  %s: the enhancer's fixed picture %p (threads 1) %p (threads all)\n", tier, one, many);
        check("enhance: a fixed picture through the random weights gives the same bytes on one thread and on all", one == many && one);
        if (fused)
        {
                if (!enhance_fused_hash)
                        enhance_fused_hash = one;
                check("enhance: a fixed picture gives the pinned bytes on every machine whose multiply-add is fused",
                      one == enhance_fused_hash && (!ENHANCE_TEST_HASH || one == ENHANCE_TEST_HASH));
                if (!enhance_fused_picture)
                        enhance_fused_picture = picture, picture = null;
        }
        else if (enhance_fused_picture)
        {
                positive off = 0, far = 0;

                for (positive i = 0; i < ENHANCE_TEST_W * ENHANCE_TEST_H; i++)
                        for (int b = 0; b < 24; b += 8)
                        {
                                int d = (int)(picture[i] >> b & 255) - (int)(enhance_fused_picture[i] >> b & 255);

                                off += d != 0, far += d > 1 || d < -1;
                        }
                string_format(log, "  %s: %p of %p bytes one level from the fused machines'\n", tier, off,
                              (positive)(3 * ENHANCE_TEST_W * ENHANCE_TEST_H));
                check("enhance: without a fused multiply-add every byte is within one level of the fused machines'", far == 0);
        }
        if (picture)
                memory_free(picture, ENHANCE_TEST_W * ENHANCE_TEST_H * 4);
}

static fn enhance_tier(string_address tier)
{
        enhance_faults f = {0, 0, 0, 0};

        enhance_conv_cases(&f);
        string_format(log, "  %s: conv3x3_f32 %p cases\n", tier, f.cases);
        check("conv3x3_f32: every width to 14 and to 40, in 0-48, out 1-65, ReLU or not, within the bound", f.values == 0);
        check("conv3x3_f32: sentinels before out and the pages after every array untouched", f.outside == 0);
        check("conv3x3_f32: rows, weights and bias unchanged", f.written == 0);
        check("conv3x3_f32: the ReLU makes a negative, a minus zero and a NaN plus zero; width or out 0 touch nothing",
              enhance_conv_relu_edges());
        check("u8_to_f32: every count to 100, exact, nothing outside", enhance_bytes_hold());
        check("picture_quads_f32: every quad count to 40, each quad's 16 bytes in memory order", enhance_quads_hold());
        check("picture_quads_add_f32: residuals, ties to even, NaN, infinities, alpha 0xff, nothing outside",
              enhance_quads_add_hold());
        check("picture_lut3d_u32: random tables through a stride of all 2^24 colours, alpha kept, counts to 40",
              enhance_lut_hold(false));
        check("picture_lut3d_u32: the identity table gives every colour back", enhance_lut_hold(true));
        enhance_runtime_tier(tier, !string_equals(tier, (string_address) "sse2"));
}

b32 main()
{
        check("enhance: guard pages", enhance_guard());
        if (!enhance_region[5])
                return test_report(null);
        enhance_make_file();
        check("enhance: the file of the compiled shapes parses; a byte short or long, a misaligned buffer, and every header "
              "word, channel count, ReLU and float made wrong are refused", enhance_runtime_parse());
        check("enhance: every side from 1 to 9 runs, keeps the source, gives alpha 0xff; an empty picture is refused",
              enhance_runtime_sides());
        check("enhance: the grade at 0 is the identity table and leaves the picture; at full strength within 26 levels, "
              "the same on one thread and on all; a picture under 64 a side is refused", enhance_grade_hold());
#if X64
        p8 wide512 = cpu_has_avx512, wide2 = cpu_has_avx2, fused = cpu_has_fma;

        for (p32 tier = 0; tier < 3; tier++)
        {
                cpu_has_avx512 = tier == 0 ? wide512 : 0;
                cpu_has_avx2 = tier < 2 ? wide2 : 0;
                cpu_has_fma = tier < 2 ? fused : 0;
                enhance_tier(cpu_has_avx512 ? "avx512" : cpu_has_avx2 && cpu_has_fma ? "avx2 fma" : "sse2");
        }
        cpu_has_avx512 = wide512, cpu_has_avx2 = wide2, cpu_has_fma = fused;
#elif RISCV64
        p8 vector = cpu_has_vector;

        for (p32 tier = 0; tier < 2; tier++)
        {
                cpu_has_vector = tier ? 1 : vector;
                enhance_first = tier == 0;
                enhance_tier(tier ? "no V" : vector == 2 ? "V, every call the first" : "no V, every call the first");
        }
        enhance_first = false;
        cpu_has_vector = vector;
#else
        enhance_tier("neon");
#endif
        return test_report(null);
}
#endif /* CHECK_enhance */
