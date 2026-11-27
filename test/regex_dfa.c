/*
        Differential C-reference parity, fuzz, mmap guard-page security &
        throughput/size benchmark for src/sh/regex_dfa.asm.c
        (arm64, x86-64, riscv64 pure-ASM regex DFA inner-loop kernels).

        Build & run:
          cc -O2 -Wall -Werror test/regex_dfa.c -o /tmp/t_rxdfa && /tmp/t_rxdfa
*/
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>

#define SH_REGEX_DFA_ASM_IMPLEMENTATION
#include "../src/sh/regex_dfa.asm.c"

/* ── Reference -O2 C implementations (noinline for fair size & speed diff) ── */

static __attribute__((noinline)) sh_rx_i32
ref_rx_dfa_step4(const sh_rx_u8 *classes, const sh_rx_i32 *trans,
                 struct sh_rx_lane *lane, sh_rx_u64 m, sh_rx_i32 *out_n)
{
        if (!classes || !trans || !lane || !m)
                return 0;
        const sh_rx_u8 *a0 = lane[0].at, *a1 = lane[1].at, *a2 = lane[2].at, *a3 = lane[3].at;
        sh_rx_i32 r0 = lane[0].row, r1 = lane[1].row, r2 = lane[2].row, r3 = lane[3].row;
        sh_rx_i32 n0 = 0, n1 = 0, n2 = 0, n3 = 0;
        int stopped = 0;
        while (m--) {
                n0 = trans[r0 + classes[*a0]];
                n1 = trans[r1 + classes[*a1]];
                n2 = trans[r2 + classes[*a2]];
                n3 = trans[r3 + classes[*a3]];
                if ((n0 | n1 | n2 | n3) < 0) {
                        stopped = 1;
                        break;
                }
                a0++; a1++; a2++; a3++;
                r0 = n0; r1 = n1; r2 = n2; r3 = n3;
        }
        lane[0].at = a0; lane[1].at = a1; lane[2].at = a2; lane[3].at = a3;
        lane[0].row = r0; lane[1].row = r1; lane[2].row = r2; lane[3].row = r3;
        if (!stopped)
                return 0;
        if (out_n) {
                out_n[0] = n0; out_n[1] = n1; out_n[2] = n2; out_n[3] = n3;
        }
        return 1;
}

static __attribute__((noinline)) sh_rx_i32
ref_rx_dfa_step1(const sh_rx_u8 *classes, const sh_rx_i32 *trans,
                 const sh_rx_u8 **at_io, const sh_rx_u8 *past, sh_rx_i32 *row_io)
{
        if (!classes || !trans || !at_io || !row_io)
                return 0;
        const sh_rx_u8 *at = *at_io;
        sh_rx_i32 row = *row_io;
        sh_rx_i32 next = 0;
        while (at < past && (next = trans[row + classes[*at]]) >= 0) {
                row = next;
                at++;
        }
        *at_io = at;
        *row_io = row;
        return next;
}

static __attribute__((noinline)) sh_rx_i32
ref_rx_dfa_first_fast(const sh_rx_u8 *classes, const sh_rx_i32 *trans,
                      const sh_rx_u8 *text, sh_rx_u64 *p_io,
                      sh_rx_u64 length, sh_rx_i32 *row_io)
{
        if (!classes || !trans || !p_io || !row_io)
                return 0;
        sh_rx_u64 p = *p_io;
        sh_rx_i32 row = *row_io;
        if (p >= length)
                return 0;
        if (!text)
                return 0;
        while (p < length) {
                sh_rx_i32 next = trans[row + classes[text[p]]];
                if (next < 0) {
                        *p_io = p;
                        *row_io = row;
                        return next;
                }
                row = next;
                p++;
        }
        *p_io = p;
        *row_io = row;
        return 0;
}

static __attribute__((noinline)) sh_rx_i32
ref_rx_dfa_longest_fast(const struct sh_rx_longest_ctx *ctx,
                        struct sh_rx_longest_state *st)
{
        if (!ctx || !st)
                return 0;
        sh_rx_u64 p = st->p;
        sh_rx_u64 last = st->last;
        sh_rx_i32 row = st->row;
        if (p >= ctx->length)
                return 0;
        if (!ctx->text)
                return 0;
        while (p < ctx->length) {
                sh_rx_i32 state = (sh_rx_i32)(((sh_rx_u64)row * ctx->class_rcp) >> 32);
                sh_rx_u8 cls = ctx->classes[ctx->text[p]];
                if (ctx->accept[state] & (ctx->name[cls] ? 1u : 2u))
                        last = p;
                sh_rx_i32 next = ctx->trans[row + cls];
                if (next < 0) {
                        st->p = p;
                        st->last = last;
                        st->row = row;
                        return next;
                }
                row = next;
                p++;
        }
        st->p = p;
        st->last = last;
        st->row = row;
        return 0;
}

