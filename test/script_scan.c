/*
        Differential C-reference parity, security/guard-page, fuzz, size & speed
        proof for src/moonwater/script_scan.asm.c (arm64, x86-64, riscv64).

        Build & run:
          cc -O2 -Wall -Werror test/script_scan.c -o /tmp/t_sscan && /tmp/t_sscan
          cc -O2 -fsanitize=address,undefined -Wall -Werror test/script_scan.c -o /tmp/t_sscan_asan && /tmp/t_sscan_asan
*/
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>

#define MOONWATER_SCRIPT_SCAN_ASM_IMPLEMENTATION
#include "../src/moonwater/script_scan.asm.c"

/* ── Exact -O2 C reference implementations (original moonwater.c logic) ───── */

static const unsigned char ref_script_kind[256] = {
        ['\t'] = MW_SK_SPACE, ['\n'] = MW_SK_NL, ['\r'] = MW_SK_SPACE, [' '] = MW_SK_SPACE,
        ['#'] = MW_SK_HASH, ['\\'] = MW_SK_ESC,
        ['('] = MW_SK_PUNCT, [')'] = MW_SK_PUNCT, ['{'] = MW_SK_PUNCT, ['}'] = MW_SK_PUNCT,
        ['|'] = MW_SK_PUNCT, [';'] = MW_SK_SEMI,
        ['"'] = MW_SK_QUOTE, ['\''] = MW_SK_QUOTE, ['`'] = MW_SK_QUOTE,
        ['A' ... 'Z'] = MW_SK_WORD, ['a' ... 'z'] = MW_SK_WORD,
        ['_'] = MW_SK_WORD, ['$'] = MW_SK_WORD, ['*'] = MW_SK_WORD, ['?'] = MW_SK_WORD,
        ['['] = MW_SK_WORD, ['@'] = MW_SK_WORD,
        ['0' ... '9'] = MW_SK_WORD2, ['-'] = MW_SK_WORD2, ['/'] = MW_SK_WORD2,
        [']'] = MW_SK_WORD2, ['.'] = MW_SK_WORD2,
};

__attribute__((noinline)) mw_scan_u32 ref_script_char_kind(mw_scan_u32 c)
{
        return c < 256u ? ref_script_kind[c] : 0u;
}

__attribute__((noinline)) mw_scan_i32 ref_script_eq(const char *a, const char *b)
{
        if (!a || !b) return 0;
        while (*a && *a == *b) { a++; b++; }
        return *a == *b;
}

__attribute__((noinline)) mw_scan_i32 ref_script_event_is(const char *pattern, const char *event)
{
        if (!pattern || !event) return 0;
        while (*pattern && *event) {
                char want = *event == ' ' ? '_' : *event;
                if (*pattern != want && *pattern != *event) return 0;
                pattern++;
                event++;
        }
        return !*pattern && !*event;
}

__attribute__((noinline)) void ref_script_heredoc_start(struct mw_script_read *scan)
{
        unsigned char quote = 0;
        unsigned int used = 0;
        mw_scan_u64 total = 0;

        if (!scan || !scan->text) return;
        scan->at += 2;
        scan->heredoc_tabs = scan->at < scan->length && scan->text[scan->at] == '-';
        if (scan->heredoc_tabs) scan->at++;
        while (scan->at < scan->length &&
               (scan->text[scan->at] == ' ' || scan->text[scan->at] == '\t'))
                scan->at++;
        while (scan->at < scan->length) {
                unsigned char value = scan->text[scan->at];
                if (!quote && (value == '\'' || value == '"')) {
                        quote = value;
                        scan->at++;
                        continue;
                }
                if (quote && value == quote) {
                        quote = 0;
                        scan->at++;
                        continue;
                }
                if (value == '\n' ||
                    (!quote && (value == ' ' || value == '\t' || value == ';' ||
                                value == '|' || value == '&' || value == '>' ||
                                value == '<' || value == ')')))
                        break;
                if (value == '\\' && quote != '\'' && scan->at + 1 < scan->length) {
                        scan->at++;
                        value = scan->text[scan->at];
                        if (value == '\n') {
                                scan->at++;
                                scan->line++;
                                continue;
                        }
                }
                total++;
                if (used + 1 < MW_SCRIPT_WORD)
                        scan->heredoc[used++] = (char)value;
                scan->at++;
        }
        scan->heredoc[used] = 0;
        scan->heredoc_active = 1;
        scan->heredoc_length = (unsigned char)used;
        scan->heredoc_total = total;
}

