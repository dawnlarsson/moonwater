/*
        Exhaustive correctness, security, mmap guard-page, differential fuzz,
        and performance/size proof for src/waterlink/modem_sync.asm.c across
        arm64, x86-64, and riscv64.
*/
#define _GNU_SOURCE
#define WATERLINK_MODEM_SYNC_ASM_IMPLEMENTATION
#include "../src/waterlink/modem_sync.asm.c"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define SYNC_V2 0xC5693DA7u
#define SYNC_V1 0x3C96C35Au

/* ---- Reference -O2 C implementations (exported for nm --print-size) ---- */

__attribute__((noinline))
ms_u64 ref_modem_sync_unpack(const ms_u8 *best, ms_u64 n, ms_u64 off, ms_u64 sym, ms_u32 sym_bits, ms_u8 *bits)
{
        ms_u64 nb = 0, at;
        if (!best || !bits || !sym || sym_bits == 0 || sym_bits > 8 || off > n || n - off < sym)
                return 0;
        for (at = off; at <= n - sym; at += sym) {
                ms_u32 j, v = best[at];
                for (j = 0; j < sym_bits; j++)
                        bits[nb++] = (ms_u8)((v >> j) & 1u);
                if (n - at < sym)
                        break;
        }
        return nb;
}

__attribute__((noinline))
ms_i64 ref_modem_sync_find(const ms_u8 *bits, ms_u64 nb, ms_u64 start_bit, ms_u32 sync_word)
{
        ms_u32 window = 0;
        ms_u64 i;
        if (!bits || nb < 32 || start_bit > nb - 32)
                return -1;
        for (i = 0; i < 32; i++)
                window |= (ms_u32)(bits[start_bit + i] & 1u) << i;
        for (i = start_bit;; i++) {
                ms_u64 p = i + 32;
                if (window == sync_word)
                        return (ms_i64)i;
                if (p >= nb)
                        break;
                window = (window >> 1) | ((ms_u32)(bits[p] & 1u) << 31);
        }
        return -1;
}

__attribute__((noinline))
ms_i32 ref_modem_sync_read_bytes(const ms_u8 *bits, ms_u64 nb, ms_u64 p, ms_u8 *dst, ms_u64 count)
{
        ms_u64 i, j;
        if (!bits || !dst || p > nb || count > (nb - p) / 8)
                return 0;
        for (i = 0; i < count; i++) {
                ms_u8 v = 0;
                for (j = 0; j < 8; j++)
                        v |= (ms_u8)((bits[p + 8 * i + j] & 1u) << j);
                dst[i] = v;
        }
        return 1;
}

__attribute__((noinline))
ms_i32 ref_modem_sync_memo_has(const ms_u8 *frames, const ms_u64 *totals, ms_u32 count, ms_u64 stride,
                               const ms_u8 *bytes, ms_u64 total)
{
        ms_u32 i, found = 0;
        if (!frames || !totals || !bytes || count == 0 || total > stride)
                return 0;
        for (i = 0; i < count; i++) {
                ms_u64 diff = totals[i] ^ total, t;
                const ms_u8 *slot = frames + (ms_u64)i * stride;
                for (t = 0; t < total; t++)
                        diff |= (ms_u64)(slot[t] ^ bytes[t]);
                found |= (diff == 0);
        }
        return (ms_i32)found;
}

/* ---- Test helpers ------------------------------------------------------ */

static int failures = 0;

static void check(int ok, const char *name)
{
        if (!ok) {
                fprintf(stderr, "FAIL: %s\n", name);
                failures++;
        }
}

static uint64_t rng = 0x243f6a8885a308d3ULL;

static uint64_t xorshift64(void)
{
        uint64_t x = rng;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        rng = x;
        return x;
}

static void write_u32_bits(ms_u8 *bits, ms_u64 pos, ms_u32 word)
{
        for (int k = 0; k < 32; k++)
                bits[pos + (ms_u64)k] = (ms_u8)((word >> k) & 1u);
}