static __attribute__((noinline)) sh_rx_i32
ref_rx_single(const struct sh_rx_program *program, const struct sh_rx_node *node, sh_rx_u8 byte)
{
        if (!program || !node)
                return 0;
        if (node->kind == 2)
                return ((program->policy & 1u) || byte != '\n') ? 1 : 0;
        if (node->kind == 3)
                return program->sets[node->argument][byte] ? 1 : 0;
        sh_rx_u8 b = byte;
        if ((program->flags & 128u) && b >= 'A' && b <= 'Z')
                b = (sh_rx_u8)(b | 32u);
        return (b == node->argument) ? 1 : 0;
}

/* ── Helpers ───────────────────────────────────────────────────────────────── */

static uint64_t g_checks = 0;
static int g_failures = 0;

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

static uint64_t now_ns(void)
{
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

#define CHECK(cond, msg) do { \
        g_checks++; \
        if (!(cond)) { \
                if (g_failures < 20) \
                        fprintf(stderr, "FAIL (%s:%d): %s\n", __FILE__, __LINE__, msg); \
                g_failures++; \
        } \
} while (0)

/* Build a deterministic random DFA table with `nstates` states and `nclasses` classes.
   `neg_prob` out of 1000 transitions are negative stop codes (-1, -2, -3, -4, -6). */
static void build_dfa(sh_rx_u8 *classes, sh_rx_u8 *name, sh_rx_u8 *accept,
                      sh_rx_i32 *trans, int nstates, int nclasses, int neg_prob)
{
        static const sh_rx_i32 neg_codes[5] = { -1, -2, -3, -4, -6 };
        for (int b = 0; b < 256; b++)
                classes[b] = (sh_rx_u8)(xorshift64() % (uint64_t)nclasses);
        for (int c = 0; c < 256; c++) {
                uint64_t r = xorshift64() & 7;
                name[c] = (r == 0) ? 0 : (r == 1 ? 1 : (r == 2 ? 0x55 : 0));
        }
        for (int s = 0; s < nstates; s++) {
                accept[s] = (sh_rx_u8)(xorshift64() & 7);
                for (int c = 0; c < nclasses; c++) {
                        int idx = s * nclasses + c;
                        if ((int)(xorshift64() % 1000) < neg_prob) {
                                trans[idx] = neg_codes[xorshift64() % 5];
                        } else {
                                int dst = (int)(xorshift64() % (uint64_t)nstates);
                                trans[idx] = dst * nclasses;
                        }
                }
        }
}

/* ── Edge-case & exhaustive tests ──────────────────────────────────────────── */