__attribute__((noinline)) void ref_script_skip_heredoc(struct mw_script_read *scan)
{
        mw_scan_u64 at, open, length;
        unsigned int lines = 0;

        if (!scan || !scan->text) return;
        at = scan->at;
        open = scan->at;
        length = scan->heredoc_length;

        while (scan->rescan && at < scan->length) {
                mw_scan_u64 start = ++at, stop = start, from;
                lines++;
                while (stop < scan->length && scan->text[stop] != '\n')
                        stop++;
                from = start;
                if (scan->heredoc_tabs)
                        while (from < stop && scan->text[from] == '\t')
                                from++;
                at = stop;
                if (stop - from == scan->heredoc_total) {
                        mw_scan_u64 k = 0;
                        while (k < length &&
                               scan->text[from + k] == (unsigned char)scan->heredoc[k])
                                k++;
                        if (k == length) {
                                scan->at = at;
                                scan->line += lines;
                                break;
                        }
                }
        }
        if (scan->at == open) {
                mw_scan_u64 spent = at > open ? at - open : 0;
                scan->rescan -= spent < scan->rescan ? spent : scan->rescan;
                scan->at++;
                scan->line++;
        }
        scan->heredoc_active = 0;
        scan->heredoc_length = 0;
}

__attribute__((noinline)) void ref_script_next(struct mw_script_read *scan, struct mw_script_token *token)
{
        unsigned char value, quote, kind, fresh = 0;
        unsigned int used = 0;

        if (!token) return;
        memset(token, 0, sizeof(*token));
        if (!scan || !scan->text) return;

        while (scan->at < scan->length) {
                value = scan->text[scan->at];
                kind = ref_script_kind[value];
                if (kind == MW_SK_NL && scan->heredoc_active) {
                        ref_script_skip_heredoc(scan);
                        fresh = 1;
                        continue;
                }
                if (kind == MW_SK_NL || kind == MW_SK_SPACE) {
                        fresh |= kind == MW_SK_NL;
                        scan->line += kind == MW_SK_NL;
                        scan->at++;
                        continue;
                }
                if (kind == MW_SK_ESC && scan->at + 1 < scan->length &&
                    scan->text[scan->at + 1] == '\n') {
                        scan->at += 2;
                        scan->line++;
                        continue;
                }
                if (kind == MW_SK_HASH) {
                        while (scan->at < scan->length && scan->text[scan->at] != '\n')
                                scan->at++;
                        continue;
                }
                break;
        }
        token->line = (mw_scan_u16)(scan->line > 65535u ? 65535u : scan->line);
        token->fresh = fresh;
        if (scan->at >= scan->length) {
                token->kind = MW_SCRIPT_TOK_END;
                return;
        }

        value = scan->text[scan->at];
        kind = ref_script_kind[value];
        if (kind == MW_SK_SEMI && scan->at + 1 < scan->length &&
            scan->text[scan->at + 1] == ';') {
                scan->at += 2;
                token->kind = MW_SCRIPT_TOK_DSEMI;
                return;
        }
        if (value == '<' && scan->at + 2 < scan->length &&
            scan->text[scan->at + 1] == '<' && scan->text[scan->at + 2] != '<') {
                ref_script_heredoc_start(scan);
                token->kind = MW_SCRIPT_TOK_PUNCT;
                token->punct = '<';
                token->word[0] = '<';
                return;
        }
        if (kind == MW_SK_PUNCT || kind == MW_SK_SEMI) {
                scan->at++;
                token->kind = MW_SCRIPT_TOK_PUNCT;
                token->punct = value;
                token->word[0] = (char)value;
                return;
        }

        token->kind = MW_SCRIPT_TOK_WORD;
        if (kind == MW_SK_QUOTE) {
                quote = value;
                scan->at++;
                while (scan->at < scan->length && scan->text[scan->at] != quote) {
                        value = scan->text[scan->at];
                        if (quote != '\'' && value == '\\' && scan->at + 1 < scan->length) {
                                scan->at++;
                                value = scan->text[scan->at];
                        }
                        if (value == '\n') scan->line++;
                        if (used + 1 < MW_SCRIPT_WORD)
                                token->word[used++] = (char)value;
                        scan->at++;
                }
                if (scan->at < scan->length) scan->at++;
                return;
        }

        if (kind == MW_SK_WORD) {
                while (scan->at < scan->length) {
                        kind = ref_script_kind[scan->text[scan->at]];
                        if (kind != MW_SK_WORD && kind != MW_SK_WORD2 && kind != MW_SK_HASH)
                                break;
                        if (used + 1 < MW_SCRIPT_WORD)
                                token->word[used++] = (char)scan->text[scan->at];
                        scan->at++;
                }
                return;
        }

        scan->at++;
        token->kind = MW_SCRIPT_TOK_PUNCT;
        token->punct = value;
        token->word[0] = (char)value;
}

