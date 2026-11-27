/*
        Differential C-reference parity, exhaustive edge-case, fuzz, mmap
        guard-page security & throughput proof for src/sh/shell_sort.asm.c
        (arm64, x86-64, riscv64 pure-ASM).

        Build & run:
          cc -O2 -Wall -Werror test/shell_sort.c -o /tmp/t_sort && /tmp/t_sort
          cc -O2 -fsanitize=address,undefined -Wall -Werror test/shell_sort.c -o /tmp/t_sort_asan && /tmp/t_sort_asan
*/
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>

#define SH_SHELL_SORT_ASM_IMPLEMENTATION
#include "../src/sh/shell_sort.asm.c"

/* ── Reference -O2 C implementations (non-inlined for nm --print-size proof) ── */

static inline int ref_is_blank(sh_sort_u8 c)
{
        return c == ' ' || c == '\t' || c == '\n';
}

static inline int ref_is_kept(sh_sort_u8 c, sh_sort_u64 how)
{
        int keep = 1;
        if (how & SH_SORT_DICTIONARY) {
                int alnum = (c >= '0' && c <= '9') ||
                            (c >= 'A' && c <= 'Z') ||
                            (c >= 'a' && c <= 'z');
                keep = ref_is_blank(c) || alnum;
        }
        if ((how & SH_SORT_PRINTABLE) && (c < 0x20 || c >= 0x7f))
                keep = 0;
        return keep;
}

static inline sh_sort_u8 ref_to_upper(sh_sort_u8 c)
{
        return (c >= 'a' && c <= 'z') ? (sh_sort_u8)(c - 32) : c;
}

__attribute__((noinline))
sh_sort_u64 ref_sh_sort_number_window(const sh_sort_u8 *text, sh_sort_u64 length, sh_sort_u8 *exact)
{
        if (!text || !length) {
                if (exact)
                        *exact = 1;
                return (sh_sort_u64)0x80 << 56;
        }
        sh_sort_u64 at = 0;
        while (at < length && ref_is_blank(text[at]))
                at++;
        int minus = (at < length && text[at] == '-');
        at += (sh_sort_u64)minus;
        sh_sort_u64 first = at;
        while (at < length && (sh_sort_u8)(text[at] - '0') < 10)
                at++;
        while (first < at && text[first] == '0')
                first++;
        sh_sort_u64 digits = at - first;
        sh_sort_u64 fraction = at;
        if (at < length && text[at] == '.') {
                fraction = ++at;
                while (at < length && (sh_sort_u8)(text[at] - '0') < 10)
                        at++;
                while (at > fraction && text[at - 1] == '0')
                        at--;
        }
        sh_sort_u64 places = at - fraction;
        if (!digits && !places) {
                if (exact)
                        *exact = 1;
                return (sh_sort_u64)0x80 << 56;
        }
        sh_sort_u64 rank = digits + 1;
        sh_sort_u64 body = 0;
        sh_sort_u64 taken = 0;
        if (rank < 127) {
                for (sh_sort_u64 i = 0; i < digits && taken < 7; i++, taken++)
                        body = (body << 8) | text[first + i];
                for (sh_sort_u64 i = 0; i < places && taken < 7; i++, taken++)
                        body = (body << 8) | text[fraction + i];
                body <<= 8 * (7 - taken);
        } else {
                rank = 127;
        }
        if (exact)
                *exact = (sh_sort_u8)(rank < 127 && digits + places <= 7);
        if (minus)
                return ((sh_sort_u64)(0x80 - rank) << 56) | (~body & 0x00ffffffffffffffull);
        return ((sh_sort_u64)(0x80 + rank) << 56) | body;
}

