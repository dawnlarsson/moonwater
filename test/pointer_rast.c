/*
 * Exhaustive correctness, security (mmap guard pages + NULL/edge bounds),
 * differential fuzzing, and speed benchmark for src/canvas/compositor/pointer_rast.asm.c
 */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define COMPOSITOR_POINTER_RAST_ASM_IMPLEMENTATION
#include "../src/canvas/compositor/pointer_rast.asm.c"
/* Test double-emit guard (must compile with zero duplicate symbol errors) */
#include "../src/canvas/compositor/pointer_rast.asm.c"

#if defined(__GNUC__) || defined(__clang__)
#define NOINLINE __attribute__((noinline))
#else
#define NOINLINE
#endif

/* =========================================================================
 * Reference bitmap art & hotspots (subset + all 11 shapes, 24x25 each)
 * ========================================================================= */
static char test_art[PRAST_SHAPES][PRAST_CELL][PRAST_CELL_STRIDE];
static const signed char test_hot[PRAST_SHAPES][2] = {
    {0, 0}, {11, 11}, {11, 11}, {11, 11}, {11, 11},
    {11, 11}, {11, 11}, {11, 11}, {5, 0}, {11, 11}, {11, 11},
};

static void init_test_art(void)
{
        static const char arrow[PRAST_CELL][PRAST_CELL_STRIDE] = {
            "#.......................",
            "##......................",
            "#o#.....................",
            "#oo#....................",
            "#ooo#...................",
            "#oooo#..................",
            "#ooooo#.................",
            "#oooooo#................",
            "#ooooooo#...............",
            "#oooooooo#..............",
            "#ooooooooo#.............",
            "#oooooooooo#............",
            "#oooooo#####............",
            "#ooo#oo#................",
            "#oo#.#oo#...............",
            "#o#..#oo#...............",
            "##....#oo#..............",
            "#.....#oo#..............",
            ".......#oo#.............",
            ".......#oo#.............",
            "........##..............",
            "........................",
            "........................",
            "........................",
        };
        memcpy(test_art[0], arrow, sizeof(arrow));
        for (int s = 1; s < PRAST_SHAPES; s++) {
                for (int y = 0; y < PRAST_CELL; y++) {
                        for (int x = 0; x < PRAST_CELL; x++) {
                                int dx = x - 11, dy = y - 11;
                                int d = dx * dx + dy * dy;
                                if (s & 1) {
                                        test_art[s][y][x] = (d <= 64 && d >= 36) ? '#' : (d < 36 ? 'o' : '.');
                                } else {
                                        test_art[s][y][x] = (x == y || x + y == 23) ? '#' : ((abs(dx) <= 3 && abs(dy) <= 8) ? 'o' : '.');
                                }
                        }
                        test_art[s][y][PRAST_CELL] = '\0';
                }
        }
}

/* =========================================================================
 * -O2 C Reference Implementations (exact pointer.c logic, noinline for size)
 * ========================================================================= */
static unsigned long long ref_isqrt(unsigned long long n)
{
        unsigned long long root = 0, bit = 1ull << 62;
        while (bit > n)
                bit >>= 2;
        for (; bit; bit >>= 2) {
                if (n >= root + bit)
                        n -= root + bit, root = (root >> 1) + bit;
                else
                        root >>= 1;
        }
        return root;
}

NOINLINE int ref_counts(int sum, long long more)
{
        long long total = (long long)sum + more;
        return (int)(total > PRAST_COORD_MAX ? PRAST_COORD_MAX : total < -PRAST_COORD_MAX ? -PRAST_COORD_MAX : total);
}

NOINLINE int ref_gain_wanted(int gain, int flat, int percent)
{
        long long base = flat ? PRAST_ACCEL_ONE : gain;
        if (!percent || percent == 100)
                return (int)base;
        return (int)(base * percent / 100);
}

NOINLINE int ref_tablet(int value, int minimum, int maximum, int size)
{
        long long span = (long long)maximum - minimum, offset;
        if (span <= 0)
                return 0;
        offset = (long long)(value < minimum ? minimum : value > maximum ? maximum : value) - minimum;
        return (int)(offset * size / span);
}

NOINLINE int ref_shake(struct prast_shake *shake, int delta, unsigned long long now)
{
        static const int steps[3] = {6, 9, 4}, reversals[3] = {5, 7, 4};
        int direction, feel;
        if (!shake)
                return 0;
        feel = shake->feel >= 0 && shake->feel < 3 ? shake->feel : 0;
        if (delta > -steps[feel] && delta < steps[feel])
                return 0;
        direction = delta > 0 ? 1 : -1;
        if (now - shake->window > PRAST_SHAKE_WINDOW) {
                shake->window = now;
                shake->count = 0;
        }
        if (direction == shake->direction)
                return 0;
        shake->direction = direction;
        if (++shake->count >= reversals[feel]) {
                shake->count = 0;
                return 1;
        }
        return 0;
}

