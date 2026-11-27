#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>

#define SH_SHELL_DIFF_ASM_IMPLEMENTATION
#include "../src/sh/shell_diff.asm.c"

static uint64_t g_checks = 0;
static uint64_t g_fails = 0;

#define CHECK(cond, ...) do { \
        g_checks++; \
        if (!(cond)) { \
                g_fails++; \
                fprintf(stderr, "FAIL [%s:%d]: ", __FILE__, __LINE__); \
                fprintf(stderr, __VA_ARGS__); \
                fprintf(stderr, "\n"); \
                if (g_fails >= 20) exit(1); \
        } \
} while (0)

/* ---- Exact C reference implementations (noinline for size/speed comparison) ---- */

__attribute__((noinline))
sh_diff_u64 ref_diff_common_prefix_u32(const sh_diff_i32 *xv, const sh_diff_i32 *yv,
                                       sh_diff_u64 max_len)
{
        if (!xv || !yv || max_len == 0)
                return 0;
        sh_diff_u64 n = 0;
        while (n < max_len && xv[n] == yv[n])
                n++;
        return n;
}

__attribute__((noinline))
sh_diff_u64 ref_diff_common_suffix_u32(const sh_diff_i32 *xv_end, const sh_diff_i32 *yv_end,
                                       sh_diff_u64 max_len)
{
        if (!xv_end || !yv_end || max_len == 0)
                return 0;
        sh_diff_u64 n = 0;
        while (n < max_len && xv_end[-1 - (sh_diff_i64)n] == yv_end[-1 - (sh_diff_i64)n])
                n++;
        return n;
}

__attribute__((noinline))
sh_diff_i32 ref_diff_meet(struct sh_diff_req *req)
{
        if (!req || !req->fwd || !req->bwd)
                return 0;
        sh_diff_u64 xoff = req->xoff, xlim = req->xlim;
        sh_diff_u64 yoff = req->yoff, ylim = req->ylim;
        if (xoff > xlim || yoff > ylim)
                return 0;
        if (xoff == xlim && yoff == ylim) {
                req->xmid = xoff;
                req->ymid = yoff;
                return 1;
        }
        if ((xoff < xlim && !req->xv) || (yoff < ylim && !req->yv))
                return 0;

        const sh_diff_i32 *xv = req->xv;
        const sh_diff_i32 *yv = req->yv;
        sh_diff_i64 low   = (sh_diff_i64)xoff - (sh_diff_i64)ylim;
        sh_diff_i64 high  = (sh_diff_i64)xlim - (sh_diff_i64)yoff;
        sh_diff_i64 front = (sh_diff_i64)xoff - (sh_diff_i64)yoff;
        sh_diff_i64 back  = (sh_diff_i64)xlim - (sh_diff_i64)ylim;
        sh_diff_i64 first[2] = { front, back };
        sh_diff_i64 last[2]  = { front, back };
        sh_diff_i32 *frontier[2] = { req->fwd, req->bwd };
        int odd = (int)((front - back) & 1);
        sh_diff_u64 shift = req->shift;
        sh_diff_u64 rem_limit = req->limit ? req->limit : ~(sh_diff_u64)0;

        req->fwd[shift + (sh_diff_u64)front] = (sh_diff_i32)xoff;
        req->bwd[shift + (sh_diff_u64)back]  = (sh_diff_i32)xlim;

        for (;;) {
                for (int reverse = 0; reverse < 2; reverse++) {
                        if (rem_limit == 0)
                                return 0;
                        rem_limit--;

                        sh_diff_i32 *edge  = frontier[reverse] + shift;
                        sh_diff_i32 *other = frontier[1 - reverse] + shift;
                        sh_diff_i32 outside = reverse ? 0x7fffffff : -1;
                        sh_diff_i64 step = reverse ? -1 : 1;

                        if (first[reverse] > low)
                                edge[--first[reverse] - 1] = outside;
                        else
                                first[reverse]++;
                        if (last[reverse] < high)
                                edge[++last[reverse] + 1] = outside;
                        else
                                last[reverse]--;

                        for (sh_diff_i64 d = last[reverse]; d >= first[reverse]; d -= 2) {
                                sh_diff_i64 lower = edge[d - 1];
                                sh_diff_i64 upper = edge[d + 1];
                                sh_diff_i64 x = reverse ? (lower < upper ? lower : upper - 1)
                                                        : (lower < upper ? upper : lower + 1);
                                sh_diff_i64 y = x - d;

                                while (reverse ? ((sh_diff_i64)xoff < x && (sh_diff_i64)yoff < y)
                                               : (x < (sh_diff_i64)xlim && y < (sh_diff_i64)ylim)) {
                                        if (xv[x - reverse] != yv[y - reverse])
                                                break;
                                        x += step;
                                        y += step;
                                }
                                edge[d] = (sh_diff_i32)x;
                                if (odd != reverse && first[1 - reverse] <= d &&
                                    d <= last[1 - reverse] &&
                                    (reverse ? x <= other[d] : other[d] <= x)) {
                                        req->xmid = (sh_diff_u64)x;
                                        req->ymid = (sh_diff_u64)y;
                                        return 1;
                                }
                        }
                }
        }
}

