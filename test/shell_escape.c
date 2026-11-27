/*
        Differential C-reference parity, exhaustive edge-case, fuzz, mmap
        guard-page security & throughput proof for src/sh/shell_escape.asm.c
        (arm64, x86-64, riscv64 pure-ASM).

        Build & run:
          cc -O2 -Wall -Werror test/shell_escape.c -o /tmp/t_esc && /tmp/t_esc
          cc -O2 -fsanitize=address,undefined -Wall -Werror test/shell_escape.c -o /tmp/t_esc_asan && /tmp/t_esc_asan
*/
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>

#define SH_SHELL_ESCAPE_ASM_IMPLEMENTATION
#include "../src/sh/shell_escape.asm.c"

/* ── Reference -O2 C implementations (non-inlined for nm --print-size proof) ── */

__attribute__((noinline))
sh_esc_u64 ref_sh_unescape_c(const char *src, sh_esc_u64 len,
                             char *dst, sh_esc_u64 cap, sh_esc_u32 flags)
{
        if (!src || !len || !dst || !cap)
                return 0;

        sh_esc_u64 in = 0, out = 0;
        while (in < len && out < cap) {
                unsigned char c = (unsigned char)src[in++];
                if (c != '\\') {
                        if ((flags & SH_ESC_STOP_NUL) && c == 0)
                                break;
                        dst[out++] = (char)c;
                        continue;
                }
                if (in >= len) {
                        if (!(flags & SH_ESC_DROP_TRAIL))
                                dst[out++] = '\\';
                        break;
                }

                unsigned char n = (unsigned char)src[in++];
                int have = 0;
                unsigned int val = 0;

                switch (n) {
                case 'n': val = '\n'; have = 1; break;
                case 't': val = '\t'; have = 1; break;
                case 'r': val = '\r'; have = 1; break;
                case 'a': val = 7;    have = 1; break;
                case 'f': val = 12;   have = 1; break;
                case 'v': val = 11;   have = 1; break;
                case 'b': if (flags & SH_ESC_BS)      { val = 8;  have = 1; } break;
                case 'e': if (flags & SH_ESC_E_LOWER) { val = 27; have = 1; } break;
                case 'E': if (flags & SH_ESC_E_UPPER) { val = 27; have = 1; } break;
                default: break;
                }

                if (!have) {
                        unsigned int base = 0, limit = 0, digits = 0;
                        if (n >= '0' && n <= '7' && (flags & SH_ESC_OCTAL)) {
                                base = 8; limit = 2; val = (unsigned int)(n - '0'); digits = 1;
                        } else if (n == '0' && (flags & SH_ESC_OCTAL_ZERO)) {
                                base = 8; limit = 3; val = 0; digits = 1;
                        } else if (n == 'x' && (flags & (SH_ESC_HEX2 | SH_ESC_HEX3))) {
                                base = 16; limit = (flags & SH_ESC_HEX3) ? 3 : 2;
                        } else if (n == 'o' && (flags & SH_ESC_OCTAL_O)) {
                                base = 8; limit = 3;
                        } else if (n == 'd' && (flags & SH_ESC_DEC_D)) {
                                base = 10; limit = 3;
                        } else if (n == 'c') {
                                if (flags & SH_ESC_STOP_C)
                                        break;
                                if ((flags & (SH_ESC_CTRL_XOR | SH_ESC_CTRL_MASK)) && in < len) {
                                        unsigned char x = (unsigned char)src[in++];
                                        if ((flags & SH_ESC_CTRL_DBLBS) && x == '\\' &&
                                            in < len && src[in] == '\\')
                                                in++;
                                        if (flags & SH_ESC_CTRL_XOR) {
                                                if (x >= 'a' && x <= 'z')
                                                        x = (unsigned char)(x - 32);
                                                val = (unsigned int)(x ^ 0x40);
                                        } else {
                                                val = (x == '?') ? 127u : (unsigned int)(x & 31);
                                        }
                                        have = 1;
                                }
                        }

                        if (base) {
                                while (limit > 0 && in < len) {
                                        unsigned char ch = (unsigned char)src[in];
                                        unsigned int d;
                                        if (ch >= '0' && ch <= '9')
                                                d = (unsigned int)(ch - '0');
                                        else if (base == 16 && ((ch | 32) >= 'a' && (ch | 32) <= 'f'))
                                                d = (unsigned int)(((ch | 32) - 'a') + 10);
                                        else
                                                break;
                                        if (d >= base)
                                                break;
                                        val = val * base + d;
                                        in++;
                                        digits++;
                                        limit--;
                                }
                                if (digits > 0) {
                                        val &= 0xffu;
                                        have = 1;
                                }
                        }
                }

                if (!have) {
                        if (flags & SH_ESC_KEEP_UNKNOWN) {
                                int drop_bs = 0;
                                if ((flags & SH_ESC_KEEP_QUOTES) &&
                                    (n == '\\' || n == '\'' || n == '"'))
                                        drop_bs = 1;
                                if ((flags & SH_ESC_KEEP_QMARK) && n == '?')
                                        drop_bs = 1;
                                if (!drop_bs) {
                                        dst[out++] = '\\';
                                        if (out >= cap)
                                                break;
                                }
                        }
                        val = n;
                }

                if ((flags & SH_ESC_STOP_NUL) && val == 0)
                        break;
                dst[out++] = (char)val;
        }
        return out;
}

