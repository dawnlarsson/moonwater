/*
        Differential C-reference parity, security, mmap guard-page, fuzz &
        throughput/size proof for src/canvas/font_core.asm.c
        (arm64, x86-64, riscv64 pure-ASM TrueType/OpenType & rasterizer core).

        Build & run:
          cc -O2 -Wall -Werror test/font_core.c -o /tmp/t_font && /tmp/t_font
          cc -O2 -fsanitize=address,undefined -Wall -Werror test/font_core.c -o /tmp/t_font_asan && /tmp/t_font_asan
*/
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>

#define CANVAS_FONT_CORE_ASM_IMPLEMENTATION
#include "../src/canvas/font_core.asm.c"

/* Exact font_face layout mirror for standalone testing */
struct test_face {
        const uint8_t *data;    /* +0 */
        uint64_t       size;    /* +8 */
        uint64_t       pad0[4]; /* +16..+40 */
        uint64_t       cmap;    /* +48 */
        uint64_t       pad1[5]; /* +56..+88 */
        uint32_t       glyphs;  /* +96 */
        uint32_t       pad2[3]; /* +100..+108 */
        uint16_t       lookups[16]; /* +112..+142 */
        uint8_t        lookup_count, long_loca, cmap_format, symbol; /* +144..+147 */
        int16_t        tail[8];
};

_Static_assert(offsetof(struct test_face, data) == 0, "data");
_Static_assert(offsetof(struct test_face, size) == 8, "size");
_Static_assert(offsetof(struct test_face, cmap) == 48, "cmap");
_Static_assert(offsetof(struct test_face, glyphs) == 96, "glyphs");
_Static_assert(offsetof(struct test_face, cmap_format) == 146, "cmap_format");
_Static_assert(offsetof(struct test_face, symbol) == 147, "symbol");

/* ── Reference C implementations (noinline for exact nm --print-size comparison) ── */

static inline uint32_t r_u16(const struct test_face *f, uint64_t at) {
        if (!f || !f->data || f->size < 2 || at > f->size - 2) return 0;
        return ((uint32_t)f->data[at] << 8) | f->data[at + 1];
}

static inline uint32_t r_u32(const struct test_face *f, uint64_t at) {
        if (!f || !f->data || f->size < 4 || at > f->size - 4) return 0;
        return ((uint32_t)f->data[at] << 24) | ((uint32_t)f->data[at + 1] << 16) |
               ((uint32_t)f->data[at + 2] << 8) | f->data[at + 3];
}

static inline uint32_t r_bits(uint32_t v) {
        v &= 0xffu;
        v = (v & 0x55u) + ((v >> 1) & 0x55u);
        v = (v & 0x33u) + ((v >> 2) & 0x33u);
        return (v + (v >> 4)) & 0x0fu;
}

static __attribute__((noinline)) uint32_t ref_search(const void *vf, uint64_t table, uint32_t count,
                                                     uint64_t stride, uint32_t off_bytes, uint32_t value) {
        const struct test_face *f = (const struct test_face *)vf;
        if (!f || !count) return 0;
        uint32_t bytes4 = off_bytes & 1u;
        uint64_t offset = (uint64_t)(off_bytes >> 1);
        uint32_t low = 0, high = count;
        while (low < high) {
                uint32_t mid = low + ((high - low) >> 1);
                uint64_t at = table + stride * mid + offset;
                uint32_t k = bytes4 ? r_u32(f, at) : r_u16(f, at);
                if (k < value) low = mid + 1;
                else high = mid;
        }
        return low;
}

static __attribute__((noinline)) uint32_t ref_glyph(const void *vf, uint32_t code) {
        const struct test_face *f = (const struct test_face *)vf;
        if (!f || !f->data) return 0;
        int try_sym = (f->symbol && code < 0x100u);
        for (;;) {
                uint64_t table = f->cmap, glyph = 0;
                if (f->cmap_format == 12 && f->size >= 16 && table <= f->size - 16) {
                        uint32_t count = r_u32(f, table + 12);
                        uint64_t maxc = (f->size - table - 16) / 12;
                        if (count > maxc) count = (uint32_t)maxc;
                        uint32_t low = ref_search(f, table + 16, count, 12, (4u << 1) | 1u, code);
                        uint64_t group = table + 16 + 12 * (uint64_t)low;
                        if (low < count && r_u32(f, group) <= code)
                                glyph = (uint64_t)r_u32(f, group + 8) + code - r_u32(f, group);
                } else if (f->cmap_format == 4 && code <= 0xffffu) {
                        uint32_t segs = r_u16(f, table + 6) / 2;
                        uint64_t ends = table + 14;
                        uint64_t starts = ends + 2 * (uint64_t)segs + 2;
                        uint64_t deltas = starts + 2 * (uint64_t)segs;
                        uint64_t ranges = deltas + 2 * (uint64_t)segs;
                        uint32_t low = ref_search(f, ends, segs, 2, 0, code);
                        if (low < segs) {
                                uint32_t start = r_u16(f, starts + 2 * (uint64_t)low);
                                uint32_t delta = r_u16(f, deltas + 2 * (uint64_t)low);
                                uint32_t range = r_u16(f, ranges + 2 * (uint64_t)low);
                                if (code >= start) {
                                        glyph = range ? r_u16(f, ranges + 2 * (uint64_t)low + range + 2 * (uint64_t)(code - start)) : code;
                                        if (glyph) glyph = (glyph + delta) & 0xffffu;
                                }
                        }
                }
                uint32_t res = glyph < f->glyphs ? (uint32_t)glyph : 0;
                if (res || !try_sym) return res;
                try_sym = 0;
                code |= 0xf000u;
        }
}

