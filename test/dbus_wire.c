/*
        Differential C-reference parity, security, guard-page, fuzz & throughput/size
        benchmark for src/dbus/dbus_wire.asm.c (arm64, x86-64, riscv64 pure-ASM).

        Build & run:
          cc -O2 -Wall -Werror test/dbus_wire.c -o /tmp/mw_dbus_wire && /tmp/mw_dbus_wire
          cc -O2 -fsanitize=address,undefined -Wall -Werror test/dbus_wire.c -o /tmp/mw_dbus_wire_asan && /tmp/mw_dbus_wire_asan
*/
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>

#define DBUS_WIRE_ASM_IMPLEMENTATION
#include "../src/dbus/dbus_wire.asm.c"

/* ── Reference C implementations (noinline for exact .text size comparison) ── */

#define REF_ALIGN(at, n) (((at) + (n) - 1) & ~((dbus_i64)(n) - 1))

static __attribute__((noinline)) dbus_i32 ref_dbus_wire_utf8_valid(const unsigned char *s, dbus_i64 n)
{
        if (n == 0) return 1;
        if (n < 0 || !s) return 0;
        for (dbus_i64 at = 0; at < n;) {
                unsigned char c = s[at];
                dbus_i64 more = c < 0x80 ? 0 : c >= 0xc2 && c < 0xe0 ? 1 : c >= 0xe0 && c < 0xf0 ? 2 : c >= 0xf0 && c < 0xf5 ? 3 : -1;
                dbus_u64 code;
                if (!c || more < 0 || at + more >= n + (more == 0)) return 0;
                if (!more) { at++; continue; }
                code = more == 1 ? c & 0x1fu : more == 2 ? c & 0x0fu : c & 0x07u;
                for (dbus_i64 k = 1; k <= more; k++) {
                        if ((s[at + k] & 0xc0) != 0x80) return 0;
                        code = (code << 6) | (s[at + k] & 0x3fu);
                }
                if ((more == 2 && (code < 0x800 || (code >= 0xd800 && code < 0xe000))) ||
                    (more == 3 && (code < 0x10000 || code > 0x10ffff)))
                        return 0;
                at += more + 1;
        }
        return 1;
}

static int ref_letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static int ref_digit(char c)  { return c >= '0' && c <= '9'; }

static __attribute__((noinline)) dbus_i32 ref_dbus_wire_valid_path(const char *s, dbus_i64 n)
{
        if (!s || n < 1 || n > 255 || s[0] != '/') return 0;
        if (n == 1) return 1;
        for (dbus_i64 at = 1, start = 1; at <= n; at++) {
                if (at == n || s[at] == '/') {
                        if (at == start) return 0;
                        start = at + 1;
                } else if (!ref_letter(s[at]) && !ref_digit(s[at])) {
                        return 0;
                }
        }
        return s[n - 1] != '/';
}

static __attribute__((noinline)) dbus_i32 ref_dbus_wire_valid_name(const char *s, dbus_i64 n, dbus_i32 kind)
{
        if (!s || n < 1 || n > 255) return 0;
        if (kind == 2) {
                if (!ref_letter(s[0])) return 0;
                for (dbus_i64 at = 1; at < n; at++)
                        if (!ref_letter(s[at]) && !ref_digit(s[at])) return 0;
                return 1;
        }
        dbus_i64 dots = 0, start = 0;
        int unique = kind == 1 && s[0] == ':';
        if (unique) start = 1;
        for (dbus_i64 at = start; at <= n; at++) {
                if (at == n || s[at] == '.') {
                        if (at == start) return 0;
                        start = at + 1;
                        dots += at < n;
                } else {
                        int first = at == start;
                        int dash = kind == 1 && s[at] == '-';
                        if (!ref_letter(s[at]) && !dash && !(ref_digit(s[at]) && (!first || unique)))
                                return 0;
                }
        }
        return dots >= 1 && s[n - 1] != '.';
}

static __attribute__((noinline)) dbus_i32 ref_dbus_wire_sig_one(const char *s, dbus_i64 n, dbus_i64 *at, dbus_i32 arrays, dbus_i32 structs, dbus_i32 dicts)
{
        if (!s || !at || *at < 0 || *at >= n || arrays > 32 || structs > 32 || dicts > 32) return 0;
        char c = s[(*at)++];
        switch (c) {
        case 'y': case 'b': case 'n': case 'q': case 'i': case 'u': case 'x': case 't': case 'd':
        case 's': case 'o': case 'g': case 'h': case 'v':
                return 1;
        case 'a':
                if (*at < n && s[*at] == '{') {
                        (*at)++;
                        if (*at >= n) return 0;
                        char k = s[(*at)++];
                        if (!(k=='y'||k=='b'||k=='n'||k=='q'||k=='i'||k=='u'||k=='x'||k=='t'||k=='d'||k=='s'||k=='o'||k=='g'||k=='h'))
                                return 0;
                        if (!ref_dbus_wire_sig_one(s, n, at, arrays + 1, structs, dicts + 1) || *at >= n || s[(*at)++] != '}')
                                return 0;
                        return 1;
                }
                return ref_dbus_wire_sig_one(s, n, at, arrays + 1, structs, dicts);
        case '(': {
                dbus_i64 members = 0;
                while (*at < n && s[*at] != ')') {
                        if (!ref_dbus_wire_sig_one(s, n, at, arrays, structs + 1, dicts)) return 0;
                        members++;
                }
                if (*at >= n || !members) return 0;
                (*at)++;
                return 1;
        }
        default:
                return 0;
        }
}

static __attribute__((noinline)) dbus_i32 ref_dbus_wire_valid_signature(const char *s, dbus_i64 n)
{
        if (n == 0) return 1;
        if (n < 0 || n > 255 || !s) return 0;
        dbus_i64 at = 0;
        while (at < n)
                if (!ref_dbus_wire_sig_one(s, n, &at, 0, 0, 0)) return 0;
        return 1;
}

static __attribute__((noinline)) dbus_i64 ref_dbus_wire_variant_sig(const unsigned char *p, dbus_i64 *at, dbus_i64 limit)
{
        if (!p || !at || *at < 0 || limit - *at < 1) return -1;
        dbus_i64 length = p[(*at)++];
        dbus_i64 start = *at, inner = 0;
        if (limit - *at <= length || p[*at + length] ||
            !ref_dbus_wire_sig_one((const char *)p + start, length, &inner, 0, 0, 0) || inner != length)
                return -1;
        *at += length + 1;
        return length;
}

