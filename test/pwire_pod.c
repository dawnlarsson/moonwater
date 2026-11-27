/*
        Exhaustive PipeWire SPA pod parser security, edge-case, guard-page,
        differential-fuzz, and speed/size proof for src/cast/pwire_pod.asm.c
        (arm64, x86-64, riscv64).

        Build & run:
          cc -O2 -Wall -Werror test/pwire_pod.c -o /tmp/t_pod && /tmp/t_pod
          cc -O2 -fsanitize=address,undefined -Wall -Werror test/pwire_pod.c -o /tmp/t_pod_asan && /tmp/t_pod_asan
*/
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>

#define CAST_PWIRE_POD_ASM_IMPLEMENTATION
#include "../src/cast/pwire_pod.asm.c"

/* ── -O2 C Reference Implementations (exact safe parity with pipewire.c) ── */

static __attribute__((noinline)) pw_u32 ref_pwire_pod_get32(const pw_u8 *p)
{
        if (!p) return 0;
        return (pw_u32)p[0] | ((pw_u32)p[1] << 8) | ((pw_u32)p[2] << 16) | ((pw_u32)p[3] << 24);
}

static __attribute__((noinline)) void ref_pwire_pod_put32(pw_u8 *p, pw_u32 v)
{
        if (!p) return;
        p[0] = (pw_u8)v;
        p[1] = (pw_u8)(v >> 8);
        p[2] = (pw_u8)(v >> 16);
        p[3] = (pw_u8)(v >> 24);
}

static __attribute__((noinline)) pw_i32 ref_pwire_pod_next(
        const pw_u8 *p, pw_i64 n, pw_i64 *at, struct pwire_pod_view *pod)
{
        if (!p || !at || !pod) return -1;
        pw_i64 pos = *at;
        if (pos < 0 || n < 0 || pos > n) return -1;
        pw_u64 rem = (pw_u64)(n - pos);
        if (rem < 8u) return -1;
        pw_u64 size = (pw_u64)ref_pwire_pod_get32(p + pos);
        pw_u32 type = ref_pwire_pod_get32(p + pos + 4);
        if (size > rem - 8u) return -1;
        pod->type = type;
        pod->size = (pw_u32)size;
        pod->body = p + pos + 8;
        pw_u64 step = 8u + ((size + 7u) & ~(pw_u64)7u);
        *at = (step >= rem) ? n : (pos + (pw_i64)step);
        return 0;
}

static __attribute__((noinline)) pw_i32 ref_pwire_pod_struct(
        const struct pwire_pod_view *pod, struct pwire_pod_members *m)
{
        if (!pod || !m) return -1;
        m->p = pod->body;
        m->n = (pw_i64)pod->size;
        m->at = 0;
        m->bad = (pod->type != PW_POD_STRUCT) ? 1 : 0;
        return m->bad ? -1 : 0;
}

static __attribute__((noinline)) pw_i32 ref_pwire_pod_member(
        struct pwire_pod_members *m, struct pwire_pod_view *out)
{
        if (!out) {
                if (m) m->bad = 1;
                return -1;
        }
        out->type = 0;
        out->size = 0;
        out->body = NULL;
        if (!m) return -1;
        if (m->bad || ref_pwire_pod_next(m->p, m->n, &m->at, out) < 0) {
                m->bad = 1;
                out->type = 0;
                out->size = 0;
                out->body = NULL;
                return -1;
        }
        return 0;
}

static __attribute__((noinline)) pw_u32 ref_pwire_pod_int(struct pwire_pod_members *m)
{
        if (!m) return 0;
        struct pwire_pod_view pod;
        if (ref_pwire_pod_member(m, &pod) < 0) return 0;
        if ((pod.type != PW_POD_INT && pod.type != PW_POD_ID && pod.type != PW_POD_BOOL) || pod.size < 4u) {
                m->bad = 1;
                return 0;
        }
        return ref_pwire_pod_get32(pod.body);
}

static __attribute__((noinline)) pw_i32 ref_pwire_pod_string(
        struct pwire_pod_members *m, const char **out, pw_i64 *length)
{
        if (out) *out = NULL;
        if (length) *length = 0;
        if (!m) return -1;
        struct pwire_pod_view pod;
        if (ref_pwire_pod_member(m, &pod) < 0) return -1;
        if (pod.type == PW_POD_NONE) return 0;
        if (pod.type != PW_POD_STRING || !pod.size || pod.body[pod.size - 1u] != 0) {
                m->bad = 1;
                return -1;
        }
        pw_i64 n = 0;
        while (pod.body[n]) n++;
        if (out) *out = (const char *)pod.body;
        if (length) *length = n;
        return 1;
}

static __attribute__((noinline)) pw_i32 ref_pwire_pod_values(
        const struct pwire_pod_view *pod, struct pwire_pod_values *v)
{
        if (!pod || !v) return -1;
        if (pod->type != PW_POD_CHOICE) {
                v->type = pod->type;
                v->choice = PW_CHOICE_NONE;
                v->size = pod->size;
                v->count = 1u;
                v->at = pod->body;
                return 0;
        }
        if (!pod->body || pod->size < 16u) return -1;
        pw_u32 choice = ref_pwire_pod_get32(pod->body);
        pw_u32 esize = ref_pwire_pod_get32(pod->body + 8);
        pw_u32 etype = ref_pwire_pod_get32(pod->body + 12);
        pw_u32 avail = pod->size - 16u;
        if (!esize || esize > avail) return -1;
        v->type = etype;
        v->choice = choice;
        v->size = esize;
        v->count = avail / esize;
        v->at = pod->body + 16;
        return 0;
}