static void test_edge_cases(void)
{
        sh_rx_u8 classes[256], name[256], accept[64];
        sh_rx_i32 trans[64 * 256];
        memset(classes, 0, sizeof(classes));
        memset(trans, 0, sizeof(trans));
        sh_rx_u8 buf[128];
        for (int i = 0; i < 128; i++) buf[i] = (sh_rx_u8)(i * 3 + 7);

        /* NULL / zero-length safety */
        struct sh_rx_lane lanes[4];
        memset(lanes, 0, sizeof(lanes));
        sh_rx_i32 out_n[4] = { 99, 99, 99, 99 };
        CHECK(rx_dfa_step4(NULL, trans, lanes, 10, out_n) == 0, "step4 null classes");
        CHECK(rx_dfa_step4(classes, NULL, lanes, 10, out_n) == 0, "step4 null trans");
        CHECK(rx_dfa_step4(classes, trans, NULL, 10, out_n) == 0, "step4 null lanes");
        CHECK(rx_dfa_step4(classes, trans, lanes, 0, out_n) == 0, "step4 m=0");

        const sh_rx_u8 *at = buf;
        sh_rx_i32 row = 0;
        CHECK(rx_dfa_step1(NULL, trans, &at, buf + 10, &row) == 0, "step1 null classes");
        CHECK(rx_dfa_step1(classes, NULL, &at, buf + 10, &row) == 0, "step1 null trans");
        CHECK(rx_dfa_step1(classes, trans, NULL, buf + 10, &row) == 0, "step1 null at_io");
        CHECK(rx_dfa_step1(classes, trans, &at, buf + 10, NULL) == 0, "step1 null row_io");
        CHECK(rx_dfa_step1(classes, trans, &at, buf, &row) == 0, "step1 empty");

        sh_rx_u64 p = 0;
        CHECK(rx_dfa_first_fast(NULL, trans, buf, &p, 10, &row) == 0, "first null classes");
        CHECK(rx_dfa_first_fast(classes, NULL, buf, &p, 10, &row) == 0, "first null trans");
        CHECK(rx_dfa_first_fast(classes, trans, NULL, &p, 10, &row) == 0, "first null text");
        CHECK(rx_dfa_first_fast(classes, trans, buf, NULL, 10, &row) == 0, "first null p_io");
        CHECK(rx_dfa_first_fast(classes, trans, buf, &p, 10, NULL) == 0, "first null row_io");
        p = 10;
        CHECK(rx_dfa_first_fast(classes, trans, NULL, &p, 10, &row) == 0, "first p==len null text");

        struct sh_rx_longest_ctx lctx = { classes, name, accept, trans, 1ULL << 32, buf, 10 };
        struct sh_rx_longest_state lst = { 0, (sh_rx_u64)-1, 0, 0 };
        CHECK(rx_dfa_longest_fast(NULL, &lst) == 0, "longest null ctx");
        CHECK(rx_dfa_longest_fast(&lctx, NULL) == 0, "longest null st");
        lctx.text = NULL;
        CHECK(rx_dfa_longest_fast(&lctx, &lst) == 0, "longest null text");
        lst.p = 10;
        CHECK(rx_dfa_longest_fast(&lctx, &lst) == 0, "longest p==len");

        CHECK(rx_single(NULL, NULL, 'a') == 0, "rx_single null");

        /* All 256 byte class mappings (identity 256 classes) + 1..64 byte lengths + unaligned offsets */
        int nclasses = 256, nstates = 16;
        for (int b = 0; b < 256; b++) {
                classes[b] = (sh_rx_u8)b;
                name[b] = (b >= 'a' && b <= 'z') ? 1 : 0;
        }
        for (int s = 0; s < nstates; s++) {
                accept[s] = (sh_rx_u8)((s & 3) ? (s & 7) : 0);
                for (int c = 0; c < nclasses; c++)
                        trans[s * nclasses + c] = ((s + c + 1) % nstates) * nclasses;
        }
        sh_rx_u64 rcp = ((1ULL << 32) + (sh_rx_u64)nclasses - 1) / (sh_rx_u64)nclasses;

        for (int len = 0; len <= 64; len++) {
                for (int align = 0; align < 8; align++) {
                        const sh_rx_u8 *base = buf + align;
                        /* step1 */
                        const sh_rx_u8 *a_asm = base, *a_ref = base;
                        sh_rx_i32 r_asm = 0, r_ref = 0;
                        sh_rx_i32 ret_asm = rx_dfa_step1(classes, trans, &a_asm, base + len, &r_asm);
                        sh_rx_i32 ret_ref = ref_rx_dfa_step1(classes, trans, &a_ref, base + len, &r_ref);
                        CHECK(ret_asm == ret_ref && a_asm == a_ref && r_asm == r_ref, "step1 0..64 align");

                        /* first_fast */
                        sh_rx_u64 p_asm = 0, p_ref = 0;
                        r_asm = 0; r_ref = 0;
                        ret_asm = rx_dfa_first_fast(classes, trans, base, &p_asm, (sh_rx_u64)len, &r_asm);
                        ret_ref = ref_rx_dfa_first_fast(classes, trans, base, &p_ref, (sh_rx_u64)len, &r_ref);
                        CHECK(ret_asm == ret_ref && p_asm == p_ref && r_asm == r_ref, "first 0..64 align");

                        /* longest_fast */
                        struct sh_rx_longest_ctx c = { classes, name, accept, trans, rcp, base, (sh_rx_u64)len };
                        struct sh_rx_longest_state s_asm = { 0, (sh_rx_u64)-1, 0, 0 };
                        struct sh_rx_longest_state s_ref = s_asm;
                        ret_asm = rx_dfa_longest_fast(&c, &s_asm);
                        ret_ref = ref_rx_dfa_longest_fast(&c, &s_ref);
                        CHECK(ret_asm == ret_ref && s_asm.p == s_ref.p &&
                              s_asm.last == s_ref.last && s_asm.row == s_ref.row, "longest 0..64 align");

                        /* step4 */
                        struct sh_rx_lane l_asm[4], l_ref[4];
                        for (int k = 0; k < 4; k++) {
                                l_asm[k].at = base + k;
                                l_asm[k].past = base + k + len;
                                l_asm[k].begin = base + k;
                                l_asm[k].row = (k * 3 % nstates) * nclasses;
                                l_asm[k]._pad = 0;
                                l_ref[k] = l_asm[k];
                        }
                        sh_rx_i32 n_asm[4] = {0}, n_ref[4] = {0};
                        ret_asm = rx_dfa_step4(classes, trans, l_asm, (sh_rx_u64)len, n_asm);
                        ret_ref = ref_rx_dfa_step4(classes, trans, l_ref, (sh_rx_u64)len, n_ref);
                        CHECK(ret_asm == ret_ref && memcmp(l_asm, l_ref, sizeof(l_asm)) == 0 &&
                              memcmp(n_asm, n_ref, sizeof(n_asm)) == 0, "step4 0..64 align");
                }
        }

        /* Early termination on each individual lane (0..3) and with out_n == NULL */
        for (int stop_lane = 0; stop_lane < 4; stop_lane++) {
                sh_rx_u8 b4[4][16];
                memset(b4, 'a', sizeof(b4));
                b4[stop_lane][5] = 'Z';
                sh_rx_i32 saved = trans[0];
                for (int s = 0; s < nstates; s++)
                        trans[s * nclasses + 'Z'] = -2 - stop_lane;
                struct sh_rx_lane l_asm[4], l_ref[4];
                for (int k = 0; k < 4; k++) {
                        l_asm[k] = (struct sh_rx_lane){ b4[k], b4[k] + 16, b4[k], 0, 0 };
                        l_ref[k] = l_asm[k];
                }
                sh_rx_i32 n_asm[4] = {0}, n_ref[4] = {0};
                sh_rx_i32 r1 = rx_dfa_step4(classes, trans, l_asm, 16, n_asm);
                sh_rx_i32 r2 = ref_rx_dfa_step4(classes, trans, l_ref, 16, n_ref);
                CHECK(r1 == 1 && r2 == 1 && memcmp(l_asm, l_ref, sizeof(l_asm)) == 0 &&
                      memcmp(n_asm, n_ref, sizeof(n_asm)) == 0, "step4 per-lane stop");
                /* Also test out_n == NULL on stop */
                for (int k = 0; k < 4; k++) l_asm[k] = (struct sh_rx_lane){ b4[k], b4[k] + 16, b4[k], 0, 0 };
                CHECK(rx_dfa_step4(classes, trans, l_asm, 16, NULL) == 1, "step4 stop out_n=NULL");
                for (int s = 0; s < nstates; s++)
                        trans[s * nclasses + 'Z'] = ((s + 'Z' + 1) % nstates) * nclasses;
                trans[0] = saved;
        }

        /* Exhaustive rx_single across all 256 bytes x all kinds x all flags/policies */
        sh_rx_u8 sets[4][256];
        for (int s = 0; s < 4; s++)
                for (int b = 0; b < 256; b++)
                        sets[s][b] = (sh_rx_u8)(((b + s * 17) % 5 == 0) ? (s + 1) : 0);
        struct sh_rx_program prog;
        memset(&prog, 0, sizeof(prog));
        prog.sets = sets;

        struct sh_rx_node node;
        memset(&node, 0, sizeof(node));

        /* RX_ANY (kind=2) with policy=0 and policy=1 */
        node.kind = 2;
        for (int pol = 0; pol <= 3; pol++) {
                prog.policy = (sh_rx_u8)pol;
                for (int b = 0; b < 256; b++) {
                        CHECK(rx_single(&prog, &node, (sh_rx_u8)b) == ref_rx_single(&prog, &node, (sh_rx_u8)b),
                              "rx_single RX_ANY");
                }
        }
        /* RX_SET (kind=3) across 4 sets */
        node.kind = 3;
        for (int s = 0; s < 4; s++) {
                node.argument = (sh_rx_u16)s;
                for (int b = 0; b < 256; b++) {
                        CHECK(rx_single(&prog, &node, (sh_rx_u8)b) == ref_rx_single(&prog, &node, (sh_rx_u8)b),
                              "rx_single RX_SET");
                }
        }
        /* RX_BYTE (kind=1) with flags=0 and flags=128 (RX_IGNORE_CASE) */
        node.kind = 1;
        for (int fl = 0; fl <= 128; fl += 128) {
                prog.flags = (sh_rx_u8)fl;
                for (int arg = 0; arg < 256; arg += 7) {
                        node.argument = (sh_rx_u16)arg;
                        for (int b = 0; b < 256; b++) {
                                CHECK(rx_single(&prog, &node, (sh_rx_u8)b) == ref_rx_single(&prog, &node, (sh_rx_u8)b),
                                      "rx_single RX_BYTE");
                        }
                }
                /* Also test argument > 255 (16-bit comparison) */
                node.argument = 300;
                for (int b = 0; b < 256; b++) {
                        CHECK(rx_single(&prog, &node, (sh_rx_u8)b) == 0, "rx_single arg>255");
                }
        }
}