static __attribute__((noinline)) dbus_i64 ref_dbus_wire_fixed_size(dbus_i32 c)
{
        return c == 'y' ? 1 : (c == 'n' || c == 'q') ? 2 :
               (c == 'b' || c == 'i' || c == 'u' || c == 'h') ? 4 :
               (c == 'x' || c == 't' || c == 'd') ? 8 : 0;
}

static __attribute__((noinline)) dbus_i32 ref_dbus_wire_pad_zero(const unsigned char *p, dbus_i64 *at, dbus_i64 limit, dbus_i64 align)
{
        if (!p || !at || *at < 0) return 0;
        dbus_i64 to = REF_ALIGN(*at, align);
        if (to > limit) return 0;
        for (dbus_i64 k = *at; k < to; k++)
                if (p[k]) return 0;
        *at = to;
        return 1;
}

static __attribute__((noinline)) dbus_u32 ref_dbus_wire_get32(const unsigned char *p, dbus_i32 big_endian)
{
        if (big_endian)
                return ((dbus_u32)p[0] << 24) | ((dbus_u32)p[1] << 16) | ((dbus_u32)p[2] << 8) | (dbus_u32)p[3];
        return (dbus_u32)p[0] | ((dbus_u32)p[1] << 8) | ((dbus_u32)p[2] << 16) | ((dbus_u32)p[3] << 24);
}

static __attribute__((noinline)) dbus_i32 ref_dbus_wire_value(struct dbus_wire_cursor *c)
{
        if (!c || !c->p || !c->sig || c->at < 0 || c->at > c->limit || c->s < 0 || c->s >= c->sig_len || c->depth > 64)
                return 0;
        unsigned char ch = (unsigned char)c->sig[c->s++];
        dbus_i64 size = ref_dbus_wire_fixed_size(ch);
        if (size) {
                if (!ref_dbus_wire_pad_zero(c->p, &c->at, c->limit, size) || c->limit - c->at < size)
                        return 0;
                if (ch == 'b' && ref_dbus_wire_get32(c->p + c->at, c->big_endian) > 1) return 0;
                if (ch == 'h' && (dbus_u64)ref_dbus_wire_get32(c->p + c->at, c->big_endian) >= c->fds) return 0;
                c->at += size;
                return 1;
        }
        switch (ch) {
        case 's':
        case 'o': {
                if (!ref_dbus_wire_pad_zero(c->p, &c->at, c->limit, 4) || c->limit - c->at < 4) return 0;
                dbus_i64 length = (dbus_i64)(dbus_u64)ref_dbus_wire_get32(c->p + c->at, c->big_endian);
                c->at += 4;
                if (c->limit - c->at <= length || c->p[c->at + length] ||
                    !ref_dbus_wire_utf8_valid(c->p + c->at, length) ||
                    (ch == 'o' && !ref_dbus_wire_valid_path((const char *)c->p + c->at, length)))
                        return 0;
                c->at += length + 1;
                return 1;
        }
        case 'g': {
                if (c->limit - c->at < 1) return 0;
                dbus_i64 length = c->p[c->at++];
                if (c->limit - c->at <= length || c->p[c->at + length] ||
                    !ref_dbus_wire_valid_signature((const char *)c->p + c->at, length))
                        return 0;
                c->at += length + 1;
                return 1;
        }
        case 'v': {
                dbus_i64 start = c->at + 1;
                dbus_i64 length = ref_dbus_wire_variant_sig(c->p, &c->at, c->limit);
                if (length < 0) return 0;
                const char *old_sig = c->sig;
                dbus_i64 old_len = c->sig_len, old_s = c->s;
                dbus_i32 old_d = c->depth;
                c->sig = (const char *)c->p + start; c->sig_len = length; c->s = 0; c->depth = old_d + 1;
                dbus_i32 ok = ref_dbus_wire_value(c);
                c->sig = old_sig; c->sig_len = old_len; c->s = old_s; c->depth = old_d;
                return ok;
        }
        case 'a': {
                dbus_i64 element = c->s;
                if (element >= c->sig_len) return 0;
                if (!ref_dbus_wire_pad_zero(c->p, &c->at, c->limit, 4) || c->limit - c->at < 4) return 0;
                dbus_i64 length = (dbus_i64)(dbus_u64)ref_dbus_wire_get32(c->p + c->at, c->big_endian);
                c->at += 4;
                dbus_i64 counted = c->at;
                unsigned char first = (unsigned char)c->sig[element];
                dbus_i64 align = (first == '{' || first == '(') ? 8 :
                                 ref_dbus_wire_fixed_size(first) ? ref_dbus_wire_fixed_size(first) :
                                 (first == 's' || first == 'o' || first == 'a') ? 4 : 1;
                if (!ref_dbus_wire_pad_zero(c->p, &c->at, c->limit, align) || length > 67108864 || length > c->limit - c->at)
                        return 0;
                dbus_i64 stop = c->at + length;
                dbus_i64 after = element - 1;
                if (!ref_dbus_wire_sig_one(c->sig, c->sig_len, &after, 0, 0, 0)) return 0;
                dbus_i64 old_limit = c->limit, previous = 0, elements = 0;
                dbus_i32 old_depth = c->depth;
                c->limit = stop;
                while (c->at < stop) {
                        previous = c->at;
                        elements++;
                        if (first == '{') {
                                if (!ref_dbus_wire_pad_zero(c->p, &c->at, stop, 8)) { c->limit = old_limit; c->depth = old_depth; return 0; }
                                c->s = element + 1;
                                c->depth = old_depth + 2;
                                if (!ref_dbus_wire_value(c) || !ref_dbus_wire_value(c)) { c->limit = old_limit; c->depth = old_depth; return 0; }
                        } else {
                                c->s = element;
                                c->depth = old_depth + 1;
                                if (!ref_dbus_wire_value(c)) { c->limit = old_limit; c->depth = old_depth; return 0; }
                        }
                }
                c->limit = old_limit; c->depth = old_depth;
                if (c->at != stop || ((first == '(' || first == '{') && counted + 4 == stop - length && elements > 1 && previous >= stop - 4))
                        return 0;
                c->s = after;
                return 1;
        }
        case '(': {
                if (!ref_dbus_wire_pad_zero(c->p, &c->at, c->limit, 8)) return 0;
                dbus_i32 old_depth = c->depth;
                c->depth = old_depth + 1;
                dbus_i64 members = 0;
                while (c->s < c->sig_len && c->sig[c->s] != ')') {
                        if (!ref_dbus_wire_value(c)) { c->depth = old_depth; return 0; }
                        members++;
                }
                c->depth = old_depth;
                if (c->s >= c->sig_len || !members) return 0;
                c->s++;
                return 1;
        }
        default:
                return 0;
        }
}