__attribute__((noinline)) void ref_script_peek(struct mw_script_read *scan, struct mw_script_token *token)
{
        if (!token) return;
        if (!scan) { ref_script_next(scan, token); return; }
        struct mw_script_read held = *scan;
        ref_script_next(scan, token);
        *scan = held;
}

__attribute__((noinline)) mw_scan_i32 ref_script_command_start(const struct mw_script_token *token)
{
        static const char *const before[] = {
                "if", "while", "until", "then", "do", "else", "elif", "!", "time", 0
        };
        if (!token) return 0;
        if (token->kind == MW_SCRIPT_TOK_DSEMI) return 1;
        if (token->kind == MW_SCRIPT_TOK_PUNCT)
                return token->punct == ';' || token->punct == '|' ||
                       token->punct == '(' || token->punct == ')' ||
                       token->punct == '{' || token->punct == '}' ||
                       token->punct == '&';
        if (token->kind != MW_SCRIPT_TOK_WORD) return 0;
        for (unsigned i = 0; before[i]; i++)
                if (ref_script_eq(token->word, before[i])) return 1;
        return 0;
}

__attribute__((noinline)) mw_scan_u32 ref_script_keyword(const struct mw_script_token *token)
{
        if (!token || token->kind != MW_SCRIPT_TOK_WORD) return 0;
        if (ref_script_eq(token->word, "if") || ref_script_eq(token->word, "while") ||
            ref_script_eq(token->word, "until") || ref_script_eq(token->word, "for") ||
            ref_script_eq(token->word, "select"))
                return MW_KW_OPENER;
        if (ref_script_eq(token->word, "fi") || ref_script_eq(token->word, "done"))
                return MW_KW_CLOSER;
        if (ref_script_eq(token->word, "case"))     return MW_KW_CASE;
        if (ref_script_eq(token->word, "esac"))     return MW_KW_ESAC;
        if (ref_script_eq(token->word, "in"))       return MW_KW_IN;
        if (ref_script_eq(token->word, "[["))       return MW_KW_TEST;
        if (ref_script_eq(token->word, "function")) return MW_KW_FUNCTION;
        return 0;
}

/* ── Helpers ──────────────────────────────────────────────────────────────── */

static unsigned long g_fails = 0;
static unsigned long g_checks = 0;

static void init_read(struct mw_script_read *r, const char *s, size_t n)
{
        memset(r, 0, sizeof(*r));
        r->text = (const unsigned char *)s;
        r->length = (mw_scan_u64)n;
        r->at = 0;
        r->line = 1;
        r->rescan = 8u * (mw_scan_u64)n;
}

static int eq_read(const struct mw_script_read *a, const struct mw_script_read *b)
{
        return a->text == b->text && a->length == b->length && a->at == b->at &&
               a->line == b->line && a->heredoc_tabs == b->heredoc_tabs &&
               a->heredoc_active == b->heredoc_active &&
               a->heredoc_length == b->heredoc_length &&
               a->heredoc_total == b->heredoc_total && a->rescan == b->rescan &&
               memcmp(a->heredoc, b->heredoc, a->heredoc_length + 1) == 0;
}

static int eq_tok(const struct mw_script_token *a, const struct mw_script_token *b)
{
        return a->kind == b->kind && a->punct == b->punct && a->fresh == b->fresh &&
               a->line == b->line && memcmp(a->word, b->word, MW_SCRIPT_WORD) == 0;
}