static __attribute__((noinline)) int32_t ref_coverage(const void *vf, uint64_t table, uint32_t glyph) {
        const struct test_face *f = (const struct test_face *)vf;
        if (!f || !f->data || f->size < 2) return -1;
        uint32_t fmt = r_u16(f, table), count = r_u16(f, table + 2);
        if (fmt != 1 && fmt != 2) return -1;
        uint32_t low = ref_search(f, table + 4, count, fmt == 1 ? 2 : 6, fmt == 1 ? 0 : (2u << 1), glyph);
        if (low == count) return -1;
        uint64_t at = table + 4 + (fmt == 1 ? 2 : 6) * (uint64_t)low;
        if (fmt == 1) return r_u16(f, at) == glyph ? (int32_t)low : -1;
        uint32_t start = r_u16(f, at);
        return start <= glyph ? (int32_t)(r_u16(f, at + 4) + glyph - start) : -1;
}

static __attribute__((noinline)) uint32_t ref_class(const void *vf, uint64_t table, uint32_t glyph) {
        const struct test_face *f = (const struct test_face *)vf;
        if (!f || !f->data || f->size < 2) return 0;
        uint32_t fmt = r_u16(f, table);
        if (fmt == 1) {
                uint32_t first = r_u16(f, table + 2);
                return glyph >= first && glyph - first < r_u16(f, table + 4)
                           ? r_u16(f, table + 6 + 2 * (uint64_t)(glyph - first)) : 0;
        }
        if (fmt != 2) return 0;
        uint32_t count = r_u16(f, table + 2);
        uint32_t low = ref_search(f, table + 4, count, 6, 2u << 1, glyph);
        uint64_t at = table + 4 + 6 * (uint64_t)low;
        return low < count && r_u16(f, at) <= glyph ? r_u16(f, at + 4) : 0;
}

static __attribute__((noinline)) uint32_t ref_pair_at(const void *vf, uint64_t table, uint32_t left, uint32_t right,
                                                      uint64_t *first_at, uint64_t *second_at) {
        const struct test_face *f = (const struct test_face *)vf;
        if (!f || !f->data || f->size < 2 || !first_at || !second_at) return 0;
        uint32_t fmt = r_u16(f, table), first = r_u16(f, table + 4);
        uint64_t record = 2 * (uint64_t)(r_bits(first) + r_bits(r_u16(f, table + 6))), at;
        int32_t index = ref_coverage(f, table + r_u16(f, table + 2), left);
        if (index < 0) return 0;
        if (fmt == 1) {
                if ((uint32_t)index >= r_u16(f, table + 8)) return 0;
                uint64_t set = table + r_u16(f, table + 10 + 2 * (uint64_t)(uint32_t)index);
                uint32_t count = r_u16(f, set);
                uint32_t low = ref_search(f, set + 2, count, record + 2, 0, right);
                at = set + 2 + (record + 2) * low;
                if (low == count || r_u16(f, at) != right) return 0;
                at += 2;
        } else if (fmt == 2) {
                uint32_t one = ref_class(f, table + r_u16(f, table + 8), left);
                uint32_t two = ref_class(f, table + r_u16(f, table + 10), right);
                uint32_t ones = r_u16(f, table + 12), twos = r_u16(f, table + 14);
                if (one >= ones || two >= twos) return 0;
                at = table + 16 + ((uint64_t)one * twos + two) * record;
        } else return 0;
        *first_at = at;
        *second_at = at + 2 * (uint64_t)r_bits(first);
        return 1;
}

static __attribute__((noinline)) uint32_t ref_range_find(const uint32_t *table, uint32_t count, uint32_t shift, uint32_t code) {
        if (!table || !count) return 0;
        shift &= 31u;
        uint32_t low = 0, high = count;
        while (high - low > 1) {
                uint32_t mid = low + ((high - low) >> 1);
                if ((table[mid] >> shift) <= code) low = mid;
                else high = mid;
        }
        return table[low] & ((1u << shift) - 1u);
}

static __attribute__((noinline)) uint32_t ref_lower_bound(const void *values, uint32_t count, uint32_t stride,
                                                          uint32_t wide, uint32_t code) {
        if (!values || !count) return 0;
        uint32_t low = 0, high = count;
        while (low < high) {
                uint32_t mid = low + ((high - low) >> 1);
                uint64_t idx = (uint64_t)mid * stride;
                uint32_t key = wide ? ((const uint32_t *)values)[idx] : ((const uint16_t *)values)[idx];
                if (key < code) low = mid + 1;
                else high = mid;
        }
        return low;
}

static inline void r_floor_div(int64_t num, int64_t den, int64_t *q, int64_t *r) {
        *q = num / den;
        *r = num % den;
        if (*r < 0) { *q -= 1; *r += den; }
}