__attribute__((noinline))
sh_sort_i64 ref_sh_sort_human_order(const sh_sort_u8 *at, sh_sort_u64 length)
{
        if (!at || !length)
                return 0;
        sh_sort_u64 scan = 0;
        sh_sort_i64 sign = 1;
        int nonzero = 0;
        while (scan < length && ref_is_blank(at[scan]))
                scan++;
        if (scan < length && at[scan] == '-') {
                sign = -1;
                scan++;
        }
        while (scan < length && (sh_sort_u8)(at[scan] - '0') < 10) {
                if (at[scan] != '0')
                        nonzero = 1;
                scan++;
        }
        if (scan < length && at[scan] == '.') {
                scan++;
                while (scan < length && (sh_sort_u8)(at[scan] - '0') < 10) {
                        if (at[scan] != '0')
                                nonzero = 1;
                        scan++;
                }
        }
        if (!nonzero || scan >= length)
                return 0;
        switch (at[scan]) {
        case 'k':
        case 'K': return sign * 1;
        case 'M': return sign * 2;
        case 'G': return sign * 3;
        case 'T': return sign * 4;
        case 'P': return sign * 5;
        case 'E': return sign * 6;
        case 'Z': return sign * 7;
        case 'Y': return sign * 8;
        case 'R': return sign * 9;
        case 'Q': return sign * 10;
        default:  return 0;
        }
}

__attribute__((noinline))
void ref_sh_sort_number_parse(const sh_sort_u8 *text, sh_sort_u64 length, struct sh_sort_number *out)
{
        if (!out)
                return;
        if (!text || !length) {
                out->integer.packed = 0;
                out->fraction = text;
                out->rank = 0;
                out->places = 0;
                return;
        }
        sh_sort_u64 at = 0;
        while (at < length && ref_is_blank(text[at]))
                at++;
        int minus = (at < length && text[at] == '-');
        at += (sh_sort_u64)minus;
        sh_sort_u64 first = at;
        while (at < length && (sh_sort_u8)(text[at] - '0') < 10)
                at++;
        while (first < at && text[first] == '0')
                first++;
        sh_sort_u64 digits = at - first;
        sh_sort_u64 fraction = at;
        if (at < length && text[at] == '.') {
                fraction = ++at;
                while (at < length && (sh_sort_u8)(text[at] - '0') < 10)
                        at++;
                while (at > fraction && text[at - 1] == '0')
                        at--;
        }
        sh_sort_u64 places = at - fraction;
        sh_sort_i64 rank = (digits || places) ? (sh_sort_i64)digits + 1 : 0;
        out->integer.at = text + first;
        out->fraction = text + fraction;
        out->rank = minus ? -rank : rank;
        out->places = places;
        if (digits <= sizeof(sh_sort_u64)) {
                sh_sort_u64 packed = 0;
                for (sh_sort_u64 i = 0; i < digits; i++)
                        packed = (packed << 8) | text[first + i];
                out->integer.packed = packed;
        }
}

__attribute__((noinline))
sh_sort_i64 ref_sh_sort_compare_parsed(const struct sh_sort_number *a, const struct sh_sort_number *b)
{
        if (a == b)
                return 0;
        if (!a)
                return -1;
        if (!b)
                return 1;
        if (a->rank != b->rank)
                return a->rank < b->rank ? -1 : 1;
        if (!a->rank)
                return 0;
        sh_sort_i64 sign = a->rank < 0 ? -1 : 1;
        sh_sort_u64 digits = (sh_sort_u64)(a->rank * sign - 1);
        sh_sort_i64 answer = 0;
        if (digits <= sizeof(sh_sort_u64)) {
                answer = (a->integer.packed > b->integer.packed) -
                         (a->integer.packed < b->integer.packed);
        } else {
                int c = memcmp(a->integer.at, b->integer.at, (size_t)digits);
                answer = (c > 0) - (c < 0);
        }
        if (!answer) {
                sh_sort_u64 places = a->places < b->places ? a->places : b->places;
                if (places) {
                        int c = memcmp(a->fraction, b->fraction, (size_t)places);
                        answer = (c > 0) - (c < 0);
                }
                if (!answer && a->places != b->places)
                        answer = a->places < b->places ? -1 : 1;
        }
        return sign * answer;
}

__attribute__((noinline))
sh_sort_u64 ref_sh_sort_translate(sh_sort_u8 *into, const sh_sort_u8 *from, sh_sort_u64 length, sh_sort_u64 how)
{
        if (!into || !from || !length)
                return 0;
        sh_sort_u64 used = 0;
        for (sh_sort_u64 at = 0; at < length; at++) {
                sh_sort_u8 byte = from[at];
                if ((how & (SH_SORT_DICTIONARY | SH_SORT_PRINTABLE)) && !ref_is_kept(byte, how))
                        continue;
                into[used++] = (how & SH_SORT_FOLD) ? ref_to_upper(byte) : byte;
        }
        return used;
}