static __attribute__((noinline)) dbus_i32 ref_dbus_wire_body_valid(const unsigned char *body, dbus_i64 length, const char *sig, dbus_i64 sig_len, dbus_u64 fds, dbus_i32 big_endian)
{
        if (length < 0 || sig_len < 0 || !sig) return 0;
        if (sig_len == 0) return length == 0;
        if (!body) return 0;
        struct dbus_wire_cursor c = {body, 0, length, sig, sig_len, 0, fds, 0, big_endian};
        while (c.s < sig_len)
                if (!ref_dbus_wire_value(&c)) return 0;
        return c.at == length;
}

static __attribute__((noinline)) dbus_i32 ref_dbus_wire_header_check(const unsigned char *b, dbus_i64 size, dbus_i64 max_size, struct dbus_wire_hdr *out)
{
        if (!b || !out || size < 16 || size > max_size) return 0;
        dbus_u8 be;
        if (b[0] == 'l') be = 0;
        else if (b[0] == 'B') be = 1;
        else return 0;
        if (b[3] != 1 || b[1] < 1 || b[1] > 4) return 0;
        dbus_u32 body = ref_dbus_wire_get32(b + 4, be);
        dbus_u32 serial = ref_dbus_wire_get32(b + 8, be);
        dbus_u32 fields = ref_dbus_wire_get32(b + 12, be);
        if (!serial || (dbus_u64)fields > (dbus_u64)(size - 16) || (dbus_u64)body > (dbus_u64)size) return 0;
        dbus_i64 limit = 16 + (dbus_i64)fields;
        dbus_i64 pos = REF_ALIGN(limit, 8);
        if (pos + (dbus_i64)body != size) return 0;
        for (dbus_i64 k = limit; k < pos; k++)
                if (b[k]) return 0;
        out->serial = serial;
        out->body_len = body;
        out->fields_len = fields;
        out->type = b[1];
        out->flags = b[2];
        out->big_endian = be;
        out->_pad = 0;
        out->fields_limit = limit;
        out->body_at = pos;
        return 1;
}

static __attribute__((noinline)) void ref_dbus_wire_o_pad(struct dbus_wire_out *w, dbus_i64 align)
{
        if (!w || w->bad) return;
        dbus_i64 to = REF_ALIGN(w->length, align);
        if (to > w->room) { w->bad = 1; return; }
        for (dbus_i64 k = w->length; k < to; k++) w->at[k] = 0;
        w->length = to;
}

static __attribute__((noinline)) void ref_dbus_wire_o_u32(struct dbus_wire_out *w, dbus_u32 v)
{
        if (!w || w->bad) return;
        dbus_i64 to = REF_ALIGN(w->length, 4);
        if (to > w->room || w->room - to < 4) { w->bad = 1; return; }
        for (dbus_i64 k = w->length; k < to; k++) w->at[k] = 0;
        w->at[to] = (unsigned char)v;
        w->at[to + 1] = (unsigned char)(v >> 8);
        w->at[to + 2] = (unsigned char)(v >> 16);
        w->at[to + 3] = (unsigned char)(v >> 24);
        w->length = to + 4;
}

static __attribute__((noinline)) void ref_dbus_wire_o_string(struct dbus_wire_out *w, const char *s)
{
        if (!w || w->bad) return;
        if (!s) { w->bad = 1; return; }
        dbus_i64 n = 0;
        while (s[n]) n++;
        dbus_i64 to = REF_ALIGN(w->length, 4);
        if (to > w->room || w->room - to < n + 5) { w->bad = 1; return; }
        for (dbus_i64 k = w->length; k < to; k++) w->at[k] = 0;
        w->at[to] = (unsigned char)n;
        w->at[to + 1] = (unsigned char)((dbus_u64)n >> 8);
        w->at[to + 2] = (unsigned char)((dbus_u64)n >> 16);
        w->at[to + 3] = (unsigned char)((dbus_u64)n >> 24);
        memcpy(w->at + to + 4, s, (size_t)(n + 1));
        w->length = to + 4 + n + 1;
}

/* ── Harness helpers ──────────────────────────────────────────────────────── */

static uint64_t g_checks = 0;
static int g_failures = 0;

#define CHECK_EQ(expr_a, expr_b, msg) do { \
        g_checks++; \
        int64_t _a = (int64_t)(expr_a), _b = (int64_t)(expr_b); \
        if (_a != _b) { \
                if (g_failures < 20) \
                        fprintf(stderr, "FAIL %s: asm=%lld ref=%lld\n", (msg), (long long)_a, (long long)_b); \
                g_failures++; \
        } \
} while (0)

static uint64_t rng_state = 0x9e3779b97f4a7c15ULL;
static uint64_t rng_next(void) {
        uint64_t x = rng_state;
        x ^= x << 13; x ^= x >> 7; x ^= x << 17;
        return rng_state = x;
}

