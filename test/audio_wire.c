/*
        Exhaustive security, edge-case, guard-page, differential-fuzz,
        and speed/size proof for src/audio/wire.asm.c (arm64, x86-64, riscv64).

        Build & run:
          cc -O2 -Wall -Werror test/audio_wire.c -o /tmp/t_awire && /tmp/t_awire
          cc -O2 -fsanitize=address,undefined -Wall -Werror test/audio_wire.c -o /tmp/t_awire_asan && /tmp/t_awire_asan
*/
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>

#define AUDIO_WIRE_ASM_IMPLEMENTATION
#include "../src/audio/wire.asm.c"

/* ── -O2 C Reference Implementations (exact parity with wire.c & pulse.c) ── */

static __attribute__((noinline)) aw_u32 ref_aw_be32(const aw_u8 *p) {
        if (!p) return 0;
        return ((aw_u32)p[0] << 24) | ((aw_u32)p[1] << 16) | ((aw_u32)p[2] << 8) | (aw_u32)p[3];
}

static __attribute__((noinline)) void ref_aw_put_be32(aw_u8 *to, aw_u32 v) {
        if (!to) return;
        to[0] = (aw_u8)(v >> 24);
        to[1] = (aw_u8)(v >> 16);
        to[2] = (aw_u8)(v >> 8);
        to[3] = (aw_u8)v;
}

static __attribute__((noinline)) aw_i64 ref_aw_strlen(const char *s) {
        if (!s) return 0;
        const char *p = s;
        while (*p) p++;
        return (aw_i64)(p - s);
}

static __attribute__((noinline)) aw_i32 ref_aw_r_tag(struct aw_r *r, aw_u32 tag) {
        if (!r || r->bad) return 0;
        if (!r->p || r->length <= 0 || r->at < 0 || r->at >= r->length || r->p[r->at] != (aw_u8)tag) {
                r->bad = 1;
                return 0;
        }
        r->at++;
        return 1;
}

static __attribute__((noinline)) aw_u32 ref_aw_r_u8(struct aw_r *r) {
        if (!r || r->bad) return 0;
        if (!r->p || r->length <= 0 || r->at < 0 || r->at >= r->length ||
            r->length - r->at < 2 || r->p[r->at] != 'B') {
                r->bad = 1;
                return 0;
        }
        aw_u32 v = r->p[r->at + 1];
        r->at += 2;
        return v;
}

static __attribute__((noinline)) aw_u32 ref_aw_r_u32(struct aw_r *r) {
        if (!r || r->bad) return 0;
        if (!r->p || r->length <= 0 || r->at < 0 || r->at >= r->length ||
            r->length - r->at < 5 || r->p[r->at] != 'L') {
                r->bad = 1;
                return 0;
        }
        aw_u32 v = ref_aw_be32(r->p + r->at + 1);
        r->at += 5;
        return v;
}

static __attribute__((noinline)) aw_i32 ref_aw_r_bool(struct aw_r *r) {
        if (!r || r->bad) return 0;
        if (!r->p || r->length <= 0 || r->at < 0 || r->at >= r->length ||
            (r->p[r->at] != '1' && r->p[r->at] != '0')) {
                r->bad = 1;
                return 0;
        }
        return r->p[r->at++] == '1';
}

static __attribute__((noinline)) const char *ref_aw_r_string(struct aw_r *r, aw_i64 limit, aw_i32 *isnull) {
        if (isnull) *isnull = 0;
        if (!r || r->bad) return NULL;
        if (!r->p || r->length <= 0 || r->at < 0 || r->at >= r->length) {
                r->bad = 1;
                return NULL;
        }
        if (r->p[r->at] == 'N') {
                r->at++;
                if (isnull) *isnull = 1;
                return NULL;
        }
        if (r->p[r->at] != 't') {
                r->bad = 1;
                return NULL;
        }
        aw_i64 start = r->at + 1;
        aw_i64 avail = r->length - start;
        if (avail <= 0) {
                r->bad = 1;
                return NULL;
        }
        if (limit > 0 && avail - 1 > limit)
                avail = limit + 1;
        const aw_u8 *nul = (const aw_u8 *)memchr(r->p + start, 0, (size_t)avail);
        if (!nul) {
                r->bad = 1;
                return NULL;
        }
        r->at = (aw_i64)(nul - r->p) + 1;
        return (const char *)(r->p + start);
}

static __attribute__((noinline)) void ref_aw_r_sample_spec(struct aw_r *r, aw_i64 *format, aw_i64 *channels, aw_i64 *rate) {
        if (!r || r->bad) return;
        if (!r->p || r->length <= 0 || r->at < 0 || r->at >= r->length ||
            r->length - r->at < 7 || r->p[r->at] != 'a') {
                r->bad = 1;
                return;
        }
        const aw_u8 *q = r->p + r->at;
        r->at += 7;
        if (format) *format = q[1];
        if (channels) *channels = q[2];
        if (rate) *rate = (aw_i64)ref_aw_be32(q + 3);
}

static __attribute__((noinline)) aw_i64 ref_aw_r_channel_map(struct aw_r *r, aw_i8 *map, aw_i64 out_cap, aw_i64 max_ch) {
        if (!r || r->bad) return 0;
        if (!r->p || r->length <= 0 || max_ch < 0 || r->at < 0 || r->at >= r->length ||
            r->length - r->at < 2 || r->p[r->at] != 'm') {
                r->bad = 1;
                return 0;
        }
        aw_i64 n = r->p[r->at + 1];
        if (n > max_ch || r->length - r->at - 2 < n) {
                r->bad = 1;
                return 0;
        }
        const aw_u8 *q = r->p + r->at + 2;
        r->at += 2 + n;
        if (map && out_cap > 0) {
                aw_i64 lim = n < out_cap ? n : out_cap;
                for (aw_i64 i = 0; i < lim; i++)
                        map[i] = (aw_i8)(q[i] < 64 ? q[i] : 63);
        }
        return n;
}

