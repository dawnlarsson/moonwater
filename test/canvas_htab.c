/*
        Correctness, security (guard-page + OOB + non-pow2 + full-table), differential fuzz,
        and speed/size proof harness for src/canvas/canvas_htab.asm.c.
*/
#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>

#define CANVAS_HTAB_ASM_IMPLEMENTATION
#include "../src/canvas/canvas_htab.asm.c"

/* ---- -O2 C reference (matching original wayland_host.c + guards for apples-to-apples size/speed) ---- */

__attribute__((noinline))
chtab_u64 ref_htab_key(const struct chtab_table *records, chtab_i64 slot, chtab_i32 key64)
{
        if (!records || !records->at || slot < 0 || records->unit <= 0 || slot >= records->count)
                return 0;
        const void *p = records->at + (chtab_u64)slot * (chtab_u64)records->unit;
        return key64 ? *(const volatile chtab_u64 *)p : (chtab_u64)*(const chtab_u32 *)p;
}

__attribute__((noinline))
chtab_u64 ref_htab_hash(chtab_u64 key, chtab_i64 buckets, chtab_i32 key64)
{
        if (buckets <= 0 || (buckets & (buckets - 1)) != 0)
                return 0;
        if (key64)
                return ((key >> 12) * 0x9e3779b97f4a7c15ull >> 20) & (chtab_u64)(buckets - 1);
        return ((chtab_u64)(chtab_u32)key * 0x9e3779b97f4a7c15ull >> 24) & (chtab_u64)(buckets - 1);
}

__attribute__((noinline))
void ref_htab_put(struct chtab_table *index, chtab_u64 key, chtab_i64 slot, chtab_i32 key64)
{
        if (!index || !index->at || slot < 0 || index->count <= 0 || (index->count & (index->count - 1)) != 0)
                return;
        chtab_u32 *bucket = (chtab_u32 *)(void *)index->at;
        chtab_u64 mask = (chtab_u64)(index->count - 1);
        chtab_u64 b = ref_htab_hash(key, index->count, key64);
        for (chtab_i64 n = 0; n < index->count; n++, b = (b + 1) & mask)
        {
                if (!bucket[b])
                {
                        bucket[b] = (chtab_u32)(slot + 1);
                        return;
                }
        }
}

__attribute__((noinline))
chtab_i64 ref_htab_find(const struct chtab_table *index, const struct chtab_table *records,
                        chtab_u64 key, chtab_i32 first, chtab_u64 *at, chtab_i32 key64)
{
        if (!index || !records || !index->at || !records->at ||
            index->count <= 0 || (index->count & (index->count - 1)) != 0 ||
            records->unit <= 0 || records->count <= 0)
                return -1;
        chtab_i64 buckets = index->count, found = -1;
        const chtab_u32 *bucket = (const chtab_u32 *)(const void *)index->at;
        chtab_u64 mask = (chtab_u64)(buckets - 1);
        for (chtab_u64 b = ref_htab_hash(key, buckets, key64), n = 0; n < (chtab_u64)buckets;
             n++, b = (b + 1) & mask)
        {
                if (!bucket[b])
                        return found;
                chtab_i64 slot = (chtab_i64)bucket[b] - 1;
                if (slot < 0 || slot >= records->count)
                        continue;
                if (ref_htab_key(records, slot, key64) == key)
                {
                        if (!first)
                        {
                                if (at)
                                        *at = b;
                                return slot;
                        }
                        if (found < 0 || slot < found)
                                found = slot;
                }
        }
        return found;
}