static uint64_t now_ns(void) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void put32(unsigned char *p, uint32_t v, int be) {
        if (be) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
        else    { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
}

/* ── Security & edge-case tests ───────────────────────────────────────────── */

static void test_utf8_security(void) {
        static const struct { const unsigned char b[8]; dbus_i64 n; int want; } cases[] = {
                {{0}, 0, 1},
                {{'a'}, 1, 1},
                {{0}, 1, 0},                                 /* NUL */
                {{'a','b','c','d','e','f','g',0}, 8, 0},     /* NUL in 8-byte SWAR */
                {{'a','b','c','d','e','f','g','h'}, 8, 1},   /* exact 8-byte SWAR */
                {{0xc0, 0x80}, 2, 0},                        /* overlong NUL */
                {{0xc1, 0xbf}, 2, 0},                        /* overlong 0x7F */
                {{0xc2, 0x80}, 2, 1},                        /* U+0080 */
                {{0xdf, 0xbf}, 2, 1},                        /* U+07FF */
                {{0xe0, 0x80, 0x80}, 3, 0},                  /* overlong 3-byte */
                {{0xe0, 0x9f, 0xbf}, 3, 0},                  /* overlong 3-byte max */
                {{0xe0, 0xa0, 0x80}, 3, 1},                  /* U+0800 */
                {{0xed, 0x9f, 0xbf}, 3, 1},                  /* U+D7FF */
                {{0xed, 0xa0, 0x80}, 3, 0},                  /* surrogate U+D800 */
                {{0xed, 0xbf, 0xbf}, 3, 0},                  /* surrogate U+DFFF */
                {{0xee, 0x80, 0x80}, 3, 1},                  /* U+E000 */
                {{0xf0, 0x80, 0x80, 0x80}, 4, 0},            /* overlong 4-byte */
                {{0xf0, 0x8f, 0xbf, 0xbf}, 4, 0},            /* overlong 4-byte max */
                {{0xf0, 0x90, 0x80, 0x80}, 4, 1},            /* U+10000 */
                {{0xf4, 0x8f, 0xbf, 0xbf}, 4, 1},            /* U+10FFFF */
                {{0xf4, 0x90, 0x80, 0x80}, 4, 0},            /* U+110000 */
                {{0xf5, 0x80, 0x80, 0x80}, 4, 0},            /* > F4 */
                {{0xc2}, 1, 0},                              /* truncated 2 */
                {{0xe0, 0xa0}, 2, 0},                        /* truncated 3 */
                {{0xf0, 0x90, 0x80}, 3, 0},                  /* truncated 4 */
                {{0x80}, 1, 0},                              /* bare continuation */
                {{0xff}, 1, 0},                              /* invalid 0xFF */
        };
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
                CHECK_EQ(dbus_wire_utf8_valid(cases[i].b, cases[i].n), cases[i].want, "utf8_want");
                CHECK_EQ(dbus_wire_utf8_valid(cases[i].b, cases[i].n), ref_dbus_wire_utf8_valid(cases[i].b, cases[i].n), "utf8_ref");
        }
        CHECK_EQ(dbus_wire_utf8_valid(NULL, 0), 1, "utf8_null_0");
        CHECK_EQ(dbus_wire_utf8_valid(NULL, 5), 0, "utf8_null_5");
        CHECK_EQ(dbus_wire_utf8_valid((const unsigned char *)"a", -1), 0, "utf8_neg");

        /* Unaligned SWAR offsets 0..15 with valid ASCII + trailing NUL / non-ASCII */
        unsigned char buf[64];
        memset(buf, 'A', sizeof(buf));
        for (int off = 0; off < 16; off++) {
                for (int len = 0; len <= 40; len++) {
                        CHECK_EQ(dbus_wire_utf8_valid(buf + off, len), 1, "utf8_swar_valid");
                }
        }
        for (int pos = 0; pos < 24; pos++) {
                memset(buf, 'A', 32);
                buf[pos] = 0;
                CHECK_EQ(dbus_wire_utf8_valid(buf, 24), 0, "utf8_swar_nul");
                buf[pos] = 0x80;
                CHECK_EQ(dbus_wire_utf8_valid(buf, 24), 0, "utf8_swar_bad80");
        }
}

static void test_paths_names_sigs_security(void) {
        const char *paths[] = {
                "", "/", "//", "/a", "/a/", "/a/b_1/C2", "/a//b", "a/b", "/a-b", "/a.b", "/0", "/_/_"
        };
        for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++)
                CHECK_EQ(dbus_wire_valid_path(paths[i], (dbus_i64)strlen(paths[i])),
                         ref_dbus_wire_valid_path(paths[i], (dbus_i64)strlen(paths[i])), "path");
        /* 255 vs 256 byte path */
        char longp[260];
        longp[0] = '/'; memset(longp + 1, 'a', 258); longp[259] = 0;
        CHECK_EQ(dbus_wire_valid_path(longp, 255), 1, "path_255");
        CHECK_EQ(dbus_wire_valid_path(longp, 256), 0, "path_256");

        const char *names[] = {
                "", "a", "a.b", "a.b.c", "org.freedesktop.DBus", ":1.42", ":1.0.a-b", ":.a", ":a.",
                "org.free-desktop.Portal", "a.1b", ":a.1b", "-a.-b", "a..b", ".a.b", "a.b.",
                "Member_1", "1Member", "a-b", "org.freedesktop.portal.Desktop"
        };
        for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
                dbus_i64 l = (dbus_i64)strlen(names[i]);
                for (int k = 0; k <= 2; k++)
                        CHECK_EQ(dbus_wire_valid_name(names[i], l, k), ref_dbus_wire_valid_name(names[i], l, k), "name");
        }

        /* Deeply nested signatures: 32 valid, 33 invalid */
        char deep_a[64], deep_s[128], deep_d[256];
        memset(deep_a, 'a', 32); deep_a[32] = 'y'; deep_a[33] = 0;
        CHECK_EQ(dbus_wire_valid_signature(deep_a, 33), 1, "sig_32a");
        memset(deep_a, 'a', 33); deep_a[33] = 'y'; deep_a[34] = 0;
        CHECK_EQ(dbus_wire_valid_signature(deep_a, 34), 0, "sig_33a");

        for (int i = 0; i < 32; i++) deep_s[i] = '(';
        deep_s[32] = 'u';
        for (int i = 0; i < 32; i++) deep_s[33 + i] = ')';
        CHECK_EQ(dbus_wire_valid_signature(deep_s, 65), 1, "sig_32s");
        for (int i = 0; i < 33; i++) deep_s[i] = '(';
        deep_s[33] = 'u';
        for (int i = 0; i < 33; i++) deep_s[34 + i] = ')';
        CHECK_EQ(dbus_wire_valid_signature(deep_s, 67), 0, "sig_33s");

        /* Dict depth 32 valid vs 33 invalid */
        int p = 0;
        for (int i = 0; i < 32; i++) { deep_d[p++] = 'a'; deep_d[p++] = '{'; deep_d[p++] = 's'; }
        deep_d[p++] = 'u';
        for (int i = 0; i < 32; i++) deep_d[p++] = '}';
        CHECK_EQ(dbus_wire_valid_signature(deep_d, p), 1, "sig_32d");
        p = 0;
        for (int i = 0; i < 33; i++) { deep_d[p++] = 'a'; deep_d[p++] = '{'; deep_d[p++] = 's'; }
        deep_d[p++] = 'u';
        for (int i = 0; i < 33; i++) deep_d[p++] = '}';
        CHECK_EQ(dbus_wire_valid_signature(deep_d, p), 0, "sig_33d");

        /* Invalid dict keys (v, a, (), invalid char) and empty struct () */
        const char *bad_sigs[] = { "()", "(())", "a{vs}", "a{as}", "a{(u)s}", "a{s}", "a{ss", "a", "(u", "u)", "z", "a{su}!" };
        for (size_t i = 0; i < sizeof(bad_sigs) / sizeof(bad_sigs[0]); i++)
                CHECK_EQ(dbus_wire_valid_signature(bad_sigs[i], (dbus_i64)strlen(bad_sigs[i])), 0, "bad_sig");

        /* All 256 bytes for dbus_wire_fixed_size */
        for (int c = -1; c <= 256; c++)
                CHECK_EQ(dbus_wire_fixed_size(c), ref_dbus_wire_fixed_size(c), "fixed_size");
}

