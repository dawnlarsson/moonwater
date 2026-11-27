/*
        Differential C-reference parity, fuzz, guard-page security & throughput
        proof for src/sh/shell_glob.asm.c (arm64, x86-64, riscv64 pure-ASM).

        Build & run:
          cc -O2 -Wall -Werror test/shell_glob.c -o /tmp/mw_shell_glob && /tmp/mw_shell_glob
          cc -O2 -fsanitize=address,undefined -Wall -Werror test/shell_glob.c -o /tmp/mw_shell_glob_asan && /tmp/mw_shell_glob_asan
*/
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>

#define SH_SHELL_GLOB_IMPLEMENTATION
#include "../src/sh/shell_glob.asm.c"

/* ── Reference C implementations (matching src/sh/expand.c exactly) ────────── */

static int ref_ext_head(unsigned char c) {
        return c == '?' || c == '*' || c == '+' || c == '@' || c == '!';
}

static unsigned char ref_lower(unsigned char c) {
        return (c >= 'A' && c <= 'Z') ? (unsigned char)(c | 32) : c;
}

static const char *ref_byte_class_end(const char *text, const char *limit) {
        if ((limit && limit - text < 2) || text[0] != '[' || text[1] != ':')
                return NULL;
        for (text += 2; (!limit || text + 1 < limit) && *text; text++) {
                if (text[0] == ':' && text[1] == ']')
                        return text + 2;
        }
        return NULL;
}

static __attribute__((noinline)) sh_glob_i32 ref_sh_glob_magic(const char *pattern, sh_glob_u32 extglob) {
        if (!pattern) return 0;
        while (*pattern) {
                unsigned char c = (unsigned char)*pattern;
                if (c == '\\' && pattern[1]) {
                        pattern += 2;
                        continue;
                }
                if (c == '*' || c == '?' || c == '[')
                        return 1;
                if (extglob && pattern[1] == '(' && ref_ext_head(c))
                        return 1;
                pattern++;
        }
        return 0;
}

static __attribute__((noinline)) const char *ref_sh_glob_set_end(const char *at) {
        if (!at || !*at) return NULL;
        const char *step = at + 1;
        if (*step == '!' || *step == '^')
                step++;
        if (*step == ']')
                step++;
        while (*step && *step != ']') {
                const char *past = ref_byte_class_end(step, NULL);
                if (past)
                        step = past;
                else if (*step == '\\' && step[1])
                        step += 2;
                else
                        step++;
        }
        return *step ? step : NULL;
}

static __attribute__((noinline)) const char *ref_sh_glob_unit_end(const char *at, const char *bound) {
        if (!at) return NULL;
        if (bound && at >= bound) return NULL;
        if (*at == '\\' && (!bound || at + 1 < bound) && at[1])
                return at + 2;
        if (*at == '[') {
                const char *close = ref_sh_glob_set_end(at);
                if (close && (!bound || close < bound))
                        return close + 1;
        }
        return NULL;
}

static __attribute__((noinline)) const char *ref_sh_glob_group_end(const char *at) {
        if (!at) return NULL;
        const char *step = at;
        sh_glob_u64 depth = 0;
        while (*step) {
                const char *past = ref_sh_glob_unit_end(step, NULL);
                if (past) {
                        step = past;
                        continue;
                }
                if (*step == '(')
                        depth++;
                else if (*step == ')' && !--depth)
                        return step + 1;
                step++;
        }
        return NULL;
}

static __attribute__((noinline)) sh_glob_i32 ref_sh_glob_extended_anywhere(const char *pattern) {
        if (!pattern) return 0;
        const char *at = pattern;
        while (1) {
                at = strchr(at, '(');
                if (!at) return 0;
                if (at > pattern && ref_ext_head((unsigned char)at[-1]))
                        return 1;
                at++;
        }
}