NOINLINE unsigned int ref_edges_at(const struct prast_edges_arg *a)
{
        unsigned int edges = 0;
        long long px, py, x1, y1, x2, y2, grip, in, top_in, corner;
        if (!a)
                return 0;
        px = a->px; py = a->py; x1 = a->x1; y1 = a->y1; x2 = a->x2; y2 = a->y2;
        grip = a->grip; in = a->in; top_in = a->top_in; corner = a->corner;
        if (px < x1 - grip || px >= x2 + grip || py < y1 - grip || py >= y2 + grip)
                return 0;
        edges |= px < x1 + in ? PRAST_EDGE_LEFT : px >= x2 - in ? PRAST_EDGE_RIGHT : 0;
        edges |= py < y1 + top_in ? PRAST_EDGE_TOP : py >= y2 - in ? PRAST_EDGE_BOTTOM : 0;
        if (corner && (edges & (PRAST_EDGE_LEFT | PRAST_EDGE_RIGHT)) && !(edges & (PRAST_EDGE_TOP | PRAST_EDGE_BOTTOM)))
                edges |= py < y1 + corner ? PRAST_EDGE_TOP : py >= y2 - corner ? PRAST_EDGE_BOTTOM : 0;
        else if (corner && (edges & (PRAST_EDGE_TOP | PRAST_EDGE_BOTTOM)) && !(edges & (PRAST_EDGE_LEFT | PRAST_EDGE_RIGHT)))
                edges |= px < x1 + corner ? PRAST_EDGE_LEFT : px >= x2 - corner ? PRAST_EDGE_RIGHT : 0;
        return edges;
}

NOINLINE void ref_dyn_cover(const struct prast_pointer *p, const struct prast_dyn_pre *pre,
                            long long u, long long v, unsigned char *fill, unsigned char *edge)
{
        long long a, reach, x, y, away, nx = 64, ny = 0, along, across, outer, inner, line, lined;
        if (!fill || !edge)
                return;
        *fill = *edge = 0;
        if (!p || !pre || p->scale <= 0)
                return;
        a = pre->a; reach = pre->reach; line = pre->line; lined = pre->lined;
        if (u > pre->bound || u < -pre->bound || v > pre->bound || v < -pre->bound)
                return;
        if (p->lens) {
                long long la = (u * p->ux + v * p->uy) / 64, b = (v * p->ux - u * p->uy) / 64;
                long long r = p->hh, tip = pre->tip;
                la = la < 0 ? -la : la;
                b = b < 0 ? -b : b;
                if (tip == r) {
                        away = (long long)ref_isqrt((unsigned long long)(la * la + b * b)) - r;
                        goto lined;
                }
                if (!tip)
                        return;
                {
                        long long leg = pre->leg;
                        if (la * leg <= b * r)
                                away = (long long)ref_isqrt((unsigned long long)(la * la + b * b)) - r;
                        else if ((la - tip) * leg >= b * r)
                                away = (long long)ref_isqrt((unsigned long long)((la - tip) * (la - tip) + b * b));
                        else
                                away = (la * r + b * leg) / tip - r;
                }
                goto lined;
        }
        if (p->grow) {
                if (!a)
                        return;
                along = (u * p->ux + v * p->uy) / 64 * 256 / a;
                across = (v * p->ux - u * p->uy) / 64 * a / 256;
                u = (along * p->ux - across * p->uy) / 64;
                v = (along * p->uy + across * p->ux) / 64;
        }
        x = (u < 0 ? -u : u) - (p->hw - p->c);
        y = (v < 0 ? -v : v) - (p->hh - p->c);
        if (x > 0 && y > 0) {
                long long square = x * x + y * y, out = p->c + reach, in = p->c - lined * a / 256 - reach;
                if (square >= out * out)
                        return;
                if (in > 0 && square <= in * in) {
                        *fill = 255;
                        return;
                }
                away = (long long)ref_isqrt((unsigned long long)square);
                nx = x * 64 / away;
                ny = y * 64 / away;
                away -= p->c;
        } else if (x > y) {
                away = x - p->c;
        } else {
                away = y - p->c;
                nx = 0;
                ny = 64;
        }
        if (p->grow) {
                if (!a)
                        return;
                nx = u < 0 ? -nx : nx;
                ny = v < 0 ? -ny : ny;
                along = (nx * p->ux + ny * p->uy) / 64 * 256 / a;
                across = (ny * p->ux - nx * p->uy) / 64 * a / 256;
                along = (long long)ref_isqrt((unsigned long long)(along * along + across * across));
                away = away * 64 / (along ? along : 1);
        }
lined:
        away = away * p->scale / 256;
        outer = away < -128 ? 256 : away > 128 ? 0 : 128 - away;
        inner = away + line < -128 ? 256 : away + line > 128 ? 0 : 128 - away - line;
        *edge = (unsigned char)((outer - inner) * 255 / 256);
        *fill = (unsigned char)(inner ? inner * 255 / (256 - (outer - inner)) : 0);
}