__attribute__((noinline))
sh_esc_i64 ref_sh_unescape_tr(const char *src, sh_esc_u64 len,
                              char *dst, char *esc_out, sh_esc_u64 cap)
{
        if (!src || !len || !dst || !esc_out || !cap)
                return 0;

        sh_esc_u64 in = 0, out = 0;
        while (in < len && out < cap) {
                unsigned char c = (unsigned char)src[in++];
                if (c != '\\') {
                        dst[out] = (char)c;
                        esc_out[out] = 0;
                        out++;
                        continue;
                }
                if (in >= len)
                        return -1;
                unsigned char n = (unsigned char)src[in++];
                unsigned int val;
                if (n >= '0' && n <= '7') {
                        val = (unsigned int)(n - '0');
                        if (in < len && src[in] >= '0' && src[in] <= '7') {
                                val = val * 8 + (unsigned int)(src[in++] - '0');
                                if (in < len && src[in] >= '0' && src[in] <= '7') {
                                        unsigned int wider = val * 8 + (unsigned int)(src[in] - '0');
                                        if (wider > 255)
                                                return -1;
                                        val = wider;
                                        in++;
                                }
                        }
                } else {
                        switch (n) {
                        case 'n': val = '\n'; break;
                        case 't': val = '\t'; break;
                        case 'r': val = '\r'; break;
                        case 'a': val = 7;    break;
                        case 'b': val = 8;    break;
                        case 'f': val = 12;   break;
                        case 'v': val = 11;   break;
                        default:  val = n;    break;
                        }
                }
                dst[out] = (char)val;
                esc_out[out] = 1;
                out++;
        }
        return (sh_esc_i64)out;
}

__attribute__((noinline))
sh_esc_u64 ref_sh_ansi_sgr_skip(const char *src, sh_esc_u64 len)
{
        if (!src || len < 3 || (unsigned char)src[0] != 0x1b || src[1] != '[')
                return 0;
        for (sh_esc_u64 i = 2; i < len; i++) {
                unsigned char c = (unsigned char)src[i];
                if (c == 'm')
                        return i + 1;
                if (c < 0x20 || c > 0x3f)
                        return 0;
        }
        return 0;
}

/* ── Helpers ───────────────────────────────────────────────────────────────── */

static unsigned long long g_checks = 0;
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

