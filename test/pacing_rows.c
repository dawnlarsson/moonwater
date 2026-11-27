/*
        Differential C-reference parity, security, guard-page, fuzz & throughput/size
        benchmark for src/canvas/compositor/pacing_rows.asm.c (arm64, x86-64, riscv64 pure-ASM).

        Build & run:
          cc -O2 -Wall -Werror test/pacing_rows.c -o /tmp/mw_pacing_rows && /tmp/mw_pacing_rows
          cc -O2 -fsanitize=address,undefined -Wall -Werror test/pacing_rows.c -o /tmp/mw_pacing_rows_asan && /tmp/mw_pacing_rows_asan
*/
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define COMPOSITOR_PACING_ROWS_ASM_IMPLEMENTATION
#include "../src/canvas/compositor/pacing_rows.asm.c"

/* ── -O2 C reference implementations (exact Moonwater semantics, noinline for size/speed) ── */

__attribute__((noinline))
static prows_i32 ref_next(const prows_u64 *words, prows_u32 nwords, prows_u32 limit, prows_i32 from)
{
        if (!words) return -1;
        if (from < 0) from = 0;
        if ((prows_u32)from >= limit) return -1;
        prows_u32 wi = (prows_u32)from >> 6;
        if (wi >= nwords) return -1;
        prows_u64 w = words[wi] & (~0ULL << ((prows_u32)from & 63u));
        for (;;) {
                if (w) {
                        prows_u32 idx = (wi << 6) + (prows_u32)__builtin_ctzll(w);
                        return idx < limit ? (prows_i32)idx : -1;
                }
                if (++wi >= nwords) return -1;
                w = words[wi];
        }
}

__attribute__((noinline))
static prows_i32 ref_any(const prows_u64 *words, prows_u32 nwords, prows_u32 last_word)
{
        if (!words) return 0;
        prows_u64 acc = (last_word != PROWS_NO_LAST && last_word >= nwords) ? words[last_word] : 0;
        for (prows_u32 w = 0; w < nwords; w++)
                acc |= words[w];
        return acc != 0;
}

__attribute__((noinline))
static void ref_none(prows_u64 *words, prows_u32 nwords, prows_u32 last_word)
{
        if (!words) return;
        for (prows_u32 w = 0; w < nwords; w++)
                words[w] = 0;
        if (last_word != PROWS_NO_LAST && last_word >= nwords)
                words[last_word] = 0;
}

__attribute__((noinline))
static void ref_or(prows_u64 *into, const prows_u64 *with, prows_u32 nwords, prows_u32 last_word)
{
        if (!into || !with) return;
        for (prows_u32 w = 0; w < nwords; w++)
                into[w] |= with[w];
        if (last_word != PROWS_NO_LAST && last_word >= nwords)
                into[last_word] |= with[last_word];
}

__attribute__((noinline))
static void ref_and_not(prows_u64 *into, const prows_u64 *without, prows_u32 nwords, prows_u32 last_word)
{
        if (!into || !without) return;
        for (prows_u32 w = 0; w < nwords; w++)
                into[w] &= ~without[w];
        if (last_word != PROWS_NO_LAST && last_word >= nwords)
                into[last_word] &= ~without[last_word];
}

__attribute__((noinline))
static void ref_take(prows_u64 *into, prows_u64 *from, prows_u32 nwords, prows_u32 last_word)
{
        if (!into || !from) return;
        for (prows_u32 w = 0; w < nwords; w++)
                into[w] = from[w] ? __atomic_exchange_n(&from[w], 0ULL, __ATOMIC_ACQ_REL) : 0;
        if (last_word != PROWS_NO_LAST && last_word >= nwords)
                into[last_word] = from[last_word] ? __atomic_exchange_n(&from[last_word], 0ULL, __ATOMIC_ACQ_REL) : 0;
}