__attribute__((noinline))
void ref_htab_del(struct chtab_table *index, const struct chtab_table *records,
                  chtab_u64 hole, chtab_i32 key64)
{
        if (!index || !records || !index->at || !records->at ||
            index->count <= 0 || (index->count & (index->count - 1)) != 0 ||
            hole >= (chtab_u64)index->count ||
            records->unit <= 0 || records->count <= 0)
                return;
        chtab_i64 buckets = index->count;
        chtab_u32 *bucket = (chtab_u32 *)(void *)index->at;
        chtab_u64 mask = (chtab_u64)(buckets - 1);

        bucket[hole] = 0;
        for (chtab_u64 b = (hole + 1) & mask, n = 1; n < (chtab_u64)buckets && bucket[b];
             n++, b = (b + 1) & mask)
        {
                chtab_i64 slot = (chtab_i64)bucket[b] - 1;
                if (slot < 0 || slot >= records->count)
                        continue;
                chtab_u64 home = ref_htab_hash(ref_htab_key(records, slot, key64), buckets, key64);
                if ((b > hole && (home <= hole || home > b)) || (b < hole && home <= hole && home > b))
                {
                        bucket[hole] = bucket[b];
                        bucket[b] = 0;
                        hole = b;
                }
        }
}

/* ---- Helpers ---- */

static int failures = 0;

#define CHECK(cond, fmt, ...) do { \
        if (!(cond)) { \
                fprintf(stderr, "FAIL %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
                failures++; \
        } \
} while (0)

static uint64_t rng_state = 0x9e3779b97f4a7c15ull;
static uint64_t rng_next(void)
{
        uint64_t x = rng_state;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        rng_state = x;
        return x * 0x2545f4914f6cdd1dull;
}