static void check_unescape(const char *src, size_t len, size_t cap, sh_esc_u32 flags, const char *ctx)
{
        char ref_buf[512], asm_buf[512], inp_buf[512];
        if (cap > sizeof(ref_buf) - 16) cap = sizeof(ref_buf) - 16;

        memset(ref_buf, 0xA5, sizeof(ref_buf));
        memset(asm_buf, 0xA5, sizeof(asm_buf));

        sh_esc_u64 r_ref = ref_sh_unescape_c(src, len, ref_buf, cap, flags);
        sh_esc_u64 r_asm = sh_unescape_c(src, len, asm_buf, cap, flags);
        g_checks++;

        if (r_ref != r_asm || memcmp(ref_buf, asm_buf, sizeof(ref_buf)) != 0) {
                fprintf(stderr, "FAIL sh_unescape_c [%s] flags=0x%x len=%zu cap=%zu: ref=%llu asm=%llu\n",
                        ctx, flags, len, cap, (unsigned long long)r_ref, (unsigned long long)r_asm);
                exit(1);
        }

        /* Also test in-place (dst == src) when cap >= len and len <= sizeof(inp_buf) */
        if (src && len <= sizeof(inp_buf) && cap == len) {
                memcpy(inp_buf, src, len);
                sh_esc_u64 r_inp = sh_unescape_c(inp_buf, len, inp_buf, len, flags);
                g_checks++;
                if (r_inp != r_ref || memcmp(inp_buf, ref_buf, (size_t)r_ref) != 0) {
                        fprintf(stderr, "FAIL sh_unescape_c in-place [%s] flags=0x%x len=%zu\n",
                                ctx, flags, len);
                        exit(1);
                }
        }
}

static void check_tr(const char *src, size_t len, size_t cap, const char *ctx)
{
        char ref_dst[256], asm_dst[256], ref_esc[256], asm_esc[256];
        if (cap > sizeof(ref_dst) - 8) cap = sizeof(ref_dst) - 8;

        memset(ref_dst, 0xA5, sizeof(ref_dst));
        memset(asm_dst, 0xA5, sizeof(asm_dst));
        memset(ref_esc, 0x5A, sizeof(ref_esc));
        memset(asm_esc, 0x5A, sizeof(asm_esc));

        sh_esc_i64 r_ref = ref_sh_unescape_tr(src, len, ref_dst, ref_esc, cap);
        sh_esc_i64 r_asm = sh_unescape_tr(src, len, asm_dst, asm_esc, cap);
        g_checks++;

        if (r_ref != r_asm) {
                fprintf(stderr, "FAIL sh_unescape_tr [%s] len=%zu cap=%zu: ref=%lld asm=%lld\n",
                        ctx, len, cap, (long long)r_ref, (long long)r_asm);
                exit(1);
        }
        if (r_ref >= 0 && (memcmp(ref_dst, asm_dst, sizeof(ref_dst)) != 0 ||
                           memcmp(ref_esc, asm_esc, sizeof(ref_esc)) != 0)) {
                fprintf(stderr, "FAIL sh_unescape_tr bytes [%s] len=%zu cap=%zu\n", ctx, len, cap);
                exit(1);
        }
}

static void check_sgr(const char *src, size_t len, const char *ctx)
{
        sh_esc_u64 r_ref = ref_sh_ansi_sgr_skip(src, len);
        sh_esc_u64 r_asm = sh_ansi_sgr_skip(src, len);
        g_checks++;

        if (r_ref != r_asm) {
                fprintf(stderr, "FAIL sh_ansi_sgr_skip [%s] len=%zu: ref=%llu asm=%llu\n",
                        ctx, len, (unsigned long long)r_ref, (unsigned long long)r_asm);
                exit(1);
        }
}

/* ── 1. Exhaustive edge cases & security tests ─────────────────────────────── */