NOINLINE void ref_pointer_ring(const struct prast_rows_arg *arg)
{
        const struct prast_pointer *p;
        unsigned char *fill, *edge;
        int span, hot_x, hot_y, first, rows, pitch;
        long long cx, cy, r, core, line = 256, reach, inner, outer, turn;
        if (!arg || !arg->p || !arg->fill || !arg->edge || arg->span <= 0 || arg->rows <= 0 || arg->p->scale <= 0)
                return;
        p = arg->p; fill = arg->fill; edge = arg->edge;
        span = arg->span; hot_x = arg->hot_x; hot_y = arg->hot_y;
        first = arg->first; rows = arg->rows; pitch = arg->pitch;
        core = (p->scale > 256 ? p->scale : 256) * 3 / 4;
        turn = 4096ll * p->ring / PRAST_RING_STEPS;
        if (p->dyn) {
                cx = (long long)hot_x * 256;
                cy = (long long)hot_y * 256;
                r = 12ll * p->scale;
        } else {
                if (!arg->hot || p->shape >= PRAST_SHAPES)
                        return;
                cx = (long long)hot_x * 256 + (PRAST_CELL / 2 - arg->hot[p->shape * 2]) * (long long)p->scale;
                cy = (long long)hot_y * 256 + (PRAST_CELL / 2 - arg->hot[p->shape * 2 + 1]) * (long long)p->scale;
                r = 19ll * p->scale / 2;
        }
        reach = core + line + 256;
        inner = r > reach ? (r - reach) * (r - reach) : 0;
        outer = (r + reach) * (r + reach);
        for (int row = first; row < first + rows; row++, fill += pitch, edge += pitch) {
                long long dy = (long long)row * 256 + 128 - cy;
                if (dy * dy > outer)
                        continue;
                for (int col = 0; col < span; col++) {
                        long long dx = (long long)col * 256 + 128 - cx, d2 = dx * dx + dy * dy;
                        long long x = dx, y = -dy, a, b, quarter, off, c, e;
                        if (d2 > outer || d2 < inner)
                                continue;
                        if (x >= 0 && y > 0)      quarter = 0, a = x, b = y;
                        else if (x > 0)           quarter = 1, a = -y, b = x;
                        else if (y < 0)           quarter = 2, a = -x, b = -y;
                        else                      quarter = 3, a = y, b = -x;
                        if (quarter * 1024 + a * 1024 / (a + b ? a + b : 1) >= turn)
                                continue;
                        off = (long long)ref_isqrt((unsigned long long)d2) - r;
                        off = off < 0 ? -off : off;
                        c = core + 128 - off; c = c < 0 ? 0 : c > 256 ? 256 : c;
                        e = core + line + 128 - off; e = (e < 0 ? 0 : e > 256 ? 256 : e) - c;
                        c = c * 255 >> 8; e = e * 255 >> 8;
                        fill[col] = (unsigned char)(fill[col] > c ? fill[col] : c);
                        edge[col] = (unsigned char)(edge[col] > e ? edge[col] : e);
                }
        }
}

NOINLINE void ref_pointer_rows(const struct prast_rows_arg *arg)
{
        const struct prast_pointer *p;
        unsigned char *fill, *edge;
        int span, hot_x, hot_y, first, rows, pitch;
        if (!arg || !arg->p || !arg->fill || !arg->edge || arg->span <= 0 || arg->rows <= 0 || arg->p->scale <= 0)
                return;
        p = arg->p; fill = arg->fill; edge = arg->edge;
        span = arg->span; hot_x = arg->hot_x; hot_y = arg->hot_y;
        first = arg->first; rows = arg->rows; pitch = arg->pitch;
        if (!p->dyn && (!arg->art || !arg->hot || p->shape >= PRAST_SHAPES)) {
                for (int r = 0; r < rows; r++, fill += pitch, edge += pitch) {
                        memset(fill, 0, (size_t)span);
                        memset(edge, 0, (size_t)span);
                }
                if (p->ring)
                        ref_pointer_ring(arg);
                return;
        }
        int n = p->dyn ? 1 : p->scale < 4 * 256 ? 4 : 2, all = n * n;
        long long step = (32768ll << 16) / ((long long)all * p->scale);
        long long ox = (long long)hot_x * 65536ll / p->scale + (p->dyn ? 128 : -256ll * arg->hot[p->shape * 2]);
        long long oy = (long long)hot_y * 65536ll / p->scale + (p->dyn ? 128 : -256ll * arg->hot[p->shape * 2 + 1]);
        signed char across[PRAST_SPAN_MOST * 16], down[16];
        unsigned char share[17 * 17][2];
        for (int e = 0; !p->dyn && e <= all; e++) {
                for (int f = 0; e + f <= all; f++) {
                        int lined = e * 255 / all, filled = f ? f * 255 / (all - e) : 0;
                        if (p->scale < 256) {
                                lined = e * 255 * 256 / (all * p->scale);
                                filled = f * 255 * 256 / (all * p->scale);
                                lined = lined < 255 ? lined : 255;
                                filled = filled < 255 - lined ? filled : 255 - lined;
                                filled = filled ? filled * 255 / (255 - lined) : 0;
                        }
                        share[e * (all + 1) + f][0] = (unsigned char)filled;
                        share[e * (all + 1) + f][1] = (unsigned char)lined;
                }
        }
        for (int k = 0; !p->dyn && k < span * all; k++) {
                long long u = ((2ll * k + 1) * step >> 16) - ox;
                across[k] = (signed char)(u >= 0 && u < PRAST_CELL * 256 ? u >> 8 : -1);
        }
        struct prast_dyn_pre pre = {0};
        if (p->dyn) {
                pre.a = 256 + p->grow;
                pre.half = 32768 / p->scale + 1;
                pre.reach = pre.half * pre.a / 256 + 1;
                pre.line = p->scale < 256 ? 256 : p->scale;
                pre.lined = pre.line * 256 / p->scale;
                pre.bound = (p->hw + p->hh) * pre.a / 256 + pre.half;
                pre.tip = p->hw > p->hh ? p->hw : p->hh;
                pre.leg = p->lens && pre.tip > p->hh
                              ? (long long)ref_isqrt((unsigned long long)(pre.tip * pre.tip - (long long)p->hh * p->hh))
                              : 0;
        }
        for (int row = first; row < first + rows; row++, fill += pitch, edge += pitch) {
                if (p->dyn) {
                        long long v = ((2ll * row + 1) * step >> 16) - oy;
                        if (v > pre.bound || v < -pre.bound) {
                                memset(fill, 0, (size_t)span);
                                memset(edge, 0, (size_t)span);
                                continue;
                        }
                        for (int col = 0; col < span; col++)
                                ref_dyn_cover(p, &pre, ((2ll * col + 1) * step >> 16) - ox, v, &fill[col], &edge[col]);
                        continue;
                }
                for (int k = 0; k < all; k++) {
                        long long v = ((2ll * ((long long)row * all + k) + 1) * step >> 16) - oy;
                        down[k] = (signed char)(v >= 0 && v < PRAST_CELL * 256 ? v >> 8 : -1);
                }
                const char *shape_art = arg->art + (size_t)p->shape * PRAST_CELL * PRAST_CELL_STRIDE;
                for (int col = 0; col < span; col++) {
                        const signed char *at = across + col * all;
                        int inks[3] = {0, 0, 0};
                        for (int j = 0; j < n; j++)
                                for (int i = 0; i < n; i++) {
                                        int u = at[i * n + j], v = down[j * n + i];
                                        char ink = u >= 0 && v >= 0 ? shape_art[v * PRAST_CELL_STRIDE + u] : '.';
                                        inks[ink == '#' ? 2 : ink == 'o']++;
                                }
                        fill[col] = share[inks[2] * (all + 1) + inks[1]][0];
                        edge[col] = share[inks[2] * (all + 1) + inks[1]][1];
                }
        }
        if (p->ring)
                ref_pointer_ring(arg);
}