static __attribute__((noinline)) aw_i64 ref_aw_r_cvolume(struct aw_r *r, aw_u64 *volume, aw_i64 max_ch, aw_u64 vol_max) {
        if (!r || r->bad) return 0;
        if (!r->p || r->length <= 0 || max_ch < 0 || r->at < 0 || r->at >= r->length ||
            r->length - r->at < 2 || r->p[r->at] != 'v') {
                r->bad = 1;
                return 0;
        }
        aw_i64 n = r->p[r->at + 1];
        if (n > max_ch || r->length - r->at - 2 < 4 * n) {
                r->bad = 1;
                return 0;
        }
        const aw_u8 *q = r->p + r->at + 2;
        r->at += 2 + 4 * n;
        if (volume) {
                for (aw_i64 i = 0; i < n; i++) {
                        aw_u64 v = ref_aw_be32(q + 4 * i);
                        if (v > vol_max) v = vol_max;
                        volume[i] = v;
                }
        }
        return n;
}

static __attribute__((noinline)) void ref_aw_r_timeval(struct aw_r *r, aw_u64 *seconds, aw_u64 *microseconds) {
        if (!r || r->bad) return;
        if (!r->p || r->length <= 0 || r->at < 0 || r->at >= r->length ||
            r->length - r->at < 9 || r->p[r->at] != 'T') {
                r->bad = 1;
                return;
        }
        const aw_u8 *q = r->p + r->at;
        r->at += 9;
        if (seconds) *seconds = ref_aw_be32(q + 1);
        if (microseconds) *microseconds = ref_aw_be32(q + 5);
}

static __attribute__((noinline)) const aw_u8 *ref_aw_r_arbitrary(struct aw_r *r, aw_u64 n) {
        if (!r || r->bad) return NULL;
        if (!r->p || r->length <= 0 || r->at < 0 || r->at >= r->length ||
            r->length - r->at < 5 || r->p[r->at] != 'x') {
                r->bad = 1;
                return NULL;
        }
        if ((aw_u64)ref_aw_be32(r->p + r->at + 1) != n || (aw_u64)(r->length - r->at - 5) < n) {
                r->bad = 1;
                return NULL;
        }
        const aw_u8 *at = r->p + r->at + 5;
        r->at += 5 + (aw_i64)n;
        return at;
}

static __attribute__((noinline)) void ref_aw_w_raw(struct aw_w *w, const void *data, aw_i64 n) {
        if (!w || w->bad) return;
        if (n < 0 || w->len < 0 || w->cap < w->len) {
                w->bad = 1;
                return;
        }
        if (n == 0) return;
        if (!w->p || !data || n > w->cap - w->len) {
                w->bad = 1;
                return;
        }
        memcpy(w->p + w->len, data, (size_t)n);
        w->len += n;
}

static __attribute__((noinline)) void ref_aw_w_byte(struct aw_w *w, aw_u32 c) {
        if (!w || w->bad) return;
        if (!w->p || w->len < 0 || w->cap <= w->len) {
                w->bad = 1;
                return;
        }
        w->p[w->len++] = (aw_u8)c;
}

static __attribute__((noinline)) void ref_aw_w_be32(struct aw_w *w, aw_u32 v) {
        if (!w || w->bad) return;
        if (!w->p || w->len < 0 || w->cap < w->len || w->cap - w->len < 4) {
                w->bad = 1;
                return;
        }
        ref_aw_put_be32(w->p + w->len, v);
        w->len += 4;
}

static __attribute__((noinline)) void ref_aw_w_tagged32(struct aw_w *w, aw_u32 tag, aw_u32 v) {
        if (!w || w->bad) return;
        if (!w->p || w->len < 0 || w->cap < w->len || w->cap - w->len < 5) {
                w->bad = 1;
                return;
        }
        w->p[w->len] = (aw_u8)tag;
        ref_aw_put_be32(w->p + w->len + 1, v);
        w->len += 5;
}

static __attribute__((noinline)) void ref_aw_w_bool(struct aw_w *w, aw_i32 v) {
        ref_aw_w_byte(w, v ? '1' : '0');
}

static __attribute__((noinline)) void ref_aw_w_u8(struct aw_w *w, aw_u32 v) {
        if (!w || w->bad) return;
        if (!w->p || w->len < 0 || w->cap < w->len || w->cap - w->len < 2) {
                w->bad = 1;
                return;
        }
        w->p[w->len] = 'B';
        w->p[w->len + 1] = (aw_u8)v;
        w->len += 2;
}

static __attribute__((noinline)) void ref_aw_w_64(struct aw_w *w, aw_u32 tag, aw_u64 v) {
        if (!w || w->bad) return;
        if (!w->p || w->len < 0 || w->cap < w->len || w->cap - w->len < 9) {
                w->bad = 1;
                return;
        }
        w->p[w->len] = (aw_u8)tag;
        ref_aw_put_be32(w->p + w->len + 1, (aw_u32)(v >> 32));
        ref_aw_put_be32(w->p + w->len + 5, (aw_u32)v);
        w->len += 9;
}

static __attribute__((noinline)) void ref_aw_w_sample_spec(struct aw_w *w, aw_i64 format, aw_i64 channels, aw_i64 rate) {
        if (!w || w->bad) return;
        if (!w->p || w->len < 0 || w->cap < w->len || w->cap - w->len < 7) {
                w->bad = 1;
                return;
        }
        w->p[w->len] = 'a';
        w->p[w->len + 1] = (aw_u8)format;
        w->p[w->len + 2] = (aw_u8)channels;
        ref_aw_put_be32(w->p + w->len + 3, (aw_u32)rate);
        w->len += 7;
}

static __attribute__((noinline)) void ref_aw_w_channel_map(struct aw_w *w, const aw_i8 *map, aw_i64 channels, aw_i64 map_cap, aw_u32 aux0) {
        if (!w || w->bad) return;
        if (channels < 0 || channels > 255 || (channels > 0 && map_cap > 0 && !map)) {
                w->bad = 1;
                return;
        }
        if (!w->p || w->len < 0 || w->cap < w->len || w->cap - w->len < channels + 2) {
                w->bad = 1;
                return;
        }
        w->p[w->len] = 'm';
        w->p[w->len + 1] = (aw_u8)channels;
        for (aw_i64 i = 0; i < channels; i++)
                w->p[w->len + 2 + i] = (aw_u8)(i < map_cap ? map[i] : (aux0 + (aw_u32)i));
        w->len += channels + 2;
}

static __attribute__((noinline)) void ref_aw_w_string(struct aw_w *w, const char *s) {
        if (!w || w->bad) return;
        if (!w->p || w->len < 0 || w->cap < w->len) {
                w->bad = 1;
                return;
        }
        if (!s) {
                if (w->cap - w->len < 1) { w->bad = 1; return; }
                w->p[w->len++] = 'N';
                return;
        }
        aw_i64 slen = ref_aw_strlen(s) + 1;
        if (w->cap - w->len < slen + 1) {
                w->bad = 1;
                return;
        }
        w->p[w->len] = 't';
        memcpy(w->p + w->len + 1, s, (size_t)slen);
        w->len += slen + 1;
}

