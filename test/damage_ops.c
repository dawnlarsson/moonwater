/*
        Differential C-reference parity, security, guard-page, fuzz & throughput/size
        benchmark for src/canvas/compositor/damage_ops.asm.c (arm64, x86-64, riscv64 pure-ASM).

        Build & run:
          cc -O2 -Wall -Werror test/damage_ops.c -o /tmp/t_dmg && /tmp/t_dmg
          cc -O2 -fsanitize=address,undefined -Wall -Werror test/damage_ops.c -o /tmp/t_dmg_asan && /tmp/t_dmg_asan
*/
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define COMPOSITOR_DAMAGE_OPS_ASM_IMPLEMENTATION
#include "../src/canvas/compositor/damage_ops.asm.c"
/* Verify double-emit guard is idempotent */
#include "../src/canvas/compositor/damage_ops.asm.c"

/* ── Reference C implementations (noinline + external linkage for exact nm --print-size) ── */

static inline uint64_t ref_u64_area(struct dmg_rect r)
{
        int64_t dx = (int64_t)r.x2 - (int64_t)r.x1;
        int64_t dy = (int64_t)r.y2 - (int64_t)r.y1;
        if (dx <= 0 || dy <= 0)
                return 0;
        return (uint64_t)dx * (uint64_t)dy;
}

__attribute__((noinline)) dmg_i32 ref_dmg_rect_empty(const struct dmg_rect *r)
{
        if (!r)
                return 1;
        return r->x2 <= r->x1 || r->y2 <= r->y1;
}

__attribute__((noinline)) dmg_i64 ref_dmg_rect_area(const struct dmg_rect *r)
{
        if (!r)
                return 0;
        uint64_t a = ref_u64_area(*r);
        return a > (uint64_t)INT64_MAX ? INT64_MAX : (dmg_i64)a;
}

__attribute__((noinline)) void ref_dmg_rect_join(const struct dmg_rect *a, const struct dmg_rect *b, struct dmg_rect *out)
{
        if (!a || !b || !out)
                return;
        struct dmg_rect j = {
                a->x1 < b->x1 ? a->x1 : b->x1,
                a->y1 < b->y1 ? a->y1 : b->y1,
                a->x2 > b->x2 ? a->x2 : b->x2,
                a->y2 > b->y2 ? a->y2 : b->y2,
        };
        *out = j;
}

__attribute__((noinline)) dmg_i32 ref_dmg_rect_meet(const struct dmg_rect *a, const struct dmg_rect *b, struct dmg_rect *out)
{
        if (!a || !b) {
                if (out)
                        *out = (struct dmg_rect){0, 0, 0, 0};
                return 0;
        }
        struct dmg_rect m = {
                a->x1 > b->x1 ? a->x1 : b->x1,
                a->y1 > b->y1 ? a->y1 : b->y1,
                a->x2 < b->x2 ? a->x2 : b->x2,
                a->y2 < b->y2 ? a->y2 : b->y2,
        };
        if (m.x2 <= m.x1 || m.y2 <= m.y1) {
                if (out)
                        *out = (struct dmg_rect){0, 0, 0, 0};
                return 0;
        }
        if (out)
                *out = m;
        return 1;
}

__attribute__((noinline)) dmg_i32 ref_dmg_rect_cover(const struct dmg_rect *outer, const struct dmg_rect *inner)
{
        if (!inner || inner->x2 <= inner->x1 || inner->y2 <= inner->y1)
                return 1;
        if (!outer)
                return 0;
        return outer->x1 <= inner->x1 && outer->y1 <= inner->y1 &&
               outer->x2 >= inner->x2 && outer->y2 >= inner->y2;
}

