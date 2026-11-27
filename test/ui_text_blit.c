/*
        Proof, security, differential fuzz, guard-page, size & speed test suite for
        src/canvas/ui/ui_text_blit.asm.c across arm64, x86-64, and riscv64.
*/
#define CANVAS_UI_TEXT_BLIT_ASM_IMPLEMENTATION
#include "../src/canvas/ui/ui_text_blit.asm.c"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if !defined(_WIN32)
#include <sys/mman.h>
#include <unistd.h>
#endif

/* ---- C reference implementations (noinline so nm --print-size measures exact .text bytes) ---- */

static inline uint32_t ref_div255(uint32_t x)
{
        return (x + 1u + (x >> 8)) >> 8;
}

static inline uint32_t ref_tone(uint32_t colour, uint32_t alpha)
{
        if (alpha > 255u)
                alpha = 255u;
        uint32_t amt = alpha + (alpha >> 7);
        uint32_t mixed = 0;
        for (uint32_t shift = 0; shift < 24; shift += 8)
                mixed |= ((((colour >> shift) & 255u) * amt + 128u) >> 8) << shift;
        return (alpha << 24) | mixed;
}

__attribute__((noinline))
uint32_t c_ref_over(uint32_t pixel, uint32_t under)
{
        uint32_t a = pixel >> 24;
        if (!a)
                return under;
        uint32_t rest = 255u - a;
        if (!rest || !under)
                return pixel;
        uint32_t out = 0;
        for (uint32_t shift = 0; shift < 32; shift += 8)
        {
                uint32_t ch = ((pixel >> shift) & 255u) + ref_div255(((under >> shift) & 255u) * rest + 127u);
                out |= (ch > 255u ? 255u : ch) << shift;
        }
        return out;
}

__attribute__((noinline))
void c_ref_cover_row(const uint32_t *src, uint8_t *dst, uint64_t width, uint32_t xor_mask)
{
        if (!src || !dst || !width)
                return;
        uint8_t xv = (uint8_t)(xor_mask & 255u);
        for (uint64_t i = 0; i < width; i++)
                dst[i] = (uint8_t)(((src[i] >> 8) & 255u) ^ xv);
}