__attribute__((noinline))
sh_sort_i64 ref_sh_sort_compare_bytes(const sh_sort_u8 *a, sh_sort_u64 la,
                                      const sh_sort_u8 *b, sh_sort_u64 lb, sh_sort_u64 how)
{
        if (!a) la = 0;
        if (!b) lb = 0;
        how &= 7;
        if (!how) {
                sh_sort_u64 n = la < lb ? la : lb;
                if (n) {
                        int c = memcmp(a, b, (size_t)n);
                        if (c) return c < 0 ? -1 : 1;
                }
                return la == lb ? 0 : la < lb ? -1 : 1;
        }
        if (how == SH_SORT_FOLD) {
                sh_sort_u64 n = la < lb ? la : lb;
                for (sh_sort_u64 i = 0; i < n; i++) {
                        sh_sort_u8 one = ref_to_upper(a[i]);
                        sh_sort_u8 two = ref_to_upper(b[i]);
                        if (one != two)
                                return one < two ? -1 : 1;
                }
                return la == lb ? 0 : la < lb ? -1 : 1;
        }
        sh_sort_u64 i = 0, j = 0;
        for (;;) {
                while (i < la && !ref_is_kept(a[i], how))
                        i++;
                while (j < lb && !ref_is_kept(b[j], how))
                        j++;
                if (i == la || j == lb)
                        break;
                sh_sort_u8 one = a[i++];
                sh_sort_u8 two = b[j++];
                if (how & SH_SORT_FOLD) {
                        one = ref_to_upper(one);
                        two = ref_to_upper(two);
                }
                if (one != two)
                        return one < two ? -1 : 1;
        }
        if (i == la && j == lb)
                return 0;
        return i == la ? -1 : 1;
}

/* ── Helpers ── */

static uint64_t rng_state = 0x9e3779b97f4a7c15ULL;
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
        return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static int failures = 0;

static void check_num(const char *tag, const sh_sort_u8 *s, size_t len)
{
        sh_sort_u8 ea = 0xaa, er = 0x55;
        sh_sort_u64 wa = sh_sort_number_window(s, len, &ea);
        sh_sort_u64 wr = ref_sh_sort_number_window(s, len, &er);
        if (wa != wr || ea != er) {
                fprintf(stderr, "FAIL number_window [%s] len=%zu: asm=(0x%016llx,%u) ref=(0x%016llx,%u)\n",
                        tag, len, (unsigned long long)wa, ea, (unsigned long long)wr, er);
                failures++;
        }

        sh_sort_i64 ha = sh_sort_human_order(s, len);
        sh_sort_i64 hr = ref_sh_sort_human_order(s, len);
        if (ha != hr) {
                fprintf(stderr, "FAIL human_order [%s] len=%zu: asm=%lld ref=%lld\n",
                        tag, len, (long long)ha, (long long)hr);
                failures++;
        }

        struct sh_sort_number na, nr;
        memset(&na, 0xaa, sizeof(na));
        memset(&nr, 0x55, sizeof(nr));
        sh_sort_number_parse(s, len, &na);
        ref_sh_sort_number_parse(s, len, &nr);
        if (na.rank != nr.rank || na.places != nr.places || na.integer.packed != nr.integer.packed ||
            (na.places > 0 && na.fraction != nr.fraction)) {
                fprintf(stderr, "FAIL number_parse [%s] len=%zu: asm=(%llx,%lld,%llu) ref=(%llx,%lld,%llu)\n",
                        tag, len,
                        (unsigned long long)na.integer.packed, (long long)na.rank, (unsigned long long)na.places,
                        (unsigned long long)nr.integer.packed, (long long)nr.rank, (unsigned long long)nr.places);
                failures++;
        }
}

