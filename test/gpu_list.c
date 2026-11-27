/*
        Differential C-reference parity, security, guard-page, fuzz & throughput/size
        benchmark for src/canvas/compositor/gpu_list.asm.c (arm64, x86-64, riscv64 pure-ASM).

        Build & run:
          cc -O2 -Wall -Werror test/gpu_list.c -o /tmp/mw_gpu_list && /tmp/mw_gpu_list
          cc -O2 -fsanitize=address,undefined -Wall -Werror test/gpu_list.c -o /tmp/mw_gpu_list_asan && /tmp/mw_gpu_list_asan
*/
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define COMPOSITOR_GPU_LIST_ASM_IMPLEMENTATION
#include "../src/canvas/compositor/gpu_list.asm.c"

/* ── Reference C implementations (noinline + external linkage for exact nm --print-size) ── */

__attribute__((noinline)) gpu_list_i32 ref_gpu_list_cut(gpu_list_u32 word, gpu_list_i32 low, gpu_list_i32 high)
{
        gpu_list_i32 value = (gpu_list_i32)word;
        return value < low ? low : value > high ? high : value;
}

__attribute__((noinline)) gpu_list_u32 ref_gpu_list_ops(const struct gpu_list_spec *spec)
{
        if (!spec)
                return COMPOSITOR_GPU_LIST_REFUSED;
        const gpu_list_u32 *from = spec->from;
        gpu_list_u32 *to = spec->to;
        gpu_list_u64 cover_at = spec->cover_at;
        gpu_list_u64 cover_used = spec->cover_used;
        gpu_list_u64 list_bytes = spec->list_bytes;
        gpu_list_u32 count = spec->count;
        gpu_list_u32 room = to ? spec->room : 0u;
        gpu_list_i32 width = spec->width;
        gpu_list_i32 height = spec->height;
        gpu_list_u32 slot = spec->slot;

        if (width <= 0 || height <= 0 || slot < 1 || slot > 255)
                return COMPOSITOR_GPU_LIST_REFUSED;
        if (count && !from)
                return COMPOSITOR_GPU_LIST_REFUSED;

        gpu_list_u32 n = 0;
        int text_room = cover_at >= COMPOSITOR_GPU_LIST_REACH && cover_at <= list_bytes &&
                        cover_used <= list_bytes - cover_at &&
                        list_bytes - cover_at - cover_used >= COMPOSITOR_GPU_LIST_REACH;

        for (gpu_list_u32 i = 0; i < count; i++) {
                struct gpu_list_op snap = *(const volatile struct gpu_list_op *)(from + 8 * (gpu_list_u64)i);
                gpu_list_u32 *w = snap.v;
                gpu_list_u32 kind = w[0] & 255u;

                if (kind <= 1u) {
                        gpu_list_i32 x0 = ref_gpu_list_cut(w[1], 0, width);
                        gpu_list_i32 y0 = ref_gpu_list_cut(w[2], 0, height);
                        gpu_list_i32 x1 = ref_gpu_list_cut(w[3], 0, width);
                        gpu_list_i32 y1 = ref_gpu_list_cut(w[4], 0, height);

                        if (w[0] >> 8)
                                return COMPOSITOR_GPU_LIST_REFUSED;
                        if (x0 >= x1 || y0 >= y1)
                                continue;
                        if (n >= room)
                                return COMPOSITOR_GPU_LIST_REFUSED;
                        to[8 * (gpu_list_u64)n]     = kind;
                        to[8 * (gpu_list_u64)n + 1] = (gpu_list_u32)x0;
                        to[8 * (gpu_list_u64)n + 2] = (gpu_list_u32)y0;
                        to[8 * (gpu_list_u64)n + 3] = (gpu_list_u32)x1;
                        to[8 * (gpu_list_u64)n + 4] = (gpu_list_u32)y1;
                        to[8 * (gpu_list_u64)n + 5] = w[5];
                        to[8 * (gpu_list_u64)n + 6] = 0u;
                        to[8 * (gpu_list_u64)n + 7] = 0u;
                        n++;
                } else if (kind == 2u) {
                        gpu_list_u32 records = w[0] >> 8;
                        if (records > 3u || records > count - 1u - i || (gpu_list_u64)n + 1u + records > room)
                                return COMPOSITOR_GPU_LIST_REFUSED;
                        for (gpu_list_u32 k = 0; k <= records; k++) {
                                struct gpu_list_op rsnap;
                                gpu_list_u32 *r = rsnap.v;
                                if (k) {
                                        rsnap = *(const volatile struct gpu_list_op *)(from + 8 * (gpu_list_u64)(i + k));
                                        if (r[0] != 8u && r[0] != 9u)
                                                return COMPOSITOR_GPU_LIST_REFUSED;
                                } else {
                                        rsnap = snap;
                                }
                                to[8 * (gpu_list_u64)n]     = k ? r[0] : (2u | (records << 8));
                                to[8 * (gpu_list_u64)n + 1] = (gpu_list_u32)ref_gpu_list_cut(r[1], -COMPOSITOR_GPU_LIST_COORD, COMPOSITOR_GPU_LIST_COORD);
                                to[8 * (gpu_list_u64)n + 2] = (gpu_list_u32)ref_gpu_list_cut(r[2], -COMPOSITOR_GPU_LIST_COORD, COMPOSITOR_GPU_LIST_COORD);
                                to[8 * (gpu_list_u64)n + 3] = (gpu_list_u32)ref_gpu_list_cut(r[3], -COMPOSITOR_GPU_LIST_COORD, COMPOSITOR_GPU_LIST_COORD);
                                to[8 * (gpu_list_u64)n + 4] = (gpu_list_u32)ref_gpu_list_cut(r[4], -COMPOSITOR_GPU_LIST_COORD, COMPOSITOR_GPU_LIST_COORD);
                                to[8 * (gpu_list_u64)n + 5] = r[5];
                                to[8 * (gpu_list_u64)n + 6] = (gpu_list_u32)ref_gpu_list_cut(r[6], 0, COMPOSITOR_GPU_LIST_COORD);
                                to[8 * (gpu_list_u64)n + 7] = (gpu_list_u32)ref_gpu_list_cut(r[7], 0, COMPOSITOR_GPU_LIST_COORD);
                                n++;
                        }
                        i += records;
                } else if (kind == 3u) {
                        gpu_list_u64 pitch = w[0] >> 8;
                        gpu_list_i32 x0 = (gpu_list_i32)w[1], y0 = (gpu_list_i32)w[2];
                        gpu_list_i32 x1 = (gpu_list_i32)w[3], y1 = (gpu_list_i32)w[4];
                        if (!text_room || pitch < 3u || pitch > 3500u || x0 < 0 || y0 < 0 || x1 > width || y1 > height)
                                return COMPOSITOR_GPU_LIST_REFUSED;
                        if (x0 >= x1 || y0 >= y1)
                                continue;
                        if (n >= room || (gpu_list_u64)(gpu_list_u32)(x1 - x0) + 2u > pitch)
                                return COMPOSITOR_GPU_LIST_REFUSED;
                        if ((gpu_list_u64)w[7] < pitch + 1u)
                                return COMPOSITOR_GPU_LIST_REFUSED;
                        gpu_list_u64 bytes_from = (gpu_list_u64)w[7] - pitch - 1u;
                        if (bytes_from < cover_at || (gpu_list_u64)w[7] > cover_at + cover_used ||
                            (gpu_list_u64)(gpu_list_u32)(y1 - y0) * pitch + (gpu_list_u64)(gpu_list_u32)(x1 - x0) + 1u >
                                cover_at + cover_used - (gpu_list_u64)w[7])
                                return COMPOSITOR_GPU_LIST_REFUSED;
                        to[8 * (gpu_list_u64)n]     = 3u | ((gpu_list_u32)pitch << 8);
                        to[8 * (gpu_list_u64)n + 1] = (gpu_list_u32)x0;
                        to[8 * (gpu_list_u64)n + 2] = (gpu_list_u32)y0;
                        to[8 * (gpu_list_u64)n + 3] = (gpu_list_u32)x1;
                        to[8 * (gpu_list_u64)n + 4] = (gpu_list_u32)y1;
                        to[8 * (gpu_list_u64)n + 5] = w[5];
                        to[8 * (gpu_list_u64)n + 6] = (w[6] & 0xffffu) | (slot << 16);
                        to[8 * (gpu_list_u64)n + 7] = w[7];
                        n++;
                } else if (kind == 10u) {
                        gpu_list_u64 pitch = w[0] >> 8, table = w[5], at = w[7];
                        gpu_list_i32 x0 = (gpu_list_i32)w[1], y0 = (gpu_list_i32)w[2];
                        gpu_list_i32 x1 = (gpu_list_i32)w[3], y1 = (gpu_list_i32)w[4];
                        if (!text_room || x0 < 0 || y0 < 0 || x1 > width || y1 > height)
                                return COMPOSITOR_GPU_LIST_REFUSED;
                        if (x0 >= x1 || y0 >= y1)
                                continue;
                        gpu_list_u64 cols = (gpu_list_u64)(gpu_list_u32)(x1 - x0);
                        gpu_list_u64 rows = (gpu_list_u64)(gpu_list_u32)(y1 - y0);
                        if (n >= room || pitch < cols || pitch > 3500u || (table & 3u) || table < cover_at ||
                            table > cover_at + cover_used || 1024u > cover_at + cover_used - table ||
                            at < cover_at || at > cover_at + cover_used ||
                            (rows - 1u) * pitch + cols > cover_at + cover_used - at)
                                return COMPOSITOR_GPU_LIST_REFUSED;
                        to[8 * (gpu_list_u64)n]     = 10u | ((gpu_list_u32)pitch << 8);
                        to[8 * (gpu_list_u64)n + 1] = (gpu_list_u32)x0;
                        to[8 * (gpu_list_u64)n + 2] = (gpu_list_u32)y0;
                        to[8 * (gpu_list_u64)n + 3] = (gpu_list_u32)x1;
                        to[8 * (gpu_list_u64)n + 4] = (gpu_list_u32)y1;
                        to[8 * (gpu_list_u64)n + 5] = w[5];
                        to[8 * (gpu_list_u64)n + 6] = slot << 16;
                        to[8 * (gpu_list_u64)n + 7] = w[7];
                        n++;
                } else {
                        return COMPOSITOR_GPU_LIST_REFUSED;
                }
        }
        return n;
}