/* ---- PRNG ---- */
static uint64_t rng_state = 0x9e3779b97f4a7c15ULL;
static uint64_t rng_next(void)
{
        uint64_t x = rng_state;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        rng_state = x;
        return x;
}

/* ---- Helper: compare ASM vs C ref on one sh_diff_meet case ---- */
static void check_meet_case(const sh_diff_i32 *xv, const sh_diff_i32 *yv,
                            sh_diff_u64 xoff, sh_diff_u64 xlim,
                            sh_diff_u64 yoff, sh_diff_u64 ylim,
                            sh_diff_u64 limit, const char *tag)
{
        sh_diff_u64 diag = (xlim >= xoff ? xlim - xoff : 0) +
                           (ylim >= yoff ? ylim - yoff : 0) + 16;
        sh_diff_u64 shift = (ylim > xoff ? ylim - xoff : 0) + 8;
        sh_diff_u64 total = shift + (xlim > yoff ? xlim - yoff : 0) + diag + 16;

        sh_diff_i32 *fwd_a = (sh_diff_i32 *)malloc(total * sizeof(sh_diff_i32));
        sh_diff_i32 *bwd_a = (sh_diff_i32 *)malloc(total * sizeof(sh_diff_i32));
        sh_diff_i32 *fwd_r = (sh_diff_i32 *)malloc(total * sizeof(sh_diff_i32));
        sh_diff_i32 *bwd_r = (sh_diff_i32 *)malloc(total * sizeof(sh_diff_i32));
        memset(fwd_a, 0x5a, total * sizeof(sh_diff_i32));
        memset(bwd_a, 0x5a, total * sizeof(sh_diff_i32));
        memset(fwd_r, 0x5a, total * sizeof(sh_diff_i32));
        memset(bwd_r, 0x5a, total * sizeof(sh_diff_i32));

        struct sh_diff_req ra = { xv, yv, fwd_a, bwd_a, shift, xoff, xlim, yoff, ylim, limit, 0xdeadULL, 0xbeefULL };
        struct sh_diff_req rr = { xv, yv, fwd_r, bwd_r, shift, xoff, xlim, yoff, ylim, limit, 0xdeadULL, 0xbeefULL };

        sh_diff_i32 ret_a = sh_diff_meet(&ra);
        sh_diff_i32 ret_r = ref_diff_meet(&rr);

        CHECK(ret_a == ret_r, "%s: ret mismatch asm=%d ref=%d (x=[%llu,%llu) y=[%llu,%llu) lim=%llu)",
              tag, ret_a, ret_r, (unsigned long long)xoff, (unsigned long long)xlim,
              (unsigned long long)yoff, (unsigned long long)ylim, (unsigned long long)limit);
        if (ret_a == 1) {
                CHECK(ra.xmid == rr.xmid && ra.ymid == rr.ymid,
                      "%s: mid mismatch asm=(%llu,%llu) ref=(%llu,%llu)",
                      tag, (unsigned long long)ra.xmid, (unsigned long long)ra.ymid,
                      (unsigned long long)rr.xmid, (unsigned long long)rr.ymid);
        }

        free(fwd_a);
        free(bwd_a);
        free(fwd_r);
        free(bwd_r);
}

