/*
        Differential C-reference parity, exhaustive edge-case, fuzz, mmap
        guard-page security & throughput proof for src/sh/shell_arith_test.asm.c
        (arm64, x86-64, riscv64 pure-ASM).

        Build & run:
          cc -O2 -Wall -Werror test/shell_arith_test.c -o /tmp/t_arith && /tmp/t_arith
          cc -O2 -fsanitize=address,undefined -Wall -Werror test/shell_arith_test.c -o /tmp/t_arith_asan && /tmp/t_arith_asan
*/
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>

#define SH_SHELL_ARITH_TEST_ASM_IMPLEMENTATION
#include "../src/sh/shell_arith_test.asm.c"

/* ── Reference -O2 C implementations (non-inlined for nm --print-size proof) ── */

__attribute__((noinline))
sh_at_i32 ref_sh_test_digits_compare(const char *left, const char *right,
                                     sh_at_i64 *cmp_out)
{
        if (!left || !right || !cmp_out)
                return 0;

        unsigned char ca = (unsigned char)left[0];
        unsigned char cb = (unsigned char)right[0];
        if ((unsigned char)(ca - '0') > 9 || (unsigned char)(cb - '0') > 9)
                return 0;

        if (!left[1] && !right[1]) {
                *cmp_out = ca < cb ? -1 : (ca > cb ? 1 : 0);
                return 1;
        }

        const char *a = left;
        const char *b = right;
        while (ca == '0' && (unsigned char)((unsigned char)a[1] - '0') <= 9) {
                a++;
                ca = (unsigned char)*a;
        }
        while (cb == '0' && (unsigned char)((unsigned char)b[1] - '0') <= 9) {
                b++;
                cb = (unsigned char)*b;
        }

        const char *as = a;
        do
                a++;
        while ((unsigned char)((unsigned char)*a - '0') <= 9);
        if (*a)
                return 0;

        const char *bs = b;
        do
                b++;
        while ((unsigned char)((unsigned char)*b - '0') <= 9);
        if (*b)
                return 0;

        size_t la = (size_t)(a - as);
        size_t lb = (size_t)(b - bs);
        if (la > 18 || lb > 18)
                return 0;

        if (la != lb) {
                *cmp_out = la < lb ? -1 : 1;
                return 1;
        }

        while (as != a) {
                ca = (unsigned char)*as++;
                cb = (unsigned char)*bs++;
                if (ca != cb) {
                        *cmp_out = ca < cb ? -1 : 1;
                        return 1;
                }
        }

        *cmp_out = 0;
        return 1;
}

__attribute__((noinline))
sh_at_u64 ref_sh_arith_operator(const char *at, sh_at_u64 *width_out)
{
        if (!width_out)
                return 0;
        *width_out = 1;
        if (!at)
                return 0;

        unsigned char op = (unsigned char)at[0];
        if (!op)
                return 0;
        unsigned char next = (unsigned char)at[1];

        switch (op) {
        case '*': case '/': case '%':
                return next != '=' ? 10 : 0;
        case '+': case '-':
                return next != '=' ? 9 : 0;
        case '<': case '>':
                if (next == op) {
                        *width_out = 2;
                        return (unsigned char)at[2] == '=' ? 0 : 8;
                }
                *width_out = next == '=' ? 2 : 1;
                return 7;
        case '=': case '!':
                *width_out = 2;
                return next == '=' ? 6 : 0;
        case '&':
                if (next == '&') {
                        *width_out = 2;
                        return 2;
                }
                return next != '=' ? 5 : 0;
        case '^':
                return next != '=' ? 4 : 0;
        case '|':
                if (next == '|') {
                        *width_out = 2;
                        return 1;
                }
                return next != '=' ? 3 : 0;
        }
        return 0;
}

__attribute__((noinline))
sh_at_i64 ref_sh_arith_power_of(sh_at_i64 base, sh_at_i64 exponent)
{
        if (exponent < 0)
                return 0;
        sh_at_u64 val = 1;
        sh_at_u64 b = (sh_at_u64)base;
        sh_at_u64 e = (sh_at_u64)exponent;
        while (e) {
                if (e & 1)
                        val *= b;
                e >>= 1;
                if (e)
                        b *= b;
        }
        return (sh_at_i64)val;
}