static __attribute__((noinline)) sh_glob_i32 ref_sh_glob_match(const char *pattern, const char *text, sh_glob_u32 flags) {
        if (!pattern || !text) return 0;
        int fold = (flags & SH_GLOB_FOLD) != 0;
        int utf8 = (flags & SH_GLOB_UTF8) != 0;
        const char *star = NULL;
        const char *back = NULL;
        unsigned char behind = 0;

        while (*text) {
                unsigned char tc = (unsigned char)*text;
                unsigned char want = (unsigned char)*pattern;
                const char *stop = NULL;

                if (want == '*') {
                        while (*pattern == '*') pattern++;
                        if (!*pattern) return 1;
                        star = pattern;
                        back = text;
                        behind = fold ? 0 : (unsigned char)*pattern;
                        if (behind == '?' || behind == '[' || behind == '\\')
                                behind = 0;
                        continue;
                }

                if (want == '[')
                        stop = ref_sh_glob_set_end(pattern);

                if (stop) {
                        if (utf8 && tc >= 0x80) return -1;
                        const char *step = pattern + 1;
                        int invert = 0, found = 0;
                        if (*step == '!' || *step == '^') {
                                invert = 1;
                                step++;
                        }
                        unsigned char member = fold ? ref_lower(tc) : tc;
                        while (step < stop) {
                                const char *past = ref_byte_class_end(step, stop);
                                if (past && past <= stop)
                                        return -1;
                                if (*step == '\\' && step + 1 < stop)
                                        step++;
                                unsigned char low = (unsigned char)*step++;
                                if (fold) low = ref_lower(low);
                                if (step + 1 < stop && *step == '-') {
                                        step++;
                                        if (*step == '\\' && step + 1 < stop)
                                                step++;
                                        unsigned char high = (unsigned char)*step++;
                                        if (fold) high = ref_lower(high);
                                        if (member >= low && member <= high)
                                                found = 1;
                                        continue;
                                }
                                if (member == low)
                                        found = 1;
                        }
                        if (invert ? !found : found) {
                                pattern = stop + 1;
                                text++;
                                continue;
                        }
                } else {
                        int escaped = 0;
                        if (want == '\\' && pattern[1]) {
                                want = (unsigned char)*++pattern;
                                escaped = 1;
                        }
                        if (want && ((!escaped && want == '?') ||
                                     want == tc ||
                                     (fold && ref_lower(want) == ref_lower(tc)))) {
                                if (!escaped && want == '?' && utf8 && tc >= 0x80)
                                        return -1;
                                pattern++;
                                text++;
                                continue;
                        }
                }

                if (!star) return 0;
                if (utf8 && (unsigned char)*back >= 0x80) return -1;
                pattern = star;
                back++;
                text = back;
                if (behind) {
                        while (*text && (unsigned char)*text != behind)
                                text++;
                        if (!*text) return 0;
                        back = text;
                }
        }

        while (*pattern == '*') pattern++;
        return *pattern == '\0' ? 1 : 0;
}

/* ── Harness & PRNG ────────────────────────────────────────────────────────── */

static uint64_t g_rng = 0x9e3779b97f4a7c15ULL;
static uint64_t rng_u64(void) {
        uint64_t x = g_rng;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        g_rng = x;
        return x;
}

static uint64_t now_ns(void) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static uint64_t g_checks = 0;
static int g_failures = 0;

static void check_match_one(const char *tag, const char *pat, const char *txt, sh_glob_u32 flags) {
        g_checks++;
        sh_glob_i32 a = sh_glob_match(pat, txt, flags);
        sh_glob_i32 r = ref_sh_glob_match(pat, txt, flags);
        if (a != r) {
                if (g_failures < 20)
                        fprintf(stderr, "FAIL [%s] match pat=\"%s\" txt=\"%s\" flags=%u: asm=%d ref=%d\n",
                                tag, pat ? pat : "(null)", txt ? txt : "(null)", flags, a, r);
                g_failures++;
        }
}

/* ── Hardware mmap guard-page tests (leading AND trailing PROT_NONE) ───────── */