__attribute__((noinline))
void c_ref_run_walk(const struct ui_text_blit_walk *walk)
{
        if (!walk || !walk->scratch || !walk->pixels || !walk->ramp)
                return;
        if (walk->wide <= 0 || walk->rows <= 0)
                return;
        int64_t first = walk->first < 0 ? 0 : walk->first;
        int64_t last  = walk->last > walk->wide ? walk->wide : walk->last;
        if (first >= last)
                return;

        int64_t y_min = walk->ui_top < 0 ? 0 : walk->ui_top;
        int64_t y_max = walk->ui_bottom < walk->ui_height ? walk->ui_bottom : walk->ui_height;
        if (walk->band_bottom > walk->band_top)
        {
                if (walk->band_top > y_min)
                        y_min = walk->band_top;
                if (walk->band_bottom < y_max)
                        y_max = walk->band_bottom;
        }
        int64_t r0 = y_min - walk->top;
        if (r0 < 0)
                r0 = 0;
        int64_t r1 = y_max - walk->top;
        if (r1 > walk->rows)
                r1 = walk->rows;
        if (r0 >= r1)
                return;

        uint32_t paper  = (uint32_t)walk->paper;
        uint32_t ground = (uint32_t)walk->ground;
        uint32_t flip   = (uint32_t)(walk->flip & 255u);
        uint32_t stroke = (uint32_t)walk->stroke;
        struct ui_text_ramp *ramp = walk->ramp;

        for (int64_t row = r0; row < r1; row++)
        {
                int64_t y = walk->top + row;
                const uint32_t *cov = walk->scratch + row * walk->wide;
                uint32_t *pic = walk->pixels + y * walk->pitch + walk->pen_x;
                for (int64_t col = first; col < last; col++)
                {
                        if (!stroke && cov[col] == paper)
                                continue;
                        uint32_t cover = ((cov[col] >> 8) & 255u) ^ flip;
                        if (stroke && cover < 255u)
                        {
                                uint32_t most = 0;
                                if (col > 0)
                                {
                                        uint32_t c = ((cov[col - 1] >> 8) & 255u) ^ flip;
                                        if (c > most) most = c;
                                }
                                if (col + 1 < walk->wide)
                                {
                                        uint32_t c = ((cov[col + 1] >> 8) & 255u) ^ flip;
                                        if (c > most) most = c;
                                }
                                if (row > 0)
                                {
                                        uint32_t c = ((cov[col - walk->wide] >> 8) & 255u) ^ flip;
                                        if (c > most) most = c;
                                }
                                if (row + 1 < walk->rows)
                                {
                                        uint32_t c = ((cov[col + walk->wide] >> 8) & 255u) ^ flip;
                                        if (c > most) most = c;
                                }
                                cover += (most * stroke) >> 7;
                                if (cover > 255u)
                                        cover = 255u;
                        }
                        if (!cover)
                                continue;
                        uint32_t bit = cover & 63u;
                        if (pic[col] == ground)
                        {
                                if (!((ramp->made[cover >> 6] >> bit) & 1ull))
                                {
                                        ramp->over[cover] = c_ref_over(ref_tone(ramp->colour, cover * ramp->alpha / 255u), ramp->ground);
                                        ramp->made[cover >> 6] |= 1ull << bit;
                                }
                                pic[col] = ramp->over[cover];
                                continue;
                        }
                        if (!((ramp->known[cover >> 6] >> bit) & 1ull))
                        {
                                ramp->tone[cover] = ref_tone(ramp->colour, cover * ramp->alpha / 255u);
                                ramp->known[cover >> 6] |= 1ull << bit;
                        }
                        pic[col] = c_ref_over(ramp->tone[cover], pic[col]);
                }
        }
}

/* ---- PRNG ---- */

static uint64_t rng_state = 0x9e3779b97f4a7c15ull;
static uint32_t rng_u32(void)
{
        uint64_t x = rng_state;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        rng_state = x;
        return (uint32_t)((x * 0x2545f4914f6cdd1dull) >> 32);
}

static int failures = 0;

#define CHECK(cond, ...) do { \
        if (!(cond)) { \
                fprintf(stderr, "FAIL [%s:%d]: ", __FILE__, __LINE__); \
                fprintf(stderr, __VA_ARGS__); \
                fprintf(stderr, "\n"); \
                failures++; \
                if (failures > 20) exit(1); \
        } \
} while (0)

/* ---- Test 1: ui_text_over ---- */

static void test_over(void)
{
        static const uint32_t alphas[] = {0, 1, 2, 127, 128, 254, 255};
        static const uint32_t extremes[] = {
                0x00000000u, 0x00ffffffu, 0x01000000u, 0x01010101u, 0x01ffffffu,
                0x7f000000u, 0x7f7f7f7fu, 0x7fffffffu, 0x80000000u, 0x80808080u,
                0x80ffffffu, 0xfe000000u, 0xfefefefeu, 0xfeffffffu, 0xff000000u,
                0xff123456u, 0xffffffffu
        };

        for (size_t i = 0; i < sizeof(extremes)/sizeof(extremes[0]); i++)
        {
                for (size_t j = 0; j < sizeof(extremes)/sizeof(extremes[0]); j++)
                {
                        uint32_t got = ui_text_over(extremes[i], extremes[j]);
                        uint32_t exp = c_ref_over(extremes[i], extremes[j]);
                        CHECK(got == exp, "over extreme (%08x, %08x): got=%08x exp=%08x",
                              extremes[i], extremes[j], got, exp);
                }
        }

        /* Exhaustive single-channel sweep across all (a, c_pix, c_under) combinations for key alphas */
        for (size_t ai = 0; ai < sizeof(alphas)/sizeof(alphas[0]); ai++)
        {
                uint32_t a = alphas[ai];
                for (uint32_t u = 0; u < 256; u++)
                {
                        for (uint32_t p = 0; p < 256; p += 17)
                        {
                                uint32_t pix = (a << 24) | (p << 16) | ((255u - p) << 8) | u;
                                uint32_t und = (u << 24) | (u << 16) | (u << 8) | u;
                                uint32_t got = ui_text_over(pix, und);
                                uint32_t exp = c_ref_over(pix, und);
                                CHECK(got == exp, "over sweep (%08x, %08x): got=%08x exp=%08x", pix, und, got, exp);
                        }
                }
        }

        /* Randomized differential fuzzing */
        for (int i = 0; i < 200000; i++)
        {
                uint32_t pix = rng_u32();
                uint32_t und = rng_u32();
                uint32_t got = ui_text_over(pix, und);
                uint32_t exp = c_ref_over(pix, und);
                CHECK(got == exp, "over fuzz #%d (%08x, %08x): got=%08x exp=%08x", i, pix, und, got, exp);
        }
}