__attribute__((noinline))
sh_at_i32 ref_sh_expand_bracket_end_fast(const char *at, sh_at_u32 open,
                                         sh_at_u32 close, sh_at_u32 dollar_brace,
                                         const char **out_ptr)
{
        if (!at) {
                if (out_ptr) *out_ptr = NULL;
                return SH_BRACKET_EOF;
        }
        unsigned char op = (unsigned char)(open & 0xffu);
        unsigned char cl = (unsigned char)(close & 0xffu);
        int db = dollar_brace != 0;
        sh_at_u64 depth = 1;

        for (;;) {
                unsigned char v = (unsigned char)*at;
                if (!v) {
                        if (out_ptr) *out_ptr = NULL;
                        return SH_BRACKET_EOF;
                } else if (v == cl) {
                        if (!--depth) {
                                if (out_ptr) *out_ptr = at;
                                return SH_BRACKET_FOUND;
                        }
                } else if (v == '\'' || v == '"' || v == '\\' || v == '$' ||
                           (db && (v == '`' ||
                                   ((v == '<' || v == '>') && at[1] == '(')))) {
                        if (out_ptr) *out_ptr = NULL;
                        return SH_BRACKET_QUOTED;
                } else if (v == op && !db) {
                        depth++;
                }
                at++;
        }
}

/* ── Helpers ── */

static uint64_t g_rng = 0x9e3779b97f4a7c15ULL;
static uint64_t xorshift64(void)
{
        uint64_t x = g_rng;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        g_rng = x;
        return x;
}

static uint64_t failures = 0;
static uint64_t checks = 0;

static void check_digits(const char *l, const char *r, const char *tag)
{
        sh_at_i64 c_ref = 0x5555555555555555LL, c_asm = 0x5555555555555555LL;
        sh_at_i32 r_ref = ref_sh_test_digits_compare(l, r, &c_ref);
        sh_at_i32 r_asm = sh_test_digits_compare(l, r, &c_asm);
        checks++;
        if (r_ref != r_asm || (r_ref && c_ref != c_asm)) {
                fprintf(stderr, "FAIL digits[%s]: l=%s r=%s ref=(%d,%lld) asm=(%d,%lld)\n",
                        tag, l ? l : "(null)", r ? r : "(null)",
                        (int)r_ref, (long long)c_ref, (int)r_asm, (long long)c_asm);
                failures++;
        }
}

static void check_op(const char *s, const char *tag)
{
        sh_at_u64 w_ref = 99, w_asm = 99;
        sh_at_u64 l_ref = ref_sh_arith_operator(s, &w_ref);
        sh_at_u64 l_asm = sh_arith_operator(s, &w_asm);
        checks++;
        if (l_ref != l_asm || w_ref != w_asm) {
                fprintf(stderr, "FAIL op[%s]: s=%s ref=(%llu,w=%llu) asm=(%llu,w=%llu)\n",
                        tag, s ? s : "(null)",
                        (unsigned long long)l_ref, (unsigned long long)w_ref,
                        (unsigned long long)l_asm, (unsigned long long)w_asm);
                failures++;
        }
}

static void check_pow(sh_at_i64 base, sh_at_i64 exp, const char *tag)
{
        sh_at_i64 r_ref = ref_sh_arith_power_of(base, exp);
        sh_at_i64 r_asm = sh_arith_power_of(base, exp);
        checks++;
        if (r_ref != r_asm) {
                fprintf(stderr, "FAIL pow[%s]: %lld ** %lld ref=%lld asm=%lld\n",
                        tag, (long long)base, (long long)exp,
                        (long long)r_ref, (long long)r_asm);
                failures++;
        }
}

static void check_bracket(const char *s, sh_at_u32 op, sh_at_u32 cl,
                          sh_at_u32 db, const char *tag)
{
        const char *p_ref = (const char *)0x1, *p_asm = (const char *)0x1;
        sh_at_i32 r_ref = ref_sh_expand_bracket_end_fast(s, op, cl, db, &p_ref);
        sh_at_i32 r_asm = sh_expand_bracket_end_fast(s, op, cl, db, &p_asm);
        checks++;
        if (r_ref != r_asm || p_ref != p_asm) {
                fprintf(stderr, "FAIL bracket[%s]: op=%u cl=%u db=%u ref=(%d,%td) asm=(%d,%td)\n",
                        tag, (unsigned)op, (unsigned)cl, (unsigned)db,
                        (int)r_ref, p_ref && s ? (ptrdiff_t)(p_ref - s) : -1,
                        (int)r_asm, p_asm && s ? (ptrdiff_t)(p_asm - s) : -1);
                failures++;
        }
}