static void test_exhaustive(void)
{
        static const sh_esc_u32 modes[] = {
                SH_ESC_MODE_AWK,
                SH_ESC_MODE_SED,
                SH_ESC_MODE_PTX,
                SH_ESC_MODE_ANSI_BASH,
                SH_ESC_MODE_ANSI_DASH,
                SH_ESC_MODE_ANSI_BASH | SH_ESC_CTRL_DBLBS,
                SH_ESC_MODE_ANSI_DASH | SH_ESC_CTRL_DBLBS,
                0u,
                0x3ffffu,
        };
        const size_t nmodes = sizeof(modes) / sizeof(modes[0]);

        /* NULL / zero-len / zero-cap / 1-byte cap */
        for (size_t m = 0; m < nmodes; m++) {
                char tiny[2] = {0, 0};
                if (sh_unescape_c(NULL, 5, tiny, 2, modes[m]) != 0 ||
                    sh_unescape_c("abc", 0, tiny, 2, modes[m]) != 0 ||
                    sh_unescape_c("abc", 3, NULL, 2, modes[m]) != 0 ||
                    sh_unescape_c("abc", 3, tiny, 0, modes[m]) != 0) {
                        fprintf(stderr, "FAIL null/zero guard\n");
                        exit(1);
                }
                g_checks += 4;
                check_unescape("\\z", 2, 1, modes[m], "cap1_unknown");
                check_unescape("\\", 1, 1, modes[m], "cap1_trail");
                check_unescape("a\\nb", 4, 1, modes[m], "cap1_plain");
                check_unescape("a\\zb", 4, 2, modes[m], "cap2_keep_unknown");
        }

        if (sh_unescape_tr(NULL, 1, (char *)1, (char *)1, 1) != 0 ||
            sh_unescape_tr("a", 0, (char *)1, (char *)1, 1) != 0 ||
            sh_unescape_tr("a", 1, NULL, (char *)1, 1) != 0 ||
            sh_unescape_tr("a", 1, (char *)1, NULL, 1) != 0 ||
            sh_unescape_tr("a", 1, (char *)1, (char *)1, 0) != 0 ||
            sh_ansi_sgr_skip(NULL, 10) != 0) {
                fprintf(stderr, "FAIL tr/sgr null guard\n");
                exit(1);
        }
        g_checks += 6;

        /* All 256 escaped bytes \x00..\xff across all modes and tr */
        for (int b = 0; b < 256; b++) {
                char pair[2] = { '\\', (char)b };
                char ctrl[3] = { '\\', 'c', (char)b };
                for (size_t m = 0; m < nmodes; m++) {
                        check_unescape(pair, 2, 16, modes[m], "all256_pair");
                        check_unescape(ctrl, 3, 16, modes[m], "all256_ctrl");
                }
                check_tr(pair, 2, 16, "all256_tr");
        }

        /* Specific tricky escape sequences */
        static const struct { const char *s; size_t len; } cases[] = {
                { "", 0 },
                { "\\", 1 },
                { "\\\\", 2 },
                { "\\\\\\", 3 },
                { "\\a\\b\\e\\E\\f\\n\\r\\t\\v", 18 },
                { "\\'\\\"\\?\\z", 8 },
                { "\\0", 2 },
                { "\\00", 3 },
                { "\\000", 4 },
                { "\\0000", 5 },
                { "\\0123", 5 },
                { "\\377", 4 },
                { "\\378", 4 },
                { "\\400", 4 },
                { "\\777", 4 },
                { "\\7777", 5 },
                { "\\o", 2 },
                { "\\o7", 3 },
                { "\\o77", 4 },
                { "\\o777", 5 },
                { "\\o8", 3 },
                { "\\d", 2 },
                { "\\d9", 3 },
                { "\\d255", 5 },
                { "\\d999", 5 },
                { "\\dx", 3 },
                { "\\x", 2 },
                { "\\x0", 3 },
                { "\\x00", 4 },
                { "\\x41", 4 },
                { "\\x414", 5 },
                { "\\xfff", 5 },
                { "\\xG", 3 },
                { "\\xAG", 4 },
                { "\\c", 2 },
                { "\\c@", 3 },
                { "\\cA", 3 },
                { "\\ca", 3 },
                { "\\c?", 3 },
                { "\\c\\", 3 },
                { "\\c\\\\", 4 },
                { "\\c\\\\x", 5 },
                { "hi\\cbye", 7 },
                { "a\0b\\nc", 6 },
                { "a\\0b", 4 },
                { "a\\x00b", 6 },
                { "a\\c@b", 5 },
                { "\\u0041\\U00000041", 16 },
        };
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
                for (size_t m = 0; m < nmodes; m++) {
                        check_unescape(cases[i].s, cases[i].len, 64, modes[m], "case");
                        check_unescape(cases[i].s, cases[i].len, cases[i].len, modes[m], "case_exact");
                }
                check_tr(cases[i].s, cases[i].len, 64, "case_tr");
        }

        /* ANSI SGR edge cases */
        static const struct { const char *s; size_t len; } sgr_cases[] = {
                { "", 0 },
                { "\x1b", 1 },
                { "\x1b[", 2 },
                { "\x1b[m", 3 },
                { "\x1b[0m", 4 },
                { "\x1b[31m", 5 },
                { "\x1b[1;31;40m", 10 },
                { "\x1b[38;5;196m", 11 },
                { "\x1b[38:2:255:128:0m", 17 },
                { "\x1b[999999999m", 12 },
                { "\x1b[31", 4 },
                { "\x1b[31K", 5 },
                { "\x1b[31\x00m", 6 },
                { "\x1b[31\x1fm", 6 },
                { "\x1b[31\x40m", 6 },
                { "\x1b[ \x3fm", 5 },
                { "\x1b]0;title\x07", 10 },
                { "plain text", 10 },
        };
        for (size_t i = 0; i < sizeof(sgr_cases) / sizeof(sgr_cases[0]); i++)
                check_sgr(sgr_cases[i].s, sgr_cases[i].len, "sgr_case");
}