/* ---- Test 2: ui_text_cover_row ---- */

static void test_cover_row(void)
{
        /* NULL and 0-width safety */
        ui_text_cover_row(NULL, NULL, 0, 0);
        ui_text_cover_row(NULL, NULL, 100, 0xff);
        uint32_t dummy_src[4] = {0};
        uint8_t dummy_dst[4] = {0xaa, 0xaa, 0xaa, 0xaa};
        ui_text_cover_row(dummy_src, NULL, 4, 0);
        ui_text_cover_row(NULL, dummy_dst, 4, 0);
        ui_text_cover_row(dummy_src, dummy_dst, 0, 0xff);
        CHECK(dummy_dst[0] == 0xaa, "cover_row touched dst on width=0");

        static const uint64_t widths[] = {0, 1, 2, 3, 4, 5, 7, 8, 15, 16, 17, 31, 32, 33, 47, 48, 49, 63, 64, 65, 127, 128, 257};
        static const uint32_t xors[]   = {0x00u, 0xffu, 0x5au, 0x1ffu, 0xdeadbe00u};

        uint32_t src_buf[300];
        uint8_t got_buf[300 + 16];
        uint8_t exp_buf[300 + 16];

        for (size_t wi = 0; wi < sizeof(widths)/sizeof(widths[0]); wi++)
        {
                uint64_t w = widths[wi];
                for (size_t xi = 0; xi < sizeof(xors)/sizeof(xors[0]); xi++)
                {
                        for (uint64_t align = 0; align < 4; align++)
                        {
                                for (uint64_t i = 0; i < w; i++)
                                        src_buf[i] = rng_u32();
                                memset(got_buf, 0xcd, sizeof(got_buf));
                                memset(exp_buf, 0xcd, sizeof(exp_buf));

                                ui_text_cover_row(src_buf, got_buf + align, w, xors[xi]);
                                c_ref_cover_row(src_buf, exp_buf + align, w, xors[xi]);

                                CHECK(memcmp(got_buf, exp_buf, sizeof(got_buf)) == 0,
                                      "cover_row mismatch w=%llu xor=%x align=%llu",
                                      (unsigned long long)w, xors[xi], (unsigned long long)align);
                        }
                }
        }
}

/* ---- Test 3: ui_text_run_walk ---- */

