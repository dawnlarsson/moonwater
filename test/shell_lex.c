/*
        Differential C-reference parity, fuzz & throughput/size benchmark for
        src/sh/shell_lex.c (arm64, x86-64, riscv64 pure-ASM core primitive).

        Build & run:
          cc -O2 -Wall -Werror test/shell_lex.c -o /tmp/mw_shell_lex && /tmp/mw_shell_lex
*/
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>

#define SH_SHELL_LEX_IMPLEMENTATION
#include "../src/sh/shell_lex.c"

/* ── Reference C implementations ───────────────────────────────────────────── */

static int ref_is_blank(unsigned char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int ref_is_op(unsigned char c) {
        return c == '|' || c == '&' || c == ';' ||
               c == '<' || c == '>' || c == '(' || c == ')';
}

static __attribute__((noinline)) sh_i32 ref_sh_lex_line(const char *src, sh_u64 len, struct sh_token *out, sh_u32 cap) {
        if (!src || !out || cap == 0) return 0;
        sh_u64 i = 0;
        sh_u32 count = 0;
        while (i < len) {
                while (i < len && ref_is_blank((unsigned char)src[i])) i++;
                if (i >= len) break;
                unsigned char c = (unsigned char)src[i];
                if (c == '#') break;
                sh_u64 start = i;
                if (ref_is_op(c)) {
                        sh_u32 op = 0;
                        sh_u64 oplen = 1;
                        sh_u64 rem = len - i;
                        if (c == '&') {
                                if (rem >= 3 && src[i + 1] == '>' && src[i + 2] == '>') {
                                        op = SH_OP_ANDDGREAT; oplen = 3;
                                } else if (rem >= 2 && src[i + 1] == '&') {
                                        op = SH_OP_AND_IF; oplen = 2;
                                } else if (rem >= 2 && src[i + 1] == '>') {
                                        op = SH_OP_ANDGREAT; oplen = 2;
                                } else {
                                        op = SH_OP_AMP; oplen = 1;
                                }
                        } else if (c == '|') {
                                if (rem >= 2 && src[i + 1] == '|') {
                                        op = SH_OP_OR_IF; oplen = 2;
                                } else if (rem >= 2 && src[i + 1] == '&') {
                                        op = SH_OP_PIPEAND; oplen = 2;
                                } else {
                                        op = SH_OP_PIPE; oplen = 1;
                                }
                        } else if (c == ';') {
                                if (rem >= 3 && src[i + 1] == ';' && src[i + 2] == '&') {
                                        op = SH_OP_DSEMIAND; oplen = 3;
                                } else if (rem >= 2 && src[i + 1] == ';') {
                                        op = SH_OP_DSEMI; oplen = 2;
                                } else if (rem >= 2 && src[i + 1] == '&') {
                                        op = SH_OP_SEMIAND; oplen = 2;
                                } else {
                                        op = SH_OP_SEMI; oplen = 1;
                                }
                        } else if (c == '<') {
                                if (rem >= 3 && src[i + 1] == '<' && src[i + 2] == '<') {
                                        op = SH_OP_HERESTRING; oplen = 3;
                                } else if (rem >= 2 && src[i + 1] == '<') {
                                        op = SH_OP_DLESS; oplen = 2;
                                } else if (rem >= 2 && src[i + 1] == '&') {
                                        op = SH_OP_LESSAND; oplen = 2;
                                } else if (rem >= 2 && src[i + 1] == '>') {
                                        op = SH_OP_LESSGREAT; oplen = 2;
                                } else {
                                        op = SH_OP_LESS; oplen = 1;
                                }
                        } else if (c == '>') {
                                if (rem >= 2 && src[i + 1] == '>') {
                                        op = SH_OP_DGREAT; oplen = 2;
                                } else if (rem >= 2 && src[i + 1] == '&') {
                                        op = SH_OP_GREATAND; oplen = 2;
                                } else if (rem >= 2 && src[i + 1] == '|') {
                                        op = SH_OP_CLOBBER; oplen = 2;
                                } else {
                                        op = SH_OP_GREAT; oplen = 1;
                                }
                        } else if (c == '(') {
                                op = SH_OP_LPAREN; oplen = 1;
                        } else {
                                op = SH_OP_RPAREN; oplen = 1;
                        }
                        out[count].kind = SH_LEX_OPERATOR;
                        out[count].op = op;
                        out[count].text = src + start;
                        out[count].length = oplen;
                        out[count].at = start;
                        i += oplen;
                        count++;
                        if (count >= cap) break;
                } else {
                        while (i < len) {
                                unsigned char w = (unsigned char)src[i];
                                if (ref_is_blank(w) || ref_is_op(w)) break;
                                if (w == '\\') {
                                        i++;
                                        if (i < len) i++;
                                } else if (w == '\'') {
                                        i++;
                                        while (i < len && src[i] != '\'') i++;
                                        if (i >= len) return -1;
                                        i++;
                                } else if (w == '"') {
                                        i++;
                                        while (i < len && src[i] != '"') {
                                                if (src[i] == '\\' && i + 1 < len) i += 2;
                                                else i++;
                                        }
                                        if (i >= len) return -1;
                                        i++;
                                } else {
                                        i++;
                                }
                        }
                        out[count].kind = SH_LEX_WORD;
                        out[count].op = 0;
                        out[count].text = src + start;
                        out[count].length = i - start;
                        out[count].at = start;
                        count++;
                        if (count >= cap) break;
                }
        }
        return (sh_i32)count;
}

static __attribute__((noinline)) sh_u64 ref_sh_unquote(const char *src, sh_u64 len, char *dst, sh_u64 dst_cap) {
        if (!dst || dst_cap == 0) return 0;
        if (!src || len == 0) {
                dst[0] = '\0';
                return 0;
        }
        sh_u64 limit = dst_cap - 1;
        sh_u64 i = 0, out = 0;
        int mode = 0;
        while (i < len && out < limit) {
                char c = src[i];
                if (mode == 0) {
                        if (c == '\\') {
                                if (i + 1 < len) {
                                        dst[out++] = src[i + 1];
                                        i += 2;
                                } else {
                                        i++;
                                }
                        } else if (c == '\'') {
                                mode = 1;
                                i++;
                        } else if (c == '"') {
                                mode = 2;
                                i++;
                        } else {
                                dst[out++] = c;
                                i++;
                        }
                } else if (mode == 1) {
                        if (c == '\'') {
                                mode = 0;
                                i++;
                        } else {
                                dst[out++] = c;
                                i++;
                        }
                } else {
                        if (c == '"') {
                                mode = 0;
                                i++;
                        } else if (c == '\\') {
                                if (i + 1 < len) {
                                        char n = src[i + 1];
                                        if (n == '"' || n == '\\' || n == '$' || n == '`' || n == '\n') {
                                                dst[out++] = n;
                                                i += 2;
                                        } else {
                                                dst[out++] = '\\';
                                                i++;
                                        }
                                } else {
                                        dst[out++] = '\\';
                                        i++;
                                }
                        } else {
                                dst[out++] = c;
                                i++;
                        }
                }
        }
        dst[out] = '\0';
        return out;
}

static __attribute__((noinline)) sh_i32 ref_sh_valid_name(const char *s, sh_u64 len) {
        if (!s || len == 0) return 0;
        unsigned char c = (unsigned char)s[0];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_')) return 0;
        for (sh_u64 i = 1; i < len; i++) {
                c = (unsigned char)s[i];
                if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
                        return 0;
        }
        return 1;
}

static __attribute__((noinline)) sh_i32 ref_sh_param_name(const char *s, sh_u64 len) {
        if (!s || len == 0) return 0;
        unsigned char c = (unsigned char)s[0];
        if (len == 1 && (c == '@' || c == '*' || c == '#' || c == '?' || c == '$' || c == '!' || c == '-'))
                return 1;
        if (c >= '0' && c <= '9') {
                for (sh_u64 i = 1; i < len; i++) {
                        unsigned char d = (unsigned char)s[i];
                        if (d < '0' || d > '9') return 0;
                }
                return 1;
        }
        return ref_sh_valid_name(s, len);
}

static __attribute__((noinline)) const char *ref_sh_escaped_end(const char *at, sh_u32 quote) {
        if (!at) return NULL;
        unsigned char q = (unsigned char)quote;
        while (*at) {
                unsigned char c = (unsigned char)*at;
                if (c == q) return at;
                at++;
                if (c == '\\' && *at) at++;
        }
        return at;
}

static __attribute__((noinline)) sh_i32 ref_sh_assign_split(const char *w, sh_u64 len, sh_u64 *name_len_out) {
        if (!w || len == 0) return 0;
        unsigned char c = (unsigned char)w[0];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_')) return 0;
        sh_u64 i = 1;
        while (i < len) {
                c = (unsigned char)w[i];
                if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
                        break;
                i++;
        }
        if (i >= len) return 0;
        if (w[i] == '=') {
                if (name_len_out) *name_len_out = i;
                return 1;
        }
        if (w[i] == '+' && i + 1 < len && w[i + 1] == '=') {
                if (name_len_out) *name_len_out = i;
                return 2;
        }
        return 0;
}

static __attribute__((noinline)) sh_i32 ref_sh_param_pattern(const char *at) {
        if (!at) return 0;
        const unsigned char *s = (const unsigned char *)at;
        if (*s == '!') s++;
        if (*s == '#') s++;
        if ((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z') || *s == '_') {
                while ((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || *s == '_')
                        s++;
        } else if (*s >= '0' && *s <= '9') {
                while (*s >= '0' && *s <= '9') s++;
        } else if (*s) {
                s++;
        }
        if (*s == '[') {
                sh_u64 depth = 1;
                s++;
                while (*s && depth) {
                        if (*s == '\\' && s[1]) s += 2;
                        else if (*s == '[') { depth++; s++; }
                        else if (*s == ']') { depth--; s++; }
                        else s++;
                }
        }
        return (*s == '#' || *s == '%') ? 1 : 0;
}

static __attribute__((noinline)) sh_i32 ref_sh_dless_candidate(const char *line, const char *at) {
        if (!line || !at || at < line) return 0;
        if (at[0] && at[1] == '<' && at[2] != '<') return 1;
        const char *before = at;
        sh_u64 dist = (sh_u64)(at - line);
        while (dist >= 2 && before[-1] == '\n' && before[-2] == '\\') {
                before -= 2;
                dist -= 2;
        }
        return (dist && before[-1] == '<') ? 1 : 0;
}

static __attribute__((noinline)) sh_i32 ref_sh_word_is(const char *at, const char *word, sh_u64 length) {
        if (!at || !word) return 0;
        for (sh_u64 i = 0; i < length; i++) {
                if (at[i] != word[i] || at[i] == '\0') return 0;
        }
        unsigned char a = (unsigned char)at[length];
        return (a == 0 || a == ' ' || a == '\t' || a == '\n' ||
                a == '|' || a == '&' || a == ';' || a == '<' || a == '>' || a == '(' || a == ')') ? 1 : 0;
}

static __attribute__((noinline)) sh_i32 ref_sh_assign_head(const char *text, sh_u64 length) {
        if (!text || !length) return 0;
        if (text[length - 1] == '+') length--;
        if (!length) return 0;
        unsigned char c = (unsigned char)text[0];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_')) return 0;
        sh_u64 at = 1;
        while (at < length) {
                c = (unsigned char)text[at];
                if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
                        break;
                at++;
        }
        if (at == length) return 1;
        return (text[at] == '[' && text[length - 1] == ']' && (length - at) > 2) ? 1 : 0;
}

/* ── Hardware mmap guard-page OOB read/write trap suite ────────────────────── */

static int g_failures = 0;
static uint64_t g_checks = 0;

static void run_guard_page_security_tests(void) {
        long ps = sysconf(_SC_PAGESIZE);
        size_t page = (ps > 0) ? (size_t)ps : 4096;
        /* [PROT_NONE guard] [RW data page] [PROT_NONE guard] */
        char *region = (char *)mmap(NULL, 3 * page, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (region == MAP_FAILED) {
                fprintf(stderr, "FAIL: mmap guard region failed\n");
                g_failures++;
                return;
        }
        char *rw = region + page;
        if (mprotect(rw, page, PROT_READ | PROT_WRITE) != 0) {
                fprintf(stderr, "FAIL: mprotect rw page failed\n");
                g_failures++;
                munmap(region, 3 * page);
                return;
        }

        /* 1. End-of-page (1-byte OOB read trap) for bounded & NUL-terminated inputs */
        const char *edge_inputs[] = {
                "", "a", "<", "<<", "<<<", "&", "&>", "&>>", ";", ";;", ";;&",
                "|", "||", "|&", ">", ">>", ">|", ">&", "(", ")", "'", "\"", "\\",
                "a\\", "\"\\", "'unclosed", "\"unclosed", "a[b]#", "!#a[\\]#",
                "a+", "a[]+", "a[0]+", "in", "in ", "in|", "in\t", "in\r", "foo=bar", "foo+=bar"
        };
        for (size_t i = 0; i < sizeof(edge_inputs) / sizeof(edge_inputs[0]); i++) {
                size_t len = strlen(edge_inputs[i]);
                /* Place WITHOUT NUL right at end of RW page for bounded functions */
                char *end_raw = rw + page - len;
                if (len) memcpy(end_raw, edge_inputs[i], len);
                struct sh_token toks[8];
                char uq[64];
                g_checks += 6;
                if (sh_lex_line(end_raw, len, toks, 8) != ref_sh_lex_line(end_raw, len, toks, 8)) g_failures++;
                if (sh_unquote(end_raw, len, uq, sizeof(uq)) != ref_sh_unquote(end_raw, len, uq, sizeof(uq))) g_failures++;
                if (sh_valid_name(end_raw, len) != ref_sh_valid_name(end_raw, len)) g_failures++;
                if (sh_param_name(end_raw, len) != ref_sh_param_name(end_raw, len)) g_failures++;
                sh_u64 n1 = 0, n2 = 0;
                if (sh_assign_split(end_raw, len, &n1) != ref_sh_assign_split(end_raw, len, &n2) || n1 != n2) g_failures++;
                if (sh_assign_head(end_raw, len) != ref_sh_assign_head(end_raw, len)) g_failures++;

                /* Place WITH NUL right at end of RW page for NUL-terminated functions */
                char *end_nul = rw + page - (len + 1);
                memcpy(end_nul, edge_inputs[i], len + 1);
                g_checks += 4;
                if (sh_escaped_end(end_nul, '"') != ref_sh_escaped_end(end_nul, '"')) g_failures++;
                if (sh_param_pattern(end_nul) != ref_sh_param_pattern(end_nul)) g_failures++;
                if (sh_dless_candidate(end_nul, end_nul) != ref_sh_dless_candidate(end_nul, end_nul)) g_failures++;
                if (sh_word_is(end_nul, "in", 2) != ref_sh_word_is(end_nul, "in", 2)) g_failures++;

                /* Place at START of RW page (1-byte under-read trap for backward scans) */
                memcpy(rw, edge_inputs[i], len + 1);
                for (size_t pos = 0; pos <= len; pos++) {
                        g_checks++;
                        if (sh_dless_candidate(rw, rw + pos) != ref_sh_dless_candidate(rw, rw + pos)) g_failures++;
                }
        }

        /* 2. Exact output-buffer end-of-page (1-byte OOB write trap) */
        for (sh_u32 cap = 1; cap <= 4; cap++) {
                struct sh_token *out_end = (struct sh_token *)(rw + page - (size_t)cap * sizeof(struct sh_token));
                const char *s = "a && b || c <<< d &>> e ;;& f";
                g_checks++;
                sh_i32 rc = sh_lex_line(s, strlen(s), out_end, cap);
                if (rc != (sh_i32)cap) g_failures++;
        }
        for (size_t dcap = 1; dcap <= 16; dcap++) {
                char *dst_end = rw + page - dcap;
                const char *s = "hello_\"world\"_'test'_\\escaped_long_string";
                g_checks++;
                sh_u64 n = sh_unquote(s, strlen(s), dst_end, dcap);
                if (n != dcap - 1 || dst_end[dcap - 1] != '\0') g_failures++;
        }

        /* 3. Adversarial deep nesting (4096 brackets) & long \\\n chains (1024 pairs) */
        {
                char deep[4104];
                deep[0] = 'x';
                for (int i = 0; i < 2048; i++) deep[1 + i] = '[';
                for (int i = 0; i < 2048; i++) deep[2049 + i] = ']';
                deep[4097] = '#';
                deep[4098] = '\0';
                g_checks += 2;
                if (sh_param_pattern(deep) != 1) g_failures++;
                deep[4097] = '\0'; /* unclosed/no-op */
                if (sh_param_pattern(deep) != 0) g_failures++;

                char cont[2060];
                cont[0] = '<';
                for (int i = 0; i < 1024; i++) { cont[1 + 2 * i] = '\\'; cont[2 + 2 * i] = '\n'; }
                cont[2049] = '<';
                cont[2050] = '\0';
                g_checks += 2;
                if (sh_dless_candidate(cont, cont + 2049) != 1) g_failures++;
                cont[0] = 'x';
                if (sh_dless_candidate(cont, cont + 2049) != 0) g_failures++;
        }

        munmap(region, 3 * page);
}

/* ── Test harness helpers ──────────────────────────────────────────────────── */

static void check_lex_case(const char *label, const char *src, sh_u64 len, sh_u32 cap) {
        enum { MAX_CAP = 256 };
        struct sh_token ref_toks[MAX_CAP + 1];
        struct sh_token asm_toks[MAX_CAP + 1];
        if (cap > MAX_CAP) cap = MAX_CAP;

        memset(ref_toks, 0xA5, sizeof(ref_toks));
        memset(asm_toks, 0xA5, sizeof(asm_toks));

        sh_i32 rc_ref = ref_sh_lex_line(src, len, ref_toks, cap);
        sh_i32 rc_asm = sh_lex_line(src, len, asm_toks, cap);
        g_checks++;

        if (rc_ref != rc_asm) {
                if (g_failures < 20)
                        fprintf(stderr, "FAIL [lex %s] rc mismatch: asm=%d ref=%d (len=%llu cap=%u)\n",
                                label, rc_asm, rc_ref, (unsigned long long)len, cap);
                g_failures++;
                return;
        }
        if (rc_asm > 0) {
                for (sh_i32 k = 0; k < rc_asm; k++) {
                        if (ref_toks[k].kind != asm_toks[k].kind ||
                            ref_toks[k].op != asm_toks[k].op ||
                            ref_toks[k].text != asm_toks[k].text ||
                            ref_toks[k].length != asm_toks[k].length ||
                            ref_toks[k].at != asm_toks[k].at) {
                                if (g_failures < 20)
                                        fprintf(stderr,
                                                "FAIL [lex %s] tok[%d] mismatch: "
                                                "asm={k=%u op=%u len=%llu at=%llu} "
                                                "ref={k=%u op=%u len=%llu at=%llu}\n",
                                                label, k,
                                                asm_toks[k].kind, asm_toks[k].op,
                                                (unsigned long long)asm_toks[k].length,
                                                (unsigned long long)asm_toks[k].at,
                                                ref_toks[k].kind, ref_toks[k].op,
                                                (unsigned long long)ref_toks[k].length,
                                                (unsigned long long)ref_toks[k].at);
                                g_failures++;
                                return;
                        }
                }
        }
        unsigned char sentinel[sizeof(struct sh_token)];
        memset(sentinel, 0xA5, sizeof(sentinel));
        if (memcmp(&asm_toks[cap], sentinel, sizeof(sentinel)) != 0) {
                if (g_failures < 20)
                        fprintf(stderr, "FAIL [lex %s] wrote past cap=%u!\n", label, cap);
                g_failures++;
        }
}

static void check_unquote_case(const char *label, const char *src, sh_u64 len, sh_u64 dst_cap) {
        enum { MAX_DST = 512 };
        char ref_buf[MAX_DST + 4];
        char asm_buf[MAX_DST + 4];
        if (dst_cap > MAX_DST) dst_cap = MAX_DST;

        memset(ref_buf, 0x5A, sizeof(ref_buf));
        memset(asm_buf, 0x5A, sizeof(asm_buf));

        sh_u64 r_ref = ref_sh_unquote(src, len, ref_buf, dst_cap);
        sh_u64 r_asm = sh_unquote(src, len, asm_buf, dst_cap);
        g_checks++;

        if (r_ref != r_asm) {
                if (g_failures < 20)
                        fprintf(stderr, "FAIL [unquote %s] len mismatch: asm=%llu ref=%llu (dst_cap=%llu)\n",
                                label, (unsigned long long)r_asm, (unsigned long long)r_ref,
                                (unsigned long long)dst_cap);
                g_failures++;
                return;
        }
        if (dst_cap > 0) {
                if (memcmp(ref_buf, asm_buf, (size_t)r_asm + 1) != 0) {
                        if (g_failures < 20)
                                fprintf(stderr, "FAIL [unquote %s] content mismatch (dst_cap=%llu)\n",
                                        label, (unsigned long long)dst_cap);
                        g_failures++;
                        return;
                }
        }
        for (size_t k = (size_t)dst_cap; k < sizeof(asm_buf); k++) {
                if ((unsigned char)asm_buf[k] != 0x5A) {
                        if (g_failures < 20)
                                fprintf(stderr, "FAIL [unquote %s] wrote past dst_cap=%llu at %zu!\n",
                                        label, (unsigned long long)dst_cap, k);
                        g_failures++;
                        return;
                }
        }
}

static uint64_t rng_state = 0xD1B54A32D192ED03ULL;
static uint64_t rng_u64(void) {
        uint64_t x = rng_state;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        rng_state = x;
        return x * 0x2545F4914F6CDD1DULL;
}

static uint64_t now_ns(void) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

int main(void) {
        struct { const char *s; sh_u32 op; sh_u64 len; } all_ops[22] = {
                { "&&",  SH_OP_AND_IF,     2 },
                { "||",  SH_OP_OR_IF,      2 },
                { ";;",  SH_OP_DSEMI,      2 },
                { "<<",  SH_OP_DLESS,      2 },
                { ">>",  SH_OP_DGREAT,     2 },
                { "<&",  SH_OP_LESSAND,    2 },
                { ">&",  SH_OP_GREATAND,   2 },
                { "<>",  SH_OP_LESSGREAT,  2 },
                { ">|",  SH_OP_CLOBBER,    2 },
                { ";",   SH_OP_SEMI,       1 },
                { "|",   SH_OP_PIPE,       1 },
                { "&",   SH_OP_AMP,        1 },
                { "<",   SH_OP_LESS,       1 },
                { ">",   SH_OP_GREAT,      1 },
                { "(",   SH_OP_LPAREN,     1 },
                { ")",   SH_OP_RPAREN,     1 },
                { "&>",  SH_OP_ANDGREAT,   2 },
                { "&>>", SH_OP_ANDDGREAT,  3 },
                { "<<<", SH_OP_HERESTRING, 3 },
                { "|&",  SH_OP_PIPEAND,    2 },
                { ";&",  SH_OP_SEMIAND,    2 },
                { ";;&", SH_OP_DSEMIAND,   3 },
        };
        for (int i = 0; i < 22; i++) {
                struct sh_token tok;
                sh_i32 n = sh_lex_line(all_ops[i].s, all_ops[i].len, &tok, 1);
                g_checks++;
                if (n != 1 || tok.kind != SH_LEX_OPERATOR || tok.op != all_ops[i].op ||
                    tok.length != all_ops[i].len || tok.at != 0 || tok.text != all_ops[i].s) {
                        fprintf(stderr, "FAIL [op %s] got n=%d kind=%u op=%u len=%llu\n",
                                all_ops[i].s, n, tok.kind, tok.op, (unsigned long long)tok.length);
                        g_failures++;
                }
                check_lex_case(all_ops[i].s, all_ops[i].s, all_ops[i].len, 8);
        }

        const char *lex_cases[] = {
                "",
                "   \t\r\n  ",
                "# full line comment",
                "   # indented comment",
                "echo hello world # trailing comment",
                "echo foo#bar baz#qux # real comment",
                "echo '#not a comment' \"#also not\"",
                "a&&b||c;;d<<e>>f<&g>&h<>i>|j;k|l&m<n>o(p)q&>r&>>s<<<t|&u;&v;;&w",
                "&&& ||| ;;; <<< >>> &&>> ;;;& <<<<",
                "foo\"bar\"'baz'qux",
                "\"\" '' \"a\"'b'\"\"''",
                "a\\ b\\|c\\&d\\;e\\<f\\>g\\(h\\)i\\#j\\\\k\\'l\\\"m",
                "trailing_backslash\\",
                "\"escaped \\\" quote\" \"escaped \\\\ slash\" \"\\$var \\`cmd\\`\"",
                "'single \\\" \\\\ \\' no escapes'",
                "echo 'unclosed single",
                "echo \"unclosed double",
                "echo \"unclosed trailing esc\\",
                "echo \"unclosed escaped quote\\\"",
                "echo 'ok' && (cat <<< 'hello world' |& tee &>> out.log);;&",
                "  \t\r\n (a && b) || (c | d) >| out 2>&1 <&0 <>rw ;& ;;& ",
        };
        for (size_t i = 0; i < sizeof(lex_cases) / sizeof(lex_cases[0]); i++) {
                sh_u64 len = (sh_u64)strlen(lex_cases[i]);
                check_lex_case("corpus", lex_cases[i], len, 64);
                check_lex_case("corpus_cap1", lex_cases[i], len, 1);
                check_lex_case("corpus_cap3", lex_cases[i], len, 3);
                check_lex_case("corpus_cap0", lex_cases[i], len, 0);
        }

        {
                struct sh_token toks[4];
                g_checks++;
                if (sh_lex_line(NULL, 10, toks, 4) != 0) g_failures++;
                if (sh_lex_line("abc", 3, NULL, 4) != 0) g_failures++;
                if (sh_lex_line("abc", 3, toks, 0) != 0) g_failures++;
                const char emb[] = { 'a', '\0', 'b', ' ', '&', '&', ' ', '\'', '\0', '\'' };
                check_lex_case("embedded_nul", emb, sizeof(emb), 16);
        }

        const char *uq_cases[] = {
                "",
                "plain_word",
                "\"\"",
                "''",
                "foo\"bar\"'baz'",
                "a\\ b\\c\\\\d\\'e\\\"f",
                "trailing\\",
                "'single \\\" \\\\ \\$ \\` \\n verbatim'",
                "\"double \\\" \\\\ \\$ \\` \\\n keep:\\a\\t\\x end\"",
                "\"dangling_bs\\",
                "\"unclosed double",
                "'unclosed single",
                "\\a\"\\b\\\"\\$\\`\\\\\\\n\"'\\c'",
        };
        for (size_t i = 0; i < sizeof(uq_cases) / sizeof(uq_cases[0]); i++) {
                sh_u64 len = (sh_u64)strlen(uq_cases[i]);
                for (sh_u64 cap = 0; cap <= len + 4; cap++)
                        check_unquote_case("uq_corpus", uq_cases[i], len, cap);
        }

        {
                char buf[16];
                g_checks++;
                if (sh_unquote("abc", 3, NULL, 16) != 0) g_failures++;
                if (sh_unquote("abc", 3, buf, 0) != 0) g_failures++;
                buf[0] = 'X';
                if (sh_unquote(NULL, 5, buf, 16) != 0 || buf[0] != '\0') g_failures++;
                buf[0] = 'X';
                if (sh_unquote("abc", 0, buf, 16) != 0 || buf[0] != '\0') g_failures++;
                const char emb[] = { 'a', '\\', '\0', '"', '\0', 'b', '"' };
                for (sh_u64 cap = 0; cap <= 10; cap++)
                        check_unquote_case("uq_embedded_nul", emb, sizeof(emb), cap);
        }

        static const char alphabet[] = "abcXYZ019_/-=. \t\r\n#|&;<>()\\'\"$`";
        const size_t alpha_len = sizeof(alphabet) - 1;
        char fuzz_buf[128];

        for (int iter = 0; iter < 20000; iter++) {
                sh_u64 len = rng_u64() % 96;
                for (sh_u64 j = 0; j < len; j++) {
                        uint64_t r = rng_u64();
                        if ((r & 63) == 0) fuzz_buf[j] = (char)(r >> 8);
                        else fuzz_buf[j] = alphabet[r % alpha_len];
                }
                fuzz_buf[len] = '\0';
                sh_u32 cap = (sh_u32)(rng_u64() % 40);
                check_lex_case("fuzz", fuzz_buf, len, cap);
                sh_u64 dst_cap = rng_u64() % 100;
                check_unquote_case("fuzz", fuzz_buf, len, dst_cap);

                /* Fuzz sh_valid_name, sh_param_name, sh_escaped_end, sh_assign_split,
                   sh_param_pattern, sh_dless_candidate, sh_word_is, sh_assign_head */
                g_checks += 8;
                if (sh_valid_name(fuzz_buf, len) != ref_sh_valid_name(fuzz_buf, len)) g_failures++;
                if (sh_param_name(fuzz_buf, len) != ref_sh_param_name(fuzz_buf, len)) g_failures++;
                sh_u32 q = (sh_u32)(rng_u64() & 0x3FF); /* includes > 255 to test low-byte masking */
                if (sh_escaped_end(fuzz_buf, q) != ref_sh_escaped_end(fuzz_buf, q)) g_failures++;
                sh_u64 nl_a = 0xDEAD, nl_r = 0xDEAD;
                sh_i32 as_a = sh_assign_split(fuzz_buf, len, &nl_a);
                sh_i32 as_r = ref_sh_assign_split(fuzz_buf, len, &nl_r);
                if (as_a != as_r || nl_a != nl_r) g_failures++;
                if (sh_param_pattern(fuzz_buf) != ref_sh_param_pattern(fuzz_buf)) g_failures++;
                sh_u64 pos = len ? (rng_u64() % (len + 1)) : 0;
                if (sh_dless_candidate(fuzz_buf, fuzz_buf + pos) != ref_sh_dless_candidate(fuzz_buf, fuzz_buf + pos)) g_failures++;
                sh_u64 wlen = len ? (rng_u64() % (len > 6 ? 6 : len)) : 0;
                if (sh_word_is(fuzz_buf, fuzz_buf, wlen) != ref_sh_word_is(fuzz_buf, fuzz_buf, wlen)) g_failures++;
                if (sh_assign_head(fuzz_buf, len) != ref_sh_assign_head(fuzz_buf, len)) g_failures++;
        }

        /* Targeted checks for sh_param_pattern, sh_dless_candidate, sh_word_is, sh_assign_head */
        {
                const char *pp[] = { "", "a#", "a%", "!a#", "#a%", "!#a[0]#", "a[b[c]d]%", "a[\\]]#", "a[\\]#",
                                     "123#", "@#", "*%", "a[unclosed", "a[trail\\", "a:b", "a-b", "!#%" };
                for (size_t i = 0; i < sizeof(pp) / sizeof(pp[0]); i++) {
                        g_checks++;
                        if (sh_param_pattern(pp[i]) != ref_sh_param_pattern(pp[i])) g_failures++;
                }
                g_checks += 4;
                if (sh_param_pattern(NULL) != 0) g_failures++;
                if (sh_dless_candidate(NULL, "a") != 0) g_failures++;
                if (sh_dless_candidate("abc", NULL) != 0) g_failures++;
                { const char *s = "abc"; if (sh_dless_candidate(s + 2, s) != 0) g_failures++; }

                const char *ah[] = { "", "+", "a", "a+", "_9+", "0a", "a[0]", "a[0]+", "a[]", "a[]+", "a[0", "a[0]x", "a[b+c]+" };
                for (size_t i = 0; i < sizeof(ah) / sizeof(ah[0]); i++) {
                        sh_u64 l = (sh_u64)strlen(ah[i]);
                        g_checks++;
                        if (sh_assign_head(ah[i], l) != ref_sh_assign_head(ah[i], l)) g_failures++;
                }
                /* All 256 follower bytes for sh_word_is */
                char wbuf[8] = "inX";
                for (int b = 0; b < 256; b++) {
                        wbuf[2] = (char)b;
                        g_checks++;
                        if (sh_word_is(wbuf, "in", 2) != ref_sh_word_is(wbuf, "in", 2)) g_failures++;
                }
        }

        /* Hardware mmap guard-page OOB read/write trap suite */
        run_guard_page_security_tests();

        /* Targeted checks for sh_valid_name, sh_param_name, sh_escaped_end, sh_assign_split */
        {
                const char *names[] = { "", "a", "_", "_a1", "FOO_bar9", "0", "123", "1a", "a-b",
                                        "@", "*", "#", "?", "$", "!", "-", "!!", "@1", "a=1", "a+=1",
                                        "FOO_9=bar", "FOO_9+=bar", "_=", "_+=", "1=x", "=x", "a+", "a+b" };
                for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
                        sh_u64 l = (sh_u64)strlen(names[i]);
                        g_checks += 4;
                        if (sh_valid_name(names[i], l) != ref_sh_valid_name(names[i], l)) g_failures++;
                        if (sh_param_name(names[i], l) != ref_sh_param_name(names[i], l)) g_failures++;
                        sh_u64 na = 99, nr = 99;
                        if (sh_assign_split(names[i], l, &na) != ref_sh_assign_split(names[i], l, &nr) || na != nr)
                                g_failures++;
                        if (sh_assign_split(names[i], l, NULL) != ref_sh_assign_split(names[i], l, NULL))
                                g_failures++;
                }
                /* All 256 single-byte inputs for sh_valid_name & sh_param_name */
                for (int b = 0; b < 256; b++) {
                        char ch = (char)b;
                        g_checks += 2;
                        if (sh_valid_name(&ch, 1) != ref_sh_valid_name(&ch, 1)) g_failures++;
                        if (sh_param_name(&ch, 1) != ref_sh_param_name(&ch, 1)) g_failures++;
                }
                g_checks += 4;
                if (sh_valid_name(NULL, 3) != 0) g_failures++;
                if (sh_param_name(NULL, 3) != 0) g_failures++;
                if (sh_escaped_end(NULL, '"') != NULL) g_failures++;
                if (sh_assign_split(NULL, 3, NULL) != 0) g_failures++;
                const char *escs[] = { "", "\"", "\\\"\"", "abc\\\"def\"ghi", "trail\\", "\\\\\"", "noquote" };
                for (size_t i = 0; i < sizeof(escs) / sizeof(escs[0]); i++) {
                        g_checks += 3;
                        if (sh_escaped_end(escs[i], '"') != ref_sh_escaped_end(escs[i], '"')) g_failures++;
                        if (sh_escaped_end(escs[i], '\'') != ref_sh_escaped_end(escs[i], '\'')) g_failures++;
                        if (sh_escaped_end(escs[i], '\\') != ref_sh_escaped_end(escs[i], '\\')) g_failures++;
                }
        }

        if (g_failures != 0) {
                fprintf(stderr, "FAILED: %d failures across %llu checks\n",
                        g_failures, (unsigned long long)g_checks);
                return 1;
        }

        const char *bench_line =
                "export PATH=\"/usr/local/bin:$PATH\" && "
                "(grep -rn 'pattern' src/ |& sed 's/foo/bar/g' | sort -u) &>> /tmp/out.log <<< 'input' # done";
        sh_u64 bench_len = (sh_u64)strlen(bench_line);
        struct sh_token bench_toks[64];
        volatile sh_i32 sink_lex = 0;
        const int N_LEX = 400000;

        for (int i = 0; i < 4000; i++) {
                sink_lex += sh_lex_line(bench_line, bench_len, bench_toks, 64);
                sink_lex += ref_sh_lex_line(bench_line, bench_len, bench_toks, 64);
        }

        uint64_t t0 = now_ns();
        for (int i = 0; i < N_LEX; i++) sink_lex += sh_lex_line(bench_line, bench_len, bench_toks, 64);
        uint64_t t1 = now_ns();
        for (int i = 0; i < N_LEX; i++) sink_lex += ref_sh_lex_line(bench_line, bench_len, bench_toks, 64);
        uint64_t t2 = now_ns();

        double asm_lex_ns = (double)(t1 - t0) / (double)N_LEX;
        double ref_lex_ns = (double)(t2 - t1) / (double)N_LEX;
        double asm_lex_mb = ((double)bench_len * (double)N_LEX / 1e6) / ((double)(t1 - t0) / 1e9);
        double ref_lex_mb = ((double)bench_len * (double)N_LEX / 1e6) / ((double)(t2 - t1) / 1e9);

        const char *uq_word =
                "prefix_\"hello \\\"world\\\" \\$HOME \\`uname\\`\"_'raw\\n\\tliteral'_esc\\ space\\|pipe";
        sh_u64 uq_len = (sh_u64)strlen(uq_word);
        char uq_out[256];
        volatile sh_u64 sink_uq = 0;
        const int N_UQ = 400000;

        for (int i = 0; i < 4000; i++) {
                sink_uq += sh_unquote(uq_word, uq_len, uq_out, sizeof(uq_out));
                sink_uq += ref_sh_unquote(uq_word, uq_len, uq_out, sizeof(uq_out));
        }

        uint64_t u0 = now_ns();
        for (int i = 0; i < N_UQ; i++) sink_uq += sh_unquote(uq_word, uq_len, uq_out, sizeof(uq_out));
        uint64_t u1 = now_ns();
        for (int i = 0; i < N_UQ; i++) sink_uq += ref_sh_unquote(uq_word, uq_len, uq_out, sizeof(uq_out));
        uint64_t u2 = now_ns();

        double asm_uq_ns = (double)(u1 - u0) / (double)N_UQ;
        double ref_uq_ns = (double)(u2 - u1) / (double)N_UQ;
        double asm_uq_mb = ((double)uq_len * (double)N_UQ / 1e6) / ((double)(u1 - u0) / 1e9);
        double ref_uq_mb = ((double)uq_len * (double)N_UQ / 1e6) / ((double)(u2 - u1) / 1e9);

        printf("PASS: 0 failures across %llu differential checks\n", (unsigned long long)g_checks);
        printf("BENCH sh_lex_line : ASM %5.1f ns/line (%6.1f MB/s) | C -O2 %5.1f ns/line (%6.1f MB/s) | %.2fx\n",
               asm_lex_ns, asm_lex_mb, ref_lex_ns, ref_lex_mb, ref_lex_ns / asm_lex_ns);
        printf("BENCH sh_unquote  : ASM %5.1f ns/word (%6.1f MB/s) | C -O2 %5.1f ns/word (%6.1f MB/s) | %.2fx\n",
               asm_uq_ns, asm_uq_mb, ref_uq_ns, ref_uq_mb, ref_uq_ns / asm_uq_ns);
        (void)sink_lex;
        (void)sink_uq;
        return 0;
}