static void test_wire_values_and_headers(void) {
        /* Test both 'l' (0) and 'B' (1) endianness */
        for (int be = 0; be <= 1; be++) {
                /* 1. Complex body: "yus(ia{sv})bo" */
                unsigned char body[256];
                memset(body, 0, sizeof(body));
                dbus_i64 pos = 0;
                body[pos++] = 0x7f; /* y */
                pos = 4; /* pad to 4 */
                put32(body + pos, 0x12345678u, be); pos += 4; /* u */
                put32(body + pos, 5, be); pos += 4; /* s len */
                memcpy(body + pos, "hello", 6); pos += 6;
                pos = REF_ALIGN(pos, 8); /* struct (ia{sv}) */
                put32(body + pos, 42, be); pos += 4; /* i */
                /* a{sv}: 1 entry "k" -> variant(u=7) */
                dbus_i64 arr_len_pos = pos; pos += 4;
                pos = REF_ALIGN(pos, 8);
                dbus_i64 arr_start = pos;
                put32(body + pos, 1, be); pos += 4;
                body[pos++] = 'k'; body[pos++] = 0;
                body[pos++] = 1; body[pos++] = 'u'; body[pos++] = 0; /* variant sig "u" */
                pos = REF_ALIGN(pos, 4);
                put32(body + pos, 7, be); pos += 4;
                put32(body + arr_len_pos, (uint32_t)(pos - arr_start), be);
                pos = REF_ALIGN(pos, 4);
                put32(body + pos, 1, be); pos += 4; /* b = true */
                put32(body + pos, 4, be); pos += 4; /* o = "/a/b" */
                memcpy(body + pos, "/a/b", 5); pos += 5;

                const char *sig = "yus(ia{sv})bo";
                CHECK_EQ(dbus_wire_body_valid(body, pos, sig, (dbus_i64)strlen(sig), 0, be), 1, "body_valid_ok");
                CHECK_EQ(ref_dbus_wire_body_valid(body, pos, sig, (dbus_i64)strlen(sig), 0, be), 1, "body_valid_ref");

                /* Corrupt non-zero padding byte at index 1 (between y and u) */
                body[1] = 1;
                CHECK_EQ(dbus_wire_body_valid(body, pos, sig, (dbus_i64)strlen(sig), 0, be), 0, "body_bad_pad");
                body[1] = 0;

                /* Truncate by 1 byte */
                CHECK_EQ(dbus_wire_body_valid(body, pos - 1, sig, (dbus_i64)strlen(sig), 0, be), 0, "body_trunc");

                /* 0xFFFFFFFF string length overflow test */
                unsigned char ovf[16];
                memset(ovf, 0, sizeof(ovf));
                put32(ovf, 0xffffffffu, be);
                CHECK_EQ(dbus_wire_body_valid(ovf, 16, "s", 1, 0, be), 0, "str_0xffffffff");
                CHECK_EQ(dbus_wire_body_valid(ovf, 16, "ay", 2, 0, be), 0, "arr_0xffffffff");

                /* Boolean > 1 rejected */
                put32(ovf, 2, be);
                CHECK_EQ(dbus_wire_body_valid(ovf, 4, "b", 1, 0, be), 0, "bool_2");
                put32(ovf, 1, be);
                CHECK_EQ(dbus_wire_body_valid(ovf, 4, "b", 1, 0, be), 1, "bool_1");

                /* Unix FD bounds check */
                put32(ovf, 3, be);
                CHECK_EQ(dbus_wire_body_valid(ovf, 4, "h", 1, 3, be), 0, "fd_oob");
                CHECK_EQ(dbus_wire_body_valid(ovf, 4, "h", 1, 4, be), 1, "fd_ok");

                /* Header check: valid + non-zero pad + 0xFFFFFFFF fields + serial 0 */
                unsigned char hdr_buf[64];
                memset(hdr_buf, 0, sizeof(hdr_buf));
                hdr_buf[0] = be ? 'B' : 'l';
                hdr_buf[1] = 1; /* CALL */
                hdr_buf[2] = 0;
                hdr_buf[3] = 1; /* version */
                put32(hdr_buf + 4, 8, be);  /* body_len = 8 */
                put32(hdr_buf + 8, 99, be); /* serial = 99 */
                put32(hdr_buf + 12, 5, be); /* fields_len = 5 -> limit=21, body_at=24, total=32 */
                struct dbus_wire_hdr ha, hr;
                CHECK_EQ(dbus_wire_header_check(hdr_buf, 32, 65536, &ha), 1, "hdr_ok");
                CHECK_EQ(ref_dbus_wire_header_check(hdr_buf, 32, 65536, &hr), 1, "hdr_ref_ok");
                CHECK_EQ(memcmp(&ha, &hr, sizeof(ha)), 0, "hdr_memcmp");

                hdr_buf[22] = 0x42; /* non-zero header padding */
                CHECK_EQ(dbus_wire_header_check(hdr_buf, 32, 65536, &ha), 0, "hdr_bad_pad");
                hdr_buf[22] = 0;

                put32(hdr_buf + 8, 0, be); /* serial 0 */
                CHECK_EQ(dbus_wire_header_check(hdr_buf, 32, 65536, &ha), 0, "hdr_serial_0");
                put32(hdr_buf + 8, 99, be);

                put32(hdr_buf + 12, 0xffffffffu, be); /* overflow fields_len */
                CHECK_EQ(dbus_wire_header_check(hdr_buf, 32, 65536, &ha), 0, "hdr_ovf_fields");
        }

        /* Variant nesting depth 65 rejected */
        unsigned char vdeep[512];
        dbus_i64 vp = 0;
        for (int i = 0; i < 66; i++) { vdeep[vp++] = 1; vdeep[vp++] = 'v'; vdeep[vp++] = 0; }
        vdeep[vp++] = 1; vdeep[vp++] = 'y'; vdeep[vp++] = 0; vdeep[vp++] = 42;
        CHECK_EQ(dbus_wire_body_valid(vdeep, vp, "v", 1, 0, 0), 0, "variant_depth_66");
        CHECK_EQ(ref_dbus_wire_body_valid(vdeep, vp, "v", 1, 0, 0), 0, "variant_depth_66_ref");
}

