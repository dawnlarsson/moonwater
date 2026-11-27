/*
        Exhaustive correctness, security (guard-page + NULL + overflow + fuzz),
        and performance test suite for src/canvas/compositor/input_scan.asm.c.
*/
#define COMPOSITOR_INPUT_SCAN_ASM_IMPLEMENTATION
#include "../src/canvas/compositor/input_scan.asm.c"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static uint64_t g_checks = 0;
static uint64_t g_fails = 0;

#define CHECK(cond, fmt, ...) do { \
        g_checks++; \
        if (!(cond)) { \
                g_fails++; \
                fprintf(stderr, "FAIL [%s:%d]: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
                if (g_fails > 25) exit(1); \
        } \
} while (0)

/* ---- -O2 C reference implementations (noinline for size & speed comparison) ---- */

static uint64_t ref_isqrt64(uint64_t n)
{
        uint64_t root = 0, bit = 1ull << 62;
        while (bit > n) bit >>= 2;
        for (; bit; bit >>= 2) {
                if (n >= root + bit) { n -= root + bit; root = (root >> 1) + bit; }
                else root >>= 1;
        }
        return root;
}

__attribute__((noinline))
int32_t ref_evdev_batch(const struct inscan_event *raw, uint32_t count, uint32_t flags, struct inscan_batch *out)
{
        if (!raw || !count) return INSCAN_BATCH_FALLBACK;
        {
                uint32_t k = 0;
                while (k < count && (raw[k].type == 4u || (raw[k].type == 0u && raw[k].code == 0u)))
                        k++;
                if (k == count) return INSCAN_BATCH_NOISE;
        }
        if ((flags & INSCAN_F_NO_DIRECT) || !out) return INSCAN_BATCH_FALLBACK;

        int64_t dx = 0, dy = 0;
        int32_t key = -1, key_val = 0, btn_val = 0, wheel_v120 = 0, wheel_horiz = 0;
        uint32_t btn = 0;
        for (uint32_t k = 0; k < count; k++) {
                uint16_t ty = raw[k].type, co = raw[k].code;
                int32_t va = raw[k].value;
                if (ty == 2u && co == 0u) {
                        dx = (int64_t)((uint64_t)dx + (uint64_t)(int64_t)va);
                } else if (ty == 2u && co == 1u) {
                        dy = (int64_t)((uint64_t)dy + (uint64_t)(int64_t)va);
                } else if (ty == 2u && (co == 8u || co == 6u || co == 11u || co == 12u) && !wheel_v120) {
                        int fine = (co == 11u || co == 12u);
                        int side = (co == 6u || co == 12u);
                        if (!fine && ((flags >> (3 + side)) & 1u)) continue;
                        uint32_t v = fine ? (uint32_t)va : (uint32_t)va * 120u;
                        if (flags & INSCAN_F_FLIP) v = 0u - v;
                        wheel_v120 = (int32_t)v;
                        wheel_horiz = side;
                } else if (ty == 1u && co >= 0x110u && co <= 0x117u && !btn) {
                        btn = co; btn_val = va;
                } else if (ty == 1u && co < 256u && key < 0) {
                        key = (int32_t)co; key_val = va;
                } else if (ty != 4u && !(ty == 0u && co == 0u)) {
                        return INSCAN_BATCH_FALLBACK;
                }
        }
        uint64_t micro = (uint64_t)raw[count - 1].seconds * 1000000ull + (uint64_t)raw[count - 1].microseconds;
        if (key >= 0) {
                if (dx || dy || btn || wheel_v120) return INSCAN_BATCH_FALLBACK;
                memset(out, 0, sizeof(*out));
                out->micro = micro;
                out->key = key;
                out->key_val = key_val;
                return INSCAN_BATCH_KEY;
        }
        if (!dx && !dy && !btn && !wheel_v120) return INSCAN_BATCH_FALLBACK;
        out->dx = dx; out->dy = dy; out->micro = micro;
        out->key = -1; out->key_val = 0;
        out->btn = btn; out->btn_val = btn_val;
        out->wheel_v120 = wheel_v120; out->wheel_horiz = wheel_horiz;
        return INSCAN_BATCH_POINTER;
}