static void check_pair(const char *tag, const sh_sort_u8 *a, size_t la, const sh_sort_u8 *b, size_t lb)
{
        struct sh_sort_number na, nb;
        sh_sort_number_parse(a, la, &na);
        sh_sort_number_parse(b, lb, &nb);
        sh_sort_i64 ca = sh_sort_compare_parsed(&na, &nb);
        sh_sort_i64 cr = ref_sh_sort_compare_parsed(&na, &nb);
        if (ca != cr) {
                fprintf(stderr, "FAIL compare_parsed [%s]: asm=%lld ref=%lld\n",
                        tag, (long long)ca, (long long)cr);
                failures++;
        }

        /* Window ordering must agree with compare_parsed */
        sh_sort_u8 ea = 0, eb = 0;
        sh_sort_u64 wa = sh_sort_number_window(a, la, &ea);
        sh_sort_u64 wb = sh_sort_number_window(b, lb, &eb);
        if (wa != wb) {
                sh_sort_i64 wcmp = (wa < wb) ? -1 : 1;
                if (wcmp != cr) {
                        fprintf(stderr, "FAIL window_order [%s]: wa=0x%016llx wb=0x%016llx cmp=%lld\n",
                                tag, (unsigned long long)wa, (unsigned long long)wb, (long long)cr);
                        failures++;
                }
        } else if (ea && eb && cr != 0) {
                fprintf(stderr, "FAIL window_exact [%s]: equal exact windows but cmp=%lld\n",
                        tag, (long long)cr);
                failures++;
        }

        for (sh_sort_u64 how = 0; how < 8; how++) {
                sh_sort_i64 ba = sh_sort_compare_bytes(a, la, b, lb, how);
                sh_sort_i64 br = ref_sh_sort_compare_bytes(a, la, b, lb, how);
                if (ba != br) {
                        fprintf(stderr, "FAIL compare_bytes [%s] how=%llu: asm=%lld ref=%lld\n",
                                tag, (unsigned long long)how, (long long)ba, (long long)br);
                        failures++;
                }
        }
}

static void check_translate(const char *tag, const sh_sort_u8 *s, size_t len)
{
        sh_sort_u8 out_a[512], out_r[512];
        if (len > sizeof(out_a))
                return;
        for (sh_sort_u64 how = 0; how < 8; how++) {
                memset(out_a, 0xcc, sizeof(out_a));
                memset(out_r, 0xdd, sizeof(out_r));
                sh_sort_u64 la = sh_sort_translate(out_a, s, len, how);
                sh_sort_u64 lr = ref_sh_sort_translate(out_r, s, len, how);
                if (la != lr || (la && memcmp(out_a, out_r, (size_t)la) != 0)) {
                        fprintf(stderr, "FAIL translate [%s] how=%llu len=%zu: asm=%llu ref=%llu\n",
                                tag, (unsigned long long)how, len,
                                (unsigned long long)la, (unsigned long long)lr);
                        failures++;
                }
        }
}

/* ── Exhaustive edge cases ── */