/* ---- Full recursive diff_compare (end-to-end) ---- */
static void run_diff_compare(int use_asm, const sh_diff_i32 *xv, const sh_diff_i32 *yv,
                             sh_diff_i32 *fwd, sh_diff_i32 *bwd, sh_diff_u64 shift,
                             sh_diff_u64 xoff, sh_diff_u64 xlim,
                             sh_diff_u64 yoff, sh_diff_u64 ylim,
                             uint8_t *xchg, uint8_t *ychg)
{
        while (1) {
                sh_diff_u64 span = xlim - xoff;
                sh_diff_u64 yspan = ylim - yoff;
                if (yspan < span) span = yspan;
                sh_diff_u64 pre = use_asm ? sh_diff_common_prefix_u32(xv + xoff, yv + yoff, span)
                                          : ref_diff_common_prefix_u32(xv + xoff, yv + yoff, span);
                xoff += pre;
                yoff += pre;
                span -= pre;
                sh_diff_u64 suf = use_asm ? sh_diff_common_suffix_u32(xv + xlim, yv + ylim, span)
                                          : ref_diff_common_suffix_u32(xv + xlim, yv + ylim, span);
                xlim -= suf;
                ylim -= suf;

                if (xoff == xlim) {
                        while (yoff < ylim) ychg[yoff++] = 1;
                        return;
                }
                if (yoff == ylim) {
                        while (xoff < xlim) xchg[xoff++] = 1;
                        return;
                }
                struct sh_diff_req req = { xv, yv, fwd, bwd, shift, xoff, xlim, yoff, ylim, 0, 0, 0 };
                sh_diff_i32 ok = use_asm ? sh_diff_meet(&req) : ref_diff_meet(&req);
                CHECK(ok == 1, "diff_compare meet failed");
                run_diff_compare(use_asm, xv, yv, fwd, bwd, shift, xoff, req.xmid, yoff, req.ymid, xchg, ychg);
                xoff = req.xmid;
                yoff = req.ymid;
        }
}

/* ---- Guard-page helpers ---- */
struct guard_buf {
        void  *base;
        size_t total;
        size_t page;
};

static void *guard_alloc_end(size_t bytes, struct guard_buf *gb)
{
        size_t pg = (size_t)sysconf(_SC_PAGESIZE);
        size_t data_pages = (bytes + pg - 1) / pg;
        if (data_pages == 0) data_pages = 1;
        size_t total = (data_pages + 2) * pg;
        void *p = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        CHECK(p != MAP_FAILED, "mmap failed");
        CHECK(mprotect(p, pg, PROT_NONE) == 0, "mprotect low failed");
        CHECK(mprotect((char *)p + (data_pages + 1) * pg, pg, PROT_NONE) == 0, "mprotect high failed");
        gb->base = p;
        gb->total = total;
        gb->page = pg;
        /* Flush against high PROT_NONE page */
        return (char *)p + (data_pages + 1) * pg - bytes;
}

static void *guard_alloc_start(size_t bytes, struct guard_buf *gb)
{
        size_t pg = (size_t)sysconf(_SC_PAGESIZE);
        size_t data_pages = (bytes + pg - 1) / pg;
        if (data_pages == 0) data_pages = 1;
        size_t total = (data_pages + 2) * pg;
        void *p = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        CHECK(p != MAP_FAILED, "mmap failed");
        CHECK(mprotect(p, pg, PROT_NONE) == 0, "mprotect low failed");
        CHECK(mprotect((char *)p + (data_pages + 1) * pg, pg, PROT_NONE) == 0, "mprotect high failed");
        gb->base = p;
        gb->total = total;
        gb->page = pg;
        /* Flush against low PROT_NONE page */
        return (char *)p + pg;
}