static uint64_t now_ns(void)
{
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* ---- 1. Edge cases & security tests ----------------------------------- */

static void test_edge_cases(void)
{
        ms_u8 bits[256];
        ms_u8 dst[64], ref_dst[64];
        ms_u8 best[256];
        ms_u8 frames[4 * 276];
        ms_u64 totals[4] = {10, 20, 0, 276};

        /* NULL pointers & overflow bounds */
        check(modem_sync_find(NULL, 64, 0, SYNC_V2) == -1, "find NULL");
        check(modem_sync_find(bits, 0, 0, SYNC_V2) == -1, "find nb=0");
        check(modem_sync_find(bits, 31, 0, SYNC_V2) == -1, "find nb=31");
        check(modem_sync_find(bits, 32, 1, SYNC_V2) == -1, "find start>nb-32");
        check(modem_sync_find(bits, 64, UINT64_MAX, SYNC_V2) == -1, "find start=UINT64_MAX");
        check(modem_sync_find(bits, 64, 33, SYNC_V2) == -1, "find start=33 nb=64");

        check(modem_sync_read_bytes(NULL, 64, 0, dst, 1) == 0, "read NULL bits");
        check(modem_sync_read_bytes(bits, 64, 0, NULL, 1) == 0, "read NULL dst");
        check(modem_sync_read_bytes(bits, 64, 65, dst, 0) == 0, "read p>nb count=0");
        check(modem_sync_read_bytes(bits, 64, 64, dst, 0) == 1, "read p==nb count=0");
        check(modem_sync_read_bytes(bits, 64, 0, dst, 9) == 0, "read count>nb/8");
        check(modem_sync_read_bytes(bits, 64, 1, dst, 8) == 0, "read unaligned oob");
        check(modem_sync_read_bytes(bits, 64, 0, dst, UINT64_MAX) == 0, "read count=UINT64_MAX");
        check(modem_sync_read_bytes(bits, 64, UINT64_MAX, dst, 1) == 0, "read p=UINT64_MAX");

        check(modem_sync_unpack(NULL, 80, 0, 40, 4, bits) == 0, "unpack NULL best");
        check(modem_sync_unpack(best, 80, 0, 40, 4, NULL) == 0, "unpack NULL bits");
        check(modem_sync_unpack(best, 80, 0, 0, 4, bits) == 0, "unpack sym=0");
        check(modem_sync_unpack(best, 80, 0, 40, 0, bits) == 0, "unpack sym_bits=0");
        check(modem_sync_unpack(best, 80, 0, 40, 9, bits) == 0, "unpack sym_bits=9");
        check(modem_sync_unpack(best, 39, 0, 40, 4, bits) == 0, "unpack n<sym");
        check(modem_sync_unpack(best, 80, 41, 40, 4, bits) == 0, "unpack n-off<sym");
        check(modem_sync_unpack(best, 80, 81, 40, 4, bits) == 0, "unpack off>n");

        check(modem_sync_memo_has(NULL, totals, 4, 276, dst, 10) == 0, "memo NULL frames");
        check(modem_sync_memo_has(frames, NULL, 4, 276, dst, 10) == 0, "memo NULL totals");
        check(modem_sync_memo_has(frames, totals, 4, 276, NULL, 10) == 0, "memo NULL bytes");
        check(modem_sync_memo_has(frames, totals, 0, 276, dst, 10) == 0, "memo count=0");
        check(modem_sync_memo_has(frames, totals, 4, 276, dst, 277) == 0, "memo total>stride");

        /* Exact sync-word placement at every bit offset 0..95 including 0, 1, 7, 8, 31, 32, N-32 */
        for (ms_u64 pos = 0; pos <= 96; pos++) {
                ms_u64 nb = pos + 32;
                memset(bits, 0, sizeof(bits));
                write_u32_bits(bits, pos, SYNC_V2);
                check(modem_sync_find(bits, nb, 0, SYNC_V2) == (ms_i64)pos, "find exact pos at end");
                check(modem_sync_find(bits, nb, pos, SYNC_V2) == (ms_i64)pos, "find exact start==pos");
                if (pos > 0)
                        check(modem_sync_find(bits, nb, pos - 1, SYNC_V2) == (ms_i64)pos, "find start==pos-1");
                check(modem_sync_find(bits, nb, pos + 1, SYNC_V2) == -1, "find start==pos+1 miss");
                if (nb > 32)
                        check(modem_sync_find(bits, nb - 1, 0, SYNC_V2) == -1, "find truncated partial sync");
        }

        /* Partial / 1-bit-flipped false positives */
        memset(bits, 0, sizeof(bits));
        write_u32_bits(bits, 19, SYNC_V2);
        for (int flip = 0; flip < 32; flip++) {
                bits[19 + flip] ^= 1;
                check(modem_sync_find(bits, 128, 0, SYNC_V2) == -1, "find 1-bit flip rejected");
                bits[19 + flip] ^= 1;
        }
        check(modem_sync_find(bits, 128, 0, SYNC_V2) == 19, "find restored sync");

        /* Sync word with upper bits dirty (non-0/1 bytes in bits[]) */
        for (int i = 0; i < 128; i++)
                bits[i] = (ms_u8)((i * 37) & 0xFEu);
        for (int k = 0; k < 32; k++)
                bits[37 + k] |= (ms_u8)((SYNC_V1 >> k) & 1u);
        check(modem_sync_find(bits, 128, 0, SYNC_V1) == 37, "find masks dirty upper bits");

        /* Non-byte-aligned read_bytes across all offsets 0..15 */
        for (int i = 0; i < 256; i++)
                bits[i] = (ms_u8)((xorshift64() & 1u) | ((i & 7) << 1));
        for (ms_u64 p = 0; p < 16; p++) {
                ms_u64 cnt = (256 - p) / 8;
                ms_i32 ra = modem_sync_read_bytes(bits, 256, p, dst, cnt);
                ms_i32 rc = ref_modem_sync_read_bytes(bits, 256, p, ref_dst, cnt);
                check(ra == 1 && rc == 1 && memcmp(dst, ref_dst, (size_t)cnt) == 0, "read unaligned parity");
        }

        /* Multiple sync words + resumable scan */
        memset(bits, 0, sizeof(bits));
        write_u32_bits(bits, 0, SYNC_V2);
        write_u32_bits(bits, 35, SYNC_V2);
        write_u32_bits(bits, 100, SYNC_V2);
        check(modem_sync_find(bits, 160, 0, SYNC_V2) == 0, "multi hit 0");
        check(modem_sync_find(bits, 160, 1, SYNC_V2) == 35, "multi hit 35");
        check(modem_sync_find(bits, 160, 36, SYNC_V2) == 100, "multi hit 100");
        check(modem_sync_find(bits, 160, 101, SYNC_V2) == -1, "multi hit done");

        /* Memo exact + 0-length + 1-byte-diff + wrong-length */
        memset(frames, 0x5A, sizeof(frames));
        for (int i = 0; i < 4 * 276; i++)
                frames[i] = (ms_u8)(i * 13 + 7);
        memcpy(dst, frames + 276, 20);
        check(modem_sync_memo_has(frames, totals, 4, 276, dst, 20) == 1, "memo hit slot 1");
        dst[19] ^= 1;
        check(modem_sync_memo_has(frames, totals, 4, 276, dst, 20) == 0, "memo 1-bit tail diff");
        check(modem_sync_memo_has(frames, totals, 4, 276, dst, 0) == 1, "memo zero-length hit slot 2");
        check(modem_sync_memo_has(frames, totals, 2, 276, dst, 0) == 0, "memo zero-length outside count");
}

/* ---- 2. mmap PROT_NONE guard-page tests (before AND after) ------------ */

static void test_guard_pages(void)
{
        size_t pg = (size_t)sysconf(_SC_PAGESIZE);
        /* 3 pages: [PROT_NONE][RW][PROT_NONE] */
        uint8_t *map_in = mmap(NULL, 3 * pg, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
        uint8_t *map_out = mmap(NULL, 3 * pg, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
        check(map_in != MAP_FAILED && map_out != MAP_FAILED, "mmap guard alloc");
        if (map_in == MAP_FAILED || map_out == MAP_FAILED)
                return;
        check(mprotect(map_in + pg, pg, PROT_READ | PROT_WRITE) == 0, "mprotect in");
        check(mprotect(map_out + pg, pg, PROT_READ | PROT_WRITE) == 0, "mprotect out");

        uint8_t *in_lo = map_in + pg;
        uint8_t *in_hi = map_in + 2 * pg;
        uint8_t *out_lo = map_out + pg;
        uint8_t *out_hi = map_out + 2 * pg;

        /* Test modem_sync_find flush against BOTH before-guard and after-guard */
        for (ms_u64 nb = 32; nb <= 128; nb++) {
                uint8_t *b_after = in_hi - nb;
                memset(b_after, 0, (size_t)nb);
                write_u32_bits(b_after, nb - 32, SYNC_V2);
                check(modem_sync_find(b_after, nb, 0, SYNC_V2) == (ms_i64)(nb - 32), "guard after find hit");
                b_after[nb - 1] ^= 1;
                check(modem_sync_find(b_after, nb, 0, SYNC_V2) == -1, "guard after find miss");

                memset(in_lo, 0, (size_t)nb);
                write_u32_bits(in_lo, 0, SYNC_V2);
                check(modem_sync_find(in_lo, nb, 0, SYNC_V2) == 0, "guard before find hit");
                in_lo[0] ^= 1;
                check(modem_sync_find(in_lo, nb, 0, SYNC_V2) == -1, "guard before find miss");
        }

        /* Test modem_sync_read_bytes flush against after-guard and before-guard */
        for (ms_u64 cnt = 1; cnt <= 33; cnt++) {
                for (ms_u64 off = 0; off < 8; off++) {
                        ms_u64 nb = off + cnt * 8;
                        uint8_t *b = in_hi - nb;
                        uint8_t *d = out_hi - cnt;
                        for (ms_u64 k = 0; k < nb; k++)
                                b[k] = (ms_u8)(k & 1u);
                        check(modem_sync_read_bytes(b, nb, off, d, cnt) == 1, "guard after read_bytes");
                        check(modem_sync_read_bytes(in_lo, nb, off, out_lo, cnt) == 1, "guard before read_bytes");
                }
        }

        /* Test modem_sync_unpack flush against after-guard and before-guard (sym_bits 1..8) */
        for (ms_u32 sb = 1; sb <= 8; sb++) {
                ms_u64 sym = 40, nsyms = 17, n = nsyms * sym;
                uint8_t *best_after = in_hi - n;
                uint8_t *bits_after = out_hi - nsyms * sb;
                for (ms_u64 k = 0; k < n; k++)
                        best_after[k] = (ms_u8)(k & 0xFFu);
                check(modem_sync_unpack(best_after, n, 0, sym, sb, bits_after) == nsyms * sb, "guard after unpack");
                memcpy(in_lo, best_after, (size_t)n);
                check(modem_sync_unpack(in_lo, n, 0, sym, sb, out_lo) == nsyms * sb, "guard before unpack");
        }

        /* Test modem_sync_memo_has flush against after-guard and before-guard */
        for (ms_u64 total = 0; total <= 39; total++) {
                ms_u64 stride = 40;
                uint8_t *fr = in_hi - 4 * stride;
                uint8_t *by = out_hi - (total ? total : 1);
                ms_u64 tot[4] = {total, total + 1, 0, total};
                memset(fr, 0x33, (size_t)(4 * stride));
                if (total)
                        memset(by, 0x33, (size_t)total);
                check(modem_sync_memo_has(fr, tot, 4, stride, by, total) == 1, "guard after memo");
                memset(in_lo, 0x44, (size_t)(4 * stride));
                if (total)
                        memset(out_lo, 0x44, (size_t)total);
                check(modem_sync_memo_has(in_lo, tot, 4, stride, out_lo, total) == 1, "guard before memo");
        }

        munmap(map_in, 3 * pg);
        munmap(map_out, 3 * pg);
}

/* ---- 3. Randomized differential fuzzing vs -O2 C reference ------------ */

static void test_fuzz(void)
{
        ms_u8 bits[1024], best[4096], out_a[1024], out_c[1024];
        ms_u8 frames[4 * 276], cand[276];
        ms_u64 totals[4];

        for (int iter = 0; iter < 5000; iter++) {
                /* Fuzz modem_sync_find */
                ms_u64 nb = xorshift64() % 512;
                ms_u64 start = xorshift64() % (nb + 8);
                ms_u32 sw = (ms_u32)xorshift64();
                for (ms_u64 k = 0; k < nb; k++)
                        bits[k] = (ms_u8)(xorshift64() & 0xFFu);
                if (nb >= 32 && (xorshift64() & 1u)) {
                        ms_u64 pos = xorshift64() % (nb - 31);
                        write_u32_bits(bits, pos, sw);
                }
                ms_i64 fa = modem_sync_find(bits, nb, start, sw);
                ms_i64 fc = ref_modem_sync_find(bits, nb, start, sw);
                if (fa != fc) {
                        fprintf(stderr, "FUZZ find mismatch iter=%d nb=%llu start=%llu asm=%lld ref=%lld\n",
                                iter, (unsigned long long)nb, (unsigned long long)start,
                                (long long)fa, (long long)fc);
                        failures++;
                        return;
                }

                /* Fuzz modem_sync_read_bytes */
                ms_u64 p = xorshift64() % (nb + 8);
                ms_u64 cnt = xorshift64() % 70;
                memset(out_a, 0xAA, 70);
                memset(out_c, 0xAA, 70);
                ms_i32 ra = modem_sync_read_bytes(bits, nb, p, out_a, cnt);
                ms_i32 rc = ref_modem_sync_read_bytes(bits, nb, p, out_c, cnt);
                if (ra != rc || (ra && memcmp(out_a, out_c, (size_t)cnt) != 0)) {
                        fprintf(stderr, "FUZZ read_bytes mismatch iter=%d\n", iter);
                        failures++;
                        return;
                }

                /* Fuzz modem_sync_unpack */
                ms_u64 n = xorshift64() % 2000;
                ms_u64 sym = (xorshift64() % 4) == 0 ? 40 : ((xorshift64() % 40) + 1);
                ms_u64 off = xorshift64() % (sym + 5);
                ms_u32 sb = (ms_u32)((xorshift64() % 8) + 1);
                if (n / sym * sb <= sizeof(out_a)) {
                        for (ms_u64 k = 0; k < n; k++)
                                best[k] = (ms_u8)(xorshift64() & 0xFFu);
                        ms_u64 ua = modem_sync_unpack(best, n, off, sym, sb, out_a);
                        ms_u64 uc = ref_modem_sync_unpack(best, n, off, sym, sb, out_c);
                        if (ua != uc || memcmp(out_a, out_c, (size_t)ua) != 0) {
                                fprintf(stderr, "FUZZ unpack mismatch iter=%d ua=%llu uc=%llu sb=%u\n",
                                        iter, (unsigned long long)ua, (unsigned long long)uc, sb);
                                failures++;
                                return;
                        }
                }

                /* Fuzz modem_sync_memo_has */
                ms_u32 mc = (ms_u32)(xorshift64() % 5);
                ms_u64 total = xorshift64() % 277;
                for (ms_u32 s = 0; s < 4; s++) {
                        totals[s] = xorshift64() % 277;
                        for (ms_u64 k = 0; k < 276; k++)
                                frames[s * 276 + k] = (ms_u8)(xorshift64() & 0xFFu);
                }
                for (ms_u64 k = 0; k < total; k++)
                        cand[k] = (ms_u8)(xorshift64() & 0xFFu);
                if (mc > 0 && (xorshift64() & 1u)) {
                        ms_u32 pick = (ms_u32)(xorshift64() % mc);
                        totals[pick] = total;
                        memcpy(frames + pick * 276, cand, (size_t)total);
                        if (total > 0 && (xorshift64() & 1u))
                                cand[xorshift64() % total] ^= (ms_u8)(1u << (xorshift64() & 7u));
                }
                ms_i32 ma = modem_sync_memo_has(frames, totals, mc, 276, cand, total);
                ms_i32 mref = ref_modem_sync_memo_has(frames, totals, mc, 276, cand, total);
                if (ma != mref) {
                        fprintf(stderr, "FUZZ memo mismatch iter=%d ma=%d mref=%d\n", iter, ma, mref);
                        failures++;
                        return;
                }
        }
}

/* ---- 4. Benchmark vs -O2 C reference ---------------------------------- */

static void bench(void)
{
        static ms_u8 best[65536];
        static ms_u8 bits[16384];
        static ms_u8 dst[2048];
        static ms_u8 frames[4 * 276];
        ms_u64 totals[4] = {120, 276, 80, 276};
        volatile int64_t sink = 0;

        for (size_t i = 0; i < sizeof(best); i++)
                best[i] = (ms_u8)(xorshift64() & 0x0Fu);
        for (size_t i = 0; i < sizeof(bits); i++)
                bits[i] = (ms_u8)(xorshift64() & 1u);
        write_u32_bits(bits, sizeof(bits) - 40, SYNC_V2);
        for (size_t i = 0; i < sizeof(frames); i++)
                frames[i] = (ms_u8)(xorshift64() & 0xFFu);
        memcpy(dst, frames + 3 * 276, 276);

        const int it_find = 4000;
        uint64_t t0 = now_ns();
        for (int i = 0; i < it_find; i++) {
                __asm__ volatile("" : "+m"(bits));
                sink += modem_sync_find(bits, sizeof(bits), 0, SYNC_V2);
        }
        uint64_t t1 = now_ns();
        for (int i = 0; i < it_find; i++) {
                __asm__ volatile("" : "+m"(bits));
                sink += ref_modem_sync_find(bits, sizeof(bits), 0, SYNC_V2);
        }
        uint64_t t2 = now_ns();
        double asm_find = (double)(t1 - t0) / it_find;
        double ref_find = (double)(t2 - t1) / it_find;

        const int it_read = 20000;
        t0 = now_ns();
        for (int i = 0; i < it_read; i++) {
                __asm__ volatile("" : "+m"(bits), "+m"(dst));
                sink += modem_sync_read_bytes(bits, sizeof(bits), 3, dst, 276);
        }
        t1 = now_ns();
        for (int i = 0; i < it_read; i++) {
                __asm__ volatile("" : "+m"(bits), "+m"(dst));
                sink += ref_modem_sync_read_bytes(bits, sizeof(bits), 3, dst, 276);
        }
        t2 = now_ns();
        double asm_read = (double)(t1 - t0) / it_read;
        double ref_read = (double)(t2 - t1) / it_read;

        const int it_unp = 20000;
        t0 = now_ns();
        for (int i = 0; i < it_unp; i++) {
                __asm__ volatile("" : "+m"(best), "+m"(bits));
                sink += (int64_t)modem_sync_unpack(best, sizeof(best), 0, 40, 4, bits);
        }
        t1 = now_ns();
        for (int i = 0; i < it_unp; i++) {
                __asm__ volatile("" : "+m"(best), "+m"(bits));
                sink += (int64_t)ref_modem_sync_unpack(best, sizeof(best), 0, 40, 4, bits);
        }
        t2 = now_ns();
        double asm_unp = (double)(t1 - t0) / it_unp;
        double ref_unp = (double)(t2 - t1) / it_unp;

        const int it_memo = 50000;
        t0 = now_ns();
        for (int i = 0; i < it_memo; i++) {
                __asm__ volatile("" : "+m"(frames), "+m"(totals), "+m"(dst));
                sink += modem_sync_memo_has(frames, totals, 4, 276, dst, 276);
        }
        t1 = now_ns();
        for (int i = 0; i < it_memo; i++) {
                __asm__ volatile("" : "+m"(frames), "+m"(totals), "+m"(dst));
                sink += ref_modem_sync_memo_has(frames, totals, 4, 276, dst, 276);
        }
        t2 = now_ns();
        double asm_memo = (double)(t1 - t0) / it_memo;
        double ref_memo = (double)(t2 - t1) / it_memo;

        (void)sink;
        printf("BENCH modem_sync_find      (16384b): asm=%.1f ns/op  ref=%.1f ns/op  speedup=%.2fx\n",
               asm_find, ref_find, ref_find / asm_find);
        printf("BENCH modem_sync_read_bytes (276B) : asm=%.1f ns/op  ref=%.1f ns/op  speedup=%.2fx\n",
               asm_read, ref_read, ref_read / asm_read);
        printf("BENCH modem_sync_unpack    (1638s) : asm=%.1f ns/op  ref=%.1f ns/op  speedup=%.2fx\n",
               asm_unp, ref_unp, ref_unp / asm_unp);
        printf("BENCH modem_sync_memo_has  (4x276B): asm=%.1f ns/op  ref=%.1f ns/op  speedup=%.2fx\n",
               asm_memo, ref_memo, ref_memo / asm_memo);
}

int main(void)
{
        test_edge_cases();
        test_guard_pages();
        test_fuzz();
        if (failures) {
                fprintf(stderr, "FAILED: %d failures\n", failures);
                return 1;
        }
        printf("PASS: modem_sync (edge + guard-pages + 5000 fuzz iters)\n");
        bench();
        return 0;
}