static void run_guard_page_tests(void) {
        long ps = sysconf(_SC_PAGESIZE);
        if (ps <= 0) ps = 4096;
        size_t page = (size_t)ps;
        char *base = (char *)mmap(NULL, 3 * page, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (base == MAP_FAILED) {
                fprintf(stderr, "mmap failed\n");
                g_failures++;
                return;
        }
        char *rw = base + page;
        if (mprotect(rw, page, PROT_READ | PROT_WRITE) != 0) {
                fprintf(stderr, "mprotect failed\n");
                munmap(base, 3 * page);
                g_failures++;
                return;
        }

        const char *cases[] = {
                "", "a", "[", "]", "\\", "(", ")", "*", "?", "+", "@", "!",
                "[!", "[^", "[]", "[a", "[a]", "[!]", "[^]", "[!]]", "[^]]",
                "[\\]", "[\\", "[[:", "[[:]", "[[:a:]]", "[[:a:]", "[[::]]",
                "@(", "+(", "!(", "*(", "?(", "@(a|b)", "@([)]|a)z", "@(\\)a)z",
                "foo*bar?baz[a-z]", "a*b*c", "***", "\\*", "\\?", "\\[",
                "trailing\\", "unclosed[abc", "a-z", "[a-]", "[a-zA-Z0-9_]"
        };
        const size_t ncases = sizeof(cases) / sizeof(cases[0]);

        for (size_t i = 0; i < ncases; i++) {
                size_t len = strlen(cases[i]);
                /* Place flush against TRAILING PROT_NONE page (NUL at last byte of rw) */
                char *tail = rw + page - (len + 1);
                memcpy(tail, cases[i], len + 1);
                /* Place flush against LEADING PROT_NONE page (first byte at start of rw) */
                char *head = rw;
                memcpy(head, cases[i], len + 1);

                const char *ptrs[2] = { head, tail };
                for (int side = 0; side < 2; side++) {
                        const char *p = ptrs[side];
                        g_checks += 7;
                        if (sh_glob_magic(p, 0) != ref_sh_glob_magic(p, 0)) g_failures++;
                        if (sh_glob_magic(p, 1) != ref_sh_glob_magic(p, 1)) g_failures++;
                        if (sh_glob_set_end(p) != ref_sh_glob_set_end(p)) g_failures++;
                        if (sh_glob_unit_end(p, NULL) != ref_sh_glob_unit_end(p, NULL)) g_failures++;
                        if (sh_glob_unit_end(p, p + len) != ref_sh_glob_unit_end(p, p + len)) g_failures++;
                        if (sh_glob_group_end(p) != ref_sh_glob_group_end(p)) g_failures++;
                        if (sh_glob_extended_anywhere(p) != ref_sh_glob_extended_anywhere(p)) g_failures++;

                        for (sh_glob_u32 fl = 0; fl < 4; fl++) {
                                check_match_one("guard_self", p, p, fl);
                                check_match_one("guard_cross", head, tail, fl);
                                check_match_one("guard_cross2", tail, head, fl);
                        }
                }
        }

        munmap(base, 3 * page);
}

int main(void) {
        /* 1. NULL and basic boundary tests */
        g_checks += 10;
        if (sh_glob_magic(NULL, 0) != 0) g_failures++;
        if (sh_glob_magic(NULL, 1) != 0) g_failures++;
        if (sh_glob_set_end(NULL) != NULL) g_failures++;
        if (sh_glob_set_end("") != NULL) g_failures++;
        if (sh_glob_unit_end(NULL, NULL) != NULL) g_failures++;
        if (sh_glob_group_end(NULL) != NULL) g_failures++;
        if (sh_glob_extended_anywhere(NULL) != 0) g_failures++;
        if (sh_glob_match(NULL, "a", 0) != 0) g_failures++;
        if (sh_glob_match("a", NULL, 0) != 0) g_failures++;
        if (sh_glob_match(NULL, NULL, 0) != 0) g_failures++;

        /* 2. Targeted edge-case corpus */
        const char *pats[] = {
                "", "*", "**", "***", "?", "??", "a", "A", "abc", "a*c", "*a*",
                "*a*b*c*", "a?c", "\\*", "\\?", "\\[", "\\\\", "\\", "a\\",
                "[a]", "[!a]", "[^a]", "[a-z]", "[A-Z]", "[Z-a]", "[!a-z]",
                "[^A-Z]", "[]a]", "[!]]", "[^]]", "[]", "[!]", "[^]", "[",
                "[a-]", "[-a]", "[\\]]", "[\\\\]", "[a-\\]]", "[a\\-z]",
                "[[:alpha:]]", "[[:digit:]a]", "[[:alpha:]", "[[:]", "[[::]]",
                "[a[:lower:]z]", "unclosed[abc", "a[unclosed", "*[a-z]*",
                "*.c", "src/*.asm.c", "foo?bar[0-9]*baz", "\x80", "[\x80-\xff]",
                "?\x80", "*\xc3*", "[\x7f-\x81]"
        };
        const char *txts[] = {
                "", "a", "A", "b", "z", "Z", "0", "-", "]", "[", "\\", "*", "?",
                "abc", "ABC", "aBc", "axc", "abbbbbc", "a\\b", "foo1bar9xyzbaz",
                "src/shell_glob.asm.c", "unclosed[abc", "a[unclosed",
                "\x80", "\xc3", "\xc3\xa9", "x\xc3\xa9y", "\xff", "\x7f"
        };
        for (size_t i = 0; i < sizeof(pats) / sizeof(pats[0]); i++) {
                g_checks += 6;
                if (sh_glob_magic(pats[i], 0) != ref_sh_glob_magic(pats[i], 0)) g_failures++;
                if (sh_glob_magic(pats[i], 1) != ref_sh_glob_magic(pats[i], 1)) g_failures++;
                if (sh_glob_set_end(pats[i]) != ref_sh_glob_set_end(pats[i])) g_failures++;
                if (sh_glob_unit_end(pats[i], NULL) != ref_sh_glob_unit_end(pats[i], NULL)) g_failures++;
                if (sh_glob_group_end(pats[i]) != ref_sh_glob_group_end(pats[i])) g_failures++;
                if (sh_glob_extended_anywhere(pats[i]) != ref_sh_glob_extended_anywhere(pats[i])) g_failures++;

                for (size_t j = 0; j < sizeof(txts) / sizeof(txts[0]); j++) {
                        for (sh_glob_u32 fl = 0; fl < 4; fl++)
                                check_match_one("corpus", pats[i], txts[j], fl);
                }
        }

        /* 3. Extglob / group / unit targeted cases */
        const char *ext_cases[] = {
                "", "(", ")", "()", "a(b)", "@(a|b)", "!(a|b)", "+(a|b)", "*(a|b)", "?(a|b)",
                "@(a|(b|c))rest", "@([)]|a)rest", "@(\\)a)rest", "@([[:alpha:]])rest",
                "@(unclosed", ")a(b)c", "foo+(bar)baz", "foo\\+(bar)", "foo!bar", "foo!(bar)",
                "foo@bar", "foo@(bar)", "foo+bar", "foo+(bar)", "\\(", "\\[a]", "[a-z]x"
        };
        for (size_t i = 0; i < sizeof(ext_cases) / sizeof(ext_cases[0]); i++) {
                size_t l = strlen(ext_cases[i]);
                g_checks += 5;
                if (sh_glob_magic(ext_cases[i], 0) != ref_sh_glob_magic(ext_cases[i], 0)) g_failures++;
                if (sh_glob_magic(ext_cases[i], 1) != ref_sh_glob_magic(ext_cases[i], 1)) g_failures++;
                if (sh_glob_extended_anywhere(ext_cases[i]) != ref_sh_glob_extended_anywhere(ext_cases[i])) g_failures++;
                if (sh_glob_group_end(ext_cases[i]) != ref_sh_glob_group_end(ext_cases[i])) g_failures++;
                if (sh_glob_set_end(ext_cases[i]) != ref_sh_glob_set_end(ext_cases[i])) g_failures++;
                for (size_t b = 0; b <= l; b++) {
                        g_checks++;
                        if (sh_glob_unit_end(ext_cases[i], ext_cases[i] + b) !=
                            ref_sh_glob_unit_end(ext_cases[i], ext_cases[i] + b))
                                g_failures++;
                }
        }

        /* 4. ReDoS / linear-time backtracking immunity test */
        {
                char redos_txt[512];
                memset(redos_txt, 'a', 500);
                redos_txt[500] = '\0';
                const char *redos_pat = "*a*a*a*a*a*a*a*a*a*a*a*a*b";
                uint64_t r0 = now_ns();
                check_match_one("redos_nomatch", redos_pat, redos_txt, 0);
                check_match_one("redos_fold", redos_pat, redos_txt, SH_GLOB_FOLD);
                redos_txt[499] = 'b';
                check_match_one("redos_match", redos_pat, redos_txt, 0);
                uint64_t r1 = now_ns();
                if (r1 - r0 > 50000000ULL) { /* > 50ms would indicate exponential blowup */
                        fprintf(stderr, "FAIL: ReDoS took %llu ns\n", (unsigned long long)(r1 - r0));
                        g_failures++;
                }
        }

        /* 5. Randomized differential fuzzing (50,000 iterations) */
        static const char alpha[] = "abAB09*?[]!^-\\():+@|/";
        const size_t alen = sizeof(alpha) - 1;
        char pbuf[64], tbuf[64];
        for (int it = 0; it < 50000; it++) {
                size_t plen = (size_t)(rng_u64() % 24);
                size_t tlen = (size_t)(rng_u64() % 24);
                for (size_t k = 0; k < plen; k++) {
                        uint64_t r = rng_u64();
                        if ((r & 31) == 0) pbuf[k] = (char)(1 + ((r >> 8) % 255));
                        else pbuf[k] = alpha[r % alen];
                }
                pbuf[plen] = '\0';
                for (size_t k = 0; k < tlen; k++) {
                        uint64_t r = rng_u64();
                        if ((r & 31) == 0) tbuf[k] = (char)(1 + ((r >> 8) % 255));
                        else tbuf[k] = alpha[r % alen];
                }
                tbuf[tlen] = '\0';

                g_checks += 6;
                if (sh_glob_magic(pbuf, 0) != ref_sh_glob_magic(pbuf, 0)) g_failures++;
                if (sh_glob_magic(pbuf, 1) != ref_sh_glob_magic(pbuf, 1)) g_failures++;
                if (sh_glob_set_end(pbuf) != ref_sh_glob_set_end(pbuf)) g_failures++;
                size_t bnd = plen ? (size_t)(rng_u64() % (plen + 1)) : 0;
                if (sh_glob_unit_end(pbuf, (it & 1) ? (pbuf + bnd) : NULL) !=
                    ref_sh_glob_unit_end(pbuf, (it & 1) ? (pbuf + bnd) : NULL)) g_failures++;
                if (sh_glob_group_end(pbuf) != ref_sh_glob_group_end(pbuf)) g_failures++;
                if (sh_glob_extended_anywhere(pbuf) != ref_sh_glob_extended_anywhere(pbuf)) g_failures++;

                sh_glob_u32 fl = (sh_glob_u32)(it & 3);
                check_match_one("fuzz", pbuf, tbuf, fl);
        }

        /* 6. Hardware mmap guard-page OOB read/write trap suite */
        run_guard_page_tests();

        if (g_failures != 0) {
                fprintf(stderr, "FAILED: %d failures across %llu checks\n",
                        g_failures, (unsigned long long)g_checks);
                return 1;
        }

        /* 7. Throughput benchmark vs -O2 C reference */
        const char * volatile b_pat1 = "src/sh/*_[a-z]*.asm.c";
        const char * volatile b_txt1 = "src/sh/shell_glob.asm.c";
        const char * volatile b_pat2 = "*_release_2026_*.tar.gz";
        const char * volatile b_txt2 = "moonwater_linux_arm64_release_2026_v1.tar.gz";
        const char * volatile b_magic = "src/sh/plain_identifier_without_any_glob_chars_12345.c";
        const char * volatile b_ext   = "src/sh/plain_identifier_without_any_extglob_parens_12345.c";
        const char * volatile b_set   = "[!a-zA-Z0-9_.-]";
        const char * volatile b_grp   = "@(foo|bar|(baz|[)]|qux))_tail";
        volatile intptr_t sink = 0;
        const int N = 1000000;

        for (int i = 0; i < 10000; i++) {
                sink += sh_glob_match(b_pat1, b_txt1, 0) + ref_sh_glob_match(b_pat1, b_txt1, 0);
                sink += sh_glob_magic(b_magic, 1) + ref_sh_glob_magic(b_magic, 1);
        }

        uint64_t t0 = now_ns();
        for (int i = 0; i < N; i++) {
                sink += sh_glob_match(b_pat1, b_txt1, 0);
                sink += sh_glob_match(b_pat2, b_txt2, 0);
        }
        uint64_t t1 = now_ns();
        for (int i = 0; i < N; i++) {
                sink += ref_sh_glob_match(b_pat1, b_txt1, 0);
                sink += ref_sh_glob_match(b_pat2, b_txt2, 0);
        }
        uint64_t t2 = now_ns();

        uint64_t m0 = now_ns();
        for (int i = 0; i < N; i++) sink += sh_glob_magic(b_magic, 1);
        uint64_t m1 = now_ns();
        for (int i = 0; i < N; i++) sink += ref_sh_glob_magic(b_magic, 1);
        uint64_t m2 = now_ns();

        uint64_t e0 = now_ns();
        for (int i = 0; i < N; i++) sink += sh_glob_extended_anywhere(b_ext);
        uint64_t e1 = now_ns();
        for (int i = 0; i < N; i++) sink += ref_sh_glob_extended_anywhere(b_ext);
        uint64_t e2 = now_ns();

        uint64_t s0 = now_ns();
        for (int i = 0; i < N; i++) sink += (intptr_t)sh_glob_set_end(b_set);
        uint64_t s1 = now_ns();
        for (int i = 0; i < N; i++) sink += (intptr_t)ref_sh_glob_set_end(b_set);
        uint64_t s2 = now_ns();

        uint64_t g0 = now_ns();
        for (int i = 0; i < N; i++) sink += (intptr_t)sh_glob_group_end(b_grp);
        uint64_t g1 = now_ns();
        for (int i = 0; i < N; i++) sink += (intptr_t)ref_sh_glob_group_end(b_grp);
        uint64_t g2 = now_ns();

        double am = (double)(t1 - t0) / (double)(2 * N), rm = (double)(t2 - t1) / (double)(2 * N);
        double amg = (double)(m1 - m0) / (double)N, rmg = (double)(m2 - m1) / (double)N;
        double aex = (double)(e1 - e0) / (double)N, rex = (double)(e2 - e1) / (double)N;
        double ase = (double)(s1 - s0) / (double)N, rse = (double)(s2 - s1) / (double)N;
        double agr = (double)(g1 - g0) / (double)N, rgr = (double)(g2 - g1) / (double)N;

        printf("PASS: 0 failures across %llu differential + guard-page checks\n", (unsigned long long)g_checks);
        printf("BENCH sh_glob_match             : ASM %5.1f ns/op | C -O2 %5.1f ns/op | %.2fx\n", am, rm, rm / am);
        printf("BENCH sh_glob_magic             : ASM %5.1f ns/op | C -O2 %5.1f ns/op | %.2fx\n", amg, rmg, rmg / amg);
        printf("BENCH sh_glob_extended_anywhere : ASM %5.1f ns/op | C -O2 %5.1f ns/op | %.2fx\n", aex, rex, rex / aex);
        printf("BENCH sh_glob_set_end           : ASM %5.1f ns/op | C -O2 %5.1f ns/op | %.2fx\n", ase, rse, rse / ase);
        printf("BENCH sh_glob_group_end         : ASM %5.1f ns/op | C -O2 %5.1f ns/op | %.2fx\n", agr, rgr, rgr / agr);
        (void)sink;
        return 0;
}