/* ── Exhaustive edge-case tests ── */

static void test_digits_exhaustive(void)
{
        /* NULL & NULL cmp_out */
        check_digits(NULL, "1", "null-l");
        check_digits("1", NULL, "null-r");
        check_digits(NULL, NULL, "null-both");
        if (sh_test_digits_compare("1", "2", NULL) != 0) {
                fprintf(stderr, "FAIL digits: NULL cmp_out\n");
                failures++;
        }
        checks++;

        /* All 100 single-digit pairs */
        for (int a = '0'; a <= '9'; a++) {
                char sa[2] = { (char)a, 0 };
                for (int b = '0'; b <= '9'; b++) {
                        char sb[2] = { (char)b, 0 };
                        check_digits(sa, sb, "1digit");
                }
        }

        const char *cases[] = {
                "", "0", "1", "9", "00", "01", "09", "10", "00000000000000000000000000",
                "00000000000000000000000001", "0000000000000000000999999999999999999",
                "999999999999999998", "999999999999999999",
                "1000000000000000000",  /* 19 digits -> must return 0 */
                "9223372036854775807",  /* INT64_MAX (19 digits) -> must return 0 */
                "9223372036854775808",  /* INT64_MAX+1 -> must return 0 */
                "99999999999999999999999999999999999999999999999999",
                " 0", " 123", "123 ", "+0", "+1", "-0", "-1", "-123",
                "0a", "1a", "123x", "0x10", "/", ":", "\x7f", "\x80", "\xff",
                "000000000000000000a", "123456789012345678", "123456789012345679",
                "000123456789012345678", "500000", "499999", "500001"
        };
        size_t n = sizeof(cases) / sizeof(cases[0]);
        for (size_t i = 0; i < n; i++)
                for (size_t j = 0; j < n; j++)
                        check_digits(cases[i], cases[j], "matrix");
}

static void test_operator_exhaustive(void)
{
        check_op(NULL, "null");
        check_op("", "empty");
        if (sh_arith_operator("+", NULL) != 0) {
                fprintf(stderr, "FAIL op: NULL width_out\n");
                failures++;
        }
        checks++;

        const char *explicit_ops[] = {
                "+", "-", "*", "/", "%", "<", ">", "<=", ">=", "<<", ">>",
                "<<=", ">>=", "==", "!=", "=", "!", "&", "&&", "^", "|", "||",
                "+=", "-=", "*=", "/=", "%=", "&=", "^=", "|=",
                "++", "--", "**", "<<<", ">>>", "===", "!==", "&&&", "|||",
                "&&=", "||=", "< =", "> =", "a", "0", " ", "(", ")", "?", ":"
        };
        for (size_t i = 0; i < sizeof(explicit_ops) / sizeof(explicit_ops[0]); i++)
                check_op(explicit_ops[i], "explicit");

        /* Exhaustive 256 first bytes x representative 2nd/3rd bytes */
        const unsigned char nexts[] = { 0, '=', '<', '>', '&', '|', '+', '-', '*', 'a', 0xff };
        const unsigned char thirds[] = { 0, '=', 'a' };
        for (int c0 = 1; c0 < 256; c0++) {
                for (size_t j = 0; j < sizeof(nexts); j++) {
                        if (nexts[j] == 0) {
                                char buf[2] = { (char)c0, 0 };
                                check_op(buf, "exhaust2");
                        } else {
                                for (size_t k = 0; k < sizeof(thirds); k++) {
                                        char buf[4] = { (char)c0, (char)nexts[j], (char)thirds[k], 0 };
                                        check_op(buf, "exhaust3");
                                }
                        }
                }
        }
}

static void test_power_exhaustive(void)
{
        const sh_at_i64 bases[] = {
                0, 1, -1, 2, -2, 3, -3, 5, 10, -10, 127, -128,
                INT32_MAX, INT32_MIN, INT64_MAX, INT64_MIN,
                0x123456789abcdefLL, -0x123456789abcdefLL
        };
        const sh_at_i64 exps[] = {
                INT64_MIN, -100, -2, -1, 0, 1, 2, 3, 4, 5, 7, 8, 15, 16,
                31, 32, 62, 63, 64, 65, 100, 1000, INT32_MAX,
                INT64_MAX - 1, INT64_MAX
        };
        for (size_t i = 0; i < sizeof(bases) / sizeof(bases[0]); i++)
                for (size_t j = 0; j < sizeof(exps) / sizeof(exps[0]); j++)
                        check_pow(bases[i], exps[j], "matrix");
}

