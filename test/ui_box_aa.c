/*
        Proof, security, differential fuzz, guard-page, size & speed test suite for
        src/canvas/ui/ui_box_aa.asm.c across arm64, x86-64, and riscv64.
*/
#define CANVAS_UI_BOX_AA_ASM_IMPLEMENTATION
#include "../src/canvas/ui/ui_box_aa.asm.c"

#include <limits.h>
#include <math.h>
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

__attribute__((noinline))
void c_ref_radii(const struct ui_aa_box *box, int32_t *rx, int32_t *ry)
{
        if (!box || !rx || !ry)
                return;
        if (box->x1 <= box->x0 || box->y1 <= box->y0)
        {
                *rx = 0;
                *ry = 0;
                return;
        }
        uint64_t w = (uint64_t)((int64_t)box->x1 - (int64_t)box->x0);
        uint64_t h = (uint64_t)((int64_t)box->y1 - (int64_t)box->y0);
        if (*rx < 0) *rx = 0;
        if (*ry < 0) *ry = 0;
        uint64_t f = 65536;
        if (*rx > 0 && 2 * (uint64_t)*rx > w)
                f = (w << 16) / (2 * (uint64_t)*rx);
        if (*ry > 0 && 2 * (uint64_t)*ry > h)
        {
                uint64_t fy = (h << 16) / (2 * (uint64_t)*ry);
                if (fy < f) f = fy;
        }
        if (f < 65536)
        {
                *rx = (int32_t)(((uint64_t)*rx * f) >> 16);
                *ry = (int32_t)(((uint64_t)*ry * f) >> 16);
        }
}

static inline uint64_t ref_isqrt64(uint64_t n)
{
        if (n == (1ull << 32))
                return 65536ull;
        uint64_t res = 0, bit = 1ull << 30;
        while (bit)
        {
                uint64_t t = res + bit;
                res >>= 1;
                if (n >= t)
                {
                        n -= t;
                        res += bit;
                }
                bit >>= 2;
        }
        return res;
}

__attribute__((noinline))
uint64_t c_ref_arc(uint64_t u)
{
        if (u >= 65536ull)
                return 0;
        return ref_isqrt64((1ull << 32) - u * u);
}

__attribute__((noinline))
void c_ref_insets(const int32_t *dy, int32_t rx, int32_t ry, int32_t *inset)
{
        if (!dy || !inset)
                return;
        if (rx <= 0 || ry <= 0)
        {
                for (int i = 0; i < UI_BOX_AA_SUB; i++)
                        inset[i] = 0;
                return;
        }
        for (int sub = 0; sub < UI_BOX_AA_SUB; sub++)
        {
                int32_t d = dy[sub];
                if (d <= 0)
                        inset[sub] = rx;
                else if (d >= ry)
                        inset[sub] = 0;
                else
                {
                        uint64_t u = ((uint64_t)d << 16) / (uint64_t)ry;
                        inset[sub] = u >= 65536ull ? 0 : (int32_t)(((uint64_t)rx * c_ref_arc(u)) >> 16);
                }
        }
}