static void check_stream(const char *label, const char *src, size_t len)
{
        struct mw_script_read ra, rr;
        init_read(&ra, src, len);
        init_read(&rr, src, len);

        for (unsigned step = 0; step < 100000; step++) {
                struct mw_script_token pa, pr, ta, tr;
                memset(&pa, 0xaa, sizeof(pa));
                memset(&pr, 0xaa, sizeof(pr));
                mw_script_peek(&ra, &pa);
                ref_script_peek(&rr, &pr);
                g_checks++;
                if (!eq_tok(&pa, &pr) || !eq_read(&ra, &rr)) {
                        fprintf(stderr, "FAIL peek [%s] step %u: kind=%u/%u punct=%u/%u fresh=%u/%u line=%u/%u word='%s'/'%s' at=%llu/%llu\n",
                                label, step, pa.kind, pr.kind, pa.punct, pr.punct, pa.fresh, pr.fresh,
                                pa.line, pr.line, pa.word, pr.word,
                                (unsigned long long)ra.at, (unsigned long long)rr.at);
                        g_fails++;
                        return;
                }

                memset(&ta, 0x55, sizeof(ta));
                memset(&tr, 0x55, sizeof(tr));
                mw_script_next(&ra, &ta);
                ref_script_next(&rr, &tr);
                g_checks++;
                if (!eq_tok(&ta, &tr) || !eq_read(&ra, &rr)) {
                        fprintf(stderr, "FAIL next [%s] step %u: kind=%u/%u punct=%u/%u fresh=%u/%u line=%u/%u word='%s'/'%s' at=%llu/%llu\n",
                                label, step, ta.kind, tr.kind, ta.punct, tr.punct, ta.fresh, tr.fresh,
                                ta.line, tr.line, ta.word, tr.word,
                                (unsigned long long)ra.at, (unsigned long long)rr.at);
                        g_fails++;
                        return;
                }

                mw_scan_i32 ca = mw_script_command_start(&ta), cr = ref_script_command_start(&tr);
                mw_scan_u32 ka = mw_script_keyword(&ta), kr = ref_script_keyword(&tr);
                g_checks += 2;
                if (ca != cr || ka != kr) {
                        fprintf(stderr, "FAIL cmd/kw [%s] step %u word='%s': cmd=%d/%d kw=%u/%u\n",
                                label, step, ta.word, ca, cr, ka, kr);
                        g_fails++;
                        return;
                }
                if (ta.kind == MW_SCRIPT_TOK_END) break;
        }
}

/* ── 1. Exhaustive char_kind, eq, event_is, command_start, keyword ────────── */