static __attribute__((noinline)) void ref_aw_w_cvolume(struct aw_w *w, const aw_u64 *volume, aw_i64 channels) {
        if (!w || w->bad) return;
        if (channels < 0 || channels > 255 || (channels > 0 && !volume)) {
                w->bad = 1;
                return;
        }
        if (!w->p || w->len < 0 || w->cap < w->len || w->cap - w->len < 2 + 4 * channels) {
                w->bad = 1;
                return;
        }
        w->p[w->len] = 'v';
        w->p[w->len + 1] = (aw_u8)channels;
        for (aw_i64 i = 0; i < channels; i++)
                ref_aw_put_be32(w->p + w->len + 2 + 4 * i, (aw_u32)volume[i]);
        w->len += 2 + 4 * channels;
}

/* ── Test Harness ────────────────────────────────────────────────────────── */

static int g_checks = 0, g_fails = 0;
static volatile uint64_t g_sink = 0;

#define CHECK(cond, msg) do { \
        g_checks++; \
        if (!(cond)) { \
                g_fails++; \
                fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg); \
        } \
} while (0)

static uint64_t g_rng = 0x9e3779b97f4a7c15ULL;
static uint64_t xorshift64(void) {
        uint64_t x = g_rng;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        g_rng = x;
        return x;
}

static double now_ns(void) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

/* ── 1. Edge Cases & Security Tests ──────────────────────────────────────── */