/* ── Hardware mmap guard-page tests (PROT_NONE before AND after) ──────────── */

static void run_guard_page_tests(void) {
        size_t pg = (size_t)sysconf(_SC_PAGESIZE);
        unsigned char *slab = mmap(NULL, pg * 3, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (slab == MAP_FAILED) { perror("mmap"); exit(1); }
        if (mprotect(slab + pg, pg, PROT_READ | PROT_WRITE) != 0) { perror("mprotect"); exit(1); }
        unsigned char *mid = slab + pg;

        /* 1. UTF-8 flush before & flush after (lengths 0..33 including SWAR boundaries 7,8,9,15,16,17) */
        for (dbus_i64 len = 0; len <= 33; len++) {
                unsigned char *p_lo = mid;
                unsigned char *p_hi = mid + pg - len;
                if (len) { memset(p_lo, 'x', (size_t)len); memset(p_hi, 'x', (size_t)len); }
                CHECK_EQ(dbus_wire_utf8_valid(p_lo, len), 1, "gp_utf8_lo");
                CHECK_EQ(dbus_wire_utf8_valid(p_hi, len), 1, "gp_utf8_hi");
                if (len >= 1) {
                        p_lo[len - 1] = 0xc2; p_hi[len - 1] = 0xc2; /* truncated 2-byte at exact page edge */
                        CHECK_EQ(dbus_wire_utf8_valid(p_lo, len), 0, "gp_utf8_trunc2_lo");
                        CHECK_EQ(dbus_wire_utf8_valid(p_hi, len), 0, "gp_utf8_trunc2_hi");
                        p_lo[len - 1] = 0xf0; p_hi[len - 1] = 0xf0; /* truncated 4-byte at exact page edge */
                        CHECK_EQ(dbus_wire_utf8_valid(p_lo, len), 0, "gp_utf8_trunc4_lo");
                        CHECK_EQ(dbus_wire_utf8_valid(p_hi, len), 0, "gp_utf8_trunc4_hi");
                }
        }

        /* 2. Path, Name, Signature flush before & after */
        const char *sample_path = "/org/freedesktop/portal/desktop";
        dbus_i64 plen = (dbus_i64)strlen(sample_path);
        memcpy(mid, sample_path, (size_t)plen);
        memcpy(mid + pg - plen, sample_path, (size_t)plen);
        CHECK_EQ(dbus_wire_valid_path((const char *)mid, plen), 1, "gp_path_lo");
        CHECK_EQ(dbus_wire_valid_path((const char *)(mid + pg - plen), plen), 1, "gp_path_hi");

        const char *sample_name = "org.freedesktop.portal.Desktop";
        dbus_i64 nlen = (dbus_i64)strlen(sample_name);
        memcpy(mid, sample_name, (size_t)nlen);
        memcpy(mid + pg - nlen, sample_name, (size_t)nlen);
        CHECK_EQ(dbus_wire_valid_name((const char *)mid, nlen, 1), 1, "gp_name_lo");
        CHECK_EQ(dbus_wire_valid_name((const char *)(mid + pg - nlen), nlen, 1), 1, "gp_name_hi");

        const char *sample_sig = "a{sv}(uus)";
        dbus_i64 slen = (dbus_i64)strlen(sample_sig);
        memcpy(mid, sample_sig, (size_t)slen);
        memcpy(mid + pg - slen, sample_sig, (size_t)slen);
        CHECK_EQ(dbus_wire_valid_signature((const char *)mid, slen), 1, "gp_sig_lo");
        CHECK_EQ(dbus_wire_valid_signature((const char *)(mid + pg - slen), slen), 1, "gp_sig_hi");

        /* 3. Header check & body_valid flush before & after */
        unsigned char h[24];
        memset(h, 0, sizeof(h));
        h[0] = 'l'; h[1] = 1; h[3] = 1;
        put32(h + 4, 8, 0); put32(h + 8, 1, 0); put32(h + 12, 0, 0);
        struct dbus_wire_hdr out;
        memcpy(mid, h, 24);
        memcpy(mid + pg - 24, h, 24);
        CHECK_EQ(dbus_wire_header_check(mid, 24, 1024, &out), 1, "gp_hdr_lo");
        CHECK_EQ(dbus_wire_header_check(mid + pg - 24, 24, 1024, &out), 1, "gp_hdr_hi");

        /* Body "uu" = 8 bytes flush against after guard page */
        memset(mid + pg - 8, 0, 8);
        CHECK_EQ(dbus_wire_body_valid(mid + pg - 8, 8, "uu", 2, 0, 0), 1, "gp_body_hi");
        CHECK_EQ(dbus_wire_body_valid(mid + pg - 7, 7, "uu", 2, 0, 0), 0, "gp_body_trunc_hi");

        /* 4. Wire writer flush against end of writable page (must set bad=1 without touching guard page) */
        struct dbus_wire_out w = { mid + pg - 8, 0, 8, 0, 0 };
        dbus_wire_o_u32(&w, 0x11223344u);
        CHECK_EQ(w.bad, 0, "gp_o_u32_ok");
        CHECK_EQ(w.length, 4, "gp_o_u32_len");
        dbus_wire_o_string(&w, "abc"); /* needs 4 + 4 = 8 > 4 remaining -> must set bad=1, no OOB write */
        CHECK_EQ(w.bad, 1, "gp_o_string_ovf");

        munmap(slab, pg * 3);
}

/* ── Randomized differential fuzzing ──────────────────────────────────────── */

static void run_fuzz(void) {
        unsigned char buf[128];
        char sig[64];

        /* Fuzz UTF-8, path, name, signature */
        for (int iter = 0; iter < 15000; iter++) {
                dbus_i64 len = (dbus_i64)(rng_next() % 48);
                int mode = iter % 4;
                for (dbus_i64 i = 0; i < len; i++) {
                        uint64_t r = rng_next();
                        if (mode == 0) buf[i] = (unsigned char)(r & 0xff);
                        else if (mode == 1) buf[i] = (unsigned char)(1 + (r % 127));
                        else if (mode == 2) {
                                static const char alpha[] = "abcXYZ019_./:-";
                                buf[i] = (unsigned char)alpha[r % (sizeof(alpha) - 1)];
                        } else {
                                static const char salpha[] = "ybnqiuxtdsoghva(){}";
                                buf[i] = (unsigned char)salpha[r % (sizeof(salpha) - 1)];
                        }
                }
                CHECK_EQ(dbus_wire_utf8_valid(buf, len), ref_dbus_wire_utf8_valid(buf, len), "fuzz_utf8");
                CHECK_EQ(dbus_wire_valid_path((const char *)buf, len), ref_dbus_wire_valid_path((const char *)buf, len), "fuzz_path");
                CHECK_EQ(dbus_wire_valid_name((const char *)buf, len, iter % 3), ref_dbus_wire_valid_name((const char *)buf, len, iter % 3), "fuzz_name");
                CHECK_EQ(dbus_wire_valid_signature((const char *)buf, len), ref_dbus_wire_valid_signature((const char *)buf, len), "fuzz_sig");

                dbus_i64 at_a = (dbus_i64)(rng_next() % (len + 1)), at_r = at_a;
                CHECK_EQ(dbus_wire_variant_sig(buf, &at_a, len), ref_dbus_wire_variant_sig(buf, &at_r, len), "fuzz_vsig");
                CHECK_EQ(at_a, at_r, "fuzz_vsig_at");

                dbus_i64 align = 1LL << (rng_next() & 3);
                at_a = (dbus_i64)(rng_next() % (len + 1)); at_r = at_a;
                CHECK_EQ(dbus_wire_pad_zero(buf, &at_a, len, align), ref_dbus_wire_pad_zero(buf, &at_r, len, align), "fuzz_pad");
                CHECK_EQ(at_a, at_r, "fuzz_pad_at");
        }

        /* Fuzz body_valid and header_check */
        static const char *sigs[] = { "", "y", "u", "s", "o", "g", "v", "b", "h", "ay", "as", "(us)", "a{su}", "a(yy)", "a{sv}" };
        for (int iter = 0; iter < 10000; iter++) {
                dbus_i64 len = (dbus_i64)(rng_next() % 64);
                for (dbus_i64 i = 0; i < len; i++)
                        buf[i] = (rng_next() % 3 == 0) ? 0 : (unsigned char)(rng_next() & 0xff);
                const char *s = sigs[rng_next() % (sizeof(sigs) / sizeof(sigs[0]))];
                dbus_i64 sl = (dbus_i64)strlen(s);
                int be = (int)(rng_next() & 1);
                dbus_u64 fds = rng_next() % 5;
                CHECK_EQ(dbus_wire_body_valid(buf, len, s, sl, fds, be),
                         ref_dbus_wire_body_valid(buf, len, s, sl, fds, be), "fuzz_body");

                struct dbus_wire_hdr ha, hr;
                memset(&ha, 0xaa, sizeof(ha)); memset(&hr, 0xaa, sizeof(hr));
                dbus_i32 ra = dbus_wire_header_check(buf, len, 65536, &ha);
                dbus_i32 rr = ref_dbus_wire_header_check(buf, len, 65536, &hr);
                CHECK_EQ(ra, rr, "fuzz_hdr");
                if (ra && rr) CHECK_EQ(memcmp(&ha, &hr, sizeof(ha)), 0, "fuzz_hdr_eq");
        }

        /* Fuzz wire writer (o_pad, o_u32, o_string) */
        unsigned char oa[128], or_[128];
        for (int iter = 0; iter < 5000; iter++) {
                dbus_i64 room = (dbus_i64)(rng_next() % 96);
                memset(oa, 0x55, sizeof(oa)); memset(or_, 0x55, sizeof(or_));
                struct dbus_wire_out wa = { oa, 0, room, 0, 0 };
                struct dbus_wire_out wr = { or_, 0, room, 0, 0 };
                int ops = 1 + (int)(rng_next() % 6);
                for (int k = 0; k < ops; k++) {
                        int op = (int)(rng_next() % 3);
                        if (op == 0) {
                                dbus_i64 al = 1LL << (rng_next() & 3);
                                dbus_wire_o_pad(&wa, al); ref_dbus_wire_o_pad(&wr, al);
                        } else if (op == 1) {
                                dbus_u32 v = (dbus_u32)rng_next();
                                dbus_wire_o_u32(&wa, v); ref_dbus_wire_o_u32(&wr, v);
                        } else {
                                int sl = (int)(rng_next() % 20);
                                for (int j = 0; j < sl; j++) sig[j] = (char)('a' + (rng_next() % 26));
                                sig[sl] = 0;
                                dbus_wire_o_string(&wa, sig); ref_dbus_wire_o_string(&wr, sig);
                        }
                }
                CHECK_EQ(wa.bad, wr.bad, "fuzz_o_bad");
                CHECK_EQ(wa.length, wr.length, "fuzz_o_len");
                if (!wa.bad && wa.length > 0) CHECK_EQ(memcmp(oa, or_, (size_t)wa.length), 0, "fuzz_o_bytes");
        }
}

int main(void) {
        test_utf8_security();
        test_paths_names_sigs_security();
        test_wire_values_and_headers();
        run_guard_page_tests();
        run_fuzz();

        if (g_failures != 0) {
                fprintf(stderr, "FAILED: %d failures across %llu checks\n", g_failures, (unsigned long long)g_checks);
                return 1;
        }
        printf("PASS: 0 failures across %llu differential + security + guard-page checks\n", (unsigned long long)g_checks);

        /* ── Benchmarks (ASM vs C -O2) ─────────────────────────────────────── */
        const char *utf8_str = "org.freedesktop.portal.Desktop/org/freedesktop/portal/desktop/Settings/ReadOne/color-scheme";
        dbus_i64 utf8_len = (dbus_i64)strlen(utf8_str);
        const char *path_str = "/org/freedesktop/portal/desktop/request/1_42/token_98765";
        dbus_i64 path_len = (dbus_i64)strlen(path_str);
        const char *sig_str = "a{sa{sv}}(uss)ay";
        dbus_i64 sig_len = (dbus_i64)strlen(sig_str);

        /* Realistic body for "ssu": ("org.freedesktop.appearance", "color-scheme", 1) */
        unsigned char bench_body[128];
        memset(bench_body, 0, sizeof(bench_body));
        struct dbus_wire_out bw = { bench_body, 0, sizeof(bench_body), 0, 0 };
        dbus_wire_o_string(&bw, "org.freedesktop.appearance");
        dbus_wire_o_string(&bw, "color-scheme");
        dbus_wire_o_u32(&bw, 1);

        volatile int64_t sink = 0;
        const int N = 400000;

        for (int i = 0; i < 4000; i++) {
                sink += dbus_wire_utf8_valid((const unsigned char *)utf8_str, utf8_len);
                sink += ref_dbus_wire_utf8_valid((const unsigned char *)utf8_str, utf8_len);
                sink += dbus_wire_body_valid(bench_body, bw.length, "ssu", 3, 0, 0);
                sink += ref_dbus_wire_body_valid(bench_body, bw.length, "ssu", 3, 0, 0);
        }

        uint64_t t0 = now_ns();
        for (int i = 0; i < N; i++) { const char *p = utf8_str; __asm__ volatile("" : "+r"(p)); sink += dbus_wire_utf8_valid((const unsigned char *)p, utf8_len); }
        uint64_t t1 = now_ns();
        for (int i = 0; i < N; i++) { const char *p = utf8_str; __asm__ volatile("" : "+r"(p)); sink += ref_dbus_wire_utf8_valid((const unsigned char *)p, utf8_len); }
        uint64_t t2 = now_ns();
        printf("BENCH utf8_valid  : ASM %5.1f ns/op | C -O2 %5.1f ns/op | %.2fx\n",
               (double)(t1 - t0) / N, (double)(t2 - t1) / N, (double)(t2 - t1) / (double)(t1 - t0));

        t0 = now_ns();
        for (int i = 0; i < N; i++) { const char *p = path_str; __asm__ volatile("" : "+r"(p)); sink += dbus_wire_valid_path(p, path_len); }
        t1 = now_ns();
        for (int i = 0; i < N; i++) { const char *p = path_str; __asm__ volatile("" : "+r"(p)); sink += ref_dbus_wire_valid_path(p, path_len); }
        t2 = now_ns();
        printf("BENCH valid_path  : ASM %5.1f ns/op | C -O2 %5.1f ns/op | %.2fx\n",
               (double)(t1 - t0) / N, (double)(t2 - t1) / N, (double)(t2 - t1) / (double)(t1 - t0));

        t0 = now_ns();
        for (int i = 0; i < N; i++) { const char *p = sig_str; __asm__ volatile("" : "+r"(p)); sink += dbus_wire_valid_signature(p, sig_len); }
        t1 = now_ns();
        for (int i = 0; i < N; i++) { const char *p = sig_str; __asm__ volatile("" : "+r"(p)); sink += ref_dbus_wire_valid_signature(p, sig_len); }
        t2 = now_ns();
        printf("BENCH valid_sig   : ASM %5.1f ns/op | C -O2 %5.1f ns/op | %.2fx\n",
               (double)(t1 - t0) / N, (double)(t2 - t1) / N, (double)(t2 - t1) / (double)(t1 - t0));

        t0 = now_ns();
        for (int i = 0; i < N; i++) { const unsigned char *p = bench_body; __asm__ volatile("" : "+r"(p)); sink += dbus_wire_body_valid(p, bw.length, "ssu", 3, 0, 0); }
        t1 = now_ns();
        for (int i = 0; i < N; i++) { const unsigned char *p = bench_body; __asm__ volatile("" : "+r"(p)); sink += ref_dbus_wire_body_valid(p, bw.length, "ssu", 3, 0, 0); }
        t2 = now_ns();
        printf("BENCH body_valid  : ASM %5.1f ns/op | C -O2 %5.1f ns/op | %.2fx\n",
               (double)(t1 - t0) / N, (double)(t2 - t1) / N, (double)(t2 - t1) / (double)(t1 - t0));

        unsigned char obuf[128];
        t0 = now_ns();
        for (int i = 0; i < N; i++) {
                struct dbus_wire_out w = { obuf, 0, sizeof(obuf), 0, 0 };
                dbus_wire_o_string(&w, "org.freedesktop.appearance");
                dbus_wire_o_string(&w, "color-scheme");
                dbus_wire_o_u32(&w, 1);
                sink += w.length;
        }
        t1 = now_ns();
        for (int i = 0; i < N; i++) {
                struct dbus_wire_out w = { obuf, 0, sizeof(obuf), 0, 0 };
                ref_dbus_wire_o_string(&w, "org.freedesktop.appearance");
                ref_dbus_wire_o_string(&w, "color-scheme");
                ref_dbus_wire_o_u32(&w, 1);
                sink += w.length;
        }
        t2 = now_ns();
        printf("BENCH wire_write  : ASM %5.1f ns/op | C -O2 %5.1f ns/op | %.2fx\n",
               (double)(t1 - t0) / N, (double)(t2 - t1) / N, (double)(t2 - t1) / (double)(t1 - t0));

        (void)sink;
        return 0;
}