__attribute__((noinline))
static prows_i64 ref_door_next(prows_u64 *words, prows_u32 nwords, prows_u32 last_word)
{
        if (!words) return -1;
        prows_u32 walked = (last_word != PROWS_NO_LAST && last_word >= nwords) ? nwords + 1 : nwords;
        for (prows_u32 k = 0; k < walked; k++) {
                prows_u32 w = k < nwords ? k : last_word;
                prows_u64 word = __atomic_load_n(&words[w], __ATOMIC_ACQUIRE);
                while (word) {
                        prows_u64 bit = word & (0ULL - word);
                        if (__atomic_fetch_and(&words[w], ~bit, __ATOMIC_ACQ_REL) & bit)
                                return (prows_i64)w * 64 + (prows_i64)__builtin_ctzll(bit);
                        word = __atomic_load_n(&words[w], __ATOMIC_ACQUIRE);
                }
        }
        return -1;
}

__attribute__((noinline))
static prows_u64 ref_metric_worst(const prows_u64 *ring, prows_u32 ring_size, prows_u32 at, prows_u32 count, prows_u32 last)
{
        if (!ring || !ring_size) return 0;
        prows_u64 worst = 0;
        prows_u32 idx = at % ring_size;
        for (prows_u32 back = 1; back <= last && back <= count && back <= ring_size; back++) {
                idx = idx ? idx - 1 : ring_size - 1;
                prows_u64 t = ring[idx];
                if (t > worst) worst = t;
        }
        return worst;
}

__attribute__((noinline))
static prows_u32 ref_popcount_mask(prows_u32 bits, prows_u32 count)
{
        if (!count) return 0;
        prows_u32 mask = count >= 32u ? ~0u : (1u << count) - 1u;
        prows_u32 v = bits & mask;
        v -= (v >> 1) & 0x55555555u;
        v = (v & 0x33333333u) + ((v >> 2) & 0x33333333u);
        return (((v + (v >> 4)) & 0x0f0f0f0fu) * 0x01010101u) >> 24;
}

__attribute__((noinline))
static prows_u64 ref_frame_dated(prows_u64 last, prows_u64 period, prows_u64 stamp)
{
        prows_u64 due = last + period;
        return last && period && stamp >= due && stamp - due < period ? due : stamp;
}

__attribute__((noinline))
static prows_u64 ref_present_due(prows_u64 presented, prows_u64 period, prows_u64 vblank, prows_u64 lead, prows_u64 now)
{
        prows_u64 due = now - presented < period ? presented + period : now, late, least;
        if (!lead || !period || lead >= period || !vblank || now < vblank || now - vblank >= period)
                return due;
        late = vblank + period - lead;
        late = now > late ? now : late;
        least = presented + period / 2;
        return late > least ? late : least;
}

__attribute__((noinline))
static prows_u64 ref_lead_of(prows_u64 worst, prows_u32 timed, prows_u64 widen, prows_u64 period)
{
        prows_u64 lead = worst + worst / 2 + PROWS_LEAD_SPARE + widen;
        if (timed < 4 || !period || widen >= period || lead >= period)
                return 0;
        return lead;
}

__attribute__((noinline))
static prows_u64 ref_cadence_slot(prows_u64 due, prows_u64 flip_when, prows_u64 period, prows_u64 lead, prows_u32 cadence_k)
{
        if (cadence_k < 2 || !flip_when || !period)
                return due;
        prows_u64 slot = flip_when + (prows_u64)cadence_k * period - (lead && lead < period ? lead : period / 2);
        return slot > due ? slot : due;
}

__attribute__((noinline))
static void ref_late_landed(struct prows_late_state *s, prows_u64 when, prows_u64 period)
{
        if (!s) return;
        struct prows_lead_step *step = s->aimed_direct ? &s->direct : &s->composed;
        prows_u32 need = step->calm_need ? step->calm_need : PROWS_LEAD_CALM;

        if (s->landed && when > s->landed && when - s->landed >= PROWS_LEAD_IDLE_NS)
                s->warm = 0;
        s->landed = when;
        if (s->aim && when > s->aim && when - s->aim >= period / 2) {
                s->missed++;
                if (s->warm >= PROWS_LEAD_WARM) {
                        step->calm = 0;
                        if (!step->widen)
                                step->widen = period / 4;
                        else {
                                step->widen = period;
                                prows_u32 dbl = need * 2u;
                                step->calm_need = dbl < PROWS_LEAD_CALM_MOST ? dbl : PROWS_LEAD_CALM_MOST;
                        }
                }
        } else if (step->widen && ++step->calm >= need) {
                step->calm = 0;
                if (step->widen >= period)
                        step->widen = period / 4;
                else
                        step->widen = 0, step->calm_need = 0;
        }
        s->warm += s->warm < PROWS_LEAD_WARM;
        s->aim = 0;
}