static void test_null_and_bad_edges(void) {
        /* NULL pointers */
        CHECK(aw_be32(NULL) == 0, "be32(NULL)");
        aw_put_be32(NULL, 0x12345678u);
        CHECK(aw_strlen(NULL) == 0, "strlen(NULL)");
        CHECK(aw_strlen("") == 0, "strlen empty");
        CHECK(aw_strlen("pulseaudio") == 10, "strlen normal");

        aw_i32 isnull = 7;
        CHECK(aw_r_tag(NULL, 'L') == 0, "r_tag(NULL)");
        CHECK(aw_r_u8(NULL) == 0, "r_u8(NULL)");
        CHECK(aw_r_u32(NULL) == 0, "r_u32(NULL)");
        CHECK(aw_r_bool(NULL) == 0, "r_bool(NULL)");
        CHECK(aw_r_string(NULL, 1024, &isnull) == NULL && isnull == 0, "r_string(NULL) clears isnull");
        CHECK(aw_r_string(NULL, 1024, NULL) == NULL, "r_string(NULL, NULL)");
        aw_r_sample_spec(NULL, NULL, NULL, NULL);
        CHECK(aw_r_channel_map(NULL, NULL, 8, 32) == 0, "r_channel_map(NULL)");
        CHECK(aw_r_cvolume(NULL, NULL, 32, 0x20000) == 0, "r_cvolume(NULL)");
        aw_r_timeval(NULL, NULL, NULL);
        CHECK(aw_r_arbitrary(NULL, 4) == NULL, "r_arbitrary(NULL)");

        aw_w_raw(NULL, "abc", 3);
        aw_w_byte(NULL, 'L');
        aw_w_be32(NULL, 1);
        aw_w_tagged32(NULL, 'L', 1);
        aw_w_bool(NULL, 1);
        aw_w_u8(NULL, 1);
        aw_w_64(NULL, 'R', 1);
        aw_w_sample_spec(NULL, 3, 2, 48000);
        aw_w_channel_map(NULL, NULL, 2, 8, 12);
        aw_w_string(NULL, "hi");
        aw_w_cvolume(NULL, NULL, 2);

        /* NULL buffer inside struct */
        struct aw_r r = { NULL, 100, 0, 0, 0 };
        CHECK(aw_r_u32(&r) == 0 && r.bad == 1, "r->p == NULL sets bad");

        /* Negative at / negative length / overflow at + need */
        aw_u8 buf[32] = { 'L', 0, 0, 0, 42 };
        r = (struct aw_r){ buf, 32, -1, 0, 0 };
        CHECK(aw_r_u32(&r) == 0 && r.bad == 1, "negative r->at sets bad");
        r = (struct aw_r){ buf, -5, 0, 0, 0 };
        CHECK(aw_r_u32(&r) == 0 && r.bad == 1, "negative r->length sets bad");
        r = (struct aw_r){ buf, INT64_MAX, INT64_MAX - 2, 0, 0 };
        CHECK(aw_r_u32(&r) == 0 && r.bad == 1, "at + need near INT64_MAX sets bad");
        r = (struct aw_r){ buf, INT64_MAX, INT64_MAX - 2, 0, 0 };
        CHECK(aw_r_arbitrary(&r, 10) == NULL && r.bad == 1, "arbitrary near INT64_MAX");

        /* 0xffffffff arbitrary length & 64-bit > u32 arbitrary length */
        aw_u8 arb[16] = { 'x', 0xff, 0xff, 0xff, 0xff, 1, 2, 3 };
        r = (struct aw_r){ arb, sizeof(arb), 0, 0, 0 };
        CHECK(aw_r_arbitrary(&r, 0xffffffffULL) == NULL && r.bad == 1 && r.at == 0,
              "0xffffffff arbitrary fails atomically");

        aw_u8 arb0[8] = { 'x', 0, 0, 0, 0, 99 };
        r = (struct aw_r){ arb0, sizeof(arb0), 0, 0, 0 };
        CHECK(aw_r_arbitrary(&r, 0x100000000ULL) == NULL && r.bad == 1 && r.at == 0,
              "n > u32 max fails");
        r = (struct aw_r){ arb0, sizeof(arb0), 0, 0, 0 };
        const aw_u8 *p0 = aw_r_arbitrary(&r, 0);
        CHECK(p0 == arb0 + 5 && r.bad == 0 && r.at == 5, "arbitrary n=0 succeeds");

        /* Embedded NUL in tag string & limit edge cases */
        aw_u8 emb[12] = { 't', 'f', 'o', 'o', 0, 'b', 'a', 'r', 0 };
        r = (struct aw_r){ emb, 9, 0, 0, 0 };
        const char *s = aw_r_string(&r, 1024, &isnull);
        CHECK(s != NULL && strcmp(s, "foo") == 0 && isnull == 0 && r.at == 5 && r.bad == 0,
              "embedded NUL stops at first NUL");
        CHECK(aw_r_tag(&r, 't') == 0 && r.bad == 1, "trailing bytes after embedded NUL fail next tag");

        /* limit == INT64_MAX, limit == exact length, limit == exact - 1 */
        r = (struct aw_r){ emb, 5, 0, 0, 0 };
        CHECK(aw_r_string(&r, INT64_MAX, &isnull) != NULL && r.bad == 0 && r.at == 5, "limit INT64_MAX");
        r = (struct aw_r){ emb, 5, 0, 0, 0 };
        CHECK(aw_r_string(&r, 3, &isnull) != NULL && r.bad == 0 && r.at == 5, "limit == strlen succeeds");
        r = (struct aw_r){ emb, 5, 0, 0, 0 };
        CHECK(aw_r_string(&r, 2, &isnull) == NULL && r.bad == 1 && r.at == 0, "limit < strlen fails atomically");

        /* NULL tag 'N' */
        aw_u8 ntag[2] = { 'N', 'x' };
        r = (struct aw_r){ ntag, 2, 0, 0, 0 };
        CHECK(aw_r_string(&r, 10, &isnull) == NULL && isnull == 1 && r.bad == 0 && r.at == 1, "tag N sets isnull");

        /* Channel map & cvolume boundary checks (0, max, max+1, 255, clamping) */
        aw_u8 cm_zero[2] = { 'm', 0 };
        aw_i8 map[8];
        r = (struct aw_r){ cm_zero, 2, 0, 0, 0 };
        CHECK(aw_r_channel_map(&r, map, 8, 32) == 0 && r.bad == 0 && r.at == 2, "channel_map n=0");

        aw_u8 cm_over[40];
        cm_over[0] = 'm'; cm_over[1] = 33;
        memset(cm_over + 2, 1, 38);
        r = (struct aw_r){ cm_over, sizeof(cm_over), 0, 0, 0 };
        CHECK(aw_r_channel_map(&r, map, 8, 32) == 0 && r.bad == 1 && r.at == 0, "channel_map n > max_ch fails");

        cm_over[1] = 10;
        cm_over[2] = 0; cm_over[3] = 63; cm_over[4] = 64; cm_over[5] = 255;
        memset(map, 0x7f, sizeof(map));
        r = (struct aw_r){ cm_over, 12, 0, 0, 0 };
        CHECK(aw_r_channel_map(&r, map, 4, 32) == 10 && r.bad == 0 && r.at == 12, "channel_map n=10 out_cap=4");
        CHECK(map[0] == 0 && map[1] == 63 && map[2] == 63 && map[3] == 63 && map[4] == 0x7f, "channel_map clamping & cap");

        aw_u8 cv_over[200];
        cv_over[0] = 'v'; cv_over[1] = 33;
        memset(cv_over + 2, 0, sizeof(cv_over) - 2);
        aw_u64 vols[32];
        r = (struct aw_r){ cv_over, sizeof(cv_over), 0, 0, 0 };
        CHECK(aw_r_cvolume(&r, vols, 32, 0x20000) == 0 && r.bad == 1 && r.at == 0, "cvolume n > max_ch fails");

        /* Zero-cap write buffer & invalid writer states */
        struct aw_w w = { NULL, 0, 0, 0, 0 };
        aw_w_raw(&w, NULL, 0);
        CHECK(w.bad == 0 && w.len == 0, "w_raw(0) on zero-cap NULL buffer succeeds");
        aw_w_raw(&w, "a", 1);
        CHECK(w.bad == 1 && w.len == 0, "w_raw(1) on zero-cap sets bad");

        aw_u8 wbuf[8];
        memset(wbuf, 0xaa, sizeof(wbuf));
        w = (struct aw_w){ wbuf, 4, 0, 0, 0 };
        aw_w_tagged32(&w, 'L', 0x12345678u);
        CHECK(w.bad == 1 && w.len == 0 && wbuf[0] == 0xaa, "w_tagged32 atomic fail on short cap");

        w = (struct aw_w){ wbuf, 8, 0, 0, 0 };
        aw_w_cvolume(&w, vols, -1);
        CHECK(w.bad == 1, "w_cvolume negative channels");
        w = (struct aw_w){ wbuf, 8, 0, 0, 0 };
        aw_w_cvolume(&w, vols, 256);
        CHECK(w.bad == 1, "w_cvolume channels > 255");
        w = (struct aw_w){ wbuf, 8, 0, 0, 0 };
        aw_w_cvolume(&w, NULL, 1);
        CHECK(w.bad == 1, "w_cvolume NULL volume with channels=1");
        w = (struct aw_w){ wbuf, 8, 0, 0, 0 };
        aw_w_channel_map(&w, NULL, 256, 8, 12);
        CHECK(w.bad == 1, "w_channel_map channels > 255");
        w = (struct aw_w){ wbuf, 8, 0, 0, 0 };
        aw_w_channel_map(&w, NULL, 1, 8, 12);
        CHECK(w.bad == 1, "w_channel_map NULL map with channels=1");
        w = (struct aw_w){ wbuf, 8, 0, 0, 0 };
        aw_w_raw(&w, "abc", -1);
        CHECK(w.bad == 1, "w_raw negative n");
}

/* ── 2. mmap Guard-Page Tests (leading AND trailing PROT_NONE) ───────────── */