static void guard_free(struct guard_buf *gb)
{
        munmap(gb->base, gb->total);
}

static void test_edge_and_security(void)
{
        sh_diff_i32 dummy[16] = {1, 2, 3, 4, 5, 6, 7, 8};
        sh_diff_i32 fwd[64], bwd[64];

        /* NULL / zero bounds for prefix & suffix */
        CHECK(sh_diff_common_prefix_u32(NULL, dummy, 8) == 0, "prefix NULL xv");
        CHECK(sh_diff_common_prefix_u32(dummy, NULL, 8) == 0, "prefix NULL yv");
        CHECK(sh_diff_common_prefix_u32(dummy, dummy, 0) == 0, "prefix max_len=0");
        CHECK(sh_diff_common_suffix_u32(NULL, dummy + 8, 8) == 0, "suffix NULL xv");
        CHECK(sh_diff_common_suffix_u32(dummy + 8, NULL, 8) == 0, "suffix NULL yv");
        CHECK(sh_diff_common_suffix_u32(dummy + 8, dummy + 8, 0) == 0, "suffix max_len=0");

        /* NULL / inverted bounds for sh_diff_meet */
        CHECK(sh_diff_meet(NULL) == 0, "meet NULL req");
        struct sh_diff_req r;
        memset(&r, 0, sizeof(r));
        r.xv = dummy; r.yv = dummy; r.fwd = NULL; r.bwd = bwd; r.shift = 16; r.xlim = 4; r.ylim = 4;
        CHECK(sh_diff_meet(&r) == 0, "meet NULL fwd");
        r.fwd = fwd; r.bwd = NULL;
        CHECK(sh_diff_meet(&r) == 0, "meet NULL bwd");
        r.bwd = bwd; r.xoff = 5; r.xlim = 4;
        CHECK(sh_diff_meet(&r) == 0, "meet xoff > xlim");
        r.xoff = 0; r.yoff = 5; r.ylim = 4;
        CHECK(sh_diff_meet(&r) == 0, "meet yoff > ylim");
        r.yoff = 0; r.xv = NULL; r.xlim = 4; r.ylim = 4;
        CHECK(sh_diff_meet(&r) == 0, "meet NULL xv non-empty");
        r.xv = dummy; r.yv = NULL;
        CHECK(sh_diff_meet(&r) == 0, "meet NULL yv non-empty");

        /* Both empty (even with NULL xv/yv) */
        r.xv = NULL; r.yv = NULL; r.xoff = 3; r.xlim = 3; r.yoff = 7; r.ylim = 7;
        CHECK(sh_diff_meet(&r) == 1 && r.xmid == 3 && r.ymid == 7, "meet both empty");

        /* One empty, one non-empty (odd & even lengths) */
        for (sh_diff_u64 n = 1; n <= 9; n++) {
                check_meet_case(NULL, dummy, 0, 0, 0, n, 0, "empty_x");
                check_meet_case(dummy, NULL, 0, n, 0, 0, 0, "empty_y");
        }

        /* 1-element identical & disjoint */
        sh_diff_i32 a1[1] = {42}, b1[1] = {42}, c1[1] = {99};
        check_meet_case(a1, b1, 0, 1, 0, 1, 0, "1elem_eq");
        check_meet_case(a1, c1, 0, 1, 0, 1, 0, "1elem_ne");

        /* Identical sequences of various lengths (including negative i32 values) */
        sh_diff_i32 id[128];
        for (int i = 0; i < 128; i++) id[i] = (i & 1) ? -i * 1337 : i * 7919;
        sh_diff_u64 lens[] = {1, 2, 3, 4, 7, 8, 15, 16, 31, 64, 128};
        for (size_t i = 0; i < sizeof(lens)/sizeof(lens[0]); i++) {
                CHECK(sh_diff_common_prefix_u32(id, id, lens[i]) == lens[i], "prefix identical");
                CHECK(sh_diff_common_suffix_u32(id + lens[i], id + lens[i], lens[i]) == lens[i], "suffix identical");
                check_meet_case(id, id, 0, lens[i], 0, lens[i], 0, "identical");
        }

        /* Completely disjoint sequences (odd & even delta m - n) */
        sh_diff_i32 d0[64], d1[64];
        for (int i = 0; i < 64; i++) { d0[i] = i + 1; d1[i] = -(i + 1); }
        for (sh_diff_u64 m = 1; m <= 12; m++) {
                for (sh_diff_u64 n = 1; n <= 12; n++) {
                        check_meet_case(d0, d1, 0, m, 0, n, 0, "disjoint");
                }
        }

        /* Long matching snakes with single diff at start/mid/end */
        sh_diff_i32 s0[100], s1[100];
        for (int i = 0; i < 100; i++) s0[i] = s1[i] = i * 3 + 7;
        s1[0] = -1;
        check_meet_case(s0, s1, 0, 100, 0, 100, 0, "snake_start_diff");
        s1[0] = s0[0]; s1[50] = -1;
        check_meet_case(s0, s1, 0, 100, 0, 100, 0, "snake_mid_diff");
        s1[50] = s0[50]; s1[99] = -1;
        check_meet_case(s0, s1, 0, 100, 0, 100, 0, "snake_end_diff");

        /* Budget limit hit tests */
        /* Disjoint m=5, n=5 requires D=10 half-steps; limit=1..9 must fail (0), limit=10 must succeed (1) */
        for (sh_diff_u64 lim = 1; lim <= 12; lim++) {
                check_meet_case(d0, d1, 0, 5, 0, 5, lim, "limit_even");
                check_meet_case(d0, d1, 0, 5, 0, 4, lim, "limit_odd");
        }
}