static __attribute__((noinline)) pw_u32 ref_pwire_pod_value32(
        const struct pwire_pod_values *v, pw_u32 k, pw_u32 word)
{
        if (!v || !v->at || k >= v->count) return 0;
        pw_u64 need = ((pw_u64)word + 1u) << 2;
        if ((pw_u64)v->size < need) return 0;
        pw_u64 off = (pw_u64)k * (pw_u64)v->size + ((pw_u64)word << 2);
        return ref_pwire_pod_get32(v->at + off);
}

static __attribute__((noinline)) pw_i32 ref_pwire_pod_allows(
        const struct pwire_pod_values *v, pw_u32 x)
{
        if (!v) return 0;
        if (v->choice == PW_CHOICE_NONE)
                return ref_pwire_pod_value32(v, 0, 0) == x;
        if (v->choice == PW_CHOICE_ENUM) {
                for (pw_u32 k = v->count > 1u ? 1u : 0u; k < v->count; k++)
                        if (ref_pwire_pod_value32(v, k, 0) == x)
                                return 1;
                return 0;
        }
        if (v->choice == PW_CHOICE_RANGE || v->choice == PW_CHOICE_STEP)
                return v->count >= 3u && ref_pwire_pod_value32(v, 1, 0) <= x && x <= ref_pwire_pod_value32(v, 2, 0);
        if (v->choice == PW_CHOICE_FLAGS)
                return (ref_pwire_pod_value32(v, 0, 0) & x) == x;
        return 0;
}

static __attribute__((noinline)) pw_i32 ref_pwire_pod_object(
        const struct pwire_pod_view *pod, struct pwire_pod_props *o)
{
        if (!pod || !o || !pod->body) return -1;
        if (pod->type != PW_POD_OBJECT || pod->size < 8u) return -1;
        o->type = ref_pwire_pod_get32(pod->body);
        o->id = ref_pwire_pod_get32(pod->body + 4);
        o->p = pod->body + 8;
        o->n = (pw_i64)(pod->size - 8u);
        o->at = 0;
        return 0;
}

static __attribute__((noinline)) pw_i32 ref_pwire_pod_prop(
        struct pwire_pod_props *o, pw_u32 *key, struct pwire_pod_view *value)
{
        if (!o || !key || !value) return -1;
        if (o->at < 0) return -1;
        if (o->at >= o->n) return 0;
        if (!o->p || (pw_u64)(o->n - o->at) < 8u) return -1;
        *key = ref_pwire_pod_get32(o->p + o->at);
        o->at += 8;
        return ref_pwire_pod_next(o->p, o->n, &o->at, value) < 0 ? -1 : 1;
}

/* ── Test Harness & Pod Builder Helpers ─────────────────────────────────── */

static unsigned g_checks = 0, g_fails = 0;

#define CHECK(cond, ...) do { \
        g_checks++; \
        if (!(cond)) { \
                g_fails++; \
                fprintf(stderr, "FAIL [line %d]: ", __LINE__); \
                fprintf(stderr, __VA_ARGS__); \
                fprintf(stderr, "\n"); \
        } \
} while (0)

static size_t put_pod(pw_u8 *buf, size_t off, pw_u32 type, const void *body, pw_u32 size)
{
        ref_pwire_pod_put32(buf + off, size);
        ref_pwire_pod_put32(buf + off + 4, type);
        if (size && body) memcpy(buf + off + 8, body, size);
        size_t padded = (size + 7u) & ~(size_t)7u;
        if (padded > size) memset(buf + off + 8 + size, 0, padded - size);
        return off + 8 + padded;
}

static size_t put_u32_pod(pw_u8 *buf, size_t off, pw_u32 type, pw_u32 v)
{
        pw_u8 b[4];
        ref_pwire_pod_put32(b, v);
        return put_pod(buf, off, type, b, 4);
}

static size_t put_choice_u32(pw_u8 *buf, size_t off, pw_u32 choice, pw_u32 etype, const pw_u32 *vals, pw_u32 n)
{
        pw_u8 body[256];
        ref_pwire_pod_put32(body, choice);
        ref_pwire_pod_put32(body + 4, 0);
        ref_pwire_pod_put32(body + 8, 4);
        ref_pwire_pod_put32(body + 12, etype);
        for (pw_u32 i = 0; i < n; i++)
                ref_pwire_pod_put32(body + 16 + i * 4, vals[i]);
        return put_pod(buf, off, PW_POD_CHOICE, body, 16u + n * 4u);
}

/* ── 1. Exhaustive Edge-Case & Security Tests ───────────────────────────── */