__attribute__((noinline))
int32_t c_ref_spans_radii(const struct ui_aa_box *box, int32_t rx, int32_t ry,
                          int32_t y, int32_t first, int32_t width,
                          struct ui_aa_spans *s)
{
        if (!s)
                return 0;
        s->count = 0;
        if (!box || width <= 0)
                return 0;
        int64_t x0 = box->x0, y0 = box->y0, x1 = box->x1, y1 = box->y1;
        int64_t top = (int64_t)y * 256, bottom = top + 256;
        int64_t base = (int64_t)first * 256, reach = (int64_t)width * 256;
        if (bottom <= y0 || top >= y1 || x1 <= x0)
                return 0;
        int straight = top >= y0 + (int64_t)ry && bottom <= y1 - (int64_t)ry && top >= y0 && bottom <= y1;
        if (straight)
        {
                int64_t left = x0 - base, right = x1 - base;
                if (left < 0) left = 0;
                if (right > reach) right = reach;
                if (right <= left)
                        return 0;
                for (int sub = 0; sub < UI_BOX_AA_SUB; sub++)
                        s->left[sub] = (int32_t)left, s->right[sub] = (int32_t)right;
                s->count = UI_BOX_AA_SUB;
                s->lo = (int32_t)(left >> 8);
                s->hi = (int32_t)((right + 255) >> 8);
                s->a  = (int32_t)((left + 255) >> 8);
                s->b  = (int32_t)(right >> 8);
                if (s->b <= s->a)
                        s->a = s->b = s->hi;
                s->inside = (uint32_t)UI_BOX_AA_SUB * 256u;
                return 1;
        }
        int corner = ry > 0 && rx > 0;
        uint32_t outside = 0;
        int32_t dys[UI_BOX_AA_SUB], inset[UI_BOX_AA_SUB];
        for (int sub = 0; sub < UI_BOX_AA_SUB; sub++)
        {
                int64_t at = top + sub * 16 + 8;
                int32_t dy = 0;
                if (at < y0 || at >= y1)
                        outside |= 1u << sub;
                else if (at < y0 + (int64_t)ry)
                        dy = (int32_t)(y0 + (int64_t)ry - at);
                else if (at >= y1 - (int64_t)ry)
                        dy = (int32_t)(at - (y1 - (int64_t)ry) + 1);
                dys[sub] = dy;
        }
        if (corner)
                c_ref_insets(dys, rx, ry, inset);
        else
                for (int sub = 0; sub < UI_BOX_AA_SUB; sub++)
                        inset[sub] = 0;
        int64_t x0_adj = corner ? x0 + (int64_t)rx : x0;
        int64_t x1_adj = corner ? x1 - (int64_t)rx : x1;
        int64_t low = 0, high = 0, inner_low = 0, inner_high = 0;
        for (int sub = 0; sub < UI_BOX_AA_SUB; sub++)
        {
                if ((outside >> sub) & 1u)
                        continue;
                int64_t left  = x0_adj - (int64_t)inset[sub];
                int64_t right = x1_adj + (int64_t)inset[sub];
                if (right <= left)
                        continue;
                left -= base;
                right -= base;
                if (left < 0) left = 0;
                if (right > reach) right = reach;
                if (right <= left)
                        continue;
                if (!s->count)
                        low = inner_low = left, high = inner_high = right;
                if (left < low) low = left;
                if (right > high) high = right;
                if (left > inner_low) inner_low = left;
                if (right < inner_high) inner_high = right;
                s->left[s->count] = (int32_t)left;
                s->right[s->count] = (int32_t)right;
                s->count++;
        }
        for (int k = s->count; k < UI_BOX_AA_SUB; k++)
                s->left[k] = s->right[k] = 0;
        if (!s->count)
                return 0;
        s->lo = (int32_t)(low >> 8);
        s->hi = (int32_t)((high + 255) >> 8);
        s->a  = (int32_t)((inner_low + 255) >> 8);
        s->b  = (int32_t)(inner_high >> 8);
        if (s->b <= s->a)
                s->a = s->b = s->hi;
        s->inside = (uint32_t)s->count * 256u;
        return 1;
}

__attribute__((noinline))
uint32_t c_ref_spans_cover(const struct ui_aa_spans *s, int32_t x)
{
        if (!s || s->count <= 0 || x < s->lo || x >= s->hi)
                return 0;
        if (x >= s->a && x < s->b)
                return s->inside;
        int32_t p0 = x * 256, p1 = p0 + 256;
        uint32_t cover = 0;
        int n = s->count > UI_BOX_AA_SUB ? UI_BOX_AA_SUB : s->count;
        for (int k = 0; k < n; k++)
        {
                int32_t from = s->left[k] > p0 ? s->left[k] : p0;
                int32_t to   = s->right[k] < p1 ? s->right[k] : p1;
                if (to > from)
                        cover += (uint32_t)(to - from);
        }
        return cover;
}

__attribute__((noinline))
int32_t c_ref_span_ends(int32_t first, int32_t width, int32_t left, int32_t right,
                        int32_t *p0, int32_t *p1, uint32_t *c0, uint32_t *c1)
{
        if (!p0 || !p1 || !c0 || !c1 || width <= 0)
                return 0;
        int64_t base = (int64_t)first * 256, reach = (int64_t)width * 256;
        int64_t l = (int64_t)left - base, r = (int64_t)right - base;
        if (l < 0) l = 0;
        if (r > reach) r = reach;
        if (r <= l)
                return 0;
        int64_t q0 = l >> 8, q1 = (r - 1) >> 8;
        *p0 = (int32_t)q0;
        *p1 = (int32_t)q1;
        if (q0 == q1)
                *c0 = *c1 = (uint32_t)((r - l) << 4);
        else
        {
                *c0 = (uint32_t)((256 - (l & 255)) << 4);
                *c1 = (uint32_t)((r - (q1 << 8)) << 4);
        }
        return 1;
}