/* ── Randomized differential fuzzing ───────────────────────────────────────── */

static void test_fuzz(void)
{
        sh_rx_u8 classes[256], name[256], accept[128];
        sh_rx_i32 trans[128 * 256];
        sh_rx_u8 text[512];

        const int class_counts[] = { 1, 2, 3, 5, 16, 37, 64, 127, 255, 256 };
        for (int trial = 0; trial < 2000; trial++) {
                int nclasses = class_counts[trial % (int)(sizeof(class_counts) / sizeof(class_counts[0]))];
                int nstates = 1 + (int)(xorshift64() % 64);
                int neg_prob = (trial % 4 == 0) ? 0 : (int)(xorshift64() % 150);
                build_dfa(classes, name, accept, trans, nstates, nclasses, neg_prob);
                sh_rx_u64 rcp = ((1ULL << 32) + (sh_rx_u64)nclasses - 1) / (sh_rx_u64)nclasses;

                for (int i = 0; i < 512; i++) text[i] = (sh_rx_u8)xorshift64();

                for (int sub = 0; sub < 8; sub++) {
                        sh_rx_u64 len = xorshift64() % 120;
                        sh_rx_u64 off = xorshift64() % 16;
                        sh_rx_i32 init_row = (sh_rx_i32)(xorshift64() % (uint64_t)nstates) * nclasses;

                        /* step1 */
                        const sh_rx_u8 *a1 = text + off, *a2 = text + off;
                        sh_rx_i32 r1 = init_row, r2 = init_row;
                        sh_rx_i32 ret1 = rx_dfa_step1(classes, trans, &a1, text + off + len, &r1);
                        sh_rx_i32 ret2 = ref_rx_dfa_step1(classes, trans, &a2, text + off + len, &r2);
                        CHECK(ret1 == ret2 && a1 == a2 && r1 == r2, "fuzz step1");

                        /* first_fast */
                        sh_rx_u64 p1 = xorshift64() % (len + 1), p2 = p1;
                        r1 = init_row; r2 = init_row;
                        ret1 = rx_dfa_first_fast(classes, trans, text + off, &p1, len, &r1);
                        ret2 = ref_rx_dfa_first_fast(classes, trans, text + off, &p2, len, &r2);
                        CHECK(ret1 == ret2 && p1 == p2 && r1 == r2, "fuzz first_fast");

                        /* longest_fast */
                        struct sh_rx_longest_ctx lctx = { classes, name, accept, trans, rcp, text + off, len };
                        sh_rx_u64 lp = xorshift64() % (len + 1);
                        struct sh_rx_longest_state s1 = { lp, (sh_rx_u64)-1, init_row, 0 };
                        struct sh_rx_longest_state s2 = s1;
                        ret1 = rx_dfa_longest_fast(&lctx, &s1);
                        ret2 = ref_rx_dfa_longest_fast(&lctx, &s2);
                        CHECK(ret1 == ret2 && s1.p == s2.p && s1.last == s2.last && s1.row == s2.row,
                              "fuzz longest_fast");

                        /* step4 */
                        struct sh_rx_lane l1[4], l2[4];
                        for (int k = 0; k < 4; k++) {
                                sh_rx_u64 koff = (sh_rx_u64)(k * 120) + (xorshift64() % 8);
                                l1[k] = (struct sh_rx_lane){
                                        text + koff, text + koff + len, text + koff,
                                        (sh_rx_i32)(xorshift64() % (uint64_t)nstates) * nclasses, 0
                                };
                                l2[k] = l1[k];
                        }
                        sh_rx_i32 n1[4] = {0}, n2[4] = {0};
                        ret1 = rx_dfa_step4(classes, trans, l1, len, n1);
                        ret2 = ref_rx_dfa_step4(classes, trans, l2, len, n2);
                        CHECK(ret1 == ret2 && memcmp(l1, l2, sizeof(l1)) == 0 &&
                              memcmp(n1, n2, sizeof(n1)) == 0, "fuzz step4");
                }
        }
}