static void test_edge_cases(void)
{
        /* get32 / put32 NULL & unaligned */
        CHECK(pwire_pod_get32(NULL) == 0, "get32(NULL)");
        pwire_pod_put32(NULL, 0x12345678u);
        pw_u8 scratch[16] = {0};
        const pw_u32 vals[] = {0u, 1u, 0x7fffffffu, 0x80000000u, 0xffffffffu, 0xdeadbeefu};
        for (size_t i = 0; i < sizeof(vals)/sizeof(vals[0]); i++) {
                for (size_t off = 0; off < 8; off++) {
                        pwire_pod_put32(scratch + off, vals[i]);
                        CHECK(pwire_pod_get32(scratch + off) == vals[i], "put/get32 off=%zu val=%#x", off, vals[i]);
                        CHECK(ref_pwire_pod_get32(scratch + off) == vals[i], "ref get32 off=%zu", off);
                }
        }

        /* pwire_pod_next: NULL, negative, truncated, overflow */
        pw_u8 buf[64];
        memset(buf, 0, sizeof(buf));
        pw_i64 at = 0;
        struct pwire_pod_view pod;
        CHECK(pwire_pod_next(NULL, 16, &at, &pod) == -1, "next NULL p");
        CHECK(pwire_pod_next(buf, 16, NULL, &pod) == -1, "next NULL at");
        CHECK(pwire_pod_next(buf, 16, &at, NULL) == -1, "next NULL pod");

        at = -1; CHECK(pwire_pod_next(buf, 16, &at, &pod) == -1, "next neg at");
        at = 0;  CHECK(pwire_pod_next(buf, -1, &at, &pod) == -1, "next neg n");
        at = 17; CHECK(pwire_pod_next(buf, 16, &at, &pod) == -1, "next at > n");
        for (pw_i64 len = 0; len < 8; len++) {
                at = 0;
                CHECK(pwire_pod_next(buf, len, &at, &pod) == -1, "next truncated hdr len=%lld", (long long)len);
        }

        /* 0xffffffff / 0xfffffff8..0xfffffffe size overflow attacks */
        const pw_u32 evil_sizes[] = {0xffffffffu, 0xfffffffeu, 0xfffffffdu, 0xfffffff9u, 0xfffffff8u, 0x80000000u, 0x7fffffffu, 57u};
        for (size_t i = 0; i < sizeof(evil_sizes)/sizeof(evil_sizes[0]); i++) {
                ref_pwire_pod_put32(buf, evil_sizes[i]);
                ref_pwire_pod_put32(buf + 4, PW_POD_INT);
                at = 0;
                CHECK(pwire_pod_next(buf, 64, &at, &pod) == -1, "next evil size=%#x", evil_sizes[i]);
                CHECK(at == 0, "next evil size must not advance at");
        }

        /* INT64_MAX signed wraparound test on *at += step */
        ref_pwire_pod_put32(buf, 1u);
        ref_pwire_pod_put32(buf + 4, PW_POD_INT);
        buf[8] = 0x42;
        pw_i64 big_n = INT64_MAX;
        pw_i64 big_at = INT64_MAX - 9; /* rem == 9: 8-byte header + 1-byte body, step == 16 > rem! */
        const pw_u8 *big_base = (const pw_u8 *)((uintptr_t)buf - (uintptr_t)big_at);
        CHECK(pwire_pod_next(big_base, big_n, &big_at, &pod) == 0, "next INT64_MAX clamp");
        CHECK(big_at == INT64_MAX && pod.size == 1 && pod.type == PW_POD_INT, "next INT64_MAX post");

        /* Exact 8-byte padding clamping without trailing pad bytes */
        ref_pwire_pod_put32(buf, 1u);
        ref_pwire_pod_put32(buf + 4, PW_POD_STRING);
        buf[8] = 0;
        at = 0;
        CHECK(pwire_pod_next(buf, 9, &at, &pod) == 0 && at == 9 && pod.size == 1 && pod.body == buf + 8, "next unpadded tail");

        /* Multi-pod stream with all sizes 0..17 */
        pw_u8 stream[512];
        size_t off = 0;
        for (pw_u32 sz = 0; sz <= 17; sz++) {
                pw_u8 payload[20];
                for (pw_u32 j = 0; j < sz; j++) payload[j] = (pw_u8)(sz + j);
                off = put_pod(stream, off, PW_POD_INT, payload, sz);
        }
        at = 0;
        for (pw_u32 sz = 0; sz <= 17; sz++) {
                CHECK(pwire_pod_next(stream, (pw_i64)off, &at, &pod) == 0, "stream sz=%u", sz);
                CHECK(pod.size == sz && pod.type == PW_POD_INT, "stream hdr sz=%u", sz);
                for (pw_u32 j = 0; j < sz; j++)
                        CHECK(pod.body[j] == (pw_u8)(sz + j), "stream body sz=%u j=%u", sz, j);
        }
        CHECK(at == (pw_i64)off, "stream end");
        CHECK(pwire_pod_next(stream, (pw_i64)off, &at, &pod) == -1, "stream past end");

        /* Struct / member / int / string */
        pw_u8 sbuf[256];
        size_t soff = 0;
        soff = put_u32_pod(sbuf, soff, PW_POD_BOOL, 1u);
        soff = put_u32_pod(sbuf, soff, PW_POD_ID, 0x12345678u);
        soff = put_u32_pod(sbuf, soff, PW_POD_INT, 0xfedcba98u);
        soff = put_pod(sbuf, soff, PW_POD_STRING, "hello", 6u);
        soff = put_pod(sbuf, soff, PW_POD_NONE, NULL, 0u);
        soff = put_pod(sbuf, soff, PW_POD_STRING, "ab\0cd", 6u); /* embedded NUL */
        soff = put_pod(sbuf, soff, PW_POD_STRING, "", 1u);       /* empty string */

        struct pwire_pod_view sp = {PW_POD_STRUCT, (pw_u32)soff, sbuf};
        struct pwire_pod_members m;
        CHECK(pwire_pod_struct(NULL, &m) == -1, "struct NULL pod");
        CHECK(pwire_pod_struct(&sp, NULL) == -1, "struct NULL m");
        struct pwire_pod_view bad_sp = {PW_POD_INT, (pw_u32)soff, sbuf};
        CHECK(pwire_pod_struct(&bad_sp, &m) == -1 && m.bad == 1, "struct wrong type");
        CHECK(pwire_pod_struct(&sp, &m) == 0 && m.bad == 0, "struct ok");

        CHECK(pwire_pod_int(&m) == 1u && !m.bad, "int BOOL");
        CHECK(pwire_pod_int(&m) == 0x12345678u && !m.bad, "int ID");
        CHECK(pwire_pod_int(&m) == 0xfedcba98u && !m.bad, "int INT");

        const char *str = NULL;
        pw_i64 slen = -1;
        CHECK(pwire_pod_string(&m, &str, &slen) == 1 && slen == 5 && strcmp(str, "hello") == 0 && !m.bad, "string hello");
        CHECK(pwire_pod_string(&m, &str, &slen) == 0 && str == NULL && slen == 0 && !m.bad, "string NONE");
        CHECK(pwire_pod_string(&m, &str, &slen) == 1 && slen == 2 && memcmp(str, "ab", 2) == 0 && !m.bad, "string embedded NUL");
        CHECK(pwire_pod_string(&m, &str, &slen) == 1 && slen == 0 && str && str[0] == '\0' && !m.bad, "string empty");
        /* Past end sets bad */
        CHECK(pwire_pod_member(&m, &pod) == -1 && m.bad == 1 && pod.type == 0, "member EOF");
        CHECK(pwire_pod_int(&m) == 0 && m.bad == 1, "int after bad");
        CHECK(pwire_pod_string(&m, &str, &slen) == -1 && m.bad == 1, "string after bad");

        /* Non-NUL-terminated & zero-length string pods */
        pw_u8 badstr[64];
        size_t boff = put_pod(badstr, 0, PW_POD_STRING, "no_nul", 6u);
        sp = (struct pwire_pod_view){PW_POD_STRUCT, (pw_u32)boff, badstr};
        CHECK(pwire_pod_struct(&sp, &m) == 0, "badstr struct");
        CHECK(pwire_pod_string(&m, &str, &slen) == -1 && m.bad == 1 && str == NULL && slen == 0, "non-NUL string rejected");

        boff = put_pod(badstr, 0, PW_POD_STRING, NULL, 0u);
        sp = (struct pwire_pod_view){PW_POD_STRUCT, (pw_u32)boff, badstr};
        CHECK(pwire_pod_struct(&sp, &m) == 0, "zerostr struct");
        CHECK(pwire_pod_string(&m, &str, &slen) == -1 && m.bad == 1, "zero-len string rejected");

        /* Int with size < 4 and wrong type */
        boff = put_pod(badstr, 0, PW_POD_INT, "abc", 3u);
        sp = (struct pwire_pod_view){PW_POD_STRUCT, (pw_u32)boff, badstr};
        CHECK(pwire_pod_struct(&sp, &m) == 0, "shortint struct");
        CHECK(pwire_pod_int(&m) == 0 && m.bad == 1, "short int rejected");

        /* Choice values, value32 overflow, allows */
        pw_u8 cbuf[128];
        const pw_u32 rng[] = {10u, 5u, 20u};
        size_t clen = put_choice_u32(cbuf, 0, PW_CHOICE_RANGE, PW_POD_INT, rng, 3);
        at = 0;
        CHECK(pwire_pod_next(cbuf, (pw_i64)clen, &at, &pod) == 0, "choice next");
        struct pwire_pod_values v;
        CHECK(pwire_pod_values(&pod, &v) == 0 && v.choice == PW_CHOICE_RANGE && v.type == PW_POD_INT && v.size == 4 && v.count == 3, "choice values");
        CHECK(pwire_pod_value32(&v, 0, 0) == 10u && pwire_pod_value32(&v, 1, 0) == 5u && pwire_pod_value32(&v, 2, 0) == 20u, "choice value32");
        CHECK(pwire_pod_value32(&v, 3, 0) == 0u, "value32 k OOB");
        CHECK(pwire_pod_value32(&v, 0, 1) == 0u, "value32 word OOB");
        CHECK(pwire_pod_value32(&v, 0, 0x3fffffffu) == 0u, "value32 word 0x3fffffff overflow");
        CHECK(pwire_pod_value32(&v, 0, 0x40000000u) == 0u, "value32 word 0x40000000 overflow");
        CHECK(pwire_pod_value32(&v, 0, 0xffffffffu) == 0u, "value32 word 0xffffffff overflow");
        CHECK(pwire_pod_allows(&v, 4u) == 0 && pwire_pod_allows(&v, 5u) == 1 && pwire_pod_allows(&v, 12u) == 1 &&
              pwire_pod_allows(&v, 20u) == 1 && pwire_pod_allows(&v, 21u) == 0, "allows RANGE");

        const pw_u32 enm[] = {99u, 8u, 12u, 16u}; /* default=99 skipped when count>1 */
        clen = put_choice_u32(cbuf, 0, PW_CHOICE_ENUM, PW_POD_ID, enm, 4);
        at = 0;
        CHECK(pwire_pod_next(cbuf, (pw_i64)clen, &at, &pod) == 0 && pwire_pod_values(&pod, &v) == 0, "enum values");
        CHECK(pwire_pod_allows(&v, 99u) == 0 && pwire_pod_allows(&v, 8u) == 1 && pwire_pod_allows(&v, 12u) == 1 &&
              pwire_pod_allows(&v, 16u) == 1 && pwire_pod_allows(&v, 0u) == 0, "allows ENUM");

        const pw_u32 flg[] = {0x55u};
        clen = put_choice_u32(cbuf, 0, PW_CHOICE_FLAGS, PW_POD_INT, flg, 1);
        at = 0;
        CHECK(pwire_pod_next(cbuf, (pw_i64)clen, &at, &pod) == 0 && pwire_pod_values(&pod, &v) == 0, "flags values");
        CHECK(pwire_pod_allows(&v, 0x01u) == 1 && pwire_pod_allows(&v, 0x55u) == 1 &&
              pwire_pod_allows(&v, 0x02u) == 0 && pwire_pod_allows(&v, 0u) == 1, "allows FLAGS");

        /* Choice esize == 0 and esize > avail */
        pw_u8 badc[24];
        memset(badc, 0, sizeof(badc));
        ref_pwire_pod_put32(badc, PW_CHOICE_ENUM);
        ref_pwire_pod_put32(badc + 8, 0u); /* esize == 0 */
        struct pwire_pod_view badcp = {PW_POD_CHOICE, 24u, badc};
        CHECK(pwire_pod_values(&badcp, &v) == -1, "choice esize=0 rejected");
        ref_pwire_pod_put32(badc + 8, 12u); /* esize > 24 - 16 = 8 */
        CHECK(pwire_pod_values(&badcp, &v) == -1, "choice esize>avail rejected");

        /* Object + properties */
        pw_u8 obuf[256];
        ref_pwire_pod_put32(obuf, 0x40003u); /* type */
        ref_pwire_pod_put32(obuf + 4, 3u);   /* id */
        size_t ooff = 8;
        ref_pwire_pod_put32(obuf + ooff, 1u); ref_pwire_pod_put32(obuf + ooff + 4, 0u); ooff += 8;
        ooff = put_u32_pod(obuf, ooff, PW_POD_ID, 2u);
        ref_pwire_pod_put32(obuf + ooff, 2u); ref_pwire_pod_put32(obuf + ooff + 4, 0u); ooff += 8;
        ooff = put_choice_u32(obuf, ooff, PW_CHOICE_ENUM, PW_POD_ID, enm, 4);

        struct pwire_pod_view op = {PW_POD_OBJECT, (pw_u32)ooff, obuf};
        struct pwire_pod_props props;
        CHECK(pwire_pod_object(NULL, &props) == -1, "object NULL");
        CHECK(pwire_pod_object(&op, &props) == 0 && props.type == 0x40003u && props.id == 3u, "object ok");
        pw_u32 key = 0;
        CHECK(pwire_pod_prop(&props, &key, &pod) == 1 && key == 1u && pod.type == PW_POD_ID, "prop 1");
        CHECK(pwire_pod_prop(&props, &key, &pod) == 1 && key == 2u && pod.type == PW_POD_CHOICE, "prop 2");
        CHECK(pwire_pod_prop(&props, &key, &pod) == 0, "prop EOF");
}