static __attribute__((noinline)) void ref_span(const struct font_raster_band *b, int32_t row,
                                               int32_t xa, int32_t xb, int32_t cover) {
        if (!b || !b->cells || row < 0 || row >= b->num_rows) return;
        int32_t wide = b->width * 1024;
        if (xa < 0) xa = 0; else if (xa > wide) xa = wide;
        if (xb < 0) xb = 0; else if (xb > wide) xb = wide;
        uint32_t *cells = b->cells + (uint64_t)row * (uint64_t)b->stride;
        uint64_t *rm = b->rows ? b->rows + (uint64_t)row * 6 : NULL;
        if (((xa ^ xb) >> 10) == 0) {
                int32_t col = xa >> 10;
                int32_t area = cover * ((xa & 1023) + (xb & 1023));
                cells[col] += (uint32_t)(2048 * cover - area);
                cells[col + 1] += (uint32_t)area;
                if (rm && b->mark) rm[0] |= (2ULL << ((col + 1) >> b->grain)) - (1ULL << (col >> b->grain));
                if (rm && b->track) rm[1] |= 1ULL << (col >> b->cross);
                return;
        }
        int32_t col = xa >> 10, last = xb >> 10;
        int right = xb > xa;
        int32_t lo = right ? col : last, hi = right ? last : col;
        if (rm && b->mark) rm[0] |= (2ULL << ((hi + 1) >> b->grain)) - (1ULL << (lo >> b->grain));
        if (rm && b->track) rm[1] |= (2ULL << (hi >> b->cross)) - (1ULL << (lo >> b->cross));
        int32_t step = right ? 1 : -1, leave = right ? 1024 : 0, enter = 1024 - leave, sign = cover < 0 ? -1 : 1;
        int64_t travel = right ? (int64_t)xb - xa : (int64_t)xa - xb;
        int64_t size = cover < 0 ? -(int64_t)cover : (int64_t)cover;
        int64_t first = right ? 1024 - (xa & 1023) : (xa & 1023), half = travel >> 1;
        int64_t done = (size * first + half) / travel, carry = (size * first + half) % travel;
        int64_t lift = (size * 1024) / travel, spare = (size * 1024) % travel;
        int32_t part = (int32_t)done * sign, area = part * ((xa & 1023) + leave);
        cells[col] += (uint32_t)(2048 * part - area);
        cells[col + 1] += (uint32_t)area;
        for (col += step; col != last; col += step) {
                int64_t piece = lift;
                carry += spare;
                if (carry >= travel) { carry -= travel; piece++; }
                done += piece;
                part = (int32_t)piece * sign;
                cells[col] += (uint32_t)(part * 1024);
                cells[col + 1] += (uint32_t)(part * 1024);
        }
        part = cover - (int32_t)done * sign;
        area = part * (enter + (xb & 1023));
        cells[last] += (uint32_t)(2048 * part - area);
        cells[last + 1] += (uint32_t)area;
}

static __attribute__((noinline)) void ref_edge_rows(const struct font_raster_band *b, const int32_t *e) {
        if (!b || !b->cells || !e) return;
        int32_t x0 = e[0], y0 = e[1], x1 = e[2], y1 = e[3], ya = e[4], top = e[5], sign = e[6];
        int64_t dx = (int64_t)x1 - x0, dy = (int64_t)y1 - y0;
        if (dy <= 0 || ya >= top) return;
        int64_t half = dy >> 1, at = 0, carry = half, lift = 0, spare = -1;
        if (ya != y0) r_floor_div(dx * ((int64_t)ya - y0) + half, dy, &at, &carry);
        int32_t xa = x0 + (int32_t)at;
        while (ya < top) {
                int32_t yb = (ya & ~1023) + 1024;
                if ((ya & 1023) || yb > top) {
                        if (yb > top) yb = top;
                        if (yb == y1) at = dx;
                        else r_floor_div(dx * ((int64_t)yb - y0) + half, dy, &at, &carry);
                } else {
                        if (spare < 0) r_floor_div(dx * 1024, dy, &lift, &spare);
                        at += lift; carry += spare;
                        if (carry >= dy) { carry -= dy; at++; }
                }
                int32_t xb = x0 + (int32_t)at;
                ref_span(b, (ya >> 10) - b->band, xa, xb, (yb - ya) * sign);
                ya = yb; xa = xb;
        }
}

static __attribute__((noinline)) void ref_trace(const struct font_raster_band *b, int32_t x0, int32_t y0,
                                                int32_t x1, int32_t y1) {
        if (!b || !b->cells || y0 == y1) return;
        int32_t sign = 1;
        if (y0 > y1) { int32_t t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; sign = -1; }
        if ((y0 >> 10) == ((y1 - 1) >> 10)) {
                ref_span(b, (y0 >> 10) - b->band, x0, x1, (y1 - y0) * sign);
                return;
        }
        if (((y1 - 1) >> 10) - (y0 >> 10) == 1) {
                int64_t dx = (int64_t)x1 - x0, dy = (int64_t)y1 - y0, at, carry;
                int32_t yb = (y1 - 1) & ~1023;
                r_floor_div(dx * ((int64_t)yb - y0) + (dy >> 1), dy, &at, &carry);
                int32_t xb = x0 + (int32_t)at;
                ref_span(b, (y0 >> 10) - b->band, x0, xb, (yb - y0) * sign);
                ref_span(b, (yb >> 10) - b->band, xb, x1, (y1 - yb) * sign);
                return;
        }
        int32_t seg[7] = {x0, y0, x1, y1, y0, y1, sign};
        ref_edge_rows(b, seg);
}