static uint64_t now_ns(void)
{
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void write_rec_key(chtab_u8 *buf, chtab_i64 unit, chtab_i64 slot, chtab_u64 key, int key64)
{
        if (key64)
                *(chtab_u64 *)(buf + (chtab_u64)slot * (chtab_u64)unit) = key;
        else
                *(chtab_u32 *)(buf + (chtab_u64)slot * (chtab_u64)unit) = (chtab_u32)key;
}

/* ---- 1. Exhaustive edge cases & security guards ---- */

static void test_guards_and_edges(void)
{
        /* NULL & invalid table checks */
        CHECK(chtab_key(NULL, 0, 0) == 0, "key NULL");
        CHECK(chtab_key(NULL, 0, 1) == 0, "key NULL 64");

        chtab_u64 recs64[4] = { 0, 1, 0x123456789abcdef0ull, UINT64_MAX };
        struct chtab_table rt = { .at = (chtab_u8 *)recs64, .unit = 8, .count = 4 };
        CHECK(chtab_key(&rt, -1, 1) == 0, "key slot=-1");
        CHECK(chtab_key(&rt, INT64_MIN, 1) == 0, "key slot=INT64_MIN");
        CHECK(chtab_key(&rt, 4, 1) == 0, "key slot==count");
        CHECK(chtab_key(&rt, 1000, 1) == 0, "key slot>count");
        CHECK(chtab_key(&rt, 0, 1) == 0, "key slot=0 val=0");
        CHECK(chtab_key(&rt, 3, 1) == UINT64_MAX, "key slot=3 val=UINT64_MAX");
        CHECK(chtab_key(&rt, 2, 0) == 0x9abcdef0u, "key slot=2 key64=0");

        struct chtab_table bad_rt = rt;
        bad_rt.at = NULL;
        CHECK(chtab_key(&bad_rt, 0, 1) == 0, "key at=NULL");
        bad_rt = rt; bad_rt.unit = 0;
        CHECK(chtab_key(&bad_rt, 0, 1) == 0, "key unit=0");
        bad_rt = rt; bad_rt.unit = -8;
        CHECK(chtab_key(&bad_rt, 0, 1) == 0, "key unit<0");

        /* Non-power-of-2 bucket guards */
        const chtab_i64 bad_buckets[] = { 0, -1, -64, INT64_MIN, 3, 5, 6, 7, 9, 10, 12, 15, 17, 63, 65, 100, 1023 };
        for (size_t i = 0; i < sizeof(bad_buckets) / sizeof(bad_buckets[0]); i++)
        {
                chtab_i64 b = bad_buckets[i];
                CHECK(chtab_hash(0x123456789abcdef0ull, b, 0) == 0, "hash non-pow2 %lld k32", (long long)b);
                CHECK(chtab_hash(0x123456789abcdef0ull, b, 1) == 0, "hash non-pow2 %lld k64", (long long)b);

                chtab_u32 dummy[16] = {0};
                struct chtab_table idx = { .at = (chtab_u8 *)dummy, .unit = 4, .count = b };
                chtab_put(&idx, 42, 0, 0);
                chtab_put(&idx, 42, 0, 1);
                CHECK(dummy[0] == 0, "put non-pow2 must be no-op");
                CHECK(chtab_find(&idx, &rt, 1, 0, NULL, 1) == -1, "find non-pow2");
                chtab_del(&idx, &rt, 0, 1);
        }

        /* Hash parity across key extremes & all powers of 2 */
        const chtab_u64 test_keys[] = {
                0, 1, 2, 0xfff, 0x1000, 0x7fffffffu, 0x80000000u, 0xffffffffu,
                0x100000000ull, 0xdeadbeef00000000ull, 0x00007fff12345000ull,
                UINT64_MAX - 1, UINT64_MAX
        };
        for (int p = 0; p <= 20; p++)
        {
                chtab_i64 b = 1ll << p;
                for (size_t k = 0; k < sizeof(test_keys) / sizeof(test_keys[0]); k++)
                {
                        for (int k64 = 0; k64 <= 1; k64++)
                        {
                                chtab_u64 got = chtab_hash(test_keys[k], b, k64);
                                chtab_u64 exp = ref_htab_hash(test_keys[k], b, k64);
                                CHECK(got == exp, "hash mismatch b=%lld k=%llx k64=%d got=%llu exp=%llu",
                                      (long long)b, (unsigned long long)test_keys[k], k64,
                                      (unsigned long long)got, (unsigned long long)exp);
                        }
                }
        }

        /* NULL / empty / OOB / full table safety */
        chtab_put(NULL, 1, 0, 0);
        CHECK(chtab_find(NULL, &rt, 1, 0, NULL, 1) == -1, "find NULL idx");
        CHECK(chtab_find(&rt, NULL, 1, 0, NULL, 1) == -1, "find NULL rec");
        chtab_del(NULL, &rt, 0, 1);
        chtab_del(&rt, NULL, 0, 1);

        /* Full table: buckets=4, fill all 4 slots, verify put/find/del never hang */
        chtab_u32 b4[4] = {0};
        struct chtab_table idx4 = { .at = (chtab_u8 *)b4, .unit = 4, .count = 4 };
        for (int i = 0; i < 4; i++)
                chtab_put(&idx4, recs64[i], i, 1);
        /* 5th put into full table must be a safe no-op */
        chtab_u32 snap[4];
        memcpy(snap, b4, sizeof snap);
        chtab_put(&idx4, 0x99990000ull, 0, 1);
        CHECK(memcmp(snap, b4, sizeof snap) == 0, "put on full table must not corrupt");
        /* Find missing key on full table must return -1 and not touch *at */
        chtab_u64 at_val = 0x55aa55aaull;
        CHECK(chtab_find(&idx4, &rt, 0x55550000ull, 0, &at_val, 1) == -1, "find miss on full table");
        CHECK(at_val == 0x55aa55aaull, "find miss must not modify *at");
        /* Find existing key=0 and key=UINT64_MAX */
        CHECK(chtab_find(&idx4, &rt, 0, 0, &at_val, 1) == 0, "find key=0");
        CHECK(chtab_find(&idx4, &rt, UINT64_MAX, 0, &at_val, 1) == 3, "find key=UINT64_MAX");
        /* OOB hole in del must be safe no-op */
        chtab_del(&idx4, &rt, 4, 1);
        chtab_del(&idx4, &rt, UINT64_MAX, 1);
        CHECK(memcmp(snap, b4, sizeof snap) == 0, "del OOB hole must be no-op");
        /* Delete from full table */
        chtab_del(&idx4, &rt, at_val, 1);
        CHECK(chtab_find(&idx4, &rt, UINT64_MAX, 0, NULL, 1) == -1, "del from full table");

        /* buckets == 1 minimal table */
        chtab_u32 b1[1] = {0};
        struct chtab_table idx1 = { .at = (chtab_u8 *)b1, .unit = 4, .count = 1 };
        chtab_put(&idx1, recs64[2], 2, 1);
        CHECK(b1[0] == 3, "b1 put");
        at_val = 99;
        CHECK(chtab_find(&idx1, &rt, recs64[2], 0, &at_val, 1) == 2 && at_val == 0, "b1 find");
        chtab_del(&idx1, &rt, 0, 1);
        CHECK(b1[0] == 0, "b1 del");

        /* Corrupted bucket pointing to slot >= records->count must NOT read OOB */
        b4[0] = 999999u;
        b4[1] = 0;
        b4[2] = 0;
        b4[3] = 0;
        CHECK(chtab_find(&idx4, &rt, 1, 0, NULL, 1) == -1, "corrupted slot find");
        chtab_del(&idx4, &rt, 3, 1); /* wraps to b=0 with corrupted slot */

        /* 32-bit mode with upper 32 bits set in query key must NOT match */
        memset(b4, 0, sizeof b4);
        chtab_u32 recs32[4] = { 0x11111111u, 0x22222222u, 0xffffffffu, 0x00000001u };
        struct chtab_table rt32 = { .at = (chtab_u8 *)recs32, .unit = 4, .count = 4 };
        chtab_put(&idx4, recs32[0], 0, 0);
        CHECK(chtab_find(&idx4, &rt32, 0x11111111u, 0, NULL, 0) == 0, "k32 exact hit");
        CHECK(chtab_find(&idx4, &rt32, 0x0000000111111111ull, 0, NULL, 0) == -1, "k32 upper bits must not match");
}

/* ---- 2. Heavy collision chains + wrap-around backward-shift deletes + first=1 ---- */

static void test_collisions_and_wraparound(void)
{
        for (int key64 = 0; key64 <= 1; key64++)
        {
                const int B = 16;
                chtab_u32 b_asm[16] = {0}, b_ref[16] = {0};
                chtab_u64 rbuf[32] = {0};
                struct chtab_table ia = { .at = (chtab_u8 *)b_asm, .unit = 8, .count = B };
                struct chtab_table ir = { .at = (chtab_u8 *)b_ref, .unit = 8, .count = B };
                struct chtab_table rt = { .at = (chtab_u8 *)rbuf, .unit = 8, .count = 32 };
                ia.unit = 4; ir.unit = 4;

                /* Find 12 keys that all hash to bucket B-2 (14) so chain wraps around 14->15->0->1->...->9 */
                int found = 0;
                for (chtab_u64 cand = 1; found < 12; cand++)
                {
                        chtab_u64 k = key64 ? (cand << 12) : cand;
                        if (chtab_hash(k, B, key64) == (chtab_u64)(B - 2))
                        {
                                write_rec_key((chtab_u8 *)rbuf, 8, found, k, key64);
                                chtab_put(&ia, k, found, key64);
                                ref_htab_put(&ir, k, found, key64);
                                found++;
                        }
                }
                /* Also add 2 duplicate keys at slots 12 and 13 with same key as slot 5, plus 2 keys at bucket 15 and 0 */
                chtab_u64 dupk = chtab_key(&rt, 5, key64);
                write_rec_key((chtab_u8 *)rbuf, 8, 12, dupk, key64);
                chtab_put(&ia, dupk, 12, key64);
                ref_htab_put(&ir, dupk, 12, key64);

                CHECK(memcmp(b_asm, b_ref, sizeof b_asm) == 0, "collision chain put parity k64=%d", key64);

                /* Check first=1 returns lowest slot (5, not 12) and doesn't touch *at */
                chtab_u64 at_a = 777, at_r = 777;
                chtab_i64 fa = chtab_find(&ia, &rt, dupk, 1, &at_a, key64);
                chtab_i64 fr = ref_htab_find(&ir, &rt, dupk, 1, &at_r, key64);
                CHECK(fa == fr && fa == 5 && at_a == 777 && at_r == 777, "first=1 duplicate k64=%d", key64);

                /* Delete from middle, head (bucket 14), wrap boundary (bucket 15 & 0), and verify bit-exact parity */
                const chtab_u64 del_holes[] = { 14, 15, 0, 2, 1, 14, 15, 0, 14, 14, 14, 14, 14 };
                for (size_t d = 0; d < sizeof(del_holes) / sizeof(del_holes[0]); d++)
                {
                        chtab_u64 h = del_holes[d];
                        if (!b_asm[h])
                                continue;
                        chtab_i64 slot = (chtab_i64)b_asm[h] - 1;
                        chtab_del(&ia, &rt, h, key64);
                        ref_htab_del(&ir, &rt, h, key64);
                        write_rec_key((chtab_u8 *)rbuf, 8, slot, 0, key64);
                        CHECK(memcmp(b_asm, b_ref, sizeof b_asm) == 0,
                              "wraparound del parity step=%zu hole=%llu k64=%d",
                              d, (unsigned long long)h, key64);
                }
        }
}

/* ---- 3. Randomized differential fuzzing vs -O2 C reference ---- */

static void test_differential_fuzz(void)
{
        const int bucket_sizes[] = { 1, 2, 4, 8, 16, 64, 256 };
        const int unit_sizes[] = { 8, 16, 24, 48, 72 };

        for (int key64 = 0; key64 <= 1; key64++)
        {
                for (size_t bi = 0; bi < sizeof(bucket_sizes) / sizeof(bucket_sizes[0]); bi++)
                {
                        int B = bucket_sizes[bi];
                        int U = unit_sizes[(bi + (size_t)key64) % (sizeof(unit_sizes) / sizeof(unit_sizes[0]))];
                        int R = B * 2;
                        chtab_u32 *ba = calloc((size_t)B, sizeof(chtab_u32));
                        chtab_u32 *br = calloc((size_t)B, sizeof(chtab_u32));
                        chtab_u8  *rb = calloc((size_t)R, (size_t)U);
                        uint8_t   *live = calloc((size_t)R, 1);

                        struct chtab_table ia = { .at = (chtab_u8 *)ba, .unit = 4, .count = B };
                        struct chtab_table ir = { .at = (chtab_u8 *)br, .unit = 4, .count = B };
                        struct chtab_table rt = { .at = rb, .unit = U, .count = R };

                        int count_live = 0;
                        int ops = (B <= 16) ? 3000 : 1500;
                        for (int op = 0; op < ops; op++)
                        {
                                uint64_t r = rng_next();
                                int action = (int)(r % 10);

                                if (action < 4 && count_live < B)
                                {
                                        /* Insert */
                                        int slot = (int)((r >> 8) % (uint64_t)R);
                                        if (live[slot])
                                                continue;
                                        chtab_u64 k = (r >> 16) | 1ull;
                                        if (!key64)
                                                k = (chtab_u32)k | 1u;
                                        /* Occasionally reuse an existing key to test duplicates */
                                        if ((r & 0x70) == 0 && count_live > 0)
                                        {
                                                for (int s = 0; s < R; s++)
                                                        if (live[s]) { k = chtab_key(&rt, s, key64); break; }
                                        }
                                        write_rec_key(rb, U, slot, k, key64);
                                        live[slot] = 1;
                                        count_live++;
                                        chtab_put(&ia, k, slot, key64);
                                        ref_htab_put(&ir, k, slot, key64);
                                        CHECK(memcmp(ba, br, (size_t)B * sizeof(chtab_u32)) == 0,
                                              "fuzz put mismatch B=%d U=%d k64=%d op=%d", B, U, key64, op);
                                }
                                else if (action < 7)
                                {
                                        /* Find (hit or miss, first=0 or first=1) */
                                        int first = (int)((r >> 4) & 1);
                                        chtab_u64 k;
                                        if ((r & 0x20) && count_live > 0)
                                        {
                                                int s = (int)((r >> 8) % (uint64_t)R);
                                                while (!live[s]) s = (s + 1) % R;
                                                k = chtab_key(&rt, s, key64);
                                        }
                                        else
                                        {
                                                k = key64 ? (r >> 16) : (chtab_u32)(r >> 16);
                                        }
                                        chtab_u64 aa = 0x12345678ull, ar = 0x12345678ull;
                                        chtab_i64 fa = chtab_find(&ia, &rt, k, first, (r & 0x40) ? &aa : NULL, key64);
                                        chtab_i64 fr = ref_htab_find(&ir, &rt, k, first, (r & 0x40) ? &ar : NULL, key64);
                                        CHECK(fa == fr && aa == ar,
                                              "fuzz find mismatch B=%d k64=%d first=%d fa=%lld fr=%lld aa=%llu ar=%llu",
                                              B, key64, first, (long long)fa, (long long)fr,
                                              (unsigned long long)aa, (unsigned long long)ar);
                                }
                                else if (count_live > 0)
                                {
                                        /* Delete a random live key */
                                        int s = (int)((r >> 8) % (uint64_t)R);
                                        while (!live[s]) s = (s + 1) % R;
                                        chtab_u64 k = chtab_key(&rt, s, key64);
                                        chtab_u64 ha = 0, hr = 0;
                                        chtab_i64 fa = chtab_find(&ia, &rt, k, 0, &ha, key64);
                                        chtab_i64 fr = ref_htab_find(&ir, &rt, k, 0, &hr, key64);
                                        CHECK(fa == fr && ha == hr && fa >= 0, "fuzz pre-del find");
                                        if (fa >= 0)
                                        {
                                                chtab_del(&ia, &rt, ha, key64);
                                                ref_htab_del(&ir, &rt, hr, key64);
                                                write_rec_key(rb, U, fa, 0, key64);
                                                live[fa] = 0;
                                                count_live--;
                                                CHECK(memcmp(ba, br, (size_t)B * sizeof(chtab_u32)) == 0,
                                                      "fuzz del mismatch B=%d U=%d k64=%d op=%d", B, U, key64, op);
                                        }
                                }
                        }

                        free(ba); free(br); free(rb); free(live);
                }
        }
}

/* ---- 4. mmap guard-page tests (PROT_NONE before AND after bucket & record arrays) ---- */

static void *alloc_guarded(size_t bytes, int align_end, void **base_out, size_t *total_out)
{
        long pg = sysconf(_SC_PAGESIZE);
        if (pg <= 0) pg = 4096;
        size_t data_pages = (bytes + (size_t)pg - 1) / (size_t)pg;
        size_t total = (data_pages + 2) * (size_t)pg;
        uint8_t *base = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (base == MAP_FAILED)
        {
                perror("mmap");
                exit(2);
        }
        if (mprotect(base, (size_t)pg, PROT_NONE) != 0 ||
            mprotect(base + (data_pages + 1) * (size_t)pg, (size_t)pg, PROT_NONE) != 0)
        {
                perror("mprotect");
                exit(2);
        }
        *base_out = base;
        *total_out = total;
        if (align_end)
                return base + (data_pages + 1) * (size_t)pg - bytes;
        return base + (size_t)pg;
}

static void test_guard_pages(void)
{
        for (int align_end = 0; align_end <= 1; align_end++)
        {
                for (int key64 = 0; key64 <= 1; key64++)
                {
                        const int B = 64;
                        const int R = 64;
                        const int U = key64 ? 24 : 16;
                        void *b_base = NULL, *r_base = NULL;
                        size_t b_tot = 0, r_tot = 0;

                        chtab_u32 *buckets = alloc_guarded((size_t)B * sizeof(chtab_u32), align_end, &b_base, &b_tot);
                        chtab_u8  *records = alloc_guarded((size_t)R * (size_t)U, align_end, &r_base, &r_tot);
                        memset(buckets, 0, (size_t)B * sizeof(chtab_u32));
                        memset(records, 0, (size_t)R * (size_t)U);

                        struct chtab_table idx = { .at = (chtab_u8 *)buckets, .unit = 4, .count = B };
                        struct chtab_table rec = { .at = records, .unit = U, .count = R };

                        /* Fill all 64 slots flush against guard pages */
                        for (int i = 0; i < R; i++)
                        {
                                chtab_u64 k = key64 ? ((chtab_u64)(i + 1) << 12) | 0xabcull : (chtab_u64)(i + 100);
                                write_rec_key(records, U, i, k, key64);
                                chtab_put(&idx, k, i, key64);
                        }
                        /* Boundary key reads */
                        CHECK(chtab_key(&rec, -1, key64) == 0, "guard key -1");
                        CHECK(chtab_key(&rec, R, key64) == 0, "guard key R");
                        CHECK(chtab_key(&rec, 0, key64) != 0, "guard key 0");
                        CHECK(chtab_key(&rec, R - 1, key64) != 0, "guard key R-1");

                        /* Full-table put & miss find right against guard pages */
                        chtab_put(&idx, 0xdeadbeef0000ull, 0, key64);
                        CHECK(chtab_find(&idx, &rec, 0xdeadbeefull, 0, NULL, key64) == -1, "guard full miss");
                        CHECK(chtab_find(&idx, &rec, 0xdeadbeefull, 1, NULL, key64) == -1, "guard full miss first=1");

                        /* Delete every item one by one (triggers wrap-around across bucket 0 and B-1!) */
                        for (int i = 0; i < R; i++)
                        {
                                chtab_u64 k = chtab_key(&rec, i, key64);
                                chtab_u64 hole = 0;
                                chtab_i64 s = chtab_find(&idx, &rec, k, 0, &hole, key64);
                                CHECK(s == i, "guard find %d", i);
                                chtab_del(&idx, &rec, hole, key64);
                                write_rec_key(records, U, i, 0, key64);
                        }
                        /* OOB hole delete right next to guard page */
                        chtab_del(&idx, &rec, (chtab_u64)B, key64);
                        chtab_del(&idx, &rec, UINT64_MAX, key64);

                        /* Corrupted slot = R (just past end into guard page) must NOT fault! */
                        buckets[0] = (chtab_u32)(R + 1);
                        buckets[B - 1] = (chtab_u32)(R + 2);
                        CHECK(chtab_find(&idx, &rec, 12345, 0, NULL, key64) == -1, "guard corrupted slot find");
                        chtab_del(&idx, &rec, (chtab_u64)(B - 1), key64);

                        munmap(b_base, b_tot);
                        munmap(r_base, r_tot);
                }
        }
}

/* ---- 5. Microbenchmark: ASM vs -O2 C reference ---- */

static void run_bench(void)
{
        const int B = 2048;
        const int N = 1000;
        const int ITERS = 400000;

        chtab_u32 *b_asm = calloc((size_t)B, sizeof(chtab_u32));
        chtab_u32 *b_ref = calloc((size_t)B, sizeof(chtab_u32));
        chtab_u64 *r64   = calloc((size_t)N, 3 * sizeof(chtab_u64));
        chtab_u32 *r32   = calloc((size_t)N, 4 * sizeof(chtab_u32));

        struct chtab_table ia64 = { .at = (chtab_u8 *)b_asm, .unit = 4, .count = B };
        struct chtab_table ir64 = { .at = (chtab_u8 *)b_ref, .unit = 4, .count = B };
        struct chtab_table rt64 = { .at = (chtab_u8 *)r64, .unit = 24, .count = N };
        struct chtab_table rt32 = { .at = (chtab_u8 *)r32, .unit = 16, .count = N };

        for (int i = 0; i < N; i++)
        {
                chtab_u64 k64 = ((chtab_u64)(i + 1) * 0x100000ull) ^ 0x7f0000000000ull;
                write_rec_key((chtab_u8 *)r64, 24, i, k64, 1);
                chtab_put(&ia64, k64, i, 1);
                ref_htab_put(&ir64, k64, i, 1);
        }

        volatile chtab_i64 sink = 0;
        uint64_t t0, t1, ta, tr;

        /* 64-bit find hit */
        t0 = now_ns();
        for (int i = 0; i < ITERS; i++)
                sink += chtab_find(&ia64, &rt64, r64[(i % N) * 3], 0, NULL, 1);
        t1 = now_ns(); ta = t1 - t0;

        t0 = now_ns();
        for (int i = 0; i < ITERS; i++)
                sink += ref_htab_find(&ir64, &rt64, r64[(i % N) * 3], 0, NULL, 1);
        t1 = now_ns(); tr = t1 - t0;
        printf("BENCH find64_hit : asm=%.2f ns/op  ref=%.2f ns/op  speedup=%.2fx\n",
               (double)ta / ITERS, (double)tr / ITERS, (double)tr / (double)ta);

        /* 64-bit find miss */
        t0 = now_ns();
        for (int i = 0; i < ITERS; i++)
                sink += chtab_find(&ia64, &rt64, ((chtab_u64)(i + 1) << 12) | 7ull, 0, NULL, 1);
        t1 = now_ns(); ta = t1 - t0;

        t0 = now_ns();
        for (int i = 0; i < ITERS; i++)
                sink += ref_htab_find(&ir64, &rt64, ((chtab_u64)(i + 1) << 12) | 7ull, 0, NULL, 1);
        t1 = now_ns(); tr = t1 - t0;
        printf("BENCH find64_miss: asm=%.2f ns/op  ref=%.2f ns/op  speedup=%.2fx\n",
               (double)ta / ITERS, (double)tr / ITERS, (double)tr / (double)ta);

        /* 64-bit del+put churn */
        const int CHURN = 200000;
        t0 = now_ns();
        for (int i = 0; i < CHURN; i++)
        {
                int idx = i % N;
                chtab_u64 k = r64[idx * 3], hole = 0;
                chtab_i64 s = chtab_find(&ia64, &rt64, k, 0, &hole, 1);
                chtab_del(&ia64, &rt64, hole, 1);
                chtab_put(&ia64, k, s, 1);
        }
        t1 = now_ns(); ta = t1 - t0;

        t0 = now_ns();
        for (int i = 0; i < CHURN; i++)
        {
                int idx = i % N;
                chtab_u64 k = r64[idx * 3], hole = 0;
                chtab_i64 s = ref_htab_find(&ir64, &rt64, k, 0, &hole, 1);
                ref_htab_del(&ir64, &rt64, hole, 1);
                ref_htab_put(&ir64, k, s, 1);
        }
        t1 = now_ns(); tr = t1 - t0;
        printf("BENCH churn64    : asm=%.2f ns/op  ref=%.2f ns/op  speedup=%.2fx\n",
               (double)ta / CHURN, (double)tr / CHURN, (double)tr / (double)ta);

        /* 32-bit find hit */
        memset(b_asm, 0, (size_t)B * sizeof(chtab_u32));
        memset(b_ref, 0, (size_t)B * sizeof(chtab_u32));
        for (int i = 0; i < N; i++)
        {
                chtab_u32 k32 = (chtab_u32)(i * 2654435761u + 1u);
                write_rec_key((chtab_u8 *)r32, 16, i, k32, 0);
                chtab_put(&ia64, k32, i, 0);
                ref_htab_put(&ir64, k32, i, 0);
        }

        t0 = now_ns();
        for (int i = 0; i < ITERS; i++)
                sink += chtab_find(&ia64, &rt32, r32[(i % N) * 4], 0, NULL, 0);
        t1 = now_ns(); ta = t1 - t0;

        t0 = now_ns();
        for (int i = 0; i < ITERS; i++)
                sink += ref_htab_find(&ir64, &rt32, r32[(i % N) * 4], 0, NULL, 0);
        t1 = now_ns(); tr = t1 - t0;
        printf("BENCH find32_hit : asm=%.2f ns/op  ref=%.2f ns/op  speedup=%.2fx\n",
               (double)ta / ITERS, (double)tr / ITERS, (double)tr / (double)ta);

        (void)sink;
        free(b_asm); free(b_ref); free(r64); free(r32);
}

int main(void)
{
        test_guards_and_edges();
        test_collisions_and_wraparound();
        test_differential_fuzz();
        test_guard_pages();
        if (failures)
        {
                fprintf(stderr, "FAILED: %d errors\n", failures);
                return 1;
        }
        printf("PASS: all correctness, security, fuzz, and guard-page tests passed!\n");
        run_bench();
        return 0;
}