/* ── PRNG & helpers ── */

static uint64_t rng_s = 0x9e3779b97f4a7c15ULL;
static uint64_t rng64(void)
{
        uint64_t x = rng_s;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        return rng_s = x;
}

static uint64_t now_ns(void)
{
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static unsigned long g_checks = 0;
static unsigned long g_fails  = 0;

#define CHECK(cond, fmt, ...) do { \
        g_checks++; \
        if (!(cond)) { \
                fprintf(stderr, "FAIL line %d: " fmt "\n", __LINE__, ##__VA_ARGS__); \
                g_fails++; \
                if (g_fails > 20) exit(1); \
        } \
} while (0)

/* ── Edge & Security Tests ── */

static void test_edges_and_security(void)
{
        /* NULL safety across all pointer-taking functions */
        CHECK(prows_next(NULL, 4, 256, 0) == -1, "next NULL");
        CHECK(prows_any(NULL, 4, 3) == 0, "any NULL");
        prows_none(NULL, 4, 3);
        prows_u64 dummy[4] = {1, 2, 3, 4};
        prows_or(NULL, dummy, 4, 3);
        prows_or(dummy, NULL, 4, 3);
        prows_and_not(NULL, dummy, 4, 3);
        prows_and_not(dummy, NULL, 4, 3);
        prows_take(NULL, dummy, 4, 3);
        prows_take(dummy, NULL, 4, 3);
        CHECK(prows_door_next(NULL, 4, 3) == -1, "door_next NULL");
        CHECK(prows_metric_worst(NULL, 128, 0, 10, 10) == 0, "metric_worst NULL");
        CHECK(prows_metric_worst(dummy, 0, 0, 10, 10) == 0, "metric_worst ring_size=0");
        prows_late_landed(NULL, 1000, 16666667ULL);

        /* prows_next: empty, full, single-bit, boundary-bit, negative from, from >= limit, limit > nwords*64 */
        prows_u64 bits[8];
        memset(bits, 0, sizeof(bits));
        CHECK(prows_next(bits, 8, 512, 0) == -1, "next empty");
        CHECK(prows_next(bits, 0, 512, 0) == -1, "next nwords=0");
        CHECK(prows_next(bits, 8, 0, 0) == -1, "next limit=0");

        memset(bits, 0xff, sizeof(bits));
        CHECK(prows_next(bits, 8, 512, -1) == 0, "next full from=-1");
        CHECK(prows_next(bits, 8, 512, INT32_MIN) == 0, "next full from=INT32_MIN");
        CHECK(prows_next(bits, 8, 512, 511) == 511, "next full from=511");
        CHECK(prows_next(bits, 8, 512, 512) == -1, "next full from=512");
        CHECK(prows_next(bits, 8, 512, 999999) == -1, "next full from=999999");
        CHECK(prows_next(bits, 2, 512, 128) == -1, "next limit>nwords*64");
        CHECK(prows_next(bits, 2, 100, 99) == 99, "next partial word hit");
        CHECK(prows_next(bits, 2, 100, 100) == -1, "next partial word miss");

        /* Single bit at every boundary 0..255 */
        for (int b = 0; b < 256; b++) {
                memset(bits, 0, sizeof(bits));
                bits[b >> 6] = 1ULL << (b & 63);
                CHECK(prows_next(bits, 4, 256, 0) == b, "single bit %d from 0", b);
                CHECK(prows_next(bits, 4, 256, b) == b, "single bit %d from b", b);
                CHECK(prows_next(bits, 4, 256, b + 1) == -1, "single bit %d from b+1", b);
                CHECK(prows_next(bits, 4, (prows_u32)b, 0) == -1, "single bit %d limit=b", b);
                CHECK(prows_next(bits, 4, (prows_u32)(b + 1), 0) == b, "single bit %d limit=b+1", b);
        }

        /* prows_door_next: drain all 64 bits of word 0 + word 2 + misc slot word 7 */
        memset(bits, 0, sizeof(bits));
        bits[0] = ~0ULL;
        bits[2] = (1ULL << 0) | (1ULL << 31) | (1ULL << 63);
        bits[7] = (1ULL << 5) | (1ULL << 62);
        for (int b = 0; b < 64; b++)
                CHECK(prows_door_next(bits, 3, 7) == b, "door drain w0 bit %d", b);
        CHECK(prows_door_next(bits, 3, 7) == 128, "door drain w2 bit 0");
        CHECK(prows_door_next(bits, 3, 7) == 159, "door drain w2 bit 31");
        CHECK(prows_door_next(bits, 3, 7) == 191, "door drain w2 bit 63");
        CHECK(prows_door_next(bits, 3, 7) == 7 * 64 + 5, "door drain misc bit 5");
        CHECK(prows_door_next(bits, 3, 7) == 7 * 64 + 62, "door drain misc bit 62");
        CHECK(prows_door_next(bits, 3, 7) == -1, "door drain empty");
        CHECK(prows_any(bits, 3, 7) == 0, "door drain left all zero");

        /* prows_take: last_word < nwords must NOT double-exchange! */
        prows_u64 src[4] = {0x11, 0x22, 0x33, 0x44}, dst[4] = {0};
        prows_take(dst, src, 4, 3);
        CHECK(dst[0] == 0x11 && dst[1] == 0x22 && dst[2] == 0x33 && dst[3] == 0x44, "take no double-visit");
        CHECK(src[0] == 0 && src[1] == 0 && src[2] == 0 && src[3] == 0, "take cleared src");

        /* prows_popcount_mask edges */
        prows_u32 counts[] = {0, 1, 2, 7, 15, 16, 31, 32, 33, 64, 0xffffffffu};
        prows_u32 masks[]  = {0u, 1u, 0x80000000u, 0x55555555u, 0xaaaaaaaau, 0x0f0f0f0fu, 0xffffffffu};
        for (size_t i = 0; i < sizeof(counts)/sizeof(counts[0]); i++)
                for (size_t j = 0; j < sizeof(masks)/sizeof(masks[0]); j++)
                        CHECK(prows_popcount_mask(masks[j], counts[i]) == ref_popcount_mask(masks[j], counts[i]),
                              "popcount_mask bits=%x count=%u", masks[j], counts[i]);

        /* prows_frame_dated edges including zero & wraparound */
        prows_u64 fd_cases[][3] = {
                {0, 16666667ULL, 1000}, {1000, 0, 2000},
                {1000, 100, 1099}, {1000, 100, 1100}, {1000, 100, 1199}, {1000, 100, 1200},
                {UINT64_MAX - 50, 100, 10}, {UINT64_MAX - 50, 100, 50}, {UINT64_MAX - 50, 100, 149}, {UINT64_MAX - 50, 100, 150},
        };
        for (size_t i = 0; i < sizeof(fd_cases)/sizeof(fd_cases[0]); i++)
                CHECK(prows_frame_dated(fd_cases[i][0], fd_cases[i][1], fd_cases[i][2]) ==
                      ref_frame_dated(fd_cases[i][0], fd_cases[i][1], fd_cases[i][2]), "frame_dated case %zu", i);

        /* prows_late_landed full state-machine lifecycle (warm-up -> miss1 -> miss2 -> calm step-back -> idle reset) */
        struct prows_late_state sa, sr;
        memset(&sa, 0, sizeof(sa));
        memset(&sr, 0, sizeof(sr));
        prows_u64 period = 16666667ULL;
        prows_u64 t = 1000000000ULL;
        for (int step = 0; step < 600; step++) {
                t += (step == 350) ? PROWS_LEAD_IDLE_NS + 1000ULL : period;
                sa.aimed_direct = sr.aimed_direct = (step >= 300 && step < 450) ? 1 : 0;
                /* Inject misses during warm-up (<8), then at step 12, 13, 14, and after idle */
                if (step == 2 || step == 12 || step == 13 || step == 14 || step == 352 || step == 362 || step == 363) {
                        sa.aim = sr.aim = t - period;
                } else {
                        sa.aim = sr.aim = t - 1000ULL;
                }
                prows_late_landed(&sa, t, period);
                ref_late_landed(&sr, t, period);
                CHECK(memcmp(&sa, &sr, sizeof(sa)) == 0, "late_landed lifecycle step %d", step);
        }
}

/* ── mmap Guard-Page Security Tests (PROT_NONE before AND after) ── */

static void test_guard_pages(void)
{
        long ps = sysconf(_SC_PAGESIZE);
        size_t page = (size_t)ps;
        uint8_t *region = mmap(NULL, 3 * page, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
        CHECK(region != MAP_FAILED, "mmap guard region");
        CHECK(mprotect(region + page, page, PROT_READ | PROT_WRITE) == 0, "mprotect rw");

        uint8_t *rw_lo = region + page;
        uint8_t *rw_hi = region + 2 * page;

        /* 1. Bitset flush against upper guard page (4 words = 32 bytes at end of RW page) */
        prows_u64 *b_hi = (prows_u64 *)(rw_hi - 4 * sizeof(prows_u64));
        prows_u64 *b_lo = (prows_u64 *)rw_lo;
        for (int i = 0; i < 4; i++) { b_hi[i] = 0x8000000000000001ULL; b_lo[i] = 0x0f0f0f0f0f0f0f0fULL; }

        /* Security: limit > nwords*64 must NOT cross into PROT_NONE! */
        CHECK(prows_next(b_hi, 4, 10000, 0) == 0, "guard hi next 0");
        CHECK(prows_next(b_hi, 4, 10000, 255) == 255, "guard hi next 255");
        CHECK(prows_next(b_hi, 4, 10000, 256) == -1, "guard hi next 256 (must not touch guard)");
        CHECK(prows_next(b_lo, 4, 10000, -100) == 0, "guard lo next -100");
        CHECK(prows_any(b_hi, 4, 3) == 1, "guard hi any");
        CHECK(prows_any(b_hi, 4, PROWS_NO_LAST) == 1, "guard hi any no_last");
        prows_or(b_hi, b_lo, 4, 3);
        prows_and_not(b_hi, b_lo, 4, 3);
        prows_take(b_lo, b_hi, 4, 3);
        CHECK(prows_any(b_hi, 4, 3) == 0, "guard hi taken");
        CHECK(prows_door_next(b_lo, 4, 3) == 63, "guard lo door_next");
        prows_none(b_lo, 4, 3);
        CHECK(prows_any(b_lo, 4, 3) == 0, "guard lo none");

        /* 2. Metric ring (128 words = 1024 bytes) flush against upper & lower guard pages */
        prows_u64 *r_hi = (prows_u64 *)(rw_hi - 128 * sizeof(prows_u64));
        prows_u64 *r_lo = (prows_u64 *)rw_lo;
        for (int i = 0; i < 128; i++) { r_hi[i] = (prows_u64)(i + 1) * 1000ULL; r_lo[i] = (prows_u64)(128 - i) * 1000ULL; }
        CHECK(prows_metric_worst(r_hi, 128, 0, 500, 500) == 128000ULL, "guard hi metric_worst");
        CHECK(prows_metric_worst(r_hi, 128, 0xffffffffu, 128, 128) == 128000ULL, "guard hi metric_worst huge at");
        CHECK(prows_metric_worst(r_lo, 128, 1, 128, 128) == 128000ULL, "guard lo metric_worst");

        /* 3. struct prows_late_state (80 bytes) flush against upper & lower guard pages */
        struct prows_late_state *ls_hi = (struct prows_late_state *)(rw_hi - sizeof(struct prows_late_state));
        struct prows_late_state *ls_lo = (struct prows_late_state *)rw_lo;
        memset(ls_hi, 0, sizeof(*ls_hi));
        memset(ls_lo, 0, sizeof(*ls_lo));
        ls_hi->warm = 8; ls_hi->aim = 1000; ls_hi->aimed_direct = 1;
        ls_lo->warm = 8; ls_lo->aim = 1000; ls_lo->aimed_direct = 0;
        prows_late_landed(ls_hi, 20000000ULL, 16666667ULL);
        prows_late_landed(ls_lo, 20000000ULL, 16666667ULL);
        CHECK(ls_hi->missed == 1 && ls_hi->direct.widen == 16666667ULL / 4, "guard hi late_landed");
        CHECK(ls_lo->missed == 1 && ls_lo->composed.widen == 16666667ULL / 4, "guard lo late_landed");

        munmap(region, 3 * page);
}

/* ── Randomized Differential Fuzzing ── */

static void test_differential_fuzz(void)
{
        prows_u64 a[16], b[16], a_ref[16], b_ref[16], ring[128];

        for (int iter = 0; iter < 50000; iter++) {
                /* 1. Bitset fuzz */
                prows_u32 nw = (prows_u32)(rng64() % 12);
                prows_u32 lw = (rng64() & 3) ? (nw + (prows_u32)(rng64() % (16 - nw))) :
                               ((rng64() & 1) ? PROWS_NO_LAST : (nw ? nw - 1 : 0));
                if (lw != PROWS_NO_LAST && lw >= 16) lw = 15;
                for (int i = 0; i < 16; i++) {
                        prows_u64 v = (rng64() & 3) ? 0ULL : rng64();
                        prows_u64 w = (rng64() & 3) ? 0ULL : rng64();
                        a[i] = a_ref[i] = v;
                        b[i] = b_ref[i] = w;
                }
                prows_u32 lim = (prows_u32)(rng64() % (nw * 64 + 20));
                prows_i32 frm = (prows_i32)((int64_t)(rng64() % (nw * 64 + 40)) - 10);
                CHECK(prows_next(a, nw, lim, frm) == ref_next(a_ref, nw, lim, frm), "fuzz next");
                CHECK(prows_any(a, nw, lw) == ref_any(a_ref, nw, lw), "fuzz any");

                prows_or(a, b, nw, lw);
                ref_or(a_ref, b_ref, nw, lw);
                CHECK(memcmp(a, a_ref, sizeof(a)) == 0, "fuzz or");

                prows_and_not(a, b, nw, lw);
                ref_and_not(a_ref, b_ref, nw, lw);
                CHECK(memcmp(a, a_ref, sizeof(a)) == 0, "fuzz and_not");

                prows_take(b, a, nw, lw);
                ref_take(b_ref, a_ref, nw, lw);
                CHECK(memcmp(a, a_ref, sizeof(a)) == 0 && memcmp(b, b_ref, sizeof(b)) == 0, "fuzz take");

                prows_i64 d1 = prows_door_next(b, nw, lw);
                prows_i64 d2 = ref_door_next(b_ref, nw, lw);
                CHECK(d1 == d2 && memcmp(b, b_ref, sizeof(b)) == 0, "fuzz door_next");

                prows_none(b, nw, lw);
                ref_none(b_ref, nw, lw);
                CHECK(memcmp(b, b_ref, sizeof(b)) == 0, "fuzz none");

                /* 2. Pacing & metric fuzz */
                prows_u32 bits32 = (prows_u32)rng64(), cnt32 = (prows_u32)(rng64() % 40);
                CHECK(prows_popcount_mask(bits32, cnt32) == ref_popcount_mask(bits32, cnt32), "fuzz popcount");

                prows_u64 p = (rng64() & 7) ? (rng64() % 50000000ULL) : 0ULL;
                prows_u64 last = (rng64() & 7) ? rng64() : 0ULL;
                prows_u64 stamp = (rng64() & 1) ? (last + p + (rng64() % (p * 2 + 1))) : rng64();
                CHECK(prows_frame_dated(last, p, stamp) == ref_frame_dated(last, p, stamp), "fuzz frame_dated");

                prows_u64 presented = rng64() % 1000000000ULL;
                prows_u64 vblank = (rng64() & 7) ? (presented + (rng64() % (p + 10000ULL))) : 0ULL;
                prows_u64 lead = (rng64() & 7) ? (rng64() % (p + 2000000ULL)) : 0ULL;
                prows_u64 now = vblank + (rng64() % (p * 2 + 10000ULL)) - (rng64() & 1 ? 0 : 5000ULL);
                CHECK(prows_present_due(presented, p, vblank, lead, now) ==
                      ref_present_due(presented, p, vblank, lead, now), "fuzz present_due");

                prows_u64 worst = rng64() % 30000000ULL;
                prows_u32 timed = (prows_u32)(rng64() % 40);
                prows_u64 widen = (rng64() & 3) ? (p / 4) : (rng64() % (p + 1000000ULL));
                CHECK(prows_lead_of(worst, timed, widen, p) == ref_lead_of(worst, timed, widen, p), "fuzz lead_of");

                prows_u32 cad_k = (prows_u32)(rng64() % 6);
                CHECK(prows_cadence_slot(now, vblank, p, lead, cad_k) ==
                      ref_cadence_slot(now, vblank, p, lead, cad_k), "fuzz cadence_slot");

                /* 3. late_landed fuzz */
                struct prows_late_state ls1, ls2;
                ls1.early = (prows_i32)(rng64() & 1);
                ls1.aimed_direct = (prows_i32)(rng64() & 1);
                ls1.composed.calm = (prows_u32)(rng64() % 8000);
                ls1.composed.calm_need = (rng64() & 1) ? 0u : (prows_u32)(rng64() % 9000);
                ls1.composed.widen = (rng64() % 3 == 0) ? 0ULL : ((rng64() & 1) ? p / 4 : p);
                ls1.direct.calm = (prows_u32)(rng64() % 8000);
                ls1.direct.calm_need = (rng64() & 1) ? 0u : (prows_u32)(rng64() % 9000);
                ls1.direct.widen = (rng64() % 3 == 0) ? 0ULL : ((rng64() & 1) ? p / 4 : p);
                ls1.warm = (prows_u32)(rng64() % 12);
                ls1._pad = 0;
                ls1.landed = (rng64() & 7) ? 1000000000ULL : 0ULL;
                ls1.aim = (rng64() & 3) ? (1000000000ULL + (rng64() % 30000000ULL)) : 0ULL;
                ls1.made = rng64() % 1000;
                ls1.missed = rng64() % 1000;
                ls2 = ls1;
                prows_u64 when = 1000000000ULL + (rng64() % 400000000ULL);
                prows_late_landed(&ls1, when, p);
                ref_late_landed(&ls2, when, p);
                CHECK(memcmp(&ls1, &ls2, sizeof(ls1)) == 0, "fuzz late_landed");
        }

        /* 4. metric_worst fuzz (power-of-2 & non-power-of-2 ring sizes) */
        for (int iter = 0; iter < 5000; iter++) {
                prows_u32 rsz = (iter & 1) ? 128u : (prows_u32)(1u + (rng64() % 128u));
                for (prows_u32 i = 0; i < rsz; i++) ring[i] = rng64();
                prows_u32 at = (prows_u32)rng64();
                prows_u32 cnt = (prows_u32)(rng64() % (rsz + 40));
                prows_u32 lst = (prows_u32)(rng64() % (rsz + 40));
                CHECK(prows_metric_worst(ring, rsz, at, cnt, lst) ==
                      ref_metric_worst(ring, rsz, at, cnt, lst), "fuzz metric_worst");
        }
}

/* ── Throughput Benchmark ── */

static void run_benchmarks(void)
{
        const int N = 2000000;
        prows_u64 bw[8] = {0x0000000000000000ULL, 0x0000000000100000ULL, 0, 0, 0, 0, 0, 0x10ULL};
        prows_u64 ring[128];
        for (int i = 0; i < 128; i++) ring[i] = (prows_u64)(i * 73 % 128) * 50000ULL + 100000ULL;

        volatile uint64_t sink = 0;
        uint64_t t0, t1, t2;

        /* 1. prows_next */
        t0 = now_ns();
        for (int i = 0; i < N; i++) sink += (uint64_t)prows_next(bw, 4, 256, i & 63);
        t1 = now_ns();
        for (int i = 0; i < N; i++) sink += (uint64_t)ref_next(bw, 4, 256, i & 63);
        t2 = now_ns();
        printf("bench prows_next        : asm=%5.2f ns/op  ref=%5.2f ns/op  speedup=%.2fx\n",
               (double)(t1 - t0) / N, (double)(t2 - t1) / N, (double)(t2 - t1) / (double)(t1 - t0));

        /* 2. prows_metric_worst (32 frames in 128-slot ring) */
        t0 = now_ns();
        for (int i = 0; i < N; i++) sink += prows_metric_worst(ring, 128, (prows_u32)(i & 127), 128, 32);
        t1 = now_ns();
        for (int i = 0; i < N; i++) sink += ref_metric_worst(ring, 128, (prows_u32)(i & 127), 128, 32);
        t2 = now_ns();
        printf("bench prows_metric_worst: asm=%5.2f ns/op  ref=%5.2f ns/op  speedup=%.2fx\n",
               (double)(t1 - t0) / N, (double)(t2 - t1) / N, (double)(t2 - t1) / (double)(t1 - t0));

        /* 3. prows_present_due */
        t0 = now_ns();
        for (int i = 0; i < N; i++) sink += prows_present_due(1000000000ULL, 16666667ULL, 1005000000ULL, 3000000ULL, 1010000000ULL + (prows_u64)(i & 1023));
        t1 = now_ns();
        for (int i = 0; i < N; i++) sink += ref_present_due(1000000000ULL, 16666667ULL, 1005000000ULL, 3000000ULL, 1010000000ULL + (prows_u64)(i & 1023));
        t2 = now_ns();
        printf("bench prows_present_due : asm=%5.2f ns/op  ref=%5.2f ns/op  speedup=%.2fx\n",
               (double)(t1 - t0) / N, (double)(t2 - t1) / N, (double)(t2 - t1) / (double)(t1 - t0));

        /* 4. prows_late_landed */
        struct prows_late_state ls = {0};
        ls.warm = 8;
        t0 = now_ns();
        for (int i = 0; i < N; i++) { ls.aim = 1000000000ULL; prows_late_landed(&ls, 1001000000ULL + (prows_u64)(i & 1023), 16666667ULL); }
        t1 = now_ns();
        for (int i = 0; i < N; i++) { ls.aim = 1000000000ULL; ref_late_landed(&ls, 1001000000ULL + (prows_u64)(i & 1023), 16666667ULL); }
        t2 = now_ns();
        printf("bench prows_late_landed : asm=%5.2f ns/op  ref=%5.2f ns/op  speedup=%.2fx\n",
               (double)(t1 - t0) / N, (double)(t2 - t1) / N, (double)(t2 - t1) / (double)(t1 - t0));

        /* 5. prows_door_next */
        prows_u64 dq[4] = {0};
        t0 = now_ns();
        for (int i = 0; i < N; i++) { dq[0] = 1ULL << (i & 63); sink += (uint64_t)prows_door_next(dq, 2, 3); }
        t1 = now_ns();
        for (int i = 0; i < N; i++) { dq[0] = 1ULL << (i & 63); sink += (uint64_t)ref_door_next(dq, 2, 3); }
        t2 = now_ns();
        printf("bench prows_door_next   : asm=%5.2f ns/op  ref=%5.2f ns/op  speedup=%.2fx\n",
               (double)(t1 - t0) / N, (double)(t2 - t1) / N, (double)(t2 - t1) / (double)(t1 - t0));
        (void)sink;
}

int main(void)
{
        test_edges_and_security();
        test_guard_pages();
        test_differential_fuzz();
        printf("PASS pacing_rows: %lu checks, 0 failures\n", g_checks);
        run_benchmarks();
        return g_fails ? 1 : 0;
}