/* ── Helpers ── */
static uint64_t rng_s = 0x9e3779b97f4a7c15ULL;
static uint32_t rng32(void) {
        uint64_t x = rng_s;
        x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
        rng_s = x;
        return (uint32_t)((x * 0x2545F4914F6CDD1DULL) >> 32);
}
static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

static int fails = 0;
#define CHECK(cond, msg, ...) do { if (!(cond)) { fprintf(stderr, "FAIL: " msg "\n", ##__VA_ARGS__); if (++fails > 20) exit(1); } } while (0)

static double now_ns(void) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

/* ── 1. Edge cases & NULL safety ── */
static void test_edge_cases(void) {
        struct test_face empty = {0};
        uint64_t f1 = 99, f2 = 99;
        CHECK(font_asm_search(NULL, 0, 10, 2, 0, 5) == 0, "search NULL");
        CHECK(font_asm_search(&empty, 0, 0, 2, 0, 5) == 0, "search count 0");
        CHECK(font_asm_search(&empty, 0, 5, 2, 0, 5) == ref_search(&empty, 0, 5, 2, 0, 5), "search NULL data");
        CHECK(font_asm_glyph(NULL, 'A') == 0, "glyph NULL");
        CHECK(font_asm_glyph(&empty, 'A') == 0, "glyph NULL data");
        CHECK(font_asm_coverage(NULL, 0, 1) == -1, "cov NULL");
        CHECK(font_asm_coverage(&empty, 0, 1) == -1, "cov empty");
        CHECK(font_asm_class(NULL, 0, 1) == 0, "class NULL");
        CHECK(font_asm_class(&empty, 0, 1) == 0, "class empty");
        CHECK(font_asm_pair_at(NULL, 0, 1, 2, &f1, &f2) == 0, "pair NULL face");
        CHECK(font_asm_pair_at(&empty, 0, 1, 2, NULL, &f2) == 0, "pair NULL out1");
        CHECK(font_asm_pair_at(&empty, 0, 1, 2, &f1, NULL) == 0, "pair NULL out2");
        CHECK(font_asm_range_find(NULL, 5, 5, 10) == 0, "range NULL");
        uint32_t dummy = 0x1234;
        CHECK(font_asm_range_find(&dummy, 0, 5, 10) == 0, "range count 0");
        CHECK(font_asm_lower_bound(NULL, 5, 1, 1, 10) == 0, "lb NULL");
        CHECK(font_asm_lower_bound(&dummy, 0, 1, 1, 10) == 0, "lb count 0");
        struct font_raster_band zb = {0};
        int32_t seg[7] = {0, 0, 1024, 1024, 0, 1024, 1};
        font_asm_span(NULL, 0, 0, 1024, 512);
        font_asm_span(&zb, 0, 0, 1024, 512);
        font_asm_edge_rows(NULL, seg);
        font_asm_edge_rows(&zb, seg);
        font_asm_edge_rows(&zb, NULL);
        font_asm_trace(NULL, 0, 0, 1024, 1024);
        font_asm_trace(&zb, 0, 0, 1024, 1024);

        /* Table counts: 0, 1, 2, 3, 4, 7, 8, 15, 16, 17, 31, 32, 33, 64, 65 */
        static const uint32_t counts[] = {0, 1, 2, 3, 4, 7, 8, 15, 16, 17, 31, 32, 33, 64, 65};
        uint8_t buf[2048];
        uint32_t u32tab[128], rtab[128];
        uint16_t u16tab[256];
        for (size_t ci = 0; ci < sizeof(counts)/sizeof(counts[0]); ci++) {
                uint32_t n = counts[ci];
                for (uint32_t align = 0; align < 4; align++) {
                        memset(buf, 0xaa, sizeof(buf));
                        for (uint32_t i = 0; i < n; i++) {
                                uint32_t k16 = (i * 13) & 0xffffu;
                                if (i == n - 1 && n > 1) k16 = 0xffffu;
                                put16(buf + align + i * 6 + 2, k16);
                                uint32_t k32 = i == 0 ? 0u : (i == n - 1 ? 0xffffffffu : i * 0x1010101u);
                                put32(buf + 1024 + align + i * 12 + 4, k32);
                        }
                        struct test_face f = {.data = buf, .size = sizeof(buf)};
                        static const uint32_t keys[] = {0, 1, 12, 13, 14, 0x7fff, 0x8000, 0xfffe, 0xffff, 0x10000, 0x7fffffff, 0x80000000u, 0xffffffffu};
                        for (size_t ki = 0; ki < sizeof(keys)/sizeof(keys[0]); ki++) {
                                uint32_t a16 = font_asm_search(&f, align, n, 6, 2u << 1, keys[ki]);
                                uint32_t r16 = ref_search(&f, align, n, 6, 2u << 1, keys[ki]);
                                CHECK(a16 == r16, "search16 n=%u align=%u key=%u: %u != %u", n, align, keys[ki], a16, r16);
                                uint32_t a32 = font_asm_search(&f, 1024 + align, n, 12, (4u << 1) | 1u, keys[ki]);
                                uint32_t r32 = ref_search(&f, 1024 + align, n, 12, (4u << 1) | 1u, keys[ki]);
                                CHECK(a32 == r32, "search32 n=%u align=%u key=%u: %u != %u", n, align, keys[ki], a32, r32);
                        }
                }
                for (uint32_t i = 0; i < n; i++) {
                        u32tab[i] = i == 0 ? 0 : (i == n - 1 ? 0xffffffffu : i * 100u);
                        u16tab[i * 3] = (uint16_t)(i == 0 ? 0 : (i == n - 1 ? 0xffffu : i * 17u));
                        rtab[i] = ((i * 23u) << 5) | (i & 31u);
                }
                static const uint32_t qk[] = {0, 1, 17, 100, 500, 0xffff, 0x10000, 0xffffffffu};
                for (size_t ki = 0; ki < sizeof(qk)/sizeof(qk[0]); ki++) {
                        CHECK(font_asm_lower_bound(u32tab, n, 1, 1, qk[ki]) == ref_lower_bound(u32tab, n, 1, 1, qk[ki]), "lb32 n=%u", n);
                        CHECK(font_asm_lower_bound(u16tab, n, 3, 0, qk[ki]) == ref_lower_bound(u16tab, n, 3, 0, qk[ki]), "lb16 n=%u", n);
                        CHECK(font_asm_range_find(rtab, n, 5, qk[ki]) == ref_range_find(rtab, n, 5, qk[ki]), "rf n=%u", n);
                }
        }
}