static void compare_walk_case(const char *label, const struct ui_text_blit_walk *tpl,
                              const uint32_t *init_pixels, size_t pix_count,
                              uint32_t colour, uint32_t alpha, uint32_t rground, int warm_twice)
{
        uint32_t *pix_asm = (uint32_t *)malloc(pix_count * sizeof(uint32_t));
        uint32_t *pix_ref = (uint32_t *)malloc(pix_count * sizeof(uint32_t));
        CHECK(pix_asm && pix_ref, "oom");
        memcpy(pix_asm, init_pixels, pix_count * sizeof(uint32_t));
        memcpy(pix_ref, init_pixels, pix_count * sizeof(uint32_t));

        struct ui_text_ramp ramp_asm, ramp_ref;
        memset(&ramp_asm, 0, sizeof(ramp_asm));
        memset(&ramp_ref, 0, sizeof(ramp_ref));
        ramp_asm.colour = ramp_ref.colour = colour;
        ramp_asm.alpha  = ramp_ref.alpha  = alpha;
        ramp_asm.ground = ramp_ref.ground = rground;
        ramp_asm.set    = ramp_ref.set    = 1;

        struct ui_text_blit_walk w_asm = *tpl, w_ref = *tpl;
        w_asm.pixels = pix_asm; w_asm.ramp = &ramp_asm;
        w_ref.pixels = pix_ref; w_ref.ramp = &ramp_ref;

        for (int pass = 0; pass <= warm_twice; pass++)
        {
                ui_text_run_walk(&w_asm);
                c_ref_run_walk(&w_ref);

                CHECK(memcmp(pix_asm, pix_ref, pix_count * sizeof(uint32_t)) == 0,
                      "run_walk pixel mismatch in %s (pass %d)", label, pass);
                CHECK(memcmp(ramp_asm.known, ramp_ref.known, sizeof(ramp_asm.known)) == 0,
                      "run_walk ramp.known mismatch in %s (pass %d)", label, pass);
                CHECK(memcmp(ramp_asm.made, ramp_ref.made, sizeof(ramp_asm.made)) == 0,
                      "run_walk ramp.made mismatch in %s (pass %d)", label, pass);
                for (int c = 0; c < 256; c++)
                {
                        if ((ramp_ref.known[c >> 6] >> (c & 63)) & 1ull)
                                CHECK(ramp_asm.tone[c] == ramp_ref.tone[c],
                                      "run_walk ramp.tone[%d] mismatch in %s: got=%08x exp=%08x",
                                      c, label, ramp_asm.tone[c], ramp_ref.tone[c]);
                        if ((ramp_ref.made[c >> 6] >> (c & 63)) & 1ull)
                                CHECK(ramp_asm.over[c] == ramp_ref.over[c],
                                      "run_walk ramp.over[%d] mismatch in %s: got=%08x exp=%08x",
                                      c, label, ramp_asm.over[c], ramp_ref.over[c]);
                }
        }

        free(pix_asm);
        free(pix_ref);
}