static void test_edge_cases(void)
{
        static const char *nums[] = {
                "", " ", "\t\n ", "-", "--", "+", "+0", "+7", "+123.45",
                "0", "-0", "0000", "-0000", "0.0", "-0.0", "0.000", "-0.000",
                ".", "-.", ".0", "-.0", ".000", "-.000",
                ".5", "-.5", "0.5", "-0.5", "0.500", "-0.500", "0.0001", "-0.0001",
                "1", "-1", "9", "-9", "10", "-10", "42", "-42",
                "   \t\n00042.10500xyz", "\t -000000012345678.901000",
                "1234567", "12345678", "123456789", "12345678901234567890",
                "-1234567", "-12345678", "-123456789", "-12345678901234567890",
                "99999999", "99999999.1", "99999999.000000001", "99999999.000000002",
                "100000000", "100000001", "100000000.000000001",
                "0K", "-0K", "0.000M", "1k", "1K", "-1k", "-1K",
                "2M", "-2M", "3G", "-3G", "4T", "-4T", "5P", "-5P",
                "6E", "-6E", "7Z", "-7Z", "8Y", "-8Y", "9R", "-9R", "10Q", "-10Q",
                "25.G", "-25.G", "0.5K", "-0.5K", "0.0001Q", "-0.0001Q",
                "1m", "1g", "1t", "1x", "K6", "abc", "12..34", "  \t\n -000.000001000"
        };
        size_t n = sizeof(nums) / sizeof(nums[0]);

        /* Huge 100-digit and 140-digit numbers */
        char huge1[160], huge2[160], huge3[160], nhuge1[160], nhuge2[160];
        memset(huge1, '9', 100); huge1[100] = 0;
        memset(huge2, '9', 100); huge2[99] = '8'; huge2[100] = 0;
        memset(huge3, '1', 140); huge3[140] = 0;
        nhuge1[0] = '-'; memcpy(nhuge1 + 1, huge1, 101);
        nhuge2[0] = '-'; memcpy(nhuge2 + 1, huge3, 141);

        for (size_t i = 0; i < n; i++) {
                check_num("edge", (const sh_sort_u8 *)nums[i], strlen(nums[i]));
                check_translate("edge", (const sh_sort_u8 *)nums[i], strlen(nums[i]));
        }
        check_num("huge1", (const sh_sort_u8 *)huge1, 100);
        check_num("huge2", (const sh_sort_u8 *)huge2, 100);
        check_num("huge3", (const sh_sort_u8 *)huge3, 140);
        check_num("nhuge1", (const sh_sort_u8 *)nhuge1, 101);
        check_num("nhuge2", (const sh_sort_u8 *)nhuge2, 141);

        /* Pairwise compare across all edge numbers */
        for (size_t i = 0; i < n; i++) {
                for (size_t j = 0; j < n; j++) {
                        check_pair("edge_pair",
                                   (const sh_sort_u8 *)nums[i], strlen(nums[i]),
                                   (const sh_sort_u8 *)nums[j], strlen(nums[j]));
                }
        }
        check_pair("huge12", (const sh_sort_u8 *)huge1, 100, (const sh_sort_u8 *)huge2, 100);
        check_pair("huge13", (const sh_sort_u8 *)huge1, 100, (const sh_sort_u8 *)huge3, 140);
        check_pair("nhuge12", (const sh_sort_u8 *)nhuge1, 101, (const sh_sort_u8 *)nhuge2, 141);

        /* All 256 byte values through translate and compare_bytes */
        sh_sort_u8 all256[256];
        for (int i = 0; i < 256; i++)
                all256[i] = (sh_sort_u8)i;
        check_translate("all256", all256, 256);

        /* Embedded NULs and filtered trailing junk */
        static const sh_sort_u8 nul1[] = {'a', 0, 'b', 'c'};
        static const sh_sort_u8 nul2[] = {'A', 0, 'B', 'c'};
        static const sh_sort_u8 nul3[] = {'a', 'b', 'c'};
        static const sh_sort_u8 junk1[] = "foo";
        static const sh_sort_u8 junk2[] = "foo!!!\x01\x7f";
        static const sh_sort_u8 junk3[] = "!!!\x01\x7f";
        check_pair("nul12", nul1, sizeof(nul1), nul2, sizeof(nul2));
        check_pair("nul13", nul1, sizeof(nul1), nul3, sizeof(nul3));
        check_pair("junk12", junk1, 3, junk2, 8);
        check_pair("junk21", junk2, 8, junk1, 3);
        check_pair("junk3_empty", junk3, 5, (const sh_sort_u8 *)"", 0);
        check_pair("empty_junk3", (const sh_sort_u8 *)"", 0, junk3, 5);

        /* NULL pointer safety */
        if (sh_sort_number_window(NULL, 0, NULL) != ((sh_sort_u64)0x80 << 56)) failures++;
        if (sh_sort_number_window(NULL, 10, NULL) != ((sh_sort_u64)0x80 << 56)) failures++;
        if (sh_sort_number_window((const sh_sort_u8 *)"42", 2, NULL) !=
            ref_sh_sort_number_window((const sh_sort_u8 *)"42", 2, NULL)) failures++;
        if (sh_sort_human_order(NULL, 0) != 0 || sh_sort_human_order(NULL, 5) != 0) failures++;
        sh_sort_number_parse(NULL, 5, NULL);
        struct sh_sort_number z;
        sh_sort_number_parse(NULL, 5, &z);
        if (z.rank != 0 || z.places != 0 || z.integer.packed != 0) failures++;
        if (sh_sort_compare_parsed(NULL, NULL) != 0) failures++;
        if (sh_sort_compare_parsed(NULL, &z) != -1) failures++;
        if (sh_sort_compare_parsed(&z, NULL) != 1) failures++;
        if (sh_sort_translate(NULL, all256, 10, 7) != 0) failures++;
        if (sh_sort_translate(all256, NULL, 10, 7) != 0) failures++;
        for (sh_sort_u64 how = 0; how < 8; how++) {
                if (sh_sort_compare_bytes(NULL, 5, NULL, 5, how) != 0) failures++;
                if (sh_sort_compare_bytes(NULL, 5, (const sh_sort_u8 *)"a", 1, how) != -1) failures++;
                if (sh_sort_compare_bytes((const sh_sort_u8 *)"a", 1, NULL, 5, how) != 1) failures++;
        }
}