static void test_guard_pages(void)
{
        /* Prefix & suffix flush against PROT_NONE before AND after */
        for (sh_diff_u64 len = 1; len <= 33; len++) {
                struct guard_buf gx, gy;
                for (int mode = 0; mode < 2; mode++) {
                        sh_diff_i32 *x = (sh_diff_i32 *)(mode ? guard_alloc_end(len * 4, &gx)
                                                              : guard_alloc_start(len * 4, &gx));
                        sh_diff_i32 *y = (sh_diff_i32 *)(mode ? guard_alloc_end(len * 4, &gy)
                                                              : guard_alloc_start(len * 4, &gy));
                        for (sh_diff_u64 i = 0; i < len; i++) x[i] = y[i] = (sh_diff_i32)(i * 17 + 3);
                        CHECK(sh_diff_common_prefix_u32(x, y, len) == len, "guard prefix full len=%llu", (unsigned long long)len);
                        CHECK(sh_diff_common_suffix_u32(x + len, y + len, len) == len, "guard suffix full len=%llu", (unsigned long long)len);

                        /* Mismatch at every position */
                        for (sh_diff_u64 pos = 0; pos < len; pos++) {
                                y[pos] ^= 0x55aa;
                                CHECK(sh_diff_common_prefix_u32(x, y, len) == pos, "guard prefix pos");
                                CHECK(sh_diff_common_suffix_u32(x + len, y + len, len) == (len - 1 - pos), "guard suffix pos");
                                y[pos] ^= 0x55aa;
                        }
                        guard_free(&gx);
                        guard_free(&gy);
                }
        }

        /* sh_diff_meet with xv, yv, fwd, bwd ALL flush against PROT_NONE before AND after! */
        sh_diff_u64 sizes[][2] = {
                {1, 1}, {1, 2}, {2, 1}, {3, 3}, {7, 8}, {8, 7}, {15, 16}, {16, 15}, {31, 33}, {32, 32}
        };
        for (size_t s = 0; s < sizeof(sizes)/sizeof(sizes[0]); s++) {
                sh_diff_u64 m = sizes[s][0], n = sizes[s][1];
                /* Exact frontier size: shift = n + 1, indices [shift + (-n) - 1 .. shift + m + 1] = [0 .. m + n + 2] => m + n + 3 elements */
                sh_diff_u64 flen = m + n + 3;
                sh_diff_u64 shift = n + 1;
                for (int mode = 0; mode < 2; mode++) {
                        struct guard_buf gx, gy, gf, gb;
                        sh_diff_i32 *x = (sh_diff_i32 *)(mode ? guard_alloc_end(m * 4, &gx) : guard_alloc_start(m * 4, &gx));
                        sh_diff_i32 *y = (sh_diff_i32 *)(mode ? guard_alloc_end(n * 4, &gy) : guard_alloc_start(n * 4, &gy));
                        sh_diff_i32 *f = (sh_diff_i32 *)(mode ? guard_alloc_end(flen * 4, &gf) : guard_alloc_start(flen * 4, &gf));
                        sh_diff_i32 *b = (sh_diff_i32 *)(mode ? guard_alloc_end(flen * 4, &gb) : guard_alloc_start(flen * 4, &gb));

                        for (int pat = 0; pat < 3; pat++) {
                                for (sh_diff_u64 i = 0; i < m; i++)
                                        x[i] = (pat == 0) ? (sh_diff_i32)i : (pat == 1 ? (sh_diff_i32)(i + 1000) : (sh_diff_i32)(rng_next() % 4));
                                for (sh_diff_u64 i = 0; i < n; i++)
                                        y[i] = (pat == 0) ? (sh_diff_i32)i : (pat == 1 ? (sh_diff_i32)(i + 2000) : (sh_diff_i32)(rng_next() % 4));

                                struct sh_diff_req ra = { x, y, f, b, shift, 0, m, 0, n, 0, 0, 0 };
                                sh_diff_i32 ret_a = sh_diff_meet(&ra);

                                sh_diff_i32 *fr = (sh_diff_i32 *)malloc(flen * 4);
                                sh_diff_i32 *br = (sh_diff_i32 *)malloc(flen * 4);
                                struct sh_diff_req rr = { x, y, fr, br, shift, 0, m, 0, n, 0, 0, 0 };
                                sh_diff_i32 ret_r = ref_diff_meet(&rr);

                                CHECK(ret_a == ret_r && ra.xmid == rr.xmid && ra.ymid == rr.ymid,
                                      "guard meet m=%llu n=%llu mode=%d pat=%d",
                                      (unsigned long long)m, (unsigned long long)n, mode, pat);
                                free(fr);
                                free(br);
                        }

                        guard_free(&gx);
                        guard_free(&gy);
                        guard_free(&gf);
                        guard_free(&gb);
                }
        }
}