static void test_run_walk(void)
{
        /* NULL & degenerate safety */
        ui_text_run_walk(NULL);
        struct ui_text_blit_walk empty;
        memset(&empty, 0, sizeof(empty));
        ui_text_run_walk(&empty);

        struct ui_text_ramp dummy_ramp;
        memset(&dummy_ramp, 0, sizeof(dummy_ramp));
        uint32_t dummy_buf[4] = {0};
        empty.scratch = dummy_buf; empty.pixels = dummy_buf; empty.ramp = &dummy_ramp;
        empty.wide = 0; empty.rows = 4; empty.last = 4;
        ui_text_run_walk(&empty);
        empty.wide = 4; empty.rows = 0;
        ui_text_run_walk(&empty);
        empty.wide = -5; empty.rows = 4;
        ui_text_run_walk(&empty);
        empty.wide = 4; empty.rows = -2;
        ui_text_run_walk(&empty);
        empty.wide = 4; empty.rows = 4; empty.first = 3; empty.last = 2;
        ui_text_run_walk(&empty);

        /* Targeted edge cases */
        static const int widths[]  = {1, 2, 3, 5, 17, 33};
        static const int heights[] = {1, 2, 3, 7};
        static const uint32_t alphas[]  = {0, 1, 127, 128, 254, 255, 300};
        static const uint32_t strokes[] = {0, 1, 64, 128, 255};

        for (size_t wi = 0; wi < sizeof(widths)/sizeof(widths[0]); wi++)
        {
                for (size_t hi = 0; hi < sizeof(heights)/sizeof(heights[0]); hi++)
                {
                        int W = widths[wi], H = heights[hi];
                        int pitch = W + 7; /* pitch > width */
                        int img_h = H + 4;
                        size_t sc_n = (size_t)W * (size_t)H;
                        size_t px_n = (size_t)pitch * (size_t)img_h;
                        uint32_t *scratch = (uint32_t *)malloc(sc_n * sizeof(uint32_t));
                        uint32_t *pixels  = (uint32_t *)malloc(px_n * sizeof(uint32_t));
                        for (size_t k = 0; k < sc_n; k++)
                                scratch[k] = (rng_u32() & 3u) == 0 ? 0x00ffffffu : (rng_u32() & 0x00ffffffu);
                        uint32_t ground = 0xff202428u;
                        for (size_t k = 0; k < px_n; k++)
                                pixels[k] = (k & 1) ? ground : rng_u32();

                        struct ui_text_blit_walk w = {
                                .scratch = scratch, .wide = W, .rows = H, .top = 1,
                                .first = -2, .last = W + 3, /* deliberately out of bounds -> must clamp */
                                .pixels = NULL, .pitch = pitch, .pen_x = 2,
                                .ui_top = 0, .ui_bottom = img_h, .ui_height = img_h,
                                .band_top = 0, .band_bottom = 0,
                                .paper = 0x00ffffffu, .ground = ground,
                                .flip = 255, .stroke = strokes[(wi + hi) % 5], .ramp = NULL
                        };
                        compare_walk_case("edge", &w, pixels, px_n,
                                          0x00e8a43cu, alphas[(wi + hi) % 7], ground, 1);

                        /* Also test light ink (flip=0, paper=0) + band clipping + negative top */
                        for (size_t k = 0; k < sc_n; k++)
                                scratch[k] = (rng_u32() & 1u) ? 0u : rng_u32();
                        w.top = -1;
                        w.first = 0; w.last = W;
                        w.pen_x = 0;
                        w.paper = 0;
                        w.flip = 0;
                        w.band_top = 0; w.band_bottom = H > 1 ? H - 1 : 1;
                        compare_walk_case("light+band", &w, pixels, px_n,
                                          0x00ffffffu, 255, 0u, 1);

                        free(scratch);
                        free(pixels);
                }
        }

        /* 500 randomized differential fuzz cases */
        for (int t = 0; t < 500; t++)
        {
                int W = (int)(rng_u32() % 48) + 1;
                int H = (int)(rng_u32() % 16) + 1;
                int extra_x = (int)(rng_u32() % 12);
                int img_w = W + extra_x + 4;
                int pitch = img_w + (int)(rng_u32() % 9);
                int img_h = H + 6;
                int pen_x = (int)(rng_u32() % 11) - 4; /* can be negative */
                int first = pen_x < 0 ? -pen_x : (int)(rng_u32() % (uint32_t)(W + 1));
                int last  = W;
                if (pen_x + last > img_w)
                        last = img_w - pen_x;
                if (first < last && (rng_u32() & 3u) == 0)
                        last = first + (int)(rng_u32() % (uint32_t)(last - first + 1));

                size_t sc_n = (size_t)W * (size_t)H;
                size_t px_n = (size_t)pitch * (size_t)img_h;
                uint32_t *scratch = (uint32_t *)malloc(sc_n * sizeof(uint32_t));
                uint32_t *pixels  = (uint32_t *)malloc(px_n * sizeof(uint32_t));

                int light = (int)(rng_u32() & 1u);
                uint32_t paper = light ? 0u : ((rng_u32() & 7u) == 0 ? 0xffffffffu : 0x00ffffffu);
                uint32_t ground = (rng_u32() & 3u) == 0 ? 0u : ((rng_u32() & 3u) == 0 ? 0xffffffffu : rng_u32());
                uint32_t stroke = (rng_u32() & 1u) ? 0u : (rng_u32() % 256u);
                uint32_t alpha  = (rng_u32() % 10u == 0) ? (rng_u32() % 300u) : (rng_u32() % 256u);
                uint32_t colour = rng_u32() & 0x00ffffffu;

                for (size_t k = 0; k < sc_n; k++)
                {
                        uint32_t r = rng_u32();
                        scratch[k] = (r % 5u < 3u) ? paper : r;
                }
                for (size_t k = 0; k < px_n; k++)
                        pixels[k] = (rng_u32() & 1u) ? ground : rng_u32();

                int band_on = (int)(rng_u32() & 1u);
                int btop = (int)(rng_u32() % (uint32_t)img_h);
                int bbot = band_on ? btop + (int)(rng_u32() % (uint32_t)(img_h - btop + 1)) : btop;

                struct ui_text_blit_walk w = {
                        .scratch = scratch, .wide = W, .rows = H,
                        .top = (int)(rng_u32() % 9) - 3,
                        .first = first, .last = last,
                        .pixels = NULL, .pitch = pitch, .pen_x = pen_x,
                        .ui_top = (int)(rng_u32() % 4) - 1,
                        .ui_bottom = img_h - (int)(rng_u32() % 3),
                        .ui_height = img_h,
                        .band_top = btop, .band_bottom = bbot,
                        .paper = paper, .ground = ground,
                        .flip = light ? 0u : 255u, .stroke = stroke, .ramp = NULL
                };
                compare_walk_case("fuzz", &w, pixels, px_n, colour, alpha, ground, 0);

                free(scratch);
                free(pixels);
        }
}