__attribute__((noinline)) dmg_i32 ref_dmg_rect_cut(const struct dmg_rect *r, const struct dmg_rect *cut, struct dmg_rect out[4])
{
        if (!r || !out)
                return 0;
        struct dmg_rect rv = *r;
        if (rv.x2 <= rv.x1 || rv.y2 <= rv.y1)
                return 0;
        struct dmg_rect m;
        if (!cut || !ref_dmg_rect_meet(&rv, cut, &m)) {
                out[0] = rv;
                return 1;
        }
        struct dmg_rect parts[4] = {
                {rv.x1, rv.y1, rv.x2, m.y1},
                {rv.x1, m.y2, rv.x2, rv.y2},
                {rv.x1, m.y1, m.x1, m.y2},
                {m.x2, m.y1, rv.x2, m.y2},
        };
        dmg_i32 n = 0;
        for (int k = 0; k < 4; k++)
                if (parts[k].x2 > parts[k].x1 && parts[k].y2 > parts[k].y1)
                        out[n++] = parts[k];
        return n;
}

__attribute__((noinline)) void ref_dmg_add(struct dmg_list *d, const struct dmg_rect *rp, dmg_i64 slack)
{
        if (!d || !rp)
                return;
        struct dmg_rect r = *rp;
        if (r.x2 <= r.x1 || r.y2 <= r.y1)
                return;
        dmg_u32 count = d->count > DMG_LIST_CAP ? DMG_LIST_CAP : d->count;
        uint64_t area_r = ref_u64_area(r);
        dmg_u32 at = 0;

        while (at < count) {
                struct dmg_rect e = d->r[at];
                struct dmg_rect j = {
                        r.x1 < e.x1 ? r.x1 : e.x1,
                        r.y1 < e.y1 ? r.y1 : e.y1,
                        r.x2 > e.x2 ? r.x2 : e.x2,
                        r.y2 > e.y2 ? r.y2 : e.y2,
                };
                uint64_t area_j = ref_u64_area(j);
                uint64_t area_e = ref_u64_area(e);
                uint64_t diff = area_j - area_r;
                int merge = 0;
                if (slack >= 0) {
                        uint64_t s = (uint64_t)slack;
                        uint64_t sum = area_e + s;
                        merge = (sum < area_e) || (diff <= sum);
                } else {
                        uint64_t ns = 0ULL - (uint64_t)slack;
                        merge = (area_e >= ns) && (diff <= area_e - ns);
                }
                if (merge) {
                        r = j;
                        area_r = area_j;
                        d->r[at] = d->r[--count];
                        at = 0;
                } else {
                        at++;
                }
        }
        if (count == DMG_LIST_CAP) {
                dmg_u32 best = 0;
                uint64_t least = UINT64_MAX;
                for (at = 0; at < DMG_LIST_CAP; at++) {
                        struct dmg_rect e = d->r[at];
                        struct dmg_rect j = {
                                r.x1 < e.x1 ? r.x1 : e.x1,
                                r.y1 < e.y1 ? r.y1 : e.y1,
                                r.x2 > e.x2 ? r.x2 : e.x2,
                                r.y2 > e.y2 ? r.y2 : e.y2,
                        };
                        uint64_t grown = ref_u64_area(j) - ref_u64_area(e);
                        if (grown < least) {
                                least = grown;
                                best = at;
                        }
                }
                ref_dmg_rect_join(&r, &d->r[best], &d->r[best]);
                d->count = count;
                return;
        }
        d->r[count++] = r;
        d->count = count;
}

__attribute__((noinline)) dmg_i32 ref_dmg_subtract(struct dmg_list *d, const struct dmg_rect *cutp)
{
        if (!d)
                return 0;
        dmg_u32 count = d->count > DMG_LIST_CAP ? DMG_LIST_CAP : d->count;
        if (!count || !cutp)
                return 1;
        struct dmg_rect cut = *cutp;
        if (cut.x2 <= cut.x1 || cut.y2 <= cut.y1)
                return 1;
        struct dmg_list out;
        out.count = 0;
        for (dmg_u32 at = 0; at < count; at++) {
                struct dmg_rect r = d->r[at];
                if (r.x2 <= cut.x1 || r.x1 >= cut.x2 || r.y2 <= cut.y1 || r.y1 >= cut.y2) {
                        if (out.count == DMG_LIST_CAP)
                                return 0;
                        out.r[out.count++] = r;
                        continue;
                }
                if (r.x2 <= r.x1 || r.y2 <= r.y1)
                        continue;
                struct dmg_rect pieces[4];
                dmg_i32 n = ref_dmg_rect_cut(&r, &cut, pieces);
                for (dmg_i32 k = 0; k < n; k++) {
                        if (out.count == DMG_LIST_CAP)
                                return 0;
                        out.r[out.count++] = pieces[k];
                }
        }
        for (dmg_u32 i = 0; i < out.count; i++)
                d->r[i] = out.r[i];
        d->count = out.count;
        return 1;
}