static void test_bracket_exhaustive(void)
{
        check_bracket(NULL, '(', ')', 0, "null");
        check_bracket(NULL, '{', '}', 1, "null-db");
        if (sh_expand_bracket_end_fast("a + b)", '(', ')', 0, NULL) != SH_BRACKET_FOUND) {
                fprintf(stderr, "FAIL bracket: NULL out_ptr\n");
                failures++;
        }
        checks++;

        const char *inputs[] = {
                "", ")", "}", "]", "i + 1)", "(i + 1))", "((a + b) * (c - d)))",
                "((unclosed)", "x#?}", "a{b}c}", "a{b{c}}}", "a'b')", "a\"b\")",
                "a\\)b)", "a$b)", "a`b`)", "a`b`}", "a<(cmd))", "a<(cmd)}",
                "a>(cmd)}", "a<b}", "a>b}", "a<", "a>", "a<}", "a>}",
                "plain_long_identifier_without_any_special_chars_0123456789)",
                "plain_long_identifier_without_any_special_chars_0123456789}",
                "\x80\x81\xfe\xff)", "\x80\x81\xfe\xff}", "a[b[c]]]"
        };
        size_t n = sizeof(inputs) / sizeof(inputs[0]);
        for (size_t i = 0; i < n; i++) {
                check_bracket(inputs[i], '(', ')', 0, "paren");
                check_bracket(inputs[i], '[', ']', 0, "sq");
                check_bracket(inputs[i], '{', '}', 1, "brace-db");
                check_bracket(inputs[i], '{', '}', 0, "brace-nodb");
                check_bracket(inputs[i], ')', ')', 0, "same-op-cl");
                check_bracket(inputs[i], '<', '>', 0, "angle-nodb");
                check_bracket(inputs[i], '<', '>', 1, "angle-db");
                check_bracket(inputs[i], '(', 0, 0, "close-nul");
                check_bracket(inputs[i], 0x80, 0xff, 0, "high-bytes");
                check_bracket(inputs[i], '"', ')', 0, "open-quote");
        }
}

/* ── Randomized differential fuzzing ── */

static void test_fuzz(void)
{
        char b1[64], b2[64];

        /* 20,000 digit comparisons */
        for (int i = 0; i < 20000; i++) {
                int l1 = (int)(xorshift64() % 28);
                int l2 = (int)(xorshift64() % 28);
                int z1 = (int)(xorshift64() % 5);
                int z2 = (int)(xorshift64() % 5);
                int p = 0;
                for (int k = 0; k < z1 && p < 60; k++) b1[p++] = '0';
                for (int k = 0; k < l1 && p < 60; k++) b1[p++] = (char)('0' + (xorshift64() % 10));
                if ((xorshift64() & 15) == 0 && p > 0) b1[xorshift64() % p] = (char)(32 + (xorshift64() % 95));
                b1[p] = 0;
                p = 0;
                for (int k = 0; k < z2 && p < 60; k++) b2[p++] = '0';
                for (int k = 0; k < l2 && p < 60; k++) b2[p++] = (char)('0' + (xorshift64() % 10));
                if ((xorshift64() & 15) == 0 && p > 0) b2[xorshift64() % p] = (char)(32 + (xorshift64() % 95));
                b2[p] = 0;
                check_digits(b1, b2, "fuzz");
        }

        /* 10,000 power_of tests */
        for (int i = 0; i < 10000; i++) {
                sh_at_i64 base = (sh_at_i64)xorshift64();
                sh_at_i64 exp = (xorshift64() & 7) == 0
                        ? -(sh_at_i64)(xorshift64() & 0xffff)
                        : (sh_at_i64)(xorshift64() & ((xorshift64() & 1) ? 0x7fffffffffffffffULL : 0xffULL));
                check_pow(base, exp, "fuzz");
        }

        /* 20,000 bracket scanner tests */
        static const char alphabet[] = "abc0123()[]{}'\"\\$`<> \t+-*/\x80\xff";
        for (int i = 0; i < 20000; i++) {
                int len = (int)(xorshift64() % 55);
                for (int k = 0; k < len; k++)
                        b1[k] = alphabet[xorshift64() % (sizeof(alphabet) - 1)];
                b1[len] = 0;
                sh_at_u32 mode = (sh_at_u32)(xorshift64() % 4);
                sh_at_u32 op = mode == 0 ? '(' : mode == 1 ? '[' : mode == 2 ? '{' : (sh_at_u32)(xorshift64() & 0xff);
                sh_at_u32 cl = mode == 0 ? ')' : mode == 1 ? ']' : mode == 2 ? '}' : (sh_at_u32)(xorshift64() & 0xff);
                sh_at_u32 db = (mode == 2) ? 1u : (sh_at_u32)(xorshift64() & 1u);
                check_bracket(b1, op, cl, db, "fuzz");
        }
}