/* ---- Test 4: mmap PROT_NONE guard-page tests ---- */

#if !defined(_WIN32)
static void *alloc_guarded(size_t bytes, size_t *map_size_out, int at_end)
{
        size_t ps = (size_t)sysconf(_SC_PAGESIZE);
        size_t data_pages = (bytes + ps - 1) / ps;
        if (data_pages == 0) data_pages = 1;
        size_t total = (data_pages + 2) * ps;
        uint8_t *base = (uint8_t *)mmap(NULL, total, PROT_READ | PROT_WRITE,
                                        MAP_PRIVATE | MAP_ANON, -1, 0);
        CHECK(base != MAP_FAILED, "mmap failed");
        CHECK(mprotect(base, ps, PROT_NONE) == 0, "mprotect head failed");
        CHECK(mprotect(base + (data_pages + 1) * ps, ps, PROT_NONE) == 0, "mprotect tail failed");
        *map_size_out = total;
        if (at_end)
                return base + (data_pages + 1) * ps - bytes;
        return base + ps;
}

static void free_guarded(void *ptr, size_t bytes, size_t map_size, int at_end)
{
        size_t ps = (size_t)sysconf(_SC_PAGESIZE);
        size_t data_pages = (bytes + ps - 1) / ps;
        if (data_pages == 0) data_pages = 1;
        uint8_t *base = at_end
                ? ((uint8_t *)ptr + bytes - (data_pages + 1) * ps)
                : ((uint8_t *)ptr - ps);
        munmap(base, map_size);
}