__attribute__((noinline))
int64_t ref_scale_rem(int64_t delta, int64_t num, int64_t den, int64_t *rem)
{
        if (!den || !rem) return 0;
        int64_t total = (int64_t)((uint64_t)delta * (uint64_t)num + (uint64_t)*rem);
        if (den == -1) { *rem = 0; return (int64_t)(0ull - (uint64_t)total); }
        int64_t out = total / den;
        *rem = total - out * den;
        return out;
}

static inline int64_t ref_abs64(int64_t v) { return v < 0 ? -v : v; }
static inline int64_t ref_clamp64(int64_t v, int64_t lo, int64_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline int64_t ref_predict_px(int64_t fx) { return fx >= 0 ? (fx + 512) / 1024 : -((-fx + 512) / 1024); }

__attribute__((noinline))
void ref_predict_lead(struct inscan_predict *track, uint64_t period_ns, int32_t scale)
{
        if (!track) return;
        track->lead_x = track->lead_y = 0;
        if (track->count < 2 || track->count > INSCAN_PREDICT_HISTORY) return;
        int64_t lim = (int64_t)INSCAN_PREDICT_CAP * (int64_t)scale / 100;
        if (lim <= 0) return;
        int64_t n = track->count, tn = (int64_t)track->when[n - 1], i0 = n - 1;
        while (i0 > 0 && (int64_t)track->when[i0 - 1] >= tn - (int64_t)INSCAN_PREDICT_WINDOW_NS) i0--;
        int64_t m = n - i0;
        if (m < 2 || tn - (int64_t)track->when[i0] < (int64_t)INSCAN_PREDICT_SPAN_NS) return;
        int64_t period = (int64_t)period_ns / 1000;
        int64_t St = 0, Stt = 0, Sx = 0, Stx = 0, Sy = 0, Sty = 0, path = 0;
        for (int64_t i = i0; i < n; i++) {
                int64_t t = ((int64_t)track->when[i] - tn) / 1000;
                St += t; Stt += t * t;
                Sx += track->x[i]; Stx += t * track->x[i];
                Sy += track->y[i]; Sty += t * track->y[i];
                if (i > i0)
                        path += ref_abs64(track->x[i] - track->x[i - 1]) + ref_abs64(track->y[i] - track->y[i - 1]);
        }
        int64_t denom = m * Stt - St * St;
        if (!denom) return;
        int64_t lx = (m * Stx - St * Sx) * period / denom;
        int64_t ly = (m * Sty - St * Sy) * period / denom;
        int64_t net = ref_abs64(track->x[n - 1] - track->x[i0]) + ref_abs64(track->y[n - 1] - track->y[i0]);
        int64_t g = path > 0 ? net * 1000 / path : 0;
        int64_t w = ref_clamp64((g - INSCAN_PREDICT_STRAIGHT_LO) * 1000 / INSCAN_PREDICT_STRAIGHT_SPAN, 0, 1000);
        int64_t ws = ref_clamp64((net * 1000000 / (tn - (int64_t)track->when[i0]) - INSCAN_PREDICT_STILL_LO) * 1000 /
                                 (INSCAN_PREDICT_STILL_HI - INSCAN_PREDICT_STILL_LO), 0, 1000);
        lx = lx * w / 1000 * ws / 1000;
        ly = ly * w / 1000 * ws / 1000;
        if ((track->x[n - 1] - track->x[n - 2] < 0 && lx > 0) || (track->x[n - 1] - track->x[n - 2] > 0 && lx < 0)) lx = 0;
        if ((track->y[n - 1] - track->y[n - 2] < 0 && ly > 0) || (track->y[n - 1] - track->y[n - 2] > 0 && ly < 0)) ly = 0;
        lx = ref_predict_px(lx);
        ly = ref_predict_px(ly);
        int64_t len2 = lx * lx + ly * ly;
        if (len2 > lim * lim) {
                int64_t len = (int64_t)ref_isqrt64((uint64_t)len2);
                len += (len * len < len2);
                lx = lx * lim / len;
                ly = ly * lim / len;
        }
        track->lead_x = lx * INSCAN_PREDICT_FX;
        track->lead_y = ly * INSCAN_PREDICT_FX;
}

__attribute__((noinline))
int32_t ref_wheel_stream(struct inscan_wheel_dev *dev, uint32_t *hist_n, uint64_t now)
{
        if (!dev) return 0;
        uint64_t last = dev->wheel_at, since = dev->wheel_stream_at;
        int within = last && now >= last && now - last <= INSCAN_HAND_GAP_NS;
        int stream = within || (dev->wheel_streamed && since && now >= since && now - since <= INSCAN_WHEEL_MEMORY_NS);
        if (stream) {
                dev->wheel_streamed |= (uint8_t)(++dev->wheel_run >= INSCAN_WHEEL_STREAMS);
                dev->wheel_stream_at = now;
        } else {
                dev->wheel_run = 0;
                if (hist_n) *hist_n = 0;
        }
        dev->wheel_at = now;
        return stream;
}

__attribute__((noinline))
int64_t ref_stream_gain(uint64_t rate)
{
        int64_t x = rate > 100000 ? 100000 : rate > 1000 ? (int64_t)(rate - 1000) : 0;
        if (!x) return INSCAN_GAIN_BASE;
        uint64_t xq = (uint64_t)x * 65536 / 3500;
        if (xq > 65536) xq = 65536;
        uint64_t f = xq * ref_isqrt64(xq * 65536) / 65536;
        return INSCAN_GAIN_BASE + INSCAN_GAIN_BASE * 3 * (int64_t)f / 65536;
}

__attribute__((noinline))
uint64_t ref_stream_rate(const uint64_t *hist, uint32_t n)
{
        if (!hist || n < 2) return 0;
        uint32_t m = n < INSCAN_STREAM_HIST ? n : INSCAN_STREAM_HIST;
        uint64_t last = hist[(n - 1) % INSCAN_STREAM_HIST];
        uint64_t first = hist[(n - m) % INSCAN_STREAM_HIST];
        return last < first ? 0 : last == first ? 100000000000ull : (uint64_t)(m - 1) * 100000000000ull / (last - first);
}

__attribute__((noinline))
int64_t ref_stream_turn(int64_t value120, int64_t gain, int32_t speed, int64_t *rest)
{
        if (!rest) return 0;
        value120 = ref_clamp64(value120, -1000000000ll, 1000000000ll);
        return ref_scale_rem(value120, gain * (int64_t)speed, INSCAN_GAIN_TURN, rest);
}

/* ---- PRNG ---- */
static uint64_t rng_state = 0x9e3779b97f4a7c15ull;
static uint64_t xrand(void)
{
        uint64_t x = rng_state;
        x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
        rng_state = x;
        return x * 0x2545f4914f6cdd1dull;
}

/* ---- Guard-page helper ---- */
struct guard_buf {
        void  *base;
        size_t total;
        size_t page;
};

static struct guard_buf guard_alloc(size_t payload_bytes)
{
        size_t page = (size_t)sysconf(_SC_PAGESIZE);
        size_t mid = ((payload_bytes + page - 1) / page) * page;
        if (!mid) mid = page;
        size_t total = page + mid + page;
        void *p = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (p == MAP_FAILED) { perror("mmap"); exit(1); }
        if (mprotect(p, page, PROT_NONE) != 0) { perror("mprotect lo"); exit(1); }
        if (mprotect((char *)p + page + mid, page, PROT_NONE) != 0) { perror("mprotect hi"); exit(1); }
        struct guard_buf g = { p, total, page };
        return g;
}

static void guard_free(struct guard_buf g) { munmap(g.base, g.total); }
static void *guard_lo(struct guard_buf g) { return (char *)g.base + g.page; }
static void *guard_hi(struct guard_buf g, size_t bytes) { return (char *)g.base + g.total - g.page - bytes; }

static void check_evdev_pair(const struct inscan_event *ev, uint32_t count, uint32_t flags)
{
        struct inscan_batch a, r;
        memset(&a, 0xaa, sizeof(a));
        memset(&r, 0xaa, sizeof(r));
        int32_t ka = inscan_evdev_batch(ev, count, flags, &a);
        int32_t kr = ref_evdev_batch(ev, count, flags, &r);
        CHECK(ka == kr, "evdev kind mismatch: asm=%d ref=%d count=%u flags=%u", ka, kr, count, flags);
        if (ka == INSCAN_BATCH_KEY || ka == INSCAN_BATCH_POINTER) {
                CHECK(memcmp(&a, &r, sizeof(a)) == 0,
                      "evdev batch mismatch (kind=%d): dx=%lld/%lld dy=%lld/%lld micro=%llu/%llu key=%d/%d btn=%u/%u w=%d/%d h=%d/%d",
                      ka, (long long)a.dx, (long long)r.dx, (long long)a.dy, (long long)r.dy,
                      (unsigned long long)a.micro, (unsigned long long)r.micro,
                      a.key, r.key, a.btn, r.btn, a.wheel_v120, r.wheel_v120, a.wheel_horiz, r.wheel_horiz);
        }
}

static void test_null_and_edges(void)
{
        struct inscan_batch b;
        struct inscan_event ev = {10, 500, 2, 0, 5};
        /* NULL & zero-count checks */
        CHECK(inscan_evdev_batch(NULL, 1, 0, &b) == 0, "evdev NULL raw");
        CHECK(inscan_evdev_batch(&ev, 0, 0, &b) == 0, "evdev count=0");
        CHECK(inscan_evdev_batch(&ev, 1, 0, NULL) == 0, "evdev NULL out");
        struct inscan_event noise = {1, 2, 4, 99, 123};
        CHECK(inscan_evdev_batch(&noise, 1, INSCAN_F_NO_DIRECT, NULL) == INSCAN_BATCH_NOISE, "evdev noise with NULL out");
        CHECK(inscan_evdev_batch(&ev, 1, INSCAN_F_NO_DIRECT, NULL) == INSCAN_BATCH_FALLBACK, "evdev non-noise with NO_DIRECT");

        /* INT32_MIN / INT32_MAX deltas & wheel */
        struct inscan_event ext[4] = {
                {1, 1, 2, 0, INT32_MIN},
                {1, 2, 2, 0, INT32_MIN},
                {1, 3, 2, 1, INT32_MAX},
                {1, 4, 0, 0, 0},
        };
        check_evdev_pair(ext, 4, 0);
        struct inscan_event whext[2] = {
                {2, 5, 2, 11, INT32_MIN},
                {2, 6, 0, 0, 0},
        };
        check_evdev_pair(whext, 2, 0);
        check_evdev_pair(whext, 2, INSCAN_F_FLIP);

        /* scale_rem edge cases */
        inscan_i64 rem = 7;
        CHECK(inscan_scale_rem(10, 5, 0, &rem) == 0 && rem == 7, "scale_rem den=0");
        CHECK(inscan_scale_rem(10, 5, 3, NULL) == 0, "scale_rem NULL rem");
        inscan_i64 r1 = 0, r2 = 0;
        CHECK(inscan_scale_rem(INT64_MIN, 1, -1, &r1) == ref_scale_rem(INT64_MIN, 1, -1, &r2) && r1 == r2, "scale_rem INT64_MIN/-1");

        /* predict_lead edge cases */
        inscan_predict_lead(NULL, 16666666ull, 100);
        struct inscan_predict p;
        memset(&p, 0, sizeof(p));
        p.lead_x = 999; p.lead_y = -999;
        inscan_predict_lead(&p, 16666666ull, 100);
        CHECK(p.lead_x == 0 && p.lead_y == 0, "predict count=0 clears lead");
        p.count = 1; p.lead_x = 123;
        inscan_predict_lead(&p, 16666666ull, 100);
        CHECK(p.lead_x == 0 && p.lead_y == 0, "predict count=1 clears lead");
        p.count = 17; p.lead_x = 123;
        inscan_predict_lead(&p, 16666666ull, 100);
        CHECK(p.lead_x == 0 && p.lead_y == 0, "predict count>16 clears lead");
        p.count = 2; p.when[0] = 0; p.when[1] = 10000000ull; p.x[1] = 100000;
        inscan_predict_lead(&p, 16666666ull, 0);
        CHECK(p.lead_x == 0 && p.lead_y == 0, "predict scale=0 clears lead");
        inscan_predict_lead(&p, 16666666ull, -100);
        CHECK(p.lead_x == 0 && p.lead_y == 0, "predict scale<0 clears lead");

        /* wheel_stream, stream_rate, stream_gain, stream_turn NULL & extremes */
        CHECK(inscan_wheel_stream(NULL, NULL, 1000) == 0, "wheel_stream NULL dev");
        struct inscan_wheel_dev wd;
        memset(&wd, 0, sizeof(wd));
        CHECK(inscan_wheel_stream(&wd, NULL, 1000) == 0 && wd.wheel_at == 1000, "wheel_stream NULL hist_n");
        CHECK(inscan_stream_rate(NULL, 5) == 0, "stream_rate NULL hist");
        uint64_t h[6] = {100, 100, 100, 100, 100, 100};
        CHECK(inscan_stream_rate(h, 0) == 0 && inscan_stream_rate(h, 1) == 0, "stream_rate n<2");
        CHECK(inscan_stream_rate(h, 2) == 100000000000ull, "stream_rate equal timestamps");
        h[0] = 200; h[1] = 100;
        CHECK(inscan_stream_rate(h, 2) == 0, "stream_rate non-monotone");
        CHECK(inscan_stream_gain(0) == 2048 && inscan_stream_gain(1000) == 2048, "stream_gain low");
        CHECK(inscan_stream_gain(UINT64_MAX) == ref_stream_gain(UINT64_MAX), "stream_gain UINT64_MAX");
        CHECK(inscan_stream_turn(100, 2048, 100, NULL) == 0, "stream_turn NULL rest");
}

static void test_guard_pages(void)
{
        /* 1. evdev batch flush against PROT_NONE before AND after for counts 1..64 */
        struct guard_buf g_ev = guard_alloc(64 * sizeof(struct inscan_event));
        for (uint32_t cnt = 1; cnt <= 64; cnt++) {
                size_t bytes = cnt * sizeof(struct inscan_event);
                struct inscan_event *lo = (struct inscan_event *)guard_lo(g_ev);
                struct inscan_event *hi = (struct inscan_event *)guard_hi(g_ev, bytes);
                for (uint32_t i = 0; i < cnt; i++) {
                        struct inscan_event e = { (int64_t)(100 + i), (int64_t)(i * 125), 2, (uint16_t)(i & 1), (int32_t)(i + 1) };
                        if (i == cnt - 1) { e.type = 0; e.code = 0; e.value = 0; }
                        lo[i] = hi[i] = e;
                }
                check_evdev_pair(lo, cnt, 0);
                check_evdev_pair(hi, cnt, 0);
                /* Also pure noise flush against guard pages */
                for (uint32_t i = 0; i < cnt; i++) {
                        struct inscan_event e = { (int64_t)i, 0, (uint16_t)((i & 1) ? 4 : 0), 0, 0 };
                        lo[i] = hi[i] = e;
                }
                check_evdev_pair(lo, cnt, INSCAN_F_NO_DIRECT);
                check_evdev_pair(hi, cnt, INSCAN_F_NO_DIRECT);
        }
        guard_free(g_ev);

        /* 2. predict_lead struct flush against PROT_NONE before AND after */
        struct guard_buf g_pr = guard_alloc(sizeof(struct inscan_predict));
        struct inscan_predict *plo = (struct inscan_predict *)guard_lo(g_pr);
        struct inscan_predict *phi = (struct inscan_predict *)guard_hi(g_pr, sizeof(struct inscan_predict));
        for (int side = 0; side < 2; side++) {
                struct inscan_predict *p = side ? phi : plo;
                struct inscan_predict ref;
                memset(p, 0, sizeof(*p));
                p->count = 16;
                for (int i = 0; i < 16; i++) {
                        p->x[i] = (int64_t)i * 4096;
                        p->y[i] = (int64_t)i * 2048;
                        p->when[i] = 1000000000ull + (uint64_t)i * 2000000ull;
                }
                ref = *p;
                inscan_predict_lead(p, 16666666ull, 150);
                ref_predict_lead(&ref, 16666666ull, 150);
                CHECK(p->lead_x == ref.lead_x && p->lead_y == ref.lead_y,
                      "guard predict side=%d mismatch: (%lld,%lld) vs (%lld,%lld)",
                      side, (long long)p->lead_x, (long long)p->lead_y, (long long)ref.lead_x, (long long)ref.lead_y);
        }
        guard_free(g_pr);

        /* 3. stream_rate 6-slot history flush against PROT_NONE before AND after */
        struct guard_buf g_h = guard_alloc(6 * sizeof(uint64_t));
        uint64_t *hlo = (uint64_t *)guard_lo(g_h);
        uint64_t *hhi = (uint64_t *)guard_hi(g_h, 6 * sizeof(uint64_t));
        for (int i = 0; i < 6; i++) hlo[i] = hhi[i] = 1000000ull + (uint64_t)i * 15000000ull;
        for (uint32_t n = 0; n <= 18; n++) {
                CHECK(inscan_stream_rate(hlo, n) == ref_stream_rate(hlo, n), "guard rate lo n=%u", n);
                CHECK(inscan_stream_rate(hhi, n) == ref_stream_rate(hhi, n), "guard rate hi n=%u", n);
        }
        guard_free(g_h);
}

static void test_differential_fuzz(void)
{
        /* 1. Fuzz evdev_batch across 20,000 randomized batches of 0..64 events */
        struct inscan_event ev[64];
        for (int iter = 0; iter < 20000; iter++) {
                uint32_t cnt = (uint32_t)(xrand() % 65);
                uint32_t flags = (uint32_t)(xrand() & 31u);
                int mode = iter % 6;
                for (uint32_t i = 0; i < cnt; i++) {
                        uint64_t r = xrand();
                        ev[i].seconds = (int64_t)(r % 100000);
                        ev[i].microseconds = (int64_t)((r >> 17) % 1000000);
                        if (mode == 0) { /* pure noise */
                                ev[i].type = (r & 1) ? 4 : 0;
                                ev[i].code = 0;
                                ev[i].value = (int32_t)r;
                        } else if (mode == 1) { /* pointer + wheel + btn */
                                uint32_t pick = (uint32_t)((r >> 8) % 8);
                                if (pick < 3) { ev[i].type = 2; ev[i].code = (uint16_t)(pick & 1); ev[i].value = (int32_t)(int16_t)r; }
                                else if (pick == 3) { static const uint16_t wc[4] = {6, 8, 11, 12}; ev[i].type = 2; ev[i].code = wc[(r >> 16) & 3]; ev[i].value = (int32_t)((r >> 24) % 9) - 4; }
                                else if (pick == 4) { ev[i].type = 1; ev[i].code = (uint16_t)(0x110 + ((r >> 16) & 7)); ev[i].value = (int32_t)(r & 1); }
                                else { ev[i].type = (pick & 1) ? 4 : 0; ev[i].code = 0; ev[i].value = 0; }
                        } else if (mode == 2) { /* single key */
                                if (i == 0) { ev[i].type = 1; ev[i].code = (uint16_t)(r & 255); ev[i].value = (int32_t)(r % 3); }
                                else { ev[i].type = (r & 1) ? 4 : 0; ev[i].code = 0; ev[i].value = 0; }
                        } else { /* wild mix including invalid types/codes/INT32 extremes */
                                ev[i].type = (uint16_t)((r >> 4) % 6);
                                ev[i].code = (uint16_t)((r >> 12) % 300);
                                ev[i].value = (r & 16) ? ((r & 32) ? INT32_MIN : INT32_MAX) : (int32_t)(int16_t)(r >> 20);
                        }
                }
                check_evdev_pair(ev, cnt, flags);
        }

        /* 2. Fuzz scale_rem across 10,000 cases */
        for (int iter = 0; iter < 10000; iter++) {
                int64_t delta = (int64_t)(int32_t)xrand();
                int64_t num = (int64_t)(int16_t)xrand();
                int64_t den = (int64_t)(int16_t)xrand();
                if ((iter & 31) == 0) den = 0;
                if ((iter & 63) == 1) den = -1;
                inscan_i64 ra = (int64_t)(int16_t)xrand(), rr = ra;
                inscan_i64 oa = inscan_scale_rem(delta, num, den, &ra);
                int64_t or_ = ref_scale_rem(delta, num, den, &rr);
                CHECK(oa == or_ && ra == rr, "scale_rem mismatch: (%lld,%lld) vs (%lld,%lld)",
                      (long long)oa, (long long)ra, (long long)or_, (long long)rr);
        }

        /* 3. Fuzz predict_lead across 10,000 trajectories (stationary, fast, curved, reversing, non-monotone) */
        for (int iter = 0; iter < 10000; iter++) {
                struct inscan_predict pa, pr;
                memset(&pa, 0, sizeof(pa));
                uint32_t n = (uint32_t)(xrand() % 18);
                pa.count = n;
                uint32_t lim_n = n <= 16 ? n : 16;
                uint64_t t = 10000000000ull;
                int64_t vx = ((int64_t)(xrand() % 20001) - 10000);
                int64_t vy = ((int64_t)(xrand() % 20001) - 10000);
                int64_t x = 500000, y = 500000;
                for (uint32_t i = 0; i < lim_n; i++) {
                        uint64_t dt = (iter % 10 == 0) ? (xrand() % 20000000ull) : (1000000ull + (xrand() % 8000000ull));
                        if (iter % 25 == 0 && i > 0) t -= dt / 2; /* non-monotone timestamp */
                        else t += dt;
                        x += vx + ((int64_t)(xrand() % 4097) - 2048);
                        y += vy + ((int64_t)(xrand() % 4097) - 2048);
                        if (iter % 7 == 0 && i == lim_n - 1) { x -= vx * 3; y -= vy * 3; } /* reversal */
                        if (iter % 11 == 0) { x = 500000; y = 500000; } /* stationary */
                        pa.x[i] = x; pa.y[i] = y; pa.when[i] = t;
                }
                pr = pa;
                uint64_t period_ns = 4000000ull + (xrand() % 30000000ull);
                int32_t scale = (iter % 20 == 0) ? (int32_t)((xrand() % 5) - 2) * 50 : (int32_t)(50 + (xrand() % 350));
                inscan_predict_lead(&pa, period_ns, scale);
                ref_predict_lead(&pr, period_ns, scale);
                CHECK(pa.lead_x == pr.lead_x && pa.lead_y == pr.lead_y,
                      "predict iter=%d n=%u mismatch: (%lld,%lld) vs (%lld,%lld)",
                      iter, n, (long long)pa.lead_x, (long long)pa.lead_y, (long long)pr.lead_x, (long long)pr.lead_y);
        }

        /* 4. Fuzz wheel_stream, stream_gain, stream_rate, stream_turn */
        struct inscan_wheel_dev da, dr;
        memset(&da, 0, sizeof(da));
        memset(&dr, 0, sizeof(dr));
        inscan_u32 ha = 5, hr = 5;
        uint64_t now = 1000000000ull;
        for (int iter = 0; iter < 5000; iter++) {
                uint64_t r = xrand();
                if ((r & 15) == 0) now -= (r % 100000000ull); /* non-monotone */
                else if ((r & 7) == 0) now += 4000000000ull; /* > memory */
                else if ((r & 3) == 0) now += 100000000ull;  /* > gap, <= memory */
                else now += (r % 60000001ull);               /* <= gap */
                int32_t sa = inscan_wheel_stream(&da, &ha, now);
                int32_t sr = ref_wheel_stream(&dr, &hr, now);
                CHECK(sa == sr && ha == hr && memcmp(&da, &dr, sizeof(da)) == 0,
                      "wheel_stream iter=%d mismatch: sa=%d sr=%d ha=%u hr=%u", iter, sa, sr, ha, hr);
        }

        for (uint64_t rate = 0; rate <= 105000; rate += (rate < 5000 ? 1 : 97)) {
                CHECK(inscan_stream_gain(rate) == ref_stream_gain(rate), "stream_gain rate=%llu", (unsigned long long)rate);
        }

        uint64_t hist[6];
        for (int iter = 0; iter < 5000; iter++) {
                uint64_t base = 1000000000ull;
                for (int i = 0; i < 6; i++) {
                        base += (xrand() % 100000000ull);
                        hist[i] = base;
                }
                uint32_t n = (iter < 100) ? (uint32_t)iter : (uint32_t)(xrand() % 10000);
                if (iter == 100) n = UINT32_MAX;
                CHECK(inscan_stream_rate(hist, n) == ref_stream_rate(hist, n), "stream_rate iter=%d n=%u", iter, n);
        }

        inscan_i64 resta = 0, restr = 0;
        for (int iter = 0; iter < 5000; iter++) {
                int64_t v = ((iter & 7) == 0) ? ((iter & 8) ? INT64_MAX : INT64_MIN) : ((int64_t)(int32_t)xrand());
                int64_t g = 2048 + (int64_t)(xrand() % 6145);
                int32_t sp = 25 + (int32_t)(xrand() % 376);
                inscan_i64 ta = inscan_stream_turn(v, g, sp, &resta);
                int64_t tr = ref_stream_turn(v, g, sp, &restr);
                CHECK(ta == tr && resta == restr, "stream_turn iter=%d mismatch: (%lld,%lld) vs (%lld,%lld)",
                      iter, (long long)ta, (long long)resta, (long long)tr, (long long)restr);
        }
}

/* ---- Microbenchmark ---- */
static uint64_t now_ns(void)
{
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void run_benchmarks(void)
{
        const int N = 1000000;
        volatile int64_t sink = 0;

        /* 1. evdev_batch (typical 3-event 8kHz mouse report: REL_X, REL_Y, SYN_REPORT) */
        struct inscan_event mev[3] = { {100, 1000, 2, 0, 7}, {100, 1000, 2, 1, -3}, {100, 1000, 0, 0, 0} };
        struct inscan_batch b;
        uint64_t t0 = now_ns();
        for (int i = 0; i < N; i++) { sink += inscan_evdev_batch(mev, 3, 0, &b); }
        uint64_t t1 = now_ns();
        for (int i = 0; i < N; i++) { sink += ref_evdev_batch(mev, 3, 0, &b); }
        uint64_t t2 = now_ns();
        printf("bench evdev_batch(3):   asm=%.2f ns/op  ref=%.2f ns/op  speedup=%.2fx\n",
               (double)(t1 - t0) / N, (double)(t2 - t1) / N, (double)(t2 - t1) / (double)(t1 - t0));

        /* 2. predict_lead (12-sample trajectory) */
        struct inscan_predict p;
        memset(&p, 0, sizeof(p));
        p.count = 12;
        for (int i = 0; i < 12; i++) {
                p.x[i] = 100000 + (int64_t)i * 3500;
                p.y[i] = 100000 + (int64_t)i * 1700;
                p.when[i] = 1000000000ull + (uint64_t)i * 2000000ull;
        }
        const int NP = 300000;
        t0 = now_ns();
        for (int i = 0; i < NP; i++) { inscan_predict_lead(&p, 16666666ull, 100); sink += p.lead_x; }
        t1 = now_ns();
        for (int i = 0; i < NP; i++) { ref_predict_lead(&p, 16666666ull, 100); sink += p.lead_x; }
        t2 = now_ns();
        printf("bench predict_lead(12): asm=%.2f ns/op  ref=%.2f ns/op  speedup=%.2fx\n",
               (double)(t1 - t0) / NP, (double)(t2 - t1) / NP, (double)(t2 - t1) / (double)(t1 - t0));

        /* 3. stream_gain */
        t0 = now_ns();
        for (int i = 0; i < N; i++) { sink += inscan_stream_gain((uint64_t)(1200 + (i & 4095))); }
        t1 = now_ns();
        for (int i = 0; i < N; i++) { sink += ref_stream_gain((uint64_t)(1200 + (i & 4095))); }
        t2 = now_ns();
        printf("bench stream_gain:      asm=%.2f ns/op  ref=%.2f ns/op  speedup=%.2fx\n",
               (double)(t1 - t0) / N, (double)(t2 - t1) / N, (double)(t2 - t1) / (double)(t1 - t0));

        /* 4. wheel_stream + stream_rate + stream_turn */
        struct inscan_wheel_dev wd = {0};
        inscan_u32 hn = 6;
        uint64_t hist[6] = {1000000, 15000000, 30000000, 45000000, 60000000, 75000000};
        inscan_i64 rest = 0;
        t0 = now_ns();
        for (int i = 0; i < N; i++) {
                uint64_t ts = 75000000ull + (uint64_t)i * 10000000ull;
                sink += inscan_wheel_stream(&wd, &hn, ts);
                uint64_t r = inscan_stream_rate(hist, hn + 6);
                sink += inscan_stream_turn(120, 2048 + (int64_t)(r & 2047), 100, &rest);
        }
        t1 = now_ns();
        memset(&wd, 0, sizeof(wd)); hn = 6; rest = 0;
        for (int i = 0; i < N; i++) {
                uint64_t ts = 75000000ull + (uint64_t)i * 10000000ull;
                sink += ref_wheel_stream(&wd, &hn, ts);
                uint64_t r = ref_stream_rate(hist, hn + 6);
                sink += ref_stream_turn(120, 2048 + (int64_t)(r & 2047), 100, &rest);
        }
        t2 = now_ns();
        printf("bench wheel_pipeline:   asm=%.2f ns/op  ref=%.2f ns/op  speedup=%.2fx\n",
               (double)(t1 - t0) / N, (double)(t2 - t1) / N, (double)(t2 - t1) / (double)(t1 - t0));
        (void)sink;
}

int main(void)
{
        test_null_and_edges();
        test_guard_pages();
        test_differential_fuzz();
        printf("PASS: %llu checks, 0 failures\n", (unsigned long long)g_checks);
        run_benchmarks();
        return g_fails ? 1 : 0;
}