/* ── mmap guard-page security tests (PROT_NONE before AND after) ── */

static void test_guard_pages(void)
{
        long ps = sysconf(_SC_PAGESIZE);
        if (ps <= 0) ps = 4096;
        size_t page = (size_t)ps;
        char *region = (char *)mmap(NULL, 3 * page, PROT_NONE,
                                    MAP_PRIVATE | MAP_ANON, -1, 0);
        if (region == MAP_FAILED) {
                perror("mmap");
                failures++;
                return;
        }
        char *mid = region + page;
        if (mprotect(mid, page, PROT_READ | PROT_WRITE) != 0) {
                perror("mprotect");
                munmap(region, 3 * page);
                failures++;
                return;
        }

        const char *guard_strs[] = {
                "", "0", "9", "00", "0000012345", "123456789012345678",
                "1234567890123456789", "0a", "<", "<<", "<<=", ">", ">>", ">>=",
                "=", "==", "!", "!=", "&", "&&", "|", "||", "+", "+=",
                "a + b)", "((a + b))", "unclosed", "a<", "a>", "a<(x)}", "x#?}"
        };
        size_t n = sizeof(guard_strs) / sizeof(guard_strs[0]);

        for (size_t i = 0; i < n; i++) {
                size_t len = strlen(guard_strs[i]) + 1; /* includes NUL */

                /* Flush at END of page (NUL byte is last accessible byte before PROT_NONE) */
                char *end_buf = mid + page - len;
                memcpy(end_buf, guard_strs[i], len);

                /* Flush at START of page (first byte is right after leading PROT_NONE) */
                char *start_buf = mid;
                memcpy(start_buf, guard_strs[i], len);

                check_digits(end_buf, start_buf, "guard-e-s");
                check_digits(start_buf, end_buf, "guard-s-e");
                check_digits(end_buf, end_buf, "guard-e-e");
                check_op(end_buf, "guard-end");
                check_op(start_buf, "guard-start");
                check_bracket(end_buf, '(', ')', 0, "guard-end-p");
                check_bracket(start_buf, '(', ')', 0, "guard-start-p");
                check_bracket(end_buf, '{', '}', 1, "guard-end-b");
                check_bracket(start_buf, '{', '}', 1, "guard-start-b");
        }

        munmap(region, 3 * page);
}

/* ── Throughput benchmark ── */