static void test_fuzz(void)
{
        sh_diff_i32 xv[256], yv[256];

        /* 5000 randomized prefix/suffix tests */
        for (int t = 0; t < 5000; t++) {
                sh_diff_u64 len = rng_next() % 257;
                for (sh_diff_u64 i = 0; i < len; i++) {
                        xv[i] = (sh_diff_i32)(rng_next() & 0x7fffffff);
                        yv[i] = xv[i];
                }
                if (len > 0 && (rng_next() & 3)) {
                        sh_diff_u64 pos = rng_next() % len;
                        yv[pos] ^= 1;
                }
                sh_diff_u64 pa = sh_diff_common_prefix_u32(xv, yv, len);
                sh_diff_u64 pr = ref_diff_common_prefix_u32(xv, yv, len);
                CHECK(pa == pr, "fuzz prefix len=%llu asm=%llu ref=%llu",
                      (unsigned long long)len, (unsigned long long)pa, (unsigned long long)pr);

                sh_diff_u64 sa = sh_diff_common_suffix_u32(xv + len, yv + len, len);
                sh_diff_u64 sr = ref_diff_common_suffix_u32(xv + len, yv + len, len);
                CHECK(sa == sr, "fuzz suffix len=%llu asm=%llu ref=%llu",
                      (unsigned long long)len, (unsigned long long)sa, (unsigned long long)sr);
        }

        /* 5000 randomized sh_diff_meet tests (subranges, varying alphabets, budgets) */
        for (int t = 0; t < 5000; t++) {
                sh_diff_u64 m = rng_next() % 65;
                sh_diff_u64 n = rng_next() % 65;
                sh_diff_u64 xoff = (m > 0) ? (rng_next() % (m + 1)) : 0;
                sh_diff_u64 yoff = (n > 0) ? (rng_next() % (n + 1)) : 0;
                int alpha = 1 << (1 + (rng_next() % 5));
                for (sh_diff_u64 i = 0; i < m; i++) xv[i] = (sh_diff_i32)(rng_next() % (uint64_t)alpha);
                for (sh_diff_u64 i = 0; i < n; i++) yv[i] = (sh_diff_i32)(rng_next() % (uint64_t)alpha);
                sh_diff_u64 lim = (rng_next() & 3) ? 0 : (1 + (rng_next() % 20));
                check_meet_case(xv, yv, xoff, m, yoff, n, lim, "fuzz_meet");
        }

        /* 300 full recursive diff_compare end-to-end tests */
        sh_diff_i32 fwd_a[512], bwd_a[512], fwd_r[512], bwd_r[512];
        uint8_t xca[128], yca[128], xcr[128], ycr[128];
        for (int t = 0; t < 300; t++) {
                sh_diff_u64 m = rng_next() % 100;
                sh_diff_u64 n = rng_next() % 100;
                int alpha = 2 + (int)(rng_next() % 12);
                for (sh_diff_u64 i = 0; i < m; i++) xv[i] = (sh_diff_i32)(rng_next() % (uint64_t)alpha);
                for (sh_diff_u64 i = 0; i < n; i++) yv[i] = (sh_diff_i32)(rng_next() % (uint64_t)alpha);
                memset(xca, 0, sizeof(xca)); memset(yca, 0, sizeof(yca));
                memset(xcr, 0, sizeof(xcr)); memset(ycr, 0, sizeof(ycr));
                sh_diff_u64 shift = n + 2;
                run_diff_compare(1, xv, yv, fwd_a, bwd_a, shift, 0, m, 0, n, xca, yca);
                run_diff_compare(0, xv, yv, fwd_r, bwd_r, shift, 0, m, 0, n, xcr, ycr);
                CHECK(memcmp(xca, xcr, m) == 0 && memcmp(yca, ycr, n) == 0,
                      "diff_compare mismatch t=%d m=%llu n=%llu", t,
                      (unsigned long long)m, (unsigned long long)n);
        }
}