static void test_primitives(void)
{
        for (unsigned c = 0; c < 300; c++) {
                mw_scan_u32 a = mw_script_char_kind(c);
                mw_scan_u32 r = ref_script_char_kind(c);
                g_checks++;
                if (a != r) {
                        fprintf(stderr, "FAIL char_kind(%u): got %u want %u\n", c, a, r);
                        g_fails++;
                }
        }

        /* NULL safety */
        g_checks += 8;
        if (mw_script_eq(NULL, "a") != 0 || mw_script_eq("a", NULL) != 0 || mw_script_eq(NULL, NULL) != 0) g_fails++;
        if (mw_script_event_is(NULL, "a") != 0 || mw_script_event_is("a", NULL) != 0 || mw_script_event_is(NULL, NULL) != 0) g_fails++;
        if (mw_script_command_start(NULL) != 0 || mw_script_keyword(NULL) != 0) g_fails++;
        struct mw_script_token tok;
        memset(&tok, 0x77, sizeof(tok));
        mw_script_next(NULL, &tok);
        if (tok.kind != 0 || tok.line != 0 || tok.word[0] != 0) g_fails++;
        mw_script_peek(NULL, &tok);
        if (tok.kind != 0) g_fails++;
        mw_script_next(NULL, NULL);
        mw_script_peek(NULL, NULL);
        mw_script_heredoc_start(NULL);
        mw_script_skip_heredoc(NULL);

        /* eq & event_is */
        static const char *const pairs[][2] = {
                {"", ""}, {"a", "a"}, {"a", "b"}, {"ab", "a"}, {"a", "ab"},
                {"moonwater_event", "moonwater_event"}, {"moonwater_init", "moonwater_end"},
                {"volume_up", "volume up"}, {"volume up", "volume up"},
                {"volume_up", "volume_up"}, {"volume up", "volume_up"},
                {"canvas_on", "canvas on"}, {"canvas_on", "canvas off"},
                {"lid_close", "lid close"}, {"a_b_c", "a b c"}, {"a__b", "a  b"},
                {"a_b", "a b "}, {"a b", "a_b"},
        };
        for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++) {
                g_checks += 2;
                if (mw_script_eq(pairs[i][0], pairs[i][1]) != ref_script_eq(pairs[i][0], pairs[i][1])) {
                        fprintf(stderr, "FAIL eq('%s','%s')\n", pairs[i][0], pairs[i][1]);
                        g_fails++;
                }
                if (mw_script_event_is(pairs[i][0], pairs[i][1]) != ref_script_event_is(pairs[i][0], pairs[i][1])) {
                        fprintf(stderr, "FAIL event_is('%s','%s')\n", pairs[i][0], pairs[i][1]);
                        g_fails++;
                }
        }

        /* All keywords + near misses + puncts + unzeroed tail bytes */
        static const char *const words[] = {
                "", "!", "!x", "i", "if", "iff", "in", "inn", "d", "do", "don", "done", "donex",
                "t", "th", "the", "then", "thenx", "ti", "tim", "time", "timex",
                "e", "el", "els", "else", "elsex", "eli", "elif", "elifx", "es", "esa", "esac", "esacx",
                "w", "wh", "whi", "whil", "while", "whilex",
                "u", "un", "unt", "unti", "until", "untilx",
                "f", "fi", "fix", "fo", "for", "forx", "fu", "fun", "func", "funct", "functi", "functio", "function", "functionx",
                "s", "se", "sel", "sele", "selec", "select", "selectx",
                "c", "ca", "cas", "case", "casex",
                "[", "[[", "[[[", "]]", "echo", "moonwater_event",
        };
        for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
                struct mw_script_token t;
                memset(&t, 0xcc, sizeof(t)); /* dirty tail bytes to verify no read past NUL */
                t.kind = MW_SCRIPT_TOK_WORD;
                t.punct = 0;
                t.fresh = 0;
                t.line = 1;
                strcpy(t.word, words[i]);
                g_checks += 2;
                if (mw_script_command_start(&t) != ref_script_command_start(&t)) {
                        fprintf(stderr, "FAIL cmd_start word='%s'\n", words[i]);
                        g_fails++;
                }
                if (mw_script_keyword(&t) != ref_script_keyword(&t)) {
                        fprintf(stderr, "FAIL keyword word='%s'\n", words[i]);
                        g_fails++;
                }
        }
        for (unsigned k = 0; k < 5; k++) {
                for (unsigned p = 0; p < 256; p++) {
                        struct mw_script_token t;
                        memset(&t, 0, sizeof(t));
                        t.kind = (mw_scan_u8)k;
                        t.punct = (mw_scan_u8)p;
                        g_checks += 2;
                        if (mw_script_command_start(&t) != ref_script_command_start(&t)) {
                                fprintf(stderr, "FAIL cmd_start kind=%u punct=%u\n", k, p);
                                g_fails++;
                        }
                        if (mw_script_keyword(&t) != ref_script_keyword(&t)) {
                                fprintf(stderr, "FAIL keyword kind=%u punct=%u\n", k, p);
                                g_fails++;
                        }
                }
        }
}

/* ── 2. Exhaustive edge cases & adversarial scripts ───────────────────────── */