/* ── Hardware mmap guard-page OOB security tests ───────────────────────────── */

static void test_mmap_guard_pages(void)
{
        size_t ps = (size_t)sysconf(_SC_PAGESIZE);
        /* 3 pages: [PROT_NONE] [RW] [PROT_NONE] */
        uint8_t *m = mmap(NULL, ps * 3, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (m == MAP_FAILED) {
                perror("mmap");
                g_failures++;
                return;
        }
        if (mprotect(m + ps, ps, PROT_READ | PROT_WRITE) != 0) {
                perror("mprotect");
                munmap(m, ps * 3);
                g_failures++;
                return;
        }
        uint8_t *page = m + ps;
        for (size_t i = 0; i < ps; i++) page[i] = (uint8_t)((i * 131 + 17) & 0xff);

        sh_rx_u8 classes[256], name[256], accept[16];
        sh_rx_i32 trans[16 * 32];
        build_dfa(classes, name, accept, trans, 16, 32, 0); /* 0% stop -> must scan right up to guard */
        sh_rx_u64 rcp = ((1ULL << 32) + 31) / 32;

        const size_t lens[] = { 0, 1, 2, 3, 4, 7, 8, 15, 16, 31, 32, 63, 64, 127, 256, 1024 };
        for (size_t li = 0; li < sizeof(lens) / sizeof(lens[0]); li++) {
                size_t len = lens[li];
                /* Test BOTH: flush against lower guard (page + 0) AND flush against upper guard (page + ps - len) */
                for (int end = 0; end < 2; end++) {
                        const sh_rx_u8 *ptr = end ? (page + ps - len) : page;

                        /* rx_dfa_step1 */
                        const sh_rx_u8 *a1 = ptr, *a2 = ptr;
                        sh_rx_i32 r1 = 0, r2 = 0;
                        CHECK(rx_dfa_step1(classes, trans, &a1, ptr + len, &r1) ==
                              ref_rx_dfa_step1(classes, trans, &a2, ptr + len, &r2) &&
                              a1 == a2 && r1 == r2, "guard step1");

                        /* rx_dfa_first_fast */
                        sh_rx_u64 p1 = 0, p2 = 0;
                        r1 = 0; r2 = 0;
                        CHECK(rx_dfa_first_fast(classes, trans, ptr, &p1, len, &r1) ==
                              ref_rx_dfa_first_fast(classes, trans, ptr, &p2, len, &r2) &&
                              p1 == p2 && r1 == r2, "guard first_fast");

                        /* rx_dfa_longest_fast */
                        struct sh_rx_longest_ctx lctx = { classes, name, accept, trans, rcp, ptr, len };
                        struct sh_rx_longest_state s1 = { 0, (sh_rx_u64)-1, 0, 0 }, s2 = s1;
                        CHECK(rx_dfa_longest_fast(&lctx, &s1) == ref_rx_dfa_longest_fast(&lctx, &s2) &&
                              s1.p == s2.p && s1.last == s2.last && s1.row == s2.row, "guard longest_fast");

                        /* rx_dfa_step4: all 4 lanes flush against guard! */
                        struct sh_rx_lane l1[4], l2[4];
                        for (int k = 0; k < 4; k++) {
                                l1[k] = (struct sh_rx_lane){ ptr, ptr + len, ptr, (k * 3 % 16) * 32, 0 };
                                l2[k] = l1[k];
                        }
                        sh_rx_i32 n1[4] = {0}, n2[4] = {0};
                        CHECK(rx_dfa_step4(classes, trans, l1, len, n1) ==
                              ref_rx_dfa_step4(classes, trans, l2, len, n2) &&
                              memcmp(l1, l2, sizeof(l1)) == 0, "guard step4");
                }
        }

        /* rx_single with set table placed flush against upper guard page */
        sh_rx_u8 (*guard_sets)[256] = (sh_rx_u8 (*)[256])(page + ps - 256);
        struct sh_rx_program prog;
        memset(&prog, 0, sizeof(prog));
        prog.sets = guard_sets;
        struct sh_rx_node node;
        memset(&node, 0, sizeof(node));
        node.kind = 3;
        node.argument = 0;
        for (int b = 0; b < 256; b++) {
                CHECK(rx_single(&prog, &node, (sh_rx_u8)b) == ref_rx_single(&prog, &node, (sh_rx_u8)b),
                      "guard rx_single");
        }

        munmap(m, ps * 3);
}

/* ── Multi-MB Throughput & Latency Benchmark ───────────────────────────────── */

static void run_benchmarks(void)
{
        const size_t MB4 = 4 * 1024 * 1024;
        sh_rx_u8 *buf = (sh_rx_u8 *)malloc(MB4);
        if (!buf) { fprintf(stderr, "oom\n"); exit(1); }
        for (size_t i = 0; i < MB4; i++) buf[i] = (sh_rx_u8)((i * 1103515245u + 12345u) >> 16);

        sh_rx_u8 classes[256], name[256], accept[32];
        sh_rx_i32 trans[32 * 32];
        build_dfa(classes, name, accept, trans, 32, 32, 0);
        sh_rx_u64 rcp = ((1ULL << 32) + 31) / 32;

        volatile int64_t sink = 0;

        /* 1. rx_dfa_step4: 4 x 1 MB = 4 MB per pass, 16 passes = 64 MB */
        const size_t LANE_LEN = MB4 / 4;
        const int PASS4 = 16;
        struct sh_rx_lane lanes[4];
        sh_rx_i32 out_n[4];
        for (int w = 0; w < 2; w++) {
                for (int k = 0; k < 4; k++) lanes[k] = (struct sh_rx_lane){ buf + k * LANE_LEN, buf + (k + 1) * LANE_LEN, buf + k * LANE_LEN, 0, 0 };
                sink += rx_dfa_step4(classes, trans, lanes, LANE_LEN, out_n);
                for (int k = 0; k < 4; k++) lanes[k] = (struct sh_rx_lane){ buf + k * LANE_LEN, buf + (k + 1) * LANE_LEN, buf + k * LANE_LEN, 0, 0 };
                sink += ref_rx_dfa_step4(classes, trans, lanes, LANE_LEN, out_n);
        }
        uint64_t t0 = now_ns();
        for (int p = 0; p < PASS4; p++) {
                for (int k = 0; k < 4; k++) lanes[k] = (struct sh_rx_lane){ buf + k * LANE_LEN, buf + (k + 1) * LANE_LEN, buf + k * LANE_LEN, 0, 0 };
                sink += rx_dfa_step4(classes, trans, lanes, LANE_LEN, out_n) + lanes[0].row;
        }
        uint64_t t1 = now_ns();
        for (int p = 0; p < PASS4; p++) {
                for (int k = 0; k < 4; k++) lanes[k] = (struct sh_rx_lane){ buf + k * LANE_LEN, buf + (k + 1) * LANE_LEN, buf + k * LANE_LEN, 0, 0 };
                sink += ref_rx_dfa_step4(classes, trans, lanes, LANE_LEN, out_n) + lanes[0].row;
        }
        uint64_t t2 = now_ns();
        double bytes4 = (double)MB4 * PASS4;
        double asm_gb4 = (bytes4 / 1e9) / ((double)(t1 - t0) / 1e9);
        double ref_gb4 = (bytes4 / 1e9) / ((double)(t2 - t1) / 1e9);
        printf("BENCH rx_dfa_step4        : ASM %6.2f GB/s | C -O2 %6.2f GB/s | %.2fx\n",
               asm_gb4, ref_gb4, asm_gb4 / ref_gb4);

        /* 2. rx_dfa_step1: 4 MB x 8 = 32 MB */
        const int PASS1 = 8;
        t0 = now_ns();
        for (int p = 0; p < PASS1; p++) {
                const sh_rx_u8 *at = buf; sh_rx_i32 row = 0;
                sink += rx_dfa_step1(classes, trans, &at, buf + MB4, &row) + row;
        }
        t1 = now_ns();
        for (int p = 0; p < PASS1; p++) {
                const sh_rx_u8 *at = buf; sh_rx_i32 row = 0;
                sink += ref_rx_dfa_step1(classes, trans, &at, buf + MB4, &row) + row;
        }
        t2 = now_ns();
        double bytes1 = (double)MB4 * PASS1;
        double asm_gb1 = (bytes1 / 1e9) / ((double)(t1 - t0) / 1e9);
        double ref_gb1 = (bytes1 / 1e9) / ((double)(t2 - t1) / 1e9);
        printf("BENCH rx_dfa_step1        : ASM %6.2f GB/s | C -O2 %6.2f GB/s | %.2fx\n",
               asm_gb1, ref_gb1, asm_gb1 / ref_gb1);

        /* 3. rx_dfa_first_fast: 4 MB x 8 = 32 MB */
        t0 = now_ns();
        for (int p = 0; p < PASS1; p++) {
                sh_rx_u64 pos = 0; sh_rx_i32 row = 0;
                sink += rx_dfa_first_fast(classes, trans, buf, &pos, MB4, &row) + row;
        }
        t1 = now_ns();
        for (int p = 0; p < PASS1; p++) {
                sh_rx_u64 pos = 0; sh_rx_i32 row = 0;
                sink += ref_rx_dfa_first_fast(classes, trans, buf, &pos, MB4, &row) + row;
        }
        t2 = now_ns();
        double asm_gbf = (bytes1 / 1e9) / ((double)(t1 - t0) / 1e9);
        double ref_gbf = (bytes1 / 1e9) / ((double)(t2 - t1) / 1e9);
        printf("BENCH rx_dfa_first_fast   : ASM %6.2f GB/s | C -O2 %6.2f GB/s | %.2fx\n",
               asm_gbf, ref_gbf, asm_gbf / ref_gbf);

        /* 4. rx_dfa_longest_fast: 4 MB x 8 = 32 MB */
        struct sh_rx_longest_ctx lctx = { classes, name, accept, trans, rcp, buf, MB4 };
        t0 = now_ns();
        for (int p = 0; p < PASS1; p++) {
                struct sh_rx_longest_state st = { 0, (sh_rx_u64)-1, 0, 0 };
                sink += rx_dfa_longest_fast(&lctx, &st) + (int64_t)st.last;
        }
        t1 = now_ns();
        for (int p = 0; p < PASS1; p++) {
                struct sh_rx_longest_state st = { 0, (sh_rx_u64)-1, 0, 0 };
                sink += ref_rx_dfa_longest_fast(&lctx, &st) + (int64_t)st.last;
        }
        t2 = now_ns();
        double asm_gbl = (bytes1 / 1e9) / ((double)(t1 - t0) / 1e9);
        double ref_gbl = (bytes1 / 1e9) / ((double)(t2 - t1) / 1e9);
        printf("BENCH rx_dfa_longest_fast : ASM %6.2f GB/s | C -O2 %6.2f GB/s | %.2fx\n",
               asm_gbl, ref_gbl, asm_gbl / ref_gbl);

        /* 5. rx_single: 16M calls */
        sh_rx_u8 sets[1][256];
        for (int b = 0; b < 256; b++) sets[0][b] = (b & 1);
        struct sh_rx_program prog;
        memset(&prog, 0, sizeof(prog));
        prog.sets = sets; prog.flags = 128; prog.policy = 0;
        struct sh_rx_node nodes[3];
        memset(nodes, 0, sizeof(nodes));
        nodes[0].kind = 1; nodes[0].argument = 'a';
        nodes[1].kind = 2;
        nodes[2].kind = 3; nodes[2].argument = 0;
        const int N_SINGLE = 16000000;
        t0 = now_ns();
        for (int i = 0; i < N_SINGLE; i++)
                sink += rx_single(&prog, &nodes[i % 3], (sh_rx_u8)i);
        t1 = now_ns();
        for (int i = 0; i < N_SINGLE; i++)
                sink += ref_rx_single(&prog, &nodes[i % 3], (sh_rx_u8)i);
        t2 = now_ns();
        double asm_ns = (double)(t1 - t0) / (double)N_SINGLE;
        double ref_ns = (double)(t2 - t1) / (double)N_SINGLE;
        printf("BENCH rx_single           : ASM %5.2f ns/op | C -O2 %5.2f ns/op | %.2fx\n",
               asm_ns, ref_ns, ref_ns / asm_ns);

        (void)sink;
        free(buf);
}

int main(void)
{
        test_edge_cases();
        test_fuzz();
        test_mmap_guard_pages();

        if (g_failures != 0) {
                fprintf(stderr, "FAILED: %d failures across %llu checks\n",
                        g_failures, (unsigned long long)g_checks);
                return 1;
        }
        printf("PASS: 0 failures across %llu differential + guard-page checks\n",
               (unsigned long long)g_checks);
        run_benchmarks();
        return 0;
}