/* ── Helpers ── */

static int g_fails = 0;

#define CHECK(cond, ...) do { \
        if (!(cond)) { \
                fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
                fprintf(stderr, __VA_ARGS__); \
                fprintf(stderr, "\n"); \
                g_fails++; \
        } \
} while (0)

static int rect_eq(struct dmg_rect a, struct dmg_rect b)
{
        return a.x1 == b.x1 && a.y1 == b.y1 && a.x2 == b.x2 && a.y2 == b.y2;
}

static int list_eq(const struct dmg_list *a, const struct dmg_list *b)
{
        if (a->count != b->count)
                return 0;
        for (dmg_u32 i = 0; i < a->count; i++)
                if (!rect_eq(a->r[i], b->r[i]))
                        return 0;
        return 1;
}

static uint64_t rng_state = 0x9e3779b97f4a7c15ULL;
static uint64_t xorshift64(void)
{
        uint64_t x = rng_state;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        rng_state = x;
        return x;
}

static struct dmg_rect rand_rect(int mode)
{
        struct dmg_rect r;
        if (mode == 0) {
                /* Typical screen coords 0..4096 */
                int32_t x = (int32_t)(xorshift64() % 3840);
                int32_t y = (int32_t)(xorshift64() % 2160);
                int32_t w = (int32_t)(xorshift64() % 512) - 16;
                int32_t h = (int32_t)(xorshift64() % 512) - 16;
                r = (struct dmg_rect){x, y, x + w, y + h};
        } else if (mode == 1) {
                /* Small grid -20..20 (high collision & exact-edge density) */
                int32_t x1 = (int32_t)(xorshift64() % 41) - 20;
                int32_t y1 = (int32_t)(xorshift64() % 41) - 20;
                int32_t x2 = (int32_t)(xorshift64() % 41) - 20;
                int32_t y2 = (int32_t)(xorshift64() % 41) - 20;
                r = (struct dmg_rect){x1, y1, x2, y2};
        } else {
                /* Full INT32_MIN..INT32_MAX adversarial coords */
                r = (struct dmg_rect){
                        (int32_t)xorshift64(), (int32_t)xorshift64(),
                        (int32_t)xorshift64(), (int32_t)xorshift64(),
                };
        }
        return r;
}

/* ── 1. Edge-case & Security Tests ── */