/* ── 2. mmap Guard-Page Tests (PROT_NONE before AND after) ──────────────── */

static void test_guard_pages(void)
{
        long ps = sysconf(_SC_PAGESIZE);
        if (ps <= 0) ps = 4096;
        size_t psz = (size_t)ps;
        pw_u8 *region = mmap(NULL, psz * 3, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
        CHECK(region != MAP_FAILED, "mmap 3 pages");
        if (region == MAP_FAILED) return;
        CHECK(mprotect(region + psz, psz, PROT_READ | PROT_WRITE) == 0, "mprotect middle page");
        pw_u8 *lo = region + psz;
        pw_u8 *hi = region + psz * 2;

        /* 1. get32 / put32 flush against BOTH guard pages */
        pwire_pod_put32(lo, 0x11223344u);
        CHECK(pwire_pod_get32(lo) == 0x11223344u, "guard lo get32");
        pwire_pod_put32(hi - 4, 0x55667788u);
        CHECK(pwire_pod_get32(hi - 4) == 0x55667788u, "guard hi get32");

        /* 2. pwire_pod_next truncated & valid & evil sizes flush at right guard page */
        for (size_t len = 0; len <= 16; len++) {
                pw_u8 *p = hi - len;
                if (len >= 8) {
                        pwire_pod_put32(p, (pw_u32)(len - 8));
                        pwire_pod_put32(p + 4, PW_POD_INT);
                }
                pw_i64 at = 0;
                struct pwire_pod_view pod;
                pw_i32 rc = pwire_pod_next(p, (pw_i64)len, &at, &pod);
                CHECK(rc == (len >= 8 ? 0 : -1), "guard hi next len=%zu", len);
                if (len >= 8) {
                        /* Now set evil size 0xffffffff flush at hi */
                        pwire_pod_put32(p, 0xffffffffu);
                        at = 0;
                        CHECK(pwire_pod_next(p, (pw_i64)len, &at, &pod) == -1, "guard hi evil 0xffffffff");
                }
        }

        /* 3. pwire_pod_string flush at right guard page (valid & non-NUL) */
        pw_u8 *sp_hi = hi - 13; /* 8 header + 5 bytes "abcd\0" */
        pwire_pod_put32(sp_hi, 5u);
        pwire_pod_put32(sp_hi + 4, PW_POD_STRING);
        memcpy(sp_hi + 8, "abcd", 5);
        struct pwire_pod_members m = {sp_hi, 13, 0, 0};
        const char *s = NULL;
        pw_i64 slen = 0;
        CHECK(pwire_pod_string(&m, &s, &slen) == 1 && slen == 4 && strcmp(s, "abcd") == 0, "guard hi string ok");
        sp_hi[12] = 'X'; /* remove NUL right before PROT_NONE! */
        m = (struct pwire_pod_members){sp_hi, 13, 0, 0};
        CHECK(pwire_pod_string(&m, &s, &slen) == -1 && m.bad == 1, "guard hi non-NUL string safe");

        /* 4. Choice + value32 + allows flush at right guard page */
        pw_u8 *cp_hi = hi - 28; /* 16 choice hdr + 3*4 bytes */
        pwire_pod_put32(cp_hi, PW_CHOICE_RANGE);
        pwire_pod_put32(cp_hi + 4, 0u);
        pwire_pod_put32(cp_hi + 8, 4u);
        pwire_pod_put32(cp_hi + 12, PW_POD_INT);
        pwire_pod_put32(cp_hi + 16, 10u);
        pwire_pod_put32(cp_hi + 20, 5u);
        pwire_pod_put32(cp_hi + 24, 20u);
        struct pwire_pod_view cp = {PW_POD_CHOICE, 28u, cp_hi};
        struct pwire_pod_values v;
        CHECK(pwire_pod_values(&cp, &v) == 0 && v.count == 3, "guard hi choice");
        CHECK(pwire_pod_value32(&v, 2, 0) == 20u, "guard hi value32 last");
        CHECK(pwire_pod_value32(&v, 3, 0) == 0u, "guard hi value32 OOB");
        CHECK(pwire_pod_value32(&v, 0, 0xffffffffu) == 0u, "guard hi value32 word wrap");
        CHECK(pwire_pod_allows(&v, 15u) == 1 && pwire_pod_allows(&v, 21u) == 0, "guard hi allows");

        /* 5. Object + prop flush at left and right guard pages */
        pw_u8 *op_hi = hi - 28; /* 8 obj hdr + 8 prop key + 12 int pod */
        pwire_pod_put32(op_hi, 0x40003u);
        pwire_pod_put32(op_hi + 4, 4u);
        pwire_pod_put32(op_hi + 8, 7u);
        pwire_pod_put32(op_hi + 12, 0u);
        pwire_pod_put32(op_hi + 16, 4u);
        pwire_pod_put32(op_hi + 20, PW_POD_INT);
        pwire_pod_put32(op_hi + 24, 0xabcdef01u);
        struct pwire_pod_view op = {PW_POD_OBJECT, 28u, op_hi};
        struct pwire_pod_props props;
        pw_u32 key = 0;
        struct pwire_pod_view val;
        CHECK(pwire_pod_object(&op, &props) == 0, "guard hi object");
        CHECK(pwire_pod_prop(&props, &key, &val) == 1 && key == 7u && pwire_pod_get32(val.body) == 0xabcdef01u, "guard hi prop");
        CHECK(pwire_pod_prop(&props, &key, &val) == 0, "guard hi prop EOF");

        /* Left guard page copy */
        memcpy(lo, op_hi, 28);
        op = (struct pwire_pod_view){PW_POD_OBJECT, 28u, lo};
        CHECK(pwire_pod_object(&op, &props) == 0 && pwire_pod_prop(&props, &key, &val) == 1 && key == 7u, "guard lo object+prop");

        munmap(region, psz * 3);
}

/* ── 3. Randomized Differential Fuzzing (200,000+ ops) ──────────────────── */

static uint64_t g_rng = 0x9e3779b97f4a7c15ULL;
static uint64_t xorshift64(void)
{
        uint64_t x = g_rng;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        return g_rng = x;
}

static void test_fuzz(void)
{
        pw_u8 buf[256];
        const pw_u32 types[] = {0, PW_POD_NONE, PW_POD_BOOL, PW_POD_ID, PW_POD_INT, PW_POD_STRING,
                                PW_POD_STRUCT, PW_POD_OBJECT, PW_POD_CHOICE, 20, 0xffffffffu};
        const pw_u32 choices[] = {0, 1, 2, 3, 4, 5, 0xffffffffu};

        for (int iter = 0; iter < 50000; iter++) {
                size_t len = (size_t)(xorshift64() % sizeof(buf));
                for (size_t i = 0; i < len; i++)
                        buf[i] = (pw_u8)xorshift64();

                /* Inject structured headers ~50% of the time */
                if ((iter & 1) && len >= 8) {
                        pw_u32 sz = (pw_u32)(xorshift64() % (len + 16));
                        if ((iter % 17) == 0) sz = 0xfffffff8u + (pw_u32)(xorshift64() & 7u);
                        ref_pwire_pod_put32(buf, sz);
                        ref_pwire_pod_put32(buf + 4, types[xorshift64() % (sizeof(types)/sizeof(types[0]))]);
                        if (len >= 24 && (iter % 5) == 0) {
                                ref_pwire_pod_put32(buf + 8, choices[xorshift64() % (sizeof(choices)/sizeof(choices[0]))]);
                                ref_pwire_pod_put32(buf + 16, (pw_u32)(xorshift64() % 16));
                                ref_pwire_pod_put32(buf + 20, types[xorshift64() % (sizeof(types)/sizeof(types[0]))]);
                        }
                }

                /* Fuzz pwire_pod_next */
                pw_i64 at_a = (pw_i64)(xorshift64() % (len + 8)) - 2;
                pw_i64 at_r = at_a;
                struct pwire_pod_view pa = {0xaa, 0xbb, NULL}, pr = {0xaa, 0xbb, NULL};
                pw_i32 ra = pwire_pod_next(buf, (pw_i64)len, &at_a, &pa);
                pw_i32 rr = ref_pwire_pod_next(buf, (pw_i64)len, &at_r, &pr);
                CHECK(ra == rr && at_a == at_r && pa.type == pr.type && pa.size == pr.size && pa.body == pr.body,
                      "fuzz next iter=%d", iter);

                /* Fuzz struct + member + int + string */
                struct pwire_pod_view sp = {types[xorshift64() % (sizeof(types)/sizeof(types[0]))], (pw_u32)len, buf};
                struct pwire_pod_members ma = {0}, mr = {0};
                ra = pwire_pod_struct(&sp, &ma);
                rr = ref_pwire_pod_struct(&sp, &mr);
                CHECK(ra == rr && ma.p == mr.p && ma.n == mr.n && ma.at == mr.at && ma.bad == mr.bad, "fuzz struct iter=%d", iter);

                sp.type = PW_POD_STRUCT;
                pwire_pod_struct(&sp, &ma);
                ref_pwire_pod_struct(&sp, &mr);
                for (int step = 0; step < 3; step++) {
                        unsigned mode = (unsigned)(xorshift64() % 3);
                        if (mode == 0) {
                                struct pwire_pod_view oa, or_;
                                ra = pwire_pod_member(&ma, &oa);
                                rr = ref_pwire_pod_member(&mr, &or_);
                                CHECK(ra == rr && oa.type == or_.type && oa.size == or_.size && oa.body == or_.body &&
                                      ma.at == mr.at && ma.bad == mr.bad, "fuzz member iter=%d", iter);
                        } else if (mode == 1) {
                                pw_u32 ia = pwire_pod_int(&ma);
                                pw_u32 ir = ref_pwire_pod_int(&mr);
                                CHECK(ia == ir && ma.at == mr.at && ma.bad == mr.bad, "fuzz int iter=%d", iter);
                        } else {
                                const char *sa = NULL, *sr = NULL;
                                pw_i64 la = -1, lr = -1;
                                ra = pwire_pod_string(&ma, &sa, &la);
                                rr = ref_pwire_pod_string(&mr, &sr, &lr);
                                CHECK(ra == rr && sa == sr && la == lr && ma.at == mr.at && ma.bad == mr.bad,
                                      "fuzz string iter=%d", iter);
                        }
                }

                /* Fuzz values + value32 + allows */
                struct pwire_pod_view vp = {(iter & 3) ? PW_POD_CHOICE : types[xorshift64() % 6], (pw_u32)len, buf};
                struct pwire_pod_values va = {0}, vr = {0};
                ra = pwire_pod_values(&vp, &va);
                rr = ref_pwire_pod_values(&vp, &vr);
                CHECK(ra == rr && memcmp(&va, &vr, sizeof(va)) == 0, "fuzz values iter=%d", iter);
                if (ra == 0) {
                        pw_u32 k = (pw_u32)(xorshift64() % (va.count + 3));
                        pw_u32 w = (iter % 11 == 0) ? (0x3fffffffu + (pw_u32)(xorshift64() & 3u)) : (pw_u32)(xorshift64() % 4);
                        CHECK(pwire_pod_value32(&va, k, w) == ref_pwire_pod_value32(&vr, k, w), "fuzz value32 iter=%d", iter);
                        pw_u32 x = (pw_u32)xorshift64();
                        if ((iter & 1) && va.count > 0 && va.size >= 4)
                                x = ref_pwire_pod_value32(&vr, (pw_u32)(xorshift64() % va.count), 0);
                        CHECK(pwire_pod_allows(&va, x) == ref_pwire_pod_allows(&vr, x), "fuzz allows iter=%d", iter);
                }

                /* Fuzz object + prop */
                struct pwire_pod_view op = {PW_POD_OBJECT, (pw_u32)len, buf};
                struct pwire_pod_props oa = {0}, or_ = {0};
                ra = pwire_pod_object(&op, &oa);
                rr = ref_pwire_pod_object(&op, &or_);
                CHECK(ra == rr && memcmp(&oa, &or_, sizeof(oa)) == 0, "fuzz object iter=%d", iter);
                if (ra == 0) {
                        for (int step = 0; step < 3; step++) {
                                pw_u32 ka = 0x55, kr = 0x55;
                                struct pwire_pod_view pva = {0}, pvr = {0};
                                ra = pwire_pod_prop(&oa, &ka, &pva);
                                rr = ref_pwire_pod_prop(&or_, &kr, &pvr);
                                CHECK(ra == rr && ka == kr && oa.at == or_.at &&
                                      pva.type == pvr.type && pva.size == pvr.size && pva.body == pvr.body,
                                      "fuzz prop iter=%d", iter);
                                if (ra <= 0) break;
                        }
                }
        }
}

/* ── 4. Realistic PipeWire SPA Pod Benchmark ────────────────────────────── */

static uint64_t now_ns(void)
{
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static volatile uint64_t g_sink = 0;

static void run_bench(void)
{
        /* Build a realistic PipeWire SPA Format Object + Client Struct message */
        pw_u8 msg[512];
        size_t ooff = 8;
        ref_pwire_pod_put32(msg, 0x40003u); /* OBJECT_FORMAT */
        ref_pwire_pod_put32(msg + 4, 3u);   /* PARAM_ENUM_FORMAT */
        /* prop 1: media_type = VIDEO */
        ref_pwire_pod_put32(msg + ooff, 1u); ref_pwire_pod_put32(msg + ooff + 4, 0u); ooff += 8;
        ooff = put_u32_pod(msg, ooff, PW_POD_ID, 2u);
        /* prop 2: media_subtype = RAW */
        ref_pwire_pod_put32(msg + ooff, 2u); ref_pwire_pod_put32(msg + ooff + 4, 0u); ooff += 8;
        ooff = put_u32_pod(msg, ooff, PW_POD_ID, 1u);
        /* prop 3: format = ENUM(BGRX, BGRA, RGBx, RGBA) */
        ref_pwire_pod_put32(msg + ooff, 0x20001u); ref_pwire_pod_put32(msg + ooff + 4, 0u); ooff += 8;
        const pw_u32 fmts[] = {8u, 7u, 9u, 11u, 12u, 8u};
        ooff = put_choice_u32(msg, ooff, PW_CHOICE_ENUM, PW_POD_ID, fmts, 6);
        /* prop 4: buffers = RANGE(2, 1, 64) */
        ref_pwire_pod_put32(msg + ooff, 1u); ref_pwire_pod_put32(msg + ooff + 4, 0u); ooff += 8;
        const pw_u32 rng[] = {4u, 2u, 64u};
        ooff = put_choice_u32(msg, ooff, PW_CHOICE_RANGE, PW_POD_INT, rng, 3);

        pw_u8 sbuf[128];
        size_t soff = 0;
        soff = put_u32_pod(sbuf, soff, PW_POD_INT, 3u);
        soff = put_pod(sbuf, soff, PW_POD_STRING, "client-node", 12u);
        soff = put_pod(sbuf, soff, PW_POD_STRING, "PipeWire:Interface:ClientNode", 30u);
        soff = put_u32_pod(sbuf, soff, PW_POD_INT, 4u);

        struct pwire_pod_view op = {PW_POD_OBJECT, (pw_u32)ooff, msg};
        struct pwire_pod_view sp = {PW_POD_STRUCT, (pw_u32)soff, sbuf};

        const int N = 2000000;
        uint64_t acc_a = 0, acc_r = 0;

        /* Warmup */
        for (int i = 0; i < 10000; i++) {
                struct pwire_pod_props pr;
                pwire_pod_object(&op, &pr);
                pw_u32 k; struct pwire_pod_view pv;
                while (pwire_pod_prop(&pr, &k, &pv) > 0) {
                        struct pwire_pod_values v;
                        if (pwire_pod_values(&pv, &v) == 0)
                                acc_a += (uint64_t)pwire_pod_allows(&v, 8u);
                }
        }

        uint64_t t0 = now_ns();
        for (int i = 0; i < N; i++) {
                struct pwire_pod_props pr;
                if (pwire_pod_object(&op, &pr) == 0) {
                        pw_u32 k; struct pwire_pod_view pv;
                        while (pwire_pod_prop(&pr, &k, &pv) > 0) {
                                struct pwire_pod_values v;
                                if (pwire_pod_values(&pv, &v) == 0)
                                        acc_a += (uint64_t)pwire_pod_allows(&v, 8u) + pwire_pod_value32(&v, 0, 0);
                        }
                }
                struct pwire_pod_members m;
                if (pwire_pod_struct(&sp, &m) == 0) {
                        const char *s1, *s2; pw_i64 l1, l2;
                        acc_a += pwire_pod_int(&m);
                        pwire_pod_string(&m, &s1, &l1);
                        pwire_pod_string(&m, &s2, &l2);
                        acc_a += (uint64_t)l1 + (uint64_t)l2 + pwire_pod_int(&m);
                }
        }
        uint64_t dt_a = now_ns() - t0;

        uint64_t t1 = now_ns();
        for (int i = 0; i < N; i++) {
                struct pwire_pod_props pr;
                if (ref_pwire_pod_object(&op, &pr) == 0) {
                        pw_u32 k; struct pwire_pod_view pv;
                        while (ref_pwire_pod_prop(&pr, &k, &pv) > 0) {
                                struct pwire_pod_values v;
                                if (ref_pwire_pod_values(&pv, &v) == 0)
                                        acc_r += (uint64_t)ref_pwire_pod_allows(&v, 8u) + ref_pwire_pod_value32(&v, 0, 0);
                        }
                }
                struct pwire_pod_members m;
                if (ref_pwire_pod_struct(&sp, &m) == 0) {
                        const char *s1, *s2; pw_i64 l1, l2;
                        acc_r += ref_pwire_pod_int(&m);
                        ref_pwire_pod_string(&m, &s1, &l1);
                        ref_pwire_pod_string(&m, &s2, &l2);
                        acc_r += (uint64_t)l1 + (uint64_t)l2 + ref_pwire_pod_int(&m);
                }
        }
        uint64_t dt_r = now_ns() - t1;

        g_sink = acc_a ^ acc_r;
        double ns_a = (double)dt_a / (double)N;
        double ns_r = (double)dt_r / (double)N;
        printf("bench: ASM=%.2f ns/op  C-O2=%.2f ns/op  speedup=%.2fx\n", ns_a, ns_r, ns_r / ns_a);
}

int main(void)
{
        test_edge_cases();
        test_guard_pages();
        test_fuzz();
        printf("pwire_pod: %u checks, %u failures\n", g_checks, g_fails);
        if (g_fails) return 1;
        run_bench();
        return 0;
}