/* ---- Helpers ---- */

static uint64_t rng_state = 0x9e3779b97f4a7c15ull;
static uint64_t xorshift64(void)
{
        uint64_t x = rng_state;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        return rng_state = x;
}

static uint64_t now_ns(void)
{
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
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

static int spans_equal(const struct ui_aa_spans *a, const struct ui_aa_spans *b, int ret_a, int ret_b)
{
        if (ret_a != ret_b) return 0;
        if (a->count != b->count) return 0;
        if (!ret_a) return 1;
        if (a->lo != b->lo || a->hi != b->hi || a->a != b->a || a->b != b->b || a->inside != b->inside)
                return 0;
        for (int i = 0; i < UI_BOX_AA_SUB; i++)
                if (a->left[i] != b->left[i] || a->right[i] != b->right[i])
                        return 0;
        return 1;
}

/* ---- Tests ---- */

static void test_null_and_security(void)
{
        int32_t rx = 100, ry = 200;
        struct ui_aa_box box = {0, 0, 1000, 1000, 100, 200};
        struct ui_aa_spans sp;
        memset(&sp, 0x5a, sizeof(sp));
        int32_t dys[16] = {0}, ins[16];
        int32_t p0 = 7, p1 = 8;
        uint32_t c0 = 9, c1 = 10;

        ui_box_aa_radii(NULL, &rx, &ry);
        ui_box_aa_radii(&box, NULL, &ry);
        ui_box_aa_radii(&box, &rx, NULL);
        CHECK(rx == 100 && ry == 200, "radii NULL mutated outputs");

        ui_box_aa_insets(NULL, 100, 100, ins);
        ui_box_aa_insets(dys, 100, 100, NULL);
        ui_box_aa_insets(dys, 0, 100, ins);
        for (int i = 0; i < 16; i++) CHECK(ins[i] == 0, "insets rx=0 non-zero");
        ui_box_aa_insets(dys, 100, 0, ins);
        for (int i = 0; i < 16; i++) CHECK(ins[i] == 0, "insets ry=0 non-zero");
        ui_box_aa_insets(dys, -5, -5, ins);
        for (int i = 0; i < 16; i++) CHECK(ins[i] == 0, "insets negative non-zero");

        CHECK(ui_box_aa_spans_radii(NULL, 10, 10, 0, 0, 100, &sp) == 0 && sp.count == 0, "spans NULL box");
        CHECK(ui_box_aa_spans_radii(&box, 10, 10, 0, 0, 100, NULL) == 0, "spans NULL out");
        CHECK(ui_box_aa_spans_radii(&box, 10, 10, 0, 0, 0, &sp) == 0 && sp.count == 0, "spans width=0");
        CHECK(ui_box_aa_spans_radii(&box, 10, 10, 0, 0, -50, &sp) == 0 && sp.count == 0, "spans width<0");

        CHECK(ui_box_aa_spans_cover(NULL, 0) == 0, "cover NULL");
        sp.count = 0;
        CHECK(ui_box_aa_spans_cover(&sp, 0) == 0, "cover count=0");
        sp.count = -3;
        CHECK(ui_box_aa_spans_cover(&sp, 0) == 0, "cover count<0");

        CHECK(ui_box_aa_span_ends(0, 100, 0, 1000, NULL, &p1, &c0, &c1) == 0, "span_ends NULL p0");
        CHECK(ui_box_aa_span_ends(0, 100, 0, 1000, &p0, NULL, &c0, &c1) == 0, "span_ends NULL p1");
        CHECK(ui_box_aa_span_ends(0, 100, 0, 1000, &p0, &p1, NULL, &c1) == 0, "span_ends NULL c0");
        CHECK(ui_box_aa_span_ends(0, 100, 0, 1000, &p0, &p1, &c0, NULL) == 0, "span_ends NULL c1");
        CHECK(ui_box_aa_span_ends(0, 0, 0, 1000, &p0, &p1, &c0, &c1) == 0, "span_ends width=0");
        CHECK(ui_box_aa_span_ends(0, -10, 0, 1000, &p0, &p1, &c0, &c1) == 0, "span_ends width<0");
        CHECK(p0 == 7 && p1 == 8 && c0 == 9 && c1 == 10, "span_ends mutated on failure");
}

static void test_arc_exhaustive(void)
{
        for (uint64_t u = 0; u <= 65540; u++)
        {
                uint64_t got = ui_box_aa_arc(u);
                uint64_t exp = c_ref_arc(u);
                CHECK(got == exp, "arc(%llu): got=%llu exp=%llu",
                      (unsigned long long)u, (unsigned long long)got, (unsigned long long)exp);
        }
        uint64_t extremes[] = {65536ull, 65537ull, 100000ull, 0xffffffffull, 0xffffffffffffffffull};
        for (size_t i = 0; i < sizeof(extremes)/sizeof(extremes[0]); i++)
                CHECK(ui_box_aa_arc(extremes[i]) == 0, "arc(extreme) != 0");
}

static void test_edge_cases(void)
{
        /* 0/1/2/odd radii, radius > half-width/height, 0-width/0-height, extreme coordinates */
        const struct ui_aa_box boxes[] = {
                {0, 0, 0, 1000, 50, 50},                 /* 0-width */
                {0, 0, 1000, 0, 50, 50},                 /* 0-height */
                {100, 100, 50, 50, 10, 10},              /* inverted */
                {0, 0, 256, 256, 0, 0},                  /* 1px box, r=0 */
                {0, 0, 256, 256, 1, 1},                  /* 1px box, r=1 */
                {0, 0, 256, 256, 2, 2},                  /* 1px box, r=2 */
                {0, 0, 256, 256, 3, 5},                  /* odd distinct radii */
                {0, 0, 256, 256, 500, 700},              /* radius >> half-width/height */
                {0, 0, 2560, 512, 1000, 1000},           /* pill */
                {17, 33, 1999, 1555, 333, 555},          /* subpixel edges + odd distinct rx/ry */
                {-1000, -1000, 1000, 1000, 512, 256},    /* negative coords */
                {INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX, 1000000, 2000000}, /* extreme coords */
                {INT32_MIN, 0, INT32_MAX, 256, INT32_MAX, INT32_MAX},           /* extreme radii */
                {0, 0, 1024, 1024, -10, 200},            /* negative rx */
        };
        for (size_t i = 0; i < sizeof(boxes)/sizeof(boxes[0]); i++)
        {
                int32_t arx = boxes[i].rx, ary = boxes[i].ry;
                int32_t crx = boxes[i].rx, cry = boxes[i].ry;
                ui_box_aa_radii(&boxes[i], &arx, &ary);
                c_ref_radii(&boxes[i], &crx, &cry);
                CHECK(arx == crx && ary == cry, "radii case %zu: got=(%d,%d) exp=(%d,%d)", i, arx, ary, crx, cry);

                if (boxes[i].x0 > -100000 && boxes[i].x1 < 100000)
                {
                        for (int32_t y = (boxes[i].y0 >> 8) - 2; y <= (boxes[i].y1 >> 8) + 2; y++)
                        {
                                struct ui_aa_spans sa, sc;
                                memset(&sa, 0xaa, sizeof(sa));
                                memset(&sc, 0xcc, sizeof(sc));
                                int32_t ra = ui_box_aa_spans_radii(&boxes[i], arx, ary, y, -4, 32, &sa);
                                int32_t rc = c_ref_spans_radii(&boxes[i], crx, cry, y, -4, 32, &sc);
                                CHECK(spans_equal(&sa, &sc, ra, rc), "spans case %zu y=%d", i, y);
                                if (ra)
                                {
                                        for (int32_t x = sa.lo - 2; x <= sa.hi + 2; x++)
                                        {
                                                uint32_t ca = ui_box_aa_spans_cover(&sa, x);
                                                uint32_t cc = c_ref_spans_cover(&sc, x);
                                                CHECK(ca == cc, "cover case %zu y=%d x=%d: %u vs %u", i, y, x, ca, cc);
                                        }
                                        CHECK(ui_box_aa_spans_cover(&sa, INT32_MIN) == 0, "cover INT32_MIN");
                                        CHECK(ui_box_aa_spans_cover(&sa, INT32_MAX) == 0, "cover INT32_MAX");
                                }
                        }
                }
        }

        /* 1-px rounded ring with 4 distinct radii (outer rx, outer ry, inner rx, inner ry) */
        struct ui_aa_box outer = {256, 256, 256 * 25, 256 * 19, 777, 1111};
        struct ui_aa_box inner = {512, 512, 256 * 24, 256 * 18, 521, 855};
        int32_t orx = outer.rx, ory = outer.ry, irx = inner.rx, iry = inner.ry;
        ui_box_aa_radii(&outer, &orx, &ory);
        ui_box_aa_radii(&inner, &irx, &iry);
        for (int32_t y = 0; y < 22; y++)
        {
                struct ui_aa_spans oa, oc, ia, ic;
                int32_t roa = ui_box_aa_spans_radii(&outer, orx, ory, y, 0, 32, &oa);
                int32_t roc = c_ref_spans_radii(&outer, orx, ory, y, 0, 32, &oc);
                int32_t ria = ui_box_aa_spans_radii(&inner, irx, iry, y, 0, 32, &ia);
                int32_t ric = c_ref_spans_radii(&inner, irx, iry, y, 0, 32, &ic);
                CHECK(spans_equal(&oa, &oc, roa, roc), "ring outer y=%d", y);
                CHECK(spans_equal(&ia, &ic, ria, ric), "ring inner y=%d", y);
                if (roa)
                {
                        for (int32_t x = 0; x < 32; x++)
                        {
                                int ca = (int)ui_box_aa_spans_cover(&oa, x) - (ria ? (int)ui_box_aa_spans_cover(&ia, x) : 0);
                                int cc = (int)c_ref_spans_cover(&oc, x) - (ric ? (int)c_ref_spans_cover(&ic, x) : 0);
                                CHECK(ca == cc && ca >= 0 && ca <= UI_BOX_AA_FULL,
                                      "ring cover y=%d x=%d: %d vs %d", y, x, ca, cc);
                        }
                }
        }
}

static void test_fuzz(void)
{
        /* 10,000 randomized differential fuzz cases across all 6 functions */
        for (int iter = 0; iter < 10000; iter++)
        {
                struct ui_aa_box box;
                box.x0 = (int32_t)(xorshift64() % 4096) - 1024;
                box.y0 = (int32_t)(xorshift64() % 4096) - 1024;
                box.x1 = box.x0 + (int32_t)(xorshift64() % 4096) - 64;
                box.y1 = box.y0 + (int32_t)(xorshift64() % 4096) - 64;
                box.rx = (int32_t)(xorshift64() % 2048) - 32;
                box.ry = (int32_t)(xorshift64() % 2048) - 32;

                int32_t arx = box.rx, ary = box.ry, crx = box.rx, cry = box.ry;
                ui_box_aa_radii(&box, &arx, &ary);
                c_ref_radii(&box, &crx, &cry);
                CHECK(arx == crx && ary == cry, "fuzz radii iter=%d", iter);

                int32_t dy[16], ins_a[16], ins_c[16];
                int32_t irx = (int32_t)(xorshift64() % 65536) - 10;
                int32_t iry = (int32_t)(xorshift64() % 65536) - 10;
                for (int k = 0; k < 16; k++)
                        dy[k] = (int32_t)(xorshift64() % 70000) - 500;
                ui_box_aa_insets(dy, irx, iry, ins_a);
                c_ref_insets(dy, irx, iry, ins_c);
                for (int k = 0; k < 16; k++)
                        CHECK(ins_a[k] == ins_c[k], "fuzz insets iter=%d k=%d: %d vs %d", iter, k, ins_a[k], ins_c[k]);

                int32_t y = (box.y0 >> 8) + (int32_t)(xorshift64() % 24) - 2;
                int32_t first = (int32_t)(xorshift64() % 32) - 8;
                int32_t width = (int32_t)(xorshift64() % 64);
                struct ui_aa_spans sa, sc;
                int32_t ra = ui_box_aa_spans_radii(&box, arx, ary, y, first, width, &sa);
                int32_t rc = c_ref_spans_radii(&box, crx, cry, y, first, width, &sc);
                CHECK(spans_equal(&sa, &sc, ra, rc), "fuzz spans iter=%d", iter);
                if (ra)
                {
                        for (int k = 0; k < 4; k++)
                        {
                                int32_t x = sa.lo - 1 + (int32_t)(xorshift64() % (uint64_t)(sa.hi - sa.lo + 4));
                                CHECK(ui_box_aa_spans_cover(&sa, x) == c_ref_spans_cover(&sc, x),
                                      "fuzz cover iter=%d x=%d", iter, x);
                        }
                }

                int32_t l = (int32_t)(xorshift64() % 8192) - 2048;
                int32_t r = l + (int32_t)(xorshift64() % 8192) - 256;
                int32_t p0a = 0, p1a = 0, p0c = 0, p1c = 0;
                uint32_t c0a = 0, c1a = 0, c0c = 0, c1c = 0;
                int32_t ea = ui_box_aa_span_ends(first, width, l, r, &p0a, &p1a, &c0a, &c1a);
                int32_t ec = c_ref_span_ends(first, width, l, r, &p0c, &p1c, &c0c, &c1c);
                CHECK(ea == ec && (!ea || (p0a == p0c && p1a == p1c && c0a == c0c && c1a == c1c)),
                      "fuzz span_ends iter=%d", iter);
        }
}

#if !defined(_WIN32)
static void test_guard_pages(void)
{
        size_t pg = (size_t)sysconf(_SC_PAGESIZE);
        uint8_t *region = mmap(NULL, 3 * pg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        CHECK(region != MAP_FAILED, "mmap failed");
        CHECK(mprotect(region, pg, PROT_NONE) == 0, "mprotect pre-guard failed");
        CHECK(mprotect(region + 2 * pg, pg, PROT_NONE) == 0, "mprotect post-guard failed");
        uint8_t *mid = region + pg;

        /* Test flush-at-start (against pre-guard) and flush-at-end (against post-guard) */
        for (int end = 0; end < 2; end++)
        {
                struct ui_aa_box *box = (struct ui_aa_box *)(end ? (mid + pg - sizeof(struct ui_aa_box)) : mid);
                *box = (struct ui_aa_box){64, 64, 2560, 2560, 512, 768};
                int32_t rx = box->rx, ry = box->ry;
                ui_box_aa_radii(box, &rx, &ry);

                int32_t *dy = (int32_t *)(end ? (mid + pg - 64) : mid);
                int32_t *ins = (int32_t *)(end ? (mid + pg - 128) : (mid + 64));
                for (int i = 0; i < 16; i++) dy[i] = i * 40;
                ui_box_aa_insets(dy, rx, ry, ins);

                struct ui_aa_spans *sp = (struct ui_aa_spans *)(end ? (mid + pg - sizeof(struct ui_aa_spans)) : mid);
                struct ui_aa_box bcopy = {64, 64, 2560, 2560, 512, 768};
                int32_t ok = ui_box_aa_spans_radii(&bcopy, rx, ry, 1, 0, 16, sp);
                CHECK(ok == 1, "guard spans_radii end=%d", end);
                uint32_t cov = ui_box_aa_spans_cover(sp, sp->lo);
                CHECK(cov > 0 && cov <= UI_BOX_AA_FULL, "guard spans_cover end=%d cov=%u", end, cov);

                /* Also test straight-middle fast path flush against guard pages */
                ok = ui_box_aa_spans_radii(&bcopy, rx, ry, 5, 0, 16, sp);
                CHECK(ok == 1 && sp->count == 16, "guard straight end=%d", end);
                CHECK(ui_box_aa_spans_cover(sp, 5) == UI_BOX_AA_FULL, "guard straight cover end=%d", end);
        }

        munmap(region, 3 * pg);
}
#endif

static void bench(void)
{
        const int N = 200000;
        struct ui_aa_box box = {128, 128, 256 * 120 + 64, 256 * 60 + 64, 16 * 256, 12 * 256};
        int32_t rx = box.rx, ry = box.ry;
        ui_box_aa_radii(&box, &rx, &ry);

        int32_t dy[16] __attribute__((aligned(16)));
        int32_t ins[16] __attribute__((aligned(16)));
        for (int i = 0; i < 16; i++) dy[i] = 100 + i * 180;

        struct ui_aa_spans sp;
        volatile uint64_t sink = 0;

        /* 1. insets */
        uint64_t t0 = now_ns();
        for (int i = 0; i < N; i++) { c_ref_insets(dy, rx, ry, ins); sink += (uint32_t)ins[i & 15]; }
        uint64_t t_c_ins = now_ns() - t0;
        t0 = now_ns();
        for (int i = 0; i < N; i++) { ui_box_aa_insets(dy, rx, ry, ins); sink += (uint32_t)ins[i & 15]; }
        uint64_t t_a_ins = now_ns() - t0;

        /* 2. spans_radii (corner row) */
        t0 = now_ns();
        for (int i = 0; i < N; i++) { sink += (uint32_t)c_ref_spans_radii(&box, rx, ry, 3, 0, 128, &sp); }
        uint64_t t_c_sp = now_ns() - t0;
        t0 = now_ns();
        for (int i = 0; i < N; i++) { sink += (uint32_t)ui_box_aa_spans_radii(&box, rx, ry, 3, 0, 128, &sp); }
        uint64_t t_a_sp = now_ns() - t0;

        /* 3. spans_cover (corner pixel — vary x across [lo..lo+3] to prevent LICM hoisting) */
        ui_box_aa_spans_radii(&box, rx, ry, 3, 0, 128, &sp);
        int32_t cx_base = sp.lo;
        t0 = now_ns();
        for (int i = 0; i < N * 10; i++) { sink += c_ref_spans_cover(&sp, cx_base + (i & 3)); }
        uint64_t t_c_cov = now_ns() - t0;
        t0 = now_ns();
        for (int i = 0; i < N * 10; i++) { sink += ui_box_aa_spans_cover(&sp, cx_base + (i & 3)); }
        uint64_t t_a_cov = now_ns() - t0;

        /* 4. span_ends (vary left/right slightly to prevent LICM hoisting) */
        int32_t p0, p1; uint32_t c0, c1;
        t0 = now_ns();
        for (int i = 0; i < N * 10; i++) { sink += (uint32_t)c_ref_span_ends(0, 128, 333 + (i & 63), 29000 - (i & 63), &p0, &p1, &c0, &c1) + c0; }
        uint64_t t_c_end = now_ns() - t0;
        t0 = now_ns();
        for (int i = 0; i < N * 10; i++) { sink += (uint32_t)ui_box_aa_span_ends(0, 128, 333 + (i & 63), 29000 - (i & 63), &p0, &p1, &c0, &c1) + c0; }
        uint64_t t_a_end = now_ns() - t0;

        printf("BENCH insets     : C=%.2f ns/op  ASM=%.2f ns/op  speedup=%.2fx\n",
               (double)t_c_ins / N, (double)t_a_ins / N, (double)t_c_ins / (double)(t_a_ins ? t_a_ins : 1));
        printf("BENCH spans_radii: C=%.2f ns/op  ASM=%.2f ns/op  speedup=%.2fx\n",
               (double)t_c_sp / N, (double)t_a_sp / N, (double)t_c_sp / (double)(t_a_sp ? t_a_sp : 1));
        printf("BENCH spans_cover: C=%.2f ns/op  ASM=%.2f ns/op  speedup=%.2fx\n",
               (double)t_c_cov / (N * 10), (double)t_a_cov / (N * 10), (double)t_c_cov / (double)(t_a_cov ? t_a_cov : 1));
        printf("BENCH span_ends  : C=%.2f ns/op  ASM=%.2f ns/op  speedup=%.2fx\n",
               (double)t_c_end / (N * 10), (double)t_a_end / (N * 10), (double)t_c_end / (double)(t_a_end ? t_a_end : 1));
        (void)sink;
}

int main(void)
{
        test_null_and_security();
        test_arc_exhaustive();
        test_edge_cases();
        test_fuzz();
#if !defined(_WIN32)
        test_guard_pages();
#endif
        if (failures)
        {
                fprintf(stderr, "FAILED with %d errors\n", failures);
                return 1;
        }
        printf("ALL ui_box_aa TESTS PASSED!\n");
        bench();
        return 0;
}