static void test_edges_and_security(void)
{
        struct dmg_rect r = {10, 20, 100, 200};
        struct dmg_rect out, out4[4];
        struct dmg_list d;

        /* NULL safety across every function */
        CHECK(dmg_rect_empty(NULL) == 1, "empty(NULL)");
        CHECK(dmg_rect_area(NULL) == 0, "area(NULL)");
        dmg_rect_join(NULL, &r, &out);
        dmg_rect_join(&r, NULL, &out);
        dmg_rect_join(&r, &r, NULL);
        out = r;
        CHECK(dmg_rect_meet(NULL, &r, &out) == 0 && rect_eq(out, (struct dmg_rect){0, 0, 0, 0}), "meet(NULL, r, out)");
        out = r;
        CHECK(dmg_rect_meet(&r, NULL, &out) == 0 && rect_eq(out, (struct dmg_rect){0, 0, 0, 0}), "meet(r, NULL, out)");
        CHECK(dmg_rect_meet(&r, &r, NULL) == 1, "meet(r, r, NULL)");
        CHECK(dmg_rect_meet(NULL, NULL, NULL) == 0, "meet(NULL, NULL, NULL)");
        CHECK(dmg_rect_cover(&r, NULL) == 1, "cover(r, NULL)");
        CHECK(dmg_rect_cover(NULL, NULL) == 1, "cover(NULL, NULL)");
        CHECK(dmg_rect_cover(NULL, &r) == 0, "cover(NULL, non-empty)");
        struct dmg_rect empty_r = {5, 5, 5, 10};
        CHECK(dmg_rect_cover(NULL, &empty_r) == 1, "cover(NULL, empty)");
        CHECK(dmg_rect_cut(NULL, &r, out4) == 0, "cut(NULL, r, out)");
        CHECK(dmg_rect_cut(&r, &r, NULL) == 0, "cut(r, r, NULL)");
        CHECK(dmg_rect_cut(&r, NULL, out4) == 1 && rect_eq(out4[0], r), "cut(r, NULL, out)");
        dmg_add(NULL, &r, 0);
        memset(&d, 0, sizeof(d));
        dmg_add(&d, NULL, 0);
        CHECK(d.count == 0, "add(d, NULL)");
        CHECK(dmg_subtract(NULL, &r) == 0, "subtract(NULL, r)");
        CHECK(dmg_subtract(&d, NULL) == 1 && d.count == 0, "subtract(empty, NULL)");

        /* INT32_MIN / INT32_MAX overflow-safe area, join, meet, cover, cut */
        struct dmg_rect full_i32 = {INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX};
        struct dmg_rect wide_i32 = {INT32_MIN, 0, INT32_MAX, 1};
        struct dmg_rect tall_i32 = {0, INT32_MIN, 2, INT32_MAX};
        struct dmg_rect inv_i32  = {INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN};
        struct dmg_rect huge_ok  = {0, 0, INT32_MAX, INT32_MAX};

        CHECK(dmg_rect_empty(&full_i32) == 0, "empty(full_i32)");
        CHECK(dmg_rect_empty(&inv_i32) == 1, "empty(inv_i32)");
        CHECK(dmg_rect_area(&full_i32) == INT64_MAX, "area(full_i32) saturates INT64_MAX");
        CHECK(dmg_rect_area(&wide_i32) == 0xffffffffLL, "area(wide_i32) == 2^32-1");
        CHECK(dmg_rect_area(&tall_i32) == 2LL * 0xffffffffLL, "area(tall_i32) == 2*(2^32-1)");
        CHECK(dmg_rect_area(&inv_i32) == 0, "area(inv_i32) == 0");
        CHECK(dmg_rect_area(&huge_ok) == (int64_t)INT32_MAX * (int64_t)INT32_MAX, "area(huge_ok)");

        /* Aliasing safety: out == a, out == b, out == r, out == cut */
        struct dmg_rect a = {10, 20, 50, 60}, b = {30, 10, 70, 40};
        struct dmg_rect a_copy = a, b_copy = b, ref_o;
        dmg_rect_join(&a_copy, &b_copy, &a_copy);
        ref_dmg_rect_join(&a, &b, &ref_o);
        CHECK(rect_eq(a_copy, ref_o), "join alias out==a");
        a_copy = a; b_copy = b;
        dmg_rect_join(&a_copy, &b_copy, &b_copy);
        CHECK(rect_eq(b_copy, ref_o), "join alias out==b");

        a_copy = a; b_copy = b;
        dmg_i32 rm = ref_dmg_rect_meet(&a, &b, &ref_o);
        CHECK(dmg_rect_meet(&a_copy, &b_copy, &a_copy) == rm && rect_eq(a_copy, ref_o), "meet alias out==a");
        a_copy = a; b_copy = b;
        CHECK(dmg_rect_meet(&a_copy, &b_copy, &b_copy) == rm && rect_eq(b_copy, ref_o), "meet alias out==b");

        /* Full 4-piece cut + aliasing out == r */
        struct dmg_rect hole = {20, 30, 40, 50};
        struct dmg_rect cut_out[4], ref_out[4];
        dmg_i32 cn = dmg_rect_cut(&a, &hole, cut_out);
        dmg_i32 rn = ref_dmg_rect_cut(&a, &hole, ref_out);
        CHECK(cn == 4 && rn == 4, "4-piece cut count");
        for (int k = 0; k < 4; k++)
                CHECK(rect_eq(cut_out[k], ref_out[k]), "4-piece cut[%d]", k);
        cut_out[0] = a;
        cn = dmg_rect_cut(&cut_out[0], &hole, cut_out);
        CHECK(cn == 4 && rect_eq(cut_out[0], ref_out[0]) && rect_eq(cut_out[3], ref_out[3]), "cut alias out==r");

        /* dmg_add: coalescing cascade, cap-8 min-growth merge, extreme slacks, corrupted count */
        struct dmg_list dl_asm, dl_ref;
        memset(&dl_asm, 0, sizeof(dl_asm));
        memset(&dl_ref, 0, sizeof(dl_ref));
        /* Add 8 disjoint rects spaced far apart, then a 9th close to rect #3 */
        for (int i = 0; i < 8; i++) {
                struct dmg_rect ri = {i * 1000, 0, i * 1000 + 10, 10};
                dmg_add(&dl_asm, &ri, 0);
                ref_dmg_add(&dl_ref, &ri, 0);
        }
        CHECK(dl_asm.count == 8 && list_eq(&dl_asm, &dl_ref), "add 8 disjoint");
        struct dmg_rect r9 = {3015, 0, 3025, 10};
        dmg_add(&dl_asm, &r9, 0);
        ref_dmg_add(&dl_ref, &r9, 0);
        CHECK(dl_asm.count == 8 && list_eq(&dl_asm, &dl_ref), "add 9th min-growth merge");

        /* Corrupted count > 8 clamped safely */
        dl_asm.count = 999u;
        dl_ref.count = 999u;
        dmg_add(&dl_asm, &r9, 0);
        ref_dmg_add(&dl_ref, &r9, 0);
        CHECK(dl_asm.count == 8 && list_eq(&dl_asm, &dl_ref), "add corrupted count clamp");

        /* Extreme slacks: INT64_MAX and INT64_MIN */
        memset(&dl_asm, 0, sizeof(dl_asm));
        memset(&dl_ref, 0, sizeof(dl_ref));
        dmg_add(&dl_asm, &wide_i32, INT64_MAX);
        ref_dmg_add(&dl_ref, &wide_i32, INT64_MAX);
        dmg_add(&dl_asm, &tall_i32, INT64_MAX);
        ref_dmg_add(&dl_ref, &tall_i32, INT64_MAX);
        CHECK(dl_asm.count == 2 && list_eq(&dl_asm, &dl_ref), "add INT64_MAX slack (exact 2^64-area > 2^63 slack)");

        struct dmg_rect med_r = {0, 0, 2, 1000000000};
        memset(&dl_asm, 0, sizeof(dl_asm));
        memset(&dl_ref, 0, sizeof(dl_ref));
        dmg_add(&dl_asm, &wide_i32, INT64_MAX);
        ref_dmg_add(&dl_ref, &wide_i32, INT64_MAX);
        dmg_add(&dl_asm, &med_r, INT64_MAX);
        ref_dmg_add(&dl_ref, &med_r, INT64_MAX);
        CHECK(dl_asm.count == 1 && list_eq(&dl_asm, &dl_ref), "add INT64_MAX slack merge");

        memset(&dl_asm, 0, sizeof(dl_asm));
        memset(&dl_ref, 0, sizeof(dl_ref));
        dmg_add(&dl_asm, &a, INT64_MIN);
        ref_dmg_add(&dl_ref, &a, INT64_MIN);
        dmg_add(&dl_asm, &b, INT64_MIN);
        ref_dmg_add(&dl_ref, &b, INT64_MIN);
        CHECK(dl_asm.count == 2 && list_eq(&dl_asm, &dl_ref), "add INT64_MIN slack");

        /* dmg_subtract: overflow rollback leaves list untouched */
        memset(&dl_asm, 0, sizeof(dl_asm));
        for (int i = 0; i < 3; i++) {
                struct dmg_rect ri = {0, i * 100, 100, i * 100 + 80};
                dmg_add(&dl_asm, &ri, 0);
        }
        /* Cut a vertical strip through the middle of all 3 rects -> 6 pieces (fits in 8) */
        struct dmg_rect strip = {40, 0, 60, 300};
        dl_ref = dl_asm;
        CHECK(dmg_subtract(&dl_asm, &strip) == 1 && ref_dmg_subtract(&dl_ref, &strip) == 1, "subtract 6 pieces ok");
        CHECK(dl_asm.count == 6 && list_eq(&dl_asm, &dl_ref), "subtract 6 pieces match");

        /* Now cut a small box inside 3 of the 6 pieces -> 3*4 + 3 = 15 > 8 -> must return 0 and leave list UNTOUCHED */
        struct dmg_list before = dl_asm;
        struct dmg_rect big_hole = {10, 10, 30, 270};
        CHECK(dmg_subtract(&dl_asm, &big_hole) == 0, "subtract >8 returns 0");
        CHECK(list_eq(&dl_asm, &before), "subtract >8 leaves list untouched");

        /* Empty/inverted cut leaves list untouched and returns 1 */
        CHECK(dmg_subtract(&dl_asm, &inv_i32) == 1 && list_eq(&dl_asm, &before), "subtract inverted cut no-op");
}