static uint64_t now_ns(void)
{
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void run_bench(void)
{
        enum { N = 1024 };
        static sh_diff_i32 xv[N], yv[N], fwd[2 * N + 16], bwd[2 * N + 16];

        /* 1. Long common prefix/suffix benchmark */
        for (int i = 0; i < N; i++) xv[i] = yv[i] = i * 13 + 5;
        yv[N - 1] = -1;
        volatile sh_diff_u64 sink = 0;
        const int P_ITERS = 200000;
        const sh_diff_i32 *px = xv;
        const sh_diff_i32 *py = yv;

        uint64_t t0 = now_ns();
        for (int i = 0; i < P_ITERS; i++) {
                __asm__ volatile("" : "+r"(px), "+r"(py) :: "memory");
                sink += ref_diff_common_prefix_u32(px, py, N);
        }
        uint64_t t1 = now_ns();
        for (int i = 0; i < P_ITERS; i++) {
                __asm__ volatile("" : "+r"(px), "+r"(py) :: "memory");
                sink += sh_diff_common_prefix_u32(px, py, N);
        }
        uint64_t t2 = now_ns();

        double ref_pre = (double)(t1 - t0) / P_ITERS;
        double asm_pre = (double)(t2 - t1) / P_ITERS;

        yv[N - 1] = xv[N - 1]; yv[0] = -1;
        const sh_diff_i32 *pxe = xv + N;
        const sh_diff_i32 *pye = yv + N;
        t0 = now_ns();
        for (int i = 0; i < P_ITERS; i++) {
                __asm__ volatile("" : "+r"(pxe), "+r"(pye) :: "memory");
                sink += ref_diff_common_suffix_u32(pxe, pye, N);
        }
        t1 = now_ns();
        for (int i = 0; i < P_ITERS; i++) {
                __asm__ volatile("" : "+r"(pxe), "+r"(pye) :: "memory");
                sink += sh_diff_common_suffix_u32(pxe, pye, N);
        }
        t2 = now_ns();

        double ref_suf = (double)(t1 - t0) / P_ITERS;
        double asm_suf = (double)(t2 - t1) / P_ITERS;

        /* 2. Myers sh_diff_meet benchmark (realistic ~10% scattered edits across 512 lines) */
        enum { M = 512 };
        rng_state = 0x123456789abcdef0ULL;
        for (int i = 0; i < M; i++) {
                xv[i] = i;
                yv[i] = ((rng_next() % 10) == 0) ? (sh_diff_i32)(i + 100000) : i;
        }
        const int M_ITERS = 5000;
        struct sh_diff_req req = { xv, yv, fwd, bwd, M + 4, 0, M, 0, M, 0, 0, 0 };

        t0 = now_ns();
        for (int i = 0; i < M_ITERS; i++) {
                sink += (sh_diff_u64)ref_diff_meet(&req);
                sink += req.xmid;
        }
        t1 = now_ns();
        for (int i = 0; i < M_ITERS; i++) {
                sink += (sh_diff_u64)sh_diff_meet(&req);
                sink += req.xmid;
        }
        t2 = now_ns();

        double ref_meet = (double)(t1 - t0) / M_ITERS;
        double asm_meet = (double)(t2 - t1) / M_ITERS;

        /* 3. Myers sh_diff_meet benchmark (long matching snakes: 1024 lines, 8 edits) */
        for (int i = 0; i < N; i++) xv[i] = yv[i] = i;
        for (int k = 1; k <= 8; k++) yv[k * 110] = -k;
        struct sh_diff_req req2 = { xv, yv, fwd, bwd, N + 4, 0, N, 0, N, 0, 0, 0 };
        const int S_ITERS = 20000;

        t0 = now_ns();
        for (int i = 0; i < S_ITERS; i++) {
                sink += (sh_diff_u64)ref_diff_meet(&req2);
                sink += req2.xmid;
        }
        t1 = now_ns();
        for (int i = 0; i < S_ITERS; i++) {
                sink += (sh_diff_u64)sh_diff_meet(&req2);
                sink += req2.xmid;
        }
        t2 = now_ns();

        double ref_snake = (double)(t1 - t0) / S_ITERS;
        double asm_snake = (double)(t2 - t1) / S_ITERS;

        (void)sink;
        printf("BENCH prefix(1024):     ref=%.1f ns/op  asm=%.1f ns/op  speedup=%.2fx\n",
               ref_pre, asm_pre, ref_pre / asm_pre);
        printf("BENCH suffix(1024):     ref=%.1f ns/op  asm=%.1f ns/op  speedup=%.2fx\n",
               ref_suf, asm_suf, ref_suf / asm_suf);
        printf("BENCH meet(512,10%%):    ref=%.1f ns/op  asm=%.1f ns/op  speedup=%.2fx\n",
               ref_meet, asm_meet, ref_meet / asm_meet);
        printf("BENCH meet(1024,snake): ref=%.1f ns/op  asm=%.1f ns/op  speedup=%.2fx\n",
               ref_snake, asm_snake, ref_snake / asm_snake);
}

int main(int argc, char **argv)
{
        test_edge_and_security();
        test_guard_pages();
        test_fuzz();
        printf("PASS: %llu checks, 0 failures\n", (unsigned long long)g_checks);
        if (argc > 1 && strcmp(argv[1], "--bench") == 0)
                run_bench();
        return g_fails ? 1 : 0;
}