static void test_guard_pages(void) {
        long ps = sysconf(_SC_PAGESIZE);
        if (ps <= 0) ps = 4096;
        size_t page = (size_t)ps;
        aw_u8 *mem = (aw_u8 *)mmap(NULL, 3 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        CHECK(mem != MAP_FAILED, "mmap 3 pages");
        if (mem == MAP_FAILED) return;

        CHECK(mprotect(mem, page, PROT_NONE) == 0, "mprotect leading guard");
        CHECK(mprotect(mem + 2 * page, page, PROT_NONE) == 0, "mprotect trailing guard");
        aw_u8 *rw = mem + page;

        /* Test both flush-at-start (rw) and flush-at-end (rw + page - len) */
        for (int pos = 0; pos < 2; pos++) {
                /* 1. Read u32 exact + truncated at guard */
                for (aw_i64 len = 0; len <= 5; len++) {
                        aw_u8 *b = pos == 0 ? rw : (rw + page - (size_t)len);
                        if (len > 0) b[0] = 'L';
                        for (aw_i64 i = 1; i < len; i++) b[i] = (aw_u8)(i + 1);
                        struct aw_r r = { b, len, 0, 0, 0 };
                        aw_u32 v = aw_r_u32(&r);
                        if (len == 5) CHECK(!r.bad && v == 0x02030405u && r.at == 5, "guard r_u32 exact");
                        else CHECK(r.bad && v == 0, "guard r_u32 trunc");
                }
                /* 2. Read string (unterminated right at guard!) */
                for (aw_i64 len = 1; len <= 16; len++) {
                        aw_u8 *b = pos == 0 ? rw : (rw + page - (size_t)len);
                        b[0] = 't';
                        for (aw_i64 i = 1; i < len; i++) b[i] = 'A';
                        struct aw_r r = { b, len, 0, 0, 0 };
                        aw_i32 isnull = 1;
                        CHECK(aw_r_string(&r, 1024, &isnull) == NULL && r.bad == 1 && isnull == 0,
                              "guard r_string unterminated");
                        if (len >= 2) {
                                b[len - 1] = 0;
                                r = (struct aw_r){ b, len, 0, 0, 0 };
                                const char *s = aw_r_string(&r, 1024, &isnull);
                                CHECK(s == (const char *)(b + 1) && !r.bad && r.at == len, "guard r_string exact");
                        }
                }
                /* 3. Read sample_spec, timeval, channel_map, cvolume, arbitrary at guard */
                {
                        aw_u8 *b = pos == 0 ? rw : (rw + page - 7);
                        b[0] = 'a'; b[1] = 3; b[2] = 2; b[3] = 0; b[4] = 0; b[5] = 0xbb; b[6] = 0x80;
                        struct aw_r r = { b, 7, 0, 0, 0 };
                        aw_i64 fmt = 0, ch = 0, rate = 0;
                        aw_r_sample_spec(&r, &fmt, &ch, &rate);
                        CHECK(!r.bad && fmt == 3 && ch == 2 && rate == 48000, "guard r_sample_spec");

                        b = pos == 0 ? rw : (rw + page - 9);
                        b[0] = 'T'; b[1]=0; b[2]=0; b[3]=0; b[4]=7; b[5]=0; b[6]=0; b[7]=1; b[8]=2;
                        r = (struct aw_r){ b, 9, 0, 0, 0 };
                        aw_u64 sec = 0, usec = 0;
                        aw_r_timeval(&r, &sec, &usec);
                        CHECK(!r.bad && sec == 7 && usec == 258, "guard r_timeval");

                        b = pos == 0 ? rw : (rw + page - 10);
                        b[0] = 'v'; b[1] = 2;
                        aw_put_be32(b + 2, 0x10000u);
                        aw_put_be32(b + 6, 0x50000u);
                        r = (struct aw_r){ b, 10, 0, 0, 0 };
                        aw_u64 vols[2] = {0};
                        CHECK(aw_r_cvolume(&r, vols, 32, 0x20000u) == 2 && !r.bad &&
                              vols[0] == 0x10000u && vols[1] == 0x20000u, "guard r_cvolume");

                        b = pos == 0 ? rw : (rw + page - 8);
                        b[0] = 'x'; aw_put_be32(b + 1, 3); b[5] = 10; b[6] = 20; b[7] = 30;
                        r = (struct aw_r){ b, 8, 0, 0, 0 };
                        const aw_u8 *ar = aw_r_arbitrary(&r, 3);
                        CHECK(ar == b + 5 && !r.bad && r.at == 8, "guard r_arbitrary");
                }
                /* 4. Writers flush at guard: exact fit succeeds, cap-1 fails without touching guard */
                {
                        aw_u8 *b = pos == 0 ? rw : (rw + page - 9);
                        struct aw_w w = { b, 9, 0, 0, 0 };
                        aw_w_64(&w, 'R', 0x0102030405060708ULL);
                        CHECK(!w.bad && w.len == 9 && b[0] == 'R' && b[8] == 8, "guard w_64 exact");

                        b = pos == 0 ? rw : (rw + page - 8);
                        w = (struct aw_w){ b, 8, 0, 0, 0 };
                        aw_w_64(&w, 'R', 0x0102030405060708ULL);
                        CHECK(w.bad == 1 && w.len == 0, "guard w_64 1-short fails cleanly");

                        b = pos == 0 ? rw : (rw + page - 17);
                        w = (struct aw_w){ b, 17, 0, 0, 0 };
                        aw_w_string(&w, "hello_moonwater"); /* 1 + 15 + 1 = 17 */
                        CHECK(!w.bad && w.len == 17 && b[0] == 't' && b[16] == 0, "guard w_string exact");

                        b = pos == 0 ? rw : (rw + page - 16);
                        w = (struct aw_w){ b, 16, 0, 0, 0 };
                        aw_w_string(&w, "hello_moonwater");
                        CHECK(w.bad == 1 && w.len == 0, "guard w_string 1-short fails cleanly");

                        aw_u64 vols[4] = { 0x10000, 0x10000, 0x20000, 0 };
                        b = pos == 0 ? rw : (rw + page - 18);
                        w = (struct aw_w){ b, 18, 0, 0, 0 };
                        aw_w_cvolume(&w, vols, 4);
                        CHECK(!w.bad && w.len == 18, "guard w_cvolume exact");

                        aw_i8 cmap[4] = { 1, 2, 3, 4 };
                        b = pos == 0 ? rw : (rw + page - 12);
                        w = (struct aw_w){ b, 12, 0, 0, 0 };
                        aw_w_channel_map(&w, cmap, 10, 4, 12);
                        CHECK(!w.bad && w.len == 12 && b[2] == 1 && b[6] == 16, "guard w_channel_map exact");
                }
        }
        munmap(mem, 3 * page);
}

/* ── 3. Differential Fuzzing (ASM vs -O2 C Reference) ────────────────────── */

static void test_fuzz(void) {
        const int ITERS = 40000;
        aw_u8 buf[256], wa[256], wr[256];

        for (int it = 0; it < ITERS; it++) {
                aw_i64 len = (aw_i64)(xorshift64() % 128);
                for (aw_i64 i = 0; i < len; i++) buf[i] = (aw_u8)xorshift64();
                /* Bias tag byte so we exercise valid paths heavily too */
                static const aw_u8 tags[] = { 'L', 'B', '0', '1', 'N', 't', 'a', 'm', 'v', 'T', 'x', 'V', 'R', 'Z' };
                if (len > 0 && (xorshift64() & 1))
                        buf[0] = tags[xorshift64() % sizeof(tags)];
                if (len > 2 && buf[0] == 't' && (xorshift64() & 1))
                        buf[1 + (xorshift64() % (uint64_t)(len - 1))] = 0;
                if (len > 1 && (buf[0] == 'm' || buf[0] == 'v') && (xorshift64() & 1))
                        buf[1] = (aw_u8)(xorshift64() % 35);

                aw_i64 at = (xorshift64() % 8 == 0) ? (aw_i64)(xorshift64() % (uint64_t)(len + 4)) - 2 : 0;
                int op = (int)(xorshift64() % 20);

                if (op < 10) {
                        struct aw_r ra = { buf, len, at, 0, 0 };
                        struct aw_r rr = { buf, len, at, 0, 0 };
                        if (op == 0) {
                                aw_u32 tag = tags[xorshift64() % sizeof(tags)];
                                aw_i32 a = aw_r_tag(&ra, tag), b = ref_aw_r_tag(&rr, tag);
                                CHECK(a == b && ra.bad == rr.bad && ra.at == rr.at, "fuzz r_tag");
                        } else if (op == 1) {
                                aw_u32 a = aw_r_u8(&ra), b = ref_aw_r_u8(&rr);
                                CHECK(a == b && ra.bad == rr.bad && ra.at == rr.at, "fuzz r_u8");
                        } else if (op == 2) {
                                aw_u32 a = aw_r_u32(&ra), b = ref_aw_r_u32(&rr);
                                CHECK(a == b && ra.bad == rr.bad && ra.at == rr.at, "fuzz r_u32");
                        } else if (op == 3) {
                                aw_i32 a = aw_r_bool(&ra), b = ref_aw_r_bool(&rr);
                                CHECK(a == b && ra.bad == rr.bad && ra.at == rr.at, "fuzz r_bool");
                        } else if (op == 4) {
                                aw_i64 lim = (aw_i64)(xorshift64() % 40) - 2;
                                aw_i32 na = 9, nr = 9;
                                const char *a = aw_r_string(&ra, lim, &na);
                                const char *b = ref_aw_r_string(&rr, lim, &nr);
                                CHECK(a == b && na == nr && ra.bad == rr.bad && ra.at == rr.at, "fuzz r_string");
                        } else if (op == 5) {
                                aw_i64 fa = -1, ca = -1, rta = -1, fr = -1, cr = -1, rtr = -1;
                                aw_r_sample_spec(&ra, &fa, &ca, &rta);
                                ref_aw_r_sample_spec(&rr, &fr, &cr, &rtr);
                                CHECK(fa == fr && ca == cr && rta == rtr && ra.bad == rr.bad && ra.at == rr.at, "fuzz r_sample_spec");
                        } else if (op == 6) {
                                aw_i8 ma[8], mr[8];
                                memset(ma, 0x55, sizeof(ma)); memset(mr, 0x55, sizeof(mr));
                                aw_i64 a = aw_r_channel_map(&ra, ma, 8, 32);
                                aw_i64 b = ref_aw_r_channel_map(&rr, mr, 8, 32);
                                CHECK(a == b && ra.bad == rr.bad && ra.at == rr.at && memcmp(ma, mr, 8) == 0, "fuzz r_channel_map");
                        } else if (op == 7) {
                                aw_u64 va[32], vr[32];
                                memset(va, 0x55, sizeof(va)); memset(vr, 0x55, sizeof(vr));
                                aw_i64 a = aw_r_cvolume(&ra, va, 32, 0x20000u);
                                aw_i64 b = ref_aw_r_cvolume(&rr, vr, 32, 0x20000u);
                                CHECK(a == b && ra.bad == rr.bad && ra.at == rr.at && memcmp(va, vr, sizeof(va)) == 0, "fuzz r_cvolume");
                        } else if (op == 8) {
                                aw_u64 sa = 0, ua = 0, sr = 0, ur = 0;
                                aw_r_timeval(&ra, &sa, &ua);
                                ref_aw_r_timeval(&rr, &sr, &ur);
                                CHECK(sa == sr && ua == ur && ra.bad == rr.bad && ra.at == rr.at, "fuzz r_timeval");
                        } else {
                                aw_u64 n = (len >= 5 && (xorshift64() & 1)) ? aw_be32(buf + 1) : (xorshift64() % 32);
                                const aw_u8 *a = aw_r_arbitrary(&ra, n);
                                const aw_u8 *b = ref_aw_r_arbitrary(&rr, n);
                                CHECK(a == b && ra.bad == rr.bad && ra.at == rr.at, "fuzz r_arbitrary");
                        }
                } else {
                        aw_i64 cap = (aw_i64)(xorshift64() % 128);
                        memset(wa, 0xcc, sizeof(wa));
                        memset(wr, 0xcc, sizeof(wr));
                        struct aw_w w1 = { wa, cap, 0, 0, 0 };
                        struct aw_w w2 = { wr, cap, 0, 0, 0 };
                        if (op == 10) {
                                aw_i64 n = (aw_i64)(xorshift64() % 40) - 2;
                                aw_w_raw(&w1, buf, n); ref_aw_w_raw(&w2, buf, n);
                        } else if (op == 11) {
                                aw_u32 c = (aw_u32)xorshift64();
                                aw_w_byte(&w1, c); ref_aw_w_byte(&w2, c);
                        } else if (op == 12) {
                                aw_u32 v = (aw_u32)xorshift64();
                                aw_w_be32(&w1, v); ref_aw_w_be32(&w2, v);
                        } else if (op == 13) {
                                aw_u32 t = (aw_u32)xorshift64(), v = (aw_u32)xorshift64();
                                aw_w_tagged32(&w1, t, v); ref_aw_w_tagged32(&w2, t, v);
                        } else if (op == 14) {
                                aw_i32 v = (aw_i32)(xorshift64() % 3) - 1;
                                aw_w_bool(&w1, v); ref_aw_w_bool(&w2, v);
                        } else if (op == 15) {
                                aw_u32 v = (aw_u32)xorshift64();
                                aw_w_u8(&w1, v); ref_aw_w_u8(&w2, v);
                        } else if (op == 16) {
                                aw_u32 t = (aw_u32)xorshift64();
                                aw_u64 v = xorshift64();
                                aw_w_64(&w1, t, v); ref_aw_w_64(&w2, t, v);
                        } else if (op == 17) {
                                aw_i64 f = (aw_i64)(xorshift64() & 0xff), c = (aw_i64)(xorshift64() & 0xff), rt = (aw_i64)(xorshift64() & 0xffffffffu);
                                aw_w_sample_spec(&w1, f, c, rt); ref_aw_w_sample_spec(&w2, f, c, rt);
                        } else if (op == 18) {
                                aw_i8 map[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
                                aw_i64 ch = (aw_i64)(xorshift64() % 40) - 2;
                                aw_w_channel_map(&w1, map, ch, 8, 12);
                                ref_aw_w_channel_map(&w2, map, ch, 8, 12);
                        } else {
                                if (xorshift64() & 1) {
                                        char sbuf[40];
                                        size_t sl = (size_t)(xorshift64() % 32);
                                        for (size_t i = 0; i < sl; i++) sbuf[i] = (char)('a' + (xorshift64() % 26));
                                        sbuf[sl] = 0;
                                        const char *sp = (xorshift64() % 5 == 0) ? NULL : sbuf;
                                        aw_w_string(&w1, sp); ref_aw_w_string(&w2, sp);
                                } else {
                                        aw_u64 vols[32];
                                        for (int i = 0; i < 32; i++) vols[i] = xorshift64() & 0x3ffffu;
                                        aw_i64 ch = (aw_i64)(xorshift64() % 34) - 1;
                                        aw_w_cvolume(&w1, vols, ch); ref_aw_w_cvolume(&w2, vols, ch);
                                }
                        }
                        CHECK(w1.bad == w2.bad && w1.len == w2.len && memcmp(wa, wr, sizeof(wa)) == 0, "fuzz writer parity");
                }
        }

        /* Full PulseAudio frame roundtrip */
        aw_u8 f1[512], f2[512];
        struct aw_w w1 = { f1, sizeof(f1), 0, 0, 0 }, w2 = { f2, sizeof(f2), 0, 0, 0 };
        aw_i8 cmap[8] = { 1, 2, 3, 7, 5, 6, 10, 11 };
        aw_u64 cvol[6] = { 0x10000, 0x10000, 0x08000, 0x18000, 0x20000, 0x04000 };
        aw_u8 payload[16] = { 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15 };

        aw_w_tagged32(&w1, 'L', 3); aw_w_tagged32(&w1, 'L', 42);
        aw_w_sample_spec(&w1, 5, 6, 48000);
        aw_w_channel_map(&w1, cmap, 6, 8, 12);
        aw_w_cvolume(&w1, cvol, 6);
        aw_w_string(&w1, "alsa_output.pci-0000.analog-stereo");
        aw_w_string(&w1, NULL);
        aw_w_bool(&w1, 1); aw_w_bool(&w1, 0);
        aw_w_u8(&w1, 200);
        aw_w_64(&w1, 'R', 0x1122334455667788ULL);
        aw_w_tagged32(&w1, 'V', 0x10000);
        aw_w_byte(&w1, 'x'); aw_w_be32(&w1, 16); aw_w_raw(&w1, payload, 16);

        ref_aw_w_tagged32(&w2, 'L', 3); ref_aw_w_tagged32(&w2, 'L', 42);
        ref_aw_w_sample_spec(&w2, 5, 6, 48000);
        ref_aw_w_channel_map(&w2, cmap, 6, 8, 12);
        ref_aw_w_cvolume(&w2, cvol, 6);
        ref_aw_w_string(&w2, "alsa_output.pci-0000.analog-stereo");
        ref_aw_w_string(&w2, NULL);
        ref_aw_w_bool(&w2, 1); ref_aw_w_bool(&w2, 0);
        ref_aw_w_u8(&w2, 200);
        ref_aw_w_64(&w2, 'R', 0x1122334455667788ULL);
        ref_aw_w_tagged32(&w2, 'V', 0x10000);
        ref_aw_w_byte(&w2, 'x'); ref_aw_w_be32(&w2, 16); ref_aw_w_raw(&w2, payload, 16);

        CHECK(!w1.bad && !w2.bad && w1.len == w2.len && memcmp(f1, f2, (size_t)w1.len) == 0, "roundtrip frame encode");

        struct aw_r r = { f1, w1.len, 0, 0, 0 };
        aw_i64 fmt = 0, ch = 0, rate = 0;
        aw_i8 omap[8] = {0};
        aw_u64 ovol[6] = {0};
        aw_i32 isnull = 0;
        CHECK(aw_r_u32(&r) == 3 && aw_r_u32(&r) == 42, "rt cmd+tag");
        aw_r_sample_spec(&r, &fmt, &ch, &rate);
        CHECK(fmt == 5 && ch == 6 && rate == 48000, "rt sample_spec");
        CHECK(aw_r_channel_map(&r, omap, 8, 32) == 6 && memcmp(omap, cmap, 6) == 0, "rt channel_map");
        CHECK(aw_r_cvolume(&r, ovol, 32, 0x20000) == 6 && memcmp(ovol, cvol, sizeof(cvol)) == 0, "rt cvolume");
        const char *sn = aw_r_string(&r, 1024, &isnull);
        CHECK(sn && strcmp(sn, "alsa_output.pci-0000.analog-stereo") == 0 && !isnull, "rt string");
        CHECK(aw_r_string(&r, 1024, &isnull) == NULL && isnull == 1, "rt null string");
        CHECK(aw_r_bool(&r) == 1 && aw_r_bool(&r) == 0, "rt bools");
        CHECK(aw_r_u8(&r) == 200, "rt u8");
        CHECK(aw_r_tag(&r, 'R') == 1, "rt u64 tag");
        r.at += 8;
        CHECK(aw_r_tag(&r, 'V') == 1 && aw_be32(r.p + r.at) == 0x10000u, "rt volume");
        r.at += 4;
        const aw_u8 *ap = aw_r_arbitrary(&r, 16);
        CHECK(ap && memcmp(ap, payload, 16) == 0 && !r.bad && r.at == r.length, "rt arbitrary + eof");
}

/* ── 4. Speed Benchmark (ASM vs -O2 C Reference) ─────────────────────────── */

static void bench_all(void) {
        const int N = 2000000;
        uint64_t s = 0;
        double t0;

        /* 1. Full tag-struct frame encode */
        aw_u8 fbuf[256];
        aw_i8 cmap[8] = { 1, 2, 3, 7, 5, 6, 10, 11 };
        aw_u64 cvol[6] = { 0x10000, 0x10000, 0x08000, 0x18000, 0x20000, 0x04000 };
        aw_u8 payload[24] = {0};

        t0 = now_ns();
        for (int i = 0; i < N; i++) {
                struct aw_w w = { fbuf, sizeof(fbuf), 0, 0, 0 };
                aw_w_tagged32(&w, 'L', 3);
                aw_w_tagged32(&w, 'L', (aw_u32)i);
                aw_w_sample_spec(&w, 5, 6, 48000);
                aw_w_channel_map(&w, cmap, 6, 8, 12);
                aw_w_cvolume(&w, cvol, 6);
                aw_w_string(&w, "alsa_output.pci-0000.analog-stereo");
                aw_w_bool(&w, 1);
                aw_w_64(&w, 'R', 0x1122334455667788ULL);
                aw_w_raw(&w, payload, 24);
                s += (uint64_t)w.len;
        }
        double asm_enc = (now_ns() - t0) / N;

        t0 = now_ns();
        for (int i = 0; i < N; i++) {
                struct aw_w w = { fbuf, sizeof(fbuf), 0, 0, 0 };
                ref_aw_w_tagged32(&w, 'L', 3);
                ref_aw_w_tagged32(&w, 'L', (aw_u32)i);
                ref_aw_w_sample_spec(&w, 5, 6, 48000);
                ref_aw_w_channel_map(&w, cmap, 6, 8, 12);
                ref_aw_w_cvolume(&w, cvol, 6);
                ref_aw_w_string(&w, "alsa_output.pci-0000.analog-stereo");
                ref_aw_w_bool(&w, 1);
                ref_aw_w_64(&w, 'R', 0x1122334455667788ULL);
                ref_aw_w_raw(&w, payload, 24);
                s += (uint64_t)w.len;
        }
        double ref_enc = (now_ns() - t0) / N;

        /* 2. Full tag-struct frame decode */
        struct aw_w wp = { fbuf, sizeof(fbuf), 0, 0, 0 };
        aw_w_tagged32(&wp, 'L', 3);
        aw_w_tagged32(&wp, 'L', 42);
        aw_w_sample_spec(&wp, 5, 6, 48000);
        aw_w_channel_map(&wp, cmap, 6, 8, 12);
        aw_w_cvolume(&wp, cvol, 6);
        aw_w_string(&wp, "alsa_output.pci-0000.analog-stereo");
        aw_w_bool(&wp, 1);
        aw_w_u8(&wp, 7);
        aw_w_byte(&wp, 'T'); aw_w_be32(&wp, 100); aw_w_be32(&wp, 200);
        aw_w_byte(&wp, 'x'); aw_w_be32(&wp, 24); aw_w_raw(&wp, payload, 24);

        aw_i64 fmt, ch, rate;
        aw_i8 omap[8];
        aw_u64 ovol[6], sec, usec;
        aw_i32 isnull;

        t0 = now_ns();
        for (int i = 0; i < N; i++) {
                struct aw_r r = { fbuf, wp.len, 0, 0, 0 };
                s += aw_r_u32(&r);
                s += aw_r_u32(&r);
                aw_r_sample_spec(&r, &fmt, &ch, &rate);
                s += (uint64_t)aw_r_channel_map(&r, omap, 8, 32);
                s += (uint64_t)aw_r_cvolume(&r, ovol, 32, 0x20000);
                s += (uintptr_t)aw_r_string(&r, 1024, &isnull);
                s += (uint64_t)aw_r_bool(&r);
                s += aw_r_u8(&r);
                aw_r_timeval(&r, &sec, &usec);
                s += (uintptr_t)aw_r_arbitrary(&r, 24);
        }
        double asm_dec = (now_ns() - t0) / N;

        t0 = now_ns();
        for (int i = 0; i < N; i++) {
                struct aw_r r = { fbuf, wp.len, 0, 0, 0 };
                s += ref_aw_r_u32(&r);
                s += ref_aw_r_u32(&r);
                ref_aw_r_sample_spec(&r, &fmt, &ch, &rate);
                s += (uint64_t)ref_aw_r_channel_map(&r, omap, 8, 32);
                s += (uint64_t)ref_aw_r_cvolume(&r, ovol, 32, 0x20000);
                s += (uintptr_t)ref_aw_r_string(&r, 1024, &isnull);
                s += (uint64_t)ref_aw_r_bool(&r);
                s += ref_aw_r_u8(&r);
                ref_aw_r_timeval(&r, &sec, &usec);
                s += (uintptr_t)ref_aw_r_arbitrary(&r, 24);
        }
        double ref_dec = (now_ns() - t0) / N;

        /* 3. Isolated r_string + r_cvolume + w_cvolume */
        aw_u8 sbuf[64];
        struct aw_w ws = { sbuf, sizeof(sbuf), 0, 0, 0 };
        aw_w_string(&ws, "media.name");
        t0 = now_ns();
        for (int i = 0; i < N; i++) {
                struct aw_r r = { sbuf, ws.len, 0, 0, 0 };
                s += (uintptr_t)aw_r_string(&r, 1024, &isnull);
        }
        double asm_rs = (now_ns() - t0) / N;
        t0 = now_ns();
        for (int i = 0; i < N; i++) {
                struct aw_r r = { sbuf, ws.len, 0, 0, 0 };
                s += (uintptr_t)ref_aw_r_string(&r, 1024, &isnull);
        }
        double ref_rs = (now_ns() - t0) / N;

        g_sink = s;
        printf("  frame_encode   : ASM %6.2f ns/op | C -O2 %6.2f ns/op | speedup %.2fx\n", asm_enc, ref_enc, ref_enc / asm_enc);
        printf("  frame_decode   : ASM %6.2f ns/op | C -O2 %6.2f ns/op | speedup %.2fx\n", asm_dec, ref_dec, ref_dec / asm_dec);
        printf("  r_string       : ASM %6.2f ns/op | C -O2 %6.2f ns/op | speedup %.2fx\n", asm_rs, ref_rs, ref_rs / asm_rs);
}

int main(void) {
        test_null_and_bad_edges();
        test_guard_pages();
        test_fuzz();
        printf("audio_wire: %d checks, %d failures\n", g_checks, g_fails);
        if (g_fails == 0) bench_all();
        return g_fails ? 1 : 0;
}