/* ── Randomized differential fuzzing ── */

static size_t gen_random_num(sh_sort_u8 *buf, size_t cap)
{
        static const char suffixes[] = "kKMGTPEZYRQmgx.09";
        size_t p = 0;
        uint64_t r = xorshift64();
        size_t nblanks = r & 3; r >>= 2;
        for (size_t i = 0; i < nblanks && p < cap; i++)
                buf[p++] = (r & 1) ? ' ' : ((r & 2) ? '\t' : '\n');
        if ((r & 3) == 1 && p < cap) buf[p++] = '-';
        else if ((r & 7) == 7 && p < cap) buf[p++] = '+';
        r >>= 3;
        size_t nzeros = r & 3; r >>= 2;
        for (size_t i = 0; i < nzeros && p < cap; i++)
                buf[p++] = '0';
        size_t ndig = (xorshift64() % 20);
        if ((xorshift64() & 63) == 0) ndig = 130;
        for (size_t i = 0; i < ndig && p < cap; i++)
                buf[p++] = (sh_sort_u8)('0' + (xorshift64() % 10));
        if ((xorshift64() & 1) && p < cap) {
                buf[p++] = '.';
                size_t nfrac = xorshift64() % 14;
                for (size_t i = 0; i < nfrac && p < cap; i++)
                        buf[p++] = (sh_sort_u8)('0' + (xorshift64() % 10));
                size_t tz = xorshift64() & 3;
                for (size_t i = 0; i < tz && p < cap; i++)
                        buf[p++] = '0';
        }
        if ((xorshift64() & 1) && p < cap)
                buf[p++] = (sh_sort_u8)suffixes[xorshift64() % (sizeof(suffixes) - 1)];
        return p;
}

static void test_fuzz(void)
{
        sh_sort_u8 a[256], b[256];
        for (int iter = 0; iter < 25000; iter++) {
                size_t la = gen_random_num(a, sizeof(a));
                size_t lb = gen_random_num(b, sizeof(b));
                check_num("fuzz_num", a, la);
                check_pair("fuzz_pair", a, la, b, lb);
        }
        for (int iter = 0; iter < 25000; iter++) {
                size_t la = xorshift64() % 80;
                size_t lb = xorshift64() % 80;
                for (size_t i = 0; i < la; i++)
                        a[i] = (sh_sort_u8)(xorshift64() & 0xff);
                /* Half the time share a common prefix to exercise deep compare paths */
                if ((iter & 1) && la > 0) {
                        size_t pref = xorshift64() % (la + 1);
                        if (pref > lb) pref = lb;
                        memcpy(b, a, pref);
                        for (size_t i = pref; i < lb; i++)
                                b[i] = (sh_sort_u8)(xorshift64() & 0xff);
                        /* Optionally toggle case on some prefix bytes */
                        if (iter & 2) {
                                for (size_t i = 0; i < pref; i++) {
                                        if ((b[i] >= 'a' && b[i] <= 'z') || (b[i] >= 'A' && b[i] <= 'Z'))
                                                b[i] ^= 32;
                                }
                        }
                } else {
                        for (size_t i = 0; i < lb; i++)
                                b[i] = (sh_sort_u8)(xorshift64() & 0xff);
                }
                check_translate("fuzz_bytes", a, la);
                check_pair("fuzz_bytes", a, la, b, lb);
        }
}

/* ── mmap guard-page security tests (PROT_NONE before AND after) ── */