static void test_edge_scripts(void)
{
        static const char *const scripts[] = {
                "",
                "   \t\r\n  ",
                "# comment only",
                "# comment\n# second\n",
                "\\",
                "\\\n",
                "\\\nword\\\nnext",
                ";", ";;", ";;;",
                "<", "<<", "<<<", "<<<word",
                "<<EOF\nbody\nEOF\nnext",
                "<<-EOF\n\t\tnot\n\t\tEOF\nnext",
                "<<'EOF'\nfake() {\n}\nEOF\nreal() { :; }\n",
                "<<\"E\"O\\F\nbody\nEOF\n",
                "<<\\\nEOF\nbody\nEOF\n",
                "<<UNCLOSED\nline1\nline2\n",
                "<<\n\nnext",
                "<<\n",
                "echo $((1 << 2)) $((3 << 4)) $((5 << 6))\n",
                "'unclosed single quote",
                "\"unclosed double quote\\",
                "`unclosed backtick\\`still`",
                "'single \\' quote' \"double \\\" \\\\ \n quote\" `back \\` tick`",
                "word#with#hash $var * ? [a-z] @foo 0123 -flag /path/to.file]",
                "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                "\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"",
                "<<DELIM_LONGER_THAN_64_BYTES_01234567890123456789012345678901234567890123456789\n"
                "body\n"
                "DELIM_LONGER_THAN_64_BYTES_01234567890123456789012345678901234567890123456789\n"
                "after\n",
                "function moonwater_event {\n"
                "  case $1 in\n"
                "    (mute|volume_up) echo ok ;;\n"
                "    canvas) { echo nested; } ;;\n"
                "    *) case $2 in x) : ;; esac ;;\n"
                "  esac\n"
                "}\n"
                "moonwater_init() if :; then while :; do echo fi; done; fi\n"
                "moonwater_end() [[ -n \"x\" ]]\n"
                "moonwater_poweroff() ( echo ')' )\n",
        };
        for (size_t i = 0; i < sizeof(scripts) / sizeof(scripts[0]); i++)
                check_stream("edge", scripts[i], strlen(scripts[i]));

        /* Embedded NUL bytes & high bytes 0x80..0xff */
        const char nul_script[] = "func\0tion\0 'a\0b' \"c\\\0d\" <<E\0F\nbody\nE\0F\n\x80\xff\xfe word";
        check_stream("nul_high", nul_script, sizeof(nul_script) - 1);

        /* Line number saturation past 65535 */
        char *many_nl = (char *)malloc(70000);
        memset(many_nl, '\n', 66000);
        memcpy(many_nl + 66000, "sat_word", 8);
        check_stream("line_sat", many_nl, 66008);
        free(many_nl);

        /* Huge rescan exhaustion: many unclosed << shifts */
        char *shifts = (char *)malloc(4096);
        size_t pos = 0;
        for (int i = 0; i < 400; i++) {
                memcpy(shifts + pos, "<<a\n", 4);
                pos += 4;
        }
        check_stream("unclosed_shifts", shifts, pos);
        free(shifts);
}

/* ── 3. mmap PROT_NONE guard-page safety tests (before AND after) ─────────── */

static void test_guard_pages(void)
{
        long ps = sysconf(_SC_PAGESIZE);
        if (ps <= 0) ps = 4096;
        size_t page = (size_t)ps;
        unsigned char *region = (unsigned char *)mmap(NULL, 3 * page, PROT_READ | PROT_WRITE,
                                                      MAP_PRIVATE | MAP_ANON, -1, 0);
        if (region == MAP_FAILED) {
                perror("mmap");
                g_fails++;
                return;
        }
        if (mprotect(region, page, PROT_NONE) != 0 ||
            mprotect(region + 2 * page, page, PROT_NONE) != 0) {
                perror("mprotect");
                g_fails++;
                munmap(region, 3 * page);
                return;
        }
        unsigned char *mid = region + page;

        static const char *const cases[] = {
                "", "a", ";", ";;", "<", "<<", "<<<", "\\", "\\\n", "#hi",
                "'unclosed", "\"unclosed\\", "<<EOF\nhello\nEOF", "<<-EOF\n\tEOF",
                "<<UNCLOSED\nline1\nline2", "function moonwater_event { case $1 in *) : ;; esac; }",
        };
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
                size_t len = strlen(cases[i]);
                /* Flush against trailing PROT_NONE guard page */
                unsigned char *tail = mid + page - len;
                memcpy(tail, cases[i], len);
                check_stream("guard_after", (const char *)tail, len);

                /* Flush against leading PROT_NONE guard page */
                memcpy(mid, cases[i], len);
                check_stream("guard_before", (const char *)mid, len);
        }

        /* Also test mw_script_eq & mw_script_event_is flush against guard pages */
        const char *s1 = "volume_up";
        const char *s2 = "volume up";
        size_t l1 = strlen(s1) + 1;
        memcpy(mid + page - l1, s1, l1);
        memcpy(mid, s2, l1);
        g_checks += 2;
        if (mw_script_event_is((const char *)(mid + page - l1), (const char *)mid) != 1) g_fails++;
        if (mw_script_eq((const char *)(mid + page - l1), (const char *)(mid + page - l1)) != 1) g_fails++;

        munmap(region, 3 * page);
}