/* ── 2. Randomized differential fuzzing ────────────────────────────────────── */

static void test_fuzz(void)
{
        static const char alphabet[] =
                "\\\\\\abefnrtvE0123456789abcdefABCDEFxodc?'\"@\x1b[;m \0\x7f\xff";
        const size_t alen = sizeof(alphabet) - 1;
        static const sh_esc_u32 preset[] = {
                SH_ESC_MODE_AWK, SH_ESC_MODE_SED, SH_ESC_MODE_PTX,
                SH_ESC_MODE_ANSI_BASH, SH_ESC_MODE_ANSI_DASH
        };

        char buf[128];
        for (int iter = 0; iter < 40000; iter++) {
                size_t len = (size_t)(xorshift64() % 64);
                for (size_t i = 0; i < len; i++) {
                        uint64_t r = xorshift64();
                        buf[i] = (r & 3) ? alphabet[r % alen] : (char)(r & 0xff);
                }
                sh_esc_u32 flags = (iter & 1)
                        ? preset[xorshift64() % 5]
                        : (sh_esc_u32)(xorshift64() & 0x3ffffu);
                size_t cap = (xorshift64() & 7) ? 128 : (size_t)(xorshift64() % (len + 4));
                check_unescape(buf, len, cap, flags, "fuzz");
                if ((iter & 3) == 0)
                        check_tr(buf, len, cap, "fuzz_tr");
                if ((iter & 7) == 0) {
                        if (len >= 2 && (xorshift64() & 1)) {
                                buf[0] = 0x1b;
                                buf[1] = '[';
                        }
                        check_sgr(buf, len, "fuzz_sgr");
                }
        }
}

/* ── 3. mmap PROT_NONE guard-page security tests ───────────────────────────── */