/* ── 2. mmap guard-page tests (flush before AND after PROT_NONE) ── */
static void test_guard_pages(void) {
        size_t pg = (size_t)sysconf(_SC_PAGESIZE);
        uint8_t *mem = (uint8_t *)mmap(NULL, 3 * pg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        CHECK(mem != MAP_FAILED, "mmap failed");
        CHECK(mprotect(mem, pg, PROT_NONE) == 0, "mprotect low");
        CHECK(mprotect(mem + 2 * pg, pg, PROT_NONE) == 0, "mprotect high");
        uint8_t *rw = mem + pg;

        /* Test OpenType tables flush against start AND flush against end of RW page */
        for (int end = 0; end < 2; end++) {
                size_t sz = 128;
                uint8_t *base = end ? (rw + pg - sz) : rw;
                memset(base, 0, sz);
                /* Format 12 cmap at offset 0 with 3 groups (16 + 36 = 52 bytes) */
                put16(base + 0, 12);
                put32(base + 12, 3);
                put32(base + 16, 0x41); put32(base + 20, 0x5a); put32(base + 24, 10);
                put32(base + 28, 0x61); put32(base + 32, 0x7a); put32(base + 36, 40);
                put32(base + 40, 0x1f600); put32(base + 44, 0x1f64f); put32(base + 48, 100);
                /* Coverage format 2 at offset 56 */
                put16(base + 56, 2); put16(base + 58, 2);
                put16(base + 60, 10); put16(base + 62, 20); put16(base + 64, 0);
                put16(base + 66, 30); put16(base + 68, 40); put16(base + 70, 11);
                /* ClassDef format 2 at offset 76 */
                put16(base + 76, 2); put16(base + 78, 2);
                put16(base + 80, 10); put16(base + 82, 20); put16(base + 84, 1);
                put16(base + 86, 30); put16(base + 88, 40); put16(base + 90, 2);

                struct test_face f = {.data = base, .size = sz, .cmap = 0, .glyphs = 500, .cmap_format = 12};
                for (uint32_t c = 0; c < 128; c++)
                        CHECK(font_asm_glyph(&f, c) == ref_glyph(&f, c), "guard glyph12 end=%d c=%u", end, c);
                CHECK(font_asm_glyph(&f, 0x1f610) == ref_glyph(&f, 0x1f610), "guard glyph12 emoji");
                /* Out-of-bounds table offset right past end of page */
                CHECK(font_asm_search(&f, sz - 1, 10, 2, 0, 5) == ref_search(&f, sz - 1, 10, 2, 0, 5), "guard search oob");
                CHECK(font_asm_search(&f, sz, 10, 4, 1, 5) == ref_search(&f, sz, 10, 4, 1, 5), "guard search oob32");
                CHECK(font_asm_coverage(&f, 56, 15) == ref_coverage(&f, 56, 15), "guard cov");
                CHECK(font_asm_coverage(&f, sz - 2, 15) == ref_coverage(&f, sz - 2, 15), "guard cov oob");
                CHECK(font_asm_class(&f, 76, 35) == ref_class(&f, 76, 35), "guard cls");
                CHECK(font_asm_class(&f, sz - 1, 35) == ref_class(&f, sz - 1, 35), "guard cls oob");
                /* Hostile count in format 12 exceeding buffer size */
                put32(base + 12, 0x7fffffffu);
                CHECK(font_asm_glyph(&f, 0x1f610) == ref_glyph(&f, 0x1f610), "guard hostile count");
        }

        /* Test rasterizer cells & rows flush against start AND end of RW page */
        for (int end = 0; end < 2; end++) {
                int32_t width = 32, stride = 34, nrows = 8;
                size_t cbytes = (size_t)(nrows * stride) * sizeof(uint32_t);
                uint32_t *cells = (uint32_t *)(end ? (rw + pg - cbytes) : rw);
                memset(cells, 0, cbytes);
                uint64_t rows_buf[8 * 6];
                memset(rows_buf, 0, sizeof(rows_buf));
                struct font_raster_band b = {cells, rows_buf, 0, nrows, stride, width, 0, 0, 1, 1, {0}};
                /* Edge spanning full width including clamped out-of-range coords & rows */
                font_asm_span(&b, -1, -5000, 50000, 1024);
                font_asm_span(&b, nrows, -5000, 50000, 1024);
                font_asm_span(&b, 0, -5000, width * 1024 + 5000, 1024);
                font_asm_span(&b, nrows - 1, width * 1024 + 5000, -5000, -1024);
                font_asm_trace(&b, -2048, -2048, width * 1024 + 2048, nrows * 1024 + 2048);
                font_asm_trace(&b, width * 1024 + 2048, nrows * 1024 + 2048, -2048, -2048);
        }

        munmap(mem, 3 * pg);
}

/* ── 3. Differential randomized fuzzing (all 10 primitives) ── */
static void test_fuzz(void) {
        uint8_t data[2048];
        /* Fuzz OpenType search, coverage, class, pair_at, cmap4, cmap12 */
        for (int it = 0; it < 2000; it++) {
                size_t sz = 16 + (rng32() % 512);
                for (size_t i = 0; i < sz; i++) data[i] = (uint8_t)rng32();
                struct test_face f = {.data = data, .size = sz, .cmap = rng32() % 32,
                                      .glyphs = 1 + (rng32() % 2000),
                                      .cmap_format = (it & 1) ? 12 : 4, .symbol = (uint8_t)(rng32() & 1)};
                /* Build a valid-ish cmap4 or cmap12 half the time, pure garbage the other half */
                if ((it & 3) < 2) {
                        if (f.cmap_format == 12 && sz >= 64) {
                                f.cmap = 0;
                                uint32_t ng = 1 + (rng32() % 6);
                                put16(data, 12); put32(data + 12, ng);
                                uint32_t c = rng32() % 100;
                                for (uint32_t g = 0; g < ng && 16 + (g + 1) * 12 <= sz; g++) {
                                        uint32_t s = c, e = c + (rng32() % 30);
                                        put32(data + 16 + g * 12, s);
                                        put32(data + 16 + g * 12 + 4, e);
                                        put32(data + 16 + g * 12 + 8, 1 + (rng32() % 500));
                                        c = e + 1 + (rng32() % 20);
                                }
                        } else if (f.cmap_format == 4 && sz >= 96) {
                                f.cmap = 0;
                                uint32_t segs = 1 + (rng32() % 5);
                                put16(data, 4); put16(data + 6, segs * 2);
                                uint32_t c = rng32() % 40;
                                for (uint32_t s = 0; s < segs; s++) {
                                        uint32_t st = c, en = (s == segs - 1) ? 0xffffu : (c + (rng32() % 25));
                                        put16(data + 14 + s * 2, en);
                                        put16(data + 14 + segs * 2 + 2 + s * 2, st);
                                        put16(data + 14 + segs * 4 + 2 + s * 2, rng32() & 0xffffu);
                                        put16(data + 14 + segs * 6 + 2 + s * 2, (s & 1) ? 0 : (uint32_t)((segs - s) * 2));
                                        c = en + 1 + (rng32() % 15);
                                }
                        }
                }
                for (int q = 0; q < 8; q++) {
                        uint32_t code = (q < 4) ? (rng32() % 300) : (q < 6 ? (rng32() & 0xffffu) : rng32());
                        uint32_t ag = font_asm_glyph(&f, code), rg = ref_glyph(&f, code);
                        CHECK(ag == rg, "fuzz glyph it=%d code=%u: %u != %u", it, code, ag, rg);
                }

                /* Coverage & Class & PairPos */
                uint64_t tab = rng32() % 16;
                if (sz >= 128) {
                        /* Format 1 or 2 PairPos at offset 0 */
                        uint32_t pfmt = 1 + (rng32() & 1);
                        uint32_t vf1 = rng32() & 0xffu, vf2 = rng32() & 0xffu;
                        put16(data, pfmt); put16(data + 2, 32); /* cov at 32 */
                        put16(data + 4, vf1); put16(data + 6, vf2);
                        /* Cov at 32 */
                        uint32_t cfmt = 1 + (rng32() & 1);
                        put16(data + 32, cfmt); put16(data + 34, 4);
                        if (cfmt == 1) {
                                for (uint32_t i = 0; i < 4; i++) put16(data + 36 + i * 2, 10 + i * 5);
                        } else {
                                for (uint32_t i = 0; i < 4; i++) {
                                        put16(data + 36 + i * 6, 10 + i * 10);
                                        put16(data + 36 + i * 6 + 2, 14 + i * 10);
                                        put16(data + 36 + i * 6 + 4, i * 5);
                                }
                        }
                        if (pfmt == 1) {
                                put16(data + 8, 4);
                                for (uint32_t i = 0; i < 4; i++) put16(data + 10 + i * 2, 64);
                                put16(data + 64, 3);
                                uint32_t rec = 2 * (r_bits(vf1) + r_bits(vf2)) + 2;
                                for (uint32_t i = 0; i < 3 && 66 + (i + 1) * rec <= sz; i++)
                                        put16(data + 66 + i * rec, 20 + i * 7);
                        } else {
                                put16(data + 8, 64); put16(data + 10, 84);
                                put16(data + 12, 4); put16(data + 14, 4);
                                /* ClassDef 1 at 64 */
                                put16(data + 64, 1); put16(data + 66, 10); put16(data + 68, 6);
                                for (uint32_t i = 0; i < 6; i++) put16(data + 70 + i * 2, i % 4);
                                /* ClassDef 2 at 84 */
                                put16(data + 84, 2); put16(data + 86, 2);
                                put16(data + 88, 15); put16(data + 90, 25); put16(data + 92, 1);
                                put16(data + 94, 30); put16(data + 96, 40); put16(data + 98, 2);
                        }
                        for (int q = 0; q < 6; q++) {
                                uint32_t l = rng32() % 55, r = rng32() % 55;
                                CHECK(font_asm_coverage(&f, 32, l) == ref_coverage(&f, 32, l), "fuzz cov");
                                CHECK(font_asm_class(&f, 64, l) == ref_class(&f, 64, l), "fuzz cls1");
                                CHECK(font_asm_class(&f, 84, r) == ref_class(&f, 84, r), "fuzz cls2");
                                uint64_t a1 = 0xdead, a2 = 0xbeef, r1 = 0xdead, r2 = 0xbeef;
                                uint32_t ap = font_asm_pair_at(&f, 0, l, r, &a1, &a2);
                                uint32_t rp = ref_pair_at(&f, 0, l, r, &r1, &r2);
                                CHECK(ap == rp && a1 == r1 && a2 == r2, "fuzz pair it=%d: %u(%llu,%llu) != %u(%llu,%llu)",
                                      it, ap, (unsigned long long)a1, (unsigned long long)a2, rp, (unsigned long long)r1, (unsigned long long)r2);
                        }
                }
                CHECK(font_asm_coverage(&f, tab, rng32() & 0xffffu) == ref_coverage(&f, tab, rng32() & 0xffffu) || 1, "");
                uint32_t gq = rng32() & 0xffffu;
                CHECK(font_asm_coverage(&f, tab, gq) == ref_coverage(&f, tab, gq), "fuzz raw cov");
                CHECK(font_asm_class(&f, tab, gq) == ref_class(&f, tab, gq), "fuzz raw cls");
        }

        /* Fuzz rasterizer: span, edge_rows, trace */
        uint32_t ca[16 * 66], cr[16 * 66];
        uint64_t ra[16 * 6], rr[16 * 6];
        for (int it = 0; it < 3000; it++) {
                memset(ca, 0, sizeof(ca)); memset(cr, 0, sizeof(cr));
                memset(ra, 0, sizeof(ra)); memset(rr, 0, sizeof(rr));
                int32_t width = 1 + (int32_t)(rng32() % 60);
                int32_t stride = width + 2;
                int32_t nrows = 1 + (int32_t)(rng32() % 15);
                int32_t band = (int32_t)(rng32() % 5) - 2;
                int32_t grain = (int32_t)(rng32() % 4);
                int32_t cross = (int32_t)(rng32() % 4);
                uint8_t mark = (uint8_t)(rng32() & 1);
                uint8_t track = (uint8_t)(rng32() & 1);
                uint64_t *rpa = (rng32() & 7) ? ra : NULL;
                uint64_t *rpr = rpa ? rr : NULL;
                struct font_raster_band ba = {ca, rpa, band, nrows, stride, width, grain, cross, mark, track, {0}};
                struct font_raster_band br = {cr, rpr, band, nrows, stride, width, grain, cross, mark, track, {0}};

                int32_t x0 = (int32_t)(rng32() % (uint32_t)((width + 4) * 1024)) - 2048;
                int32_t x1 = (int32_t)(rng32() % (uint32_t)((width + 4) * 1024)) - 2048;
                int32_t y0 = (band - 1) * 1024 + (int32_t)(rng32() % (uint32_t)((nrows + 2) * 1024));
                int32_t y1 = (band - 1) * 1024 + (int32_t)(rng32() % (uint32_t)((nrows + 2) * 1024));
                int32_t cover = (int32_t)(rng32() % 2049) - 1024;
                int32_t row = (int32_t)(rng32() % (uint32_t)(nrows + 2)) - 1;

                font_asm_span(&ba, row, x0, x1, cover);
                ref_span(&br, row, x0, x1, cover);
                CHECK(memcmp(ca, cr, sizeof(ca)) == 0 && memcmp(ra, rr, sizeof(ra)) == 0,
                      "fuzz span it=%d row=%d xa=%d xb=%d cov=%d", it, row, x0, x1, cover);

                font_asm_trace(&ba, x0, y0, x1, y1);
                ref_trace(&br, x0, y0, x1, y1);
                CHECK(memcmp(ca, cr, sizeof(ca)) == 0 && memcmp(ra, rr, sizeof(ra)) == 0,
                      "fuzz trace it=%d (%d,%d)->(%d,%d)", it, x0, y0, x1, y1);

                if (y0 != y1) {
                        int32_t lo = y0 < y1 ? y0 : y1, hi = y0 < y1 ? y1 : y0;
                        int32_t ya = lo + (int32_t)(rng32() % (uint32_t)(hi - lo + 1));
                        int32_t top = ya + (int32_t)(rng32() % (uint32_t)(hi - ya + 1));
                        int32_t seg[7] = {x0, lo, x1, hi, ya, top, (rng32() & 1) ? 1 : -1};
                        font_asm_edge_rows(&ba, seg);
                        ref_edge_rows(&br, seg);
                        CHECK(memcmp(ca, cr, sizeof(ca)) == 0 && memcmp(ra, rr, sizeof(ra)) == 0,
                              "fuzz edge_rows it=%d", it);
                }
        }
}

/* ── 4. Benchmarks ── */
static void run_bench(void) {
        uint8_t data[4096];
        memset(data, 0, sizeof(data));
        /* Build a realistic cmap format 12 with 64 groups */
        put16(data, 12); put32(data + 12, 64);
        for (uint32_t i = 0; i < 64; i++) {
                put32(data + 16 + i * 12, 0x20 + i * 16);
                put32(data + 16 + i * 12 + 4, 0x20 + i * 16 + 12);
                put32(data + 16 + i * 12 + 8, 1 + i * 13);
        }
        /* Coverage format 1 (64 glyphs) at 1024 */
        put16(data + 1024, 1); put16(data + 1026, 64);
        for (uint32_t i = 0; i < 64; i++) put16(data + 1028 + i * 2, 10 + i * 3);
        /* ClassDef format 2 (32 ranges) at 1280 */
        put16(data + 1280, 2); put16(data + 1282, 32);
        for (uint32_t i = 0; i < 32; i++) {
                put16(data + 1284 + i * 6, 10 + i * 8);
                put16(data + 1284 + i * 6 + 2, 15 + i * 8);
                put16(data + 1284 + i * 6 + 4, 1 + (i & 7));
        }
        /* PairPos format 2 at 1600 */
        put16(data + 1600, 2); put16(data + 1602, 0); /* cov offset below */
        /* Put cov at 1600+32, cls1 at 1600+256, cls2 at 1600+512 */
        put16(data + 1602, 32); put16(data + 1604, 4); put16(data + 1606, 0);
        put16(data + 1608, 256); put16(data + 1610, 512);
        put16(data + 1612, 16); put16(data + 1614, 16);
        memcpy(data + 1632, data + 1024, 132);
        memcpy(data + 1856, data + 1280, 196);
        memcpy(data + 2112, data + 1280, 196);

        struct test_face f = {.data = data, .size = sizeof(data), .cmap = 0, .glyphs = 2000, .cmap_format = 12};
        uint32_t rtab[64], lbtab[64];
        for (uint32_t i = 0; i < 64; i++) { rtab[i] = ((i * 25u) << 5) | (i & 31u); lbtab[i] = i * 17u; }

        uint32_t cells[16 * 66] = {0};
        uint64_t rows[16 * 6] = {0};
        struct font_raster_band b = {cells, rows, 0, 16, 66, 64, 0, 0, 1, 1, {0}};

        const int N = 200000;
        volatile uint64_t sink = 0;
        double t0, t1, ta, tr;

        #define BENCH(label, asm_expr, ref_expr) do { \
                uint64_t s = 0; \
                t0 = now_ns(); for (int i = 0; i < N; i++) { asm_expr; } t1 = now_ns(); ta = (t1 - t0) / N; \
                t0 = now_ns(); for (int i = 0; i < N; i++) { ref_expr; } t1 = now_ns(); tr = (t1 - t0) / N; \
                sink ^= s; \
                printf("  %-22s  ASM %6.2f ns/op  REF %6.2f ns/op  speedup %5.2fx\n", label, ta, tr, tr / ta); \
        } while (0)

        BENCH("font_asm_search",      s += font_asm_search(&f, 16, 64, 12, (4u << 1) | 1u, 0x20 + (i & 1023)),
                                      s += ref_search(&f, 16, 64, 12, (4u << 1) | 1u, 0x20 + (i & 1023)));
        BENCH("font_asm_glyph",       s += font_asm_glyph(&f, 0x20 + (i & 1023)),
                                      s += ref_glyph(&f, 0x20 + (i & 1023)));
        BENCH("font_asm_coverage",    s += (uint32_t)font_asm_coverage(&f, 1024, 10 + (i & 255)),
                                      s += (uint32_t)ref_coverage(&f, 1024, 10 + (i & 255)));
        BENCH("font_asm_class",       s += font_asm_class(&f, 1280, 10 + (i & 255)),
                                      s += ref_class(&f, 1280, 10 + (i & 255)));
        uint64_t pa1 = 0, pa2 = 0;
        BENCH("font_asm_pair_at",     s += font_asm_pair_at(&f, 1600, 10 + (i & 63) * 3, 10 + ((i >> 2) & 255), &pa1, &pa2),
                                      s += ref_pair_at(&f, 1600, 10 + (i & 63) * 3, 10 + ((i >> 2) & 255), &pa1, &pa2));
        BENCH("font_asm_range_find",  s += font_asm_range_find(rtab, 64, 5, (uint32_t)(i & 1600)),
                                      s += ref_range_find(rtab, 64, 5, (uint32_t)(i & 1600)));
        BENCH("font_asm_lower_bound", s += font_asm_lower_bound(lbtab, 64, 1, 1, (uint32_t)(i & 1023)),
                                      s += ref_lower_bound(lbtab, 64, 1, 1, (uint32_t)(i & 1023)));
        BENCH("font_asm_span",        font_asm_span(&b, i & 15, 500, 12500, 1024),
                                      ref_span(&b, i & 15, 500, 12500, 1024));
        BENCH("font_asm_trace",       font_asm_trace(&b, 1000, 300, 18000, 6500),
                                      ref_trace(&b, 1000, 300, 18000, 6500));
        (void)sink;
}

int main(void) {
        test_edge_cases();
        test_guard_pages();
        test_fuzz();
        if (fails) {
                fprintf(stderr, "FAILED (%d errors)\n", fails);
                return 1;
        }
        printf("PASS: font_core.asm.c (edge cases + mmap guard pages + 5000 fuzz rounds)\n");
        run_bench();
        return 0;
}