static void test_guard_pages(void)
{
        static const uint64_t widths[] = {1, 2, 3, 15, 16, 17, 31, 32, 33, 64, 65};
        for (int at_end = 0; at_end <= 1; at_end++)
        {
                for (size_t i = 0; i < sizeof(widths)/sizeof(widths[0]); i++)
                {
                        uint64_t w = widths[i];
                        size_t s_bytes = (size_t)w * 4u;
                        size_t d_bytes = (size_t)w;
                        size_t sm = 0, dm = 0;
                        uint32_t *src = (uint32_t *)alloc_guarded(s_bytes, &sm, at_end);
                        uint8_t  *dst = (uint8_t *)alloc_guarded(d_bytes, &dm, at_end);
                        uint8_t  *ref = (uint8_t *)malloc(d_bytes);
                        for (uint64_t k = 0; k < w; k++)
                                src[k] = rng_u32();
                        ui_text_cover_row(src, dst, w, 0xffu);
                        c_ref_cover_row(src, ref, w, 0xffu);
                        CHECK(memcmp(dst, ref, d_bytes) == 0, "guard cover_row w=%llu end=%d",
                              (unsigned long long)w, at_end);
                        free(ref);
                        free_guarded(src, s_bytes, sm, at_end);
                        free_guarded(dst, d_bytes, dm, at_end);
                }

                /* Guarded ui_text_run_walk: both stroke=0 (paper-skip) and stroke=128 (4-neighbor) */
                for (int st = 0; st <= 1; st++)
                {
                        int W = 33, H = 7, pitch = 41;
                        size_t sc_bytes = (size_t)W * (size_t)H * 4u;
                        size_t px_bytes = (size_t)pitch * (size_t)H * 4u;
                        size_t sm = 0, pm = 0;
                        uint32_t *sc = (uint32_t *)alloc_guarded(sc_bytes, &sm, at_end);
                        uint32_t *px = (uint32_t *)alloc_guarded(px_bytes, &pm, at_end);
                        uint32_t *px_ref = (uint32_t *)malloc(px_bytes);
                        uint32_t paper = 0x00ffffffu, ground = 0xff101418u;
                        for (int k = 0; k < W * H; k++)
                                sc[k] = (k % 3 == 0) ? paper : rng_u32();
                        for (int k = 0; k < pitch * H; k++)
                                px[k] = px_ref[k] = (k & 1) ? ground : rng_u32();

                        struct ui_text_ramp r_asm, r_ref;
                        memset(&r_asm, 0, sizeof(r_asm));
                        memset(&r_ref, 0, sizeof(r_ref));
                        r_asm.colour = r_ref.colour = 0x00abcdefu;
                        r_asm.alpha  = r_ref.alpha  = 220u;
                        r_asm.ground = r_ref.ground = ground;
                        r_asm.set    = r_ref.set    = 1;

                        struct ui_text_blit_walk w_asm = {
                                .scratch = sc, .wide = W, .rows = H, .top = 0,
                                .first = 0, .last = W, .pixels = px, .pitch = pitch, .pen_x = 0,
                                .ui_top = 0, .ui_bottom = H, .ui_height = H,
                                .band_top = 0, .band_bottom = 0,
                                .paper = paper, .ground = ground, .flip = 255,
                                .stroke = st ? 128 : 0, .ramp = &r_asm
                        };
                        struct ui_text_blit_walk w_ref = w_asm;
                        w_ref.pixels = px_ref; w_ref.ramp = &r_ref;

                        ui_text_run_walk(&w_asm);
                        c_ref_run_walk(&w_ref);
                        CHECK(memcmp(px, px_ref, px_bytes) == 0, "guard run_walk st=%d end=%d", st, at_end);

                        free(px_ref);
                        free_guarded(sc, sc_bytes, sm, at_end);
                        free_guarded(px, px_bytes, pm, at_end);
                }
        }
}
#endif

/* ---- Benchmark ---- */