/* =========================================================================
 * Helpers: PRNG, assertions, timer, guard-page allocator
 * ========================================================================= */
static uint64_t rng_state = 0x9e3779b97f4a7c15ull;
static uint64_t rng_next(void)
{
        uint64_t x = rng_state;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        rng_state = x;
        return x * 0x2545f4914f6cdd1dull;
}

static int rng_range(int lo, int hi)
{
        return lo + (int)(rng_next() % (uint64_t)(hi - lo + 1));
}

static uint64_t now_ns(void)
{
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void fail(const char *msg)
{
        fprintf(stderr, "FAIL: %s\n", msg);
        exit(1);
}

struct guard_buf {
        void  *base;
        size_t total;
        size_t page;
        unsigned char *rw;
};

static struct guard_buf guard_alloc(size_t data_pages)
{
        size_t pg = (size_t)sysconf(_SC_PAGESIZE);
        size_t total = (data_pages + 2) * pg;
#ifndef MAP_ANON
#define MAP_ANON MAP_ANONYMOUS
#endif
        void *p = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (p == MAP_FAILED)
                fail("mmap guard_alloc failed");
        if (mprotect(p, pg, PROT_NONE) != 0)
                fail("mprotect leading guard failed");
        if (mprotect((unsigned char *)p + (data_pages + 1) * pg, pg, PROT_NONE) != 0)
                fail("mprotect trailing guard failed");
        struct guard_buf g = {p, total, pg, (unsigned char *)p + pg};
        return g;
}

static void guard_free(struct guard_buf g)
{
        munmap(g.base, g.total);
}

static struct prast_dyn_pre make_pre(const struct prast_pointer *p)
{
        struct prast_dyn_pre pre = {0};
        pre.a = 256 + p->grow;
        pre.half = 32768 / p->scale + 1;
        pre.reach = pre.half * pre.a / 256 + 1;
        pre.line = p->scale < 256 ? 256 : p->scale;
        pre.lined = pre.line * 256 / p->scale;
        pre.bound = (p->hw + p->hh) * pre.a / 256 + pre.half;
        pre.tip = p->hw > p->hh ? p->hw : p->hh;
        pre.leg = p->lens && pre.tip > p->hh
                      ? (long long)ref_isqrt((unsigned long long)(pre.tip * pre.tip - (long long)p->hh * p->hh))
                      : 0;
        return pre;
}

/* =========================================================================
 * Unit, Edge-Case, Security & Differential Fuzz Tests
 * ========================================================================= */
static void test_scalar_kernels(void)
{
        /* prast_counts */
        const int sums[] = {0, 1, -1, PRAST_COORD_MAX, -PRAST_COORD_MAX, INT_MAX, INT_MIN};
        const long long mores[] = {0, 1, -1, PRAST_COORD_MAX, -PRAST_COORD_MAX, 100000000000ll, -100000000000ll};
        for (size_t i = 0; i < sizeof(sums) / sizeof(sums[0]); i++)
                for (size_t j = 0; j < sizeof(mores) / sizeof(mores[0]); j++)
                        if (prast_counts(sums[i], mores[j]) != ref_counts(sums[i], mores[j]))
                                fail("prast_counts edge mismatch");
        for (int i = 0; i < 5000; i++) {
                int s = (int)rng_next();
                long long m = (long long)(rng_next() % 100000000ull) - 50000000ll;
                if (prast_counts(s, m) != ref_counts(s, m))
                        fail("prast_counts fuzz mismatch");
        }

        /* prast_gain_wanted */
        const int pcts[] = {0, 1, 50, 99, 100, 101, 150, 200, -50, -100};
        for (int flat = 0; flat <= 1; flat++)
                for (size_t j = 0; j < sizeof(pcts) / sizeof(pcts[0]); j++)
                        for (int g = -2560; g <= 2560; g += 320)
                                if (prast_gain_wanted(g, flat, pcts[j]) != ref_gain_wanted(g, flat, pcts[j]))
                                        fail("prast_gain_wanted edge mismatch");
        for (int i = 0; i < 5000; i++) {
                int g = rng_range(-10000, 10000), f = rng_range(0, 2), pct = rng_range(-200, 400);
                if (prast_gain_wanted(g, f, pct) != ref_gain_wanted(g, f, pct))
                        fail("prast_gain_wanted fuzz mismatch");
        }

        /* prast_tablet */
        if (prast_tablet(50, 100, 100, 1920) != 0) fail("prast_tablet span==0");
        if (prast_tablet(50, 200, 100, 1920) != 0) fail("prast_tablet span<0");
        if (prast_tablet(INT_MIN, -1000, 1000, 1920) != 0) fail("prast_tablet clamp lo");
        if (prast_tablet(INT_MAX, -1000, 1000, 1920) != 1920) fail("prast_tablet clamp hi");
        for (int i = 0; i < 5000; i++) {
                int mn = rng_range(-50000, 50000);
                int mx = mn + rng_range(-100, 100000);
                int v = rng_range(mn - 5000, mx + 5000);
                int sz = rng_range(-4096, 8192);
                if (prast_tablet(v, mn, mx, sz) != ref_tablet(v, mn, mx, sz))
                        fail("prast_tablet fuzz mismatch");
        }

        /* prast_shake */
        if (prast_shake(NULL, 10, 1000) != 0) fail("prast_shake NULL");
        for (int feel = -2; feel <= 4; feel++) {
                struct prast_shake sa = {0, 0, 0, feel, 0}, sr = sa;
                uint64_t t = 1000000000ull;
                const int deltas[] = {0, 3, -3, 4, -4, 5, -5, 6, -6, 8, -8, 9, -9, 20, -20, INT_MIN, INT_MAX};
                for (int step = 0; step < 200; step++) {
                        int d = deltas[rng_next() % (sizeof(deltas) / sizeof(deltas[0]))];
                        t += (rng_next() % 5 == 0) ? (PRAST_SHAKE_WINDOW + 1) : (rng_next() % 100000000ull);
                        int ra = prast_shake(&sa, d, t);
                        int rr = ref_shake(&sr, d, t);
                        if (ra != rr || memcmp(&sa, &sr, sizeof(sa)) != 0)
                                fail("prast_shake mismatch");
                }
        }

        /* prast_edges_at: NULL + all 9 zones + corner expansion + extreme coords */
        if (prast_edges_at(NULL) != 0) fail("prast_edges_at NULL");
        {
                /* Test all 9 zones + outside around (100,100)-(300,250) */
                unsigned int seen = 0;
                for (int corner = 0; corner <= 24; corner += 24) {
                        for (int py = 90; py <= 260; py += 5) {
                                for (int px = 90; px <= 310; px += 5) {
                                        struct prast_edges_arg a = {px, py, 100, 100, 300, 250, 6, 8, 12, corner};
                                        unsigned int ra = prast_edges_at(&a), rr = ref_edges_at(&a);
                                        if (ra != rr) fail("prast_edges_at grid mismatch");
                                        if (ra < 16) seen |= (1u << ra);
                                }
                        }
                }
                const unsigned int want = (1u << 0) | (1u << 1) | (1u << 2) | (1u << 4) | (1u << 8) |
                                          (1u << 5) | (1u << 6) | (1u << 9) | (1u << 10);
                if ((seen & want) != want) fail("prast_edges_at did not cover all 9 zones");
        }
        /* Extreme coordinates (overflow-safe 64-bit bounds) */
        {
                struct prast_edges_arg ext = {INT_MIN, INT_MIN, INT_MIN, INT_MIN, INT_MAX, INT_MAX, 10, 10, 10, 20};
                if (prast_edges_at(&ext) != ref_edges_at(&ext)) fail("prast_edges_at INT_MIN");
                ext.px = INT_MAX - 1; ext.py = INT_MAX - 1;
                if (prast_edges_at(&ext) != ref_edges_at(&ext)) fail("prast_edges_at INT_MAX");
        }
        for (int i = 0; i < 5000; i++) {
                int x1 = rng_range(-100000, 100000), y1 = rng_range(-100000, 100000);
                int x2 = x1 + rng_range(10, 2000), y2 = y1 + rng_range(10, 2000);
                struct prast_edges_arg a = {
                    rng_range(x1 - 30, x2 + 30), rng_range(y1 - 30, y2 + 30),
                    x1, y1, x2, y2,
                    rng_range(0, 16), rng_range(0, 20), rng_range(0, 32), rng_range(0, 40)
                };
                if (prast_edges_at(&a) != ref_edges_at(&a))
                        fail("prast_edges_at fuzz mismatch");
        }
}

static void test_dyn_cover(void)
{
        /* NULL & invalid guards */
        unsigned char f = 99, e = 99;
        prast_dyn_cover(NULL, NULL, 0, 0, NULL, NULL);
        prast_dyn_cover(NULL, NULL, 0, 0, &f, &e);
        if (f != 0 || e != 0) fail("prast_dyn_cover NULL did not zero outputs");

        /* Angles (ux, uy) with ux^2 + uy^2 ~ 64^2 */
        static const int dirs[][2] = {
            {64, 0}, {59, 24}, {45, 45}, {24, 59}, {0, 64},
            {-24, 59}, {-45, 45}, {-59, 24}, {-64, 0}, {-45, -45}, {0, -64}, {45, -45}
        };
        const int scales[] = {128, 192, 256, 384, 512, 768, 1024};

        for (int mode = 0; mode < 4; mode++) {
                for (size_t di = 0; di < sizeof(dirs) / sizeof(dirs[0]); di++) {
                        for (size_t si = 0; si < sizeof(scales) / sizeof(scales[0]); si++) {
                                struct prast_pointer p = {0};
                                p.dyn = 1;
                                p.scale = scales[si];
                                p.ux = dirs[di][0];
                                p.uy = dirs[di][1];
                                if (mode == 0) { /* circle */
                                        p.hw = 2048; p.hh = 2048; p.c = 2048;
                                } else if (mode == 1) { /* rounded rect (text) */
                                        p.hw = 1024; p.hh = 3072; p.c = 512;
                                } else if (mode == 2) { /* stretched */
                                        p.hw = 2048; p.hh = 2048; p.c = 2048; p.grow = 154;
                                } else { /* lens */
                                        p.hw = 3072; p.hh = 2048; p.c = 2048; p.lens = 1;
                                }
                                struct prast_dyn_pre pre = make_pre(&p);
                                for (long long v = -pre.bound - 200; v <= pre.bound + 200; v += 373) {
                                        for (long long u = -pre.bound - 200; u <= pre.bound + 200; u += 373) {
                                                unsigned char fa = 0xaa, ea = 0x55, fr = 0xaa, er = 0x55;
                                                prast_dyn_cover(&p, &pre, u, v, &fa, &ea);
                                                ref_dyn_cover(&p, &pre, u, v, &fr, &er);
                                                if (fa != fr || ea != er) {
                                                        fprintf(stderr, "dyn_cover mismatch mode=%d di=%zu scale=%d u=%lld v=%lld asm=(%u,%u) ref=(%u,%u)\n",
                                                                mode, di, p.scale, u, v, fa, ea, fr, er);
                                                        fail("prast_dyn_cover mismatch");
                                                }
                                        }
                                }
                        }
                }
        }
        /* Lens with tip == r */
        {
                struct prast_pointer p = {0};
                p.dyn = 1; p.scale = 256; p.hw = 2048; p.hh = 2048; p.c = 2048; p.ux = 64; p.uy = 0; p.lens = 1;
                struct prast_dyn_pre pre = make_pre(&p);
                for (long long u = -2500; u <= 2500; u += 250) {
                        for (long long v = -2500; v <= 2500; v += 250) {
                                unsigned char fa, ea, fr, er;
                                prast_dyn_cover(&p, &pre, u, v, &fa, &ea);
                                ref_dyn_cover(&p, &pre, u, v, &fr, &er);
                                if (fa != fr || ea != er) fail("prast_dyn_cover lens tip==r");
                        }
                }
        }
}

static void compare_rows(const struct prast_pointer *p, int span, int hot_x, int hot_y, int first, int rows, int pitch)
{
        size_t sz = (size_t)rows * (size_t)pitch + 64;
        unsigned char *fa = calloc(1, sz), *ea = calloc(1, sz);
        unsigned char *fr = calloc(1, sz), *er = calloc(1, sz);
        memset(fa, 0xCC, sz); memset(ea, 0xCC, sz);
        memset(fr, 0xCC, sz); memset(er, 0xCC, sz);
        struct prast_rows_arg aa = {p, &test_art[0][0][0], &test_hot[0][0], fa, ea, span, hot_x, hot_y, first, rows, pitch};
        struct prast_rows_arg ar = {p, &test_art[0][0][0], &test_hot[0][0], fr, er, span, hot_x, hot_y, first, rows, pitch};
        prast_pointer_rows(&aa);
        ref_pointer_rows(&ar);
        if (memcmp(fa, fr, sz) != 0 || memcmp(ea, er, sz) != 0) {
                for (int r = 0; r < rows; r++) {
                        for (int c = 0; c < pitch; c++) {
                                size_t idx = (size_t)r * (size_t)pitch + (size_t)c;
                                if (fa[idx] != fr[idx] || ea[idx] != er[idx]) {
                                        fprintf(stderr, "rows mismatch dyn=%d shape=%u scale=%d ring=%d span=%d row=%d col=%d asm=(%u,%u) ref=(%u,%u)\n",
                                                p->dyn, p->shape, p->scale, p->ring, span, first + r, c, fa[idx], ea[idx], fr[idx], er[idx]);
                                        fail("prast_pointer_rows mismatch");
                                }
                        }
                }
        }
        free(fa); free(ea); free(fr); free(er);
}

static void test_rows_and_ring(void)
{
        /* NULL / zero / invalid guards */
        unsigned char b1[16], b2[16];
        struct prast_pointer zp = {0};
        struct prast_rows_arg bad = {&zp, NULL, NULL, b1, b2, 0, 0, 0, 0, 0, 0};
        prast_pointer_rows(NULL);
        prast_pointer_ring(NULL);
        prast_pointer_rows(&bad);
        prast_pointer_ring(&bad);
        /* Invalid shape >= 11 safely zeroes */
        zp.dyn = 0; zp.shape = 99; zp.scale = 256;
        bad.span = 8; bad.rows = 2; bad.pitch = 8;
        memset(b1, 0x77, sizeof(b1)); memset(b2, 0x77, sizeof(b2));
        prast_pointer_rows(&bad);
        for (int i = 0; i < 16; i++)
                if (b1[i] != 0 || b2[i] != 0) fail("invalid shape did not zero");

        /* Dynamic cursor: span 1, 2, 32, 64, 128, 384; partial bands; pitch > span */
        const int dyn_spans[] = {1, 2, 32, 64, 128, PRAST_SPAN_MOST};
        for (size_t i = 0; i < sizeof(dyn_spans) / sizeof(dyn_spans[0]); i++) {
                int sp = dyn_spans[i];
                struct prast_pointer p = {0};
                p.dyn = 1; p.scale = sp > 128 ? 512 : 256;
                p.hw = 2048; p.hh = 2048; p.c = 2048; p.ux = 45; p.uy = 45;
                p.grow = (i & 1) ? 120 : 0;
                p.lens = (i == 2) ? 1 : 0;
                if (p.lens) { p.hw = 3072; p.grow = 0; }
                p.ring = (i == 3) ? 16 : (i == 5 ? 32 : 0);
                int rows = sp > 64 ? 16 : sp;
                int first = sp > 64 ? sp / 2 - 8 : 0;
                compare_rows(&p, sp, sp / 2, sp / 2, first, rows, sp + 7);
        }

        /* Dwell ring progress 0..1000 */
        {
                const int rings[] = {0, 1, 2, 7, 8, 9, 15, 16, 17, 23, 24, 25, 31, 32, 33, 64, 256, 500, 1000};
                for (size_t i = 0; i < sizeof(rings) / sizeof(rings[0]); i++) {
                        struct prast_pointer p = {0};
                        p.dyn = (int)(i & 1); p.shape = (unsigned int)(i % PRAST_SHAPES);
                        p.scale = (i % 3 == 0) ? 192 : (i % 3 == 1 ? 256 : 512);
                        p.hw = 2048; p.hh = 2048; p.c = 2048; p.ring = rings[i];
                        compare_rows(&p, 48, 24, 24, 0, 48, 52);
                }
        }

        /* Bitmap cursor: all 11 shapes across <256, 256, fractional, and >=1024 (2x2) scales */
        const int bm_scales[] = {128, 192, 256, 384, 512, 1024, 1280};
        for (unsigned int sh = 0; sh < PRAST_SHAPES; sh++) {
                for (size_t si = 0; si < sizeof(bm_scales) / sizeof(bm_scales[0]); si++) {
                        struct prast_pointer p = {0};
                        p.dyn = 0; p.shape = sh; p.scale = bm_scales[si];
                        p.ring = (sh == 0 && si == 2) ? 20 : 0;
                        int sp = (int)((long long)PRAST_CELL * p.scale / 256) + 4;
                        if (sp > 96) sp = 96;
                        compare_rows(&p, sp, sp / 2, sp / 2, 0, sp, sp + 3);
                }
        }

        /* Randomized differential fuzzing on rows+ring */
        for (int iter = 0; iter < 80; iter++) {
                struct prast_pointer p = {0};
                p.dyn = rng_range(0, 1);
                p.shape = (unsigned int)rng_range(0, PRAST_SHAPES - 1);
                p.scale = rng_range(64, 1400);
                p.hw = rng_range(512, 3072);
                p.hh = rng_range(512, 3072);
                p.c = rng_range(128, p.hw < p.hh ? p.hw : p.hh);
                p.ux = rng_range(-64, 64);
                p.uy = rng_range(-64, 64);
                if (!p.ux && !p.uy) p.ux = 64;
                p.grow = rng_range(0, 2) == 0 ? rng_range(1, 200) : 0;
                p.lens = (p.dyn && !p.grow && rng_range(0, 3) == 0) ? 1 : 0;
                p.ring = rng_range(0, 2) == 0 ? rng_range(1, 40) : 0;
                int sp = rng_range(1, 64);
                int rows = rng_range(1, sp);
                int first = rng_range(-2, sp - rows + 2);
                int pitch = sp + rng_range(0, 11);
                compare_rows(&p, sp, rng_range(-4, sp + 4), rng_range(-4, sp + 4), first, rows, pitch);
        }
}

static void test_mmap_guard_pages(void)
{
        const int spans[] = {1, 7, 16, 64, 128, PRAST_SPAN_MOST};
        for (size_t i = 0; i < sizeof(spans) / sizeof(spans[0]); i++) {
                int sp = spans[i];
                int rows = sp <= 64 ? sp : 4;
                size_t bytes = (size_t)sp * (size_t)rows;
                size_t pg = (size_t)sysconf(_SC_PAGESIZE);
                size_t pages = (bytes + pg - 1) / pg;

                struct guard_buf gf = guard_alloc(pages);
                struct guard_buf ge = guard_alloc(pages);

                for (int flush_end = 0; flush_end <= 1; flush_end++) {
                        unsigned char *fb = flush_end ? (gf.rw + pages * pg - bytes) : gf.rw;
                        unsigned char *eb = flush_end ? (ge.rw + pages * pg - bytes) : ge.rw;
                        for (int dyn = 0; dyn <= 1; dyn++) {
                                struct prast_pointer p = {0};
                                p.dyn = dyn; p.shape = 0; p.scale = 256;
                                p.hw = 2048; p.hh = 2048; p.c = 2048; p.ring = 24;
                                struct prast_rows_arg arg = {
                                    &p, &test_art[0][0][0], &test_hot[0][0],
                                    fb, eb, sp, sp / 2, rows / 2, 0, rows, sp
                                };
                                prast_pointer_rows(&arg);
                        }
                }
                guard_free(gf);
                guard_free(ge);
        }

        /* Single-byte guard-page test for prast_dyn_cover */
        {
                struct guard_buf gf = guard_alloc(1);
                struct guard_buf ge = guard_alloc(1);
                struct prast_pointer p = {0};
                p.dyn = 1; p.scale = 256; p.hw = 2048; p.hh = 2048; p.c = 2048;
                struct prast_dyn_pre pre = make_pre(&p);
                /* Flush at leading guard */
                prast_dyn_cover(&p, &pre, 1000, 1000, gf.rw, ge.rw);
                /* Flush at trailing guard */
                prast_dyn_cover(&p, &pre, 1000, 1000, gf.rw + gf.page - 1, ge.rw + ge.page - 1);
                guard_free(gf);
                guard_free(ge);
        }
}

/* =========================================================================
 * Speed Benchmark (ASM vs -O2 C reference)
 * ========================================================================= */
static volatile uint64_t bench_sink;

static void run_benchmarks(void)
{
        printf("\n=== Speed Benchmark (ASM vs -O2 C Reference) ===\n");

        /* 1. Dynamic cursor 64x64 */
        {
                unsigned char f[64 * 64], e[64 * 64];
                struct prast_pointer p = {0};
                p.dyn = 1; p.scale = 256; p.hw = 2048; p.hh = 2048; p.c = 2048;
                struct prast_rows_arg arg = {&p, NULL, NULL, f, e, 64, 32, 32, 0, 64, 64};
                const int iters = 4000;
                uint64_t t0 = now_ns();
                for (int i = 0; i < iters; i++) ref_pointer_rows(&arg);
                uint64_t t1 = now_ns();
                for (int i = 0; i < iters; i++) prast_pointer_rows(&arg);
                uint64_t t2 = now_ns();
                bench_sink += f[2000] + e[2000];
                double r_ns = (double)(t1 - t0) / iters, a_ns = (double)(t2 - t1) / iters;
                printf("  dyn_rows(64x64):     C=%8.1f ns/op  ASM=%8.1f ns/op  speedup=%.2fx\n", r_ns, a_ns, r_ns / a_ns);
        }

        /* 2. Bitmap cursor 48x48 (4x4 supersample) */
        {
                unsigned char f[48 * 48], e[48 * 48];
                struct prast_pointer p = {0};
                p.dyn = 0; p.shape = 0; p.scale = 384;
                struct prast_rows_arg arg = {&p, &test_art[0][0][0], &test_hot[0][0], f, e, 48, 12, 12, 0, 48, 48};
                const int iters = 4000;
                uint64_t t0 = now_ns();
                for (int i = 0; i < iters; i++) ref_pointer_rows(&arg);
                uint64_t t1 = now_ns();
                for (int i = 0; i < iters; i++) prast_pointer_rows(&arg);
                uint64_t t2 = now_ns();
                bench_sink += f[500] + e[500];
                double r_ns = (double)(t1 - t0) / iters, a_ns = (double)(t2 - t1) / iters;
                printf("  bitmap_rows(48x48):  C=%8.1f ns/op  ASM=%8.1f ns/op  speedup=%.2fx\n", r_ns, a_ns, r_ns / a_ns);
        }

        /* 3. Dwell ring 64x64 */
        {
                unsigned char f[64 * 64], e[64 * 64];
                memset(f, 0, sizeof(f)); memset(e, 0, sizeof(e));
                struct prast_pointer p = {0};
                p.dyn = 1; p.scale = 256; p.ring = 24;
                struct prast_rows_arg arg = {&p, NULL, NULL, f, e, 64, 32, 32, 0, 64, 64};
                const int iters = 6000;
                uint64_t t0 = now_ns();
                for (int i = 0; i < iters; i++) ref_pointer_ring(&arg);
                uint64_t t1 = now_ns();
                for (int i = 0; i < iters; i++) prast_pointer_ring(&arg);
                uint64_t t2 = now_ns();
                bench_sink += f[2000] + e[2000];
                double r_ns = (double)(t1 - t0) / iters, a_ns = (double)(t2 - t1) / iters;
                printf("  ring(64x64):         C=%8.1f ns/op  ASM=%8.1f ns/op  speedup=%.2fx\n", r_ns, a_ns, r_ns / a_ns);
        }

        /* 4. Scalar hot loop (shake + edges_at + counts + gain_wanted + tablet) */
        {
                struct prast_shake s = {0, 0, 0, 0, 0};
                struct prast_edges_arg ea = {104, 105, 100, 100, 500, 400, 6, 8, 12, 24};
                const int iters = 500000;
                uint64_t acc = 0, t0 = now_ns();
                for (int i = 0; i < iters; i++) {
                        acc += (uint64_t)ref_counts(i, 123);
                        acc += (uint64_t)ref_gain_wanted(1500, i & 1, 125);
                        acc += (uint64_t)ref_tablet(i & 4095, 0, 4095, 1920);
                        acc += (uint64_t)ref_shake(&s, (i & 1) ? 10 : -10, (uint64_t)i * 1000000ull);
                        acc += (uint64_t)ref_edges_at(&ea);
                }
                uint64_t t1 = now_ns();
                s.window = 0; s.direction = 0; s.count = 0;
                for (int i = 0; i < iters; i++) {
                        acc += (uint64_t)prast_counts(i, 123);
                        acc += (uint64_t)prast_gain_wanted(1500, i & 1, 125);
                        acc += (uint64_t)prast_tablet(i & 4095, 0, 4095, 1920);
                        acc += (uint64_t)prast_shake(&s, (i & 1) ? 10 : -10, (uint64_t)i * 1000000ull);
                        acc += (uint64_t)prast_edges_at(&ea);
                }
                uint64_t t2 = now_ns();
                bench_sink += acc;
                double r_ns = (double)(t1 - t0) / iters, a_ns = (double)(t2 - t1) / iters;
                printf("  scalar_bundle(5x):   C=%8.2f ns/op  ASM=%8.2f ns/op  speedup=%.2fx\n", r_ns, a_ns, r_ns / a_ns);
        }
}

int main(void)
{
        init_test_art();
        test_scalar_kernels();
        test_dyn_cover();
        test_rows_and_ring();
        test_mmap_guard_pages();
        printf("PASS: all pointer_rast tests (edges, fuzz, guard-pages, double-emit) passed!\n");
        run_benchmarks();
        return 0;
}