/* ── 2. Randomized Differential Fuzzing ── */

static void test_differential_fuzz(void)
{
        for (int i = 0; i < 40000; i++) {
                int mode = i % 3;
                struct dmg_rect a = rand_rect(mode);
                struct dmg_rect b = rand_rect(mode);
                struct dmg_rect oa, or_;

                CHECK(dmg_rect_empty(&a) == ref_dmg_rect_empty(&a), "fuzz empty %d", i);
                CHECK(dmg_rect_area(&a) == ref_dmg_rect_area(&a), "fuzz area %d", i);

                dmg_rect_join(&a, &b, &oa);
                ref_dmg_rect_join(&a, &b, &or_);
                CHECK(rect_eq(oa, or_), "fuzz join %d", i);

                dmg_i32 ma = dmg_rect_meet(&a, &b, &oa);
                dmg_i32 mr = ref_dmg_rect_meet(&a, &b, &or_);
                CHECK(ma == mr && rect_eq(oa, or_), "fuzz meet %d", i);

                CHECK(dmg_rect_cover(&a, &b) == ref_dmg_rect_cover(&a, &b), "fuzz cover %d", i);

                struct dmg_rect ca[4], cr[4];
                dmg_i32 na = dmg_rect_cut(&a, &b, ca);
                dmg_i32 nr = ref_dmg_rect_cut(&a, &b, cr);
                CHECK(na == nr, "fuzz cut n %d", i);
                for (dmg_i32 k = 0; k < na && k < nr; k++)
                        CHECK(rect_eq(ca[k], cr[k]), "fuzz cut[%d] %d", k, i);
                if (g_fails > 20)
                        return;
        }

        /* Fuzz damage_add and damage_subtract sequences */
        static const dmg_i64 slacks[] = {0, 64, 4096, -16, INT64_MAX, INT64_MIN};
        for (int seq = 0; seq < 4000; seq++) {
                struct dmg_list da, dr;
                memset(&da, 0, sizeof(da));
                memset(&dr, 0, sizeof(dr));
                int mode = seq % 3;
                for (int step = 0; step < 16; step++) {
                        struct dmg_rect r = rand_rect(mode);
                        if ((xorshift64() & 3) != 0) {
                                dmg_i64 sl = slacks[xorshift64() % (sizeof(slacks) / sizeof(slacks[0]))];
                                dmg_add(&da, &r, sl);
                                ref_dmg_add(&dr, &r, sl);
                                CHECK(list_eq(&da, &dr), "fuzz add seq=%d step=%d", seq, step);
                        } else {
                                dmg_i32 ra = dmg_subtract(&da, &r);
                                dmg_i32 rr = ref_dmg_subtract(&dr, &r);
                                CHECK(ra == rr && list_eq(&da, &dr), "fuzz sub seq=%d step=%d", seq, step);
                        }
                        if (g_fails > 20)
                                return;
                }
        }
}