/* ── 4. Randomized differential fuzzing against -O2 C reference ───────────── */

static uint64_t g_rng = 0x9e3779b97f4a7c15ULL;
static uint32_t xorshift(void)
{
        uint64_t x = g_rng;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        g_rng = x;
        return (uint32_t)x;
}

static void test_fuzz(void)
{
        static const char alpha[] =
                "abcdeifnsuw019 \t\r\n#\\;'\"`()|{}<>&-_$*?[@]./\x00\x80\xff";
        char buf[512];
        for (unsigned iter = 0; iter < 5000; iter++) {
                size_t len = xorshift() % 256;
                for (size_t j = 0; j < len; j++) {
                        uint32_t r = xorshift();
                        if ((r & 15) == 0 && j + 2 < len) {
                                buf[j] = '<'; buf[j + 1] = '<'; j++;
                        } else {
                                buf[j] = alpha[r % (sizeof(alpha) - 1)];
                        }
                }
                check_stream("fuzz", buf, len);
                if (g_fails > 10) return;
        }
}

/* ── 5. Speed benchmark (ns/op ASM vs -O2 C reference) ────────────────────── */

static uint64_t now_ns(void)
{
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static volatile uint64_t g_sink = 0;

static void run_bench(void)
{
        static const char sample[] =
                "# Moonwater machine script benchmark\n"
                "function moonwater_init {\n"
                "  echo 'initializing moonwater' && set -e\n"
                "  cat <<-'EOF'\n"
                "\t\tfunction fake_inside_heredoc() { case $1 in mute) : ;; esac; }\n"
                "\t\tEOF\n"
                "}\n"
                "moonwater_event() {\n"
                "  case $1 in\n"
                "    mute|volume_up|volume_down) audio_ctl \"$1\" ;;\n"
                "    canvas) canvas_toggle ;;\n"
                "    tablet|dock|headphone) pair_handle \"$1\" \"$2\" ;;\n"
                "    lid_close) suspend_now ;;\n"
                "    *) log \"unhandled: $1\" ;;\n"
                "  esac\n"
                "}\n"
                "moonwater_end() if [[ -n \"$1\" ]]; then while :; do break; done; fi\n";
        size_t slen = strlen(sample);
        const int iters = 20000;
        uint64_t t0, t1, asm_ns, ref_ns, acc = 0;

        /* Warmup */
        for (int i = 0; i < 200; i++) {
                struct mw_script_read r;
                struct mw_script_token t;
                init_read(&r, sample, slen);
                do { mw_script_next(&r, &t); acc += t.kind + mw_script_keyword(&t) + (uint64_t)mw_script_command_start(&t); } while (t.kind);
        }

        t0 = now_ns();
        for (int i = 0; i < iters; i++) {
                struct mw_script_read r;
                struct mw_script_token t;
                init_read(&r, sample, slen);
                do {
                        mw_script_next(&r, &t);
                        acc += t.kind + mw_script_keyword(&t) + (uint64_t)mw_script_command_start(&t);
                } while (t.kind != MW_SCRIPT_TOK_END);
        }
        t1 = now_ns();
        asm_ns = t1 - t0;

        t0 = now_ns();
        for (int i = 0; i < iters; i++) {
                struct mw_script_read r;
                struct mw_script_token t;
                init_read(&r, sample, slen);
                do {
                        ref_script_next(&r, &t);
                        acc += t.kind + ref_script_keyword(&t) + (uint64_t)ref_script_command_start(&t);
                } while (t.kind != MW_SCRIPT_TOK_END);
        }
        t1 = now_ns();
        ref_ns = t1 - t0;
        g_sink = acc;

        printf("BENCH full_scan (%zuB x %d): ASM %.1f ns/op  vs  C-O2 %.1f ns/op  (%.2fx)\n",
               slen, iters, (double)asm_ns / iters, (double)ref_ns / iters,
               (double)ref_ns / (double)(asm_ns ? asm_ns : 1));
}

int main(void)
{
        test_primitives();
        test_edge_scripts();
        test_guard_pages();
        test_fuzz();
        if (g_fails) {
                fprintf(stderr, "FAILED: %lu failures out of %lu checks\n", g_fails, g_checks);
                return 1;
        }
        printf("PASS: %lu differential/security/guard-page/fuzz checks (0 failures)\n", g_checks);
        run_bench();
        return 0;
}