/* ── Test Harness ── */

static int g_failures = 0;

static uint64_t rng_state = 0x9e3779b97f4a7c15ull;
static uint64_t xorshift64(void)
{
        uint64_t x = rng_state;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        return rng_state = x;
}

static uint64_t now_ns(void)
{
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void check_pair(const char *tag, const struct gpu_list_spec *base, gpu_list_u32 *out_a, gpu_list_u32 *out_r, size_t cap_ops)
{
        struct gpu_list_spec sa = *base, sr = *base;
        sa.to = out_a;
        sr.to = out_r;
        if (out_a && cap_ops) memset(out_a, 0xa5, cap_ops * 32);
        if (out_r && cap_ops) memset(out_r, 0xa5, cap_ops * 32);

        gpu_list_u32 ra = gpu_list_ops(&sa);
        gpu_list_u32 rr = ref_gpu_list_ops(&sr);

        if (ra != rr) {
                fprintf(stderr, "FAIL [%s]: ret asm=%u (0x%x) ref=%u (0x%x)\n", tag, ra, ra, rr, rr);
                g_failures++;
                return;
        }
        if (ra != COMPOSITOR_GPU_LIST_REFUSED && out_a && out_r && ra > 0) {
                if (memcmp(out_a, out_r, (size_t)ra * 32) != 0) {
                        fprintf(stderr, "FAIL [%s]: output mismatch on %u ops\n", tag, ra);
                        for (gpu_list_u32 i = 0; i < ra * 8; i++) {
                                if (out_a[i] != out_r[i]) {
                                        fprintf(stderr, "  word[%u] (op %u w%u): asm=0x%08x ref=0x%08x\n",
                                                i, i / 8, i % 8, out_a[i], out_r[i]);
                                        break;
                                }
                        }
                        g_failures++;
                }
        }
}

static void test_cut(void)
{
        const gpu_list_i32 vals[] = {
                INT32_MIN, INT32_MIN + 1, -COMPOSITOR_GPU_LIST_COORD - 1, -COMPOSITOR_GPU_LIST_COORD,
                -COMPOSITOR_GPU_LIST_COORD + 1, -100, -1, 0, 1, 100,
                COMPOSITOR_GPU_LIST_COORD - 1, COMPOSITOR_GPU_LIST_COORD, COMPOSITOR_GPU_LIST_COORD + 1,
                INT32_MAX - 1, INT32_MAX
        };
        const size_t nv = sizeof(vals) / sizeof(vals[0]);
        for (size_t i = 0; i < nv; i++) {
                for (size_t j = 0; j < nv; j++) {
                        for (size_t k = 0; k < nv; k++) {
                                gpu_list_i32 a = gpu_list_cut((gpu_list_u32)vals[i], vals[j], vals[k]);
                                gpu_list_i32 r = ref_gpu_list_cut((gpu_list_u32)vals[i], vals[j], vals[k]);
                                if (a != r) {
                                        fprintf(stderr, "FAIL cut(%d, %d, %d): asm=%d ref=%d\n", vals[i], vals[j], vals[k], a, r);
                                        g_failures++;
                                        return;
                                }
                        }
                }
        }
}

static void test_edge_cases(void)
{
        gpu_list_u32 in[64], oa[64], or_[64];
        memset(in, 0, sizeof(in));

        /* NULL spec */
        if (gpu_list_ops(NULL) != COMPOSITOR_GPU_LIST_REFUSED) {
                fprintf(stderr, "FAIL NULL spec\n");
                g_failures++;
        }

        struct gpu_list_spec s = {
                .from = in, .to = oa,
                .cover_at = 4096, .cover_used = 65536, .list_bytes = 4096 + 65536 + 4096,
                .count = 0, .room = 8, .width = 1920, .height = 1080, .slot = 1, ._pad = 0
        };

        /* Empty list */
        check_pair("empty", &s, oa, or_, 8);

        /* NULL from with count == 0 vs count == 1 */
        s.from = NULL; s.count = 0;
        check_pair("null_from_cnt0", &s, oa, or_, 8);
        s.count = 1;
        check_pair("null_from_cnt1", &s, oa, or_, 8);
        s.from = in;

        /* Invalid width/height/slot */
        const gpu_list_i32 bad_dims[] = {0, -1, INT32_MIN};
        for (size_t i = 0; i < 3; i++) {
                s.count = 0; s.width = bad_dims[i]; s.height = 1080; s.slot = 1;
                check_pair("bad_w", &s, oa, or_, 8);
                s.width = 1920; s.height = bad_dims[i];
                check_pair("bad_h", &s, oa, or_, 8);
        }
        s.width = 1920; s.height = 1080;
        const gpu_list_u32 slots[] = {0, 1, 127, 255, 256, UINT32_MAX};
        for (size_t i = 0; i < 6; i++) {
                s.slot = slots[i];
                check_pair("slot_edge", &s, oa, or_, 8);
        }
        s.slot = 7;

        /* NULL to: all-skipped returns 0, non-skipped returns REFUSED */
        in[0] = 0; in[1] = 100; in[2] = 100; in[3] = 50; in[4] = 200; in[5] = 0xdeadbeef;
        s.count = 1; s.room = 8;
        check_pair("null_to_skip", &s, NULL, NULL, 0);
        in[3] = 200;
        check_pair("null_to_emit", &s, NULL, NULL, 0);

        /* Kind 0 & 1: clamping extremes, upper bits, empty rects, room full */
        gpu_list_u32 k01_ops[][8] = {
                {0, (gpu_list_u32)INT32_MIN, (gpu_list_u32)INT32_MIN, (gpu_list_u32)INT32_MAX, (gpu_list_u32)INT32_MAX, 0x11223344, 0xffffffff, 0xaaaaaaaa},
                {1, (gpu_list_u32)-50, 0, 100, 2000, 0x55667788, 123, 456},
                {0, 1920, 0, 2000, 1080, 1, 0, 0},              /* clamped x0==x1 -> skip */
                {1, (gpu_list_u32)-100, (gpu_list_u32)-100, (gpu_list_u32)-1, (gpu_list_u32)-1, 2, 0, 0}, /* clamped 0==0 -> skip */
                {0 | (1u << 8), 0, 0, 10, 10, 0, 0, 0},         /* upper bits -> REFUSED */
                {1 | (1u << 31), 10, 10, 0, 0, 0, 0, 0},        /* upper bits even when empty -> REFUSED */
        };
        for (size_t i = 0; i < 6; i++) {
                memcpy(in, k01_ops[i], 32);
                s.count = 1; s.room = 1;
                check_pair("k01_case", &s, oa, or_, 8);
                s.room = 0;
                check_pair("k01_room0", &s, oa, or_, 8);
        }

        /* Kind 2: AA box with 0..4 records, truncated, bad follow-up, coord extremes */
        for (gpu_list_u32 recs = 0; recs <= 4; recs++) {
                memset(in, 0, sizeof(in));
                in[0] = 2u | (recs << 8);
                in[1] = (gpu_list_u32)INT32_MIN; in[2] = (gpu_list_u32)INT32_MAX;
                in[3] = (gpu_list_u32)(-COMPOSITOR_GPU_LIST_COORD - 1); in[4] = (gpu_list_u32)(COMPOSITOR_GPU_LIST_COORD + 1);
                in[5] = 0xcafebabe; in[6] = (gpu_list_u32)INT32_MIN; in[7] = (gpu_list_u32)INT32_MAX;
                for (gpu_list_u32 k = 1; k <= recs && k <= 4; k++) {
                        in[8 * k] = (k & 1) ? 8u : 9u;
                        in[8 * k + 1] = (gpu_list_u32)-COMPOSITOR_GPU_LIST_COORD;
                        in[8 * k + 2] = (gpu_list_u32)COMPOSITOR_GPU_LIST_COORD;
                        in[8 * k + 3] = 12345; in[8 * k + 4] = (gpu_list_u32)-12345;
                        in[8 * k + 5] = 0x100 + k;
                        in[8 * k + 6] = (gpu_list_u32)-1; in[8 * k + 7] = COMPOSITOR_GPU_LIST_COORD + 7;
                }
                s.count = recs + 1; s.room = recs + 1;
                check_pair("k2_exact", &s, oa, or_, 8);
                s.room = recs; /* 1 short */
                check_pair("k2_short_room", &s, oa, or_, 8);
                s.count = recs; s.room = 8; /* truncated input */
                check_pair("k2_trunc", &s, oa, or_, 8);
                if (recs >= 1 && recs <= 3) {
                        in[8 * recs] = 7u; /* invalid follow-up */
                        s.count = recs + 1; s.room = 8;
                        check_pair("k2_bad_rec7", &s, oa, or_, 8);
                        in[8 * recs] = 8u | (1u << 8); /* upper bits on 8 */
                        check_pair("k2_bad_rec8hi", &s, oa, or_, 8);
                }
        }

        /* Kind 3: text run boundaries */
        s.cover_at = 4096; s.cover_used = 4096; s.list_bytes = 4096 + 4096 + 4096;
        s.width = 1920; s.height = 1080; s.slot = 42; s.count = 1; s.room = 4;
        /* Exact fit: pitch=10, cols=8 (cols+2==pitch), rows=5, w7 = 4096+11 = 4107, need = 5*10+8+1 = 59 */
        gpu_list_u32 t3[8] = {3u | (10u << 8), 10, 20, 18, 25, 0xabcdef01, 0xdead1234, 4107};
        memcpy(in, t3, 32);
        check_pair("k3_valid", &s, oa, or_, 8);
        /* Exact end of cover: w7 = cover_end - 59 = 8192 - 59 = 8133 */
        in[7] = 8192 - 59; check_pair("k3_exact_end", &s, oa, or_, 8);
        in[7] = 8192 - 58; check_pair("k3_over_end", &s, oa, or_, 8);
        in[7] = 4106;      check_pair("k3_under_start", &s, oa, or_, 8);
        in[7] = 10;        check_pair("k3_w7_lt_pitch1", &s, oa, or_, 8);
        in[7] = 4107;
        in[0] = 3u | (2u << 8);    check_pair("k3_pitch2", &s, oa, or_, 8);
        in[0] = 3u | (3501u << 8); check_pair("k3_pitch3501", &s, oa, or_, 8);
        in[0] = 3u | (9u << 8);    check_pair("k3_cols2_gt_pitch", &s, oa, or_, 8);
        /* Empty text run with x1 < 0 -> skipped (not refused) */
        in[0] = 3u | (10u << 8); in[1] = 0; in[2] = 0; in[3] = (gpu_list_u32)-5; in[4] = 10;
        check_pair("k3_neg_x1_skip", &s, oa, or_, 8);
        /* text_room == 0 */
        s.list_bytes = 4096 + 4096 + 4095;
        memcpy(in, t3, 32);
        check_pair("k3_no_text_room", &s, oa, or_, 8);
        s.list_bytes = 4096 + 4096 + 4096;

        /* Kind 10: 256-tone glyph boundaries */
        /* pitch=8, cols=8, rows=4, table=4096 (aligned, 1024 bytes), at=5120, need=(4-1)*8+8 = 32 */
        gpu_list_u32 t10[8] = {10u | (8u << 8), 0, 0, 8, 4, 4096, 0x99999999, 5120};
        memcpy(in, t10, 32);
        check_pair("k10_valid", &s, oa, or_, 8);
        in[5] = 4097; check_pair("k10_unaligned_table", &s, oa, or_, 8);
        in[5] = 8192 - 1024; check_pair("k10_table_exact_end", &s, oa, or_, 8);
        in[5] = 8192 - 1020; check_pair("k10_table_over_end", &s, oa, or_, 8);
        in[5] = 4096;
        in[7] = 8192 - 32; check_pair("k10_at_exact_end", &s, oa, or_, 8);
        in[7] = 8192 - 31; check_pair("k10_at_over_end", &s, oa, or_, 8);
        /* Kind 10 empty rect with pitch=0 MUST skip, NOT refuse */
        in[0] = 10u; in[1] = 5; in[2] = 0; in[3] = 5; in[4] = 4;
        check_pair("k10_empty_pitch0", &s, oa, or_, 8);
        in[0] = 10u | (99999u << 8);
        check_pair("k10_empty_pitch_huge", &s, oa, or_, 8);

        /* Overflowing cover_at / cover_used / list_bytes */
        s.cover_at = UINT64_MAX - 100; s.cover_used = 200; s.list_bytes = UINT64_MAX;
        memcpy(in, t10, 32);
        check_pair("cover_u64_ovf", &s, oa, or_, 8);

        /* All 256 opcodes standalone */
        s.cover_at = 4096; s.cover_used = 4096; s.list_bytes = 12288; s.count = 1; s.room = 4;
        for (gpu_list_u32 op = 0; op < 256; op++) {
                memset(in, 0, 32);
                in[0] = op; in[1] = 0; in[2] = 0; in[3] = 10; in[4] = 10;
                check_pair("all_opcodes", &s, oa, or_, 8);
        }
}

/* ── mmap Guard-Page Tests (PROT_NONE before AND after both input and output) ── */

static void test_guard_pages(void)
{
        size_t pg = (size_t)sysconf(_SC_PAGESIZE);
        /* 3 pages each: [PROT_NONE][RW][PROT_NONE] */
        uint8_t *in_map  = mmap(NULL, 3 * pg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        uint8_t *out_map = mmap(NULL, 3 * pg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (in_map == MAP_FAILED || out_map == MAP_FAILED) {
                fprintf(stderr, "FAIL mmap\n");
                g_failures++;
                return;
        }
        if (mprotect(in_map, pg, PROT_NONE) || mprotect(in_map + 2 * pg, pg, PROT_NONE) ||
            mprotect(out_map, pg, PROT_NONE) || mprotect(out_map + 2 * pg, pg, PROT_NONE)) {
                fprintf(stderr, "FAIL mprotect\n");
                g_failures++;
                return;
        }

        uint8_t *in_rw  = in_map + pg;
        uint8_t *out_rw = out_map + pg;
        gpu_list_u32 ref_out[6 * 8];

        /* 6 ops: fill, tone, box+2 records, text run, glyph */
        gpu_list_u32 ops[6][8] = {
                {0u, 10, 20, 300, 400, 0xff00ff00u, 99, 88},
                {1u, (gpu_list_u32)-10, 5, 2000, 50, 0x12345678u, 77, 66},
                {2u | (2u << 8), -100, 200, 300, 400, 0xaabbccddu, 50, 60},
                {8u, 1, 2, 3, 4, 5, 6, 7},
                {9u, (gpu_list_u32)INT32_MIN, (gpu_list_u32)INT32_MAX, 0, 0, 9, (gpu_list_u32)-1, (gpu_list_u32)INT32_MAX},
                {10u | (16u << 8), 0, 0, 16, 8, 4096, 0xdeadbeefu, 5120},
        };
        size_t bytes = sizeof(ops);

        /* Mode 0: start-flush (right after leading PROT_NONE); Mode 1: end-flush (right before trailing PROT_NONE) */
        for (int in_end = 0; in_end < 2; in_end++) {
                for (int out_end = 0; out_end < 2; out_end++) {
                        gpu_list_u32 *pin  = (gpu_list_u32 *)(in_end  ? (in_rw  + pg - bytes) : in_rw);
                        gpu_list_u32 *pout = (gpu_list_u32 *)(out_end ? (out_rw + pg - bytes) : out_rw);
                        memcpy(pin, ops, bytes);

                        struct gpu_list_spec s = {
                                .from = pin, .to = pout,
                                .cover_at = 4096, .cover_used = 4096, .list_bytes = 12288,
                                .count = 6, .room = 6, .width = 1920, .height = 1080, .slot = 5, ._pad = 0
                        };
                        gpu_list_u32 ra = gpu_list_ops(&s);
                        s.to = ref_out;
                        gpu_list_u32 rr = ref_gpu_list_ops(&s);
                        if (ra != rr || ra != 6u || memcmp(pout, ref_out, bytes) != 0) {
                                fprintf(stderr, "FAIL guard(%d,%d): ra=%u rr=%u\n", in_end, out_end, ra, rr);
                                g_failures++;
                        }

                        /* Truncated kind 2 flush against trailing PROT_NONE: must refuse WITHOUT reading past end */
                        if (in_end == 1) {
                                gpu_list_u32 *p1 = (gpu_list_u32 *)(in_rw + pg - 32);
                                p1[0] = 2u | (3u << 8);
                                s.from = p1; s.count = 1; s.to = pout; s.room = 6;
                                if (gpu_list_ops(&s) != COMPOSITOR_GPU_LIST_REFUSED) {
                                        fprintf(stderr, "FAIL guard trunc k2\n");
                                        g_failures++;
                                }
                        }

                        /* Empty count=0 with pointer AT trailing PROT_NONE boundary: must not read */
                        s.from = (const gpu_list_u32 *)(in_rw + pg);
                        s.to = (gpu_list_u32 *)(out_rw + pg);
                        s.count = 0; s.room = 0;
                        if (gpu_list_ops(&s) != 0u) {
                                fprintf(stderr, "FAIL guard empty at PROT_NONE\n");
                                g_failures++;
                        }
                }
        }

        munmap(in_map, 3 * pg);
        munmap(out_map, 3 * pg);
}

/* ── Randomized Differential Fuzzing ── */

static gpu_list_u32 rand_coord(gpu_list_i32 bound)
{
        uint64_t r = xorshift64();
        switch (r & 7u) {
        case 0: return (gpu_list_u32)INT32_MIN;
        case 1: return (gpu_list_u32)INT32_MAX;
        case 2: return (gpu_list_u32)-1;
        case 3: return 0u;
        case 4: return (gpu_list_u32)bound;
        case 5: return (gpu_list_u32)(bound + 1);
        case 6: return (gpu_list_u32)((int32_t)(r >> 8) % (bound > 0 ? bound + 32 : 64) - 16);
        default: return (gpu_list_u32)(r >> 32);
        }
}

static void test_fuzz(void)
{
        enum { MAX_OPS = 24 };
        gpu_list_u32 in[MAX_OPS * 8], oa[MAX_OPS * 8], or_[MAX_OPS * 8];
        const int ITERS = 200000;

        for (int iter = 0; iter < ITERS; iter++) {
                uint64_t r = xorshift64();
                gpu_list_u32 count = (gpu_list_u32)(r % (MAX_OPS + 1));
                gpu_list_u32 room  = (gpu_list_u32)((r >> 8) % (MAX_OPS + 2));
                gpu_list_i32 w     = (r & (1ull << 16)) ? (gpu_list_i32)(1 + ((r >> 17) % 4096)) : (gpu_list_i32)((r >> 17) % 8) - 2;
                gpu_list_i32 h     = (r & (1ull << 29)) ? (gpu_list_i32)(1 + ((r >> 30) % 4096)) : (gpu_list_i32)((r >> 30) % 8) - 2;
                gpu_list_u32 slot  = ((r >> 42) & 31u) == 0 ? ((r >> 47) & 1u ? 0u : 256u) : (gpu_list_u32)(1u + ((r >> 47) % 255u));

                gpu_list_u64 cover_at   = (r & (1ull << 55)) ? 4096ull + ((r >> 56) & 0xfu) * 4ull : 4092ull + ((r >> 56) & 7ull);
                gpu_list_u64 cover_used = 4096ull + (xorshift64() & 0x7fffull);
                gpu_list_u64 list_bytes = cover_at + cover_used + ((xorshift64() & 7u) ? 4096ull : 4095ull);

                for (gpu_list_u32 i = 0; i < count; i++) {
                        gpu_list_u32 *op = in + 8 * i;
                        uint64_t k = xorshift64();
                        unsigned sel = (unsigned)(k % 12u);
                        if (sel < 3) {
                                /* kind 0 or 1 */
                                op[0] = (sel & 1u) | (((k >> 8) & 31u) == 0 ? (1u << 8) : 0u);
                                op[1] = rand_coord(w); op[2] = rand_coord(h);
                                op[3] = rand_coord(w); op[4] = rand_coord(h);
                                op[5] = (gpu_list_u32)(k >> 16); op[6] = (gpu_list_u32)k; op[7] = (gpu_list_u32)(k >> 32);
                        } else if (sel < 6) {
                                /* kind 2 + follow-ups */
                                gpu_list_u32 recs = (gpu_list_u32)((k >> 4) % 5u);
                                op[0] = 2u | (recs << 8);
                                for (int j = 1; j < 8; j++) op[j] = rand_coord(COMPOSITOR_GPU_LIST_COORD);
                                for (gpu_list_u32 m = 1; m <= recs && i + m < count; m++) {
                                        gpu_list_u32 *rop = in + 8 * (i + m);
                                        uint64_t rk = xorshift64();
                                        rop[0] = ((rk & 15u) == 0) ? (gpu_list_u32)(rk & 0xffu) : (8u + (gpu_list_u32)(rk & 1u));
                                        for (int j = 1; j < 8; j++) rop[j] = rand_coord(COMPOSITOR_GPU_LIST_COORD);
                                }
                                if (recs <= 3 && i + recs < count) i += recs;
                        } else if (sel < 9) {
                                /* kind 3 */
                                gpu_list_u32 cols = 1u + (gpu_list_u32)((k >> 8) % 64u);
                                gpu_list_u32 rows = 1u + (gpu_list_u32)((k >> 16) % 32u);
                                gpu_list_u32 pitch = cols + 2u + (gpu_list_u32)((k >> 24) % 8u);
                                if (((k >> 32) & 15u) == 0) pitch = (gpu_list_u32)((k >> 36) % 4000u);
                                gpu_list_i32 x0 = (w > (gpu_list_i32)cols) ? (gpu_list_i32)((k >> 40) % (uint64_t)(w - (gpu_list_i32)cols)) : 0;
                                gpu_list_i32 y0 = (h > (gpu_list_i32)rows) ? (gpu_list_i32)((k >> 48) % (uint64_t)(h - (gpu_list_i32)rows)) : 0;
                                op[0] = 3u | (pitch << 8);
                                op[1] = (gpu_list_u32)x0; op[2] = (gpu_list_u32)y0;
                                op[3] = (gpu_list_u32)(x0 + (gpu_list_i32)cols); op[4] = (gpu_list_u32)(y0 + (gpu_list_i32)rows);
                                op[5] = (gpu_list_u32)k; op[6] = (gpu_list_u32)(k >> 32);
                                op[7] = (gpu_list_u32)(cover_at + pitch + 1u + (xorshift64() % 2048u));
                        } else if (sel < 11) {
                                /* kind 10 */
                                gpu_list_u32 cols = 1u + (gpu_list_u32)((k >> 8) % 32u);
                                gpu_list_u32 rows = 1u + (gpu_list_u32)((k >> 16) % 32u);
                                gpu_list_u32 pitch = cols + (gpu_list_u32)((k >> 24) % 8u);
                                gpu_list_i32 x0 = (w > (gpu_list_i32)cols) ? (gpu_list_i32)((k >> 40) % (uint64_t)(w - (gpu_list_i32)cols)) : 0;
                                gpu_list_i32 y0 = (h > (gpu_list_i32)rows) ? (gpu_list_i32)((k >> 48) % (uint64_t)(h - (gpu_list_i32)rows)) : 0;
                                op[0] = 10u | (pitch << 8);
                                op[1] = (gpu_list_u32)x0; op[2] = (gpu_list_u32)y0;
                                op[3] = (gpu_list_u32)(x0 + (gpu_list_i32)cols); op[4] = (gpu_list_u32)(y0 + (gpu_list_i32)rows);
                                op[5] = (gpu_list_u32)((cover_at + (xorshift64() % 1024u)) & ~3ull) + (((k >> 56) & 15u) == 0 ? 1u : 0u);
                                op[6] = (gpu_list_u32)(k >> 32);
                                op[7] = (gpu_list_u32)(cover_at + (xorshift64() % 2048u));
                        } else {
                                /* random / invalid opcode */
                                op[0] = (gpu_list_u32)k;
                                for (int j = 1; j < 8; j++) op[j] = (gpu_list_u32)xorshift64();
                        }
                }

                struct gpu_list_spec s = {
                        .from = in, .to = oa,
                        .cover_at = cover_at, .cover_used = cover_used, .list_bytes = list_bytes,
                        .count = count, .room = room, .width = w, .height = h, .slot = slot, ._pad = 0
                };
                check_pair("fuzz", &s, oa, or_, MAX_OPS);
                if (g_failures >= 10) return;
        }
}

/* ── Throughput Benchmark ── */

static void bench(void)
{
        enum { NOPS = 1024 };
        static gpu_list_u32 in[NOPS * 8], out[NOPS * 8];
        gpu_list_u32 i = 0;
        while (i < NOPS) {
                gpu_list_u32 *op = in + 8 * i;
                unsigned m = i % 10u;
                if (m < 4) {
                        /* fill / tone */
                        op[0] = m & 1u;
                        op[1] = 10 + m * 5; op[2] = 20; op[3] = 400 + m * 10; op[4] = 300;
                        op[5] = 0xff204060u; op[6] = 0; op[7] = 0;
                        i++;
                } else if (m == 4 && i + 2 < NOPS) {
                        /* AA box + 2 records */
                        op[0] = 2u | (2u << 8);
                        op[1] = 1000; op[2] = 2000; op[3] = 50000; op[4] = 40000; op[5] = 0xff112233u; op[6] = 2048; op[7] = 2048;
                        gpu_list_u32 *r1 = in + 8 * (i + 1);
                        r1[0] = 8u; r1[1] = 0; r1[2] = 0; r1[3] = 1000; r1[4] = 1000; r1[5] = 0xff445566u; r1[6] = 512; r1[7] = 512;
                        gpu_list_u32 *r2 = in + 8 * (i + 2);
                        r2[0] = 9u; r2[1] = 10; r2[2] = 20; r2[3] = 30; r2[4] = 40; r2[5] = 0xff778899u; r2[6] = 256; r2[7] = 256;
                        i += 3;
                } else if (m < 9) {
                        /* text run */
                        op[0] = 3u | (64u << 8);
                        op[1] = 100; op[2] = 100; op[3] = 148; op[4] = 116;
                        op[5] = 0xffffffffu; op[6] = 0x000000ffu; op[7] = 4096 + 65;
                        i++;
                } else {
                        /* 256-tone glyph */
                        op[0] = 10u | (16u << 8);
                        op[1] = 200; op[2] = 200; op[3] = 216; op[4] = 216;
                        op[5] = 4096; op[6] = 0; op[7] = 5120;
                        i++;
                }
        }

        struct gpu_list_spec s = {
                .from = in, .to = out,
                .cover_at = 4096, .cover_used = 65536, .list_bytes = 4096 + 65536 + 4096,
                .count = NOPS, .room = NOPS, .width = 1920, .height = 1080, .slot = 3, ._pad = 0
        };

        /* Warmup */
        volatile gpu_list_u32 sink = 0;
        for (int w = 0; w < 2000; w++) {
                sink ^= gpu_list_ops(&s);
                sink ^= ref_gpu_list_ops(&s);
        }

        const int RUNS = 40000;
        uint64_t t0 = now_ns();
        for (int r = 0; r < RUNS; r++) sink ^= gpu_list_ops(&s);
        uint64_t t_asm = now_ns() - t0;

        t0 = now_ns();
        for (int r = 0; r < RUNS; r++) sink ^= ref_gpu_list_ops(&s);
        uint64_t t_ref = now_ns() - t0;

        double ns_asm = (double)t_asm / ((double)RUNS * NOPS);
        double ns_ref = (double)t_ref / ((double)RUNS * NOPS);
        printf("BENCH (%d x %d ops): asm=%.2f ns/op  ref=%.2f ns/op  speedup=%.2fx  (sink=%u)\n",
               RUNS, NOPS, ns_asm, ns_ref, ns_ref / ns_asm, (unsigned)sink);
}

int main(void)
{
        test_cut();
        test_edge_cases();
        test_guard_pages();
        test_fuzz();
        if (g_failures) {
                fprintf(stderr, "FAILED: %d failure(s)\n", g_failures);
                return 1;
        }
        printf("PASS: cut + edge_cases + guard_pages + 200k fuzz (0 failures)\n");
        bench();
        return 0;
}