static uint64_t now_ns(void)
{
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void run_bench(void)
{
        /* 1. ui_text_over */
        volatile uint32_t sink = 0;
        const int N_OVER = 10000000;
        uint32_t acc = 0x12345678u;
        uint64_t t0 = now_ns();
        for (int i = 0; i < N_OVER; i++)
                acc = ui_text_over(0x80336699u ^ (uint32_t)(i & 0x7fu), acc);
        uint64_t t1 = now_ns();
        sink ^= acc;

        acc = 0x12345678u;
        uint64_t t2 = now_ns();
        for (int i = 0; i < N_OVER; i++)
                acc = c_ref_over(0x80336699u ^ (uint32_t)(i & 0x7fu), acc);
        uint64_t t3 = now_ns();
        sink ^= acc;
        double ns_over_asm = (double)(t1 - t0) / N_OVER;
        double ns_over_ref = (double)(t3 - t2) / N_OVER;

        /* 2. ui_text_cover_row (width=256) */
        uint32_t csrc[256];
        uint8_t  cdst[256];
        for (int i = 0; i < 256; i++) csrc[i] = rng_u32();
        const int N_COV = 500000;
        t0 = now_ns();
        for (int i = 0; i < N_COV; i++)
                ui_text_cover_row(csrc, cdst, 256, 0xffu);
        t1 = now_ns();
        sink ^= cdst[17];

        t2 = now_ns();
        for (int i = 0; i < N_COV; i++)
                c_ref_cover_row(csrc, cdst, 256, 0xffu);
        t3 = now_ns();
        sink ^= cdst[17];
        double ns_cov_asm = (double)(t1 - t0) / N_COV;
        double ns_cov_ref = (double)(t3 - t2) / N_COV;

        /* 3. ui_text_run_walk (128x16 glyph run, ~65% paper, mixed ground/non-ground) */
        enum { W = 128, H = 16, PITCH = 160 };
        uint32_t sc[W * H];
        uint32_t px[PITCH * H];
        uint32_t paper = 0x00ffffffu, ground = 0xff1e1e24u;
        for (int i = 0; i < W * H; i++)
                sc[i] = (i % 3 != 0) ? paper : ((uint32_t)((i * 37) & 255) << 8);
        for (int i = 0; i < PITCH * H; i++)
                px[i] = (i % 4 != 0) ? ground : 0xff303440u;

        struct ui_text_ramp r_asm, r_ref;
        memset(&r_asm, 0, sizeof(r_asm));
        memset(&r_ref, 0, sizeof(r_ref));
        r_asm.colour = r_ref.colour = 0x00e0e4ecu;
        r_asm.alpha  = r_ref.alpha  = 255u;
        r_asm.ground = r_ref.ground = ground;
        r_asm.set    = r_ref.set    = 1;

        struct ui_text_blit_walk wa = {
                .scratch = sc, .wide = W, .rows = H, .top = 0, .first = 0, .last = W,
                .pixels = px, .pitch = PITCH, .pen_x = 8,
                .ui_top = 0, .ui_bottom = H, .ui_height = H,
                .band_top = 0, .band_bottom = 0,
                .paper = paper, .ground = ground, .flip = 255, .stroke = 0, .ramp = &r_asm
        };
        struct ui_text_blit_walk wr = wa;
        wr.ramp = &r_ref;

        const int N_WALK = 50000;
        t0 = now_ns();
        for (int i = 0; i < N_WALK; i++)
                ui_text_run_walk(&wa);
        t1 = now_ns();
        sink ^= px[9];

        t2 = now_ns();
        for (int i = 0; i < N_WALK; i++)
                c_ref_run_walk(&wr);
        t3 = now_ns();
        sink ^= px[9];
        double ns_walk_asm = (double)(t1 - t0) / N_WALK;
        double ns_walk_ref = (double)(t3 - t2) / N_WALK;

        printf("BENCH ui_text_over      : ASM %.2f ns/op vs C -O2 %.2f ns/op (%.2fx)\n",
               ns_over_asm, ns_over_ref, ns_over_ref / ns_over_asm);
        printf("BENCH ui_text_cover_row : ASM %.2f ns/op vs C -O2 %.2f ns/op (%.2fx)\n",
               ns_cov_asm, ns_cov_ref, ns_cov_ref / ns_cov_asm);
        printf("BENCH ui_text_run_walk  : ASM %.2f ns/op vs C -O2 %.2f ns/op (%.2fx)\n",
               ns_walk_asm, ns_walk_ref, ns_walk_ref / ns_walk_asm);
        (void)sink;
}

int main(int argc, char **argv)
{
        test_over();
        test_cover_row();
        test_run_walk();
#if !defined(_WIN32)
        test_guard_pages();
#endif
        if (failures)
        {
                fprintf(stderr, "FAILED: %d errors\n", failures);
                return 1;
        }
        printf("ALL TESTS PASSED (over, cover_row, run_walk, guard_pages)\n");
        if (argc > 1 && strcmp(argv[1], "--bench") == 0)
                run_bench();
        return 0;
}