/* ── 3. mmap Guard-Page Tests (PROT_NONE before AND after) ── */

static void test_guard_pages(void)
{
        long ps = sysconf(_SC_PAGESIZE);
        if (ps <= 0)
                ps = 4096;
        size_t page = (size_t)ps;
        uint8_t *base = (uint8_t *)mmap(NULL, 3 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        CHECK(base != MAP_FAILED, "mmap 3 pages");
        if (base == MAP_FAILED)
                return;
        CHECK(mprotect(base, page, PROT_NONE) == 0, "mprotect guard-before");
        CHECK(mprotect(base + 2 * page, page, PROT_NONE) == 0, "mprotect guard-after");

        uint8_t *rw_lo = base + page;
        uint8_t *rw_hi = base + 2 * page;

        /* Test both flush-left (against before-guard) and flush-right (against after-guard) */
        for (int side = 0; side < 2; side++) {
                struct dmg_rect *ra = (struct dmg_rect *)(side == 0 ? rw_lo : rw_hi - sizeof(struct dmg_rect));
                struct dmg_rect *rb = (struct dmg_rect *)(side == 0 ? rw_lo + 16 : rw_hi - 2 * sizeof(struct dmg_rect));
                struct dmg_rect *out4 = (struct dmg_rect *)(side == 0 ? rw_lo : rw_hi - 4 * sizeof(struct dmg_rect));
                struct dmg_list *dl = (struct dmg_list *)(side == 0 ? rw_lo : rw_hi - sizeof(struct dmg_list));

                *ra = (struct dmg_rect){10, 20, 110, 120};
                *rb = (struct dmg_rect){40, 50, 80, 90};
                CHECK(dmg_rect_empty(ra) == 0, "guard empty side=%d", side);
                CHECK(dmg_rect_area(ra) == 10000, "guard area side=%d", side);
                CHECK(dmg_rect_cover(ra, rb) == 1, "guard cover side=%d", side);
                struct dmg_rect tmp;
                dmg_rect_join(ra, rb, &tmp);
                CHECK(rect_eq(tmp, *ra), "guard join side=%d", side);
                CHECK(dmg_rect_meet(ra, rb, &tmp) == 1 && rect_eq(tmp, *rb), "guard meet side=%d", side);

                struct dmg_rect cut_in = *rb;
                struct dmg_rect r_in = *ra;
                CHECK(dmg_rect_cut(&r_in, &cut_in, out4) == 4, "guard cut side=%d", side);

                /* dmg_list flush against guard page (132 bytes, note 4-byte aligned at rw_hi - 132!) */
                memset(dl, 0, sizeof(*dl));
                for (int i = 0; i < 10; i++) {
                        struct dmg_rect ri = {i * 200, i * 10, i * 200 + 50, i * 10 + 50};
                        dmg_add(dl, &ri, 0);
                }
                CHECK(dl->count == 8, "guard add cap side=%d", side);
                struct dmg_rect sub = {0, 0, 5000, 5000};
                CHECK(dmg_subtract(dl, &sub) == 1 && dl->count == 0, "guard subtract side=%d", side);
        }

        munmap(base, 3 * page);
}

/* ── 4. Throughput Benchmark ── */

static double now_ns(void)
{
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static volatile int64_t g_sink = 0;

static void bench_all(void)
{
        const int N = 200000;
        struct dmg_rect a = {100, 100, 900, 700};
        struct dmg_rect b = {300, 250, 600, 500};
        struct dmg_rect out[4];
        int64_t acc = 0;

        /* 1. dmg_rect_cut */
        double t0 = now_ns();
        for (int i = 0; i < N; i++) {
                b.x1 = 200 + (i & 127);
                acc += dmg_rect_cut(&a, &b, out);
        }
        double asm_cut = (now_ns() - t0) / N;

        t0 = now_ns();
        for (int i = 0; i < N; i++) {
                b.x1 = 200 + (i & 127);
                acc += ref_dmg_rect_cut(&a, &b, out);
        }
        double ref_cut = (now_ns() - t0) / N;

        /* 2. dmg_add (8-rect workload with coalescing + overflow merge) */
        struct dmg_rect batch[10] = {
                {0, 0, 100, 100}, {200, 0, 300, 100}, {400, 0, 500, 100}, {600, 0, 700, 100},
                {0, 200, 100, 300}, {200, 200, 300, 300}, {400, 200, 500, 300}, {600, 200, 700, 300},
                {50, 0, 250, 100}, {650, 250, 750, 350},
        };
        const int M = 100000;
        t0 = now_ns();
        for (int i = 0; i < M; i++) {
                struct dmg_list d;
                d.count = 0;
                for (int k = 0; k < 10; k++)
                        dmg_add(&d, &batch[k], 64);
                acc += d.count;
        }
        double asm_add = (now_ns() - t0) / (M * 10.0);

        t0 = now_ns();
        for (int i = 0; i < M; i++) {
                struct dmg_list d;
                d.count = 0;
                for (int k = 0; k < 10; k++)
                        ref_dmg_add(&d, &batch[k], 64);
                acc += d.count;
        }
        double ref_add = (now_ns() - t0) / (M * 10.0);

        /* 3. dmg_subtract */
        struct dmg_list base_d;
        base_d.count = 4;
        base_d.r[0] = (struct dmg_rect){0, 0, 400, 400};
        base_d.r[1] = (struct dmg_rect){500, 0, 900, 400};
        base_d.r[2] = (struct dmg_rect){0, 500, 400, 900};
        base_d.r[3] = (struct dmg_rect){500, 500, 900, 900};
        struct dmg_rect sub = {200, 200, 700, 700};

        t0 = now_ns();
        for (int i = 0; i < N; i++) {
                struct dmg_list d = base_d;
                acc += dmg_subtract(&d, &sub);
        }
        double asm_sub = (now_ns() - t0) / N;

        t0 = now_ns();
        for (int i = 0; i < N; i++) {
                struct dmg_list d = base_d;
                acc += ref_dmg_subtract(&d, &sub);
        }
        double ref_sub = (now_ns() - t0) / N;

        g_sink = acc;
        printf("bench dmg_rect_cut : ASM %5.2f ns/op | C -O2 %5.2f ns/op | speedup %.2fx\n",
               asm_cut, ref_cut, ref_cut / asm_cut);
        printf("bench dmg_add      : ASM %5.2f ns/op | C -O2 %5.2f ns/op | speedup %.2fx\n",
               asm_add, ref_add, ref_add / asm_add);
        printf("bench dmg_subtract : ASM %5.2f ns/op | C -O2 %5.2f ns/op | speedup %.2fx\n",
               asm_sub, ref_sub, ref_sub / asm_sub);
}

int main(void)
{
        test_edges_and_security();
        test_differential_fuzz();
        test_guard_pages();
        if (g_fails) {
                fprintf(stderr, "damage_ops: FAILED (%d errors)\n", g_fails);
                return 1;
        }
        bench_all();
        printf("damage_ops: ALL PASSED (edges + security + 40k rect fuzz + 4k*16 list fuzz + mmap guard pages)\n");
        return 0;
}