static void test_guard_pages(void)
{
        long ps = sysconf(_SC_PAGESIZE);
        if (ps <= 0) ps = 4096;
        size_t page = (size_t)ps;
        /* [PROT_NONE][RW][PROT_NONE] */
        sh_sort_u8 *map = mmap(NULL, 3 * page, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANON, -1, 0);
        if (map == MAP_FAILED) {
                perror("mmap");
                failures++;
                return;
        }
        mprotect(map, page, PROT_NONE);
        mprotect(map + 2 * page, page, PROT_NONE);
        sh_sort_u8 *rw = map + page;

        sh_sort_u8 out_buf[256];
        static const size_t lens[] = {0, 1, 2, 3, 4, 7, 8, 9, 15, 16, 17, 31, 32, 63, 64, 150};
        for (size_t k = 0; k < sizeof(lens) / sizeof(lens[0]); k++) {
                size_t len = lens[k];
                for (int side = 0; side < 2; side++) {
                        /* side 0: flush at start of RW page (immediately after PROT_NONE)
                           side 1: flush at end of RW page (immediately before PROT_NONE) */
                        sh_sort_u8 *p = side ? (rw + page - len) : rw;
                        for (size_t i = 0; i < len; i++)
                                p[i] = (sh_sort_u8)('0' + ((i + 1) % 10));
                        if (len > 3) p[0] = '-';
                        if (len > 6) p[len / 2] = '.';
                        if (len > 1) p[len - 1] = 'G';

                        check_num("guard", p, len);
                        check_pair("guard_self", p, len, p, len);
                        for (sh_sort_u64 how = 0; how < 8; how++) {
                                sh_sort_u64 r1 = sh_sort_translate(out_buf, p, len, how);
                                sh_sort_u64 r2 = ref_sh_sort_translate(out_buf, p, len, how);
                                if (r1 != r2) failures++;
                        }
                }
        }
        munmap(map, 3 * page);
}

/* ── Throughput benchmark ── */

static volatile uint64_t bench_sink;