static void test_guard_pages(void)
{
        size_t pg = (size_t)sysconf(_SC_PAGESIZE);
        /* Layout: [PROT_NONE] [RW page] [PROT_NONE] */
        char *in_map  = mmap(NULL, 3 * pg, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        char *out_map = mmap(NULL, 3 * pg, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        char *esc_map = mmap(NULL, 3 * pg, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (in_map == MAP_FAILED || out_map == MAP_FAILED || esc_map == MAP_FAILED) {
                perror("mmap");
                exit(1);
        }
        if (mprotect(in_map + pg, pg, PROT_READ | PROT_WRITE) != 0 ||
            mprotect(out_map + pg, pg, PROT_READ | PROT_WRITE) != 0 ||
            mprotect(esc_map + pg, pg, PROT_READ | PROT_WRITE) != 0) {
                perror("mprotect");
                exit(1);
        }

        static const struct { const char *s; size_t len; } payloads[] = {
                { "\\", 1 },
                { "\\x", 2 },
                { "\\x4", 3 },
                { "\\x41", 4 },
                { "\\0", 2 },
                { "\\0123", 5 },
                { "\\777", 4 },
                { "\\o7", 3 },
                { "\\d9", 3 },
                { "\\c", 2 },
                { "\\c\\", 3 },
                { "\\c\\\\", 4 },
                { "\\z", 2 },
                { "hello\\nworld\\x41\\077\\z\\", 23 },
                { "\x1b[", 2 },
                { "\x1b[38;5;196", 10 },
                { "\x1b[38;5;196m", 11 },
                { "\x1b[999999999m", 12 },
        };
        static const sh_esc_u32 modes[] = {
                SH_ESC_MODE_AWK, SH_ESC_MODE_SED, SH_ESC_MODE_PTX,
                SH_ESC_MODE_ANSI_BASH, SH_ESC_MODE_ANSI_DASH
        };

        for (size_t i = 0; i < sizeof(payloads) / sizeof(payloads[0]); i++) {
                size_t len = payloads[i].len;
                /* Test BOTH flush against leading PROT_NONE and trailing PROT_NONE */
                char *in_hi = in_map + 2 * pg - len;
                char *in_lo = in_map + pg;
                memcpy(in_hi, payloads[i].s, len);
                memcpy(in_lo, payloads[i].s, len);

                for (size_t m = 0; m < 5; m++) {
                        for (size_t cap = 0; cap <= len + 2; cap++) {
                                char *out_hi = out_map + 2 * pg - cap;
                                char *out_lo = out_map + pg;
                                sh_esc_u64 a1 = sh_unescape_c(in_hi, len, out_hi, cap, modes[m]);
                                sh_esc_u64 a2 = sh_unescape_c(in_lo, len, out_lo, cap, modes[m]);
                                char ref[64];
                                sh_esc_u64 r = ref_sh_unescape_c(payloads[i].s, len, ref, cap, modes[m]);
                                g_checks += 2;
                                if (a1 != r || a2 != r ||
                                    (r > 0 && (memcmp(out_hi, ref, (size_t)r) != 0 ||
                                               memcmp(out_lo, ref, (size_t)r) != 0))) {
                                        fprintf(stderr, "FAIL guard sh_unescape_c i=%zu m=%zu cap=%zu\n", i, m, cap);
                                        exit(1);
                                }
                        }
                }

                /* Guard-page test for sh_unescape_tr */
                for (size_t cap = 0; cap <= len + 1; cap++) {
                        char *out_hi = out_map + 2 * pg - cap;
                        char *esc_hi = esc_map + 2 * pg - cap;
                        sh_esc_i64 a = sh_unescape_tr(in_hi, len, out_hi, esc_hi, cap);
                        char rdst[64], resc[64];
                        sh_esc_i64 r = ref_sh_unescape_tr(payloads[i].s, len, rdst, resc, cap);
                        g_checks++;
                        if (a != r) {
                                fprintf(stderr, "FAIL guard sh_unescape_tr i=%zu cap=%zu\n", i, cap);
                                exit(1);
                        }
                }

                /* Guard-page test for sh_ansi_sgr_skip */
                sh_esc_u64 s1 = sh_ansi_sgr_skip(in_hi, len);
                sh_esc_u64 s2 = sh_ansi_sgr_skip(in_lo, len);
                sh_esc_u64 sr = ref_sh_ansi_sgr_skip(payloads[i].s, len);
                g_checks += 2;
                if (s1 != sr || s2 != sr) {
                        fprintf(stderr, "FAIL guard sh_ansi_sgr_skip i=%zu\n", i);
                        exit(1);
                }
        }

        munmap(in_map, 3 * pg);
        munmap(out_map, 3 * pg);
        munmap(esc_map, 3 * pg);
}

/* ── 4. Speed benchmark (ns/op ASM vs -O2 C reference) ─────────────────────── */

static double now_ns(void)
{
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static void run_bench(void)
{
        const char * volatile s_awk = "line1\\n\\tcol2\\x20\\041plain_text_run_with_more_ordinary_bytes_and_escapes\\r\\n";
        const char * volatile s_sed = "hello\\nworld\\x41\\o101\\d065\\cA\\tmore_ordinary_text_spanning_several_words\\r";
        const char * volatile s_ptx = "prefix_ordinary_words_before_escapes\\t\\0101\\x42\\z_unknown_keep_plain_text\\n";
        const char * volatile s_tr  = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789\\n\\t\\r\\040\\177";
        const char * volatile s_sgr = "\x1b[38;5;196;1;4m";
        size_t l_awk = strlen(s_awk), l_sed = strlen(s_sed), l_ptx = strlen(s_ptx);
        size_t l_tr  = strlen(s_tr),  l_sgr = strlen(s_sgr);
        char dst[128], esc[128];
        const int N = 400000;
        volatile sh_esc_u64 sink = 0;

        double t0, t1, c_ns, a_ns;

        /* sh_unescape_c (AWK + SED + PTX mix) */
        t0 = now_ns();
        for (int i = 0; i < N; i++) {
                sink += ref_sh_unescape_c(s_awk, l_awk, dst, sizeof(dst), SH_ESC_MODE_AWK);
                sink += ref_sh_unescape_c(s_sed, l_sed, dst, sizeof(dst), SH_ESC_MODE_SED);
                sink += ref_sh_unescape_c(s_ptx, l_ptx, dst, sizeof(dst), SH_ESC_MODE_PTX);
        }
        t1 = now_ns();
        c_ns = (t1 - t0) / (double)(N * 3);

        t0 = now_ns();
        for (int i = 0; i < N; i++) {
                sink += sh_unescape_c(s_awk, l_awk, dst, sizeof(dst), SH_ESC_MODE_AWK);
                sink += sh_unescape_c(s_sed, l_sed, dst, sizeof(dst), SH_ESC_MODE_SED);
                sink += sh_unescape_c(s_ptx, l_ptx, dst, sizeof(dst), SH_ESC_MODE_PTX);
        }
        t1 = now_ns();
        a_ns = (t1 - t0) / (double)(N * 3);
        printf("bench sh_unescape_c   : C=%.2f ns/op  ASM=%.2f ns/op  speedup=%.2fx\n",
               c_ns, a_ns, c_ns / a_ns);

        /* sh_unescape_tr */
        t0 = now_ns();
        for (int i = 0; i < N; i++)
                sink += (sh_esc_u64)ref_sh_unescape_tr(s_tr, l_tr, dst, esc, sizeof(dst));
        t1 = now_ns();
        c_ns = (t1 - t0) / (double)N;

        t0 = now_ns();
        for (int i = 0; i < N; i++)
                sink += (sh_esc_u64)sh_unescape_tr(s_tr, l_tr, dst, esc, sizeof(dst));
        t1 = now_ns();
        a_ns = (t1 - t0) / (double)N;
        printf("bench sh_unescape_tr  : C=%.2f ns/op  ASM=%.2f ns/op  speedup=%.2fx\n",
               c_ns, a_ns, c_ns / a_ns);

        /* sh_ansi_sgr_skip */
        t0 = now_ns();
        for (int i = 0; i < N * 4; i++)
                sink += ref_sh_ansi_sgr_skip(s_sgr, l_sgr);
        t1 = now_ns();
        c_ns = (t1 - t0) / (double)(N * 4);

        t0 = now_ns();
        for (int i = 0; i < N * 4; i++)
                sink += sh_ansi_sgr_skip(s_sgr, l_sgr);
        t1 = now_ns();
        a_ns = (t1 - t0) / (double)(N * 4);
        printf("bench sh_ansi_sgr_skip: C=%.2f ns/op  ASM=%.2f ns/op  speedup=%.2fx\n",
               c_ns, a_ns, c_ns / a_ns);

        (void)sink;
}

int main(void)
{
        test_exhaustive();
        test_fuzz();
        test_guard_pages();
        printf("PASS: %llu differential + guard-page checks succeeded!\n", g_checks);
        run_bench();
        return 0;
}