static uint64_t now_ns(void)
{
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static volatile sh_at_u64 g_sink = 0;

static void run_benchmarks(void)
{
        const int N = 4000000;
        const char *d_l[] = { "499999", "0", "42", "12345678901234567", "00000500000", "9", "100000", "99999" };
        const char *d_r[] = { "500000", "5", "42", "12345678901234568", "500000",      "8", "100000", "100000" };
        const char *ops[] = { "+", "-", "*", "/", "<<", ">>=", "<=", "==", "&&", "||", "&", "^", "|", ")", " " };
        const char *brk[] = {
                "i + 1)",
                "((a + b) * (c - d) + (e / f)))",
                "counter + step_size * 42)",
                "variable_name_long_prefix_0123456789_suffix}"
        };

        sh_at_u64 sink = 0;
        sh_at_i64 cmp = 0;
        sh_at_u64 w = 0;
        const char *out = NULL;

        /* 1. sh_test_digits_compare */
        uint64_t t0 = now_ns();
        for (int i = 0; i < N; i++) {
                sh_at_i32 ok = ref_sh_test_digits_compare(d_l[i & 7], d_r[i & 7], &cmp);
                sink += (sh_at_u64)ok + (sh_at_u64)cmp;
        }
        uint64_t t_ref_d = now_ns() - t0;

        t0 = now_ns();
        for (int i = 0; i < N; i++) {
                sh_at_i32 ok = sh_test_digits_compare(d_l[i & 7], d_r[i & 7], &cmp);
                sink += (sh_at_u64)ok + (sh_at_u64)cmp;
        }
        uint64_t t_asm_d = now_ns() - t0;

        /* 2. sh_arith_operator */
        t0 = now_ns();
        for (int i = 0; i < N; i++)
                sink += ref_sh_arith_operator(ops[i % 15], &w) + w;
        uint64_t t_ref_o = now_ns() - t0;

        t0 = now_ns();
        for (int i = 0; i < N; i++)
                sink += sh_arith_operator(ops[i % 15], &w) + w;
        uint64_t t_asm_o = now_ns() - t0;

        /* 3. sh_arith_power_of */
        t0 = now_ns();
        for (int i = 0; i < N; i++)
                sink += (sh_at_u64)ref_sh_arith_power_of((sh_at_i64)((i & 31) + 2), (sh_at_i64)(i & 63));
        uint64_t t_ref_p = now_ns() - t0;

        t0 = now_ns();
        for (int i = 0; i < N; i++)
                sink += (sh_at_u64)sh_arith_power_of((sh_at_i64)((i & 31) + 2), (sh_at_i64)(i & 63));
        uint64_t t_asm_p = now_ns() - t0;

        /* 4. sh_expand_bracket_end_fast */
        t0 = now_ns();
        for (int i = 0; i < N; i++) {
                int idx = i & 3;
                sh_at_u32 op = idx == 3 ? '{' : '(';
                sh_at_u32 cl = idx == 3 ? '}' : ')';
                sh_at_u32 db = idx == 3 ? 1u : 0u;
                sink += (sh_at_u64)ref_sh_expand_bracket_end_fast(brk[idx], op, cl, db, &out);
                sink += (sh_at_u64)(out ? (out - brk[idx]) : 0);
        }
        uint64_t t_ref_b = now_ns() - t0;

        t0 = now_ns();
        for (int i = 0; i < N; i++) {
                int idx = i & 3;
                sh_at_u32 op = idx == 3 ? '{' : '(';
                sh_at_u32 cl = idx == 3 ? '}' : ')';
                sh_at_u32 db = idx == 3 ? 1u : 0u;
                sink += (sh_at_u64)sh_expand_bracket_end_fast(brk[idx], op, cl, db, &out);
                sink += (sh_at_u64)(out ? (out - brk[idx]) : 0);
        }
        uint64_t t_asm_b = now_ns() - t0;

        g_sink = sink;
        printf("BENCH (%d ops each):\n", N);
        printf("  sh_test_digits_compare    : ref=%6.2f ns/op  asm=%6.2f ns/op  speedup=%.2fx\n",
               (double)t_ref_d / N, (double)t_asm_d / N, (double)t_ref_d / (double)t_asm_d);
        printf("  sh_arith_operator         : ref=%6.2f ns/op  asm=%6.2f ns/op  speedup=%.2fx\n",
               (double)t_ref_o / N, (double)t_asm_o / N, (double)t_ref_o / (double)t_asm_o);
        printf("  sh_arith_power_of         : ref=%6.2f ns/op  asm=%6.2f ns/op  speedup=%.2fx\n",
               (double)t_ref_p / N, (double)t_asm_p / N, (double)t_ref_p / (double)t_asm_p);
        printf("  sh_expand_bracket_end_fast: ref=%6.2f ns/op  asm=%6.2f ns/op  speedup=%.2fx\n",
               (double)t_ref_b / N, (double)t_asm_b / N, (double)t_ref_b / (double)t_asm_b);
}

int main(void)
{
        test_digits_exhaustive();
        test_operator_exhaustive();
        test_power_exhaustive();
        test_bracket_exhaustive();
        test_fuzz();
        test_guard_pages();

        if (failures) {
                fprintf(stderr, "FAILED: %llu / %llu checks failed\n",
                        (unsigned long long)failures, (unsigned long long)checks);
                return 1;
        }
        printf("PASS: %llu differential + edge + fuzz + guard-page checks (0 failures)\n",
               (unsigned long long)checks);
        run_benchmarks();
        return 0;
}