static void bench(void)
{
        enum { N = 4096 };
        static sh_sort_u8 keys[N][48];
        static sh_sort_u8 keys_copy[N][48];
        static size_t lens[N];
        static struct sh_sort_number parsed[N];
        static sh_sort_u8 scratch[64];

        rng_state = 0x123456789abcdef0ULL;
        for (int i = 0; i < N; i++) {
                lens[i] = gen_random_num(keys[i], 40);
                memcpy(keys_copy[i], keys[i], lens[i]);
                sh_sort_number_parse(keys[i], lens[i], &parsed[i]);
        }

        const int rounds = 200;
        uint64_t ops = (uint64_t)rounds * N;
        uint64_t t0, t1, s = 0;
        double ns_asm, ns_ref;

        /* 1. number_window */
        t0 = now_ns();
        for (int r = 0; r < rounds; r++)
                for (int i = 0; i < N; i++) {
                        sh_sort_u8 ex = 0;
                        s += sh_sort_number_window(keys[i], lens[i], &ex) ^ ex;
                }
        t1 = now_ns(); ns_asm = (double)(t1 - t0) / (double)ops;

        t0 = now_ns();
        for (int r = 0; r < rounds; r++)
                for (int i = 0; i < N; i++) {
                        sh_sort_u8 ex = 0;
                        s += ref_sh_sort_number_window(keys[i], lens[i], &ex) ^ ex;
                }
        t1 = now_ns(); ns_ref = (double)(t1 - t0) / (double)ops;
        printf("  number_window  : asm=%5.2f ns/op  ref=%5.2f ns/op  speedup=%.2fx\n",
               ns_asm, ns_ref, ns_ref / ns_asm);

        /* 2. human_order */
        t0 = now_ns();
        for (int r = 0; r < rounds; r++)
                for (int i = 0; i < N; i++)
                        s += (uint64_t)sh_sort_human_order(keys[i], lens[i]);
        t1 = now_ns(); ns_asm = (double)(t1 - t0) / (double)ops;

        t0 = now_ns();
        for (int r = 0; r < rounds; r++)
                for (int i = 0; i < N; i++)
                        s += (uint64_t)ref_sh_sort_human_order(keys[i], lens[i]);
        t1 = now_ns(); ns_ref = (double)(t1 - t0) / (double)ops;
        printf("  human_order    : asm=%5.2f ns/op  ref=%5.2f ns/op  speedup=%.2fx\n",
               ns_asm, ns_ref, ns_ref / ns_asm);

        /* 3. number_parse */
        t0 = now_ns();
        for (int r = 0; r < rounds; r++)
                for (int i = 0; i < N; i++) {
                        struct sh_sort_number num;
                        sh_sort_number_parse(keys[i], lens[i], &num);
                        s += (uint64_t)num.rank ^ num.integer.packed;
                }
        t1 = now_ns(); ns_asm = (double)(t1 - t0) / (double)ops;

        t0 = now_ns();
        for (int r = 0; r < rounds; r++)
                for (int i = 0; i < N; i++) {
                        struct sh_sort_number num;
                        ref_sh_sort_number_parse(keys[i], lens[i], &num);
                        s += (uint64_t)num.rank ^ num.integer.packed;
                }
        t1 = now_ns(); ns_ref = (double)(t1 - t0) / (double)ops;
        printf("  number_parse   : asm=%5.2f ns/op  ref=%5.2f ns/op  speedup=%.2fx\n",
               ns_asm, ns_ref, ns_ref / ns_asm);

        /* 4. compare_parsed */
        t0 = now_ns();
        for (int r = 0; r < rounds; r++)
                for (int i = 0; i < N; i++)
                        s += (uint64_t)sh_sort_compare_parsed(&parsed[i], &parsed[(i + 1) & (N - 1)]);
        t1 = now_ns(); ns_asm = (double)(t1 - t0) / (double)ops;

        t0 = now_ns();
        for (int r = 0; r < rounds; r++)
                for (int i = 0; i < N; i++)
                        s += (uint64_t)ref_sh_sort_compare_parsed(&parsed[i], &parsed[(i + 1) & (N - 1)]);
        t1 = now_ns(); ns_ref = (double)(t1 - t0) / (double)ops;
        printf("  compare_parsed : asm=%5.2f ns/op  ref=%5.2f ns/op  speedup=%.2fx\n",
               ns_asm, ns_ref, ns_ref / ns_asm);

        /* 5. translate (-d -i -f) */
        t0 = now_ns();
        for (int r = 0; r < rounds; r++)
                for (int i = 0; i < N; i++)
                        s += sh_sort_translate(scratch, keys[i], lens[i], 7);
        t1 = now_ns(); ns_asm = (double)(t1 - t0) / (double)ops;

        t0 = now_ns();
        for (int r = 0; r < rounds; r++)
                for (int i = 0; i < N; i++)
                        s += ref_sh_sort_translate(scratch, keys[i], lens[i], 7);
        t1 = now_ns(); ns_ref = (double)(t1 - t0) / (double)ops;
        printf("  translate(-dif): asm=%5.2f ns/op  ref=%5.2f ns/op  speedup=%.2fx\n",
               ns_asm, ns_ref, ns_ref / ns_asm);

        /* 6. compare_bytes (plain & -f & -df) */
        static const sh_sort_u64 modes[] = {0, SH_SORT_FOLD, SH_SORT_DICTIONARY | SH_SORT_FOLD};
        static const char *mnames[] = {"cmp_bytes(raw) ", "cmp_bytes(-f)  ", "cmp_bytes(-df) "};
        for (int m = 0; m < 3; m++) {
                t0 = now_ns();
                for (int r = 0; r < rounds; r++)
                        for (int i = 0; i < N; i++)
                                s += (uint64_t)sh_sort_compare_bytes(keys[i], lens[i], keys_copy[i], lens[i], modes[m]);
                t1 = now_ns(); ns_asm = (double)(t1 - t0) / (double)ops;

                t0 = now_ns();
                for (int r = 0; r < rounds; r++)
                        for (int i = 0; i < N; i++)
                                s += (uint64_t)ref_sh_sort_compare_bytes(keys[i], lens[i], keys_copy[i], lens[i], modes[m]);
                t1 = now_ns(); ns_ref = (double)(t1 - t0) / (double)ops;
                printf("  %s: asm=%5.2f ns/op  ref=%5.2f ns/op  speedup=%.2fx\n",
                       mnames[m], ns_asm, ns_ref, ns_ref / ns_asm);
        }

        bench_sink = s;
}

int main(void)
{
        test_edge_cases();
        test_fuzz();
        test_guard_pages();
        if (failures) {
                fprintf(stderr, "FAILED: %d mismatches\n", failures);
                return 1;
        }
        printf("PASS: all edge cases, 50,000+ fuzz cases, and mmap guard-page tests matched 100%%\n");
        bench();
        return 0;
}
